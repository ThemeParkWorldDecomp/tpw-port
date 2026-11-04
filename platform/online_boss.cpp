// online_boss.cpp: the online server client (include/platform/online_boss.h), the Mac's "online symbols shared" library translated:
// Client (the framing, the send queue, the message records and the reading of replies), the owners' internalCallback2 (Auth, News,
// City with its ProcessQueryReply and RowStore, Upload, Chat) and the requests of AuthClient, NewsClient, CityClient, UploadClient and
// ChatClient.
// The sockets are platform/online_boss_socket.cpp's; the differences from the Mac's library are listed in the header.
#include "platform/online_boss.h"
#include "platform/online_ftp.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "FS_Online.h"
#include "FS_OnlineList.h"
#include "online_lists.h"
#include "platform.h"

namespace tpw::online::boss {

namespace {

void Put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

void Put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }

uint32_t Get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

// Wide text on the wire is UTF-16LE, as the host's char16_t is in memory on every host the port builds for: no swap.
void PutWide(uint8_t* p, size_t units, const char16_t* text) {
    std::vector<char16_t> field(units, 0);
    CopyWide(field.data(), units, text);
    for (size_t i = 0; i < units; ++i) {
        p[2 * i] = static_cast<uint8_t>(field[i]);
        p[2 * i + 1] = static_cast<uint8_t>(field[i] >> 8);
    }
}

// The wide text at `offset` of a reply, at most `units` units, up to its NUL.
std::u16string WideAt(const uint8_t* bytes, size_t size, size_t offset, size_t units) {
    std::u16string out;
    for (size_t i = 0; i < units && offset + 2 * i + 1 < size; ++i) {
        const char16_t unit = static_cast<char16_t>(bytes[offset + 2 * i] | bytes[offset + 2 * i + 1] << 8);
        if (unit == 0) break;
        out += unit;
    }
    return out;
}

}  // namespace

// ---- The framing (Client::CreateHeader, ProcessReadPackets) -----------------------------------------------------------------

void PutHeader(uint8_t* out, const Header& h) {
    out[0] = h.magic;
    out[1] = h.from;
    Put16(out + 2, h.type);
    Put16(out + 4, h.param);
    Put16(out + 6, h.status);
    Put32(out + 8, h.length);
    Put32(out + 0xc, h.context);
    Put32(out + 0x10, h.seq);
}

Header GetHeader(const uint8_t* in) {
    Header h;
    h.magic = in[0];
    h.from = in[1];
    h.type = Get16(in + 2);
    h.param = Get16(in + 4);
    h.status = Get16(in + 6);
    h.length = Get32(in + 8);
    h.context = Get32(in + 0xc);
    h.seq = Get32(in + 0x10);
    return h;
}

// ---- The text helpers ---------------------------------------------------------------------------------------------------------

std::string GuidText(const uint8_t b[16]) {
    char out[0x30] = {};
    std::snprintf(out, sizeof out, "{%02X%02X%02X%02X-%2X%2X-%2X%2X-%02X%02X-%02X%02X%02X%02X%02X%02X}", b[0], b[1], b[2], b[3],
                  b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return out;
}

std::string BossGuidText(const uint8_t b[16]) {
    char out[0x30] = {};
    std::snprintf(out, sizeof out, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", b[3], b[2], b[1], b[0],
                  b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return out;
}

std::string HexText(const char16_t* text, size_t most) {
    std::string out = "0x";
    for (size_t i = 0; text != nullptr && (most == 0 || i < most) && text[i] != 0; ++i) {
        char unit[5];
        std::snprintf(unit, sizeof unit, "%02X%02X", text[i] & 0xff, text[i] >> 8);
        out += unit;
    }
    return out;
}

void CopyWide(char16_t* out, size_t units, const char16_t* text) {
    if (units == 0) return;
    size_t i = 0;
    for (; text != nullptr && i + 1 < units && text[i] != 0; ++i) out[i] = text[i];
    for (; i < units; ++i) out[i] = 0;
}

// ---- RowStore: the City replies' rowset ---------------------------------------------------------------------------------------

namespace {

enum CellType { kI4 = 1, kF4, kSz, kUz, kBg, kDt, kRm };

// The table at 0x10013f88 (the codes RowStore::Initialise knows; an unknown code is skipped).
int CodeType(const char* code) {
    static const struct {
        char name[3];
        int type;
    } kCodes[] = {{"i4", kI4}, {"f4", kF4}, {"sz", kSz}, {"uz", kUz}, {"bg", kBg}, {"dt", kDt}, {"rm", kRm}};
    for (const auto& c : kCodes)
        if (code[0] == c.name[0] && code[1] == c.name[1]) return c.type;
    return 0;
}

size_t FormatStride(const char* format) { return std::strchr(format, ',') != nullptr ? 3 : 2; }

// RowStore::Initialise: the cell types of a format, in order.
std::vector<int> FormatTypes(const char* format) {
    std::vector<int> types;
    const size_t stride = FormatStride(format);
    for (const char* p = format; std::strlen(p) >= 2; p += stride) {
        if (const int type = CodeType(p)) types.push_back(type);
        if (p[2] == 0) break;
    }
    return types;
}

// RowStore::VerifyFormat. Where the Mac's walked past the end of the shorter string, the answer is the one its next step gave
// for a received format that ends (no match) or an expected one that ends (a match).
bool VerifyFormat(const char* expected, const char* received) {
    const size_t se = FormatStride(expected), sr = FormatStride(received);
    for (;;) {
        if (std::strlen(expected) < 2) return true;
        if (std::strlen(received) < 2 || expected[0] != received[0] || expected[1] != received[1]) return false;
        if (expected[2] == 0) return true;
        if (received[2] == 0) return false;
        expected += se;
        received += sr;
    }
}

// One cell of the store: the bytes of its column's value in the row last read. Zeroed when the store is made only, as the Mac's.
struct Cell {
    int type = 0;
    uint32_t length = 0;
    std::array<uint8_t, 0x202> data{};

    int32_t I4() const { return static_cast<int32_t>(Get32(data.data())); }
    uint32_t Bits() const { return Get32(data.data()); }
    std::u16string Wide() const { return WideAt(data.data(), data.size(), 0, data.size() / 2); }
};

// RowStore::StreamOutRow: the row at `p` read into the cells; the next row's start, or nullptr when the data has no whole row
// there. Every length is checked against the data's end and the cell's room (the Mac's checked neither).
const uint8_t* StreamOutRow(std::vector<Cell>& cells, const uint8_t* p, const uint8_t* end) {
    if (p == nullptr || p >= end) return nullptr;
    for (Cell& cell : cells) {
        const size_t left = static_cast<size_t>(end - p);
        switch (cell.type) {
        case kI4:
        case kF4:
            if (left < 4) return nullptr;
            std::memcpy(cell.data.data(), p, 4);
            cell.length = 4;
            p += 4;
            break;
        case kSz: {
            const auto* nul = static_cast<const uint8_t*>(std::memchr(p, 0, std::min(left, cell.data.size())));
            if (nul == nullptr) return nullptr;
            const size_t size = static_cast<size_t>(nul - p) + 1;
            std::memcpy(cell.data.data(), p, size);
            cell.length = static_cast<uint32_t>(size);
            p += size;
            break;
        }
        case kUz: {
            if (left < 2) return nullptr;
            const size_t size = Get16(p);
            if (size > left - 2 || size + 2 > cell.data.size()) return nullptr;
            std::memcpy(cell.data.data(), p + 2, size);
            cell.data[size] = 0;
            cell.data[size + 1] = 0;
            cell.length = static_cast<uint32_t>(size);
            p += size + 2;
            break;
        }
        case kBg:
        case kRm: {
            if (left < 2) return nullptr;
            const size_t size = Get16(p);
            if (size > left - 2 || size > cell.data.size()) return nullptr;
            std::memcpy(cell.data.data(), p + 2, size);
            cell.length = static_cast<uint32_t>(size);
            p += size + 2;
            break;
        }
        case kDt:
            if (left < 6) return nullptr;
            std::memcpy(cell.data.data(), p, 6);
            cell.length = 6;
            p += 6;
            break;
        }
    }
    return p > end ? nullptr : p;
}

using Writer = tpw::online::lists::InfoWriter;

// The infos of each list, column k of the row at the offsets the Mac's City::ProcessQueryReply wrote it.
void CityRow(Writer& w, size_t i, const std::vector<Cell>& c) {
    w.Word(i, 0x00, static_cast<uint32_t>(c[0].I4()));
    w.Wide(i, 0x04, c[1].Wide());
    w.Wide(i, 0x08, c[2].Wide());
    w.Word(i, 0x0c, c[3].Bits());
    w.Word(i, 0x10, c[4].Bits());
    w.Word(i, 0x14, c[5].Bits());
    w.Word(i, 0x20, static_cast<uint32_t>(c[6].I4()));
    w.Word(i, 0x24, static_cast<uint32_t>(c[7].I4()));
    w.Word(i, 0x28, static_cast<uint32_t>(c[8].I4()));
    w.Wide(i, 0x1c, c[9].Wide());
    w.Word(i, 0x18, static_cast<uint32_t>(c[10].I4()));
    w.Word(i, 0x2c, static_cast<uint32_t>(c[11].I4()));
}

void ParkRow(Writer& w, size_t i, const std::vector<Cell>& c) {
    w.Wide(i, 0x0c, c[0].Wide());
    w.Wide(i, 0x4c, c[1].Wide());
    w.Word(i, 0x00, static_cast<uint32_t>(c[2].I4()));
    w.Narrow(i, 0x44, BossGuidText(c[3].data.data()));
    w.Wide(i, 0x04, c[4].Wide());
    w.Wide(i, 0x08, c[5].Wide());
    w.Word(i, 0x18, static_cast<uint32_t>(c[6].I4()));
    w.Word(i, 0x1c, static_cast<uint32_t>(c[7].I4()));
    w.Word(i, 0x14, static_cast<uint32_t>(c[8].I4()));
    w.Word(i, 0x40, (c[9].I4() == 1 || c[9].I4() == 3) ? 1u : 0u);
    w.Word(i, 0x48, static_cast<uint32_t>(c[10].I4()));
    // The ride bits (+0x20..+0x3f): the '0'/'1' text right-aligned in 256 bits, its last character the last bit.
    uint8_t* bits = w.Record(i) + 0x20;
    std::memset(bits, 0, 0x20);
    const int32_t length = static_cast<int32_t>(c[11].length);
    for (int32_t k = 0; length > 1 && k < length - 1; ++k) {
        if (c[11].data[static_cast<size_t>(k)] == '0') continue;
        const int32_t u = 0x100 - (length - 1) + k;
        if (u < 0 || u > 0xff) continue;   // a text over 256 characters: its first ones have no bit (the Mac's wrote before the field)
        bits[u >> 3] = static_cast<uint8_t>(bits[u >> 3] | 1 << (7 - (u & 7)));
    }
    const uint8_t* date = c[12].data.data();
    w.Half(i, 0x50, Get16(date + 4));
    w.Half(i, 0x52, Get16(date + 2));
    w.Half(i, 0x54, Get16(date + 0));
    w.Word(i, 0x10, static_cast<uint32_t>(c[13].I4()));
    w.Word(i, 0x56, static_cast<uint32_t>(c[14].I4()));
    w.Word(i, 0x5a, static_cast<uint32_t>(c[15].I4()));
    w.Word(i, 0x5e, c[16].I4() == 0 ? 0x7fffffffu : static_cast<uint32_t>(c[16].I4()));
    w.Word(i, 0x62, static_cast<uint32_t>(c[17].I4()));
}

void ServerRow(Writer& w, size_t i, const std::vector<Cell>& c) {
    w.Word(i, 0x00, static_cast<uint32_t>(c[0].I4()));
    w.Wide(i, 0x08, c[1].Wide());
    w.Word(i, 0x04, static_cast<uint32_t>(c[2].I4()));
    w.Wide(i, 0x0c, c[3].Wide());
    w.Word(i, 0x10, static_cast<uint32_t>(c[4].I4()));
    w.Half(i, 0x14, static_cast<uint16_t>(c[5].I4()));
    w.Wide(i, 0x16, c[6].Wide());
    w.Wide(i, 0x1a, c[7].Wide());
    w.Word(i, 0x1e, static_cast<uint32_t>(c[8].I4()));
    w.Wide(i, 0x22, c[9].Wide());
}

void ThemeRow(Writer& w, size_t i, const std::vector<Cell>& c) {
    w.Word(i, 0x00, static_cast<uint32_t>(c[0].I4()));
    w.Wide(i, 0x04, c[1].Wide());
    w.Wide(i, 0x08, c[2].Wide());
    w.Wide(i, 0x0c, c[3].Wide());
}

void RideRow(Writer& w, size_t i, const std::vector<Cell>& c) {
    w.Word(i, 0x00, static_cast<uint32_t>(c[0].I4()));
    w.Wide(i, 0x04, c[1].Wide());
    w.Wide(i, 0x08, c[2].Wide());
    w.Bytes(i, 0x0c, c[3].data.data(), 16);
    w.Word(i, 0x1c, static_cast<uint32_t>(c[4].I4()));
    w.Word(i, 0x20, static_cast<uint32_t>(c[5].I4()));
    w.Wide(i, 0x24, c[6].Wide());
    w.Word(i, 0x28, static_cast<uint32_t>(c[7].I4()));
    w.Word(i, 0x2c, static_cast<uint32_t>(c[8].I4()));
    w.Word(i, 0x30, static_cast<uint32_t>(c[9].I4()));
}

void LanguageRow(Writer& w, size_t i, const std::vector<Cell>& c) {
    w.Wide(i, 0x00, c[0].Wide());
    w.Wide(i, 0x04, c[1].Wide());
}

struct LanguageInfo {
    uint8_t mF00[8];
};

// What the City answers for each sub-id: the callback's code, the record and the format the list must have.
struct Query {
    uint32_t sub;
    int32_t code;
    size_t stride;
    const char* format;
    size_t columns;
    void (*row)(Writer&, size_t, const std::vector<Cell>&);
    void* (*give)(Writer&, int32_t, int32_t);
};

template <class INFO>
void* GiveList(Writer& w, int32_t count, int32_t extra) {
    return w.Give<INFO>(count, extra);
}

const char kParkFormat[] = "uz,uz,i4,bg,uz,uz,i4,i4,i4,i4,i4,sz,dt,i4,i4,i4,i4,i4";

const Query kQueries[] = {
    {1, 2, 0x30, "i4,uz,uz,f4,f4,f4,i4,i4,i4,uz,i4,i4", 12, CityRow, GiveList<ccCityInfo>},
    {2, 3, 0x66, kParkFormat, 18, ParkRow, GiveList<ccParkInfo>},
    {3, 3, 0x66, kParkFormat, 18, ParkRow, GiveList<ccParkInfo>},
    {4, 3, 0x66, kParkFormat, 18, ParkRow, GiveList<ccParkInfo>},
    {5, 3, 0x66, kParkFormat, 18, ParkRow, GiveList<ccParkInfo>},
    {6, 3, 0x66, kParkFormat, 18, ParkRow, GiveList<ccParkInfo>},
    {10, 3, 0x66, kParkFormat, 18, ParkRow, GiveList<ccParkInfo>},
    {11, 3, 0x66, kParkFormat, 18, ParkRow, GiveList<ccParkInfo>},
    {7, 4, 0x26, "i4,uz,i4,uz,i4,i4,uz,uz,i4,uz", 10, ServerRow, GiveList<ccServerInfo>},
    {8, 5, 0x10, "i4,uz,uz,uz", 4, ThemeRow, GiveList<ccThemeInfo>},
    {9, 6, 0x34, "i4,uz,uz,bg,i4,i4,uz,i4,i4,i4", 10, RideRow, GiveList<ccRideInfo>},
    {12, 9, 8, "uz,uz", 2, LanguageRow, GiveList<LanguageInfo>},
};

// A callback to give: its code and block (none when `give` is false).
struct Answer {
    bool give = false;
    int32_t code = 0;
    void* block = nullptr;
};

Answer Give(int32_t code, void* block = nullptr) { return Answer{true, code, block}; }

// City::ProcessQueryReply: the reply's status, then the rowset (CreateFromStream) as the sub-id's list.
Answer CityReply(uint16_t status, const std::vector<uint8_t>& body) {
    switch (status) {
    case 1: return Give(-10);
    case 2: return Give(-1);
    case 4: return Give(-2);
    case 5: return Give(7);
    case 6: return Give(8);
    default: break;
    }
    if (body.size() < 4) return Give(-1);   // the Mac's read the sub-id from a reply too short to hold one
    const uint32_t sub = Get32(body.data());

    // CreateFromStream: the count, the data's length, the extra word, the format and the data, each kept inside the reply.
    uint32_t count = 0;
    int32_t extra = 0;
    std::string format;
    const uint8_t* data = nullptr;
    const uint8_t* end = nullptr;
    if (status != 3 && body.size() >= 0x10) {
        const uint8_t* stream = body.data() + 4;
        const uint8_t* bodyEnd = body.data() + body.size();
        const auto* nul = static_cast<const uint8_t*>(std::memchr(stream + 0xc, 0, static_cast<size_t>(bodyEnd - (stream + 0xc))));
        if (nul != nullptr) {
            count = Get32(stream);
            extra = static_cast<int32_t>(Get32(stream + 8));
            format.assign(reinterpret_cast<const char*>(stream + 0xc), static_cast<size_t>(nul - (stream + 0xc)));
            data = nul + 1;
            end = data + std::min<size_t>(Get32(stream + 4), static_cast<size_t>(bodyEnd - data));
        }
    }

    if (sub == 100 || sub == 101) return Give(count == 0 ? -1 : 0);
    const Query* query = nullptr;
    for (const Query& q : kQueries)
        if (q.sub == sub) query = &q;
    if (query == nullptr) return Answer{};
    if (count == 0 || !VerifyFormat(query->format, format.c_str())) return Give(query->code);

    std::vector<Cell> cells;
    for (int type : FormatTypes(format.c_str())) cells.push_back(Cell{type, 0, {}});
    if (cells.size() < query->columns) return Give(query->code);
    // The rows the data holds, at most `count` (the Mac's wrote every row the data held into `count` records), then the list.
    std::vector<Cell> scratch = cells;
    size_t rows = 0;
    for (const uint8_t* p = data; rows < count && (p = StreamOutRow(scratch, p, end)) != nullptr;) ++rows;
    Writer w(rows, query->stride);
    const uint8_t* p = data;
    for (size_t i = 0; i < rows; ++i) {
        p = StreamOutRow(cells, p, end);
        query->row(w, i, cells);
    }
    return Give(query->code, query->give(w, static_cast<int32_t>(rows), extra));
}

// ---- The connection: Client ---------------------------------------------------------------------------------------------------

// A message's record (Client's message infos): 2 queued, 3 being sent, 4 sent, 5 its reply being read, 6 answered.
struct Info {
    uint32_t seq = 0;
    int32_t state = 2;
    int32_t done = 0;
    int32_t total = 0;
};

struct Reply {
    uint16_t type = 0;
    uint16_t status = 0;
    uint32_t seq = 0;
    std::vector<uint8_t> body;
    bool unreachable = false;   // "disconnected" before the server's first answer (TakeUnreachable)
};

// What the game's thread and the connection's thread share, under `lock`.
struct Line {
    std::mutex lock;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> queue;   // the messages to send: sequence word, header and payload
    std::vector<Info> infos;
    std::deque<Reply> replies;                                      // read, not yet given
    std::vector<std::string> notes;                                 // log lines, written on the game's thread
    std::atomic<bool> stopping{false};
    std::atomic<uint32_t> sending{0};
    std::atomic<uint32_t> receiving{0};
    std::unique_ptr<Transport> transport;
    std::string host;
    uint16_t port = 0;
    bool answered = false;   // a reply came: an end after it is a lost connection, not a server that cannot be reached
};

Info* FindInfo(Line& line, uint32_t seq) {
    for (Info& info : line.infos)
        if (info.seq == seq) return &info;
    return nullptr;
}

// The connection is gone: the "disconnected" reply (type 3), and no message is out any more. Called with the lock held.
void Ended(Line& line, const std::string& why) {
    line.notes.push_back("online: the connection to " + line.host + ":" + std::to_string(line.port) + " ended: " + why);
    line.replies.push_back(Reply{3, 0, 0, {}, !line.answered});
    line.infos.clear();
    line.receiving = 0;
}

void Disconnect(Line& line, const std::string& why) {
    std::lock_guard<std::mutex> hold(line.lock);
    Ended(line, why);
}

struct Reading {
    uint8_t header[kHeader] = {};
    size_t have = 0;
    bool inBody = false;
    bool push = false;
    Header h;
    std::vector<uint8_t> body;
    size_t got = 0;
};

// Client::ProcessReadPackets: one read, into the header or the body.
bool ReadSome(Line& line, Transport& t, Reading& r) {
    uint8_t* into = r.inBody ? r.body.data() + r.got : r.header + r.have;
    const size_t want = r.inBody ? r.body.size() - r.got : kHeader - r.have;
    const long n = t.Receive(into, want);
    if (n == Transport::kWouldBlock) return true;
    if (n <= 0) {
        Disconnect(line, n == 0 ? "closed by the server" : "a read failed");
        return false;
    }
    if (!r.inBody) {
        r.have += static_cast<size_t>(n);
        if (r.have < kHeader) return true;
        r.have = 0;
        r.h = GetHeader(r.header);
        if (r.h.magic != 'B' || r.h.from != 's') {
            Disconnect(line, "a reply that does not start \"Bs\"");
            return false;
        }
        if (r.h.length > kMaxReply) {
            Disconnect(line, "a reply of " + std::to_string(r.h.length) + " bytes");
            return false;
        }
        r.push = r.h.type == 0x12e;
        std::lock_guard<std::mutex> hold(line.lock);
        if (!r.push) {
            while (!line.infos.empty() && line.infos.front().state == 6) line.infos.erase(line.infos.begin());
            if (line.infos.empty() || line.infos.front().seq != r.h.seq) {
                Ended(line, "a reply to no request (" + std::to_string(r.h.seq) + ")");
                return false;
            }
            line.receiving = r.h.seq;
            line.infos.front().total = static_cast<int32_t>(r.h.length);
            line.infos.front().done = 0;
        }
        if (r.h.length == 0) {
            line.replies.push_back(Reply{r.h.type, r.h.status, r.h.seq, {}});
            line.answered = true;
            line.receiving = 0;
            if (!r.push) line.infos.front().state = 6;
            return true;
        }
        r.inBody = true;
        r.body.assign(r.h.length, 0);
        r.got = 0;
        return true;
    }
    r.got += static_cast<size_t>(n);
    std::lock_guard<std::mutex> hold(line.lock);
    Info* front = line.infos.empty() ? nullptr : &line.infos.front();
    if (!r.push && front != nullptr && front->seq == line.receiving) {
        front->state = 5;
        front->done = static_cast<int32_t>(r.got);
    }
    if (r.got == r.body.size()) {
        line.replies.push_back(Reply{r.h.type, r.h.status, r.h.seq, std::move(r.body)});
        line.answered = true;
        r.body.clear();
        r.inBody = false;
        line.receiving = 0;
        if (!r.push && front != nullptr) front->state = 6;
    }
    return true;
}

struct Sending {
    bool active = false;
    uint32_t seq = 0;
    std::vector<uint8_t> bytes;
    size_t sent = 0;
};

// commsThread's write half: the message at the queue's head, in pieces of at most 0x2000 bytes (UploadFile's chunk).
bool WriteSome(Line& line, Transport& t, Sending& s) {
    if (!s.active) {
        std::lock_guard<std::mutex> hold(line.lock);
        if (line.queue.empty()) return true;
        s.seq = line.queue.front().first;
        s.bytes = std::move(line.queue.front().second);
        line.queue.pop_front();
        s.sent = 0;
        s.active = true;
        line.sending = s.seq;
        if (Info* info = FindInfo(line, s.seq)) {
            info->state = 3;
            info->done = 0;
            info->total = static_cast<int32_t>(s.bytes.size());
        }
    }
    const long n = t.Send(s.bytes.data() + s.sent, std::min<size_t>(0x2000, s.bytes.size() - s.sent));
    if (n == Transport::kWouldBlock) return true;
    if (n < 0) {
        Disconnect(line, "a write failed");
        return false;
    }
    s.sent += static_cast<size_t>(n);
    std::lock_guard<std::mutex> hold(line.lock);
    Info* info = FindInfo(line, s.seq);
    if (info != nullptr && info->state == 3) info->done = static_cast<int32_t>(s.sent);
    if (s.sent == s.bytes.size()) {
        if (info != nullptr && info->state == 3) info->state = 4;
        line.sending = 0;
        s.active = false;
        s.bytes.clear();
    }
    return true;
}

// commsThread: connects, then waits for the connection to be readable (or writable while a message is to be sent), 100 ms at a
// time, until the connection ends or the owner lets it go. The line is the thread's own share: the owner may be gone by then.
void Run(std::shared_ptr<Line> line) {
    Transport& t = *line->transport;
    if (!t.Open(line->host, line->port, line->stopping)) {
        const std::string why = t.Failure();
        if (!line->stopping) Disconnect(*line, "the server could not be reached" + (why.empty() ? "" : ": " + why));
        t.Close();
        return;
    }
    {
        std::lock_guard<std::mutex> hold(line->lock);
        line->notes.push_back("online: connected to " + line->host + ":" + std::to_string(line->port));
    }
    Reading reading;
    Sending sending;
    while (!line->stopping) {
        bool wantWrite;
        {
            std::lock_guard<std::mutex> hold(line->lock);
            wantWrite = sending.active || !line->queue.empty();
        }
        const int ready = t.Wait(wantWrite, 100);
        if (ready < 0) {
            Disconnect(*line, "a wait failed");
            break;
        }
        if ((ready & 1) != 0 && !ReadSome(*line, t, reading)) break;
        if ((ready & 2) != 0 && !WriteSome(*line, t, sending)) break;
    }
    t.Close();
}

// ---- The owners (the game's Auth, News, City and Upload) -------------------------------------------------------------------

struct Owner {
    Kind kind = Kind::Auth;
    Callback callback = nullptr;
    std::shared_ptr<Line> line;
    bool released = false;
    uint32_t counter = 10;               // Client's sequence counter
    std::map<uint32_t, void*> contexts;  // the context of each request out, by its sequence word
    // SetSessionInfo's (City and Upload)
    uint16_t language = 0;
    int32_t player = 0;
    int32_t customer = 0;
    std::u16string password;
    ChatCallback chat = nullptr;         // Chat's
};

std::map<const void*, std::shared_ptr<Owner>>& Owners() {
    static std::map<const void*, std::shared_ptr<Owner>> owners;
    return owners;
}

Owner* Find(const void* owner) {
    auto it = Owners().find(owner);
    return it == Owners().end() ? nullptr : it->second.get();
}

std::string& ServerSlot() {
    static std::string server;
    return server;
}

TransportMaker& MakerSlot() {
    static TransportMaker maker;
    return maker;
}

// The "disconnected" answers given for a server that could not be reached, not yet taken by the error dialog.
std::atomic<int>& Unreachable() {
    static std::atomic<int> count{0};
    return count;
}

// The hosts the server's details answer (type 7, online.sam lines) named, by key ("LoginServer", "CityServer", "NewsServer").
std::map<std::string, std::string>& Advertised() {
    static std::map<std::string, std::string> hosts;
    return hosts;
}

void NoteAdvertised(const uint8_t* text, size_t size) {
    const std::string all(reinterpret_cast<const char*>(text), strnlen(reinterpret_cast<const char*>(text), size));
    size_t at = 0;
    while (at < all.size()) {
        size_t next = all.find('\n', at);
        if (next == std::string::npos) next = all.size();
        const std::string line = all.substr(at, next - at);
        at = next + 1;
        const size_t key = line.find("OnlineInfo.");
        const size_t open = line.find('"');
        const size_t close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
        if (key == std::string::npos || close == std::string::npos) continue;
        size_t keyEnd = key + 11;
        while (keyEnd < line.size() && line[keyEnd] != ' ' && line[keyEnd] != '\t' && line[keyEnd] != '"') ++keyEnd;
        Advertised()[line.substr(key + 11, keyEnd - key - 11)] = line.substr(open + 1, close - open - 1);
    }
}

// The host a connection goes to (see the header).
std::string HostFor(Kind kind, const char* asked) {
    const std::string host = asked != nullptr ? asked : "";
    if (kind == Kind::Upload) return host.empty() ? Server() : host;
    if (kind == Kind::Chat || kind == Kind::Vote) return Server();   // the city's chat and vote hosts are the server's own
    const char* key = kind == Kind::Auth ? "LoginServer" : kind == Kind::News ? "NewsServer" : "CityServer";
    auto it = Advertised().find(key);
    return it != Advertised().end() && !host.empty() && it->second == host ? host : Server();
}

// Client::CreateHeader and AddToQueue: the message queued, its record made, its context kept; its sequence word.
uint32_t Queue(Owner& o, uint16_t type, uint16_t param, const std::vector<uint8_t>& payload, void* context) {
    Header h;
    h.type = type;
    h.param = param;
    h.length = static_cast<uint32_t>(payload.size());
    h.seq = o.counter++;
    h.context = h.seq;   // a token: the context pointer stays here (a host pointer does not fit the word)
    std::vector<uint8_t> bytes(kHeader + payload.size());
    PutHeader(bytes.data(), h);
    std::copy(payload.begin(), payload.end(), bytes.begin() + kHeader);
    o.contexts[h.seq] = context;
    std::lock_guard<std::mutex> hold(o.line->lock);
    o.line->queue.emplace_back(h.seq, std::move(bytes));
    o.line->infos.push_back(Info{h.seq, 2, 0, 0});
    return h.seq;
}

// The player id, customer id and password words every City, Upload and Vote request starts with.
void PutSession(std::vector<uint8_t>& payload, const Owner& o) {
    Put32(payload.data() + 0, static_cast<uint32_t>(o.player));
    Put32(payload.data() + 4, static_cast<uint32_t>(o.customer));
    PutWide(payload.data() + 8, 0x14, o.password.c_str());
}

// ---- The answers (the owners' internalCallback2) ---------------------------------------------------------------------------

// Auth::internalCallback2. The account block (operator new(0x6c) on the Mac) is given with a count of 2 and freed after the
// callback, which copies what it needs.
void AuthReply(const Owner& o, const Reply& r, void* context) {
    if (r.type == 3) return o.callback(-4, static_cast<int32_t>(r.seq), context, nullptr);
    if (r.type == 1) return o.callback(-2, static_cast<int32_t>(r.seq), context, nullptr);
    if (r.type == 2) return o.callback(-5, static_cast<int32_t>(r.seq), context, nullptr);
    if (r.type != 9) return;
    std::array<uint8_t, 0x140> body{};   // the Mac's read 0x140 bytes whatever the reply's length
    std::memcpy(body.data(), r.body.data(), std::min(r.body.size(), body.size()));
    std::array<uint8_t, 0x6c> block{};
    auto word = [&](size_t to, size_t from) {
        const uint32_t value = Get32(body.data() + from);
        std::memcpy(block.data() + to, &value, 4);
    };
    auto wide = [&](size_t to, size_t from, size_t units) {
        const std::u16string text = WideAt(body.data(), body.size(), from, units);
        std::vector<char16_t> field(units, 0);
        CopyWide(field.data(), units, text.c_str());
        std::memcpy(block.data() + to, field.data(), units * 2);
    };
    word(0x00, 0x00);
    word(0x04, 0x04);
    wide(0x08, 0x08, 0x14);
    wide(0x30, 0x30, 0x14);   // the Mac's copied up to 0x80 units into the 0x28 bytes before +0x58 (and past the block)
    word(0x58, 0x130);
    word(0x5c, 0x134);
    word(0x60, 0x138);
    word(0x64, 0x13c);
    const uint32_t uses = 2;
    std::memcpy(block.data() + 0x68, &uses, 4);
    o.callback(0x3c, static_cast<int32_t>(r.seq), context, block.data());
}

// A counted context (operator new(0xc) on the Mac): the reply copied with two NUL bytes after it, given with a count of 2 and freed
// after the callback (which copies the text).
template <class CALL>
void WithContext(const uint8_t* bytes, size_t size, CALL call) {
    auto* buffer = static_cast<uint8_t*>(std::calloc(size + 2, 1));
    if (buffer != nullptr && size != 0) std::memcpy(buffer, bytes, size);
    OnlineUploadContext held{buffer, 2};
    call(&held);
    std::free(buffer);
}

// News::internalCallback2: 7 the server details (narrow), 4 to 6 a news text (wide).
void NewsReply(const Owner& o, const Reply& r, void* context) {
    const int32_t seq = static_cast<int32_t>(r.seq);
    if (r.type == 3) return o.callback(-4, seq, context, nullptr);
    if (r.type == 2) return o.callback(-5, seq, context, nullptr);
    if (r.type == 7) {
        NoteAdvertised(r.body.data(), r.body.size());
        return WithContext(r.body.data(), r.body.size(), [&](OnlineUploadContext* c) { o.callback(0x33, seq, context, c); });
    }
    if (r.type >= 4 && r.type <= 6)
        WithContext(r.body.data(), r.body.size(), [&](OnlineUploadContext* c) { o.callback(0x32, seq, context, c); });
}

void CityAnswer(const Owner& o, const Reply& r, void* context) {
    const int32_t seq = static_cast<int32_t>(r.seq);
    if (r.type == 3) return o.callback(-4, seq, context, nullptr);
    if (r.type == 2) return o.callback(-5, seq, context, nullptr);
    if (r.type != 0x44c) return;
    const Answer a = CityReply(r.status, r.body);
    if (a.give) o.callback(a.code, seq, context, a.block);
}

// Upload::internalCallback2: the status first, then the type.
void UploadReply(const Owner& o, const Reply& r, void* context) {
    const int32_t seq = static_cast<int32_t>(r.seq);
    switch (r.status) {
    case 1: return o.callback(-20, seq, context, nullptr);
    case 2: return o.callback(-2, seq, context, nullptr);
    case 3: case 4: case 5: case 6: case 7: case 8: case 9: return o.callback(-1, seq, context, nullptr);
    case 0xb: return o.callback(-6, seq, context, nullptr);
    case 0xc: return o.callback(-40, seq, context, nullptr);
    case 0xd: return o.callback(-41, seq, context, nullptr);
    default: break;
    }
    if (r.type == 0x4b0) {
        if (r.status != 10 || r.body.size() < 4) return o.callback(-1, seq, context, nullptr);
        // The grant: the text after the ticket word, strdup()ed (the Mac's context kept the ticket at +4, where the host's pointer
        // is; the game never reads it).
        const size_t text = strnlen(reinterpret_cast<const char*>(r.body.data() + 4), r.body.size() - 4);
        return WithContext(r.body.data() + 4, text, [&](OnlineUploadContext* c) { o.callback(0x50, seq, context, c); });
    }
    if (r.type == 3) return o.callback(-4, seq, context, nullptr);
    if (r.type == 2) return o.callback(-5, seq, context, nullptr);
    if (r.type == 0x4b1 || r.type == 0x4b2) o.callback(r.status == 0 ? 0 : -1, seq, context, nullptr);
}

// Vote::internalCallback2: by the type, then the status of type 0xffff; type 1 and the other types are not answered.
void VoteReply(const Owner& o, const Reply& r, void* context) {
    const int32_t seq = static_cast<int32_t>(r.seq);
    if (r.type == 3) return o.callback(-4, seq, context, nullptr);
    if (r.type == 2) return o.callback(-5, seq, context, nullptr);
    if (r.type == 0) return o.callback(0, seq, context, nullptr);
    if (r.type != 0xffff) return;
    switch (r.status) {
    case 2: return o.callback(-2, seq, context, nullptr);
    case 3: return o.callback(-6, seq, context, nullptr);
    case 4: return o.callback(-30, seq, context, nullptr);
    case 5: return o.callback(-31, seq, context, nullptr);
    default: return o.callback(-1, seq, context, nullptr);
    }
}

// Chat::internalCallback2: 2 and 3 are -5 and -4, 301 and 302 the event and its arguments.
void ChatAnswer(const void* key, const Owner& o, const Reply& r, void* context) {
    static const std::vector<ChatToken> kNone;
    if (r.type == 3) return o.chat(key, -4, 0, kNone, context);
    if (r.type == 2) return o.chat(key, -5, 0, kNone, context);
    if (r.type != 301 && r.type != 302) return;
    int32_t event = 0;
    std::vector<ChatToken> tokens;
    ChatParse(r.body.data(), r.body.size(), event, tokens);
    o.chat(key, 100, event, tokens, context);
}

const char* KindName(Kind kind) {
    switch (kind) {
    case Kind::Auth: return "login";
    case Kind::News: return "news";
    case Kind::City: return "city";
    case Kind::Upload: return "upload";
    case Kind::Chat: return "chat";
    case Kind::Vote: return "vote";
    }
    return "?";
}

bool ReadHostFile(const char* file, std::vector<uint8_t>& bytes) {
    if (file == nullptr) return false;
    const std::filesystem::path path = tpw::FindHostPath(file);
    if (path.empty()) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

// ---- The setting and the transport --------------------------------------------------------------------------------------------

void SetServer(const std::string& host) {
    ServerSlot() = host;
    Advertised().clear();
}

const std::string& Server() { return ServerSlot(); }

bool Active() { return !ServerSlot().empty(); }

void SetTransportMaker(TransportMaker maker) { MakerSlot() = std::move(maker); }

std::unique_ptr<Transport> MakeTransport() { return MakerSlot() ? MakerSlot()() : MakeSocketTransport(); }

// ---- The library's calls -----------------------------------------------------------------------------------------------------

namespace {

// Client::Connect: the connection's thread is started (it resolves the host and connects); its owner, or NULL.
Owner* Open(const void* owner, Kind kind, const char* host, uint16_t port, Callback callback, ChatCallback chat) {
    Release(owner);
    if (!Active() || (callback == nullptr && chat == nullptr)) return nullptr;
    auto line = std::make_shared<Line>();
    line->transport = MakeTransport();
    line->host = HostFor(kind, host);
    line->port = port;
    if (line->transport == nullptr) return nullptr;
    auto o = std::make_shared<Owner>();
    o->kind = kind;
    o->callback = callback;
    o->chat = chat;
    o->line = line;
    tpw::Log("online: the %s connection goes to %s:%u", KindName(kind), line->host.c_str(), static_cast<unsigned>(port));
    std::thread(Run, line).detach();
    Owners()[owner] = o;
    return o.get();
}

}  // namespace

int32_t Connect(const void* owner, Kind kind, const char* host, uint16_t port, Callback callback) {
    return kind != Kind::Chat && Open(owner, kind, host, port, callback, nullptr) != nullptr ? 1 : 0;
}

// Client::Release: the connection's thread is told to stop (it closes the connection) and the owner's answers are dropped.
void Release(const void* owner) {
    auto it = Owners().find(owner);
    if (it == Owners().end()) return;
    it->second->released = true;
    it->second->line->stopping = true;
    Owners().erase(it);
}

// Client::GetCurrentMessage: the message being sent (which != 0) or whose reply is being read (0).
uint32_t GetCurrentMessage(const void* owner, int32_t which) {
    Owner* o = Find(owner);
    if (o == nullptr) return 0;
    return which != 0 ? o->line->sending.load() : o->line->receiving.load();
}

// Client::MonitorMessage: a message's progress, while it is being sent (3) or its reply read (5).
void MonitorMessage(const void* owner, uint32_t request, int32_t* done, int32_t* total) {
    Owner* o = Find(owner);
    if (o == nullptr) return;
    std::lock_guard<std::mutex> hold(o->line->lock);
    const Info* info = FindInfo(*o->line, request);
    if (info == nullptr || (info->state != 3 && info->state != 5)) return;
    if (done != nullptr) *done = info->done;
    if (total != nullptr) *total = info->total;
}

// AuthClient::Login: type 8, the name and the password, 0x20 units each.
void Login(const void* owner, uint16_t language, const char16_t* name, const char16_t* password, void* context) {
    Owner* o = Find(owner);
    if (o == nullptr) return;
    std::vector<uint8_t> payload(0x80, 0);
    PutWide(payload.data() + 0x00, 0x20, name);
    PutWide(payload.data() + 0x40, 0x20, password);
    Queue(*o, 8, language, payload, context);
}

// NewsClient's GetServerNews (4, no payload), GetGameNews (5) and GetGameServerDetails (7): the game's GUID as text in 0x30 bytes.
uint32_t News(const void* owner, uint16_t type, uint16_t language, const uint8_t* guid, void* context) {
    Owner* o = Find(owner);
    if (o == nullptr) return 0;
    std::vector<uint8_t> payload;
    if (type != 4) {
        payload.assign(0x30, 0);
        static const uint8_t kNone[16] = {};
        const std::string text = GuidText(guid != nullptr ? guid : kNone);
        std::memcpy(payload.data(), text.data(), std::min<size_t>(text.size(), 0x2f));
    }
    return Queue(*o, type, language, payload, context);
}

void SetSessionInfo(const void* owner, uint16_t language, int32_t player, int32_t customer, const char16_t* password) {
    Owner* o = Find(owner);
    if (o == nullptr) return;
    o->language = language;
    o->player = player;
    o->customer = customer;
    o->password.clear();
    for (size_t i = 0; password != nullptr && i < 0x100 && password[i] != 0; ++i) o->password += password[i];
}

// CityClient's requests: type 100, SetSessionInfo's words, the sub-id, the word, the flags and the query (0x80 bytes).
int32_t CityQuery(const void* owner, void* context, uint32_t sub, uint32_t arg, int32_t flag, const std::string& query) {
    Owner* o = Find(owner);
    if (o == nullptr) return 0;
    std::vector<uint8_t> payload(0xbc, 0);
    PutSession(payload, *o);
    Put32(payload.data() + 0x30, sub);
    Put32(payload.data() + 0x34, arg);
    Put32(payload.data() + 0x38, static_cast<uint32_t>(flag) | (arg != 0 ? 1u : 0u));
    std::memcpy(payload.data() + 0x3c, query.data(), std::min<size_t>(query.size(), 0x7f));
    return static_cast<int32_t>(Queue(*o, 100, o->language, payload, context));
}

// UploadClient::UploadParkRequest: type 200 with the park's text (its city, theme, build, name and description in hex, its rides).
// Nothing is sent when the file is not there.
void UploadParkRequest(const void* owner, const char* file, const ccParkFileInfo* info, void* context) {
    Owner* o = Find(owner);
    if (o == nullptr || info == nullptr) return;
    std::error_code ec;
    const std::filesystem::path path = file != nullptr ? tpw::FindHostPath(file) : std::filesystem::path();
    const auto size = path.empty() ? static_cast<std::uintmax_t>(-1) : std::filesystem::file_size(path, ec);
    if (path.empty() || ec) {
        tpw::Log("online: the park to publish (%s) is not there", file != nullptr ? file : "");
        return;
    }
    char head[0x9bc];   // the Mac's text buffer (a description of 255 characters is 1022 of hex)
    std::snprintf(head, sizeof head, "CITYID=%d;THEME=%d;BUILD=%d;NAME=%s;DESC=%s", info->mF00, info->mThemeID, info->mFlag,
                  HexText(info->mName, 0x10).c_str(), HexText(info->mDescription, 0xff).c_str());
    std::string text = head;
    if (info->mWords[0] != 0) {
        text += ";RIDE=";
        for (size_t i = 0; i < 256 && info->mWords[i] != 0; ++i) {
            char ride[8];
            std::snprintf(ride, sizeof ride, "%X,", info->mWords[i]);
            if (text.size() + std::strlen(ride) > 0x4bf) break;   // the Mac's appended with no bound
            text += ride;
        }
    }
    std::vector<uint8_t> payload(text.size() + 0x535, 0);
    PutSession(payload, *o);
    Put32(payload.data() + 0x30, 1);
    Put32(payload.data() + 0x34, static_cast<uint32_t>(size));
    std::memcpy(payload.data() + 0x38, text.data(), text.size());
    Queue(*o, 200, o->language, payload, context);
}

// UploadClient::UploadParkFile: type 201, SetSessionInfo's words, the size and the file.
void UploadParkFile(const void* owner, const char* file, void* context) {
    Owner* o = Find(owner);
    if (o == nullptr) return;
    std::vector<uint8_t> bytes;
    if (!ReadHostFile(file, bytes)) {
        tpw::Log("online: the park to publish (%s) could not be read", file != nullptr ? file : "");
        return;
    }
    std::vector<uint8_t> payload(0x34 + bytes.size(), 0);
    PutSession(payload, *o);
    Put32(payload.data() + 0x30, static_cast<uint32_t>(bytes.size()));
    std::copy(bytes.begin(), bytes.end(), payload.begin() + 0x34);
    Queue(*o, 201, o->language, payload, context);
}

uint32_t Vote(const void* owner, int32_t park, const uint8_t guid[16], int32_t count, void* context) {
    Owner* o = Find(owner);
    if (o == nullptr || o->kind != Kind::Vote) return 0;
    std::vector<uint8_t> payload(0x68, 0);
    PutSession(payload, *o);
    static const uint8_t kNone[16] = {};
    const std::string text = GuidText(guid != nullptr ? guid : kNone);
    std::memcpy(payload.data() + 0x30, text.data(), std::min<size_t>(text.size(), 0x2f));
    Put32(payload.data() + 0x60, static_cast<uint32_t>(park));
    Put32(payload.data() + 0x64, static_cast<uint32_t>(count));
    return Queue(*o, 10, o->language, payload, context);
}

// ---- The chat ---------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> ChatPayload(uint32_t command, const std::vector<ChatToken>& tokens) {
    std::vector<uint8_t> out(4);
    Put32(out.data(), command);
    for (const ChatToken& t : tokens) {
        if (t.wide) {
            out.push_back(2);
            for (char16_t unit : t.text) {
                out.push_back(static_cast<uint8_t>(unit));
                out.push_back(static_cast<uint8_t>(unit >> 8));
            }
            out.push_back(0xff);
            out.push_back(0xff);
        } else {
            out.push_back(1);
            out.insert(out.end(), t.narrow.begin(), t.narrow.end());
            out.push_back(0xff);
        }
    }
    return out;
}

void ChatParse(const uint8_t* bytes, size_t size, int32_t& event, std::vector<ChatToken>& tokens) {
    uint8_t word[4] = {};
    std::memcpy(word, bytes, std::min<size_t>(size, 4));
    event = static_cast<int32_t>(Get32(word));
    tokens.clear();
    if (size <= 4) return;
    auto rest = [&](size_t at) { tokens.push_back(ChatToken::Narrow(std::string(reinterpret_cast<const char*>(bytes) + at, size - at))); };
    size_t at = 4;
    while (at < size && bytes[at] != 0) {
        if (bytes[at] == 1) {
            // Chat::ExtractDataMsg: up to a single 0xff; 0xff 0xff is one 0xff of the text
            std::string text;
            ++at;
            while (at < size) {
                if (bytes[at] == 0xff) {
                    if (at + 1 < size && bytes[at + 1] == 0xff) {
                        text += '\xff';
                        at += 2;
                        continue;
                    }
                    ++at;
                    break;
                }
                text += static_cast<char>(bytes[at++]);
            }
            tokens.push_back(ChatToken::Narrow(std::move(text)));
            if (event == 99) return rest(at);
        } else if (bytes[at] == 2) {
            std::u16string text;
            ++at;
            while (at + 1 < size) {
                const char16_t unit = static_cast<char16_t>(bytes[at] | bytes[at + 1] << 8);
                at += 2;
                if (unit == 0xffff) break;
                text += unit;
            }
            if (at + 1 == size) ++at;   // an odd byte left over: the argument ran past the end
            tokens.push_back(ChatToken::Wide(std::move(text)));
            if (event == 0x15) return rest(at);
        } else {
            ++at;
        }
    }
}

int32_t ChatConnect(const void* owner, const char* host, uint16_t port, ChatCallback callback) {
    return Open(owner, Kind::Chat, host, port, nullptr, callback) != nullptr ? 1 : 0;
}

uint32_t ChatRequest(const void* owner, uint32_t command, const std::vector<ChatToken>& tokens, void* context) {
    Owner* o = Find(owner);
    if (o == nullptr || o->kind != Kind::Chat) return 0;
    return Queue(*o, 300, 0, ChatPayload(command, tokens), context);
}

void Pump() {
    std::vector<std::pair<const void*, std::shared_ptr<Owner>>> owners(Owners().begin(), Owners().end());
    for (const auto& [key, o] : owners) {
        std::deque<Reply> replies;
        std::vector<std::string> notes;
        {
            std::lock_guard<std::mutex> hold(o->line->lock);
            replies.swap(o->line->replies);
            notes.swap(o->line->notes);
        }
        for (const std::string& note : notes) tpw::Log("%s", note.c_str());
        for (const Reply& r : replies) {
            if (o->released) break;   // let go of by an earlier answer's callback
            void* context = nullptr;
            auto it = o->contexts.find(r.seq);
            if (r.seq != 0 && it != o->contexts.end()) {
                context = it->second;
                o->contexts.erase(it);
            }
            tpw::Log("online: %s reply type %u status %u to request %u (%zu bytes)", KindName(o->kind), r.type, r.status, r.seq,
                     r.body.size());
            if (r.type == 3 && r.unreachable)
                ++Unreachable();
            else
                Unreachable() = 0;
            switch (o->kind) {
            case Kind::Auth: AuthReply(*o, r, context); break;
            case Kind::News: NewsReply(*o, r, context); break;
            case Kind::City: CityAnswer(*o, r, context); break;
            case Kind::Upload: UploadReply(*o, r, context); break;
            case Kind::Chat: ChatAnswer(key, *o, r, context); break;
            case Kind::Vote: VoteReply(*o, r, context); break;
            }
        }
    }
    tpw::online::lists::Sweep();
    ftp::Pump();
}

// ---- A server that cannot be reached -----------------------------------------------------------------------------------------

const char kUnreachableText[] = "Can't reach the online server. Please check your internet connection and try again.";

bool TakeUnreachable() {
    int count = Unreachable();
    while (count > 0)
        if (Unreachable().compare_exchange_weak(count, count - 1)) {
            tpw::Log("online: the player is told the online server cannot be reached");
            return true;
        }
    return false;
}

}  // namespace tpw::online::boss
