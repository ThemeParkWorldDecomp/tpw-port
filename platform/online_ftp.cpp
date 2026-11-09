// online_ftp.cpp: the online server's park downloads (include/platform/online_ftp.h): the steps of the Mac's FTPClient (_Connect,
// _Download with _EnterPassiveMode and RecvData, the QUIT of ProcessEvents) on a connection's own thread, over boss::MakeTransport's
// connections, and the checks a downloaded park passes before it is written. The differences from the Mac's are listed in the header.
#include "platform/online_ftp.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <zlib.h>

#include "platform.h"
#include "platform/online_boss.h"
#include "path_text.h"

namespace tpw::online::boss::ftp {

namespace {

// ---- The park checks (decomp-onlinesrv's server/internal/park/park.go) ---------------------------------------------------------

constexpr size_t kOffLanguage = 4;
constexpr size_t kOffLegal = 5;
constexpr size_t kLegalSize = 0x500;
constexpr size_t kOffRides = kOffLegal + kLegalSize;   // 0x505
constexpr size_t kRidesSize = 0x100;
constexpr size_t kOffKey = kOffRides + kRidesSize;     // 0x605
constexpr size_t kOffFlag = kOffKey + 4;               // 0x609
constexpr size_t kOffHeader = kOffFlag + 4;            // 0x60d
constexpr size_t kHeaderSize = 0x23a;
constexpr size_t kBilzSize = 0x1c;
constexpr uint32_t kVersion = 500;
constexpr uint32_t kBilzTag = 0x5a4c4942;   // "BILZ" as a little endian word

uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// The BILZ block (CZLib::MemCompress's): its packed size counts its 0x1c-byte header; the stream inflates to exactly its unpacked
// size and ends with the block.
const char* CheckBilz(const uint8_t* b, size_t size) {
    if (size < kBilzSize) return "no BILZ block";
    if (Le32(b) != kBilzTag) return "no BILZ tag";
    const uint32_t unpacked = Le32(b + 4);
    if (Le32(b + 12) != 15 || Le32(b + 16) > 9 || Le32(b + 20) != 0 || Le32(b + 24) != 0) return "BILZ parameters";
    if (Le32(b + 8) != size) return "a BILZ packed size that is not the block's";
    if (unpacked > kMaxUnpacked) return "a BILZ unpacked size over the limit";
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return "the inflater could not start";
    z.next_in = const_cast<Bytef*>(b + kBilzSize);
    z.avail_in = static_cast<uInt>(size - kBilzSize);
    uint8_t out[0x4000];
    uint64_t total = 0;
    int r = Z_OK;
    while (r == Z_OK) {
        z.next_out = out;
        z.avail_out = sizeof out;
        r = inflate(&z, Z_NO_FLUSH);
        total += sizeof out - z.avail_out;
        if (total > unpacked) break;
        if (r == Z_BUF_ERROR && z.avail_in == 0) break;   // the stream is cut short
        if (r == Z_BUF_ERROR) r = Z_OK;
    }
    const uInt left = z.avail_in;
    inflateEnd(&z);
    if (total > unpacked) return "a BILZ stream longer than its unpacked size";
    if (r != Z_STREAM_END) return "a BILZ stream that does not inflate";
    if (total != unpacked) return "a BILZ stream shorter than its unpacked size";
    if (left != 0) return "bytes after the BILZ stream";
    return nullptr;
}

// ---- The settings --------------------------------------------------------------------------------------------------------------

struct Settings {
    Reports reports;
    LegalTexts legal;
    uint16_t port = 21;
    int idleMs = kIdleMs;
};

Settings& Set() {
    static Settings settings;
    return settings;
}

// ---- A link (one owner's connection and its thread) -------------------------------------------------------------------------

struct Command {
    int32_t state = 0;
    std::string a;   // Connect: the user; Download: the remote path
    std::string b;   // Connect: the password; Download: the local host path
};

struct Link {
    std::mutex lock;
    std::condition_variable wake;
    std::deque<Command> commands;
    std::deque<std::pair<int32_t, int32_t>> answers;   // done, not yet given: state, result
    std::vector<std::string> notes;                    // log lines, written on the game's thread
    int32_t done = 0;
    int32_t total = 0;
    bool moved = false;   // the progress changed since it was last given
    std::atomic<bool> stopping{false};
    // The thread's own
    std::string host;
    uint16_t port = 0;
    int idleMs = kIdleMs;
    LegalTexts legal;
};

std::map<const void*, std::shared_ptr<Link>>& Links() {
    static std::map<const void*, std::shared_ptr<Link>> links;
    return links;
}

void Note(Link& s, const std::string& text) {
    std::lock_guard<std::mutex> hold(s.lock);
    s.notes.push_back("online ftp: " + text);
}

void Progress(Link& s, int32_t done, int32_t total) {
    std::lock_guard<std::mutex> hold(s.lock);
    s.done = done;
    s.total = total;
    s.moved = true;
}

// A connection and the bytes read from it that are not yet a line.
struct Conn {
    std::unique_ptr<Transport> t;
    std::string pending;
    bool Open() const { return t != nullptr; }
    void Close() {
        if (t != nullptr) t->Close();
        t.reset();
        pending.clear();
    }
};

// Waits for the connection (readable, or writable with wantWrite): 1 when it is, 0 when the link stops, the connection fails or
// nothing came for the idle time (`why` says which).
bool Ready(Link& s, Conn& c, bool wantWrite, const char*& why) {
    int idle = 0;
    while (!s.stopping) {
        const int ready = c.t->Wait(wantWrite, 100);
        if (ready < 0) {
            why = "a wait failed";
            return false;
        }
        if ((ready & (wantWrite ? 2 : 1)) != 0) return true;
        idle += 100;
        if (idle >= s.idleMs) {
            why = "the server sent nothing for too long";
            return false;
        }
    }
    why = "the connection ended";
    return false;
}

// One reply line (CR LF stripped): 0, kErrorClosed (closed, failed or idle) or kErrorAnswer (a line over kMaxLine).
int32_t ReadLine(Link& s, Conn& c, std::string& line) {
    for (;;) {
        const size_t end = c.pending.find('\n');
        if (end != std::string::npos && end <= kMaxLine) {
            line = c.pending.substr(0, end);
            c.pending.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return 0;
        }
        if (c.pending.size() > kMaxLine) {
            Note(s, "a reply line over " + std::to_string(kMaxLine) + " bytes");
            return kErrorAnswer;
        }
        const char* why = nullptr;
        if (!Ready(s, c, false, why)) {
            Note(s, why);
            return kErrorClosed;
        }
        char bytes[0x400];
        const long n = c.t->Receive(reinterpret_cast<uint8_t*>(bytes), sizeof bytes);
        if (n == Transport::kWouldBlock) continue;
        if (n <= 0) {
            Note(s, n == 0 ? "the server closed the connection" : "a read failed");
            return kErrorClosed;
        }
        c.pending.append(bytes, static_cast<size_t>(n));
    }
}

// RecvResponse: a reply's code (a multi-line reply read to its last line), or 0 with `error` set.
int Reply(Link& s, Conn& c, int32_t& error, std::string* text = nullptr) {
    std::string line;
    error = ReadLine(s, c, line);
    if (error != 0) return 0;
    auto codeOf = [](const std::string& l) {
        if (l.size() < 3 || !std::isdigit(static_cast<unsigned char>(l[0])) || !std::isdigit(static_cast<unsigned char>(l[1])) ||
            !std::isdigit(static_cast<unsigned char>(l[2])))
            return 0;
        return (l[0] - '0') * 100 + (l[1] - '0') * 10 + (l[2] - '0');
    };
    const int code = codeOf(line);
    if (code == 0) {
        Note(s, "a reply with no code");
        error = kErrorAnswer;
        return 0;
    }
    if (text != nullptr) *text = line;
    if (line.size() > 3 && line[3] == '-') {
        for (int lines = 0;; ++lines) {
            if (lines == 64) {
                Note(s, "a reply of more than 64 lines");
                error = kErrorAnswer;
                return 0;
            }
            error = ReadLine(s, c, line);
            if (error != 0) return 0;
            if (codeOf(line) == code && (line.size() == 3 || line[3] == ' ')) break;
        }
    }
    return code;
}

// SendCommandLine: the line and CR LF; 0 or kErrorClosed.
int32_t SendLine(Link& s, Conn& c, const std::string& line) {
    const std::string bytes = line + "\r\n";
    size_t sent = 0;
    while (sent < bytes.size()) {
        const char* why = nullptr;
        if (!Ready(s, c, true, why)) {
            Note(s, why);
            return kErrorClosed;
        }
        const long n = c.t->Send(reinterpret_cast<const uint8_t*>(bytes.data()) + sent, bytes.size() - sent);
        if (n == Transport::kWouldBlock) continue;
        if (n < 0) {
            Note(s, "a write failed");
            return kErrorClosed;
        }
        sent += static_cast<size_t>(n);
    }
    return 0;
}

// A command and its reply's code (0 with `error` set when there is none).
int Ask(Link& s, Conn& c, const std::string& line, int32_t& error, std::string* text = nullptr) {
    error = SendLine(s, c, line);
    if (error != 0) return 0;
    return Reply(s, c, error, text);
}

bool HasBreak(const std::string& text) { return text.find_first_of(std::string("\r\n\0", 3)) != std::string::npos; }

// _Connect: the control connection, the greeting (220), USER (331 or 332: PASS, which must answer 230) and TYPE I (200).
int32_t DoConnect(Link& s, Conn& control, const Command& command) {
    control.Close();
    if (HasBreak(command.a) || HasBreak(command.b)) {
        Note(s, "a user or password holding a line break");
        return kErrorAnswer;
    }
    control.t = MakeTransport();
    if (control.t == nullptr || !control.t->Open(s.host, s.port, s.stopping)) {
        Note(s, "could not connect to " + s.host + ":" + std::to_string(s.port));
        control.Close();
        return kErrorConnect;
    }
    Note(s, "connected to " + s.host + ":" + std::to_string(s.port));
    int32_t error = 0;
    int32_t result = 0;
    int code = Reply(s, control, error);
    if (code == 0) {
        result = error;
    } else if (code != 220) {
        result = kErrorGreeting;
    } else if ((code = Ask(s, control, "USER " + command.a, error)) == 0) {
        result = error;
    } else if ((code == 331 || code == 332) && (code = Ask(s, control, "PASS " + command.b, error)) != 230) {
        result = code == 0 ? error : kErrorLogin;
    } else if ((code = Ask(s, control, "TYPE I", error)) != 200) {
        result = code == 0 ? error : kErrorAnswer;
    }
    if (result != 0) control.Close();
    return result;
}

// _EnterPassiveMode and OpenDataSocket: PASV (227), then the data connection to the answer's port on the control's host (the
// answer's address is not used).
int32_t OpenData(Link& s, Conn& control, Conn& data) {
    data.Close();
    int32_t error = 0;
    std::string text;
    const int code = Ask(s, control, "PASV", error, &text);
    if (code == 0) return error;
    if (code != 227) {
        Note(s, "PASV answered " + std::to_string(code));
        return kErrorAnswer;
    }
    const size_t open = text.find('(');
    unsigned v[6] = {};
    int fields = 0;
    if (open != std::string::npos) {
        const char* p = text.c_str() + open + 1;
        while (fields < 6) {
            if (!std::isdigit(static_cast<unsigned char>(*p))) break;
            unsigned value = 0;
            int digits = 0;
            while (std::isdigit(static_cast<unsigned char>(*p)) && digits < 4) {
                value = value * 10 + static_cast<unsigned>(*p++ - '0');
                ++digits;
            }
            if (value > 255) break;
            v[fields++] = value;
            if (fields < 6 && *p++ != ',') break;
        }
    }
    const uint16_t port = static_cast<uint16_t>(v[4] << 8 | v[5]);
    if (fields != 6 || port == 0) {
        Note(s, "a PASV answer with no address");
        return kErrorAnswer;
    }
    data.t = MakeTransport();
    if (data.t == nullptr || !data.t->Open(s.host, port, s.stopping)) {
        Note(s, "could not open the data connection to " + s.host + ":" + std::to_string(port));
        data.Close();
        return kErrorSocket;
    }
    return 0;
}

// RecvData: the data connection read until the server closes it; more than `most` bytes is kErrorAnswer.
int32_t ReadData(Link& s, Conn& data, std::vector<uint8_t>& out, size_t most, bool progress, int32_t total) {
    out.clear();
    uint8_t bytes[0x2000];
    for (;;) {
        const char* why = nullptr;
        if (!Ready(s, data, false, why)) {
            Note(s, why);
            return kErrorClosed;
        }
        const long n = data.t->Receive(bytes, sizeof bytes);
        if (n == Transport::kWouldBlock) continue;
        if (n < 0) {
            Note(s, "a data read failed");
            return kErrorClosed;
        }
        if (n == 0) return 0;
        if (out.size() + static_cast<size_t>(n) > most) {
            Note(s, "more data than " + std::to_string(most) + " bytes");
            return kErrorAnswer;
        }
        out.insert(out.end(), bytes, bytes + n);
        if (progress) Progress(s, static_cast<int32_t>(out.size()), total);
    }
}

bool IsGuidText(const std::string& text) {
    if (text.size() != 36) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? text[i] != '-' : !std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    return true;
}

// A park's paths: the remote's last part 36 GUID characters, the local file <first 19>/<last 17>.LAYS (LoggedInRequestFTPDownload's).
bool ParkPaths(const std::string& remote, const std::string& local) {
    if (HasBreak(remote) || HasBreak(local) || local.empty()) return false;
    const size_t slash = remote.rfind('/');
    const std::string guid = slash == std::string::npos ? remote : remote.substr(slash + 1);
    if (!IsGuidText(guid)) return false;
    const std::filesystem::path path(local);
    std::string name = tpw::PathText(path.filename());
    if (name.size() < 5) return false;
    std::string extension = name.substr(name.size() - 5);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (extension != ".LAYS") return false;
    name.resize(name.size() - 5);
    return tpw::PathText(path.parent_path().filename()) + name == guid;
}

// Received whole and checked: written beside its place and moved there.
bool Place(Link& s, const std::string& local, const std::vector<uint8_t>& bytes) {
    const std::filesystem::path path(local);
    std::filesystem::path part = path;
    part += ".part";
    std::error_code ec;
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            Note(s, "could not write " + tpw::PathText(part));
            std::filesystem::remove(part, ec);
            return false;
        }
    }
    std::filesystem::rename(part, path, ec);
    if (ec) {
        Note(s, "could not move the park to " + tpw::PathText(path) + ": " + ec.message());
        std::filesystem::remove(part, ec);
        return false;
    }
    return true;
}

// ParseFileSize: the fifth field of the listing line.
bool ListedSize(const std::vector<uint8_t>& listing, uint64_t& size) {
    const std::string text(listing.begin(), listing.end());
    size_t at = 0;
    for (int field = 0; field < 4; ++field) {
        while (at < text.size() && text[at] != ' ' && text[at] != '\t' && text[at] != '\r' && text[at] != '\n') ++at;
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) ++at;
    }
    size = 0;
    size_t digits = 0;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) && digits < 12) {
        size = size * 10 + static_cast<uint64_t>(text[at++] - '0');
        ++digits;
    }
    return digits != 0;
}

// _Download: LIST for the size (550: kErrorNoFile), RETR for the file, the checks, the file placed.
int32_t DoDownload(Link& s, Conn& control, const Command& command) {
    Conn data;
    if (!control.Open()) return kErrorClosed;
    if (!ParkPaths(command.a, command.b)) {
        Note(s, "refused a download that is not a park (" + command.a + ")");
        return kErrorNoFile;
    }
    int32_t error = OpenData(s, control, data);
    if (error != 0) return error;
    Progress(s, 0, 0);
    int code = Ask(s, control, "LIST " + command.a, error);
    if (code == 0) return error;
    if (code == 550) {
        Note(s, command.a + " is not on the server");
        return kErrorNoFile;
    }
    if (code != 150 && code != 125) return kErrorAnswer;
    std::vector<uint8_t> listing;
    error = ReadData(s, data, listing, kMaxLine, false, 0);
    data.Close();
    if (error != 0) return error;
    code = Reply(s, control, error);
    if (code == 0) return error;
    if (code != 226 && code != 250) return kErrorNoFile;
    uint64_t size = 0;
    if (!ListedSize(listing, size)) {
        Note(s, "a listing with no size");
        return kErrorNoFile;
    }
    if (size > kMaxPark) {
        Note(s, "a park of " + std::to_string(size) + " bytes");
        return kErrorAnswer;
    }
    error = OpenData(s, control, data);
    if (error != 0) return error;
    code = Ask(s, control, "RETR " + command.a, error);
    if (code == 0) return error;
    if (code == 550) return kErrorNoFile;
    if (code != 150 && code != 125) return kErrorAnswer;
    Progress(s, 0, static_cast<int32_t>(size));
    std::vector<uint8_t> bytes;
    error = ReadData(s, data, bytes, static_cast<size_t>(size), true, static_cast<int32_t>(size));
    data.Close();
    if (error != 0) return error;
    code = Reply(s, control, error);
    if (code == 0) return error;
    if (code != 226 && code != 250) return kErrorNoFile;
    if (bytes.size() != size) {
        Note(s, "received " + std::to_string(bytes.size()) + " of " + std::to_string(size) + " bytes");
        return kErrorClosed;
    }
    if (const char* why = CheckPark(bytes.data(), bytes.size(), s.legal)) {
        Note(s, "refused the park " + command.a + ": " + why);
        return kErrorAnswer;
    }
    if (!Place(s, command.b, bytes)) return kErrorSocket;
    Note(s, "downloaded " + command.a + " (" + std::to_string(bytes.size()) + " bytes)");
    return 0;
}

// ProcessEvents' state 2: QUIT (221), then the connection closed.
int32_t DoQuit(Link& s, Conn& control) {
    if (!control.Open()) return kErrorClosed;
    int32_t error = 0;
    const int code = Ask(s, control, "QUIT", error);
    if (code != 0 && code != 221) return kErrorAnswer;
    control.Close();
    return 0;
}

// ProcessEvents: each command in order, its answer kept for Pump, until the connection ends.
void Run(std::shared_ptr<Link> s) {
    Conn control;
    for (;;) {
        Command command;
        {
            std::unique_lock<std::mutex> hold(s->lock);
            s->wake.wait(hold, [&] { return s->stopping || !s->commands.empty(); });
            if (s->stopping) break;
            command = std::move(s->commands.front());
            s->commands.pop_front();
        }
        int32_t result = 0;
        if (command.state == kConnect) result = DoConnect(*s, control, command);
        else if (command.state == kDownload) result = DoDownload(*s, control, command);
        else result = DoQuit(*s, control);
        std::lock_guard<std::mutex> hold(s->lock);
        s->answers.emplace_back(command.state, result);
    }
    control.Close();
}

void Queue(void* owner, Command command) {
    auto it = Links().find(owner);
    if (it == Links().end() || it->second->stopping) {
        // A connection with no thread (stopping) only holds answers to give: a Connect starts a thread after them.
        auto s = std::make_shared<Link>();
        if (it != Links().end()) s->answers = std::move(it->second->answers);
        if (command.state != kConnect) {
            // Not connected: the answer the Mac's gave for a closed control connection.
            s->answers.emplace_back(command.state, kErrorClosed);
            s->stopping = true;
            Links()[owner] = s;
            return;
        }
        s->host = Server();
        s->port = Set().port;
        s->idleMs = Set().idleMs;
        s->legal = Set().legal;
        Links()[owner] = s;
        it = Links().find(owner);
        std::thread(Run, s).detach();
    }
    Link& s = *it->second;
    std::lock_guard<std::mutex> hold(s.lock);
    s.commands.push_back(std::move(command));
    s.wake.notify_all();
}

}  // namespace

const char* CheckPark(const uint8_t* b, size_t size, const LegalTexts& legal) {
    if (size > kMaxPark) return "a park over the size limit";
    if (size < kOffHeader + kBilzSize) return "shorter than a park's fixed part";
    if (Le32(b) != kVersion) return "not version 500";
    const uint8_t language = b[kOffLanguage];
    if (language >= 7) return "a language the game does not have";
    const std::u16string& text = legal.text[language];
    if (text.empty() || text.size() > kLegalSize / 2) return "no legal text to compare";
    uint8_t block[kLegalSize] = {};
    for (size_t i = 0; i < text.size(); ++i) {
        block[i * 2] = static_cast<uint8_t>(text[i]);
        block[i * 2 + 1] = static_cast<uint8_t>(text[i] >> 8);
    }
    if (std::memcmp(b + kOffLegal, block, kLegalSize) != 0) return "a legal text that is not the game's";
    for (size_t i = 0x20; i < kRidesSize; ++i)
        if (b[kOffRides + i] != 0) return "a ride record byte past 0x20 that is not 0";
    static const uint8_t kKey[4] = {0x01, 0x22, 0x19, 0x85};
    if (std::memcmp(b + kOffKey, kKey, 4) != 0) return "not the game's key";
    size_t at = kOffHeader;
    const uint32_t flag = Le32(b + kOffFlag);
    if (flag == 1) {
        if (size < kOffHeader + kHeaderSize + kBilzSize) return "too short for its header";
        at += kHeaderSize;
    } else if (flag != 0) {
        return "a header flag that is not 0 or 1";
    }
    return CheckBilz(b + at, size - at);
}

void SetReports(const Reports& reports) { Set().reports = reports; }
void SetLegalTexts(const LegalTexts& legal) { Set().legal = legal; }
void SetPort(uint16_t port) { Set().port = port; }
void SetIdleMs(int milliseconds) { Set().idleMs = milliseconds; }

void Connect(void* owner, const std::string& user, const std::string& password) {
    Queue(owner, Command{kConnect, user, password});
}

void Download(void* owner, const std::string& remote, const std::string& localHostPath) {
    Queue(owner, Command{kDownload, remote, localHostPath});
}

void Quit(void* owner) { Queue(owner, Command{kQuit, {}, {}}); }

void End(void* owner) {
    auto it = Links().find(owner);
    if (it == Links().end()) return;
    {
        std::lock_guard<std::mutex> hold(it->second->lock);
        it->second->stopping = true;
        it->second->wake.notify_all();
    }
    Links().erase(it);
}

bool Has(const void* owner) { return Links().count(owner) != 0; }

void Pump() {
    std::vector<std::pair<void*, std::shared_ptr<Link>>> links;
    for (auto& entry : Links()) links.emplace_back(const_cast<void*>(entry.first), entry.second);
    for (auto& [owner, s] : links) {
        std::deque<std::pair<int32_t, int32_t>> answers;
        std::vector<std::string> notes;
        bool moved;
        int32_t done;
        int32_t total;
        {
            std::lock_guard<std::mutex> hold(s->lock);
            answers.swap(s->answers);
            notes.swap(s->notes);
            moved = s->moved;
            s->moved = false;
            done = s->done;
            total = s->total;
        }
        for (const std::string& note : notes) tpw::Log("%s", note.c_str());
        const Reports reports = Set().reports;
        if (moved && reports.progress != nullptr) reports.progress(owner, done, total);
        for (const auto& [state, result] : answers) {
            auto it = Links().find(owner);
            if (it == Links().end() || it->second != s) break;   // ended by an earlier answer's report
            tpw::Log("online ftp: state %d answered %d", static_cast<int>(state), static_cast<int>(result));
            if (reports.done != nullptr) reports.done(owner, state, result);
        }
    }
}

}  // namespace tpw::online::boss::ftp
