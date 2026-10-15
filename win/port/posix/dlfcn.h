/* shim: dlsym(RTLD_DEFAULT, name) looks in the rig DLL's own export table (exports listed in
   build/baselib_exports.def); anything else -> NULL (feature off, as on a Mac build without that lane). */
#pragma once
#include <windows.h>
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NOW 2
#define RTLD_LOCAL 0
#define RTLD_NOLOAD 0x10
static inline void *dlsym(void *h, const char *name) {
    (void)h; HMODULE m = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void *)&dlsym, &m);
    return m ? (void *)GetProcAddress(m, name) : NULL;
}
static inline void *dlopen(const char *p, int f) { (void)p; (void)f; return NULL; }
/* dladdr: module file of an address (path with '/' separators, as the Mac code expects) */
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
static inline int dladdr(const void *addr, Dl_info *info) {
    static char path[MAX_PATH * 2]; HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m)) return 0;
    DWORD n = GetModuleFileNameA(m, path, sizeof path); if (!n) return 0;
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    info->dli_fname = path; info->dli_fbase = (void *)m; info->dli_sname = 0; info->dli_saddr = 0;
    return 1;
}
