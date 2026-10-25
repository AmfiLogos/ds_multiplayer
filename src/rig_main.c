#include "mpmenu.h"
// Test-rig bootstrap. Injected with DYLD_INSERT_LIBRARIES into an isolated game copy (rig/rig.py).
// Does nothing unless DSRIG_DIR is set. When GameAssembly is mapped (before any of its code runs):
// checks the build, hooks MainFlow.Update (main-thread command pump) and
// Application.get_persistentDataPath (saves go to $DSRIG_DIR/data), starts the control socket.
#include "rig.h"
#include "mp_game.h"
#include "ord_api.h"
#include "ui_mp.h"
#include "trn.h"
void ui_teams_install(void);
#include "../sdk/dsl_sdk.h"
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <dlfcn.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include "rig_quiet.inc"     /* test copies: no window but the game's, no outgoing open (here only: never in the module) */

uintptr_t ds_base;
char rig_dir[1024];
static FILE *lf;
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;

void rig_log(const char *fmt, ...) {
    if (!lf) return;
    pthread_mutex_lock(&loglock);
    struct timeval tv; gettimeofday(&tv, NULL);
    char ts[32]; strftime(ts, sizeof ts, "%H:%M:%S", localtime(&tv.tv_sec));
    fprintf(lf, "%s.%03d ", ts, (int)(tv.tv_usec / 1000));
    va_list ap; va_start(ap, fmt); vfprintf(lf, fmt, ap); va_end(ap);
    fputc('\n', lf); fflush(lf);
    pthread_mutex_unlock(&loglock);
}

static void uuid_of(const struct mach_header_64 *h, char *out, size_t n) {
    out[0] = 0;
    const uint8_t *p = (const uint8_t*)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const void*)p;
        if (lc->cmd == LC_UUID) {
            const uint8_t *u = ((const struct uuid_command*)lc)->uuid;
            snprintf(out, n, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                     u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],u[8],u[9],u[10],u[11],u[12],u[13],u[14],u[15]);
        }
        p += lc->cmdsize;
    }
}

/* ---- Units Laboratory (custom-modes.dylib; a launcher mod, its My Units test commands only): loaded the way the loader loads a module: dlopen RTLD_LOCAL, then its
   dsl_module_load / dsl_module_early with a host that has what the rig can give (hooks through rig_hook, module_loaded:
   "multiplayer" = this dylib). File: DSRIG_CUSTOM=<path>, else custom-modes.dylib next to this dylib; DSRIG_CUSTOM=0 = none. */
struct DSLPlugin { int unused; };
static struct DSLPlugin cm_self;
static char cm_dir[1024], cm_ga[1024];
static void cm_log(DSLPlugin *self, const char *fmt, ...) {
    (void)self; char b[1200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    rig_log("custom-modes: %s", b);
}
static const char *cm_id(DSLPlugin *self) { (void)self; return "custom-modes"; }
static void *cm_rva(uintptr_t rva) { return (void *)(ds_base + rv(rva)); }
static int cm_hook_rva(DSLPlugin *self, uintptr_t rva, void *repl, void **orig, const char *name) { (void)self; return rig_hook(rva, repl, orig, name); }
static const char *cm_module_dir(DSLPlugin *self) { (void)self; return cm_dir; }
static const char *cm_ga_path(void) { return cm_ga[0] ? cm_ga : NULL; }
static int cm_module_loaded(const char *id) { return id && (!strcmp(id, "multiplayer") || !strcmp(id, "custom-modes")); }
static DSLHost cm_host = { .api_version = DSL_API_VERSION, .size = sizeof(DSLHost), .game_build = RIG_BUILD, .log = cm_log,
    .plugin_id = cm_id, .rva = cm_rva, .hook_rva = cm_hook_rva, .module_dir = cm_module_dir, .ga_path = cm_ga_path,
    .module_loaded = cm_module_loaded };
static void custom_modes_start(void) {
    const char *e = getenv("DSRIG_CUSTOM");
    if (e && !strcmp(e, "0")) { rig_log("custom-modes: off (DSRIG_CUSTOM=0)"); return; }
    char p[1200];
    if (e && *e) snprintf(p, sizeof p, "%s", e);
    else { Dl_info di; if (!dladdr((void *)rig_log, &di) || !di.dli_fname) return; snprintf(p, sizeof p, "%s", di.dli_fname);
           char *sl = strrchr(p, '/'); if (sl) sl[1] = 0; else p[0] = 0; strlcat(p, "custom-modes.dylib", sizeof p); }
    snprintf(cm_dir, sizeof cm_dir, "%s", p); { char *sl = strrchr(cm_dir, '/'); if (sl) *sl = 0; }
    if (access(p, R_OK)) { rig_log("custom-modes: %s not found (Units Laboratory not loaded)", p); return; }
    void *h = dlopen(p, RTLD_NOW | RTLD_LOCAL);
    if (!h) { rig_log("custom-modes: dlopen failed: %s", dlerror()); return; }
    const uint32_t *api = dlsym(h, "dsl_plugin_api_version");
    dsl_module_load_fn ld = (dsl_module_load_fn)dlsym(h, "dsl_module_load");
    dsl_module_early_fn early = (dsl_module_early_fn)dlsym(h, "dsl_module_early");
    if (!api || *api < 1 || *api > DSL_API_VERSION) { rig_log("custom-modes: not a module (API %d)", api ? (int)*api : -1); return; }
    cm_host.ga_base = ds_base;
    int r = ld ? ld(&cm_host, &cm_self) : 0;
    if (!r && early) r = early(&cm_host, &cm_self);
    rig_log("custom-modes: %s started (rc %d)", p, r);
}

/* ---- hooks */
static void (*o_update)(void *, void *);
static void my_update(void *self, void *mi) {
    { extern int rig_hooks_late; rig_hooks_late = 1; }   /* hooks installed from now on use the late stub page */
    rig_frame(self);
    if (!getenv("DSRIG_MINIMAL")) rig_det_frame(self);                /* locked: runs this frame's sim ticks; the hooks then skip them in Update */
    o_update(self, mi);
}

typedef void *Obj;
static Obj (*o_pdp)(void *);
static Obj my_pdp(void *mi) {
    (void)mi; (void)o_pdp;
    static char p[1100];
    if (!p[0]) snprintf(p, sizeof p, "%s/data", rig_dir);
    return F(0x6F9738, Obj (*)(const char *))(p);     /* il2cpp_string_new */
}

static void on_image(const struct mach_header *mh, intptr_t slide) {
    (void)slide;
    if (ds_base) return;
    Dl_info di;
    if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "/GameAssembly.dylib")) return;
    const struct mach_header_64 *h = (const void*)mh;
    if (h->magic != MH_MAGIC_64) return;
    char uuid[40]; uuid_of(h, uuid, sizeof uuid);
    if (strcmp(uuid, RIG_GA_UUID)) { rig_log("GameAssembly uuid %s is not build %s: rig inactive", uuid, RIG_BUILD); return; }
    ds_base = (uintptr_t)mh;
    rig_log("GameAssembly %s mapped at %p (build %s)", di.dli_fname, (void*)ds_base, RIG_BUILD);
    snprintf(cm_ga, sizeof cm_ga, "%s", di.dli_fname);
    if (rig_rv_load(RIG_BUILD, NULL)) return;                   /* the rig: reference version only (DSRIG_RV_TABLE: tests) */
    int bad = 0;
    bad |= rig_hook(0xD1B648, (void*)my_update, (void**)&o_update, "MainFlow.Update");
    bad |= rig_hook(0x507B74C, (void*)my_pdp, (void**)&o_pdp, "Application.get_persistentDataPath");
    if (bad) { rig_log("hook install failed: the rig will not answer game commands"); }
    if (getenv("DSRIG_MINIMAL")) {      /* perf baseline: only the save-path redirect + command pump, game otherwise stock */
        rig_log("DSRIG_MINIMAL: det / trn / mp / ord / ui hooks NOT installed");
        char sp2[1100]; snprintf(sp2, sizeof sp2, "%s/ctl.sock", rig_dir); rig_sock_start(sp2); return;
    }
    rig_det_install();
    trn_install();                      /* floor seeds, flow barrier, host-only autosave */
    mp_game_install();                  /* speed/pause hooks; mp.json session */
    ord_install();                      /* order capture / replay */
    ui_install();                       /* multiplayer UI (title + campaign) */
    ui_teams_install();                 /* Teams UI hooks at load (ui_teams_frame keeps a late fallback) */
    mpmenu_install();                   /* Multiplayer menu: title, lobby, Esc entries */
    custom_modes_start();               /* Units Laboratory, when next to this dylib (Foreign Expedition / PvP are in mpmenu_install) */
    char sp[1100]; snprintf(sp, sizeof sp, "%s/ctl.sock", rig_dir);
    rig_sock_start(sp);
}

__attribute__((constructor)) static void rig_init(void) {
#if !defined(__arm64__)
    return;
#endif
    const char *d = getenv("DSRIG_DIR");
    if (!d || !*d) return;
    snprintf(rig_dir, sizeof rig_dir, "%s", d);
    char lp[1100]; snprintf(lp, sizeof lp, "%s/rig.log", rig_dir);
    struct stat lst;                            /* appended across starts: past 32 MB the old one becomes rig.log.1 */
    if (!stat(lp, &lst) && lst.st_size > (32 << 20)) { char lp1[1110]; snprintf(lp1, sizeof lp1, "%s.1", lp); rename(lp, lp1); }
    lf = fopen(lp, "a");
    rig_log("---- rig start pid %d (batchmode arg: %s, hide windows: %s)", getpid(),
            getenv("DSRIG_BATCH") ? "yes" : "no", getenv("DSRIG_HIDEWIN") ? "yes" : "no");
    unsetenv("DYLD_INSERT_LIBRARIES");          /* children (crash reporter etc.) do not inherit the rig */
    if (getenv("DSRIG_LIVE")) rig_log("DSRIG_LIVE: normal game (sound, frame rate, windows untouched)");
    else if (!rig_bg_app()) { rig_log("refused: a test copy must be a background-only app (Info.plist LSBackgroundOnly, rig/make_instance.sh)"); _exit(3); }
    else if (getenv("DSRIG_HIDEWIN") || getenv("DSRIG_BATCH")) rig_win_install();   /* batchmode copies check in with AppKit too */
    else if (getenv("DSRIG_SPACE")) rig_win_space_install();
    if (!getenv("DSRIG_LIVE") && !getenv("DSRIG_SPACE") && !getenv("DSRIG_SHOWWIN")) rig_noact_install();   /* every other copy */
    if (!getenv("DSRIG_LIVE")) rig_quiet_install();
    _dyld_register_func_for_add_image(on_image);
}
