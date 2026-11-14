// online_boss_socket.cpp: the online server client's TCP connection (Transport, include/platform/online_boss.h), the only socket
// code of platform/online_boss.cpp. It connects to the server the launcher names (or a host that server's own answers named), and
// nowhere else; devtools/check_offline.py allows this file for that reason.
//
// The Mac's Client used blocking BSD sockets from its comms thread (connect, then select with a 100 ms timeout). Here the connect
// is non-blocking, in 100 ms waits, so a Release while it is still connecting ends the thread without waiting for the host's
// own connect timeout.
#ifndef _WIN32
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <chrono>
#include <cstring>
#include <mutex>
#include <string>

#include "platform/online_boss.h"

#ifdef _WIN32
// Windows' sockets, after the game's headers: Windows' headers define macros that would rename what those declare.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace tpw::online::boss {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
const Socket kNone = INVALID_SOCKET;
void CloseSocket(Socket s) { closesocket(s); }
bool WouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void Startup() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}
int LastError() { return WSAGetLastError(); }
std::string ErrorText(int error) {
    switch (error) {
    case WSAECONNREFUSED: return "the connection was refused";
    case WSAETIMEDOUT: return "the connection timed out";
    case WSAEHOSTUNREACH: case WSAENETUNREACH: return "no route to the host";
    default: return "socket error " + std::to_string(error);
    }
}
#else
using Socket = int;
const Socket kNone = -1;
void CloseSocket(Socket s) { close(s); }
bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
void Startup() {}
int LastError() { return errno; }
std::string ErrorText(int error) { return std::strerror(error); }
#endif

#if defined(__linux__)
const int kSendFlags = MSG_NOSIGNAL;
#else
const int kSendFlags = 0;
#endif

constexpr auto kConnectTimeout = std::chrono::seconds(kConnectSeconds);

// SetNameLookup's stand-in, read by the connection's thread.
std::mutex gLookupLock;
NameLookup gLookup;

// The stand-in's answer for `host`: why it was not found, or empty to look it up.
std::string StandInFailure(const std::string& host) {
    NameLookup lookup;
    {
        std::lock_guard<std::mutex> hold(gLookupLock);
        lookup = gLookup;
    }
    return lookup ? lookup(host) : std::string();
}

void SetNonBlocking(Socket s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

// Waits at most `milliseconds` for `s` to be readable and/or writable: bit 1 read, bit 2 write, -1 a failure.
int Select(Socket s, bool read, bool write, int milliseconds) {
    fd_set reads, writes, errors;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_ZERO(&errors);
    if (read) FD_SET(s, &reads);
    if (write) FD_SET(s, &writes);
    FD_SET(s, &errors);
    timeval wait{milliseconds / 1000, (milliseconds % 1000) * 1000};
    const int n = select(static_cast<int>(s) + 1, &reads, &writes, &errors, &wait);
    if (n < 0) {
#ifndef _WIN32
        if (errno == EINTR) return 0;
#endif
        return -1;
    }
    int ready = 0;
    if (FD_ISSET(s, &reads) || FD_ISSET(s, &errors)) ready |= 1;   // an error is read (and the read reports it)
    if (FD_ISSET(s, &writes)) ready |= 2;
    return ready;
}

class SocketTransport : public Transport {
public:
    ~SocketTransport() override { Close(); }

    bool Open(const std::string& host, uint16_t port, const std::atomic<bool>& stopping) override {
        Startup();
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* found = nullptr;
        mFailure.clear();
        const std::string standIn = StandInFailure(host);
        if (!standIn.empty()) {
            mFailure = "the name \"" + host + "\" was not found (" + standIn + ")";
            return false;
        }
        const int looked = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found);
        if (looked != 0 || found == nullptr) {
            mFailure = "the name \"" + host + "\" was not found (" + (looked != 0 ? gai_strerror(looked) : "no address") + ")";
            return false;
        }
        bool connected = false;
        for (addrinfo* a = found; a != nullptr && !connected && !stopping; a = a->ai_next) {
            mSocket = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (mSocket == kNone) {
                mFailure = "no socket (" + ErrorText(LastError()) + ")";
                continue;
            }
            SetNonBlocking(mSocket);
            if (connect(mSocket, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0)
                connected = true;
            else if (WouldBlock())
                connected = Finish(stopping);
            else
                mFailure = ErrorText(LastError());
            if (!connected) {
                CloseSocket(mSocket);
                mSocket = kNone;
            }
        }
        freeaddrinfo(found);
        if (!connected) return false;
        const int on = 1;
        setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
#ifdef SO_NOSIGPIPE
        setsockopt(mSocket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
        return true;
    }

    int Wait(bool wantWrite, int milliseconds) override {
        return mSocket == kNone ? -1 : Select(mSocket, true, wantWrite, milliseconds);
    }

    long Send(const uint8_t* bytes, size_t size) override {
        const long n = static_cast<long>(send(mSocket, reinterpret_cast<const char*>(bytes), static_cast<int>(size), kSendFlags));
        if (n < 0) return WouldBlock() ? kWouldBlock : -1;
        return n;
    }

    long Receive(uint8_t* bytes, size_t size) override {
        const long n = static_cast<long>(recv(mSocket, reinterpret_cast<char*>(bytes), static_cast<int>(size), 0));
        if (n < 0) return WouldBlock() ? kWouldBlock : -1;
        return n;
    }

    void Close() override {
        if (mSocket != kNone) CloseSocket(mSocket);
        mSocket = kNone;
    }

    std::string Failure() const override { return mFailure; }

private:
    // The rest of a non-blocking connect: true once connected, false when it failed, timed out or the owner let go (mFailure
    // says which).
    bool Finish(const std::atomic<bool>& stopping) {
        const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
        while (!stopping && std::chrono::steady_clock::now() < deadline) {
            const int ready = Select(mSocket, false, true, 100);
            if (ready < 0) {
                mFailure = "a wait failed (" + ErrorText(LastError()) + ")";
                return false;
            }
            if (ready == 0) continue;   // bit 2 connected, bit 1 (Windows' except set) failed: SO_ERROR tells which
            int error = 0;
            socklen_t length = sizeof error;
            if (getsockopt(mSocket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0) error = LastError();
            if (error == 0) return true;
            mFailure = ErrorText(error);
            return false;
        }
        if (!stopping)
            mFailure = "no answer within " + std::to_string(kConnectSeconds) + " seconds";
        return false;
    }

    Socket mSocket = kNone;
    std::string mFailure;   // why the last Open failed
};

}  // namespace

std::unique_ptr<Transport> MakeSocketTransport() { return std::make_unique<SocketTransport>(); }

void SetNameLookup(NameLookup lookup) {
    std::lock_guard<std::mutex> hold(gLookupLock);
    gLookup = std::move(lookup);
}

}  // namespace tpw::online::boss
