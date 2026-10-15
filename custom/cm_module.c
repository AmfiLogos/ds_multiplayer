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
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

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

DSL_EXPORT int dsl_module_load(const DSLHost *host, DSLPlugin *self) {
    host->log(self, "Custom Modes loaded");
    return 0;
}

DSL_EXPORT int dsl_module_early(const DSLHost *host, DSLPlugin *self) {
    if (!host->module_loaded || !host->module_loaded("multiplayer")) {
        host->log(self, "multiplayer is not active: Foreign Expedition and PvP are hidden");
        return 0;
    }
    void *mp = dlopen("@rpath/multiplayer.dylib", RTLD_NOLOAD | RTLD_LOCAL);   /* the loaded multiplayer image, by install name */
    mp_api_get_fn get = mp ? (mp_api_get_fn)dlsym(mp, "mp_api_get") : NULL;
    const mp_api_table *api = get ? get(MP_API_VERSION) : NULL;
    if (!api) { host->log(self, "multiplayer API not found (%s): Foreign Expedition and PvP are hidden", mp ? "no mp_api_get" : "no multiplayer image"); return 0; }
    char p[1200]; Dl_info di;
    if (!dladdr((void *)dsl_module_early, &di) || !di.dli_fname) return 0;
    snprintf(p, sizeof p, "%s", di.dli_fname);
    char *sl = strrchr(p, '/'); if (sl) sl[1] = 0; else p[0] = 0;
    strlcat(p, "custom-modes-mp.dylib", sizeof p);
    void *h = dlopen(p, RTLD_NOW | RTLD_LOCAL);
    cm_mp_start_fn start = h ? (cm_mp_start_fn)dlsym(h, "cm_mp_start") : NULL;
    if (!start) { host->log(self, "%s: %s: Foreign Expedition and PvP are hidden", p, h ? "no cm_mp_start" : dlerror()); return 0; }
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
