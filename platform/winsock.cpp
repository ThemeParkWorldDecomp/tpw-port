// winsock.cpp: the winsock calls of the online code (FTP, e-mail, the connection tests), on the
// host's BSD sockets with winsock's conventions. On the Mac they were the glue imports
// 0x1023a708 to 0x1023a888 of the shared "winsock" library; the game reaches them through
// the *_callee namespaces of its source files (include/FS_Online.h, include/email.h).
//
// Winsock's conventions kept: a socket is a plain number (the host's file descriptor, the game
// compares it to -1), the calls answer -1 when they fail, and an address is Windows' sockaddr_in:
// a 16 bit family, the port as a plain number, the 4 address bytes in network order, 8 zero bytes.
// The _WIN32 parts of this file: Windows port contributed by samueleagle91 (tpw-port PR #1).
#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <cstring>
#include <mutex>

#include "FS_Online.h"
#include "email.h"
#include "platform/macdoze_system.h"

#ifdef _WIN32
// Windows' sockets, after the game's headers: Windows' headers define macros that would rename what those declare.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {

std::mutex gLock;
int32_t gStartups = 0;   // WSAStartup calls not yet matched by a WSACleanup

// Winsock's sockaddr_in as the game lays it out.
struct GameAddress {
    uint16_t family;
    uint16_t port;       // the port number itself (the Mac's big endian word)
    uint8_t ip[4];       // network order bytes
    uint8_t zero[8];
};
static_assert(sizeof(GameAddress) == 16, "winsock's sockaddr_in is 16 bytes");

sockaddr_in ToHost(const void* game, int32_t length) {
    sockaddr_in host;
    std::memset(&host, 0, sizeof host);
#ifndef _WIN32   // BSD's field; Windows' has none
    host.sin_len = sizeof host;
#endif
    host.sin_family = AF_INET;
    if (game != nullptr && length >= static_cast<int32_t>(sizeof(GameAddress))) {
        GameAddress address;
        std::memcpy(&address, game, sizeof address);
        host.sin_port = htons(address.port);
        std::memcpy(&host.sin_addr, address.ip, 4);
    }
    return host;
}

void ToGame(const sockaddr_in& host, void* game, int32_t* length) {
    if (game == nullptr) return;
    GameAddress address;
    std::memset(&address, 0, sizeof address);
    address.family = 2;   // AF_INET
    address.port = ntohs(host.sin_port);
    std::memcpy(address.ip, &host.sin_addr, 4);
    std::memcpy(game, &address, sizeof address);
    if (length != nullptr) *length = static_cast<int32_t>(sizeof address);
}

// WSAStartup (0x102394f0): version 1.1, no description or status text, 64 sockets, 0xffe3 byte
// datagrams, no vendor information. Counts the call.
int32_t Startup(void* data) {
    if (data != nullptr) {
        auto* bytes = static_cast<uint8_t*>(data);
        const uint16_t version = 0x101;
        std::memset(bytes, 0, 0x190);
        std::memcpy(bytes + 0x0, &version, 2);       // wVersion
        std::memcpy(bytes + 0x2, &version, 2);       // wHighVersion
        const uint16_t sockets = 0x40, datagram = 0xffe3;
        std::memcpy(bytes + 0x186, &sockets, 2);     // iMaxSockets
        std::memcpy(bytes + 0x188, &datagram, 2);    // iMaxUdpDg
    }
    std::lock_guard<std::mutex> lock(gLock);
    ++gStartups;
#ifdef _WIN32
    // The game's count stays its own; Windows' sockets need their own start before the first call.
    static const bool started = [] {
        WSADATA data;
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
#endif
    return 0;
}

// WSACleanup (0x102386f8): WSANOTINITIALISED (0x276d) when there was no WSAStartup.
int32_t Cleanup() {
    std::lock_guard<std::mutex> lock(gLock);
    if (gStartups == 0) return 0x276d;
    --gStartups;
    return 0;
}

// The hostent gethostbyname answers: one per thread, its storage reused by the next call (winsock's rule too).
struct HostentStorage {
    FTPHostent entry;
    char name[256];
    char* aliases[1];
    uint32_t* addresses[2];
    uint32_t address;
};

}  // namespace

// ---- The calls of the FTP client ------------------------------------------------------
namespace ftp_callee {

// socket 0x1023a708. The game asks for (2, 2, 2): AF_INET datagrams with "protocol 2", which on
// BSD sockets is IGMP, so the protocol is left to the type.
int32_t Socket(int32_t family, int32_t type, int32_t) {
    const int fd = ::socket(family == 2 ? AF_INET : family, type, 0);
    if (fd < 0) return -1;
#ifndef _WIN32   // Windows has no SIGPIPE
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    return fd;
}

// bind 0x1023a738
int32_t Bind(int32_t sock, const void* addr, int32_t length) {
    const sockaddr_in host = ToHost(addr, length);
    return ::bind(sock, reinterpret_cast<const sockaddr*>(&host), sizeof host) == 0 ? 0 : -1;
}

// gethostbyname 0x1023a750
FTPHostent* GetHostByName(const char* name) {
    thread_local HostentStorage storage;
    addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (name == nullptr || ::getaddrinfo(name, nullptr, &hints, &found) != 0 || found == nullptr) return nullptr;
    std::memcpy(&storage.address, &reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr, 4);
    ::freeaddrinfo(found);
    std::strncpy(storage.name, name, sizeof storage.name - 1);
    storage.name[sizeof storage.name - 1] = '\0';
    storage.aliases[0] = nullptr;
    storage.addresses[0] = &storage.address;
    storage.addresses[1] = nullptr;
    storage.entry.mName = storage.name;
    storage.entry.mAliases = storage.aliases;
    storage.entry.mType = 2;
    storage.entry.mLength = 4;
    storage.entry.mAddrList = storage.addresses;
    return &storage.entry;
}

// connect 0x1023a768
int32_t SockConnect(int32_t sock, const void* addr, int32_t length) {
    const sockaddr_in host = ToHost(addr, length);
    return ::connect(sock, reinterpret_cast<const sockaddr*>(&host), sizeof host) == 0 ? 0 : -1;
}

// recv 0x1023a780: recvfrom with no address.
int32_t Recv(int32_t sock, void* buffer, int32_t size, int32_t flags) {
#ifdef _WIN32
    return static_cast<int32_t>(::recvfrom(sock, static_cast<char*>(buffer), size, flags & 3, nullptr, nullptr));
#else
    return static_cast<int32_t>(::recvfrom(sock, buffer, size, flags & 3, nullptr, nullptr));
#endif
}

// shutdown 0x1023a7b0
void Shutdown(int32_t sock, int32_t how) { ::shutdown(sock, how); }

// closesocket 0x1023a7c8
#ifdef _WIN32
void CloseSocket(int32_t sock) { ::closesocket(static_cast<SOCKET>(sock)); }
#else
void CloseSocket(int32_t sock) { ::close(sock); }
#endif

// getsockname 0x1023a810
int32_t GetSockName(int32_t sock, void* addr, int32_t* length) {
    sockaddr_in host;
    socklen_t size = sizeof host;
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&host), &size) != 0) return -1;
    ToGame(host, addr, length);
    return 0;
}

// listen 0x1023a828
int32_t Listen(int32_t sock, int32_t backlog) { return ::listen(sock, backlog) == 0 ? 0 : -1; }

// accept 0x1023a840: the new socket, with the peer's address when the caller wants it.
int32_t Accept(int32_t sock, void* addr, int32_t* length) {
    sockaddr_in host;
    socklen_t size = sizeof host;
    const int fd = ::accept(sock, reinterpret_cast<sockaddr*>(&host), &size);
    if (fd < 0) return -1;
#ifndef _WIN32   // Windows has no SIGPIPE
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    ToGame(host, addr, length);
    return fd;
}

// inet_addr 0x1023a720: the address as 4 network order bytes, 0xffffffff when the text is not one.
uint32_t InetAddr(const char* text) { return text != nullptr ? ::inet_addr(text) : 0xffffffffu; }

// Sleep 0x1023a888 is macdoze's Sleep.
void Sleep(uint32_t ms) { NS_MacDoze::Sleep(ms); }

void WSACleanup() { Cleanup(); }
int32_t WSAStartup(int32_t, void* data) { return Startup(data); }

}  // namespace ftp_callee

namespace v5917_callee {
int32_t WSAStartup(int32_t, void* data) { return Startup(data); }
int32_t WSACleanup() { return Cleanup(); }
}  // namespace v5917_callee

// ---- The calls of the e-mail code and the connection tests ----------------------------
namespace email_callee {
void WSACleanup() { Cleanup(); }
}  // namespace email_callee

namespace emailcb_callee {
void WinsockStart(int32_t, void* data) { Startup(data); }
}  // namespace emailcb_callee

namespace csock_callee {
// send 0x1023a798
int32_t Send(int32_t sock, const char* data, int32_t length, int32_t flags) {
    return static_cast<int32_t>(::send(sock, data, length, flags & 3));
}
}  // namespace csock_callee

namespace conn_callee {
// gethostname 0x1023a7f8
int32_t Gethostname(char* name, int32_t size) { return ::gethostname(name, size) == 0 ? 0 : -1; }
}  // namespace conn_callee
