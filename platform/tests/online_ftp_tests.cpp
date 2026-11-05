// online_ftp_tests.cpp: the online server's park downloads (platform/online_ftp.cpp, include/platform/online_ftp.h) against fake
// connections that play the server's FTP service (decomp-onlinesrv's server/PROTOCOL.md section 7): the commands of a connect, a
// download and a quit, in order; the host every connection goes to (the server set, never the passive answer's address); each
// error a step answers (unreachable, a wrong greeting or login, a missing file, a refused path, a bad passive answer, a line or a
// file over its bound, more or fewer bytes than listed, a silent server, no connection); the file placed only when whole and
// checked; and the park checks (CheckPark) on parks built here, valid and malformed. No window, no game and no network.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
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

#include <zlib.h>

#include "platform.h"
#include "platform/online_boss.h"
#include "platform/online_ftp.h"

namespace fs = std::filesystem;
namespace boss = tpw::online::boss;
namespace ftp = tpw::online::boss::ftp;
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

// ---- Parks as the game saves them --------------------------------------------------------------------------------------------

const std::u16string kLegal = u"Theme Park World (c) test legal text";

void Le32(Bytes& b, uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) b.push_back(static_cast<uint8_t>(v >> shift));
}

void PutLe32(Bytes& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (i * 8));
}

Bytes Modules(size_t size) {
    Bytes out(size);
    for (size_t i = 0; i < size; ++i) out[i] = static_cast<uint8_t>((i * 7) ^ (i >> 5));
    return out;
}

Bytes Deflate(const Bytes& in) {
    uLongf size = compressBound(static_cast<uLong>(in.size()));
    Bytes out(size);
    compress2(out.data(), &size, in.data(), static_cast<uLong>(in.size()), 9);
    out.resize(size);
    return out;
}

// The BILZ block: tag, unpacked size, packed size (with the header), 15, 9, 0, 0, the stream.
Bytes Bilz(const Bytes& modules, const Bytes& stream) {
    Bytes b;
    Le32(b, 0x5a4c4942);
    Le32(b, static_cast<uint32_t>(modules.size()));
    Le32(b, static_cast<uint32_t>(0x1c + stream.size()));
    Le32(b, 15);
    Le32(b, 9);
    Le32(b, 0);
    Le32(b, 0);
    b.insert(b.end(), stream.begin(), stream.end());
    return b;
}

// A park: version 500, language 0, the legal text, a ride record with ride 1, the key, the flag and (flag 1) a header, the block.
Bytes Park(bool header = false, size_t modules = 5000) {
    Bytes b;
    Le32(b, 500);
    b.push_back(0);
    for (size_t i = 0; i < 0x280; ++i) {
        const char16_t unit = i < kLegal.size() ? kLegal[i] : 0;
        b.push_back(static_cast<uint8_t>(unit));
        b.push_back(static_cast<uint8_t>(unit >> 8));
    }
    Bytes rides(0x100, 0);
    rides[0] = 1;
    b.insert(b.end(), rides.begin(), rides.end());
    for (uint8_t k : {0x01, 0x22, 0x19, 0x85}) b.push_back(k);
    Le32(b, header ? 1 : 0);
    if (header) b.insert(b.end(), 0x23a, 0x41);
    const Bytes m = Modules(modules);
    const Bytes block = Bilz(m, Deflate(m));
    b.insert(b.end(), block.begin(), block.end());
    return b;
}

ftp::LegalTexts Legal() {
    ftp::LegalTexts legal;
    for (std::u16string& text : legal.text) text = kLegal;
    return legal;
}

void TestCheckPark() {
    const ftp::LegalTexts legal = Legal();
    auto check = [&](const Bytes& b) { return ftp::CheckPark(b.data(), b.size(), legal); };
    auto refused = [&](const Bytes& b, const std::string& what) { Check(check(b) != nullptr, "CheckPark refuses " + what); };
    Check(check(Park()) == nullptr, "CheckPark takes a park");
    Check(check(Park(true)) == nullptr, "CheckPark takes a park with its header");
    Check(check(Park(false, 0)) == nullptr, "CheckPark takes a park of no modules");

    const Bytes good = Park();
    const size_t block = 0x60d;
    Bytes b = good;
    b.resize(block + 0x1b);
    refused(b, "a park shorter than its fixed part");
    b = good;
    PutLe32(b, 0, 499);
    refused(b, "version 499");
    b = good;
    b[4] = 7;
    refused(b, "language 7");
    b = good;
    b[4] = 0xff;
    refused(b, "language 255");
    b = good;
    b[5] ^= 1;
    refused(b, "a legal text not the game's");
    b = good;
    b[5 + kLegal.size() * 2] = 'x';
    refused(b, "a legal text with more after it");
    b = good;
    b[0x505 + 0x20] = 1;
    refused(b, "ride record byte 0x20");
    b = good;
    b[0x505 + 0xff] = 1;
    refused(b, "ride record byte 0xff");
    b = good;
    b[0x605] = 0x85;
    b[0x606] = 0x19;
    b[0x607] = 0x22;
    b[0x608] = 0x01;
    refused(b, "the PC key");
    b = good;
    PutLe32(b, 0x609, 2);
    refused(b, "header flag 2");
    b = good;
    PutLe32(b, 0x609, 1);
    refused(b, "header flag 1 without the room for its header");
    b = good;
    b[block] = 'X';
    refused(b, "a BILZ block without its tag");
    b = good;
    PutLe32(b, block + 12, 14);
    refused(b, "BILZ window 14");
    b = good;
    PutLe32(b, block + 16, 10);
    refused(b, "BILZ level 10");
    b = good;
    PutLe32(b, block + 20, 1);
    refused(b, "BILZ word 5 not 0");
    b = good;
    PutLe32(b, block + 24, 1);
    refused(b, "BILZ word 6 not 0");
    b = good;
    PutLe32(b, block + 8, static_cast<uint32_t>(b.size() - block - 1));
    refused(b, "a BILZ packed size short of the block");
    b = good;
    b.push_back(0);
    refused(b, "a byte after the BILZ stream");
    b = good;
    PutLe32(b, block + 4, 4999);
    refused(b, "a BILZ unpacked size one short");
    b = good;
    PutLe32(b, block + 4, 5001);
    refused(b, "a BILZ unpacked size one over");
    b = good;
    PutLe32(b, block + 4, ftp::kMaxUnpacked + 1);
    refused(b, "a BILZ unpacked size over the limit");
    b = good;
    b.resize(b.size() - 10);
    PutLe32(b, block + 8, static_cast<uint32_t>(b.size() - block));
    refused(b, "a cut BILZ stream");
    b = good;
    b[b.size() - 1] ^= 0xff;   // the stream's checksum
    refused(b, "a BILZ stream with a wrong checksum");
    b = good;
    for (size_t i = block + 0x1c; i < b.size(); ++i) b[i] = 0xa5;
    refused(b, "a BILZ stream of noise");
    {
        // A stream of a bomb's shape (a large output from a small input) stops at the declared size.
        const Bytes big(ftp::kMaxUnpacked, 0);
        Bytes bomb = good;
        bomb.resize(block);
        const Bytes blk = Bilz(Modules(10), Deflate(big));
        bomb.insert(bomb.end(), blk.begin(), blk.end());
        refused(bomb, "a BILZ stream that inflates past its unpacked size");
    }
    b = good;
    b.resize(ftp::kMaxPark + 1);
    refused(b, "a park over the size limit");
    ftp::LegalTexts none;
    Check(ftp::CheckPark(good.data(), good.size(), none) != nullptr, "CheckPark refuses a park when it has no legal text");
}

// ---- The fake server ---------------------------------------------------------------------------------------------------------

// A data connection's bytes: given once the command that fills it (LIST, RETR) is answered.
struct DataSlot {
    std::mutex lock;
    Bytes bytes;
    size_t at = 0;
    bool filled = false;
};

struct Server {
    std::mutex lock;
    std::string greeting = "220 tpw-online ready\r\n";
    bool refuseControl = false;
    bool refuseData = false;
    std::map<std::string, Bytes> files;
    std::string pasv = "227 Entering Passive Mode (10,9,8,7,156,65)\r\n";   // port 40001, on an address never used
    // A reply in place of the usual one (by the command's word); "" sends none.
    std::map<std::string, std::string> replies;
    // The listed size in place of the file's, and bytes given in place of the file's.
    long long listedSize = -1;
    std::string listing;   // a listing line in place of the usual one
    bool sendOther = false;
    Bytes other;
    std::vector<std::string> commands;
    std::vector<std::string> opened;   // host:port of each connection
    std::shared_ptr<DataSlot> slot;
    int controls = 0;
};

Server* gServer = nullptr;

class FakeControl;

class FakeData : public boss::Transport {
public:
    bool Open(const std::string& host, uint16_t port, const std::atomic<bool>&) override {
        std::lock_guard<std::mutex> hold(gServer->lock);
        gServer->opened.push_back(host + ":" + std::to_string(port));
        if (gServer->refuseData || port != 40001) return false;
        mSlot = gServer->slot;
        return mSlot != nullptr;
    }
    int Wait(bool wantWrite, int milliseconds) override {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            {
                std::lock_guard<std::mutex> hold(mSlot->lock);
                if (mSlot->filled) return 1 | (wantWrite ? 2 : 0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < end);
        return 0;
    }
    long Send(const uint8_t*, size_t) override { return -1; }
    long Receive(uint8_t* bytes, size_t size) override {
        std::lock_guard<std::mutex> hold(mSlot->lock);
        if (!mSlot->filled) return kWouldBlock;
        const size_t n = std::min(size, mSlot->bytes.size() - mSlot->at);
        std::memcpy(bytes, mSlot->bytes.data() + mSlot->at, n);
        mSlot->at += n;
        return static_cast<long>(n);
    }
    void Close() override {}

private:
    std::shared_ptr<DataSlot> mSlot;
};

// The control connection: the greeting at once, each command line answered as the server answers it.
class FakeControl : public boss::Transport {
public:
    bool Open(const std::string& host, uint16_t port, const std::atomic<bool>&) override {
        std::lock_guard<std::mutex> hold(gServer->lock);
        gServer->opened.push_back(host + ":" + std::to_string(port));
        ++gServer->controls;
        if (gServer->refuseControl) return false;
        mOut = gServer->greeting;
        return true;
    }
    int Wait(bool wantWrite, int milliseconds) override {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            {
                std::lock_guard<std::mutex> hold(mLock);
                const int ready = (!mOut.empty() ? 1 : 0) | (wantWrite ? 2 : 0);
                if (ready != 0) return ready;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < end);
        return 0;
    }
    long Send(const uint8_t* bytes, size_t size) override {
        std::lock_guard<std::mutex> hold(mLock);
        mIn.append(reinterpret_cast<const char*>(bytes), size);
        size_t end;
        while ((end = mIn.find("\r\n")) != std::string::npos) {
            const std::string line = mIn.substr(0, end);
            mIn.erase(0, end + 2);
            mOut += Answer(line);
        }
        return static_cast<long>(size);
    }
    long Receive(uint8_t* bytes, size_t size) override {
        std::lock_guard<std::mutex> hold(mLock);
        if (mOut.empty()) return kWouldBlock;
        const size_t n = std::min(size, mOut.size());
        std::memcpy(bytes, mOut.data(), n);
        mOut.erase(0, n);
        return static_cast<long>(n);
    }
    void Close() override {}

private:
    static void Fill(const Bytes& bytes) {
        std::lock_guard<std::mutex> hold(gServer->slot->lock);
        gServer->slot->bytes = bytes;
        gServer->slot->filled = true;
    }
    std::string Answer(const std::string& line) {
        std::lock_guard<std::mutex> hold(gServer->lock);
        gServer->commands.push_back(line);
        const std::string word = line.substr(0, line.find(' '));
        const std::string arg = line.find(' ') == std::string::npos ? "" : line.substr(line.find(' ') + 1);
        auto replaced = gServer->replies.find(word);
        if (replaced != gServer->replies.end()) return replaced->second;
        if (word == "USER") return "331 send the e-mail\r\n";
        if (word == "PASS") return "230 logged in\r\n";
        if (word == "TYPE") return "200 binary\r\n";
        if (word == "QUIT") return "221 bye\r\n";
        if (word == "PASV") {
            gServer->slot = std::make_shared<DataSlot>();
            return gServer->pasv;
        }
        auto file = gServer->files.find(arg);
        if (word == "LIST" || word == "RETR") {
            if (file == gServer->files.end()) return "550 no such file\r\n";
            if (gServer->slot == nullptr) return "425 no data connection\r\n";
            if (word == "LIST") {
                const long long size = gServer->listedSize >= 0 ? gServer->listedSize : static_cast<long long>(file->second.size());
                const std::string text = !gServer->listing.empty() ? gServer->listing
                                         : "-r--r--r-- 1 tpw tpw " + std::to_string(size) + " Jan  1 2000 " + arg + "\r\n";
                Fill(Bytes(text.begin(), text.end()));
            } else {
                Fill(gServer->sendOther ? gServer->other : file->second);
            }
            gServer->slot = nullptr;
            return "150 opening\r\n226 done\r\n";
        }
        return "502 not here\r\n";
    }

    std::mutex mLock;
    std::string mIn;
    std::string mOut;
};

// The client opens its control connection first, then a data connection after each PASV.
std::unique_ptr<boss::Transport> MakeFake() {
    std::lock_guard<std::mutex> hold(gServer->lock);
    if (gServer->slot != nullptr) return std::make_unique<FakeData>();
    return std::make_unique<FakeControl>();
}

// ---- The answers -------------------------------------------------------------------------------------------------------------

struct Answer {
    void* owner;
    int32_t state;
    int32_t result;
};
std::vector<Answer> gAnswers;
int32_t gDone = -1;
int32_t gTotal = -1;
int gProgressReports = 0;

void OnProgress(void*, int32_t done, int32_t total) {
    gDone = done;
    gTotal = total;
    ++gProgressReports;
}
void OnDone(void* owner, int32_t state, int32_t result) { gAnswers.push_back(Answer{owner, state, result}); }

// Pumps until `count` answers came (at most 5 s).
bool PumpFor(size_t count) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (gAnswers.size() < count && std::chrono::steady_clock::now() < end) {
        boss::Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return gAnswers.size() >= count;
}

const std::string kGuid = "8DC53763-C78A-3DAD-18AA-DD1190BF5155";
const std::string kRemote = "/parks/Wonderland/" + kGuid;

// Each test's server is kept to the end: a connection's thread may still be ending when the next test starts.
struct Fixture {
    Server& server = *new Server;
    int owner = 0;
    fs::path folder;
    std::string local;
    explicit Fixture(const fs::path& root) {
        gServer = &server;
        gAnswers.clear();
        gDone = gTotal = -1;
        gProgressReports = 0;
        server.files[kRemote] = Park();
        folder = root / kGuid.substr(0, 19);
        fs::create_directories(folder);
        local = (folder / (kGuid.substr(19) + ".LAYS")).string();
        std::error_code ec;
        fs::remove(local, ec);
    }
    ~Fixture() {
        ftp::End(&owner);
        boss::Pump();
    }
    bool Connected() {
        ftp::Connect(&owner, "anonymous", "player@example.test");
        return PumpFor(1) && gAnswers[0].state == ftp::kConnect && gAnswers[0].result == 0;
    }
    // The download's answer (-1: none came).
    int32_t Download(const std::string& remote, const std::string& to) {
        const size_t before = gAnswers.size();
        ftp::Download(&owner, remote, to);
        if (!PumpFor(before + 1)) return -1;
        return gAnswers[before].state == ftp::kDownload ? gAnswers[before].result : -2;
    }
};

Bytes ReadAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool NoPart(const std::string& local) { return !fs::exists(local + ".part"); }

void TestDownload(const fs::path& root) {
    Fixture f(root);
    Check(f.Connected(), "connect: answered 0");
    const int32_t result = f.Download(kRemote, f.local);
    Check(result == 0, "download: answered 0 (" + std::to_string(result) + ")");
    Check(ReadAll(f.local) == f.server.files[kRemote], "download: the park written whole");
    Check(NoPart(f.local), "download: nothing left beside it");
    const int32_t size = static_cast<int32_t>(f.server.files[kRemote].size());
    Check(gDone == size && gTotal == size && gProgressReports > 0, "download: the progress given, at its end");
    ftp::Quit(&f.owner);
    Check(PumpFor(3) && gAnswers[2].state == ftp::kQuit && gAnswers[2].result == 0, "quit: answered 0");
    const std::vector<std::string> expected = {"USER anonymous", "PASS player@example.test", "TYPE I", "PASV", "LIST " + kRemote,
                                               "PASV", "RETR " + kRemote, "QUIT"};
    {
        std::lock_guard<std::mutex> hold(f.server.lock);
        Check(f.server.commands == expected, "the commands, in order");
        bool hosts = f.server.opened.size() == 3;
        for (const std::string& opened : f.server.opened) hosts = hosts && opened.rfind("tpw.test:", 0) == 0;
        Check(hosts, "every connection goes to the server set");
        Check(f.server.opened.size() == 3 && f.server.opened[0] == "tpw.test:2121" && f.server.opened[1] == "tpw.test:40001",
              "the control on the port set, the data on the passive answer's port");
    }
    // The same owner downloads again after a quit: not connected (the Mac's 0x38).
    Check(f.Download(kRemote, f.local) == ftp::kErrorClosed, "a download after the quit: closed");
    for (const Answer& a : gAnswers) Check(a.owner == &f.owner, "every answer goes to its owner");
}

void TestDownloadWithHeaderAndReplace(const fs::path& root) {
    Fixture f(root);
    f.server.files[kRemote] = Park(true);
    {
        std::ofstream old(f.local, std::ios::binary);
        old << "an older copy";
    }
    Check(f.Connected(), "replace: connected");
    Check(f.Download(kRemote, f.local) == 0, "replace: answered 0");
    Check(ReadAll(f.local) == f.server.files[kRemote], "replace: the older copy replaced");
}

void TestConnectErrors(const fs::path& root) {
    {
        Fixture f(root);
        f.server.refuseControl = true;
        ftp::Connect(&f.owner, "anonymous", "e");
        Check(PumpFor(1) && gAnswers[0].result == ftp::kErrorConnect, "unreachable: 0x40");
    }
    {
        Fixture f(root);
        f.server.greeting = "421 too many\r\n";
        ftp::Connect(&f.owner, "anonymous", "e");
        Check(PumpFor(1) && gAnswers[0].result == ftp::kErrorGreeting, "a greeting not 220: 0x41");
    }
    {
        Fixture f(root);
        f.server.replies["PASS"] = "530 no\r\n";
        ftp::Connect(&f.owner, "anonymous", "e");
        Check(PumpFor(1) && gAnswers[0].result == ftp::kErrorLogin, "a password refused: 0x42");
    }
    {
        Fixture f(root);
        f.server.replies["TYPE"] = "504 no\r\n";
        ftp::Connect(&f.owner, "anonymous", "e");
        Check(PumpFor(1) && gAnswers[0].result == ftp::kErrorAnswer, "TYPE refused: 0x43");
    }
    {
        Fixture f(root);
        f.server.replies["USER"] = "230 in already\r\n";
        Check(f.Connected(), "USER answering 230: connected");
        std::lock_guard<std::mutex> hold(f.server.lock);
        Check(f.server.commands.size() == 2 && f.server.commands[1] == "TYPE I", "USER answering 230: no PASS");
    }
    {
        Fixture f(root);
        f.server.greeting = "220-welcome\r\n220-to the server\r\n220 ready\r\n";
        Check(f.Connected(), "a multi-line greeting: connected");
    }
    {
        Fixture f(root);
        f.server.greeting = std::string(ftp::kMaxLine + 1, 'x');
        ftp::Connect(&f.owner, "anonymous", "e");
        Check(PumpFor(1) && gAnswers[0].result == ftp::kErrorAnswer, "a line over its bound: 0x43");
    }
    {
        Fixture f(root);
        f.server.greeting = "hello\r\n";
        ftp::Connect(&f.owner, "anonymous", "e");
        Check(PumpFor(1) && gAnswers[0].result == ftp::kErrorAnswer, "a reply with no code: 0x43");
    }
    {
        Fixture f(root);
        f.server.greeting = "";
        ftp::SetIdleMs(300);
        const auto start = std::chrono::steady_clock::now();
        ftp::Connect(&f.owner, "anonymous", "e");
        const bool answered = PumpFor(1);
        const auto took = std::chrono::steady_clock::now() - start;
        ftp::SetIdleMs(ftp::kIdleMs);
        Check(answered && gAnswers[0].result == ftp::kErrorClosed, "a silent server: closed");
        Check(took < std::chrono::seconds(3), "a silent server: given up after the idle time");
    }
    {
        Fixture f(root);
        ftp::Connect(&f.owner, "anonymous", "e\r\nDELE x");
        Check(PumpFor(1) && gAnswers[0].result == ftp::kErrorAnswer, "a password with a line break: refused");
        std::lock_guard<std::mutex> hold(f.server.lock);
        Check(f.server.controls == 0, "a password with a line break: no connection made");
    }
}

void TestDownloadErrors(const fs::path& root) {
    auto expect = [&](const std::string& what, int32_t expected, const std::function<void(Fixture&)>& set,
                      const std::string& remote = kRemote, bool nothingSent = false) {
        Fixture f(root);
        set(f);
        if (!f.Connected()) {
            Check(false, what + ": connected");
            return;
        }
        const size_t sentBefore = f.server.commands.size();
        const int32_t result = f.Download(remote, f.local);
        Check(result == expected, what + ": " + std::to_string(expected) + " (" + std::to_string(result) + ")");
        Check(!fs::exists(f.local) && NoPart(f.local), what + ": no file written");
        if (nothingSent) {
            std::lock_guard<std::mutex> hold(f.server.lock);
            Check(f.server.commands.size() == sentBefore, what + ": nothing sent");
        }
    };
    auto none = [](Fixture&) {};
    expect("a missing file", ftp::kErrorNoFile, [](Fixture& f) { f.server.files.clear(); });
    expect("a remote that is not a GUID", ftp::kErrorNoFile, none, "/parks/Wonderland/../../etc/passwd", true);
    expect("a remote with a line break", ftp::kErrorNoFile, none, kRemote + "\r\nDELE x", true);
    expect("a remote whose GUID is not the local name", ftp::kErrorNoFile, none,
           "/parks/Wonderland/8DC53763-C78A-3DAD-18AA-DD1190BF5156", true);
    expect("a bad park", ftp::kErrorAnswer, [](Fixture& f) { f.server.files[kRemote][4] = 9; });
    expect("a listed size over the limit", ftp::kErrorAnswer, [](Fixture& f) { f.server.listedSize = ftp::kMaxPark + 1LL; });
    expect("more bytes than listed", ftp::kErrorAnswer, [](Fixture& f) {
        f.server.sendOther = true;
        f.server.other = f.server.files[kRemote];
        f.server.other.push_back(0);
    });
    expect("fewer bytes than listed", ftp::kErrorClosed, [](Fixture& f) {
        f.server.sendOther = true;
        f.server.other = f.server.files[kRemote];
        f.server.other.pop_back();
    });
    expect("a listing with no size", ftp::kErrorNoFile, [](Fixture& f) { f.server.listing = "-r--r--r-- 1 tpw tpw\r\n"; });
    expect("PASV not 227", ftp::kErrorAnswer, [](Fixture& f) { f.server.replies["PASV"] = "502 no passive\r\n"; });
    expect("a PASV answer with no address", ftp::kErrorAnswer,
           [](Fixture& f) { f.server.pasv = "227 Entering Passive Mode\r\n"; });
    expect("a PASV answer with a part over 255", ftp::kErrorAnswer,
           [](Fixture& f) { f.server.pasv = "227 Entering Passive Mode (10,9,8,7,256,65)\r\n"; });
    expect("a data connection refused", ftp::kErrorSocket, [](Fixture& f) { f.server.refuseData = true; });
    expect("RETR refused", ftp::kErrorAnswer, [](Fixture& f) { f.server.replies["RETR"] = "425 no\r\n"; });
    {
        // Not connected at all: closed, and no connection made.
        Fixture f(root);
        Check(f.Download(kRemote, f.local) == ftp::kErrorClosed, "a download with no connection: closed");
        std::lock_guard<std::mutex> hold(f.server.lock);
        Check(f.server.controls == 0, "a download with no connection: nothing opened");
    }
    {
        // A local file of another shape is refused too.
        Fixture f(root);
        Check(f.Connected(), "local name: connected");
        Check(f.Download(kRemote, (f.folder / "other.LAYS").string()) == ftp::kErrorNoFile, "a local name that is not the GUID");
        Check(f.Download(kRemote, f.local.substr(0, f.local.size() - 5) + ".SAV") == ftp::kErrorNoFile, "a local name not .LAYS");
    }
}

void TestEnd(const fs::path& root) {
    Fixture f(root);
    ftp::Connect(&f.owner, "anonymous", "e");
    Check(ftp::Has(&f.owner), "a connection after Connect");
    ftp::End(&f.owner);
    Check(!ftp::Has(&f.owner), "no connection after End");
    for (int i = 0; i < 50; ++i) {
        boss::Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(gAnswers.empty(), "an ended connection's answers are dropped");
    // A report that ends the connection drops the answers after it.
    ftp::Connect(&f.owner, "anonymous", "e");
    ftp::Quit(&f.owner);
    ftp::SetReports(ftp::Reports{OnProgress, [](void* owner, int32_t state, int32_t result) {
                                     OnDone(owner, state, result);
                                     ftp::End(owner);
                                 }});
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (ftp::Has(&f.owner) && std::chrono::steady_clock::now() < end) {
        boss::Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (int i = 0; i < 50; ++i) {
        boss::Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ftp::SetReports(ftp::Reports{OnProgress, OnDone});
    Check(gAnswers.size() == 1 && gAnswers[0].state == ftp::kConnect, "answers after a report's End are dropped");
}

}  // namespace

int main() {
#ifndef _WIN32
    const int pid = static_cast<int>(getpid());
#else
    const int pid = _getpid();
#endif
    const fs::path root = fs::temp_directory_path() / ("tpw-online-ftp-" + std::to_string(pid));
    std::error_code ec;
    fs::create_directories(root, ec);

    TestCheckPark();
    Server idle;
    gServer = &idle;
    boss::SetTransportMaker(MakeFake);
    boss::SetServer("tpw.test");
    ftp::SetPort(2121);
    ftp::SetLegalTexts(Legal());
    ftp::SetReports(ftp::Reports{OnProgress, OnDone});
    TestDownload(root);
    TestDownloadWithHeaderAndReplace(root);
    TestConnectErrors(root);
    TestDownloadErrors(root);
    TestEnd(root);
    boss::SetServer("");
    boss::SetTransportMaker(nullptr);
    // The detached connections' threads see their end within a wait.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    fs::remove_all(root, ec);

    std::printf("online_ftp: %d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
