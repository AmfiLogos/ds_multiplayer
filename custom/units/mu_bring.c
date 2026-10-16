// mu_bring.c - the Bring Units window (mu_bring.h; PLAN_V13 section 4, AM-2). Owner 2026-10-15: "when I save some units
// and I can select bring to pvp for expedition I have no way to actually select them"; the old list was "basic overflow
// of text and you don't have any icons for the units". One window for the Foreign Expedition hub and PvP: tabs
// "From a save" / "My Units", Compact Plus cards (mu_card_draw, the game's own icons), a tick per card, a per-player cap,
// "New unit" (the Unit Builder, when it is in the build). Same look and code style as mu_window.c (My Units).
// Read only: a save is only read (mu_save.c), the library is only read here (the builder writes it, not this window).
#include "mu_bring.h"
#include "mu_ui.h"
#include "mu_save.h"
#include "myunits.h"
#include "cm_il.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>

/* the Unit Builder's door (mu_window.c declares the same weak import) */
int mu_builder_present(void);

static const DSLUi *U;
#define NMAX 400          /* units listed on the right (one save, or the library) */
#define SMAX 64           /* saves in the left column */
#define PMAX 64           /* picks */
#define TMAX 64           /* teams in the left column */

static struct {
    int open;
    mu_bring_req req; char title[128], sub[300];
    DSLObj win, t_sub, tab_save, tab_lib, hdr, b_new, status, t_picked, left, right, b_prev, b_next, t_page, b_cancel, b_bring;
    DSLObj left_btn[SMAX + TMAX + 1]; int nleft;
    DSLObj tick[16], card[16]; int card_item[16], ncard;
    float w, h, rx, ry, rw, rh;
    int tab;                                   /* MU_SRC_SAVE / MU_SRC_LIBRARY */
    /* From a save */
    mu_save_ref saves[SMAX]; int nsaves, sel_save;
    mu_save_unit su[NMAX]; int nsu; char su_err[400]; int su_for;   /* units of saves[su_for] (-1 none loaded) */
    /* My Units */
    jv *lib; char err[400];
    int sel_team;                              /* -1 = All units */
    char ids[NMAX][17]; int nids;
    /* picks (survive tab and page changes) */
    mu_pick picks[PMAX]; int npicks, max;
    int page, per_page, pages;
    char msg[400]; uint32_t msg_col;
    int esc_prev;
    float lw, lh;                              /* the screen layer size the window was built for */
} B = { .su_for = -1, .sel_team = -1 };

/* the done call, between frames, after the window closed */
static struct { int pending; void (*fn)(const mu_pick *, int, void *); void *ud; mu_pick p[PMAX]; int n; } D;

static void refresh(void);
static void bar_update(void);
static int build_ui(void);
static void tick_fn(void);

static void set_msg(const char *m, uint32_t col) {
    if (m != B.msg) snprintf(B.msg, sizeof B.msg, "%s", m ? m : "");
    B.msg_col = col;
    if (B.status) {
        char t[460]; snprintf(t, sizeof t, "%s%s%s", col == MU_C_BAD ? "<color=#E06A5A>" : "", B.msg, col == MU_C_BAD ? "</color>" : "");
        U->set_text(B.status, t);
    }
}

/* ------------------------------------------------------------------ saves */
static void pretty_save_name(const char *path, char *out, size_t cap) {
    const char *f = strrchr(path, '/'); f = f ? f + 1 : path;
    { const char *b = strrchr(f, '\\'); if (b) f = b + 1; }
    const char *e = strstr(f, "__"); size_t n = e ? (size_t)(e - f) : strlen(f);
    if (!e && n > 5 && !strcmp(f + n - 5, ".json")) n -= 5;
    size_t o = 0;
    for (size_t i = 0; i < n && o + 2 < cap; i++) {                  /* "MoonlightBrigade" -> "Moonlight Brigade" */
        char c = f[i];
        if (i && c >= 'A' && c <= 'Z' && f[i - 1] >= 'a' && f[i - 1] <= 'z') out[o++] = ' ';
        out[o++] = c == '_' ? ' ' : c;
    }
    out[o] = 0;
    if (!o) snprintf(out, cap, "Save");
}
static void load_saves(void) {
    B.nsaves = 0; B.su_for = -1;
    if (B.req.list_saves) {
        int n = B.req.list_saves(B.saves, SMAX, B.req.ud);
        B.nsaves = n < 0 ? 0 : n > SMAX ? SMAX : n;
        for (int i = 0; i < B.nsaves; i++) if (!B.saves[i].name[0]) pretty_save_name(B.saves[i].path, B.saves[i].name, sizeof B.saves[i].name);
        return;
    }
    char p[1200] = ""; cm_m m = cm_il_method("UnityEngine", "Application", "get_persistentDataPath", 0, NULL);
    if (m.fn) cm_il_utf8(CM_CALL(m, Obj, const void *)(m.mi), p, sizeof p);
    if (!p[0]) return;
    char dir[1300]; snprintf(dir, sizeof dir, "%s/Saves", p);
    static char files[SMAX][1024];
    int n = mu_save_files(dir, files, SMAX);
    for (int i = 0; i < n && B.nsaves < SMAX; i++) {
        mu_save_ref *s = &B.saves[B.nsaves++];
        snprintf(s->path, sizeof s->path, "%s", files[i]);
        pretty_save_name(files[i], s->name, sizeof s->name);
        struct stat st; s->when[0] = 0;
        if (!stat(files[i], &st)) { struct tm tm; time_t t = st.st_mtime; localtime_r(&t, &tm); strftime(s->when, sizeof s->when, "%Y-%m-%d %H:%M", &tm); }
    }
}
static void load_save_units(void) {
    if (B.su_for == B.sel_save) return;
    B.nsu = 0; B.su_err[0] = 0; B.su_for = B.sel_save;
    if (B.sel_save < 0 || B.sel_save >= B.nsaves) return;
    int n = mu_save_units(B.saves[B.sel_save].path, B.su, NMAX, B.su_err, sizeof B.su_err);
    B.nsu = n < 0 ? 0 : n;
    for (int i = 0; i < B.nsu; i++) {                                  /* the game's default names are text keys */
        if (!strncmp(B.su[i].name, "TEXTKEY_", 8)) { char t[96]; if (!cm_gettext(B.su[i].name, t, sizeof t)) snprintf(B.su[i].name, sizeof B.su[i].name, "%s", t); }
        if (!B.su[i].name[0]) snprintf(B.su[i].name, sizeof B.su[i].name, "Unit");
    }
}

/* ------------------------------------------------------------------ library */
static void load_lib(void) {
    if (B.lib) jfree(B.lib);
    B.lib = mu_load(B.err, sizeof B.err);
    if (B.lib && B.sel_team >= (int)mu_team_count(B.lib)) B.sel_team = -1;
}
static int lib_info(const char *id, mu_info *out) {
    for (size_t i = 0, n = B.lib ? mu_count(B.lib) : 0; i < n; i++) if (!mu_info_at(B.lib, i, out) && !strcmp(out->id, id)) return 0;
    return -1;
}
static void list_ids(void) {
    B.nids = 0;
    if (!B.lib) return;
    if (B.sel_team >= 0) {
        char nm[200], ids[MU_TEAM_MAX][17];
        int n = mu_team_at(B.lib, (size_t)B.sel_team, nm, sizeof nm, ids);
        for (int i = 0; i < n && B.nids < NMAX; i++) memcpy(B.ids[B.nids++], ids[i], 17);
    } else {
        size_t n = mu_count(B.lib);
        for (size_t i = 0; i < n && B.nids < NMAX; i++) { mu_info inf; if (!mu_info_at(B.lib, i, &inf)) memcpy(B.ids[B.nids++], inf.id, 17); }
    }
}

/* ------------------------------------------------------------------ picks */
static int pick_find_save(const char *path, const char *guid) {
    for (int i = 0; i < B.npicks; i++) if (B.picks[i].source == MU_SRC_SAVE && !strcmp(B.picks[i].save_file, path) && !strcmp(B.picks[i].unit_guid, guid)) return i;
    return -1;
}
static int pick_find_lib(const char *id) {
    for (int i = 0; i < B.npicks; i++) if (B.picks[i].source == MU_SRC_LIBRARY && !strcmp(B.picks[i].lib_id, id)) return i;
    return -1;
}
static void pick_remove(int i) { if (i < 0 || i >= B.npicks) return; memmove(&B.picks[i], &B.picks[i + 1], (size_t)(B.npicks - i - 1) * sizeof *B.picks); B.npicks--; }
static void refuse_msg(void) {
    char m[200]; snprintf(m, sizeof m, "You can bring %d unit%s at most. Untick one first.", B.max, B.max == 1 ? "" : "s");
    set_msg(m, MU_C_BAD);
}
static int pick_add_lib(const char *id) {
    if (pick_find_lib(id) >= 0) return 0;
    if (B.npicks >= B.max) return -1;
    mu_pick *p = &B.picks[B.npicks++]; memset(p, 0, sizeof *p);
    p->source = MU_SRC_LIBRARY; snprintf(p->lib_id, sizeof p->lib_id, "%s", id);
    mu_info inf; if (!lib_info(id, &inf)) { snprintf(p->name, sizeof p->name, "%s", inf.name); p->level = inf.lvl; }
    return 0;
}
/* item k of the right list: is it picked? */
static int item_pick(int k) {
    if (B.tab == MU_SRC_SAVE) return k < B.nsu && B.sel_save >= 0 ? pick_find_save(B.saves[B.sel_save].path, B.su[k].guid) : -1;
    return k < B.nids ? pick_find_lib(B.ids[k]) : -1;
}

/* ------------------------------------------------------------------ closing and done */
static void close_win(void) {
    if (!B.open) return;
    B.open = 0;
    if (U && B.win) U->destroy(B.win);
    B.win = B.left = B.right = NULL; B.ncard = B.nleft = 0;
    if (B.lib) { jfree(B.lib); B.lib = NULL; }
    mu_save_cache_clear();
    mu_drawn_reset();
}
static void fire_done(void) {
    if (!D.pending) return;
    D.pending = 0;
    if (D.fn) D.fn(D.n >= 0 ? D.p : NULL, D.n, D.ud);
}
static void finish(int ok) {
    if (!B.open) return;
    D.pending = 1; D.fn = B.req.done; D.ud = B.req.ud; D.n = ok ? B.npicks : -1;
    if (ok) memcpy(D.p, B.picks, (size_t)B.npicks * sizeof *B.picks);
    mu_log("bring units: %s (%d picked)", ok ? "Bring" : "Cancel", ok ? B.npicks : -1);
    close_win();
    if (mu_tick_add(tick_fn)) { mu_tick_remove(tick_fn); fire_done(); }   /* no timer slot: call it now (after the close) */
}
static void tick_fn(void) {
    if (D.pending) fire_done();
    if (B.open && B.win) {                                            /* the screen layer changed size: build again */
        float lw = 0, lh = 0;
        if (!cm_rect_size(cm_parent(B.win), &lw, &lh) && lw >= 100 && lh >= 100 && (fabsf(lw - B.lw) > 1 || fabsf(lh - B.lh) > 1)) {
            mu_log("bring units: screen layer %.0fx%.0f -> %.0fx%.0f, window built again", B.lw, B.lh, lw, lh);
            U->destroy(B.win); B.win = NULL;
            mu_drawn_reset();
            if (build_ui()) { finish(0); return; }
            refresh();
        }
    }
    if (B.open) {                                                     /* Escape = Cancel (edge: pressed now, not before) */
        static cm_m k; if (!k.fn) k = cm_il_method("UnityEngine", "Input", "GetKey", 1, "UnityEngine.KeyCode");
        int on = k.fn ? (int)CM_CALL(k, bool, int, const void *)(27, k.mi) : 0;
        int edge = on && !B.esc_prev; B.esc_prev = on;
        if (edge) { finish(0); return; }
    }
    if (!B.open && !D.pending) mu_tick_remove(tick_fn);
}

/* ------------------------------------------------------------------ actions */
static void on_cancel(void *ud, DSLObj w) { (void)ud; (void)w; finish(0); }
static void on_bring(void *ud, DSLObj w) { (void)ud; (void)w; if (B.npicks > 0) finish(1); }
static void on_tab(void *ud, DSLObj w) {
    (void)w; int t = (int)(intptr_t)ud;
    if (t == B.tab || !(B.req.sources & t)) return;
    B.tab = t; B.page = 0; set_msg("", MU_C_SUB); refresh();
}
static void on_left(void *ud, DSLObj w) {
    (void)w; int i = (int)(intptr_t)ud;
    B.page = 0;
    if (B.tab == MU_SRC_SAVE) { B.sel_save = i; set_msg("", MU_C_SUB); refresh(); return; }
    B.sel_team = i;                                                   /* -1 = All units */
    set_msg("", MU_C_SUB);
    if (i >= 0 && B.lib) {                                            /* a team: tick its units */
        char nm[200], ids[MU_TEAM_MAX][17];
        int n = mu_team_at(B.lib, (size_t)i, nm, sizeof nm, ids), added = 0, had = 0, left = 0;
        for (int k = 0; k < n; k++) {
            if (pick_find_lib(ids[k]) >= 0) { had++; continue; }
            if (pick_add_lib(ids[k])) left++; else added++;
        }
        char m[300];
        if (left) snprintf(m, sizeof m, "Ticked %d of the %d units of %s: you can bring %d at most.", added + had, n, nm, B.max);
        else snprintf(m, sizeof m, "Ticked the %d units of %s.", n, nm);
        set_msg(m, left ? MU_C_BAD : MU_C_GOLD);
    }
    refresh();
}
static void on_tick(void *ud, DSLObj w) {
    int c = (int)(intptr_t)ud; if (c < 0 || c >= B.ncard) return;
    int k = B.card_item[c], on = U->toggle_get(w), pi = item_pick(k);
    if (!on) { pick_remove(pi); set_msg("", MU_C_SUB); bar_update(); return; }
    if (pi >= 0) return;
    if (B.npicks >= B.max) { U->toggle_set(w, 0); refuse_msg(); return; }
    mu_pick *p = &B.picks[B.npicks]; memset(p, 0, sizeof *p);
    if (B.tab == MU_SRC_SAVE) {
        if (B.sel_save < 0 || k >= B.nsu) { U->toggle_set(w, 0); return; }
        p->source = MU_SRC_SAVE;
        snprintf(p->save_file, sizeof p->save_file, "%s", B.saves[B.sel_save].path);
        snprintf(p->unit_guid, sizeof p->unit_guid, "%s", B.su[k].guid);
        snprintf(p->name, sizeof p->name, "%s", B.su[k].name);
        p->level = B.su[k].level;
        B.npicks++;
    } else if (pick_add_lib(B.ids[k])) { U->toggle_set(w, 0); refuse_msg(); return; }
    set_msg("", MU_C_SUB);
    bar_update();
}
static void on_page(void *ud, DSLObj w) {
    (void)w; B.page += (int)(intptr_t)ud;
    if (B.page >= B.pages) B.page = B.pages - 1;
    if (B.page < 0) B.page = 0;
    refresh();
}
static void builder_saved(const char *unit_id, void *ud) {
    (void)ud; if (!B.open) return;
    load_lib();
    B.tab = MU_SRC_LIBRARY; B.sel_team = -1; B.page = 0;
    list_ids();
    if (unit_id && *unit_id && B.lib && mu_find(B.lib, unit_id)) {
        for (int i = 0; i < B.nids; i++) if (!strcmp(B.ids[i], unit_id)) { B.page = B.per_page > 0 ? i / B.per_page : 0; break; }
        if (pick_add_lib(unit_id)) { char m[200]; snprintf(m, sizeof m, "Saved to My Units. Not ticked: you can bring %d at most.", B.max); set_msg(m, MU_C_BAD); }
        else set_msg("Saved to My Units and ticked.", MU_C_GOLD);
    }
    refresh();
}
static void on_new(void *ud, DSLObj w) { (void)ud; (void)w; if (cm_builder_open) cm_builder_open(NULL, builder_saved, NULL); }

/* ------------------------------------------------------------------ drawing */
static void tab_labels(void) {
    char t[80];
    if (B.tab_save) { snprintf(t, sizeof t, "%sFrom a save%s", B.tab == MU_SRC_SAVE ? "<color=#D9BE8F>" : "", B.tab == MU_SRC_SAVE ? "</color>" : ""); U->set_text(B.tab_save, t); }
    if (B.tab_lib) { snprintf(t, sizeof t, "%sMy Units%s", B.tab == MU_SRC_LIBRARY ? "<color=#D9BE8F>" : "", B.tab == MU_SRC_LIBRARY ? "</color>" : ""); U->set_text(B.tab_lib, t); }
}
static DSLObj left_row(float y, const char *label, int sel, int ud, const char *tip_t, const char *tip_b) {
    char t[300]; snprintf(t, sizeof t, "%s%s%s", sel ? "<color=#D9BE8F>" : "", label, sel ? "</color>" : "");
    DSLObj b = mu_button(B.left, 0, y, 192, 40, t, on_left, (void *)(intptr_t)ud);
    if (b) { cm_text_ellipsis(b); if (tip_t) U->tooltip(b, tip_t, tip_b); }
    if (B.nleft < (int)(sizeof B.left_btn / sizeof *B.left_btn)) B.left_btn[B.nleft++] = b;
    return b;
}
static void draw_left(void) {
    if (B.left) U->destroy(B.left);
    B.left = U->scroll(B.win, 20, B.ry, 200, B.rh);
    B.nleft = 0;
    if (!B.left) return;
    float y = 0;
    if (B.tab == MU_SRC_SAVE) {
        for (int i = 0; i < B.nsaves; i++, y += 44) {
            char body[64]; snprintf(body, sizeof body, "%s%s", B.saves[i].when[0] ? "Saved " : "", B.saves[i].when);
            left_row(y, B.saves[i].name, i == B.sel_save, i, B.saves[i].name, body[0] ? body : " ");
        }
        if (!B.nsaves) { mu_text(B.left, 4, 0, 184, 80, "No saves found.", 15, DSL_TXT_WRAP, MU_C_SUB); y = 80; }
    } else {
        char t[200];
        snprintf(t, sizeof t, "All units (%zu)", B.lib ? mu_count(B.lib) : (size_t)0);
        left_row(y, t, B.sel_team < 0, -1, NULL, NULL); y += 44;
        size_t nt = B.lib ? mu_team_count(B.lib) : 0;
        for (size_t i = 0; i < nt && i < TMAX; i++, y += 44) {
            char nm[200], ids[MU_TEAM_MAX][17]; int n = mu_team_at(B.lib, i, nm, sizeof nm, ids);
            snprintf(t, sizeof t, "%s (%d)", nm, n);
            left_row(y, t, B.sel_team == (int)i, (int)i, nm, "Pick this team: its units are ticked.");
        }
    }
    U->scroll_height(B.left, y);
}
static void draw_cards(void) {
    if (B.right) U->destroy(B.right);
    B.right = U->panel(B.win, B.rx, B.ry, B.rw, B.rh, MU_C_BAND);
    B.ncard = 0;
    mu_drawn_reset();
    if (!B.right) return;
    cm_raycast(B.right, false);
    int n = 0; const char *empty = NULL; uint32_t ecol = MU_C_SUB;
    if (B.tab == MU_SRC_SAVE) {
        load_save_units();
        n = B.nsu;
        if (!B.nsaves) empty = "No saves to bring units from.";
        else if (B.su_err[0]) { empty = B.su_err; ecol = MU_C_BAD; }
        else if (!n) empty = "This save has no units.";
    } else {
        list_ids();
        n = B.nids;
        if (!B.lib) { empty = B.err; ecol = MU_C_BAD; }
        else if (!n) empty = B.sel_team < 0 ? "No units in My Units yet. Add some in My Units (home page), or press \"New unit\"." : "This team has no units.";
    }
    B.per_page = (int)((B.rh - 8) / (MU_CARD_H + 8)); if (B.per_page < 1) B.per_page = 1; if (B.per_page > 16) B.per_page = 16;
    B.pages = (n + B.per_page - 1) / B.per_page; if (B.pages < 1) B.pages = 1;
    if (B.page >= B.pages) B.page = B.pages - 1;
    if (B.page < 0) B.page = 0;
    if (empty) { mu_text(B.right, 16, 16, B.rw - 32, 90, empty, 16, DSL_TXT_WRAP, ecol); return; }
    static wk_build b; static mu_card c; static mu_save_extra x;
    for (int s = 0; s < B.per_page; s++) {
        int k = B.page * B.per_page + s; if (k >= n) break;
        float y = 8 + s * (MU_CARD_H + 8);
        char e[300] = ""; DSLObj card = NULL;
        B.card_item[B.ncard] = k;
        B.tick[B.ncard] = U->toggle(B.right, 8, y + (MU_CARD_H - 36) / 2, 36, "", item_pick(k) >= 0, on_tick, (void *)(intptr_t)B.ncard);
        if (B.tab == MU_SRC_SAVE) {
            if (!mu_save_build_ex(B.saves[B.sel_save].path, B.su[k].guid, &b, &x, e, sizeof e) && !mu_card_of(&b, &c)) {
                const char *bar[8]; for (int i = 0; i < x.nbar && i < 8; i++) bar[i] = x.bar[i];
                card = mu_card_draw(B.right, 52, y, B.rw - 60, &c, B.su[k].name, B.su[k].level, B.su[k].portrait, bar, x.nbar);
            }
        } else if (!mu_build_of(B.lib, B.ids[k], &b, e, sizeof e) && !mu_card_of(&b, &c)) {
            mu_info inf; const char *nm = !lib_info(B.ids[k], &inf) ? inf.name : NULL;
            card = mu_card_draw(B.right, 52, y, B.rw - 60, &c, nm, 0, NULL, NULL, 0);
        }
        if (!card) {
            card = U->panel(B.right, 52, y, B.rw - 60, MU_CARD_H, MU_C_ROW);
            if (card) mu_text(card, 12, 10, B.rw - 84, MU_CARD_H - 20, e[0] ? e : "This unit cannot be shown.", 14, DSL_TXT_WRAP, MU_C_BAD);
        }
        B.card[B.ncard++] = card;
    }
}
static void bar_update(void) {
    if (!B.open) return;
    char t[80]; snprintf(t, sizeof t, "Picked %d / %d", B.npicks, B.max); U->set_text(B.t_picked, t);
    snprintf(t, sizeof t, "%d / %d", B.page + 1, B.pages); U->set_text(B.t_page, t);
    U->set_enabled(B.b_prev, B.page > 0); U->set_enabled(B.b_next, B.page + 1 < B.pages);
    U->set_enabled(B.b_bring, B.npicks > 0);
}
static void refresh(void) {
    if (!B.open) return;
    tab_labels();
    draw_left();
    draw_cards();
    bar_update();
}

/* ------------------------------------------------------------------ open */
int mu_bring_open(const mu_bring_req *req) {
    U = mu_kit();
    if (!U || B.open || !req || req->max_units < 1 || !(req->sources & (MU_SRC_SAVE | MU_SRC_LIBRARY))) return -1;
    if (D.pending) { mu_tick_remove(tick_fn); fire_done(); }         /* the last window's answer goes first */
    B.req = *req;
    snprintf(B.title, sizeof B.title, "%s", req->title && *req->title ? req->title : "Bring Units");
    snprintf(B.sub, sizeof B.sub, "%s", req->subtitle ? req->subtitle : "");
    B.req.title = B.title; B.req.subtitle = B.sub;
    B.max = req->max_units > PMAX ? PMAX : req->max_units;
    B.npicks = 0;
    for (int i = 0; req->ticked && i < req->nticked && B.npicks < B.max; i++)
        if (req->ticked[i].source & B.req.sources) B.picks[B.npicks++] = req->ticked[i];
    B.req.ticked = NULL; B.req.nticked = 0;
    B.page = 0; B.pages = 1; B.sel_team = -1; B.sel_save = 0; B.su_for = -1; B.msg[0] = 0; B.msg_col = MU_C_SUB;
    B.tab = (B.req.sources & MU_SRC_SAVE) ? MU_SRC_SAVE : MU_SRC_LIBRARY;
    if (build_ui()) { mu_log("bring units: kit not ready"); return -1; }
    { char p[1200] = ""; cm_m m = cm_il_method("UnityEngine", "Application", "get_persistentDataPath", 0, NULL);
      if (m.fn) { cm_il_utf8(CM_CALL(m, Obj, const void *)(m.mi), p, sizeof p); if (p[0]) mu_set_data_dir(p); } }
    if (B.req.sources & MU_SRC_SAVE) load_saves();
    if (B.req.sources & MU_SRC_LIBRARY) load_lib();
    B.esc_prev = 1;                                                   /* a key already down does not count */
    mu_tick_add(tick_fn);
    refresh();
    mu_log("bring units: open (max %d, sources %d, %d saves, %d library units, %d ticked; %.0fx%.0f on a %.0fx%.0f layer)", B.max,
           B.req.sources, B.nsaves, B.lib ? (int)mu_count(B.lib) : -1, B.npicks, B.w, B.h, B.lw, B.lh);
    return 0;
}

/* the window and its fixed widgets (the lists and cards come from refresh). Again when the screen layer changes size:
   the kit's layer can still be at its default size on the very first window of a run (it is laid out a frame later),
   and the UI scale or the game window can change while the window is open. */
static int build_ui(void) {
    float w, h;
    B.win = mu_window(B.title, 1100, 720, &w, &h);
    if (!B.win) return -1;
    B.open = 1; B.w = w; B.h = h;
    B.lw = B.lh = 0; cm_rect_size(cm_parent(B.win), &B.lw, &B.lh);
    B.left = B.right = NULL; B.nleft = B.ncard = 0;
    B.tab_save = B.tab_lib = B.hdr = B.b_new = NULL;
    B.t_sub = mu_text(B.win, 20, 46, w - 40, 24, B.sub, 15, DSL_TXT_CENTER, MU_C_SUB);
    /* top row: tabs (or the one source's name), New unit, the message */
    float x = 20, y = 76;
    int both = (B.req.sources & MU_SRC_SAVE) && (B.req.sources & MU_SRC_LIBRARY);
    if (both) {
        B.tab_save = mu_button(B.win, x, y, 170, 40, "From a save", on_tab, (void *)(intptr_t)MU_SRC_SAVE); x += 176;
        B.tab_lib = mu_button(B.win, x, y, 170, 40, "My Units", on_tab, (void *)(intptr_t)MU_SRC_LIBRARY); x += 180;
    } else {
        B.hdr = mu_text(B.win, x, y, 200, 40, B.tab == MU_SRC_SAVE ? "From a save" : "My Units", 19, DSL_TXT_LEFT, MU_C_GOLD); x += 210;
    }
    if ((B.req.sources & MU_SRC_LIBRARY) && mu_builder_present()) {
        B.b_new = mu_button(B.win, x, y, 140, 40, "New unit", on_new, NULL); x += 150;
        U->tooltip(B.b_new, "New unit", "Make a unit in the Unit Builder. When you save it, it goes to My Units and is ticked here.");
    }
    B.status = mu_text(B.win, x, y, w - 20 - x, 40, "", 14, DSL_TXT_LEFT, MU_C_SUB);
    /* lists */
    B.ry = y + 50; B.rh = h - B.ry - 64; B.rx = 232; B.rw = w - B.rx - 20;
    /* bottom: Picked n / max, pages, Cancel, Bring */
    float by = h - 54;
    DSLObj bot = U->panel(B.win, 20, by, w - 40, 44, 0x00000000u);
    if (bot) {
        cm_raycast(bot, false);
        float bw = w - 40;
        B.t_picked = mu_text(bot, 0, 0, 200, 44, "", 16, DSL_TXT_LEFT, MU_C_GOLD);
        float px = 212 + (bw - 212 - 290 - 200) / 2; if (px < 212) px = 212;
        B.b_prev = mu_button(bot, px, 2, 44, 40, "<", on_page, (void *)(intptr_t)-1);
        B.t_page = mu_text(bot, px + 48, 0, 104, 44, "", 14, DSL_TXT_CENTER, MU_C_SUB);
        B.b_next = mu_button(bot, px + 156, 2, 44, 40, ">", on_page, (void *)(intptr_t)1);
        B.b_cancel = mu_button(bot, bw - 290, 2, 140, 40, "Cancel", on_cancel, NULL);
        B.b_bring = mu_button(bot, bw - 140, 2, 140, 40, "Bring", on_bring, NULL);
    }
    if (B.msg[0]) set_msg(B.msg, B.msg_col);
    return 0;
}
void mu_bring_close(void) { finish(0); }
int  mu_bring_is_open(void) { return B.open; }

/* ------------------------------------------------------------------ tests (driver "bring:<name>") */
DSLObj mu_bring_test_find(const char *n) {
    if (!B.open) return NULL;
    static const struct { const char *n; DSLObj *o; } map[] = {
        { "tab_save", &B.tab_save }, { "tab_lib", &B.tab_lib }, { "bring", &B.b_bring }, { "cancel", &B.b_cancel },
        { "new", &B.b_new }, { "next", &B.b_next }, { "prev", &B.b_prev }, { "win", &B.win }, { "right", &B.right },
        { "status", &B.status }, { "picked", &B.t_picked } };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(n, map[i].n)) return *map[i].o;
    int k;
    if (sscanf(n, "left%d", &k) == 1 && k >= 0 && k < B.nleft) return B.left_btn[k];
    if (sscanf(n, "tick%d", &k) == 1 && k >= 0 && k < B.ncard) return B.tick[k];
    if (sscanf(n, "card%d", &k) == 1 && k >= 0 && k < B.ncard) return B.card[k];
    return NULL;
}
static void jesc(char *o, size_t cap, const char *s) { size_t n = 0; for (; *s && n + 3 < cap; s++) { if (*s == '"' || *s == '\\') o[n++] = '\\'; if ((unsigned char)*s < 0x20) continue; o[n++] = *s; } o[n] = 0; }
size_t mu_bring_test_state(char *out, size_t cap) {
    char m[600]; jesc(m, sizeof m, B.msg);
    return (size_t)snprintf(out, cap, "{\"open\":%d,\"tab\":%d,\"picked\":%d,\"max\":%d,\"left_rows\":%d,\"cards\":%d,\"page\":%d,"
                            "\"pages\":%d,\"msg\":\"%s\",\"saves\":%d,\"sel_save\":%d,\"sel_team\":%d,\"units\":%d,\"builder\":%d,\"w\":%.0f,\"h\":%.0f}",
                            B.open, B.tab, B.npicks, B.max, B.open ? B.nleft : 0, B.open ? B.ncard : 0, B.page, B.pages, m,
                            B.nsaves, B.sel_save, B.sel_team, B.tab == MU_SRC_SAVE ? B.nsu : B.nids, mu_builder_present(), B.w, B.h);
}
