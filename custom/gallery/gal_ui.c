/* gal_ui.c - the Gallery window (Custom Modes page > Gallery; owner 2026-10-15: "integrate the build gallery ... they can
 * choose in the gallery build units or teams they want to select"). The wiki's public Build Gallery, read-only:
 * Teams / Units, Most popular / Newest, the wiki's filters (Races, Weapon trees, Complete only) -> a card -> "Save to My
 * Units" (myunits.h) or "Test Fight" (tf.h). Texts are the gallery page's own (site/builds, C.UI).
 * Links are read by lane units' decoder (wk_link.h, the one rule for old links); the Test Fight squad is decoded by the
 * wiki's own builder (sg_decode_blob), so the fight is the one the wiki's "Test this team" makes. */
#include "gallery.h"
#include "../fights/cm_win.h"
#include "../fights/cm_work.h"
#include "../fights/tf.h"
#include "../fights/sg_js.h"
#include "myunits.h"
#include "cm_home.h"
#include "wk_link.h"
#include "../fe/fe_json.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 1040.0f
#define WIN_H 720.0f
#define BODY_H (WIN_H - CMW_BAND - 1)
#define GOLD 0xD9BE8FFFu
#define SUB 0xA69085FFu
#define WARN 0xEA9F3DFFu
#define MAIN 0xE8DCC8FFu
#define CARD_W 484.0f
#define CARD_H 196.0f
#define MAXIT (GAL_PAGE_SIZE * (GAL_MAX_PAGE + 1))

static const DSLUi *U;
static DSLObj body, grid_holder, w_status, w_more;
static gal_words *words; static int words_tried;
static struct { int chars, recent, race, tree, complete; } q = { 0, 0, 0, 0, 1 };
static gal_item *items; static int nitems, page, more, loading, gen;   /* gen: a filter change makes older answers stale */
static char status[600]; static int status_warn;
static const char *race_keys[16], *tree_keys[40]; static int nrace, ntree;

/* ------------------------------------------------------------------ worker jobs */
typedef struct { cm_job j; int gen, page; gal_query q; char race[64], tree[64]; gal_page pg; gal_words *w; char err[300]; int ok; } PageJob;
static void page_run(cm_job *j) {
    PageJob *b = (PageJob *)j;
    if (!words && !words_tried) b->w = gal_words_fetch(b->err, sizeof b->err);
    const char *r1[1] = { b->race }, *t1[1] = { b->tree };
    if (b->race[0]) { b->q.races[0] = r1[0]; b->q.nraces = 1; }
    if (b->tree[0]) { b->q.trees[0] = t1[0]; b->q.ntrees = 1; }
    b->ok = gal_fetch_page(&b->q, &b->pg, b->err, sizeof b->err) == 0;
}
static void paint(void);
static void page_done(cm_job *j) {
    PageJob *b = (PageJob *)j;
    if (b->w) { words = b->w; words_tried = 1; }
    else if (!words) words_tried = 1;
    if (b->gen == gen) {
        loading = 0;
        if (b->ok) {
            if (b->page == 0) nitems = 0;
            for (int i = 0; i < b->pg.n && nitems < MAXIT; i++) items[nitems++] = b->pg.items[i];
            more = b->pg.more && page < GAL_MAX_PAGE; page = b->page;
            status[0] = 0;
            if (!nitems) { snprintf(status, sizeof status, "No public builds match these filters yet."); status_warn = 0; }
        } else { snprintf(status, sizeof status, "Could not load the gallery. Try again later."); status_warn = 1; cmw_log("gal: %s", b->err); }
    }
    gal_page_free(&b->pg); free(b);
    paint();
}
static void fetch(int pg) {
    if (pg == 0) gen++;
    PageJob *b = calloc(1, sizeof *b); b->j.run = page_run; b->j.done = page_done; b->gen = gen; b->page = pg;
    b->q.chars = q.chars; b->q.recent = q.recent; b->q.incomplete = !q.complete; b->q.page = pg;
    if (q.race > 0) snprintf(b->race, sizeof b->race, "%s", race_keys[q.race - 1]);
    if (q.tree > 0) snprintf(b->tree, sizeof b->tree, "%s", tree_keys[q.tree - 1]);
    loading = 1; snprintf(status, sizeof status, "Loading builds..."); status_warn = 0;
    cm_work_post(&b->j);
}

/* a card's action: fetch the full item (its stored build), then save it or test it */
typedef struct { cm_job j; char id[32]; int test; gal_item it; char *blob, *builds; char err[400]; int ok; } ItemJob;
static void title_of(const gal_item *it, char *o, size_t n) { gal_item_title(it, words, o, n); }
extern sg_engine *tf_engine_for_gallery(char *err, size_t errn);        /* tf_ui.c: the engine, opened on this thread */
static void item_run(cm_job *j) {
    ItemJob *b = (ItemJob *)j;
    if (gal_fetch_item(b->id, &b->it, &b->blob, b->err, sizeof b->err) || !b->blob) { if (!b->err[0]) snprintf(b->err, sizeof b->err, "Could not load this team. Try again later."); return; }
    if (!b->test) { b->ok = 1; return; }
    sg_engine *e = tf_engine_for_gallery(b->err, sizeof b->err); if (!e) return;
    b->builds = sg_decode_blob(e, b->blob, b->err, sizeof b->err);
    b->ok = b->builds != NULL;
}
/* the one rule for links (lane units): the blob read by wk_link. 1 ok (n units), 0 refused (err) */
static int link_ok(const char *blob, wk_build *bs, int *n, char *err, size_t errn) {
    int code = 0; char t[8192];
    if (!strncmp(blob, "b:", 2)) snprintf(t, sizeof t, "#b=%s", blob + 2); else snprintf(t, sizeof t, "%s", blob);
    *n = wkl_decode_offline(t, bs, WK_MAX_UNITS, &code, err, errn);
    return *n > 0;
}
static void item_done(cm_job *j) {
    ItemJob *b = (ItemJob *)j; char title[200]; title_of(&b->it, title, sizeof title);
    wk_build *bs = calloc(WK_MAX_UNITS, sizeof *bs); int n = 0; char e[300] = "";
    if (!b->ok) { snprintf(status, sizeof status, "%s", b->err); status_warn = 1; }
    else if (!link_ok(b->blob, bs, &n, e, sizeof e)) { snprintf(status, sizeof status, "%s", e); status_warn = 1; }
    else if (b->test) {
        const char *names[GAL_MAX_UNITS]; char nb[GAL_MAX_UNITS][96]; int nn = 0;
        for (int i = 0; i < b->it.nunits && nn < GAL_MAX_UNITS; i++) {
            char m[96]; if (b->it.team) gal_member_name(&b->it, i, words, m, sizeof m); else snprintf(m, sizeof m, "%s", title);
            snprintf(nb[nn], 96, "%s  Lv %d", m, b->it.units[i].lvl); names[nn] = nb[nn]; nn++;
        }
        tf_open_with_builds(b->builds, title, names, nn);
        status[0] = 0;
    } else {
        jv *lib = mu_load(e, sizeof e); int r = -1;
        if (lib) {
            if (b->it.team) r = mu_add_team(lib, bs, n, title, NULL, e, sizeof e) < 0 ? -1 : 0;
            else {
                /* one character: the build JSON as stored ("b:" + base64), checked by mu_add_build (wk_from_json) */
                char t[8192]; snprintf(t, sizeof t, "%s", b->blob + 2);
                size_t L = strlen(t); for (size_t i = 0; i < L; i++) { if (t[i] == '-') t[i] = '+'; else if (t[i] == '_') t[i] = '/'; }
                static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                char *out = malloc(L + 4); size_t o = 0; unsigned acc = 0; int bits = 0;
                for (size_t i = 0; i < L; i++) { const char *p = strchr(A, t[i]); if (!p || !t[i]) continue; acc = acc << 6 | (unsigned)(p - A); bits += 6; if (bits >= 8) { bits -= 8; out[o++] = (char)(acc >> bits & 255); } }
                out[o] = 0;
                jv *bj = jparse(out, o, e, sizeof e); free(out);
                char id[17];
                r = bj ? mu_add_build(lib, bj, title, id, e, sizeof e) : -1;
                jfree(bj);
            }
            if (!r) r = mu_save(lib, e, sizeof e);
            jfree(lib);
        }
        if (!r) { snprintf(status, sizeof status, "Saved to My Units: %s", title); status_warn = 0; }
        else { snprintf(status, sizeof status, "%s", e[0] ? e : "My Units could not be written."); status_warn = 1; }
    }
    free(bs); free(b->blob); free(b->builds); free(b);
    paint();
}
static void card_action(int i, int test) {
    if (i < 0 || i >= nitems) return;
    ItemJob *b = calloc(1, sizeof *b); b->j.run = item_run; b->j.done = item_done; b->test = test;
    snprintf(b->id, sizeof b->id, "%s", items[i].id);
    snprintf(status, sizeof status, test ? "Opening the build..." : "Saving..."); status_warn = 0;
    cm_work_post(&b->j);
    paint();
}
static void save_click(void *ud, DSLObj w) { (void)w; card_action((int)(intptr_t)ud, 0); }
static void test_click(void *ud, DSLObj w) { (void)w; card_action((int)(intptr_t)ud, 1); }

/* ------------------------------------------------------------------ drawing */
static void kind_click(void *ud, DSLObj w) { (void)w; q.chars = (int)(intptr_t)ud; fetch(0); paint(); }
static void sort_click(void *ud, DSLObj w) { (void)w; q.recent = (int)(intptr_t)ud; fetch(0); paint(); }
static void race_change(void *ud, DSLObj w) { (void)ud; q.race = U->value_get(w); fetch(0); paint(); }
static void tree_change(void *ud, DSLObj w) { (void)ud; q.tree = U->value_get(w); fetch(0); paint(); }
static void complete_change(void *ud, DSLObj w) { (void)ud; q.complete = U->toggle_get(w); fetch(0); paint(); }
static void more_click(void *ud, DSLObj w) { (void)ud; (void)w; if (more && !loading) { fetch(page + 1); paint(); } }

/* a card = 5 widgets (panel, title, one text block, 2 buttons): the kit holds at most 1024 live widgets */
static void card(DSLObj p, int i, float x, float y) {
    const gal_item *it = &items[i]; char t[300], nm[48];
    cmw_flat(U->panel(p, x, y, CARD_W, CARD_H, 0), 0x2D2A31FF);
    title_of(it, t, sizeof t);
    snprintf(nm, sizeof nm, "gallery:c%d.title", i);
    cmw_named(U->text(p, x + 14, y + 8, CARD_W - 28, 26, t, 18, DSL_TXT_LEFT, GOLD), CMW_TEXT, nm, NULL, NULL, t);
    char au[160], body_t[1400]; size_t k = 0; gal_author_name(it, words, au, sizeof au);
    k += (size_t)snprintf(body_t + k, sizeof body_t - k, "by %s  -  Opens %d  -  Copies %d  -  Likes %d", au, it->opens, it->copies, it->likes);
    for (int u = 0; u < it->nunits && u < 4 && k < sizeof body_t; u++) {
        const gal_unit *g = &it->units[u]; char m[96], tr[120] = ""; size_t q2 = 0;
        if (it->team) gal_member_name(it, u, words, m, sizeof m); else snprintf(m, sizeof m, "%s", gal_word(words, "races", g->race));
        for (int z = 0; z < g->ntrees && z < 3; z++) q2 += (size_t)snprintf(tr + q2, sizeof tr - q2, "%s%s", z ? ", " : "", gal_word(words, "trees", g->trees[z])[0] ? gal_word(words, "trees", g->trees[z]) : g->trees[z]);
        k += (size_t)snprintf(body_t + k, sizeof body_t - k, "\n%s  Lv %d%s%s", m, g->lvl, tr[0] ? "  -  " : "", tr);
    }
    if (it->nunits > 4 && k < sizeof body_t) snprintf(body_t + k, sizeof body_t - k, "\n+%d", it->nunits - 4);
    U->text(p, x + 14, y + 36, CARD_W - 28, CARD_H - 36 - 50, body_t, 14, DSL_TXT_LEFT, MAIN);
    snprintf(nm, sizeof nm, "gallery:c%d.save", i);
    cmw_named(U->button(p, x + 14, y + CARD_H - 46, 220, 38, "Save to My Units", save_click, (void *)(intptr_t)i), CMW_BUTTON, nm, save_click, (void *)(intptr_t)i, "Save to My Units");
    snprintf(nm, sizeof nm, "gallery:c%d.test", i);
    cmw_named(U->button(p, x + CARD_W - 14 - 180, y + CARD_H - 46, 180, 38, "Test Fight", test_click, (void *)(intptr_t)i), CMW_BUTTON, nm, test_click, (void *)(intptr_t)i, "Test Fight");
}
static void paint(void) {
    if (!cmw_is_open("gallery") || !body) return;
    cmw_forget("gallery:c"); cmw_forget("gallery:f.");
    if (grid_holder) U->destroy(grid_holder);
    grid_holder = cmw_flat(U->panel(body, 0, 0, WIN_W, BODY_H - 70, 0), 0x00000000);
    /* filters (rebuilt with the grid: the race / tree lists arrive with the words) */
    static const char *kinds[2] = { "Teams", "Units" }, *sorts[2] = { "Most popular", "Newest" };
    for (int s = 0; s < 2; s++) {
        char nm[40]; snprintf(nm, sizeof nm, "gallery:f.kind%d", s);
        cmw_named(U->button(grid_holder, 24 + s * 134, 12, 128, 38, kinds[s], kind_click, (void *)(intptr_t)s), CMW_BUTTON, nm, kind_click, (void *)(intptr_t)s, kinds[s]);
        if (q.chars == s) cmw_flat(U->panel(grid_holder, 24 + s * 134, 52, 128, 3, 0), GOLD);
        snprintf(nm, sizeof nm, "gallery:f.sort%d", s);
        cmw_named(U->button(grid_holder, 310 + s * 164, 12, 158, 38, sorts[s], sort_click, (void *)(intptr_t)s), CMW_BUTTON, nm, sort_click, (void *)(intptr_t)s, sorts[s]);
        if (q.recent == s) cmw_flat(U->panel(grid_holder, 310 + s * 164, 52, 158, 3, 0), GOLD);
    }
    cmw_named(U->toggle(grid_holder, 660, 13, 356, "Complete only", q.complete, complete_change, NULL), CMW_TOGGLE, "gallery:f.complete", complete_change, NULL, "Complete only");
    if (words) {
        nrace = gal_word_keys(words, "races", race_keys, 16); ntree = gal_word_keys(words, "trees", tree_keys, 40);
        const char *rl[17] = { "Any" }, *tl[41] = { "Any" };
        static char rn[16][64], tn[40][64];
        for (int i = 0; i < nrace; i++) { snprintf(rn[i], 64, "%s", gal_word(words, "races", race_keys[i])); rl[i + 1] = rn[i]; }
        for (int i = 0; i < ntree; i++) { snprintf(tn[i], 64, "%s", gal_word(words, "trees", tree_keys[i])); tl[i + 1] = tn[i]; }
        cmw_named(U->value_row(grid_holder, 24, 64, 488, "Races", rl, nrace + 1, q.race, race_change, NULL), CMW_VALUE, "gallery:f.race", race_change, NULL, rl[q.race]);
        cmw_named(U->value_row(grid_holder, 528, 64, 488, "Weapon trees", tl, ntree + 1, q.tree, tree_change, NULL), CMW_VALUE, "gallery:f.tree", tree_change, NULL, tl[q.tree]);
    }
    DSLObj sc = U->scroll(grid_holder, 24, 120, WIN_W - 36, BODY_H - 70 - 128);
    float y = 0;
    for (int i = 0; i < nitems; i++) {
        float x = (i % 2) * (CARD_W + 12);
        card(sc, i, x, y);
        if (i % 2) y += CARD_H + 12;
    }
    if (nitems % 2) y += CARD_H + 12;
    U->scroll_height(sc, y);
    if (w_status) U->destroy(w_status);                               /* the kit has no colour setter: a problem shows orange */
    w_status = cmw_named(U->text(body, 24, BODY_H - 58, WIN_W - 300, 52, status, 15, DSL_TXT_LEFT | DSL_TXT_WRAP, status_warn ? WARN : MAIN), CMW_TEXT, "gallery:status", NULL, NULL, status);
    U->set_active(w_more, more);
    U->set_enabled(w_more, more && !loading);
}
static void closed(void *ud) { (void)ud; body = grid_holder = w_status = w_more = NULL; cmw_forget("gallery:"); }
void gal_open(void) {
    U = cmw_ui(); if (!U) return;
    if (!items) items = calloc(MAXIT, sizeof *items);
    if (!items) return;
    body = cmw_open("gallery", "Gallery", WIN_W, WIN_H, closed, NULL);
    if (!body) return;
    grid_holder = NULL;
    cmw_flat(U->panel(body, 24, BODY_H - 66, WIN_W - 48, 1, 0), 0x633D33FF);
    w_status = NULL;
    w_more = cmw_named(U->button(body, WIN_W - 24 - 220, BODY_H - 58, 220, 46, "Show more", more_click, NULL), CMW_BUTTON, "gallery:more", more_click, NULL, "Show more");
    fetch(0);
    paint();
}

static void test_state(char *out, size_t n) {
    size_t k = (size_t)snprintf(out, n, "\"gal\":{\"items\":%d,\"loading\":%d,\"more\":%d,\"words\":%d,\"status\":\"%.300s\",\"ids\":[", nitems, loading, more, words != NULL, status);
    for (int i = 0; i < nitems && i < 24 && k + 40 < n; i++) k += (size_t)snprintf(out + k, n - k, "%s\"%s\"", i ? "," : "", items[i].id);
    snprintf(out + k, n - k, "]}");
}

__attribute__((visibility("default"))) void cm_gallery_init(const DSLHost *host, DSLPlugin *self) {
    if (cmw_init(host, self)) return;
    if (cm_work_start()) { cmw_log("gal: worker thread did not start: Gallery is off"); return; }
    cmw_test_opener("gallery", gal_open);
    cmw_test_state(test_state);
    cm_home_add("gallery", "Gallery", NULL, 30, gal_open);
}
