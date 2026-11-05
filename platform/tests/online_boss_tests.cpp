// online_boss_tests.cpp: the online server client (platform/online_boss.cpp, include/platform/online_boss.h) against a fake
// connection that plays the server: the framing and the text helpers byte for byte; every request's bytes; every answer the
// Auth, News, City and Upload owners give, from replies built as the server sends them (the rowsets of every City list, the
// login's account block, the news texts, the upload's grant); progress while a message is sent; and the replies that must drop
// the connection or stop a list early (a wrong magic, an oversized length, a reply to no request, a short body, a cell longer
// than its data or its room); a server that cannot be reached (refused, not answering, a name not found, a server that does not
// speak the protocol): its cause in launcher.log and the message the player is shown, once. The online client's 32-bit string slots
// (platform/online_slots.cpp): what each string callee leaves in a slot, the two layouts' views of one representation, and the
// releases. The buddy, ignore and contact containers (platform/online_containers.cpp) on the Mac's words. The chat: its payloads, the messages it reads, and its requests, replies and pushes. No window and no game; the only
// network is a closed port on 127.0.0.1 and a name under .invalid, which is never found (RFC 6761), each answered well within the
// connection's timeout.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#endif

#include "FS_Online.h"
#include "FS_OnlineList.h"
#include "log_file.h"
#include "platform.h"
#include "platform/arena.h"
#include "platform/online_boss.h"
#include "platform/online_slots.h"
#include "online_lists.h"

namespace fs = std::filesystem;
namespace boss = tpw::online::boss;
using Bytes = std::vector<uint8_t>;

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    }
}

// ---- Bytes as the wire has them ------------------------------------------------------------------------------------------------

void Be16(Bytes& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

void Be32(Bytes& b, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) b.push_back(static_cast<uint8_t>(v >> shift));
}

uint32_t Get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

void Le16Text(Bytes& b, const std::u16string& text, size_t units) {
    for (size_t i = 0; i < units; ++i) {
        const char16_t unit = i < text.size() ? text[i] : 0;
        b.push_back(static_cast<uint8_t>(unit));
        b.push_back(static_cast<uint8_t>(unit >> 8));
    }
}

uint32_t HostWord(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

uint16_t HostHalf(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

// The wide text an info's 32-bit word points to (an arena offset).
std::u16string ArenaWide(const uint8_t* word) {
    const uint32_t offset = HostWord(word);
    if (offset == 0) return u"<null>";
    return reinterpret_cast<const char16_t*>(tpw::ArenaBase() + offset);
}

std::string ArenaNarrow(const uint8_t* word) {
    const uint32_t offset = HostWord(word);
    if (offset == 0) return "<null>";
    return reinterpret_cast<const char*>(tpw::ArenaBase() + offset);
}

// A rowset's data, cell by cell (RowStore::StreamOutRow's formats).
struct Rows {
    Bytes data;
    Rows& I4(int32_t v) {
        Be32(data, static_cast<uint32_t>(v));
        return *this;
    }
    Rows& F4(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        Be32(data, bits);
        return *this;
    }
    Rows& Uz(const std::u16string& text) {
        Be16(data, static_cast<uint32_t>(text.size() * 2));
        Le16Text(data, text, text.size());
        return *this;
    }
    Rows& Sz(const std::string& text) {
        data.insert(data.end(), text.begin(), text.end());
        data.push_back(0);
        return *this;
    }
    Rows& Bg(const uint8_t (&guid)[16]) {
        Be16(data, 16);
        data.insert(data.end(), guid, guid + 16);
        return *this;
    }
    Rows& Dt(uint16_t year, uint16_t month, uint16_t day) {
        Be16(data, year);
        Be16(data, month);
        Be16(data, day);
        return *this;
    }
};

// A City reply's body (type 0x44c): the sub-id, then CreateFromStream's count, data length, extra word, format and data.
Bytes QueryBody(uint32_t sub, uint32_t count, int32_t extra, const std::string& format, const Bytes& data, long dataLength = -1) {
    Bytes b;
    Be32(b, sub);
    Be32(b, count);
    Be32(b, dataLength < 0 ? static_cast<uint32_t>(data.size()) : static_cast<uint32_t>(dataLength));
    Be32(b, static_cast<uint32_t>(extra));
    b.insert(b.end(), format.begin(), format.end());
    b.push_back(0);
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

// ---- The fake connection: the test plays the server --------------------------------------------------------------------------

struct FakeLine {
    std::mutex lock;
    std::string host;
    uint16_t port = 0;
    bool openOk = true;
    std::string failure;              // what Failure says when Open fails
    Bytes fromClient;                 // every byte the client sent, not yet taken by the test
    std::deque<uint8_t> toClient;     // the server's bytes, not yet read
    bool closed = false;              // the server closed the connection (after toClient is read)
    bool sendPaused = false;          // Send answers kWouldBlock
    size_t sendLimit = 0;             // at most this many bytes per Send (0: all)
    size_t receiveLimit = 0;          // at most this many bytes per Receive (0: all)
    size_t pauseAt = 0;               // Send pauses once this many bytes are waiting (0: never)
    std::atomic<bool> clientClosed{false};
};

std::mutex gLinesLock;
std::vector<std::shared_ptr<FakeLine>> gLines;
bool gNextOpenFails = false;
std::string gNextFailure;

std::shared_ptr<FakeLine> LastLine() {
    std::lock_guard<std::mutex> hold(gLinesLock);
    return gLines.empty() ? nullptr : gLines.back();
}

class FakeTransport : public boss::Transport {
public:
    explicit FakeTransport(std::shared_ptr<FakeLine> line) : mLine(std::move(line)) {}
    bool Open(const std::string& host, uint16_t port, const std::atomic<bool>&) override {
        std::lock_guard<std::mutex> hold(mLine->lock);
        mLine->host = host;
        mLine->port = port;
        return mLine->openOk;
    }
    std::string Failure() const override {
        std::lock_guard<std::mutex> hold(mLine->lock);
        return mLine->openOk ? std::string() : mLine->failure;
    }
    int Wait(bool wantWrite, int milliseconds) override {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            {
                std::lock_guard<std::mutex> hold(mLine->lock);
                int ready = 0;
                if (!mLine->toClient.empty() || mLine->closed) ready |= 1;
                if (wantWrite && !mLine->sendPaused) ready |= 2;
                if (ready != 0) return ready;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < end);
        return 0;
    }
    long Send(const uint8_t* bytes, size_t size) override {
        std::lock_guard<std::mutex> hold(mLine->lock);
        if (mLine->pauseAt != 0 && mLine->fromClient.size() >= mLine->pauseAt) mLine->sendPaused = true;
        if (mLine->sendPaused) return kWouldBlock;
        if (mLine->sendLimit != 0 && size > mLine->sendLimit) size = mLine->sendLimit;
        mLine->fromClient.insert(mLine->fromClient.end(), bytes, bytes + size);
        return static_cast<long>(size);
    }
    long Receive(uint8_t* bytes, size_t size) override {
        std::lock_guard<std::mutex> hold(mLine->lock);
        if (mLine->toClient.empty()) return mLine->closed ? 0 : kWouldBlock;
        if (mLine->receiveLimit != 0 && size > mLine->receiveLimit) size = mLine->receiveLimit;
        size_t n = 0;
        for (; n < size && !mLine->toClient.empty(); ++n) {
            bytes[n] = mLine->toClient.front();
            mLine->toClient.pop_front();
        }
        return static_cast<long>(n);
    }
    void Close() override { mLine->clientClosed = true; }

private:
    std::shared_ptr<FakeLine> mLine;
};

std::unique_ptr<boss::Transport> MakeFake() {
    auto line = std::make_shared<FakeLine>();
    line->openOk = !gNextOpenFails;
    line->failure = gNextFailure;
    gNextOpenFails = false;
    gNextFailure.clear();
    std::lock_guard<std::mutex> hold(gLinesLock);
    gLines.push_back(line);
    return std::make_unique<FakeTransport>(line);
}

// Waits (pumping the answers, as the game's frame loop does) until `done` or two seconds.
bool PumpUntil(const std::function<bool()>& done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < end) {
        boss::Pump();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    boss::Pump();
    return done();
}

// The next whole message the client sent: its header and its payload.
bool TakeRequest(FakeLine& line, boss::Header& header, Bytes& payload, Bytes* raw = nullptr) {
    bool whole = false;
    PumpUntil([&] {
        std::lock_guard<std::mutex> hold(line.lock);
        if (line.fromClient.size() < boss::kHeader) return false;
        const boss::Header h = boss::GetHeader(line.fromClient.data());
        whole = line.fromClient.size() >= boss::kHeader + h.length;
        return whole;
    });
    if (!whole) return false;
    std::lock_guard<std::mutex> hold(line.lock);
    header = boss::GetHeader(line.fromClient.data());
    const size_t size = boss::kHeader + header.length;
    payload.assign(line.fromClient.begin() + boss::kHeader, line.fromClient.begin() + static_cast<long>(size));
    if (raw != nullptr) raw->assign(line.fromClient.begin(), line.fromClient.begin() + static_cast<long>(size));
    line.fromClient.erase(line.fromClient.begin(), line.fromClient.begin() + static_cast<long>(size));
    return true;
}

void SendRaw(FakeLine& line, const Bytes& bytes) {
    std::lock_guard<std::mutex> hold(line.lock);
    line.toClient.insert(line.toClient.end(), bytes.begin(), bytes.end());
}

Bytes ReplyBytes(uint16_t type, uint16_t status, uint32_t seq, const Bytes& body, uint8_t from = 's') {
    boss::Header h;
    h.from = from;
    h.type = type;
    h.status = status;
    h.length = static_cast<uint32_t>(body.size());
    h.context = seq;
    h.seq = seq;
    Bytes b(boss::kHeader);
    boss::PutHeader(b.data(), h);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

void Reply(FakeLine& line, uint16_t type, uint16_t status, uint32_t seq, const Bytes& body = {}) {
    SendRaw(line, ReplyBytes(type, status, seq, body));
}

void CloseServer(FakeLine& line) {
    std::lock_guard<std::mutex> hold(line.lock);
    line.closed = true;
}

// ---- The answers the owners give -----------------------------------------------------------------------------------------------

struct Got {
    int32_t code = 0;
    int32_t request = 0;
    void* context = nullptr;
    void* block = nullptr;
    Bytes copy;   // what the game's callback copies: the account block, a news or grant text
};

std::vector<Got> gGot;
bool gReleaseOnAnswer = false;
const void* gReleaseOwner = nullptr;

void Record(int32_t code, int32_t request, void* context, void* block) {
    Got g{code, request, context, block, {}};
    if (code == 0x3c && block != nullptr) {
        const auto* p = static_cast<const uint8_t*>(block);
        g.copy.assign(p, p + 0x6c);
    }
    if ((code == 0x33 || code == 0x32 || code == 0x50) && block != nullptr) {
        const auto* held = static_cast<const OnlineUploadContext*>(block);
        const auto* text = static_cast<const uint8_t*>(held->mBuffer);
        size_t size = 0;
        if (code == 0x32) {
            while (text[size] != 0 || text[size + 1] != 0) size += 2;
        } else {
            size = std::strlen(reinterpret_cast<const char*>(text));
        }
        g.copy.assign(text, text + size);
        g.copy.push_back(static_cast<uint8_t>(held->mRefCount));
    }
    gGot.push_back(g);
    if (gReleaseOnAnswer) boss::Release(gReleaseOwner);
}

bool GotAnswers(size_t n) {
    return PumpUntil([n] { return gGot.size() >= n; });
}

int gAuth, gNews, gCity, gUpload, gVote;   // the owners (their addresses only)

const uint8_t kGameGuid[16] = {0x5c, 0x20, 0x0a, 0x41, 0x29, 0x81, 0x11, 0xd3, 0xad, 0xf4, 0x00, 0xe0, 0x18, 0x90, 0x4f, 0xd5};

// ---- The tests ---------------------------------------------------------------------------------------------------------------

void TestFraming() {
    boss::Header h;
    h.type = 0x0102;
    h.param = 0x0304;
    h.status = 0x0506;
    h.length = 0x0708090a;
    h.context = 0x0b0c0d0e;
    h.seq = 0x0f101112;
    uint8_t out[boss::kHeader];
    boss::PutHeader(out, h);
    const uint8_t want[boss::kHeader] = {'B', 'c', 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
    Check(std::memcmp(out, want, sizeof want) == 0, "header: every word big endian, 'B' 'c' first");
    const uint8_t server[boss::kHeader] = {'B', 's', 0x04, 0x4c, 0, 9, 0, 3, 0, 0, 1, 0, 0, 0, 0, 10, 0, 0, 0, 10};
    const boss::Header g = boss::GetHeader(server);
    Check(g.magic == 'B' && g.from == 's' && g.type == 0x44c && g.param == 9 && g.status == 3 && g.length == 0x100 &&
              g.context == 10 && g.seq == 10,
          "header: read back");

    Check(boss::GuidText(kGameGuid) == "{5C200A41-2981-11D3-ADF4-00E018904FD5}", "the game's GUID as the news requests write it");
    const uint8_t spaced[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    Check(boss::GuidText(spaced) == "{01020304- 5 6- 7 8-090A-0B0C0D0E0F10}", "GuidText: the middle bytes are %2X, as the Mac's");
    Check(boss::BossGuidText(spaced) == "04030201-0605-0807-090A-0B0C0D0E0F10", "BossGuidText: the Microsoft layout, no braces");
    Check(boss::HexText(u"Abé中", 0) == "0x41006200E9002D4E", "HexText: each unit low byte then high byte");
    Check(boss::HexText(u"abcdef", 3) == "0x610062006300", "HexText: at most `most` units");
    Check(boss::HexText(nullptr, 0) == "0x", "HexText: no text");
    char16_t field[4] = {9, 9, 9, 9};
    boss::CopyWide(field, 4, u"abcdef");
    Check(field[0] == u'a' && field[2] == u'c' && field[3] == 0, "CopyWide: units - 1 characters and a NUL");
    char16_t field2[4] = {9, 9, 9, 9};
    boss::CopyWide(field2, 4, u"a");
    Check(field2[0] == u'a' && field2[1] == 0 && field2[2] == 0 && field2[3] == 0, "CopyWide: the rest of the field zero");
}

void TestOff() {
    boss::SetServer("");
    Check(!boss::Active(), "off: no server named");
    Check(boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", 7598, Record) == 0, "off: Connect answers 0");
    Check(LastLine() == nullptr, "off: no connection made");
    Check(boss::GetCurrentMessage(&gAuth, 0) == 0, "off: no message");
}

void TestLogin() {
    gGot.clear();
    Check(boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", 7598, Record) == 1, "login: Connect answers 1 at once");
    auto line = LastLine();
    int context = 0;
    boss::Login(&gAuth, 9, u"Pat", u"secret", &context);
    boss::Header h;
    Bytes payload, raw;
    Check(TakeRequest(*line, h, payload, &raw), "login: the request is sent");
    Check(line->host == "127.0.0.1" && line->port == 7598, "login: the server named, on online.sam's port");
    Bytes want = {'B', 'c', 0, 8, 0, 9, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 10, 0, 0, 0, 10};
    Le16Text(want, u"Pat", 0x20);
    Le16Text(want, u"secret", 0x20);
    Check(raw == want, "login: type 8, the language, 0x80 bytes, sequence 10; the name and the password UTF-16LE, 0x20 units each");

    // The account (type 9): the words from big endian, the e-mail copied in the room it has.
    Bytes body(0x140, 0);
    auto put = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) body[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (24 - 8 * i));
    };
    put(0, 1234);
    put(4, 5678);
    Bytes nick;
    Le16Text(nick, u"Pat the Great", 0x14);
    std::copy(nick.begin(), nick.end(), body.begin() + 8);
    Bytes mail;
    Le16Text(mail, u"a-very-long-address@example.invalid-and-more-than-twenty", 0x80);
    std::copy(mail.begin(), mail.begin() + (0x130 - 0x30), body.begin() + 0x30);
    put(0x130, 0x11);
    put(0x134, 0x22);
    put(0x138, 1);
    put(0x13c, 7);
    Reply(*line, 9, 0, h.seq, body);
    Check(GotAnswers(1), "login: answered");
    if (gGot.size() >= 1) {
        const Got& g = gGot[0];
        Check(g.code == 0x3c && g.request == 10 && g.context == &context, "login: 0x3c to request 10 with its context");
        const uint8_t* b = g.copy.data();
        Check(HostWord(b + 0) == 1234 && HostWord(b + 4) == 5678, "login: the player and customer ids");
        Check(std::u16string(reinterpret_cast<const char16_t*>(b + 8)) == u"Pat the Great", "login: the name at +8");
        Check(std::u16string(reinterpret_cast<const char16_t*>(b + 0x30)) == u"a-very-long-address", "login: the e-mail, 19 units");
        Check(HostWord(b + 0x58) == 0x11 && HostWord(b + 0x5c) == 0x22 && HostWord(b + 0x60) == 1 && HostWord(b + 0x64) == 7,
              "login: the words at +0x58 (the child lock at +0x60, the avatar at +0x64)");
        Check(HostWord(b + 0x68) == 2, "login: given with a count of 2");
    }

    // Refusals: 1 (wrong name or password) -2, 2 -5; then the server closes: -4.
    boss::Login(&gAuth, 9, u"x", u"y", nullptr);
    Check(TakeRequest(*line, h, payload) && h.seq == 11, "login: the next request is sequence 11");
    Reply(*line, 1, 0, h.seq);
    boss::Login(&gAuth, 9, u"x", u"y", nullptr);
    Check(TakeRequest(*line, h, payload) && h.seq == 12, "login: sequence 12");
    Reply(*line, 2, 0, h.seq);
    Check(GotAnswers(3) && gGot[1].code == -2 && gGot[1].request == 11 && gGot[2].code == -5, "login: -2, -5");
    CloseServer(*line);
    Check(GotAnswers(4) && gGot[3].code == -4 && gGot[3].context == nullptr, "login: the connection's end is -4");
    boss::Release(&gAuth);
    Check(PumpUntil([&] { return line->clientClosed.load(); }), "login: Release closes the connection");
}

std::string ReadLog() {
    std::ifstream in(tpw::LogFilePath(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool LogHas(const std::string& text) { return ReadLog().find(text) != std::string::npos; }

// A connection that cannot be made: Connect still answers 1, then the "disconnected" answer (-4) comes; the cause is in the log
// and the error dialog takes one notice for it, once.
void Unreachable(const std::string& name, const std::string& failure) {
    gGot.clear();
    Check(!boss::TakeUnreachable(), name + ": no notice before");
    gNextOpenFails = true;
    gNextFailure = failure;
    Check(boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", 7598, Record) == 1, name + ": Connect still 1");
    Check(GotAnswers(1) && gGot[0].code == -4, name + ": the disconnected answer, -4");
    Check(LogHas("online: the connection to 127.0.0.1:7598 ended: the server could not be reached: " + failure),
          name + ": the cause in launcher.log");
    Check(boss::TakeUnreachable(), name + ": the error dialog's notice");
    Check(!boss::TakeUnreachable(), name + ": taken once");
    Check(LogHas("online: the player is told the online server cannot be reached"), name + ": the message logged");
    boss::Release(&gAuth);
}

void TestUnreachable() {
    Check(std::string(boss::kUnreachableText) ==
              "Can't reach the online server. Please check your internet connection and try again.",
          "unreachable: the message's words");
    Unreachable("refused", "Connection refused");
    Unreachable("timeout", "no answer within 15 seconds");
    Unreachable("bad host", "the name \"daphne.example.invalid\" was not found (nodename nor servname provided, or not known)");
    Unreachable("no cause", "");

    // A server that answers what is not the protocol (a TLS or web server) before any reply: not reached either.
    gGot.clear();
    Check(boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", 7598, Record) == 1, "not the protocol: Connect");
    auto line = LastLine();
    boss::Login(&gAuth, 9, u"x", u"y", nullptr);
    boss::Header h;
    Bytes payload;
    Check(TakeRequest(*line, h, payload), "not the protocol: the login is sent");
    SendRaw(*line, Bytes{0x16, 0x03, 0x01, 0x00, 0x2a, 0x02, 0x00, 0x00, 0x26, 0x03, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    Check(GotAnswers(1) && gGot[0].code == -4, "not the protocol: -4");
    Check(LogHas("ended: a reply that does not start \"Bs\""), "not the protocol: the cause in launcher.log");
    Check(boss::TakeUnreachable() && !boss::TakeUnreachable(), "not the protocol: one notice");
    boss::Release(&gAuth);

    // A server that closes the connection before answering: not reached.
    gGot.clear();
    boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", 7598, Record);
    line = LastLine();
    boss::Login(&gAuth, 9, u"x", u"y", nullptr);
    Check(TakeRequest(*line, h, payload), "closed first: the login is sent");
    CloseServer(*line);
    Check(GotAnswers(1) && gGot[0].code == -4, "closed first: -4");
    Check(boss::TakeUnreachable() && !boss::TakeUnreachable(), "closed first: one notice");
    boss::Release(&gAuth);

    // Wrong name or password and a full server keep the game's own messages; a connection lost after an answer is not a
    // server that cannot be reached.
    gGot.clear();
    boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", 7598, Record);
    line = LastLine();
    boss::Login(&gAuth, 9, u"x", u"y", nullptr);
    Check(TakeRequest(*line, h, payload), "refusals: the login is sent");
    Reply(*line, 1, 0, h.seq);
    boss::Login(&gAuth, 9, u"x", u"y", nullptr);
    Check(TakeRequest(*line, h, payload), "refusals: the second login is sent");
    Reply(*line, 2, 0, h.seq);
    CloseServer(*line);
    Check(GotAnswers(3) && gGot[0].code == -2 && gGot[1].code == -5 && gGot[2].code == -4, "refusals: -2, -5, then -4");
    Check(!boss::TakeUnreachable(), "refusals: no notice");
    boss::Release(&gAuth);

    // Two owners that cannot connect: two notices; an answer from a server that was reached clears what is left.
    gGot.clear();
    gNextOpenFails = true;
    boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", 7598, Record);
    Check(GotAnswers(1), "two: the first -4");
    gNextOpenFails = true;
    boss::Connect(&gNews, boss::Kind::News, "daphne.eagames.co.uk", 7598, Record);
    Check(GotAnswers(2) && gGot[1].code == -4, "two: the second -4");
    Check(boss::TakeUnreachable() && boss::TakeUnreachable() && !boss::TakeUnreachable(), "two: two notices");
    gNextOpenFails = true;
    boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", 7598, Record);
    Check(GotAnswers(3), "cleared: -4");
    boss::Connect(&gNews, boss::Kind::News, "daphne.eagames.co.uk", 7598, Record);
    line = LastLine();
    boss::News(&gNews, 4, 9, nullptr, nullptr);
    Check(TakeRequest(*line, h, payload), "cleared: the news request is sent");
    Reply(*line, 2, 0, h.seq);
    Check(GotAnswers(4) && gGot[3].code == -5, "cleared: the news answer");
    Check(!boss::TakeUnreachable(), "cleared: no notice left");
    boss::Release(&gAuth);
    boss::Release(&gNews);
}

// The sockets themselves: a closed port on this machine (refused) and a name that is never found, each answered (pumping as the
// game does) well within the connection's timeout, with the cause in the log.
void TestUnreachableSockets() {
    boss::SetTransportMaker(nullptr);
    struct Case {
        const char* name;
        const char* server;
        uint16_t port;
        const char* cause;
    };
    const Case cases[] = {{"closed port", "127.0.0.1", 1, "127.0.0.1:1 ended: the server could not be reached: "},
                          {"no such name", "tpw-test.invalid", 7598,
                           "ended: the server could not be reached: the name \"tpw-test.invalid\" was not found"}};
    for (const Case& c : cases) {
        boss::SetServer(c.server);
        gGot.clear();
        const auto start = std::chrono::steady_clock::now();
        Check(boss::Connect(&gAuth, boss::Kind::Auth, "daphne.eagames.co.uk", c.port, Record) == 1, std::string(c.name) + ": Connect");
        const auto end = start + std::chrono::seconds(boss::kConnectSeconds);
        while (gGot.empty() && std::chrono::steady_clock::now() < end) {
            boss::Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Check(!gGot.empty() && gGot[0].code == -4, std::string(c.name) + ": -4");
        Check(seconds < boss::kConnectSeconds, std::string(c.name) + ": answered in " + std::to_string(seconds) + " s");
        Check(LogHas(c.cause), std::string(c.name) + ": the cause in launcher.log");
        Check(boss::TakeUnreachable() && !boss::TakeUnreachable(), std::string(c.name) + ": one notice");
        boss::Release(&gAuth);
        std::printf("online_boss: %s: %.3f s\n", c.name, seconds);
    }
    boss::SetTransportMaker(MakeFake);
    boss::SetServer("127.0.0.1");
}

void TestNews() {
    gGot.clear();
    Check(boss::Connect(&gNews, boss::Kind::News, "daphne.eagames.co.uk", 7597, Record) == 1, "news: connected");
    auto line = LastLine();
    const uint32_t details = boss::News(&gNews, 7, 9, kGameGuid, nullptr);
    const uint32_t game = boss::News(&gNews, 5, 9, kGameGuid, nullptr);
    const uint32_t server = boss::News(&gNews, 4, 9, nullptr, nullptr);
    Check(details == 10 && game == 11 && server == 12, "news: the request ids are the sequence words");
    boss::Header h;
    Bytes payload, raw;
    Check(TakeRequest(*line, h, payload, &raw), "news: details sent");
    Bytes want = {'B', 'c', 0, 7, 0, 9, 0, 0, 0, 0, 0, 0x30, 0, 0, 0, 10, 0, 0, 0, 10};
    const std::string guid = "{5C200A41-2981-11D3-ADF4-00E018904FD5}";
    want.insert(want.end(), guid.begin(), guid.end());
    want.resize(boss::kHeader + 0x30, 0);
    Check(raw == want, "news: type 7, the GUID text in 0x30 bytes");
    Check(TakeRequest(*line, h, payload) && h.type == 5 && h.length == 0x30 &&
              std::string(reinterpret_cast<const char*>(payload.data())) == guid,
          "news: type 5 with the GUID");
    Check(TakeRequest(*line, h, payload) && h.type == 4 && h.length == 0 && h.seq == 12, "news: type 4, no payload");

    const std::string sam = "OnlineInfo.CityServer \"city.example\"\r\nOnlineInfo.CityPort 7591\r\n";
    Reply(*line, 7, 0, details, Bytes(sam.begin(), sam.end()));
    Bytes wide;
    Le16Text(wide, u"Game news", 9);
    Reply(*line, 5, 0, game, wide);
    Bytes wide2;
    Le16Text(wide2, u"Server news", 11);
    Reply(*line, 4, 0, server, wide2);
    Check(GotAnswers(3), "news: three answers");
    if (gGot.size() >= 3) {
        Check(gGot[0].code == 0x33 && gGot[0].request == 10, "news: 0x33 for the details");
        Check(std::string(gGot[0].copy.begin(), gGot[0].copy.end() - 1) == sam && gGot[0].copy.back() == 2,
              "news: the details' text, with a count of 2");
        Check(gGot[1].code == 0x32 && gGot[1].request == 11 && gGot[1].copy.size() == 19, "news: 0x32 for the game news");
        Check(gGot[2].code == 0x32 && gGot[2].request == 12 && gGot[2].copy.size() == 23, "news: 0x32 for the server news");
    }
    // A reply out of order (to 13, none asked) drops the connection.
    boss::News(&gNews, 4, 9, nullptr, nullptr);
    Check(TakeRequest(*line, h, payload), "news: one more");
    Reply(*line, 4, 0, 99, wide2);
    Check(GotAnswers(4) && gGot[3].code == -4, "news: a reply to no request drops the connection (-4)");
    boss::Release(&gNews);

    // The host rule: the city host the details named is reached; any other is the server named.
    boss::Connect(&gCity, boss::Kind::City, "city.example", 7591, Record);
    Check(PumpUntil([] { return LastLine() != nullptr && !LastLine()->host.empty(); }) && LastLine()->host == "city.example",
          "host: the city host the server's details named");
    boss::Release(&gCity);
    boss::Connect(&gCity, boss::Kind::City, "daphne.eagames.co.uk", 7591, Record);
    Check(PumpUntil([] { return !LastLine()->host.empty(); }) && LastLine()->host == "127.0.0.1",
          "host: online.sam's host is the server named");
    boss::Release(&gCity);
    boss::Connect(&gAuth, boss::Kind::Auth, "city.example", 7598, Record);
    Check(PumpUntil([] { return !LastLine()->host.empty(); }) && LastLine()->host == "127.0.0.1",
          "host: a host named for another service is not reached");
    boss::Release(&gAuth);
    boss::Connect(&gUpload, boss::Kind::Upload, "upload.example", 7590, Record);
    Check(PumpUntil([] { return !LastLine()->host.empty(); }) && LastLine()->host == "upload.example",
          "host: the upload host is the one the server's list gave");
    boss::Release(&gUpload);
}

ccReceivedList<uint8_t>* AsList(void* block) { return static_cast<ccReceivedList<uint8_t>*>(block); }

void TestCityRequests(FakeLine& line) {
    boss::SetSessionInfo(&gCity, 9, 1234, 5678, u"pw");
    int context = 0;
    const int32_t id = boss::CityQuery(&gCity, &context, 2, 0, 0, "CITYID=3");
    boss::Header h;
    Bytes payload, raw;
    Check(TakeRequest(line, h, payload, &raw), "city: request sent");
    Bytes want = {'B', 'c', 0, 100, 0, 9, 0, 0, 0, 0, 0, 0xbc, 0, 0, 0, static_cast<uint8_t>(id), 0, 0, 0, static_cast<uint8_t>(id)};
    Be32(want, 1234);
    Be32(want, 5678);
    Le16Text(want, u"pw", 0x14);
    Be32(want, 2);
    Be32(want, 0);
    Be32(want, 0);
    const std::string q = "CITYID=3";
    want.insert(want.end(), q.begin(), q.end());
    want.resize(boss::kHeader + 0xbc, 0);
    Check(raw == want, "city: type 100, 0xbc bytes: SetSessionInfo's words, the sub-id, the word, the flags, the query");
    boss::CityQuery(&gCity, nullptr, 3, 5, 4, "CITYID=3");
    Check(TakeRequest(line, h, payload) && Get32(payload.data() + 0x34) == 5 && Get32(payload.data() + 0x38) == 5,
          "city: the flags are the flag with bit 0 set for a nonzero word");
    gGot.clear();
    Reply(line, 0x44c, 3, h.seq - 1, QueryBody(2, 0, 0, "", {}));
    Reply(line, 0x44c, 3, h.seq, QueryBody(3, 0, 0, "", {}));
    Check(GotAnswers(2) && gGot[0].code == 3 && gGot[0].block == nullptr && gGot[0].context == &context,
          "city: status 3 (no rows) answers the code with no list");
}

void TestCityLists(FakeLine& line) {
    boss::Header h;
    Bytes payload;
    auto ask = [&](uint32_t sub, const Bytes& body, uint16_t status = 0) -> Got {
        gGot.clear();
        boss::CityQuery(&gCity, nullptr, sub, 0, 0, "");
        if (!TakeRequest(line, h, payload)) return Got{-999, 0, nullptr, nullptr, {}};
        Reply(line, 0x44c, status, h.seq, body);
        if (!GotAnswers(1)) return Got{-998, 0, nullptr, nullptr, {}};
        return gGot[0];
    };

    // Cities (1): two rows.
    Rows cities;
    cities.I4(2).Uz(u"Test City").Uz(u"t08").F4(1.5f).F4(-2.0f).F4(60.0f).I4(6).I4(7).I4(0).Uz(u"t1c").I4(12).I4(100);
    cities.I4(3).Uz(u"Other").Uz(u"").F4(0).F4(0).F4(0).I4(0).I4(0).I4(1).Uz(u"").I4(0).I4(50);
    Got g = ask(1, QueryBody(1, 2, 77, "i4,uz,uz,f4,f4,f4,i4,i4,i4,uz,i4,i4", cities.data));
    Check(g.code == 2 && g.block != nullptr, "cities: code 2 with a list");
    if (g.block != nullptr) {
        auto* list = AsList(g.block);
        const uint8_t* r = list->mInfos;
        Check(list->mCount == 2 && list->mF08 == 77 && list->mRefCount == 2, "cities: two, the extra word, a count of 2");
        float x, z, y;
        std::memcpy(&x, r + 0x0c, 4);
        std::memcpy(&z, r + 0x10, 4);
        std::memcpy(&y, r + 0x14, 4);
        Check(HostWord(r) == 2 && ArenaWide(r + 4) == u"Test City" && ArenaWide(r + 8) == u"t08" && x == 1.5f && z == -2.0f &&
                  y == 60.0f && HostWord(r + 0x20) == 6 && HostWord(r + 0x24) == 7 && HostWord(r + 0x28) == 0 &&
                  ArenaWide(r + 0x1c) == u"t1c" && HostWord(r + 0x18) == 12 && HostWord(r + 0x2c) == 100,
              "cities: the first row at the Mac's offsets");
        Check(HostWord(r + 0x30) == 3 && ArenaWide(r + 0x34) == u"Other" && HostWord(r + 0x30 + 0x28) == 1,
              "cities: the second row, 0x30 bytes on");
        list->mRefCount = 1;   // the game lets go
        boss::Pump();
    }

    // Parks (2..6, 10, 11): one row, every field.
    const uint8_t guid[16] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 1, 2, 3, 4, 5, 6, 7, 8};
    Rows park;
    park.Uz(u"Owner").Uz(u"o@x").I4(42).Bg(guid).Uz(u"Park").Uz(u"Desc").I4(5).I4(6).I4(3).I4(3).I4(9).Sz("1001").Dt(2026, 11, 2)
        .I4(2).I4(10).I4(11).I4(0).I4(13);
    const std::string parkFormat = "uz,uz,i4,bg,uz,uz,i4,i4,i4,i4,i4,sz,dt,i4,i4,i4,i4,i4";
    for (uint32_t sub : {2u, 3u, 4u, 5u, 6u, 10u, 11u}) {
        g = ask(sub, QueryBody(sub, 1, 0, parkFormat, park.data));
        Check(g.code == 3 && g.block != nullptr && AsList(g.block)->mCount == 1, "parks: code 3 for sub " + std::to_string(sub));
        if (g.block == nullptr) continue;
        const uint8_t* r = AsList(g.block)->mInfos;
        if (sub == 2) {
            Check(ArenaWide(r + 0x0c) == u"Owner" && ArenaWide(r + 0x4c) == u"o@x" && HostWord(r) == 42 &&
                      ArenaNarrow(r + 0x44) == "76543210-BA98-FEDC-0102-030405060708" && ArenaWide(r + 4) == u"Park" &&
                      ArenaWide(r + 8) == u"Desc" && HostWord(r + 0x18) == 5 && HostWord(r + 0x1c) == 6 &&
                      HostWord(r + 0x14) == 3 && HostWord(r + 0x40) == 1 && HostWord(r + 0x48) == 9,
                  "parks: the words and texts at the Mac's offsets");
            // "1001" (length 5 with its NUL): bits 0x100-4+0 = 252 and 0x100-4+3 = 255.
            uint8_t bits[0x20] = {};
            bits[31] = 0x09;
            Check(std::memcmp(r + 0x20, bits, 0x20) == 0, "parks: the ride bits, right aligned");
            Check(HostHalf(r + 0x50) == 2 && HostHalf(r + 0x52) == 11 && HostHalf(r + 0x54) == 2026, "parks: the date's halves");
            Check(HostWord(r + 0x10) == 2 && HostWord(r + 0x56) == 10 && HostWord(r + 0x5a) == 11 &&
                      HostWord(r + 0x5e) == 0x7fffffffu && HostWord(r + 0x62) == 13,
                  "parks: the last words, rank 0 as 0x7fffffff");
        }
        AsList(g.block)->mRefCount = 1;
    }

    // A row with flag 2 (not 1 or 3) and a rank; a ride text over 256 characters keeps its last 256.
    Rows park2;
    std::string many(300, '1');
    park2.Uz(u"").Uz(u"").I4(1).Bg(guid).Uz(u"").Uz(u"").I4(0).I4(0).I4(0).I4(2).I4(0).Sz(many).Dt(0, 0, 0).I4(0).I4(0).I4(0)
        .I4(4).I4(0);
    g = ask(2, QueryBody(2, 1, 0, parkFormat, park2.data));
    if (g.block != nullptr) {
        const uint8_t* r = AsList(g.block)->mInfos;
        bool all = true;
        for (int i = 0; i < 0x20; ++i) all = all && r[0x20 + i] == 0xff;
        Check(HostWord(r + 0x40) == 0 && HostWord(r + 0x5e) == 4 && all, "parks: flag 2 is 0, a rank kept, 300 rides fill 256 bits");
        AsList(g.block)->mRefCount = 1;
    } else {
        Check(false, "parks: the long ride text");
    }

    // Servers (7).
    Rows servers;
    servers.I4(1).Uz(u"Name").I4(0x64).Uz(u"127.0.0.1").I4(5).I4(7590).Uz(u"a").Uz(u"b").I4(8).Uz(u"c");
    g = ask(7, QueryBody(7, 1, 0, "i4,uz,i4,uz,i4,i4,uz,uz,i4,uz", servers.data));
    Check(g.code == 4 && g.block != nullptr, "servers: code 4");
    if (g.block != nullptr) {
        const uint8_t* r = AsList(g.block)->mInfos;
        Check(HostWord(r) == 1 && ArenaWide(r + 8) == u"Name" && HostWord(r + 4) == 0x64 && ArenaWide(r + 0xc) == u"127.0.0.1" &&
                  HostWord(r + 0x10) == 5 && HostHalf(r + 0x14) == 7590 && ArenaWide(r + 0x16) == u"a" &&
                  ArenaWide(r + 0x1a) == u"b" && HostWord(r + 0x1e) == 8 && ArenaWide(r + 0x22) == u"c",
              "servers: the fields, the port a half at +0x14");
        AsList(g.block)->mRefCount = 1;
    }

    // Themes (8).
    Rows themes;
    themes.I4(4).Uz(u"Lost Kingdom").Uz(u"t2").Uz(u"t3");
    g = ask(8, QueryBody(8, 1, 0, "i4,uz,uz,uz", themes.data));
    Check(g.code == 5 && g.block != nullptr && HostWord(AsList(g.block)->mInfos) == 4 &&
              ArenaWide(AsList(g.block)->mInfos + 4) == u"Lost Kingdom" && ArenaWide(AsList(g.block)->mInfos + 0xc) == u"t3",
          "themes: code 5 and the fields");
    if (g.block != nullptr) AsList(g.block)->mRefCount = 1;

    // Rides (9).
    Rows rides;
    rides.I4(9).Uz(u"Ride").Uz(u"r2").Bg(guid).I4(1).I4(2).Uz(u"r3").I4(3).I4(4).I4(5);
    g = ask(9, QueryBody(9, 1, 0, "i4,uz,uz,bg,i4,i4,uz,i4,i4,i4", rides.data));
    Check(g.code == 6 && g.block != nullptr, "rides: code 6");
    if (g.block != nullptr) {
        const uint8_t* r = AsList(g.block)->mInfos;
        Check(HostWord(r) == 9 && ArenaWide(r + 4) == u"Ride" && ArenaWide(r + 8) == u"r2" && std::memcmp(r + 0xc, guid, 16) == 0 &&
                  HostWord(r + 0x1c) == 1 && HostWord(r + 0x20) == 2 && ArenaWide(r + 0x24) == u"r3" && HostWord(r + 0x28) == 3 &&
                  HostWord(r + 0x2c) == 4 && HostWord(r + 0x30) == 5,
              "rides: the fields, the GUID's 16 bytes at +0xc");
        AsList(g.block)->mRefCount = 1;
    }

    // Languages (12).
    Rows languages;
    languages.Uz(u"en").Uz(u"English");
    g = ask(12, QueryBody(12, 1, 0, "uz,uz", languages.data));
    Check(g.code == 9 && g.block != nullptr && ArenaWide(AsList(g.block)->mInfos + 4) == u"English", "languages: code 9");
    if (g.block != nullptr) AsList(g.block)->mRefCount = 1;

    // The statuses.
    const struct {
        uint16_t status;
        int32_t code;
    } statuses[] = {{1, -10}, {2, -1}, {4, -2}, {5, 7}, {6, 8}};
    for (const auto& s : statuses) {
        g = ask(1, QueryBody(1, 0, 0, "", {}), s.status);
        Check(g.code == s.code && g.block == nullptr, "status " + std::to_string(s.status) + " is " + std::to_string(s.code));
    }
    // Unpublish (100) and visited (101): 0 when the server counted a row, -1 when not.
    Check(ask(100, QueryBody(100, 1, 0, "i4", Rows().I4(1).data)).code == 0, "unpublish: 0 with a row");
    Check(ask(101, QueryBody(101, 0, 0, "", {})).code == -1, "visited: -1 with none");

    // Malformed replies.
    g = ask(1, Bytes{0, 0});
    Check(g.code == -1 && g.block == nullptr, "short: a body under 4 bytes is -1");
    g = ask(1, QueryBody(1, 2, 0, "i4,uz,uz", cities.data));
    Check(g.code == 2 && g.block == nullptr, "format: a different format answers the code with no list");
    g = ask(1, QueryBody(1, 2, 0, "i4,uz,uz,f4,f4,f4,i4,i4,i4,uz,i4", cities.data));
    Check(g.code == 2 && g.block == nullptr, "format: a received format that ends early does not match");
    Bytes noNul;
    Be32(noNul, 1);
    Be32(noNul, 1);
    Be32(noNul, 0);
    Be32(noNul, 0);
    for (int i = 0; i < 40; ++i) noNul.push_back('i');
    g = ask(1, noNul);
    Check(g.code == 2 && g.block == nullptr, "format: a format with no NUL in the body is no rows");
    // Count 5 but two rows of data: two infos (the Mac's left three empty records it counted).
    g = ask(1, QueryBody(1, 5, 0, "i4,uz,uz,f4,f4,f4,i4,i4,i4,uz,i4,i4", cities.data));
    Check(g.code == 2 && g.block != nullptr && AsList(g.block)->mCount == 2, "rows: the count is the rows the data held");
    if (g.block != nullptr) AsList(g.block)->mRefCount = 1;
    // The data length over the body: kept to the body. The second row cut short: one info.
    Bytes cut = cities.data;
    cut.resize(cut.size() - 3);
    g = ask(1, QueryBody(1, 2, 0, "i4,uz,uz,f4,f4,f4,i4,i4,i4,uz,i4,i4", cut, 0x7fffffff));
    Check(g.code == 2 && g.block != nullptr && AsList(g.block)->mCount == 1, "rows: a row cut short is not listed");
    if (g.block != nullptr) AsList(g.block)->mRefCount = 1;
    // A uz longer than the cell's room (0x200 bytes) or than the data stops the list there.
    Rows big;
    big.I4(1);
    Be16(big.data, 0x202);
    big.data.resize(big.data.size() + 0x202, 'a');
    big.Uz(u"").F4(0).F4(0).F4(0).I4(0).I4(0).I4(0).Uz(u"").I4(0).I4(0);
    g = ask(1, QueryBody(1, 1, 0, "i4,uz,uz,f4,f4,f4,i4,i4,i4,uz,i4,i4", big.data));
    Check(g.code == 2 && g.block != nullptr && AsList(g.block)->mCount == 0, "rows: a uz over 0x200 bytes is no row");
    if (g.block != nullptr) AsList(g.block)->mRefCount = 1;
    Rows over;
    over.I4(1);
    Be16(over.data, 0x100);
    g = ask(1, QueryBody(1, 1, 0, "i4,uz,uz,f4,f4,f4,i4,i4,i4,uz,i4,i4", over.data));
    Check(g.code == 2 && g.block != nullptr && AsList(g.block)->mCount == 0, "rows: a uz past the data's end is no row");
    if (g.block != nullptr) AsList(g.block)->mRefCount = 1;
    Rows sz;
    sz.Uz(u"").Uz(u"").I4(1).Bg(guid).Uz(u"").Uz(u"").I4(0).I4(0).I4(0).I4(0).I4(0);
    sz.data.resize(sz.data.size() + 0x300, '1');   // an sz with no NUL in its room
    g = ask(2, QueryBody(2, 1, 0, parkFormat, sz.data));
    Check(g.code == 3 && g.block != nullptr && AsList(g.block)->mCount == 0, "rows: an sz with no NUL in 0x202 bytes is no row");
    if (g.block != nullptr) AsList(g.block)->mRefCount = 1;
    // An unknown sub-id: no answer.
    gGot.clear();
    boss::CityQuery(&gCity, nullptr, 50, 0, 0, "");
    if (TakeRequest(line, h, payload)) Reply(line, 0x44c, 0, h.seq, QueryBody(50, 0, 0, "", {}));
    PumpUntil([] { return false; });
    Check(gGot.empty(), "an unknown sub-id gives no answer");
    boss::Pump();
    Check(tpw::online::lists::HeldLists().empty(), "every list the game let go of is freed");
}

void TestCity() {
    gGot.clear();
    Check(boss::Connect(&gCity, boss::Kind::City, "daphne.eagames.co.uk", 7591, Record) == 1, "city: connected");
    auto line = LastLine();
    TestCityRequests(*line);
    TestCityLists(*line);

    // The header and a reply split into one-byte reads: the same answer.
    {
        std::lock_guard<std::mutex> hold(line->lock);
        line->receiveLimit = 1;
    }
    gGot.clear();
    boss::CityQuery(&gCity, nullptr, 8, 0, 0, "");
    boss::Header h;
    Bytes payload;
    Rows themes;
    themes.I4(4).Uz(u"A").Uz(u"B").Uz(u"C");
    if (TakeRequest(*line, h, payload)) Reply(*line, 0x44c, 0, h.seq, QueryBody(8, 1, 0, "i4,uz,uz,uz", themes.data));
    Check(GotAnswers(1) && gGot[0].code == 5 && gGot[0].block != nullptr, "reads: one byte at a time");
    if (!gGot.empty() && gGot[0].block != nullptr) AsList(gGot[0].block)->mRefCount = 1;

    // A reply whose magic is not "Bs" drops the connection.
    gGot.clear();
    boss::CityQuery(&gCity, nullptr, 8, 0, 0, "");
    if (TakeRequest(*line, h, payload)) SendRaw(*line, ReplyBytes(0x44c, 0, h.seq, {}, 'c'));
    Check(GotAnswers(1) && gGot[0].code == -4, "a reply marked 'c' drops the connection");
    boss::Release(&gCity);

    // A length over kMaxReply drops the connection before anything is allocated.
    gGot.clear();
    boss::Connect(&gCity, boss::Kind::City, "daphne.eagames.co.uk", 7591, Record);
    line = LastLine();
    boss::CityQuery(&gCity, nullptr, 8, 0, 0, "");
    if (TakeRequest(*line, h, payload)) {
        Bytes huge = ReplyBytes(0x44c, 0, h.seq, {});
        huge[8] = 0x7f;
        SendRaw(*line, huge);
    }
    Check(GotAnswers(1) && gGot[0].code == -4, "a reply of 2 GB drops the connection");
    boss::Release(&gCity);

    // An answer's callback that releases its owner: the answers after it are dropped.
    gGot.clear();
    boss::Connect(&gCity, boss::Kind::City, "daphne.eagames.co.uk", 7591, Record);
    line = LastLine();
    boss::CityQuery(&gCity, nullptr, 50, 0, 0, "");
    boss::CityQuery(&gCity, nullptr, 50, 0, 0, "");
    if (TakeRequest(*line, h, payload) && TakeRequest(*line, h, payload)) {
        Bytes both = ReplyBytes(0x44c, 1, h.seq - 1, {});
        const Bytes next = ReplyBytes(0x44c, 1, h.seq, {});
        both.insert(both.end(), next.begin(), next.end());
        SendRaw(*line, both);
    }
    // Both replies read before the next pass gives either.
    for (int i = 0; i < 2000; ++i) {
        {
            std::lock_guard<std::mutex> hold(line->lock);
            if (line->toClient.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    gReleaseOnAnswer = true;
    gReleaseOwner = &gCity;
    PumpUntil([] { return false; });
    gReleaseOnAnswer = false;
    Check(gGot.size() == 1 && gGot[0].code == -10, "release in a callback: the next answer is not given");
}

void TestUpload(const fs::path& user) {
    gGot.clear();
    const fs::path file = user / "park.lay";
    std::ofstream(file, std::ios::binary) << std::string(0x5000, 'P');
    Check(boss::Connect(&gUpload, boss::Kind::Upload, "", 7590, Record) == 1, "upload: connected");
    auto line = LastLine();
    boss::SetSessionInfo(&gUpload, 9, 1234, 5678, u"pw");
    ccParkFileInfo info{};
    info.mF00 = 3;
    info.mThemeID = 2;
    info.mFlag = 1;
    std::memcpy(info.mName, u"My Park", 16);
    std::memcpy(info.mDescription, u"Fun", 8);
    info.mWords[0] = 0x1a;
    info.mWords[1] = 0x2b;
    boss::UploadParkRequest(&gUpload, "park.lay", &info, nullptr);
    boss::Header h;
    Bytes payload;
    Check(PumpUntil([&] { return !line->host.empty(); }) && line->host == "127.0.0.1", "upload: no host listed is the server named");
    Check(TakeRequest(*line, h, payload), "upload: the request is sent");
    const std::string fixed = "CITYID=3;THEME=2;BUILD=1;NAME=0x4D0079002000500061007200" "6B00;DESC=0x460075006E00;RIDE=1A,2B,";
    Bytes want;
    Be32(want, 1234);
    Be32(want, 5678);
    Le16Text(want, u"pw", 0x14);
    Be32(want, 1);
    Be32(want, 0x5000);
    want.insert(want.end(), fixed.begin(), fixed.end());
    want.resize(fixed.size() + 0x535, 0);
    Check(h.type == 200 && h.length == fixed.size() + 0x535 && payload == want,
          "upload: type 200, SetSessionInfo's words, 1, the size and the park's text");
    Bytes grant;
    Be32(grant, 0xdeadbeef);
    const std::string granted = "granted";
    grant.insert(grant.end(), granted.begin(), granted.end());
    grant.push_back(0);
    Reply(*line, 0x4b0, 10, h.seq, grant);
    Check(GotAnswers(1) && gGot[0].code == 0x50 && std::string(gGot[0].copy.begin(), gGot[0].copy.end() - 1) == "granted" &&
              gGot[0].copy.back() == 2,
          "upload: granted (0x50) with the text after the ticket, a count of 2");

    // The file: sent 0x2000 bytes at a time; its progress while it is sent.
    {
        std::lock_guard<std::mutex> hold(line->lock);
        line->sendLimit = 0x2000;
        line->pauseAt = 0x2000;
    }
    boss::UploadParkFile(&gUpload, "park.lay", nullptr);
    const uint32_t seq = h.seq + 1;
    const bool partway = PumpUntil([&] {
        std::lock_guard<std::mutex> hold(line->lock);
        return line->sendPaused;
    });
    Check(partway, "upload: part of the file sent");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    int32_t done = -1, total = -1;
    boss::MonitorMessage(&gUpload, seq, &done, &total);
    Check(boss::GetCurrentMessage(&gUpload, 1) == seq && boss::GetCurrentMessage(&gUpload, 0) == 0,
          "upload: the message being sent is the file's");
    Check(done > 0 && done < total && total == 0x14 + 0x34 + 0x5000, "upload: its progress, sent of total");
    {
        std::lock_guard<std::mutex> hold(line->lock);
        line->pauseAt = 0;
        line->sendPaused = false;
    }
    Check(TakeRequest(*line, h, payload) && h.type == 201 && h.length == 0x5034 && Get32(payload.data() + 0x30) == 0x5000 &&
              payload[0x34] == 'P' && payload.back() == 'P',
          "upload: type 201, SetSessionInfo's words, the size and the file");
    Check(PumpUntil([&] { return boss::GetCurrentMessage(&gUpload, 1) == 0; }), "upload: sent");
    Reply(*line, 0x4b1, 0, h.seq);
    Check(GotAnswers(2) && gGot[1].code == 0, "upload: the file kept (0)");

    // The statuses.
    const struct {
        uint16_t status;
        int32_t code;
    } statuses[] = {{1, -20}, {2, -2}, {5, -1}, {0xb, -6}, {0xc, -40}, {0xd, -41}};
    for (const auto& s : statuses) {
        gGot.clear();
        boss::UploadParkRequest(&gUpload, "park.lay", &info, nullptr);
        if (TakeRequest(*line, h, payload)) Reply(*line, 0x4b0, s.status, h.seq);
        Check(GotAnswers(1) && gGot[0].code == s.code, "upload status " + std::to_string(s.status));
    }
    gGot.clear();
    boss::UploadParkRequest(&gUpload, "park.lay", &info, nullptr);
    if (TakeRequest(*line, h, payload)) Reply(*line, 0x4b0, 10, h.seq, Bytes{1, 2});
    Check(GotAnswers(1) && gGot[0].code == -1, "upload: a grant under 4 bytes is -1");
    gGot.clear();
    boss::UploadParkRequest(&gUpload, "no such park", &info, nullptr);
    PumpUntil([&] {
        std::lock_guard<std::mutex> hold(line->lock);
        return !line->fromClient.empty();
    });
    Check(line->fromClient.empty(), "upload: nothing is sent for a park that is not there");

    // A description of 255 characters: the request is 0x535 + the text, and the text keeps its bound.
    std::u16string longText(255, u'x');
    std::memcpy(info.mDescription, longText.c_str(), (longText.size() + 1) * 2);
    for (int i = 0; i < 255; ++i) info.mWords[i] = static_cast<uint16_t>(0x100 + i);
    boss::UploadParkRequest(&gUpload, "park.lay", &info, nullptr);
    std::string desc = "DESC=0x";
    for (int i = 0; i < 255; ++i) desc += "7800";
    Check(TakeRequest(*line, h, payload) && h.length <= 0x4bf + 0x535 && h.length > 0x800 &&
              std::string(reinterpret_cast<const char*>(payload.data() + 0x38)).find(desc + ";RIDE=100,101,") != std::string::npos,
          "upload: a long park's request: the whole description, rides to 0x4bf characters, over 0x800 bytes and within 0x9f4");
    boss::Release(&gUpload);
}

}  // namespace

// The string slots: a slot is the arena offset of a representation whose +0xc word and +0x10 pointer name the same characters.
namespace online_callee {
void WStringCtor(void* self);
void WStringCtorFrom(void* self, const char16_t* text);
void WStringAssign(void* self, const char16_t* text, uint32_t n);
void WStringCopy(void* self, const void* other, uint32_t pos, uint32_t count);
void WStringCopy(void* self, const std::u16string* other, uint32_t pos, uint32_t count);
void StringCtor(void* self);
void StringDtor(void* self, int16_t flag);
void StringAssign(void* self, const std::string& other, uint32_t pos, int32_t count);
}  // namespace online_callee
namespace init_callee {
void StringFromChars(void* self, const char* text);
}
namespace online_parse_callee {
void WStringAssignRange(void* self, const void* src, uint32_t pos, uint32_t count);
void WStringAssignRange(void* self, const std::u16string* src, uint32_t pos, uint32_t count);
}

void TestSlots() {
    namespace slots = tpw::slots;
    const size_t liveBytes = tpw::ArenaLiveBytes();
    const size_t live = slots::LiveCount();
    Check(slots::WideText(0)[0] == 0 && slots::NarrowText(0)[0] == 0 && slots::Length(0) == 0, "slots: 0 reads as the empty string");

    // The player's name and chat server words, side by side as in the online control (+8, +0xc).
    uint32_t words[3] = {0, 0, 0xdeadbeefu};
    online_callee::WStringCtorFrom(&words[0], u"Tester");
    const uint8_t* rep = tpw::gArenaBase + words[0];
    uint32_t length, capacity, users, dataWord;
    uint64_t dataPointer;
    std::memcpy(&length, rep, 4);
    std::memcpy(&capacity, rep + 4, 4);
    std::memcpy(&users, rep + 8, 4);
    std::memcpy(&dataWord, rep + 0xc, 4);
    std::memcpy(&dataPointer, rep + 0x10, 8);
    Check(words[0] != 0 && slots::IsLive(words[0]) && length == 6 && capacity >= 6 && users == 1, "slots: the constructor's representation");
    Check(static_cast<uint32_t>(dataPointer) == dataWord && tpw::gArenaBase + dataWord == reinterpret_cast<uint8_t*>(dataPointer),
          "slots: the data word is the low half of the data pointer");
    Check(std::u16string(slots::WideText(words[0])) == u"Tester", "slots: the text through the pointer");
    Check(reinterpret_cast<const char16_t*>(tpw::gArenaBase + dataWord)[6] == 0, "slots: the characters are terminated");

    init_callee::StringFromChars(&words[1], "127.0.0.1");
    Check(std::string(slots::NarrowText(words[1])) == "127.0.0.1" && slots::Length(words[1]) == 9 && words[2] == 0xdeadbeefu,
          "slots: a narrow string from characters, and nothing past its word");
    online_callee::StringAssign(&words[1], std::string("chat.example.invalid"), 5, -1);
    Check(std::string(slots::NarrowText(words[1])) == "example.invalid", "slots: assign from a position to the end");
    online_callee::StringAssign(&words[1], std::string("abc"), 9, 2);
    Check(words[1] != 0 && slots::Length(words[1]) == 0, "slots: assign from past the end is empty");

    // Copies and ranges; npos is the rest, a count past the end stops there.
    uint32_t copy = 0, part = 0;
    online_callee::WStringCopy(&copy, &words[0], 0, 0xffffffffu);
    Check(copy != words[0] && std::u16string(slots::WideText(copy)) == u"Tester", "slots: a copy has its own representation");
    online_callee::WStringCopy(&part, &words[0], 2, 3);
    Check(std::u16string(slots::WideText(part)) == u"ste", "slots: a part");
    online_parse_callee::WStringAssignRange(&part, &words[0], 4, 100);
    Check(std::u16string(slots::WideText(part)) == u"er", "slots: assign a range past the end");
    online_callee::WStringAssign(&part, slots::WideText(part) + 1, 1);
    Check(std::u16string(slots::WideText(part)) == u"r", "slots: assign from its own characters");

    // From the host's strings (a name SendCommand hands on, the last teller ProcessResponse keeps).
    const std::u16string host = u"Visitor2";
    uint32_t fromHost = 0;
    online_callee::WStringCopy(&fromHost, &host, 0, 0xffffffffu);
    Check(slots::IsLive(fromHost) && std::u16string(slots::WideText(fromHost)) == u"Visitor2" && slots::Length(fromHost) == 8,
          "slots: a copy from the host's string");
    uint32_t hostPart = 0;
    online_callee::WStringCopy(&hostPart, &host, 7, 5);
    Check(std::u16string(slots::WideText(hostPart)) == u"2", "slots: a part of the host's string, the count past its end");
    slots::Release(&hostPart);
    online_callee::WStringCopy(&hostPart, &host, 20, 1);
    Check(hostPart != 0 && slots::Length(hostPart) == 0, "slots: a copy from past the host string's end is empty");
    online_parse_callee::WStringAssignRange(&fromHost, &host, 0, 7);
    Check(std::u16string(slots::WideText(fromHost)) == u"Visitor", "slots: assign a range of the host's string");
    const std::u16string none;
    online_parse_callee::WStringAssignRange(&fromHost, &none, 0, 0xffffffffu);
    Check(slots::IsLive(fromHost) && slots::Length(fromHost) == 0, "slots: assign the empty host string");
    slots::Release(&fromHost);
    slots::Release(&hostPart);
    uint32_t empty = 0;
    online_callee::WStringCtor(&empty);
    Check(empty != 0 && slots::Length(empty) == 0 && slots::WideText(empty)[0] == 0, "slots: the empty constructor");
    uint32_t narrow = 0;
    online_callee::StringCtor(&narrow);
    Check(narrow != 0 && slots::NarrowText(narrow)[0] == 0, "slots: the empty narrow constructor");

    // Releases: the last user frees; a word the slots did not make (or 0) is left alone.
    uint8_t shared[24] = {};
    std::memcpy(shared, &words[0], 4);
    uint32_t sharedUsers = 2;
    std::memcpy(tpw::gArenaBase + words[0] + 8, &sharedUsers, 4);
    Check(slots::Release(shared) && slots::IsLive(words[0]), "slots: a shared representation outlives one user");
    uint32_t stranger = 0x1234;
    Check(!slots::Release(&stranger) && stranger == 0x1234, "slots: a word not made here is left alone");
    slots::Release(&words[0]);   // as the wide destructor (launcher/pending_manual.cpp) does
    slots::Release(&copy);
    slots::Release(&part);
    slots::Release(&empty);
    online_callee::StringDtor(&words[1], -1);
    online_callee::StringDtor(&narrow, -1);
    Check(words[0] == 0 && words[1] == 0 && copy == 0 && words[2] == 0xdeadbeefu, "slots: a released slot is 0");
    Check(slots::LiveCount() == live && tpw::ArenaLiveBytes() == liveBytes, "slots: every representation is given back");
}

// ---- The name containers (platform/online_containers.cpp) --------------------------------------------------------------------

struct BuddyKeyEntry {
    std::u16string mKey;
    uint8_t mDirect;
    uint8_t mFlag;
};
namespace online_callee {
void* BuddyTreeFind(void* tree, const std::u16string* key);
void* NocaseSetFind(void* tree, const std::u16string* key);
void BuddyTreeInsert(void* tree, const BuddyKeyEntry* entry);
void BuddyTreeEraseOne(void* tree, const std::u16string* key);
void WStringSetInsertOne(void* tree, const std::u16string& key);
void WStringSetEraseOne(void* tree, const std::u16string* key);
bool NocaseWStringEqual(const std::u16string* a, const std::u16string* b);
}  // namespace online_callee
namespace response_callee {
bool WStringEqual(const std::u16string& a, const void* b);
void* WStringSetFind(void* tree, const std::u16string* key);
void NocaseSetInsertOne(void* tree, const std::u16string* key);
void NocaseSetErase(void* tree, void* node);
}  // namespace response_callee
namespace chatdata_callee {
void TreeWStringDestroy(void* tree, uint32_t node);
void TreeBuddyDestroy(void* tree, uint32_t node);
void TreeNocaseDestroy(void* tree, uint32_t node);
}  // namespace chatdata_callee

namespace {

uint32_t Word(const void* at) {
    uint32_t w;
    std::memcpy(&w, at, 4);
    return w;
}
uint32_t NodeWord(uint32_t node, uint32_t at) { return Word(tpw::gArenaBase + node + at); }

// A tree header as OnlineChatData's constructor leaves it (v51126 WStringTreeCtor, BuddyTreeCtor): count 0, root 0, the first node
// word naming the end node (the header + 4).
uint8_t* NewHeader(uint32_t beginAt) {
    uint8_t* header = static_cast<uint8_t*>(tpw::ArenaAllocZeroed(0x10));
    const uint32_t end = tpw::ArenaOffset(header + 4);
    std::memcpy(header + beginAt, &end, 4);
    return header;
}

// The client's walk (ignore_callee::TreeIncrement, src/game/MslContainerGlue.cpp): the first node word, then up through the parents.
std::vector<std::u16string> Walk(const uint8_t* header, uint32_t beginAt) {
    std::vector<std::u16string> names;
    const uint32_t end = tpw::ArenaOffset(header + 4);
    uint32_t it = Word(header + beginAt);
    for (int guard = 0; it != end && guard < 1000; ++guard) {
        const uint32_t slot = NodeWord(it, 0xc);
        names.emplace_back(tpw::slots::WideText(slot), tpw::slots::Length(slot));
        if (NodeWord(it, 4) != 0) {
            it = NodeWord(it, 4);
            while (NodeWord(it, 0) != 0) it = NodeWord(it, 0);
            continue;
        }
        for (;;) {
            const uint32_t parent = NodeWord(it, 8) & ~1u;
            const bool cameUpLeft = NodeWord(parent, 0) == it;
            it = parent;
            if (cameUpLeft) break;
        }
    }
    return names;
}

// Every link agrees with its child's parent word, the root's parent is the end node, the count is the nodes', the first node word
// names the leftmost node (the end node when empty) and the colour bit is 0.
bool Consistent(const uint8_t* header, uint32_t beginAt) {
    const uint32_t end = tpw::ArenaOffset(header + 4);
    const uint32_t root = Word(header + 4);
    if (root == 0) return Word(header) == 0 && Word(header + beginAt) == end;
    if (NodeWord(root, 8) != end) return false;
    uint32_t count = 0;
    std::vector<uint32_t> todo{root};
    while (!todo.empty()) {
        const uint32_t node = todo.back();
        todo.pop_back();
        ++count;
        for (uint32_t side : {0u, 4u}) {
            const uint32_t child = NodeWord(node, side);
            if (child == 0) continue;
            if (NodeWord(child, 8) != node) return false;
            todo.push_back(child);
        }
    }
    uint32_t leftmost = root;
    while (NodeWord(leftmost, 0) != 0) leftmost = NodeWord(leftmost, 0);
    return count == Word(header) && Word(header + beginAt) == leftmost;
}

std::u16string KeyAt(const void* node) {
    const uint32_t slot = Word(static_cast<const uint8_t*>(node) + 0xc);
    return std::u16string(tpw::slots::WideText(slot), tpw::slots::Length(slot));
}

using Names = std::vector<std::u16string>;

}  // namespace

void TestContainers() {
    namespace slots = tpw::slots;
    const size_t liveBytes = tpw::ArenaLiveBytes();
    const size_t live = slots::LiveCount();

    // The contact set (std::set<std::wstring>): the plain order, unique keys.
    uint8_t* contacts = NewHeader(8);
    for (const char16_t* name : {u"Bob", u"alice", u"Carol", u"bob", u"Dave", u"Bob"}) online_callee::WStringSetInsertOne(contacts, name);
    Check(Walk(contacts, 8) == Names{u"Bob", u"Carol", u"Dave", u"alice", u"bob"} && Word(contacts) == 5 && Consistent(contacts, 8),
          "containers: the contact set's walk is in the plain order, without the second Bob");
    std::u16string key = u"Carol";
    void* found = response_callee::WStringSetFind(contacts, &key);
    Check(found != contacts + 4 && KeyAt(found) == u"Carol", "containers: a contact is found");
    key = u"CAROL";
    Check(response_callee::WStringSetFind(contacts, &key) == contacts + 4, "containers: the contact set's find is case-sensitive");
    key = u"Bo";
    Check(response_callee::WStringSetFind(contacts, &key) == contacts + 4, "containers: a prefix is not the name");

    // Erases of every shape: a leaf, a node with one child, with two, the root and the first node.
    for (const char16_t* name : {u"M", u"F", u"T", u"C", u"H", u"R", u"X", u"A", u"D", u"G", u"S"})
        online_callee::WStringSetInsertOne(contacts, name);
    Check(Word(contacts) == 16 && Consistent(contacts, 8), "containers: sixteen contacts");
    struct Step {
        const char16_t* name;
        const char* what;
    };
    for (const Step& step : {Step{u"X", "a leaf"}, Step{u"R", "a node with one child"}, Step{u"F", "a node with two children"},
                             Step{u"Bob", "the root"}, Step{u"A", "the first node"}, Step{u"Nobody", "a name not in the set"}}) {
        Names before = Walk(contacts, 8);
        key = step.name;
        online_callee::WStringSetEraseOne(contacts, &key);
        Names after = Walk(contacts, 8);
        before.erase(std::remove(before.begin(), before.end(), key), before.end());
        Check(after == before && Consistent(contacts, 8), std::string("containers: the contact set erases ") + step.what);
    }
    while (Word(contacts + 4) != 0) {
        key = KeyAt(tpw::gArenaBase + Word(contacts + 4));
        online_callee::WStringSetEraseOne(contacts, &key);
    }
    Check(Consistent(contacts, 8) && Walk(contacts, 8).empty(), "containers: the emptied set is as the constructor left it");
    Check(slots::LiveCount() == live, "containers: every key of an erased node is released");

    // The ignore set (std::set<nocase_string>): the Mac's case folding (Latin-1 too), the erase by the node a find answered.
    uint8_t* ignores = NewHeader(8);
    for (const char16_t* name : {u"Pat", u"zed", u"PAT", u"Émile", u"émile", u"Ann"}) {
        key = name;
        response_callee::NocaseSetInsertOne(ignores, &key);
    }
    Check(Walk(ignores, 8) == Names{u"Ann", u"Pat", u"zed", u"Émile"} && Consistent(ignores, 8),
          "containers: the ignore set folds case, the first spelling stays");
    key = u"pAt";
    found = online_callee::NocaseSetFind(ignores, &key);
    Check(found != ignores + 4 && KeyAt(found) == u"Pat", "containers: an ignored name is found in any case");
    response_callee::NocaseSetErase(ignores, found);
    response_callee::NocaseSetErase(ignores, ignores + 4);
    Check(Walk(ignores, 8) == Names{u"Ann", u"zed", u"Émile"} && Consistent(ignores, 8),
          "containers: the ignore set erases the found node, the end node is left alone");
    Check(online_callee::NocaseSetFind(ignores, &key) == ignores + 4, "containers: an erased name is not found");

    // The buddy map (std::map<nocase_string, BuddyInfo>): the first node word at +0xc, the two BuddyInfo bytes at node + 0x10.
    uint8_t* buddies = NewHeader(0xc);
    buddies[8] = 0x5a;   // the compare functor's byte stays
    online_callee::BuddyTreeInsert(buddies, new BuddyKeyEntry{u"Sam", 1, 0});
    BuddyKeyEntry again{u"SAM", 0, 1};
    online_callee::BuddyTreeInsert(buddies, &again);
    BuddyKeyEntry other{u"Kim", 0, 1};
    online_callee::BuddyTreeInsert(buddies, &other);
    key = u"sam";
    const uint8_t* sam = static_cast<const uint8_t*>(online_callee::BuddyTreeFind(buddies, &key));
    Check(sam != buddies + 4 && KeyAt(sam) == u"Sam" && sam[0x10] == 1 && sam[0x11] == 0,
          "containers: a buddy is found in any case and keeps its first BuddyInfo");
    Check(Walk(buddies, 0xc) == Names{u"Kim", u"Sam"} && Consistent(buddies, 0xc) && buddies[8] == 0x5a,
          "containers: the buddy map's walk from its first node word at +0xc");
    key = u"KIM";
    online_callee::BuddyTreeEraseOne(buddies, &key);
    Check(Walk(buddies, 0xc) == Names{u"Sam"} && Consistent(buddies, 0xc), "containers: a buddy is erased by name in any case");

    // The chat data's destructor: each tree freed from its root.
    chatdata_callee::TreeBuddyDestroy(buddies, Word(buddies + 4));
    chatdata_callee::TreeNocaseDestroy(ignores, Word(ignores + 4));
    for (const char16_t* name : {u"a", u"b", u"c"}) online_callee::WStringSetInsertOne(contacts, name);
    chatdata_callee::TreeWStringDestroy(contacts, Word(contacts + 4));
    tpw::ArenaFree(contacts);
    tpw::ArenaFree(ignores);
    tpw::ArenaFree(buddies);
    Check(slots::LiveCount() == live && tpw::ArenaLiveBytes() == liveBytes, "containers: the destroys give back every node and key");

    // The equalities: the host's string against the player's name slot (plain), and two nocase strings.
    uint32_t me = slots::MakeWide(u"Tester", 6);
    Check(response_callee::WStringEqual(u"Tester", &me) && !response_callee::WStringEqual(u"tester", &me) &&
              !response_callee::WStringEqual(u"Test", &me) && !response_callee::WStringEqual(u"Testers", &me),
          "containers: the visitor's name against the player's (plain)");
    slots::Release(&me);
    const std::u16string a = u"Tester", b = u"TESTER", c = u"Testers", d = u"é", e = u"É";
    Check(online_callee::NocaseWStringEqual(&a, &b) && !online_callee::NocaseWStringEqual(&a, &c) &&
              online_callee::NocaseWStringEqual(&d, &e),
          "containers: the nocase equality");
}

// ---- The chat ---------------------------------------------------------------------------------------------------------------

struct ChatGot {
    const void* owner = nullptr;
    int32_t code = 0;
    int32_t event = 0;
    std::vector<boss::ChatToken> args;
    void* context = nullptr;
};

std::vector<ChatGot> gChatGot;

void RecordChat(const void* owner, int32_t code, int32_t event, const std::vector<boss::ChatToken>& args, void* context) {
    gChatGot.push_back(ChatGot{owner, code, event, args, context});
}

bool GotChat(size_t n) {
    return PumpUntil([n] { return gChatGot.size() >= n; });
}

bool IsNarrow(const boss::ChatToken& t, const std::string& text) { return !t.wide && t.narrow == text; }
bool IsWide(const boss::ChatToken& t, const std::u16string& text) { return t.wide && t.text == text; }

Bytes Parsed(const Bytes& b, int32_t& event, std::vector<boss::ChatToken>& tokens) {
    boss::ChatParse(b.data(), b.size(), event, tokens);
    return b;
}

int gChat;   // the Chat block (its address only)

void TestChat() {
    using boss::ChatToken;
    // The requests' payloads: CreatePlayer (0) and CreateRoom (5) as ChatClient builds them.
    const Bytes player = boss::ChatPayload(0, {ChatToken::Wide(u"Pat"), ChatToken::Number(1000), ChatToken::Wide(u"pw"),
                                               ChatToken::Number(-3)});
    const Bytes wantPlayer = {0, 0, 0, 0, 2, 'P', 0, 'a', 0, 't', 0, 0xff, 0xff, 1, '1', '0', '0', '0', 0xff,
                              2, 'p', 0, 'w', 0, 0xff, 0xff, 1, '-', '3', 0xff};
    Check(player == wantPlayer, "chat: CreatePlayer, the command word then each argument, no end byte");
    const Bytes room = boss::ChatPayload(5, {ChatToken::Wide(u"E2E"), ChatToken::Wide(u""), ChatToken::Wide(u"é"),
                                             ChatToken::Narrow("F98"), ChatToken::Number(0x100)});
    const Bytes wantRoom = {0, 0, 0, 5, 2, 'E', 0, '2', 0, 'E', 0, 0xff, 0xff, 2, 0xff, 0xff, 2, 0xe9, 0, 0xff, 0xff,
                            1, 'F', '9', '8', 0xff, 1, '2', '5', '6', 0xff};
    Check(room == wantRoom, "chat: CreateRoom, an empty wide argument is 0x02 0xffff");
    Check(boss::ChatPayload(8, {}) == Bytes({0, 0, 0, 8}), "chat: a command of no arguments is its word");

    // What the server sends: the event, then 0x01 narrow, 0x02 wide, any other byte skipped, 0x00 the end.
    int32_t event = 0;
    std::vector<ChatToken> t;
    Parsed({0, 0, 0, 0x25, 1, '1', '2', 0xff, 2, 'A', 0, 'l', 0, 0xff, 0xff, 7, 1, 'a', 0xff, 0xff, 'b', 0xff, 0, 1, 'x', 0xff},
           event, t);
    Check(event == 0x25 && t.size() == 3 && IsNarrow(t[0], "12") && IsWide(t[1], u"Al") && IsNarrow(t[2], "a\xff" "b"),
          "chat: narrow, wide, a skipped byte, 0xff 0xff as one 0xff, nothing after 0x00");
    Parsed({0, 0, 0, 99, 1, 'i', 'd', 0xff, 2, 'r', 0, 0xff}, event, t);
    Check(event == 99 && t.size() == 2 && IsNarrow(t[0], "id") && IsNarrow(t[1], std::string("\x02r\0\xff", 4)),
          "chat: event 99, the rest after the first narrow argument is one raw argument");
    Parsed({0, 0, 0, 0x15, 2, 'B', 0, 0xff, 0xff, 1, 'z', 0}, event, t);
    Check(event == 0x15 && t.size() == 2 && IsWide(t[0], u"B") && IsNarrow(t[1], std::string("\x01z\0", 3)),
          "chat: event 0x15, the rest after the first wide argument is one raw argument");
    Parsed({0, 0, 0, 54}, event, t);
    Check(event == 54 && t.empty(), "chat: an event alone");
    Parsed({}, event, t);
    Check(event == 0 && t.empty(), "chat: nothing at all reads as event 0");
    Parsed({0, 0, 1}, event, t);
    Check(event == 0x100 && t.empty(), "chat: a short word, its missing bytes 0");
    Parsed({0, 0, 0, 1, 1, 'a', 'b'}, event, t);
    Check(t.size() == 1 && IsNarrow(t[0], "ab"), "chat: a narrow argument cut short ends at the message's end");
    Parsed({0, 0, 0, 1, 2, 'A', 0, 'B'}, event, t);
    Check(t.size() == 1 && IsWide(t[0], u"A"), "chat: a wide argument cut short keeps its whole units");

    // The connection: requests are type 300 to the server's chat port; replies (301) and pushes (302) come back in order.
    gChatGot.clear();
    Check(boss::Connect(&gChat, boss::Kind::Chat, "chat.invalid", 7593, Record) == 0, "chat: Connect does not open a chat");
    Check(boss::ChatRequest(&gChat, 8, {}, nullptr) == 0, "chat: no request before ChatConnect");
    Check(boss::ChatConnect(&gChat, "chat.invalid", 7593, RecordChat) == 1, "chat: ChatConnect answers 1");
    auto line = LastLine();
    int context = 0;
    const uint32_t seq = boss::ChatRequest(&gChat, 0, {ChatToken::Wide(u"Pat"), ChatToken::Number(1000), ChatToken::Wide(u"pw"),
                                                       ChatToken::Number(-3)}, &context);
    boss::Header h;
    Bytes payload;
    Check(seq == 10 && TakeRequest(*line, h, payload), "chat: the request is sent, sequence 10");
    Check(line->host == "127.0.0.1" && line->port == 7593, "chat: to the server named, on the chat port");
    Check(h.type == 300 && h.param == 0 && h.seq == 10 && h.context == 10 && payload == wantPlayer,
          "chat: type 300, no language, the payload as built");
    SendRaw(*line, ReplyBytes(302, 0, 0, {0, 0, 0, 0x27, 2, 'J', 0, 'o', 0, 0xff, 0xff}));
    Reply(*line, 301, 0, h.seq, {0, 0, 0, 52, 1, '7', 0xff});
    Check(GotChat(2), "chat: the push and the reply are given");
    if (gChatGot.size() >= 2) {
        const ChatGot& a = gChatGot[0];
        const ChatGot& b = gChatGot[1];
        Check(a.owner == &gChat && a.code == 100 && a.event == 0x27 && a.args.size() == 1 && IsWide(a.args[0], u"Jo") &&
                  a.context == nullptr,
              "chat: the push first, code 100, no context");
        Check(b.owner == &gChat && b.code == 100 && b.event == 52 && b.args.size() == 1 && IsNarrow(b.args[0], "7") &&
                  b.context == &context,
              "chat: the reply, code 100, its request's context");
    }
    boss::ChatRequest(&gChat, 8, {}, &context);
    Check(TakeRequest(*line, h, payload) && h.seq == 11 && payload == Bytes({0, 0, 0, 8}), "chat: the next request, 11");
    Reply(*line, 2, 0, h.seq);
    Check(GotChat(3) && gChatGot[2].code == -5 && gChatGot[2].event == 0 && gChatGot[2].args.empty() &&
              gChatGot[2].context == &context,
          "chat: the server's error reply is -5");
    CloseServer(*line);
    Check(GotChat(4) && gChatGot[3].code == -4 && gChatGot[3].args.empty(), "chat: the connection's end is -4");
    boss::Release(&gChat);
    Check(PumpUntil([&] { return line->clientClosed.load(); }), "chat: Release closes the connection");
    Check(boss::ChatRequest(&gChat, 8, {}, nullptr) == 0, "chat: no request after Release");
    Check(boss::ChatRequest(&gAuth, 8, {}, nullptr) == 0, "chat: no chat request on another kind's owner");
}

void TestVote() {
    gGot.clear();
    Check(boss::Vote(&gVote, 77, kGameGuid, 1, nullptr) == 0, "vote: no request before Connect");
    Check(boss::Connect(&gVote, boss::Kind::Vote, "vote.eagames.co.uk", 7599, Record) == 1, "vote: connected");
    auto line = LastLine();
    boss::SetSessionInfo(&gVote, 9, 1234, 5678, u"pw");
    int context = 0;
    const uint32_t seq = boss::Vote(&gVote, 77, kGameGuid, 1, &context);
    Check(seq == 10, "vote: the request's sequence word");
    boss::Header h;
    Bytes payload;
    Check(TakeRequest(*line, h, payload), "vote: the request is sent");
    Check(line->host == "127.0.0.1" && line->port == 7599, "vote: the server named, on the port asked");
    Bytes want;
    Be32(want, 1234);
    Be32(want, 5678);
    Le16Text(want, u"pw", 0x14);
    const std::string guid = "{5C200A41-2981-11D3-ADF4-00E018904FD5}";
    want.insert(want.end(), guid.begin(), guid.end());
    want.resize(0x60, 0);
    Be32(want, 77);
    Be32(want, 1);
    Check(h.type == 10 && h.param == 9 && h.length == 0x68 && payload == want,
          "vote: type 10, the language, SetSessionInfo's words, the GUID text at +0x30, the park and the count");
    Reply(*line, 0, 0, seq);
    Check(GotAnswers(1) && gGot[0].code == 0 && gGot[0].request == 10 && gGot[0].context == &context && gGot[0].block == nullptr,
          "vote: type 0 is 0, the vote counted, with its context");
    const struct {
        uint16_t type, status;
        int32_t code;
    } kAnswers[] = {{0, 7, 0}, {0xffff, 2, -2}, {0xffff, 3, -6}, {0xffff, 4, -30}, {0xffff, 5, -31}, {0xffff, 0, -1},
                    {0xffff, 1, -1}, {0xffff, 6, -1}, {2, 0, -5}};
    size_t n = 1;
    for (const auto& a : kAnswers) {
        const uint32_t s = boss::Vote(&gVote, 77, kGameGuid, 1, nullptr);
        Check(TakeRequest(*line, h, payload) && h.seq == s, "vote: sent again");
        Reply(*line, a.type, a.status, s);
        ++n;
        Check(GotAnswers(n) && gGot[n - 1].code == a.code && gGot[n - 1].request == static_cast<int32_t>(s),
              "vote: type " + std::to_string(a.type) + " status " + std::to_string(a.status) + " is " + std::to_string(a.code));
    }
    const uint32_t s1 = boss::Vote(&gVote, 77, kGameGuid, 1, nullptr);
    const uint32_t s2 = boss::Vote(&gVote, 77, kGameGuid, 1, nullptr);
    Check(TakeRequest(*line, h, payload) && TakeRequest(*line, h, payload), "vote: sent for type 1 and another type");
    Reply(*line, 1, 0, s1);
    Reply(*line, 0x44c, 0, s2);
    Check(PumpUntil([&] { return LogHas("online: vote reply type 1100 status 0 to request " + std::to_string(s2)); }),
          "vote: both replies taken");
    CloseServer(*line);
    Check(GotAnswers(n + 1) && gGot.size() == n + 1 && gGot[n].code == -4 && gGot[n].context == nullptr,
          "vote: type 1 and the other types are not answered; the connection's end is -4");
    boss::Release(&gVote);
    Check(PumpUntil([&] { return line->clientClosed.load(); }), "vote: Release closes the connection");
    Check(boss::Vote(&gVote, 77, kGameGuid, 1, nullptr) == 0, "vote: no request after Release");
    Check(boss::Vote(&gAuth, 77, kGameGuid, 1, nullptr) == 0, "vote: no vote on another kind's owner");
}

int main() {
#ifndef _WIN32
    const int pid = static_cast<int>(getpid());
#else
    const int pid = _getpid();
#endif
    const fs::path root = fs::temp_directory_path() / ("tpw-online-boss-" + std::to_string(pid));
    std::error_code ec;
    fs::create_directories(root / "user", ec);
    fs::create_directories(root / "game", ec);
    tpw::config().userDir = root / "user";
    tpw::config().gameDir = root / "game";
    std::string err;
    const char* argv[] = {"online_boss_tests"};
    Check(tpw::OpenLogFile(root / "logs", "online_boss_tests", 1, argv, err), "the log file: " + err);

    boss::SetTransportMaker(MakeFake);
    const size_t live = tpw::ArenaLiveBytes();
    TestFraming();
    TestSlots();
    TestContainers();
    TestOff();
    boss::SetServer("127.0.0.1");
    Check(boss::Active() && boss::Server() == "127.0.0.1", "on: the server named");
    TestLogin();
    TestUnreachable();
    TestNews();
    TestCity();
    TestUpload(root / "user");
    TestUnreachableSockets();
    TestChat();
    TestVote();
    boss::SetServer("");
    boss::Pump();
    Check(tpw::online::lists::HeldLists().empty() && tpw::ArenaLiveBytes() == live, "every list's strings are given back");

    tpw::CloseLogFile();
    fs::remove_all(root, ec);
    std::printf("online_boss: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
