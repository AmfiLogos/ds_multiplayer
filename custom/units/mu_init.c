// mu_init.c - My Units' entry point in custom-modes.dylib (cm_units_init, PLAN_V13 section 4) and its test hooks.
#include "mu_ui.h"
#include "mu_link.h"
#include "mu_bring.h"
#include "cm_home.h"
#include "cm_il.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void mu_window_open(void);
DSLObj mu_window_test_find(const char *n);
size_t mu_window_test_state(char *out, size_t cap);
DSLObj mu_bring_test_find(const char *n);
size_t mu_bring_test_state(char *out, size_t cap);

void cm_units_init(const DSLHost *host, DSLPlugin *self) {
    mu_ui_init(host, self);
    if (!mu_kit()) { host->log(self, "My Units: no widget kit (loader older than API 3)"); return; }
    cm_home_add("my-units", "My Units", NULL, 10, mu_window_open);
    cm_test_register("mu", mu_window_test_find, mu_window_test_state);
    cm_test_register("bring", mu_bring_test_find, mu_bring_test_state);
}

/* ------------------------------------------------------------------ test calls (driver "call": int fn(s, out, cap)) */
#define TEST_API __attribute__((visibility("default")))
TEST_API int mu_test_clip(const char *s, char *out, size_t cap) {          /* the DSMP_CLIP_PB test pasteboard ("" = empty) */
    int rc = mu_clip_test_put(s ? s : "");
    snprintf(out, cap, "%d", rc);
    return rc;
}
/* every text and icon of the cards on screen: inside its card? texts: rendered width <= box width? */
typedef struct { float x, y, z; } V3f;
typedef struct { V3f c, e; } Bounds;
TEST_API int mu_test_overflow(const char *s, char *out, size_t cap) {
    (void)s;
#ifdef _WIN32
    int (*rect)(DSLObj, float *) = NULL;                                  /* tests run on the Mac */
#else
    int (*rect)(DSLObj, float *) = (int (*)(DSLObj, float *))dlsym(RTLD_DEFAULT, "dsl_ui_test_rect");
#endif
    if (!rect) { snprintf(out, cap, "no dsl_ui_test_rect"); return -1; }
    static cm_m m_bounds; if (!m_bounds.fn) m_bounds = cm_il_method("TMPro", "TMP_Text", "get_textBounds", 0, NULL);
    void *k_tmp = cm_il_class("TMPro", "TMP_Text");
    const mu_drawn *d; int n = mu_drawn_list(&d), bad = 0, texts = 0, icons = 0, cut = 0; size_t o = 0;
    o += (size_t)snprintf(out + o, cap - o, "[");
    for (int i = 0; i < n; i++) {
        float r[4], c[4]; DSLObj card = d[i].card >= 0 ? mu_drawn_card(d[i].card) : NULL;
        if (rect(d[i].o, r) <= 0) continue;                                  /* not shown */
        if (card && rect(card, c) <= 0) continue;
        int out_of = card && (r[0] < c[0] - 1 || r[1] < c[1] - 1 || r[2] > c[2] + 1 || r[3] > c[3] + 1);
        if (!card) memcpy(c, r, sizeof c);
        float bw = 0, boxw = 0;
        if (d[i].is_text) {
            texts++;
            Obj t = cm_text_of(d[i].o); (void)k_tmp;
            if (t && m_bounds.fn) { Bounds b = CM_CALL(m_bounds, Bounds, Obj, const void *)(t, m_bounds.mi); bw = 2 * b.e.x; }
            cm_rect_size(t ? cm_tr(t) : d[i].o, &boxw, NULL);
            if (bw > boxw + 1) out_of = 1;
            if (cm_text_truncated(d[i].o)) cut++;
        } else icons++;
        if (out_of) {
            bad++;
            if (o + 200 < cap) o += (size_t)snprintf(out + o, cap - o, "%s{\"card\":%d,\"text\":%d,\"r\":[%.0f,%.0f,%.0f,%.0f],\"c\":[%.0f,%.0f,%.0f,%.0f],\"bw\":%.1f,\"boxw\":%.1f}",
                                                     bad > 1 ? "," : "", d[i].card, d[i].is_text, r[0], r[1], r[2], r[3], c[0], c[1], c[2], c[3], bw, boxw);
        }
    }
    if (o + 100 < cap) o += (size_t)snprintf(out + o, cap - o, "] texts %d icons %d ellipsis %d outside %d", texts, icons, cut, bad);
    return bad;
}
/* Bring Units opened from a test (driver call): s = "max,sources" e.g. "4,3"; the picks land in mu_test_bring_result */
static char bring_res[4096];
static void bring_done(const mu_pick *p, int n, void *ud) {
    (void)ud; size_t o = 0;
    o += (size_t)snprintf(bring_res + o, sizeof bring_res - o, "%d:", n);
    for (int i = 0; i < n && o + 200 < sizeof bring_res; i++)
        o += (size_t)snprintf(bring_res + o, sizeof bring_res - o, "%s%d|%s|%s|%s|%d", i ? ";" : "", p[i].source,
                              p[i].source == MU_SRC_SAVE ? p[i].unit_guid : p[i].lib_id, p[i].name,
                              p[i].source == MU_SRC_SAVE ? strrchr(p[i].save_file, '/') ? strrchr(p[i].save_file, '/') + 1 : p[i].save_file : "", p[i].level);
    mu_log("bring units (test): %s", bring_res);
}
TEST_API int mu_test_bring_open(const char *s, char *out, size_t cap) {
    int mx = 4, src = MU_SRC_SAVE | MU_SRC_LIBRARY;
    if (s && *s) sscanf(s, "%d,%d", &mx, &src);
    bring_res[0] = 0;
    mu_bring_req r = { .title = "Bring Units", .subtitle = "Test: pick your units", .max_units = mx, .sources = src, .done = bring_done };
    int rc = mu_bring_open(&r);
    snprintf(out, cap, "%d", rc);
    return rc;
}
TEST_API int mu_test_bring_result(const char *s, char *out, size_t cap) { (void)s; snprintf(out, cap, "%s", bring_res); return bring_res[0] ? 0 : 1; }

/* ------------------------------------------------------------------ rig commands (multiplayer's test socket, "mu_" prefix;
   registered by cm_module.c when multiplayer runs): mu_bring_open {"max":N,"sources":S}, mu_bring_result, mu_bring_state,
   mu_bring_close, mu_open (My Units), mu_state, mu_overflow. Returns malloc'd JSON. */
static int jint(const char *line, const char *k, int d) {
    char pat[40]; snprintf(pat, sizeof pat, "\"%s\":", k);
    const char *p = line ? strstr(line, pat) : NULL; return p ? atoi(p + strlen(pat)) : d;
}
static char *dupf(const char *fmt, const char *a) { size_t n = strlen(fmt) + (a ? strlen(a) : 0) + 1; char *s = malloc(n); if (s) snprintf(s, n, fmt, a ? a : ""); return s; }
char *mu_rig_exec(const char *cmd, const char *line) {
    static char out[8192];
    if (!strcmp(cmd, "mu_bring_open")) {
        char a[32]; snprintf(a, sizeof a, "%d,%d", jint(line, "max", 4), jint(line, "sources", MU_SRC_SAVE | MU_SRC_LIBRARY));
        int rc = mu_test_bring_open(a, out, sizeof out);
        return dupf(rc ? "{\"ok\":false,\"rc\":%s}" : "{\"ok\":true,\"rc\":%s}", out);
    }
    if (!strcmp(cmd, "mu_bring_result")) { int rc = mu_test_bring_result("", out, sizeof out); return dupf(rc ? "{\"done\":false}%s" : "{\"done\":true,\"picks\":\"%s\"}", out); }
    if (!strcmp(cmd, "mu_bring_state")) { mu_bring_test_state(out, sizeof out); return dupf("%s", out); }
    if (!strcmp(cmd, "mu_bring_close")) { mu_bring_close(); return dupf("{\"ok\":true}%s", ""); }
    if (!strcmp(cmd, "mu_open")) { void mu_window_open(void); mu_window_open(); mu_window_test_state(out, sizeof out); return dupf("%s", out); }
    if (!strcmp(cmd, "mu_state")) { mu_window_test_state(out, sizeof out); return dupf("%s", out); }
    if (!strcmp(cmd, "mu_overflow")) {
        static char o2[16384]; int bad = mu_test_overflow("", o2, sizeof o2);
        char e[17000]; size_t n = 0; for (const char *c = o2; *c && n + 2 < sizeof e; c++) { if (*c == '"' || *c == '\\') e[n++] = '\\'; e[n++] = *c; } e[n] = 0;
        char *s = malloc(n + 64); if (s) snprintf(s, n + 64, "{\"outside\":%d,\"detail\":\"%s\"}", bad, e); return s;
    }
    return NULL;
}
