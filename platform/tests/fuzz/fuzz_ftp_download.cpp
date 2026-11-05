// fuzz_ftp_download.cpp: libFuzzer target for the park download's FTP steps (platform/online_ftp.cpp) against a server that
// answers with the input. The client's file is compiled in here, so a connect and a download run on this thread, with fake
// connections in place of sockets. The input is split at its first two 0xff bytes into three parts: the control connection's
// replies, the listing the first data connection gives, and the park file the second gives; each connection gives its bytes in
// chunks of the size the input's first byte names. A park that passes every check is written to TPW_FUZZ_SCRATCH (the build
// folder) and removed. ParkPaths and ListedSize are also given the input's parts directly.
#include "../../online_ftp.cpp"

#include <cstdio>
#include <cstdlib>

namespace {

namespace boss = tpw::online::boss;
namespace ftp = tpw::online::boss::ftp;
using namespace tpw::online::boss::ftp;
using FtpLink = Link;

class FakeTransport final : public boss::Transport {
public:
    FakeTransport(std::string bytes, uint8_t chunk) : mBytes(std::move(bytes)), mChunk(chunk == 0 ? 1 : chunk) {}
    bool Open(const std::string&, uint16_t, const std::atomic<bool>&) override { return true; }
    int Wait(bool wantWrite, int) override { return wantWrite ? 2 : 1; }
    long Send(const uint8_t*, size_t size) override { return static_cast<long>(size); }
    long Receive(uint8_t* bytes, size_t size) override {
        if (mAt == mBytes.size()) return 0;
        const size_t n = std::min({size, mBytes.size() - mAt, static_cast<size_t>(mChunk)});
        std::memcpy(bytes, mBytes.data() + mAt, n);
        mAt += n;
        return static_cast<long>(n);
    }
    void Close() override {}

private:
    std::string mBytes;
    size_t mAt = 0;
    uint8_t mChunk;
};

std::string Scratch() {
    const char* folder = std::getenv("TPW_FUZZ_SCRATCH");
    return folder != nullptr ? folder : TPW_FUZZ_SCRATCH_DEFAULT;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    const uint8_t chunk = data[0];
    const std::string all(reinterpret_cast<const char*>(data + 1), size - 1);
    const size_t one = all.find('\xff');
    const size_t two = one == std::string::npos ? std::string::npos : all.find('\xff', one + 1);
    const std::string control = all.substr(0, one);
    const std::string listing = one == std::string::npos ? std::string() : all.substr(one + 1, two - one - 1);
    const std::string park = two == std::string::npos ? std::string() : all.substr(two + 1);

    std::vector<uint8_t> listed(listing.begin(), listing.end());
    uint64_t listedSize = 0;
    ListedSize(listed, listedSize);
    ParkPaths(control, listing);

    // The data connections, in the order the download opens them; the control connection is made first.
    std::deque<std::string> connections = {control, listing, park};
    boss::SetTransportMaker([&connections, chunk]() -> std::unique_ptr<boss::Transport> {
        if (connections.empty()) return nullptr;
        std::string bytes = std::move(connections.front());
        connections.pop_front();
        return std::make_unique<FakeTransport>(std::move(bytes), chunk);
    });
    FtpLink s;
    s.host = "fuzz";
    s.port = 21;
    s.idleMs = 100;
    for (std::u16string& text : s.legal.text) text = u"Theme Park World (c) fuzz legal text";
    Conn conn;
    const std::string folder = Scratch() + "/01234567-89AB-CDEF-";
    const std::string local = folder + "/0123-456789ABCDEF.LAYS";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const int connected = DoConnect(s, conn, Command{kConnect, "anonymous", "fuzz"});
    const int downloaded =
        connected == 0 ? DoDownload(s, conn, Command{kDownload, "/parks/01234567-89AB-CDEF-0123-456789ABCDEF", local}) : -1;
    conn.Close();
    if (std::getenv("TPW_FUZZ_LOG") != nullptr) {
        for (const std::string& note : s.notes) std::fprintf(stderr, "%s\n", note.c_str());
        std::fprintf(stderr, "connect %d download %d\n", connected, downloaded);
    }
    std::filesystem::remove(local, ec);
    boss::SetTransportMaker({});
    return 0;
}
