// cm_home.c - the Custom Modes home (PLAN_V13 R4; interface in cm_home.h).
// Title button "Custom Modes" (loader TITLE API, order 110 = right below the loader's "Mods" at 100; no hover card,
// owner v12 1). Its click opens the page the way Multiplayer > Host a game does: the stock title column is hidden and a
// column of the same kind takes its place, with one button per registered entry (lowest order first) and Back.
// How: the page column is a copy of the stock column (Object.Instantiate: same RectTransform, same layout group, so the
// same place and spacing), emptied, then filled with dsl_ui kit buttons (each a clone of the stock title button). The
// stock column is switched off as a whole while the page shows (the stock buttons, the multiplayer clones and the loader's
// buttons are in it), and on again on Back. Nothing is hooked; every il2cpp call goes by name through the host, so the
// same code builds for Windows.
#include "cm_home.h"
#include "cm_il.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AT CM_AT

static const DSLHost *H; static DSLPlugin *ME; static const DSLUi *U;
const DSLHost *cm_host(void) { return H; }
DSLPlugin *cm_self(void) { return ME; }

/* ------------------------------------------------------------------ entries */
#define EMAX 12
typedef struct { char id[48], label[96], tip[400]; int order, seq; void (*open)(void); } Entry;
static Entry ents[EMAX]; static int nents, seq_n;
int cm_home_add(const char *id, const char *label, const char *tip, int order, void (*open)(void)) {
    if (!id || !*id || !label || !*label || !open || nents >= EMAX) return -1;
    for (int i = 0; i < nents; i++) if (!strcmp(ents[i].id, id)) return -1;
    Entry *e = &ents[nents++]; memset(e, 0, sizeof *e);
    snprintf(e->id, sizeof e->id, "%s", id); snprintf(e->label, sizeof e->label, "%s", label);
    snprintf(e->tip, sizeof e->tip, "%s", tip ? tip : "");
    e->order = order; e->seq = seq_n++; e->open = open;
    if (H) H->log(ME, "home: entry \"%s\" (order %d)", label, order);
    return 0;
}
static int ent_cmp(const void *a, const void *b) {
    const Entry *x = a, *y = b;
    return x->order != y->order ? (x->order < y->order ? -1 : 1) : x->seq - y->seq;
}

static int32_t f_credit = -1; static void *k_view; static int il_ok = -1;
static int il_init(void) {
    if (il_ok == 1) return 1;
    if (!cm_il_init(H, ME)) return 0;
    k_view = cm_il_class("Refactor.UI", "TitleMenuView");
    f_credit = cm_il_field(k_view, "_creditButton");
    if (!k_view || f_credit < 0) { H->log(ME, "home: TitleMenuView._creditButton not found"); return il_ok = 0; }
    return il_ok = 1;
}
#define alive cm_alive
#define go_of cm_go
#define tr_of cm_tr
#define set_active cm_set_active
#define tr_parent cm_parent
#define child_count cm_child_count
#define child_at cm_child
#define hget cm_il_hget
#define hset cm_il_hset

/* ------------------------------------------------------------------ the page */
static uintptr_t h_view, h_col, h_page;                     /* title view seen last, its stock column, our column (Transforms) */
static DSLObj bt[EMAX + 1]; static Entry ord[EMAX]; static int nbt, page_on;
static Obj title_view(void) {
    return cm_find(k_view, false);
}
static void on_entry(void *ud, DSLObj w) {
    (void)w; int i = (int)(intptr_t)ud;
    if (i < 0 || i >= nbt - 1) return;
    H->log(ME, "home: \"%s\" clicked", ord[i].label);
    ord[i].open();
}
static void on_back(void *ud, DSLObj w) { (void)ud; (void)w; H->log(ME, "home: Back"); cm_home_close(); }

static int build_page(Obj view) {
    Obj credit = AT(view, f_credit, Obj), col = tr_parent(tr_of(credit));
    if (!col) { H->log(ME, "home: title column not found"); return -1; }
    Obj par = tr_parent(col);
    Obj pt = cm_instantiate(col, par);                        /* a component in = its clone's component out */
    if (!alive(pt)) { H->log(ME, "home: page column copy failed"); return -1; }
    cm_set_sibling(pt, cm_sibling(col) + 1);
    for (int i = child_count(pt) - 1; i >= 0; i--) {              /* empty the copy (stock buttons, MP and loader clones) */
        Obj c = child_at(pt, i), g = go_of(c);
        set_active(g, false); cm_destroy(g);
    }
    memcpy(ord, ents, sizeof ents); qsort(ord, (size_t)nents, sizeof *ord, ent_cmp);
    nbt = 0;
    for (int i = 0; i < nents; i++) {
        bt[nbt] = U->button((DSLObj)pt, 0, 0, 262, 44, ord[i].label, on_entry, (void *)(intptr_t)i);
        if (bt[nbt] && ord[i].tip[0]) U->tooltip(bt[nbt], ord[i].label, ord[i].tip);
        nbt++;
    }
    bt[nbt++] = U->button((DSLObj)pt, 0, 0, 262, 44, "<color=#A69085>Back</color>", on_back, NULL);
    hset(&h_view, view); hset(&h_col, col); hset(&h_page, pt);
    H->log(ME, "home: page built (%d entries + Back)", nents);
    return 0;
}
void cm_home_open(void) {
    if (!U || !il_init()) { if (H) H->log(ME, "home: cannot open (kit %s, il2cpp %d)", U ? "ok" : "missing", il_ok); return; }
    Obj v = title_view(); if (!v) { H->log(ME, "home: no title screen"); return; }
    if (hget(h_view) != v || !alive(hget(h_page)) || !alive(hget(h_col))) {
        if (alive(hget(h_page))) cm_destroy(go_of(hget(h_page)));
        if (build_page(v)) return;
    }
    set_active(go_of(hget(h_col)), false);
    set_active(go_of(hget(h_page)), true);
    page_on = 1;
    H->log(ME, "home: page open");
}
void cm_home_close(void) {
    if (!page_on) return;
    page_on = 0;
    set_active(go_of(hget(h_page)), false);
    set_active(go_of(hget(h_col)), true);
}
static void on_title(void *ud) { (void)ud; cm_home_open(); }

int cm_home_test_state(char *out, size_t cap) {
    size_t o = 0; if (out && cap) out[0] = 0;
    int shown = page_on && il_ok == 1 && alive(hget(h_page)) && hget(h_view) == title_view();
    for (int i = 0; i < nbt && out; i++) {
        o += (size_t)snprintf(out + o, o < cap ? cap - o : 0, "%s%s", i ? ";" : "", i < nbt - 1 ? ord[i].label : "Back");
    }
    return shown;
}
DSLObj cm_home_test_button(const char *label) {
    if (!label) return NULL;
    for (int i = 0; i < nbt; i++) if (!strcmp(label, i < nbt - 1 ? ord[i].label : "Back")) return bt[i];
    return NULL;
}

/* ------------------------------------------------------------------ test registry */
#define TMAX 8
static struct { char prefix[16]; cm_test_find_fn find; cm_test_state_fn state; } tr[TMAX]; static int ntr;
int cm_test_register(const char *prefix, cm_test_find_fn find, cm_test_state_fn state) {
    if (!prefix || ntr >= TMAX) return -1;
    snprintf(tr[ntr].prefix, sizeof tr[ntr].prefix, "%s", prefix); tr[ntr].find = find; tr[ntr].state = state; ntr++;
    return 0;
}
DSLObj cm_test_target(const char *name) {
    const char *c = name ? strchr(name, ':') : NULL; if (!c) return NULL;
    for (int i = 0; i < ntr; i++)
        if (strlen(tr[i].prefix) == (size_t)(c - name) && !strncmp(name, tr[i].prefix, (size_t)(c - name)) && tr[i].find) return tr[i].find(c + 1);
    return NULL;
}
size_t cm_test_state(char *out, size_t cap) {
    size_t o = 0; if (!out || !cap) return 0; out[0] = 0;
    for (int i = 0; i < ntr; i++) {
        if (!tr[i].state) continue;
        o += (size_t)snprintf(out + o, o < cap ? cap - o : 0, "%s\"%s\":", o ? "," : "", tr[i].prefix);
        if (o < cap) o += tr[i].state(out + o, cap - o);
    }
    return o < cap ? o : cap - 1;
}
static size_t home_state(char *out, size_t cap) {
    char l[600]; int on = cm_home_test_state(l, sizeof l);
    return (size_t)snprintf(out, cap, "{\"open\":%d,\"buttons\":\"%s\"}", on, l);
}

int cm_home_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self; U = dsl_ui(host);
    cm_test_register("home", cm_home_test_button, home_state);
    if (!U) { host->log(self, "home: no widget kit (loader older than API 3): the Custom Modes button is off"); return 0; }
    int r = dsl_title_add_button(host, self, "Custom Modes", 110, NULL, on_title, NULL);
    host->log(self, "home: title button \"Custom Modes\" %s", r ? "refused" : "added");
    return 0;
}
