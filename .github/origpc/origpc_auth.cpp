// Loads the original PC game's login library (weaauthr.dll, from the player's disc) and runs its login the way the game's
// CFSOnlineSystem::DoLogin does: Auth::Auth, Auth::Connect(server, NULL, port, callback), then Auth::Login(language, name,
// password, NULL). Every WSOCK32 call the library makes goes through a logging stand-in (its import table is patched), so the
// log shows whether it connects, whether it sends anything and what it receives. With --mock, a server on 127.0.0.1 in this
// process plays the other side: it can send a greeting first and answers a complete request with "wrong name or password".
// The password is never written out: its bytes are masked in every dump.
//
// TEMPORARY test tool of the ci-origpc trial branch. Build (x86): cl /nologo /EHsc /O1 /MT origpc_auth.cpp ws2_32.lib
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

FILE* gLog = nullptr;
DWORD gStart = 0;
CRITICAL_SECTION gLogLock;
std::string gPasswordAscii;
std::vector<unsigned char> gPasswordWide;

long gSentBytes = 0, gRecvBytes = 0, gConnects = 0, gCallbacks = 0;
int gConnectError = 0;
std::string gCallbackText;
const char* volatile gPhase = "start";

void Log(const char* format, ...) {
    EnterCriticalSection(&gLogLock);
    char line[2048];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    DWORD ms = GetTickCount() - gStart;
    fprintf(stdout, "[%6lu.%03lu t%05lu] %s\n", ms / 1000, ms % 1000, GetCurrentThreadId(), line);
    fflush(stdout);
    if (gLog != nullptr) {
        fprintf(gLog, "[%6lu.%03lu t%05lu] %s\n", ms / 1000, ms % 1000, GetCurrentThreadId(), line);
        fflush(gLog);
    }
    LeaveCriticalSection(&gLogLock);
}

void MaskRun(std::vector<unsigned char>& data, const unsigned char* needle, size_t n) {
    if (n == 0 || data.size() < n) return;
    for (size_t i = 0; i + n <= data.size(); ++i) {
        if (memcmp(&data[i], needle, n) == 0) {
            for (size_t k = 0; k < n; ++k) data[i + k] = '*';
        }
    }
}

// Hex and text of a buffer, with the password's bytes (UTF-16LE and plain) replaced by '*'.
void Dump(const char* what, const char* buf, int len) {
    if (len <= 0 || buf == nullptr) return;
    std::vector<unsigned char> data(buf, buf + len);
    MaskRun(data, gPasswordWide.data(), gPasswordWide.size());
    MaskRun(data, reinterpret_cast<const unsigned char*>(gPasswordAscii.data()), gPasswordAscii.size());
    for (int off = 0; off < len && off < 4096; off += 16) {
        char hex[64] = {0}, text[20] = {0};
        int n = len - off < 16 ? len - off : 16;
        for (int i = 0; i < n; ++i) {
            sprintf_s(hex + i * 3, sizeof(hex) - i * 3, "%02x ", data[off + i]);
            text[i] = (data[off + i] >= 0x20 && data[off + i] < 0x7f) ? static_cast<char>(data[off + i]) : '.';
        }
        Log("  %s +%04x  %-48s %s", what, off, hex, text);
    }
}

std::string Addr(const sockaddr* a, int len) {
    if (a == nullptr || len < static_cast<int>(sizeof(sockaddr_in)) || a->sa_family != AF_INET) return "?";
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(a);
    char text[64];
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&in->sin_addr);
    sprintf_s(text, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], ntohs(in->sin_port));
    return text;
}

// ---- the stand-ins for the library's WSOCK32 imports (by ordinal) -------------------------------------------------------------

typedef SOCKET(WSAAPI* SocketFn)(int, int, int);
typedef int(WSAAPI* BindFn)(SOCKET, const sockaddr*, int);
typedef int(WSAAPI* ConnectFn)(SOCKET, const sockaddr*, int);
typedef int(WSAAPI* CloseFn)(SOCKET);
typedef int(WSAAPI* IoctlFn)(SOCKET, long, u_long*);
typedef int(WSAAPI* RecvFn)(SOCKET, char*, int, int);
typedef int(WSAAPI* SendFn)(SOCKET, const char*, int, int);
typedef int(WSAAPI* SelectFn)(int, fd_set*, fd_set*, fd_set*, const timeval*);
typedef int(WSAAPI* ShutdownFn)(SOCKET, int);
typedef int(WSAAPI* SetsockoptFn)(SOCKET, int, int, const char*, int);
typedef hostent*(WSAAPI* GethostFn)(const char*);
typedef int(WSAAPI* LastErrFn)(void);
typedef int(WSAAPI* StartupFn)(WORD, WSADATA*);
typedef int(WSAAPI* CleanupFn)(void);
typedef unsigned long(WSAAPI* InetAddrFn)(const char*);
typedef char*(WSAAPI* InetNtoaFn)(in_addr);

SocketFn oSocket; BindFn oBind; ConnectFn oConnect; CloseFn oClose; IoctlFn oIoctl; RecvFn oRecv; SendFn oSend;
SelectFn oSelect; ShutdownFn oShutdown; SetsockoptFn oSetsockopt; GethostFn oGethost; LastErrFn oLastErr; StartupFn oStartup;
CleanupFn oCleanup; InetAddrFn oInetAddr; InetNtoaFn oInetNtoa;

SOCKET WSAAPI hSocket(int af, int type, int proto) {
    SOCKET s = oSocket(af, type, proto);
    Log("socket(af=%d, type=%d, proto=%d) = %u", af, type, proto, static_cast<unsigned>(s));
    return s;
}
int WSAAPI hBind(SOCKET s, const sockaddr* a, int len) {
    int r = oBind(s, a, len);
    Log("bind(%u, %s) = %d (err %d)", static_cast<unsigned>(s), Addr(a, len).c_str(), r, r ? WSAGetLastError() : 0);
    return r;
}
int WSAAPI hConnect(SOCKET s, const sockaddr* a, int len) {
    int r = oConnect(s, a, len);
    int e = r ? WSAGetLastError() : 0;
    ++gConnects;
    gConnectError = e;
    Log("connect(%u, %s) = %d (err %d%s)", static_cast<unsigned>(s), Addr(a, len).c_str(), r, e,
        e == WSAEWOULDBLOCK ? " WSAEWOULDBLOCK: non-blocking connect in progress" : "");
    WSASetLastError(e);
    return r;
}
int WSAAPI hClose(SOCKET s) {
    int r = oClose(s);
    Log("closesocket(%u) = %d", static_cast<unsigned>(s), r);
    return r;
}
int WSAAPI hIoctl(SOCKET s, long cmd, u_long* arg) {
    u_long before = arg ? *arg : 0;
    int r = oIoctl(s, cmd, arg);
    Log("ioctlsocket(%u, 0x%08lx%s, %lu) = %d -> %lu", static_cast<unsigned>(s), static_cast<unsigned long>(cmd),
        cmd == static_cast<long>(FIONBIO) ? " FIONBIO" : (cmd == static_cast<long>(FIONREAD) ? " FIONREAD" : ""), before, r,
        arg ? *arg : 0);
    return r;
}
int WSAAPI hRecv(SOCKET s, char* buf, int len, int flags) {
    int r = oRecv(s, buf, len, flags);
    int e = r < 0 ? WSAGetLastError() : 0;
    Log("recv(%u, len=%d, flags=%d) = %d (err %d)", static_cast<unsigned>(s), len, flags, r, e);
    if (r > 0) {
        gRecvBytes += r;
        Dump("recv", buf, r);
    }
    WSASetLastError(e);
    return r;
}
int WSAAPI hSend(SOCKET s, const char* buf, int len, int flags) {
    int r = oSend(s, buf, len, flags);
    int e = r < 0 ? WSAGetLastError() : 0;
    Log("send(%u, len=%d, flags=%d) = %d (err %d)", static_cast<unsigned>(s), len, flags, r, e);
    if (r > 0) gSentBytes += r;
    Dump("send", buf, len);
    WSASetLastError(e);
    return r;
}

// select is polled in a loop: a line only when what it is asked or answers changes, with how often the last one repeated.
std::string gSelectKey;
long gSelectRepeat = 0;
int WSAAPI hSelect(int n, fd_set* r, fd_set* w, fd_set* e, const timeval* t) {
    unsigned rc = r ? r->fd_count : 0, wc = w ? w->fd_count : 0, ec = e ? e->fd_count : 0;
    int res = oSelect(n, r, w, e, t);
    int err = res < 0 ? WSAGetLastError() : 0;
    char key[256];
    sprintf_s(key, "select(read %u, write %u, except %u, timeout %ld ms) = %d (err %d) -> read %u write %u except %u", rc, wc, ec,
              t ? static_cast<long>(t->tv_sec * 1000 + t->tv_usec / 1000) : -1L, res, err, r ? r->fd_count : 0,
              w ? w->fd_count : 0, e ? e->fd_count : 0);
    if (gSelectKey != key) {
        if (gSelectRepeat > 1) Log("  (the select above repeated %ld times)", gSelectRepeat);
        Log("%s", key);
        gSelectKey = key;
        gSelectRepeat = 1;
    } else {
        ++gSelectRepeat;
    }
    WSASetLastError(err);
    return res;
}
int WSAAPI hShutdown(SOCKET s, int how) {
    int r = oShutdown(s, how);
    Log("shutdown(%u, %d) = %d", static_cast<unsigned>(s), how, r);
    return r;
}
int WSAAPI hSetsockopt(SOCKET s, int level, int name, const char* val, int len) {
    int r = oSetsockopt(s, level, name, val, len);
    Log("setsockopt(%u, level=0x%x, name=0x%x, len=%d, first word %d) = %d", static_cast<unsigned>(s), level, name, len,
        (val && len >= 4) ? *reinterpret_cast<const int*>(val) : -1, r);
    return r;
}
hostent* WSAAPI hGethost(const char* name) {
    hostent* h = oGethost(name);
    std::string got = "NULL";
    if (h != nullptr && h->h_addr_list && h->h_addr_list[0]) {
        sockaddr_in in = {};
        in.sin_family = AF_INET;
        memcpy(&in.sin_addr, h->h_addr_list[0], 4);
        got = Addr(reinterpret_cast<sockaddr*>(&in), sizeof(in));
        got = got.substr(0, got.rfind(':'));
    }
    Log("gethostbyname(\"%s\") = %s (err %d)", name ? name : "(null)", got.c_str(), h ? 0 : WSAGetLastError());
    return h;
}
std::string gLastErrKey;
int WSAAPI hLastErr(void) {
    int r = oLastErr();
    char key[64];
    sprintf_s(key, "WSAGetLastError() = %d", r);
    if (gLastErrKey != key) {
        Log("%s", key);
        gLastErrKey = key;
    }
    return r;
}
int WSAAPI hStartup(WORD v, WSADATA* d) {
    int r = oStartup(v, d);
    Log("WSAStartup(0x%04x) = %d", v, r);
    return r;
}
int WSAAPI hCleanup(void) {
    int r = oCleanup();
    Log("WSACleanup() = %d", r);
    return r;
}
unsigned long WSAAPI hInetAddr(const char* text) {
    unsigned long r = oInetAddr(text);
    Log("inet_addr(\"%s\") = 0x%08lx%s", text ? text : "(null)", r, r == INADDR_NONE ? " (not a dotted address)" : "");
    return r;
}
char* WSAAPI hInetNtoa(in_addr a) {
    char* r = oInetNtoa(a);
    Log("inet_ntoa() = %s", r ? r : "(null)");
    return r;
}

struct Hook {
    WORD ordinal;
    void* replacement;
    void** original;
};
const Hook kHooks[] = {
    {23, reinterpret_cast<void*>(hSocket), reinterpret_cast<void**>(&oSocket)},
    {2, reinterpret_cast<void*>(hBind), reinterpret_cast<void**>(&oBind)},
    {4, reinterpret_cast<void*>(hConnect), reinterpret_cast<void**>(&oConnect)},
    {3, reinterpret_cast<void*>(hClose), reinterpret_cast<void**>(&oClose)},
    {12, reinterpret_cast<void*>(hIoctl), reinterpret_cast<void**>(&oIoctl)},
    {16, reinterpret_cast<void*>(hRecv), reinterpret_cast<void**>(&oRecv)},
    {19, reinterpret_cast<void*>(hSend), reinterpret_cast<void**>(&oSend)},
    {18, reinterpret_cast<void*>(hSelect), reinterpret_cast<void**>(&oSelect)},
    {22, reinterpret_cast<void*>(hShutdown), reinterpret_cast<void**>(&oShutdown)},
    {21, reinterpret_cast<void*>(hSetsockopt), reinterpret_cast<void**>(&oSetsockopt)},
    {52, reinterpret_cast<void*>(hGethost), reinterpret_cast<void**>(&oGethost)},
    {111, reinterpret_cast<void*>(hLastErr), reinterpret_cast<void**>(&oLastErr)},
    {115, reinterpret_cast<void*>(hStartup), reinterpret_cast<void**>(&oStartup)},
    {116, reinterpret_cast<void*>(hCleanup), reinterpret_cast<void**>(&oCleanup)},
    {10, reinterpret_cast<void*>(hInetAddr), reinterpret_cast<void**>(&oInetAddr)},
    {11, reinterpret_cast<void*>(hInetNtoa), reinterpret_cast<void**>(&oInetNtoa)},
};

// Points the library's WSOCK32 import slots at the stand-ins; the slots not listed (htons and the like) are left alone.
int PatchImports(HMODULE module) {
    unsigned char* base = reinterpret_cast<unsigned char*>(module);
    IMAGE_NT_HEADERS32* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name != 0; ++d) {
        const char* dll = reinterpret_cast<const char*>(base + d->Name);
        Log("import table: %s", dll);
        if (_stricmp(dll, "WSOCK32.dll") != 0) continue;
        IMAGE_THUNK_DATA32* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA32* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + d->FirstThunk);
        for (; names->u1.Ordinal != 0; ++names, ++slots) {
            if (!IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) {
                Log("  WSOCK32 import by name, left alone");
                continue;
            }
            WORD ordinal = static_cast<WORD>(IMAGE_ORDINAL32(names->u1.Ordinal));
            bool done = false;
            for (const Hook& h : kHooks) {
                if (h.ordinal != ordinal) continue;
                DWORD old;
                VirtualProtect(&slots->u1.Function, 4, PAGE_READWRITE, &old);
                *h.original = reinterpret_cast<void*>(slots->u1.Function);
                slots->u1.Function = reinterpret_cast<DWORD>(h.replacement);
                VirtualProtect(&slots->u1.Function, 4, old, &old);
                ++patched;
                done = true;
            }
            Log("  WSOCK32 ordinal %u%s", ordinal, done ? " -> logged" : " (passed through)");
        }
    }
    return patched;
}

// ---- the library's Auth class (MSVC thiscall, called through __fastcall with a dummy EDX) ------------------------------------

typedef void(__stdcall* LoginCallback)(int, int, void*, void*);
typedef void*(__fastcall* AuthCtor)(void* self, void* edx);
typedef int(__fastcall* AuthConnect)(void* self, void* edx, char* server, char* unused, unsigned short port, LoginCallback cb);
typedef unsigned long(__fastcall* AuthLogin)(void* self, void* edx, unsigned short language, unsigned short* name,
                                             unsigned short* password, void* context);
typedef int(__fastcall* AuthRelease)(void* self, void* edx);

void __stdcall OnLogin(int a, int b, void* c, void* d) {
    ++gCallbacks;
    char text[128];
    sprintf_s(text, "%s%d", gCallbackText.empty() ? "" : ",", a);
    gCallbackText += text;
    const char* meaning = a == 0x3c ? "logged in" : a == -2 ? "wrong name or password (0x4b)" : a == -3 ? "0x37" :
                          a == -4 ? "disconnected (0x4c)" : a == -5 ? "server error (0x4d)" : a == -6 ? "0x4e" : "other (0x37)";
    Log("CALLBACK login(a=%d, b=%d, c=%p, d=%p): %s", a, b, c, d, meaning);
    if (a == 0x3c && d != nullptr) Dump("account ids", static_cast<const char*>(d), 8);
}

// ---- the stand-in server (--mock) --------------------------------------------------------------------------------------------

struct Mock {
    unsigned short port = 0;
    std::vector<unsigned char> greeting;
    DWORD greetDelayMs = 500;
    DWORD seconds = 20;
    bool reply = true;
};
Mock gMock;
SOCKET gListen = INVALID_SOCKET;

DWORD WINAPI MockThread(void*) {
    SOCKET c = accept(gListen, nullptr, nullptr);
    if (c == INVALID_SOCKET) {
        Log("MOCK accept failed (err %d)", WSAGetLastError());
        return 0;
    }
    Log("MOCK accepted a connection");
    if (!gMock.greeting.empty()) {
        Sleep(gMock.greetDelayMs);
        int r = send(c, reinterpret_cast<const char*>(gMock.greeting.data()), static_cast<int>(gMock.greeting.size()), 0);
        Log("MOCK sent a %u-byte greeting (send = %d)", static_cast<unsigned>(gMock.greeting.size()), r);
        Dump("mock-greet", reinterpret_cast<const char*>(gMock.greeting.data()), static_cast<int>(gMock.greeting.size()));
    } else {
        Log("MOCK sends nothing first (like our server)");
    }
    std::vector<unsigned char> got;
    bool answered = false;
    DWORD end = GetTickCount() + gMock.seconds * 1000;
    while (GetTickCount() < end) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(c, &r);
        timeval t = {0, 200000};
        if (select(0, &r, nullptr, nullptr, &t) <= 0) continue;
        char buf[4096];
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) {
            Log("MOCK the client closed the connection (recv = %d, err %d)", n, n < 0 ? WSAGetLastError() : 0);
            break;
        }
        Log("MOCK received %d bytes (%u in all)", n, static_cast<unsigned>(got.size() + n));
        got.insert(got.end(), buf, buf + n);
        if (got.size() >= 0x14) {
            const unsigned char* h = got.data();
            unsigned typeBE = (h[2] << 8) | h[3], typeLE = h[2] | (h[3] << 8);
            unsigned long lenBE = (h[8] << 24) | (h[9] << 16) | (h[10] << 8) | h[11];
            unsigned long lenLE = h[8] | (h[9] << 8) | (h[10] << 16) | (static_cast<unsigned long>(h[11]) << 24);
            bool big = lenBE <= 0x10000;
            unsigned long length = big ? lenBE : lenLE;
            if (!answered && got.size() >= 0x14 + length) {
                Log("MOCK a complete message: magic '%c%c', type %u (big-endian) / %u (little-endian), length %lu (%s), %u bytes",
                    h[0], h[1], typeBE, typeLE, length, big ? "big-endian" : "little-endian", static_cast<unsigned>(got.size()));
                if (gMock.reply) {
                    unsigned char rep[0x14];
                    memcpy(rep, h, 0x14);
                    rep[1] = 's';
                    if (big) { rep[2] = 0; rep[3] = 1; } else { rep[2] = 1; rep[3] = 0; }
                    rep[6] = rep[7] = 0;
                    rep[8] = rep[9] = rep[10] = rep[11] = 0;
                    int r = send(c, reinterpret_cast<const char*>(rep), sizeof(rep), 0);
                    Log("MOCK answered type 1 (wrong name or password), context and sequence copied (send = %d)", r);
                }
                answered = true;
            }
        }
    }
    Log("MOCK done: received %u bytes in all", static_cast<unsigned>(got.size()));
    closesocket(c);
    return 0;
}

bool StartMock() {
    gListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(gMock.port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(gListen, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(gListen, 4) != 0) {
        Log("MOCK cannot listen on 127.0.0.1:%u (err %d)", gMock.port, WSAGetLastError());
        return false;
    }
    Log("MOCK listening on 127.0.0.1:%u", gMock.port);
    CloseHandle(CreateThread(nullptr, 0, MockThread, nullptr, 0, nullptr));
    return true;
}

// The library's Connect or Login could block (waiting for the server, say): the run still ends, with what was seen so far.
DWORD gWatchdogSeconds = 60;
DWORD WINAPI WatchdogThread(void*) {
    Sleep(gWatchdogSeconds * 1000);
    if (gSelectRepeat > 1) Log("  (the select above repeated %ld times)", gSelectRepeat);
    Log("WATCHDOG: %lu s and the run is still in: %s", gWatchdogSeconds, gPhase);
    Log("SUMMARY (watchdog) connect_calls=%ld connect_err=%d sent_bytes=%ld recv_bytes=%ld callbacks=[%s] stuck_in=%s", gConnects,
        gConnectError, gSentBytes, gRecvBytes, gCallbackText.c_str(), gPhase);
    if (gLog) fflush(gLog);
    ExitProcess(5);
}

std::vector<unsigned char> ParseHex(const char* text) {
    std::vector<unsigned char> out;
    if (_stricmp(text, "none") == 0) return out;
    for (size_t i = 0; text[i] && text[i + 1]; i += 2) {
        char pair[3] = {text[i], text[i + 1], 0};
        out.push_back(static_cast<unsigned char>(strtoul(pair, nullptr, 16)));
    }
    return out;
}

std::vector<unsigned short> Wide(const std::string& text) {
    std::vector<unsigned short> out(text.begin(), text.end());
    out.push_back(0);
    return out;
}

int RunLogin(const char* dllPath, const char* server, unsigned short port, unsigned short language, DWORD seconds) {
    HMODULE lib = LoadLibraryA(dllPath);
    if (lib == nullptr) {
        Log("FAIL LoadLibrary(%s): error %lu", dllPath, GetLastError());
        return 2;
    }
    Log("loaded %s at %p", dllPath, lib);
    Log("patched %d WSOCK32 import slots", PatchImports(lib));
    AuthCtor ctor = reinterpret_cast<AuthCtor>(GetProcAddress(lib, "??0Auth@@QAE@XZ"));
    AuthConnect connect = reinterpret_cast<AuthConnect>(GetProcAddress(lib, "?Connect@Auth@@QAEHPAD0GP6GXHHPAX1@Z@Z"));
    AuthLogin login = reinterpret_cast<AuthLogin>(GetProcAddress(lib, "?Login@Auth@@QAEKGPAG0PAX@Z"));
    AuthRelease release = reinterpret_cast<AuthRelease>(GetProcAddress(lib, "?Release@Auth@@QAEHXZ"));
    if (!ctor || !connect || !login || !release) {
        Log("FAIL an Auth export is missing (ctor %p connect %p login %p release %p)", ctor, connect, login, release);
        return 2;
    }
    // The game news the object; its size is not known here, so it gets far more room than it can need.
    void* auth = calloc(1, 0x10000);
    ctor(auth, nullptr);
    Log("Auth::Auth done");

    const char* nameEnv = getenv("ORIGPC_NAME");
    std::string name = (nameEnv && *nameEnv) ? nameEnv : "origpcci";
    std::vector<unsigned short> wideName = Wide(name), widePassword = Wide(gPasswordAscii);

    char serverCopy[256];
    strcpy_s(serverCopy, server);
    Log("calling Auth::Connect(\"%s\", NULL, %u, callback)", serverCopy, port);
    DWORD before = GetTickCount();
    gPhase = "Auth::Connect";
    int connected = connect(auth, nullptr, serverCopy, nullptr, port, OnLogin);
    Log("Auth::Connect returned %d after %lu ms", connected, GetTickCount() - before);
    if (connected == 1) {
        Log("calling Auth::Login(language %u, name \"%s\", password (%u characters, not shown), NULL)", language, name.c_str(),
            static_cast<unsigned>(gPasswordAscii.size()));
        before = GetTickCount();
        gPhase = "Auth::Login";
        unsigned long r = login(auth, nullptr, language, wideName.data(), widePassword.data(), nullptr);
        Log("Auth::Login returned %lu after %lu ms", r, GetTickCount() - before);
    } else {
        Log("Auth::Connect did not answer 1: the game would show error 0x4f (cannot connect) here");
    }
    gPhase = "the hold after Login";
    Log("HOLD: waiting %lu s for the library to send, receive or call back", seconds);
    DWORD end = GetTickCount() + seconds * 1000, nextTick = GetTickCount() + 10000;
    while (GetTickCount() < end) {
        Sleep(100);
        if (GetTickCount() >= nextTick) {
            Log("HOLD tick: sent %ld bytes, received %ld bytes, %ld callbacks so far", gSentBytes, gRecvBytes, gCallbacks);
            nextTick += 10000;
        }
    }
    if (gSelectRepeat > 1) Log("  (the select above repeated %ld times)", gSelectRepeat);
    Log("SUMMARY server=%s port=%u connect_calls=%ld connect_err=%d auth_connect=%d sent_bytes=%ld recv_bytes=%ld callbacks=[%s]",
        server, port, gConnects, gConnectError, connected, gSentBytes, gRecvBytes, gCallbackText.c_str());
    Log("calling Auth::Release");
    gPhase = "Auth::Release";
    release(auth, nullptr);
    Log("Auth::Release done");
    return 0;
}

int ProtectedRun(const char* dllPath, const char* server, unsigned short port, unsigned short language, DWORD seconds) {
    __try {
        return RunLogin(dllPath, server, port, language, seconds);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("FAIL exception 0x%08lx inside the library", GetExceptionCode());
        return 3;
    }
}

}  // namespace

int main(int argc, char** argv) {
    InitializeCriticalSection(&gLogLock);
    gStart = GetTickCount();
    const char* dll = nullptr;
    const char* server = "127.0.0.1";
    unsigned short port = 7598, language = 1;
    DWORD seconds = 30;
    bool mock = false;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--dll")) dll = argv[i + 1];
        else if (!strcmp(argv[i], "--server")) server = argv[i + 1];
        else if (!strcmp(argv[i], "--port")) port = static_cast<unsigned short>(atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--language")) language = static_cast<unsigned short>(atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--seconds")) seconds = static_cast<DWORD>(atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--log")) fopen_s(&gLog, argv[i + 1], "w");
        else if (!strcmp(argv[i], "--mock")) { mock = true; gMock.greeting = ParseHex(argv[i + 1]); }
        else if (!strcmp(argv[i], "--mock-delay-ms")) gMock.greetDelayMs = static_cast<DWORD>(atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--mock-reply")) gMock.reply = atoi(argv[i + 1]) != 0;
    }
    if (dll == nullptr) {
        fprintf(stderr, "usage: origpc_auth --dll weaauthr.dll [--server HOST] [--port N] [--seconds N] [--log FILE]\n"
                        "                   [--mock HEX|none] [--mock-delay-ms N] [--mock-reply 0|1] [--language N]\n");
        return 1;
    }
    const char* pass = getenv("ORIGPC_PASS");
    gPasswordAscii = (pass && *pass) ? pass : "not-a-secret";
    for (char ch : gPasswordAscii) {
        gPasswordWide.push_back(static_cast<unsigned char>(ch));
        gPasswordWide.push_back(0);
    }
    // The game itself has Winsock started before the library runs; the library's own WSAStartup is logged too.
    WSADATA data;
    int started = WSAStartup(MAKEWORD(1, 1), &data);
    Log("harness WSAStartup(1.1) = %d", started);
    gWatchdogSeconds = seconds + 60;
    CloseHandle(CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr));
    if (mock) {
        gMock.port = port;
        gMock.seconds = seconds;
        if (!StartMock()) return 4;
    }
    int rc = ProtectedRun(dll, server, port, language, seconds);
    Log("exit %d", rc);
    if (gLog) fclose(gLog);
    ExitProcess(static_cast<UINT>(rc));
}
