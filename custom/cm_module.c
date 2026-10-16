// cm_module.c - Custom Modes as a loader module (custom-modes.dylib, mod id "custom-modes", scope gameplay).
// MASTER_PLAN 2.2 B: Custom Modes finds multiplayer with module_loaded("multiplayer") + dlsym; multiplayer absent =
// Foreign Expedition and PvP hidden (they live in custom-modes-mp.dylib, next to this file, loaded only then). The solo
// tabs (My Units, Gallery, Test Fights, Unit Builder: lanes C1, C4, C5) go in this module and work without multiplayer.
// Phases: dsl_module_early runs after every module was dlopened, so the multiplayer image is there whatever the order.
// dsl_plugin_init (il2cpp up, widget kit there): the title home (home/cm_home.c, PLAN_V13 R4) and the solo parts, each
// through a weak entry point that exists only when its folder is in the build (custom/<folder>/sources.txt).
#include "dsl_sdk.h"
#include "mp_api.h"
#include "cm_home.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32                                            /* Windows: custom-modes.dll + custom-modes-mp.dll (lane crossmods) */
#include <windows.h>
#define CM_MP_IMAGE "multiplayer.dll"
#define CM_MP_PART "custom-modes-mp.dll"
static void *cm_self_image(void) { HMODULE m = NULL; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void *)cm_self_image, &m); return m; }
static void *cm_loaded(const char *name) { return GetModuleHandleA(name); }
static void *cm_open(const char *path) { return LoadLibraryA(path); }
static void *cm_sym(void *h, const char *n) { return (void *)GetProcAddress((HMODULE)h, n); }
static const char *cm_err(void) { static char b[32]; snprintf(b, sizeof b, "error %lu", (unsigned long)GetLastError()); return b; }
static int cm_my_dir(char *out, size_t n) { DWORD k = GetModuleFileNameA((HMODULE)cm_self_image(), out, (DWORD)n); if (!k || k >= n) return -1; char *s = strrchr(out, '\\'); if (s) s[1] = 0; else out[0] = 0; return 0; }
#else
#include <dlfcn.h>
#define CM_MP_IMAGE "@rpath/multiplayer.dylib"           /* the loaded multiplayer image, by install name */
#define CM_MP_PART "custom-modes-mp.dylib"
static void *cm_loaded(const char *name) { return dlopen(name, RTLD_NOLOAD | RTLD_LOCAL); }
static void *cm_open(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *cm_sym(void *h, const char *n) { return dlsym(h, n); }
static const char *cm_err(void) { return dlerror(); }
static int cm_my_dir(char *out, size_t n) {
    Dl_info di; if (!dladdr((void *)cm_my_dir, &di) || !di.dli_fname) return -1;
    snprintf(out, n, "%s", di.dli_fname); char *sl = strrchr(out, '/'); if (sl) sl[1] = 0; else out[0] = 0; return 0;
}
#endif

DSL_PLUGIN();

typedef int (*cm_mp_start_fn)(const DSLHost *, DSLPlugin *, const mp_api_table *);

/* solo parts (PLAN_V13 section 4): weak defaults here, the real entry point (in its folder's sources.txt) replaces it at
   link time. A default only logs "not in this build" (that lane is not merged yet). Weak definitions, not weak
   references: the Mac linker and the Windows (llvm-mingw) linker both take them without extra flags. */
typedef void cm_init_fn(const DSLHost *, DSLPlugin *);
#define CM_PART_DEFAULT(fn, what) \
    __attribute__((weak)) void fn(const DSLHost *h, DSLPlugin *s) { h->log(s, "%s: not in this build", what); }
CM_PART_DEFAULT(cm_units_init, "My Units")
CM_PART_DEFAULT(cm_builder_init, "Unit Builder")
CM_PART_DEFAULT(cm_gallery_init, "Gallery")
CM_PART_DEFAULT(cm_fights_init, "Test Fights")
/* My Units' rig commands ("mu_", units/mu_init.c) through multiplayer's test socket, when multiplayer runs */
__attribute__((weak)) char *mu_rig_exec(const char *cmd, const char *line) { (void)cmd; (void)line; return NULL; }

DSL_EXPORT int dsl_module_load(const DSLHost *host, DSLPlugin *self) {
    host->log(self, "Custom Modes loaded");
    return 0;
}

DSL_EXPORT int dsl_module_early(const DSLHost *host, DSLPlugin *self) {
    if (!host->module_loaded || !host->module_loaded("multiplayer")) {
        host->log(self, "multiplayer is not active: Foreign Expedition and PvP are hidden");
        return 0;
    }
    void *mp = cm_loaded(CM_MP_IMAGE);
    mp_api_get_fn get = mp ? (mp_api_get_fn)cm_sym(mp, "mp_api_get") : NULL;
    const mp_api_table *api = get ? get(MP_API_VERSION) : NULL;
    if (!api) { host->log(self, "multiplayer API not found (%s): Foreign Expedition and PvP are hidden", mp ? "no mp_api_get" : "no multiplayer image"); return 0; }
    if (api->register_rig_cmd) api->register_rig_cmd("mu_", mu_rig_exec);
    char p[1200];
    if (cm_my_dir(p, sizeof p)) return 0;
    size_t L = strlen(p); snprintf(p + L, sizeof p - L, "%s", CM_MP_PART);
    void *h = cm_open(p);
    cm_mp_start_fn start = h ? (cm_mp_start_fn)cm_sym(h, "cm_mp_start") : NULL;
    if (!start) { host->log(self, "%s: %s: Foreign Expedition and PvP are hidden", p, h ? "no cm_mp_start" : cm_err()); return 0; }
    int r = start(host, self, api);
    host->log(self, "Foreign Expedition + PvP %s (rc %d)", r ? "FAILED" : "on", r);
    return r;
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    cm_home_init(host, self);
    cm_init_fn *parts[] = { cm_units_init, cm_builder_init, cm_gallery_init, cm_fights_init };
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) parts[i](host, self);
    return 0;
}
