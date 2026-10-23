// Windows port contributed by samueleagle91 (tpw-port PR #1)
// dlfcn.h for Windows: the dynamic linker's lookups that the platform layer and the game make, over the process's loaded
// modules (platform/posix_win.cpp). dlsym(RTLD_DEFAULT) finds the launcher's own functions because the launcher exports
// its symbols (CMakeLists.txt); dladdr names the module only, not the symbol.
#pragma once

#define RTLD_DEFAULT ((void*)0)
#define RTLD_NEXT ((void*)-1)

typedef struct {
    const char* dli_fname;   // the module's path (UTF-8)
    void* dli_fbase;         // where it is loaded
    const char* dli_sname;   // always NULL
    void* dli_saddr;         // always NULL
} Dl_info;

#ifdef __cplusplus
extern "C" {
#endif
void* dlsym(void* handle, const char* name);
int dladdr(const void* address, Dl_info* info);
#ifdef __cplusplus
}
#endif
