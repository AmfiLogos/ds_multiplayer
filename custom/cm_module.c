// cm_module.c - Custom Modes as a loader module (custom-modes.dylib, mod id "custom-modes", scope gameplay).
// MASTER_PLAN 2.2 B: Custom Modes finds multiplayer with module_loaded("multiplayer") + dlsym; multiplayer absent =
// Foreign Expedition and PvP hidden (they live in custom-modes-mp.dylib, next to this file, loaded only then). The solo
// tabs (My Units, Gallery, Test Fights, Unit Builder: lanes C1, C4, C5) go in this module and work without multiplayer.
// Phases: dsl_module_early runs after every module was dlopened, so the multiplayer image is there whatever the order.
#include "dsl_sdk.h"
#include "mp_api.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

DSL_PLUGIN();

typedef int (*cm_mp_start_fn)(const DSLHost *, DSLPlugin *, const mp_api_table *);

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
