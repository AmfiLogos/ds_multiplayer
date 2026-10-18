// mp_moddata.c - mod data sync at the join point (lane moddata, v0.1.16; loader side: ds_modloader
// loader/src/dsl_moddata.c, docs loader/NOTES.md "Mod data folder").
// A gameplay mod lists files of its data folder (<data>/ModData/<id>/) in mod.json "sync". The host's join point save
// carries them as the block "MDAT" (mp_api joinpoint, between the det state and the save JSON); the loader packs them
// (dsl_moddata_pack / _copy). A guest hands the block to the loader before the save loads (dsl_moddata_apply): the
// loader checks every name and sha256, puts the guest's own matching files aside, writes the host's and compares the
// data fingerprint. A refusal stops the load and ends the join with the reason; back on the title with no session the
// guest's own files come back (dsl_moddata_session_end). A loader without these exports: no block, nothing changes.
// Nothing outside ModData is ever written, nothing received is ever run (all of that is in the loader).
#include "mp_api.h"
#include "rig.h"
#include "ds_i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>

static long (*md_pack)(void);
static size_t (*md_copy)(uint8_t *, size_t);
static int (*md_apply)(const uint8_t *, size_t, char *, size_t);
static void (*md_end)(void);
static void *md_sym(const char *n) {
#ifdef _WIN32
    HMODULE b = GetModuleHandleA("baselib.dll");    /* Windows: the loader is the baselib.dll proxy */
    void *p = b ? (void *)GetProcAddress(b, n) : NULL;
    if (p) return p;
#endif
    return dlsym(RTLD_DEFAULT, n);
}

/* host: the block now (0 = no running mod syncs files). The join point asks size() twice (the buffer size, then the
   write) and then write(): one pack serves all three (files changed in between would not fit the buffer); write()
   uses it up, the next join point packs again. */
static long packed_n; static int packed_fresh; static double packed_at;
static double mono_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static size_t md_size(void) {
    if (!md_pack) return 0;
    double now = mono_s();
    if (!packed_fresh || now - packed_at > 10.0) {
        packed_n = md_pack(); packed_at = now; packed_fresh = 1;
        if (packed_n < 0) rig_log("mp: moddata: the sync files could not be read: no MDAT block (guests keep their own files)");
    }
    return packed_n > 0 ? (size_t)packed_n : 0;
}
static size_t md_write(uint8_t *out, size_t cap) { packed_fresh = 0; return md_copy ? md_copy(out, cap) : 0; }

static int refused, applied;
static char refused_why[400];
static void md_read(const uint8_t *p, size_t n) {  /* guest, before the save loads */
    if (!p || !n) return;
    if (!md_apply) {
        snprintf(refused_why, sizeof refused_why, "the host syncs mod data files, but this mod loader cannot take them (update it)");
        refused = 1; rig_log("mp: moddata: join refused: %s", refused_why); return;
    }
    char why[400] = "";
    int rc = md_apply(p, n, why, sizeof why);
    if (rc) { snprintf(refused_why, sizeof refused_why, "%s", why); refused = rc; rig_log("mp: moddata: join refused (%d): %s", rc, why); }
    else { applied = 1; rig_log("mp: moddata: the host's mod data files are in place"); }
}

void mp_moddata_init(void) {
    static int done; if (done) return; done = 1;
    md_pack = (long (*)(void))md_sym("dsl_moddata_pack");
    md_copy = (size_t (*)(uint8_t *, size_t))md_sym("dsl_moddata_copy");
    md_apply = (int (*)(const uint8_t *, size_t, char *, size_t))md_sym("dsl_moddata_apply");
    md_end = (void (*)(void))md_sym("dsl_moddata_session_end");
    if (!md_pack || !md_copy) md_pack = NULL;
    static const mp_joinpoint jp = { { 'M', 'D', 'A', 'T' }, md_size, md_write, md_read, NULL };
    const mp_api_table *api = mp_api_get(MP_API_VERSION);
    if (api && api->register_joinpoint) api->register_joinpoint(&jp);
    rig_log("mp: moddata: %s", md_apply ? "the loader syncs mod data files (MDAT)" : "loader without mod data sync");
}
/* 1 = the last block was refused: the save must not load; the reason (English, for the log) in why */
int mp_moddata_refused(char *why, size_t n) {
    if (!refused) return 0;
    if (why && n) snprintf(why, n, "%s", refused_why);
    return refused;
}
void mp_moddata_clear_refusal(void) { refused = 0; refused_why[0] = 0; }
/* back on the title with no session (or the session ended before a load): the guest's own files come back */
void mp_moddata_session_over(void) {
    if (!applied && !refused) return;
    applied = 0;
    if (md_end) md_end();
}
