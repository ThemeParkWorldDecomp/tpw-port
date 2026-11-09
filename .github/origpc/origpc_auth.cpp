// Loads the original PC game's login library (weaauthr.dll, from the player's disc) and runs its login the way the game's
// CFSOnlineSystem::DoLogin does: Auth::Auth, Auth::Connect(server, NULL, port, callback), then Auth::Login(language, name,
// password, NULL). Every WSOCK32 call the library makes goes through a logging stand-in (its import table is patched), and so
// do its KERNEL32 CreateThread and OutputDebugStringA, so the log shows when its comms thread starts and ends, whether it
// connects, whether it sends anything and what it receives. With --mock, a server on 127.0.0.1 in this process plays the other
// side: it reads the request and answers "wrong name or password", the way our server does. --affinity pins the process (and
// so the library's comms thread) to the given CPUs; --attempts runs the login again with a new Auth, like a second Accept.
// --dbwin is a separate mode: it only collects other processes' OutputDebugString text (DebugView's way), for the real game.
//
// What is written out never holds the account name or the password: of every message only the 20-byte header is shown, and
// the name and password are masked in the library's debug text.
//
// The calls made between the comms thread's start and the socket being ready (CreateThread, socket, OutputDebugStringA) are
// only noted in memory there and written out later by the main thread, so that logging does not change which thread wins.
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
LARGE_INTEGER gFreq, gT0;
CRITICAL_SECTION gLogLock;
std::string gPasswordAscii, gNameAscii;
unsigned char* gLibBase = nullptr;

volatile long gSentBytes = 0, gRecvBytes = 0, gConnects = 0, gCallbacks = 0;
volatile long gCommsStarted = 0, gCommsEnded = 0, gSelectBadFd = 0, gSelectErrors = 0;
int gConnectError = 0;
std::string gCallbackText;
const char* volatile gPhase = "start";

double NowMs() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart - gT0.QuadPart) * 1000.0 / static_cast<double>(gFreq.QuadPart);
}

void LogAt(double ms, DWORD thread, const char* prefix, const char* line) {
    EnterCriticalSection(&gLogLock);
    fprintf(stdout, "[%11.3f ms t%05lu] %s%s\n", ms, thread, prefix, line);
    fflush(stdout);
    if (gLog != nullptr) {
        fprintf(gLog, "[%11.3f ms t%05lu] %s%s\n", ms, thread, prefix, line);
        fflush(gLog);
    }
    LeaveCriticalSection(&gLogLock);
}

void Log(const char* format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    LogAt(NowMs(), GetCurrentThreadId(), "", line);
}

// ---- notes: a line kept in memory with its time, written out later by ShowNotes ----------------------------------------------

struct Note {
    double ms;
    DWORD thread;
    char text[200];
    volatile long ready;
};
const long kNotes = 1024;
Note gNotes[kNotes];
volatile long gNoteCount = 0;
long gNotesShown = 0;

void AddNote(const char* format, ...) {
    long i = InterlockedIncrement(&gNoteCount) - 1;
    if (i >= kNotes) return;
    Note& n = gNotes[i];
    n.ms = NowMs();
    n.thread = GetCurrentThreadId();
    va_list args;
    va_start(args, format);
    _vsnprintf_s(n.text, sizeof(n.text), _TRUNCATE, format, args);
    va_end(args);
    InterlockedExchange(&n.ready, 1);
}

void MaskText(char* text, const std::string& secret) {
    if (secret.empty()) return;
    for (char* p = strstr(text, secret.c_str()); p != nullptr; p = strstr(p + secret.size(), secret.c_str())) {
        memset(p, '*', secret.size());
    }
}

void ShowNotes() {
    long count = gNoteCount < kNotes ? gNoteCount : kNotes;
    while (gNotesShown < count && gNotes[gNotesShown].ready) {
        Note& n = gNotes[gNotesShown++];
        for (char* p = n.text; *p; ++p) {
            if (*p == '\r' || *p == '\n') *p = ' ';
        }
        MaskText(n.text, gPasswordAscii);
        MaskText(n.text, gNameAscii);
        LogAt(n.ms, n.thread, "(noted) ", n.text);
    }
}

unsigned long BE32(const unsigned char* b) {
    return (static_cast<unsigned long>(b[0]) << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

// A message's 20-byte header (magic, type, param, status, body length, context, sequence), never its body: the login body holds
// the name and the password.
void LogMessage(const char* what, const char* buf, int len) {
    if (len <= 0 || buf == nullptr) return;
    const unsigned char* b = reinterpret_cast<const unsigned char*>(buf);
    if (len >= 2 && b[0] == 'B' && (b[1] == 'c' || b[1] == 's')) {
        int n = len < 0x14 ? len : 0x14;
        char hex[3 * 0x14 + 1] = {0};
        for (int i = 0; i < n; ++i) sprintf_s(hex + i * 3, sizeof(hex) - i * 3, "%02x ", b[i]);
        Log("  %s header: %s", what, hex);
        if (len >= 0x14) {
            Log("  %s = '%c%c' type %u param %u status %u body length %lu sequence %lu; %d body bytes in this call, not shown", what,
                b[0], b[1], (b[2] << 8) | b[3], (b[4] << 8) | b[5], (b[6] << 8) | b[7], BE32(b + 8), BE32(b + 16), len - 0x14);
        }
    } else {
        Log("  %s: %d bytes that do not start a message, not shown", what, len);
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

// ---- the stand-ins for the library's WSOCK32 imports (by ordinal) and two KERNEL32 ones (by name) -----------------------------

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
typedef HANDLE(WINAPI* CreateThreadFn)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
typedef void(WINAPI* OdsFn)(LPCSTR);

SocketFn oSocket; BindFn oBind; ConnectFn oConnect; CloseFn oClose; IoctlFn oIoctl; RecvFn oRecv; SendFn oSend;
SelectFn oSelect; ShutdownFn oShutdown; SetsockoptFn oSetsockopt; GethostFn oGethost; LastErrFn oLastErr; StartupFn oStartup;
CleanupFn oCleanup; InetAddrFn oInetAddr; InetNtoaFn oInetNtoa; CreateThreadFn oCreateThread; OdsFn oOds;

SOCKET WSAAPI hSocket(int af, int type, int proto) {
    SOCKET s = oSocket(af, type, proto);
    int e = s == INVALID_SOCKET ? WSAGetLastError() : 0;
    AddNote("socket(af=%d, type=%d, proto=%d) = %d (err %d)", af, type, proto, static_cast<int>(s), e);
    WSASetLastError(e);
    return s;
}
int WSAAPI hBind(SOCKET s, const sockaddr* a, int len) {
    int r = oBind(s, a, len);
    Log("bind(%d, %s) = %d (err %d)", static_cast<int>(s), Addr(a, len).c_str(), r, r ? WSAGetLastError() : 0);
    return r;
}
int WSAAPI hConnect(SOCKET s, const sockaddr* a, int len) {
    double before = NowMs();
    int r = oConnect(s, a, len);
    int e = r ? WSAGetLastError() : 0;
    ++gConnects;
    gConnectError = e;
    ShowNotes();
    Log("connect(%d, %s) = %d (err %d%s), took %.1f ms", static_cast<int>(s), Addr(a, len).c_str(), r, e,
        e == WSAEWOULDBLOCK ? " WSAEWOULDBLOCK: non-blocking connect in progress" : "", NowMs() - before);
    WSASetLastError(e);
    return r;
}
int WSAAPI hClose(SOCKET s) {
    int r = oClose(s);
    Log("closesocket(%d) = %d", static_cast<int>(s), r);
    return r;
}
int WSAAPI hIoctl(SOCKET s, long cmd, u_long* arg) {
    u_long before = arg ? *arg : 0;
    int r = oIoctl(s, cmd, arg);
    Log("ioctlsocket(%d, 0x%08lx%s, %lu) = %d -> %lu", static_cast<int>(s), static_cast<unsigned long>(cmd),
        cmd == static_cast<long>(FIONBIO) ? " FIONBIO" : (cmd == static_cast<long>(FIONREAD) ? " FIONREAD" : ""), before, r,
        arg ? *arg : 0);
    return r;
}
int WSAAPI hRecv(SOCKET s, char* buf, int len, int flags) {
    int r = oRecv(s, buf, len, flags);
    int e = r < 0 ? WSAGetLastError() : 0;
    Log("recv(%d, len=%d, flags=%d) = %d (err %d)", static_cast<int>(s), len, flags, r, e);
    if (r > 0) {
        gRecvBytes += r;
        LogMessage("recv", buf, r);
    }
    WSASetLastError(e);
    return r;
}
int WSAAPI hSend(SOCKET s, const char* buf, int len, int flags) {
    int r = oSend(s, buf, len, flags);
    int e = r < 0 ? WSAGetLastError() : 0;
    Log("send(%d, len=%d, flags=%d) = %d (err %d)", static_cast<int>(s), len, flags, r, e);
    if (r > 0) gSentBytes += r;
    LogMessage("send", buf, len);
    WSASetLastError(e);
    return r;
}

// select is polled in a loop: a line only when what it is asked or answers changes, with how often the last one repeated.
// The socket in the sets is shown: -1 there is INVALID_SOCKET, the race (the comms thread got there before socket() did).
std::string gSelectKey;
long gSelectRepeat = 0;
int WSAAPI hSelect(int n, fd_set* r, fd_set* w, fd_set* e, const timeval* t) {
    unsigned rc = r ? r->fd_count : 0, wc = w ? w->fd_count : 0, ec = e ? e->fd_count : 0;
    int fd = rc ? static_cast<int>(r->fd_array[0]) : (wc ? static_cast<int>(w->fd_array[0]) : 0);
    bool bad = (rc && r->fd_array[0] == INVALID_SOCKET) || (wc && w->fd_array[0] == INVALID_SOCKET);
    int res = oSelect(n, r, w, e, t);
    int err = res < 0 ? WSAGetLastError() : 0;
    if (bad) InterlockedIncrement(&gSelectBadFd);
    if (res < 0) InterlockedIncrement(&gSelectErrors);
    char key[256];
    sprintf_s(key, "select(socket %d, read %u, write %u, except %u, timeout %ld ms) = %d (err %d%s) -> read %u write %u except %u", fd,
              rc, wc, ec, t ? static_cast<long>(t->tv_sec * 1000 + t->tv_usec / 1000) : -1L, res, err,
              err == WSAENOTSOCK ? " WSAENOTSOCK" : "", r ? r->fd_count : 0, w ? w->fd_count : 0, e ? e->fd_count : 0);
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
    Log("shutdown(%d, %d) = %d", static_cast<int>(s), how, r);
    return r;
}
int WSAAPI hSetsockopt(SOCKET s, int level, int name, const char* val, int len) {
    int r = oSetsockopt(s, level, name, val, len);
    Log("setsockopt(%d, level=0x%x, name=0x%x, len=%d, first word %d) = %d", static_cast<int>(s), level, name, len,
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
HANDLE WINAPI hCreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID arg, DWORD flags,
                            LPDWORD id) {
    DWORD local = 0;
    if (id == nullptr) id = &local;
    HANDLE h = oCreateThread(sa, stack, start, arg, flags, id);
    AddNote("CreateThread(start=library+0x%lx, flags=0x%lx) = new thread t%05lu",
            static_cast<unsigned long>(reinterpret_cast<unsigned char*>(start) - gLibBase), flags, *id);
    return h;
}
void WINAPI hOds(LPCSTR text) {
    if (text != nullptr) {
        if (strstr(text, "commsThread Started") != nullptr) InterlockedIncrement(&gCommsStarted);
        if (strstr(text, "commsThread Ended") != nullptr) InterlockedIncrement(&gCommsEnded);
    }
    AddNote("OutputDebugStringA(\"%.150s\")", text ? text : "(null)");
    oOds(text);
}

struct Hook {
    const char* dll;
    WORD ordinal;      // for WSOCK32
    const char* name;  // for KERNEL32
    void* replacement;
    void** original;
};
#define HOOK_ORD(n, f, o) {"WSOCK32.dll", n, nullptr, reinterpret_cast<void*>(f), reinterpret_cast<void**>(&o)}
#define HOOK_NAME(n, f, o) {"KERNEL32.dll", 0, n, reinterpret_cast<void*>(f), reinterpret_cast<void**>(&o)}
const Hook kHooks[] = {
    HOOK_ORD(23, hSocket, oSocket), HOOK_ORD(2, hBind, oBind), HOOK_ORD(4, hConnect, oConnect), HOOK_ORD(3, hClose, oClose),
    HOOK_ORD(12, hIoctl, oIoctl), HOOK_ORD(16, hRecv, oRecv), HOOK_ORD(19, hSend, oSend), HOOK_ORD(18, hSelect, oSelect),
    HOOK_ORD(22, hShutdown, oShutdown), HOOK_ORD(21, hSetsockopt, oSetsockopt), HOOK_ORD(52, hGethost, oGethost),
    HOOK_ORD(111, hLastErr, oLastErr), HOOK_ORD(115, hStartup, oStartup), HOOK_ORD(116, hCleanup, oCleanup),
    HOOK_ORD(10, hInetAddr, oInetAddr), HOOK_ORD(11, hInetNtoa, oInetNtoa),
    HOOK_NAME("CreateThread", hCreateThread, oCreateThread), HOOK_NAME("OutputDebugStringA", hOds, oOds),
};

// Points the library's import slots at the stand-ins; the slots not listed (htons, the rest of KERNEL32) are left alone.
int PatchImports(HMODULE module) {
    unsigned char* base = reinterpret_cast<unsigned char*>(module);
    IMAGE_NT_HEADERS32* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name != 0; ++d) {
        const char* dll = reinterpret_cast<const char*>(base + d->Name);
        bool wanted = false;
        for (const Hook& h : kHooks) wanted = wanted || _stricmp(dll, h.dll) == 0;
        Log("import table: %s%s", dll, wanted ? "" : " (left alone)");
        if (!wanted) continue;
        if (d->OriginalFirstThunk == 0) {
            Log("  no import names table: %s left alone", dll);
            continue;
        }
        IMAGE_THUNK_DATA32* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA32* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + d->FirstThunk);
        for (; names->u1.Ordinal != 0; ++names, ++slots) {
            bool byOrdinal = IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal);
            WORD ordinal = byOrdinal ? static_cast<WORD>(IMAGE_ORDINAL32(names->u1.Ordinal)) : 0;
            const char* name =
                byOrdinal ? nullptr : reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name);
            for (const Hook& h : kHooks) {
                if (_stricmp(dll, h.dll) != 0) continue;
                if (h.name ? (name == nullptr || strcmp(name, h.name) != 0) : (!byOrdinal || h.ordinal != ordinal)) continue;
                DWORD old;
                VirtualProtect(&slots->u1.Function, 4, PAGE_READWRITE, &old);
                *h.original = reinterpret_cast<void*>(slots->u1.Function);
                slots->u1.Function = reinterpret_cast<DWORD>(h.replacement);
                VirtualProtect(&slots->u1.Function, 4, old, &old);
                ++patched;
                if (name) Log("  %s %s -> logged", dll, name);
                else Log("  %s ordinal %u -> logged", dll, ordinal);
            }
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
}

// ---- the stand-in server (--mock): like ours, it reads first and sends nothing until it has a whole request ------------------

struct Mock {
    unsigned short port = 0;
    DWORD seconds = 20;
};
Mock gMock;
SOCKET gListen = INVALID_SOCKET;

// One connection: read until a whole request is in, answer it, then wait for the client to close (or the time to run out).
void MockConnection(SOCKET c) {
    Log("MOCK accepted a connection; it sends nothing first (like our server)");
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
        if (got.size() >= 0x14 && !answered) {
            const unsigned char* h = got.data();
            unsigned long length = BE32(h + 8);
            if (got.size() >= 0x14 + length) {
                Log("MOCK a complete message: magic '%c%c', type %u, body length %lu, sequence %lu, %u bytes (body not shown)", h[0],
                    h[1], (h[2] << 8) | h[3], length, BE32(h + 16), static_cast<unsigned>(got.size()));
                unsigned char rep[0x14];
                memcpy(rep, h, 0x14);
                rep[1] = 's';
                rep[2] = 0;
                rep[3] = 1;
                rep[6] = rep[7] = 0;
                rep[8] = rep[9] = rep[10] = rep[11] = 0;
                int r = send(c, reinterpret_cast<const char*>(rep), sizeof(rep), 0);
                Log("MOCK answered type 1 (wrong name or password), context and sequence copied (send = %d)", r);
                answered = true;
            }
        }
    }
    Log("MOCK done with the connection: received %u bytes in all", static_cast<unsigned>(got.size()));
    closesocket(c);
}

DWORD WINAPI MockThread(void*) {
    for (;;) {
        SOCKET c = accept(gListen, nullptr, nullptr);
        if (c == INVALID_SOCKET) {
            Log("MOCK accept failed (err %d)", WSAGetLastError());
            return 0;
        }
        MockConnection(c);
    }
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
    Log("MOCK listening on 127.0.0.1:%u (so this process has made a socket before the library does)", gMock.port);
    CloseHandle(CreateThread(nullptr, 0, MockThread, nullptr, 0, nullptr));
    return true;
}

// The library's Connect or Login could block (waiting for the server, say): the run still ends, with what was seen so far.
DWORD gWatchdogSeconds = 60;
DWORD WINAPI WatchdogThread(void*) {
    Sleep(gWatchdogSeconds * 1000);
    ShowNotes();
    if (gSelectRepeat > 1) Log("  (the select above repeated %ld times)", gSelectRepeat);
    Log("WATCHDOG: %lu s and the run is still in: %s", gWatchdogSeconds, gPhase);
    Log("SUMMARY (watchdog) connect_calls=%ld connect_err=%d comms_started=%ld comms_ended=%ld select_bad_fd=%ld sent_bytes=%ld "
        "recv_bytes=%ld callbacks=[%s] stuck_in=%s",
        gConnects, gConnectError, gCommsStarted, gCommsEnded, gSelectBadFd, gSentBytes, gRecvBytes, gCallbackText.c_str(), gPhase);
    if (gLog) fflush(gLog);
    ExitProcess(5);
}

std::vector<unsigned short> Wide(const std::string& text) {
    std::vector<unsigned short> out(text.begin(), text.end());
    out.push_back(0);
    return out;
}

int RunLogin(const char* dllPath, const char* server, unsigned short port, unsigned short language, DWORD seconds, int attempts) {
    HMODULE lib = LoadLibraryA(dllPath);
    if (lib == nullptr) {
        Log("FAIL LoadLibrary(%s): error %lu", dllPath, GetLastError());
        return 2;
    }
    gLibBase = reinterpret_cast<unsigned char*>(lib);
    Log("loaded %s at %p", dllPath, lib);
    Log("patched %d import slots", PatchImports(lib));
    AuthCtor ctor = reinterpret_cast<AuthCtor>(GetProcAddress(lib, "??0Auth@@QAE@XZ"));
    AuthConnect connect = reinterpret_cast<AuthConnect>(GetProcAddress(lib, "?Connect@Auth@@QAEHPAD0GP6GXHHPAX1@Z@Z"));
    AuthLogin login = reinterpret_cast<AuthLogin>(GetProcAddress(lib, "?Login@Auth@@QAEKGPAG0PAX@Z"));
    AuthRelease release = reinterpret_cast<AuthRelease>(GetProcAddress(lib, "?Release@Auth@@QAEHXZ"));
    if (!ctor || !connect || !login || !release) {
        Log("FAIL an Auth export is missing (ctor %p connect %p login %p release %p)", ctor, connect, login, release);
        return 2;
    }
    std::vector<unsigned short> wideName = Wide(gNameAscii), widePassword = Wide(gPasswordAscii);
    DWORD_PTR processMask = 0, systemMask = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
    unsigned long cpus = 0;
    for (DWORD_PTR m = systemMask; m != 0; m >>= 1) cpus += static_cast<unsigned long>(m & 1);

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        gSentBytes = gRecvBytes = gConnects = gCallbacks = 0;
        gCommsStarted = gCommsEnded = gSelectBadFd = gSelectErrors = 0;
        gConnectError = 0;
        gCallbackText.clear();
        Log("==== attempt %d of %d", attempt, attempts);
        // The game news the object; its size is not known here, so it gets far more room than it can need.
        void* auth = calloc(1, 0x10000);
        ctor(auth, nullptr);
        Log("Auth::Auth done");

        char serverCopy[256];
        strcpy_s(serverCopy, server);
        Log("calling Auth::Connect(\"%s\", NULL, %u, callback)", serverCopy, port);
        double before = NowMs();
        gPhase = "Auth::Connect";
        int connected = connect(auth, nullptr, serverCopy, nullptr, port, OnLogin);
        ShowNotes();
        Log("Auth::Connect returned %d after %.1f ms", connected, NowMs() - before);
        if (connected == 1) {
            Log("calling Auth::Login(language %u, name (%u characters, not shown), password (%u characters, not shown), NULL)", language,
                static_cast<unsigned>(gNameAscii.size()), static_cast<unsigned>(gPasswordAscii.size()));
            before = NowMs();
            gPhase = "Auth::Login";
            unsigned long r = login(auth, nullptr, language, wideName.data(), widePassword.data(), nullptr);
            Log("Auth::Login returned %lu after %.1f ms", r, NowMs() - before);
        } else {
            Log("Auth::Connect did not answer 1: the game would show error 0x4f (cannot connect) here");
        }
        gPhase = "the hold after Login";
        Log("HOLD: waiting %lu s for the library to send, receive or call back", seconds);
        DWORD end = GetTickCount() + seconds * 1000, nextTick = GetTickCount() + 10000;
        while (GetTickCount() < end) {
            Sleep(100);
            ShowNotes();
            if (GetTickCount() >= nextTick) {
                Log("HOLD tick: sent %ld bytes, received %ld bytes, %ld callbacks, comms thread started %ld ended %ld so far", gSentBytes,
                    gRecvBytes, gCallbacks, gCommsStarted, gCommsEnded);
                nextTick += 10000;
            }
        }
        ShowNotes();
        if (gSelectRepeat > 1) Log("  (the select above repeated %ld times)", gSelectRepeat);
        long endedBefore = gCommsEnded;
        const char* race = connected != 1     ? "n/a (not connected)"
                           : gSentBytes > 0    ? "no (the login was sent)"
                           : endedBefore > 0   ? "YES (the comms thread ended before it sent anything)"
                                               : "unclear (nothing sent, the comms thread did not end)";
        Log("SUMMARY attempt=%d/%d cpus=%lu affinity=0x%lx server=%s port=%u connect_calls=%ld connect_err=%d auth_connect=%d "
            "comms_started=%ld comms_ended=%ld select_bad_fd=%ld select_errors=%ld sent_bytes=%ld recv_bytes=%ld callbacks=[%s] race=%s",
            attempt, attempts, cpus,
            static_cast<unsigned long>(processMask), server, port, gConnects, gConnectError, connected, gCommsStarted, endedBefore,
            gSelectBadFd, gSelectErrors, gSentBytes, gRecvBytes, gCallbackText.c_str(), race);
        Log("calling Auth::Release");
        gPhase = "Auth::Release";
        release(auth, nullptr);
        ShowNotes();
        Log("Auth::Release done");
    }
    return 0;
}

int ProtectedRun(const char* dllPath, const char* server, unsigned short port, unsigned short language, DWORD seconds, int attempts) {
    __try {
        return RunLogin(dllPath, server, port, language, seconds, attempts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("FAIL exception 0x%08lx inside the library", GetExceptionCode());
        return 3;
    }
}

// DebugView's way of reading other processes' OutputDebugString text (when no debugger is attached to them). Only for the real
// game: while it runs, every OutputDebugString in the session waits for this reader, which changes timing.
int RunDbwin(DWORD seconds) {
    HANDLE ready = CreateEventA(nullptr, FALSE, FALSE, "DBWIN_BUFFER_READY");
    HANDLE data = CreateEventA(nullptr, FALSE, FALSE, "DBWIN_DATA_READY");
    HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, "DBWIN_BUFFER");
    if (ready == nullptr || data == nullptr || map == nullptr) {
        Log("FAIL the DBWIN objects (error %lu)", GetLastError());
        return 2;
    }
    const char* view = static_cast<const char*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 4096));
    if (view == nullptr) {
        Log("FAIL MapViewOfFile (error %lu)", GetLastError());
        return 2;
    }
    Log("DBWIN reading OutputDebugString text for %lu s", seconds);
    long count = 0;
    DWORD end = GetTickCount() + seconds * 1000;
    SetEvent(ready);
    while (GetTickCount() < end) {
        if (WaitForSingleObject(data, 250) != WAIT_OBJECT_0) continue;
        DWORD pid = *reinterpret_cast<const DWORD*>(view);
        char text[4096 - 4 + 1];
        memcpy(text, view + 4, sizeof(text) - 1);
        text[sizeof(text) - 1] = 0;
        for (char* p = text; *p; ++p) {
            if (*p == '\r' || *p == '\n') *p = ' ';
        }
        MaskText(text, gPasswordAscii);
        MaskText(text, gNameAscii);
        Log("DBWIN pid %lu: %s", pid, text);
        ++count;
        SetEvent(ready);
    }
    Log("DBWIN done: %ld strings", count);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    InitializeCriticalSection(&gLogLock);
    QueryPerformanceFrequency(&gFreq);
    QueryPerformanceCounter(&gT0);
    const char* dll = nullptr;
    const char* server = "127.0.0.1";
    unsigned short port = 7598, language = 1;
    DWORD seconds = 30, dbwin = 0;
    DWORD_PTR affinity = 0;
    int attempts = 1;
    bool mock = false;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--dll")) dll = argv[i + 1];
        else if (!strcmp(argv[i], "--server")) server = argv[i + 1];
        else if (!strcmp(argv[i], "--port")) port = static_cast<unsigned short>(atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--language")) language = static_cast<unsigned short>(atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--seconds")) seconds = static_cast<DWORD>(atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--attempts")) attempts = atoi(argv[i + 1]) < 1 ? 1 : atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--affinity")) affinity = static_cast<DWORD_PTR>(strtoul(argv[i + 1], nullptr, 0));
        else if (!strcmp(argv[i], "--log")) fopen_s(&gLog, argv[i + 1], "w");
        else if (!strcmp(argv[i], "--mock")) mock = atoi(argv[i + 1]) != 0;
        else if (!strcmp(argv[i], "--dbwin")) dbwin = static_cast<DWORD>(atoi(argv[i + 1]));
    }
    const char* pass = getenv("ORIGPC_PASS");
    gPasswordAscii = (pass && *pass) ? pass : "not-a-secret";
    const char* name = getenv("ORIGPC_NAME");
    gNameAscii = (name && *name) ? name : "origpcci";
    if (dbwin > 0) {
        int rc = RunDbwin(dbwin);
        if (gLog) fclose(gLog);
        return rc;
    }
    if (dll == nullptr) {
        fprintf(stderr, "usage: origpc_auth --dll weaauthr.dll [--server HOST] [--port N] [--seconds N] [--attempts N]\n"
                        "                   [--affinity MASK] [--mock 1] [--language N] [--log FILE]\n"
                        "       origpc_auth --dbwin SECONDS [--log FILE]\n");
        return 1;
    }
    if (affinity != 0) {
        BOOL ok = SetProcessAffinityMask(GetCurrentProcess(), affinity);
        Log("SetProcessAffinityMask(0x%lx) = %d (error %lu)", static_cast<unsigned long>(affinity), ok, ok ? 0 : GetLastError());
    }
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    Log("this machine has %lu logical processors", si.dwNumberOfProcessors);
    // The game itself has Winsock started before the library runs; the library's own WSAStartup is logged too. No socket is made
    // here (except by --mock), so the library's socket() is the process's first, as it may be in the game.
    WSADATA data;
    int started = WSAStartup(MAKEWORD(1, 1), &data);
    Log("harness WSAStartup(1.1) = %d", started);
    gWatchdogSeconds = seconds * attempts + 60;
    CloseHandle(CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr));
    if (mock) {
        gMock.port = port;
        gMock.seconds = seconds;
        if (!StartMock()) return 4;
    }
    int rc = ProtectedRun(dll, server, port, language, seconds, attempts);
    Log("exit %d", rc);
    if (gLog) fclose(gLog);
    ExitProcess(static_cast<UINT>(rc));
}
