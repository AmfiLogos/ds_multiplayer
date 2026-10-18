// rig_rv.c - game addresses on other game versions (owner 2026-10-18: "move multiplayer's fixed addresses into
// per-game-version tables"). The source keeps the addresses of the reference version RIG_BUILD, in F(), rig_hook(),
// md() and the like; each one goes through rv() (rig.h). Reference version: rv() returns the address as it is, no
// table read. Another version: rva/<build>_<mac|win>.tsv in the module folder, made by ds_modloader
// tools/gameupd/report.py from every address of the source:
//   0xREF <TAB> 0xNEW <TAB> kind <TAB> name <TAB> status       (# lines and GONE rows are comments)
// No table for a non-reference version = the module stays off (rig_rv_load fails). An address the table lacks is
// logged once per address and returned unchanged. tools/rv_gate.py (in the builds) keeps every source address on rv().
// Tests: DSRIG_RV_TABLE=<file> loads that table on any version (with an identity table: every lookup is checked).
#include "rig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rig_rv_on;
static char game_build[32];                                       /* a non-reference version with its table */
typedef struct { uint64_t ref, cur; } RvEnt;
static RvEnt *tab; static int ntab;
static uint64_t missed[64]; static int nmissed;              /* addresses logged as missing (once each) */

static int cmp_ent(const void *a, const void *b) {
    uint64_t x = ((const RvEnt *)a)->ref, y = ((const RvEnt *)b)->ref;
    return x < y ? -1 : x > y;
}

uintptr_t rig_rv_slow(uintptr_t ref) {
    int lo = 0, hi = ntab - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (tab[mid].ref == ref) return (uintptr_t)tab[mid].cur;
        if (tab[mid].ref < ref) lo = mid + 1; else hi = mid - 1;
    }
    int seen = 0, n = __atomic_load_n(&nmissed, __ATOMIC_ACQUIRE);           /* any thread: the log line is all it does */
    for (int i = 0; i < n && i < 64; i++) seen |= missed[i] == ref;
    int k = seen ? 64 : __atomic_fetch_add(&nmissed, 1, __ATOMIC_ACQ_REL);
    if (k < 64) {
        missed[k] = ref;
        rig_log("rv: address %#lx is not in the address table of this game version (used unchanged)", (unsigned long)ref);
    }
    return ref;
}

static int load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int cap = 1024; RvEnt *t = malloc(sizeof *t * (size_t)cap); int n = 0;
    char line[512];
    while (t && fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char *e; unsigned long long a = strtoull(line, &e, 0), b = strtoull(e, NULL, 0);
        if (e == line || !b) continue;
        if (n == cap) { cap *= 2; RvEnt *u = realloc(t, sizeof *t * (size_t)cap); if (!u) { free(t); t = NULL; break; } t = u; }
        t[n].ref = a; t[n].cur = b; n++;
    }
    fclose(f);
    if (!t) return -1;
    qsort(t, (size_t)n, sizeof *t, cmp_ent);
    for (int i = 1; i < n; i++) if (t[i].ref == t[i - 1].ref) { rig_log("rv: %s lists %#llx twice: not used", path, (unsigned long long)t[i].ref); free(t); return -2; }
    tab = t; ntab = n;
    return 0;
}

/* build = this game version, dir = the module folder (NULL: only DSRIG_RV_TABLE). 0 = addresses usable. */
int rig_rv_load(const char *build, const char *dir) {
    const char *forced = getenv("DSRIG_RV_TABLE");
    char p[1400];
    if (forced && *forced) snprintf(p, sizeof p, "%s", forced);
    else if (build && !strcmp(build, RIG_BUILD)) { rig_log("rv: game version %s is the reference version: no address table", build); return 0; }
    else if (!dir || !build) { rig_log("rv: game version %s: no module folder for its address table", build ? build : "?"); return -1; }
#ifdef _WIN32
    else snprintf(p, sizeof p, "%s/rva/%s_win.tsv", dir, build);
#else
    else snprintf(p, sizeof p, "%s/rva/%s_mac.tsv", dir, build);
#endif
    int r = load_file(p);
    if (r) { rig_log("rv: address table %s %s: multiplayer stays off on game version %s", p, r == -1 ? "not found" : "unusable", build ? build : "?"); return -1; }
    rig_rv_on = 1;
    if (build && strcmp(build, RIG_BUILD)) snprintf(game_build, sizeof game_build, "%s", build);
    rig_log("rv: address table %s: %d addresses (game version %s, reference %s)", p, ntab, build ? build : "?", RIG_BUILD);
    return 0;
}

/* rig_det.c: the caller ranges belong to one game version: det_ranges.tsv (reference; Windows det_ranges_win.tsv),
   det_ranges_<build>.tsv (another version; Windows det_ranges_win_<build>.tsv) */
#ifdef _WIN32
#define RANGES_STEM "det_ranges_win"
#else
#define RANGES_STEM "det_ranges"
#endif
const char *rig_rv_ranges_name(void) {
    static char n[64];
    if (!game_build[0]) return RANGES_STEM ".tsv";
    snprintf(n, sizeof n, RANGES_STEM "_%s.tsv", game_build);
    return n;
}
