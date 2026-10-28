// online_library.cpp: the login connection of the Mac's "online" shared library (Auth, include/FS_Online.h), which main
// imports and the port does not have. On the Mac, Auth::Connect opened a connection to the TPW Online login server and
// answered 1 when it was under way; the servers are gone, so here it answers 0 at once, with no name lookup and no
// socket, and CFSOnlineSystem::DoLogin (0x10171234) takes the Mac's own failure branch: error 0x4f goes to the
// interface (CompleteLogin, AddUIError) and the player sees the game's "cannot connect" dialog over the login panel,
// whose Cancel goes back to the islands (LoginRASCancelled, PostGetCityListCallbackFunction, HandleOnlineBacktrack).
// Nothing logs in, so Login is never reached; the other connection classes (News, City, Upload) connect only after a
// login and keep their generated stand-ins. Defined here rather than by launcher/gen_stubs.py so that the connection
// fails as the Mac's library did when no server answered, and quietly (the stand-ins log "not wired up yet").
// Not in platform/msl.cpp, which the test libraries link (their Auth is the test's own fake).
#include <cstdint>

#include "FS_Online.h"

Auth::Auth() {}

Auth::~Auth() {}

void Auth::Release() {}

int32_t Auth::Connect(char* server, char* unused, uint16_t port, void (*callback)(int32_t, int32_t, void*, void*)) {
    (void)server;
    (void)unused;
    (void)port;
    (void)callback;
    return 0;
}

void Auth::Login(uint16_t language, char16_t* name, char16_t* password, void* context) {
    (void)language;
    (void)name;
    (void)password;
    (void)context;
}
