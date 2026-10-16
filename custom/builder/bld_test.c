/* bld_test - test hooks for the in-game Unit Builder test (ext/builder13/test/builder_test.py), called by the test driver's
 * "call" command (ext/units13/driver: dlsym in custom-modes). Nothing calls them in play.
 *   bld_test_open(id or "")      the Unit Builder, new unit or Edit (a saved() callback records the id)
 *   bld_test_saved               the id the last saved() callback got ("" none)
 *   bld_test_bring_open          Bring Units (both tabs, 4 units) -> its "New unit" opens the Unit Builder
 *   bld_test_bring_result        the picks Bring Units handed back: "lib:<id>|save:<guid>;..." or "cancel" / ""
 *   bld_test_list(kind)          keys the window lists now ("key,on,locked;...")
 *   bld_test_act(json)           one click by its action JSON (the level row) */
#include "bld.h"
#include "mu_bring.h"
#include <stdio.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))
static char saved_id[17], bring_res[2048];
static void on_saved(const char *id, void *ud) { (void)ud; snprintf(saved_id, sizeof saved_id, "%s", id ? id : ""); }
EXPORT int bld_test_open(const char *s, char *out, size_t n) {
    saved_id[0] = 0; cm_builder_open(s && *s ? s : NULL, on_saved, NULL);
    snprintf(out, n, "%d", bld_is_open()); return bld_is_open() ? 0 : -1;
}
EXPORT int bld_test_saved(const char *s, char *out, size_t n) { (void)s; snprintf(out, n, "%s", saved_id); return saved_id[0] ? 0 : 1; }
static void bring_done(const mu_pick *p, int np, void *ud) {
    (void)ud; size_t o = 0; bring_res[0] = 0;
    if (np < 0) { snprintf(bring_res, sizeof bring_res, "cancel"); return; }
    for (int i = 0; i < np && o + 80 < sizeof bring_res; i++)
        o += (size_t)snprintf(bring_res + o, sizeof bring_res - o, "%s%s:%s", i ? ";" : "", p[i].source == MU_SRC_LIBRARY ? "lib" : "save",
                              p[i].source == MU_SRC_LIBRARY ? p[i].lib_id : p[i].unit_guid);
}
EXPORT int bld_test_bring_open(const char *s, char *out, size_t n) {
    (void)s; bring_res[0] = 0;
    mu_bring_req r; memset(&r, 0, sizeof r);
    r.title = "Bring Units"; r.subtitle = "Unit Builder test"; r.max_units = 4; r.sources = MU_SRC_SAVE | MU_SRC_LIBRARY; r.done = bring_done;
    int rc = mu_bring_open(&r); snprintf(out, n, "%d", rc); return rc;
}
int bld_test_keys(const char *kind, char *out, size_t n);
int bld_test_act_json(const char *json);
EXPORT int bld_test_list(const char *s, char *out, size_t n) { return bld_test_keys(s, out, n); }
EXPORT int bld_test_act(const char *s, char *out, size_t n) { int rc = bld_test_act_json(s); snprintf(out, n, "%d", rc); return rc; }
EXPORT int bld_test_bring_result(const char *s, char *out, size_t n) { (void)s; snprintf(out, n, "%s", bring_res); return 0; }
