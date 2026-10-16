/* Custom Modes' solo module on Windows (custom-modes.dll, custom/cm_module.c unchanged): the dlfcn calls it makes, on
   LoadLibrary / GetProcAddress. "@rpath/<name>.dylib" = the DLL <name>.dll already in the process (RTLD_NOLOAD) or
   next to the caller; a path ending in .dylib loads the .dll of the same name. */
#pragma once
#include <windows.h>
#include <stdio.h>
#include <string.h>
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NOW 2
#define RTLD_LOCAL 0
#define RTLD_NOLOAD 0x10
static char cm_dlerr[600];
static inline void cm_dll_name(const char *p, char *out, size_t n) {
    const char *s = strncmp(p, "@rpath/", 7) ? p : p + 7;
    snprintf(out, n, "%s", s);
    size_t L = strlen(out);
    if (L > 6 && !strcmp(out + L - 6, ".dylib")) snprintf(out + L - 6, n - (L - 6), ".dll");
    for (char *c = out; *c; c++) if (*c == '/') *c = '\\';
}
static inline void *dlopen(const char *p, int f) {
    char n[1200]; cm_dll_name(p, n, sizeof n);
    HMODULE m = (f & RTLD_NOLOAD) ? GetModuleHandleA(n) : LoadLibraryExA(n, NULL, strchr(n, '\\') ? LOAD_WITH_ALTERED_SEARCH_PATH : 0);
    if (!m) snprintf(cm_dlerr, sizeof cm_dlerr, "%s: %s (error %lu)", n, (f & RTLD_NOLOAD) ? "not loaded" : "LoadLibrary failed", GetLastError());
    return m;
}
static inline void *dlsym(void *h, const char *name) {
    void *r = h ? (void *)GetProcAddress((HMODULE)h, name) : NULL;
    if (!r) snprintf(cm_dlerr, sizeof cm_dlerr, "%s not found", name);
    return r;
}
static inline const char *dlerror(void) { return cm_dlerr; }
static inline int dlclose(void *h) { return h ? !FreeLibrary((HMODULE)h) : 0; }
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
