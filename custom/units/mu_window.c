// mu_window.c - the My Units window (home page entry "My Units", PLAN_V13 R4 / owner 2026-10-15: "I just don't
// understand how the my units thing is supposed to work"). One window: the units and teams of the My Units library
// (myunits.h; the library file, never a save), Compact Plus cards, "Add from wiki link" in ONE click (mu_link.h: the
// clipboard is read; only without a link a box asks to paste), short links, tick units -> "Save as team", Rename,
// Delete, and "New unit" / "Edit" when the Unit Builder is in the build (cm_builder_open).
// Custom units stay out of regular saves: this window only edits the library file; Bring Units hands out copies.
#include "mu_ui.h"
#include "mu_link.h"
#include "myunits.h"
#include "cm_home.h"
#include "cm_il.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* the Unit Builder's door (lane builder, PLAN_V13 section 4); absent = "New unit" / "Edit" hidden */
int mu_builder_present(void) { return cm_builder_open != NULL; }

static const DSLUi *U;
#define NMAX 400
static struct {
    int open;
    int box_on;
    DSLObj box_add, box_cancel, b_rename_ok;
    DSLObj win, status, box, box_field, rename_row, rename_field, left, right, bottom, pager;
    DSLObj b_add, b_new, b_close, b_team, b_rename, b_edit, b_delete, b_delteam, b_prev, b_next, t_sel, t_page;
    DSLObj team_btn[64]; int nteam_btn; DSLObj all_btn;
    DSLObj tick[16], card[16]; char card_id[16][17]; int ncard;
    float w, h, rx, ry, rw, rh, lw, lh, scale;
    jv *lib; char err[400];
    int sel_team;                     /* -1 = All units */
    char ids[NMAX][17]; int nids;     /* the list shown (all units or the team's) */
    char ticked[NMAX][17]; int nticked;
    int page, per_page;
    char msg[400]; uint32_t msg_col;
} W = { .sel_team = -1 };

static void refresh(void);
static void win_poll(void);
static void poll_fn(void) { win_poll(); }
static int build_ui(void);
static void set_msg(const char *m, uint32_t col) {
    snprintf(W.msg, sizeof W.msg, "%s", m ? m : ""); W.msg_col = col;
    if (W.status) U->set_text(W.status, W.msg[0] ? W.msg : "");
}
static int is_ticked(const char *id) { for (int i = 0; i < W.nticked; i++) if (!strcmp(W.ticked[i], id)) return 1; return 0; }
static void untick(const char *id) { for (int i = 0; i < W.nticked; i++) if (!strcmp(W.ticked[i], id)) { memmove(W.ticked[i], W.ticked[i + 1], (size_t)(W.nticked - i - 1) * 17); W.nticked--; return; } }
static void load_lib(void) {
    if (W.lib) jfree(W.lib);
    W.lib = mu_load(W.err, sizeof W.err);
    for (int i = W.nticked - 1; i >= 0; i--) if (!W.lib || !mu_find(W.lib, W.ticked[i])) untick(W.ticked[i]);
    if (W.lib && W.sel_team >= (int)mu_team_count(W.lib)) W.sel_team = -1;
}
static int save_lib(void) {
    char e[400];
    if (!W.lib || mu_save(W.lib, e, sizeof e)) { char m[500]; snprintf(m, sizeof m, "Could not save My Units: %s", W.lib ? e : W.err); set_msg(m, MU_C_BAD); return -1; }
    return 0;
}
static void name_of(const char *id, char *out, size_t cap) {
    out[0] = 0; mu_info inf;
    for (size_t i = 0, n = W.lib ? mu_count(W.lib) : 0; i < n; i++) if (!mu_info_at(W.lib, i, &inf) && !strcmp(inf.id, id)) { snprintf(out, cap, "%s", inf.name); return; }
}
static void list_ids(void) {
    W.nids = 0;
    if (!W.lib) return;
    if (W.sel_team >= 0) {
        char nm[200], ids[MU_TEAM_MAX][17];
        int n = mu_team_at(W.lib, (size_t)W.sel_team, nm, sizeof nm, ids);
        for (int i = 0; i < n && W.nids < NMAX; i++) memcpy(W.ids[W.nids++], ids[i], 17);
    } else {
        size_t n = mu_count(W.lib);
        for (size_t i = 0; i < n && W.nids < NMAX; i++) { mu_info inf; if (!mu_info_at(W.lib, i, &inf)) memcpy(W.ids[W.nids++], inf.id, 17); }
    }
}

/* ------------------------------------------------------------------ actions */
static void close_win(void) {
    if (!W.open) return;
    W.open = 0;
    mu_tick_remove(poll_fn);
    if (W.win) U->destroy(W.win);
    W.win = NULL; W.box = W.rename_row = NULL;
    if (W.lib) { jfree(W.lib); W.lib = NULL; }
}
static void on_close(void *ud, DSLObj w) { (void)ud; (void)w; close_win(); }
static void show_box(int on) { W.box_on = on; if (W.box) U->set_active(W.box, on); }
static void win_poll(void) {
    if (!W.open) return;
    if (W.win) {
        float lw = 0, lh = 0;
        if (!cm_rect_size(cm_parent(W.win), &lw, &lh) && lw >= 100 && lh >= 100
            && (fabsf(lw - W.lw) > 1 || fabsf(lh - W.lh) > 1 || fabsf(mu_ui_scale() - W.scale) > 0.001f)) {
            mu_log("my units: screen layer %.0fx%.0f -> %.0fx%.0f, window built again", W.lw, W.lh, lw, lh);
            U->destroy(W.win); W.win = NULL; mu_drawn_reset();
            if (build_ui()) { close_win(); return; }
            refresh();
        }
    }
    mul_result r;
    int st = mul_poll(&r);
    if (st == MUL_DONE || st == MUL_FAILED) {
        set_msg(r.msg, st == MUL_DONE ? MU_C_GOLD : MU_C_BAD);
        mu_log("my units: link %s: %s", st == MUL_DONE ? "added" : "failed", r.msg);
        mul_reset();
        if (st == MUL_DONE) { load_lib(); W.sel_team = W.lib ? (int)mu_team_count(W.lib) - 1 : -1; W.page = 0; show_box(0); refresh(); }
    }
}
static void after_click(int st) {
    if (st == MUL_BUSY) { set_msg("Reading the link...", MU_C_SUB); show_box(0); }
    else if (st == MUL_NEED_BOX) {
        set_msg("No wiki link on the clipboard. Paste one below.", MU_C_SUB);
        show_box(1); if (W.box_field) U->field_set(W.box_field, "");
    }
}
static void on_add(void *ud, DSLObj w) { (void)ud; (void)w; after_click(mul_click()); }
static void on_box_add(void *ud, DSLObj w) {
    (void)ud; (void)w; char t[4096] = ""; U->field_get(W.box_field, t, sizeof t);
    int st = mul_submit(t);
    if (st == MUL_NEED_BOX) set_msg("That is not a wiki builder link. Copy the link from the wiki's Unit Builder.", MU_C_BAD);
    else after_click(st);
}
static void on_box_cancel(void *ud, DSLObj w) { (void)ud; (void)w; show_box(0); set_msg("", MU_C_SUB); }
static void builder_saved(const char *unit_id, void *ud) {
    (void)ud; if (!W.open) return;
    load_lib();
    if (unit_id && *unit_id && W.lib && mu_find(W.lib, unit_id) && !is_ticked(unit_id) && W.nticked < NMAX) memcpy(W.ticked[W.nticked++], unit_id, 17);
    set_msg("Saved to My Units.", MU_C_GOLD);
    refresh();
}
static void on_new(void *ud, DSLObj w) { (void)ud; (void)w; if (cm_builder_open) cm_builder_open(NULL, builder_saved, NULL); }
static void on_edit(void *ud, DSLObj w) { (void)ud; (void)w; if (cm_builder_open && W.nticked == 1) cm_builder_open(W.ticked[0], builder_saved, NULL); }
static void on_team(void *ud, DSLObj w) { (void)w; W.sel_team = (int)(intptr_t)ud; W.page = 0; refresh(); }
static void on_tick(void *ud, DSLObj w) {
    int i = (int)(intptr_t)ud; if (i < 0 || i >= W.ncard) return;
    const char *id = W.card_id[i];
    if (U->toggle_get(w)) { if (!is_ticked(id) && W.nticked < NMAX) memcpy(W.ticked[W.nticked++], id, 17); }
    else untick(id);
    refresh();
}
static void on_save_team(void *ud, DSLObj w) {
    (void)ud; (void)w; if (!W.lib || W.nticked < 1) return;
    if (W.nticked > MU_TEAM_MAX) { char m[120]; snprintf(m, sizeof m, "A team holds %d units at most.", MU_TEAM_MAX); set_msg(m, MU_C_BAD); return; }
    char tid[17], e[300], nm[64];
    snprintf(nm, sizeof nm, "Team %zu", mu_team_count(W.lib) + 1);
    if (mu_team_new(W.lib, nm, W.ticked, W.nticked, tid, e, sizeof e)) { set_msg(e, MU_C_BAD); return; }
    if (save_lib()) return;
    char m[200]; snprintf(m, sizeof m, "Saved %d units as %s.", W.nticked, nm); set_msg(m, MU_C_GOLD);
    W.sel_team = (int)mu_team_count(W.lib) - 1; W.page = 0; W.nticked = 0;
    refresh();
}
static void rename_show(int on) {
    if (W.rename_row) U->set_active(W.rename_row, on);
    if (W.bottom) U->set_active(W.bottom, !on);
}
static void on_rename(void *ud, DSLObj w) {
    (void)ud; (void)w; if (!W.lib || W.nticked != 1) return;
    char cur[64]; name_of(W.ticked[0], cur, sizeof cur);
    U->field_set(W.rename_field, cur);
    rename_show(1);
}
static void on_rename_ok(void *ud, DSLObj w) {
    (void)ud; (void)w; char t[64] = ""; U->field_get(W.rename_field, t, sizeof t);
    char *s = t; while (*s == ' ') s++;
    size_t L = strlen(s); while (L && s[L - 1] == ' ') s[--L] = 0;
    if (!*s) { set_msg("Type a name first.", MU_C_BAD); return; }
    if (W.lib && W.nticked == 1 && !mu_rename(W.lib, W.ticked[0], s) && !save_lib()) set_msg("Renamed.", MU_C_GOLD);
    rename_show(0); refresh();
}
static void on_rename_cancel(void *ud, DSLObj w) { (void)ud; (void)w; rename_show(0); }
static void del_ok(void *ud, DSLObj w) {
    (void)ud; (void)w; if (!W.open || !W.lib) return;
    int n = 0;
    for (int i = 0; i < W.nticked; i++) if (!mu_delete(W.lib, W.ticked[i])) n++;
    if (!save_lib()) { char m[120]; snprintf(m, sizeof m, "Deleted %d unit%s.", n, n == 1 ? "" : "s"); set_msg(m, MU_C_GOLD); }
    W.nticked = 0; load_lib(); refresh();
}
static void on_delete(void *ud, DSLObj w) {
    (void)ud; (void)w; if (W.nticked < 1) return;
    char m[300]; snprintf(m, sizeof m, "Delete %d unit%s from My Units?\nThey also leave their teams. Your saves are not changed.", W.nticked, W.nticked == 1 ? "" : "s");
    U->notice(m, "Delete", "Cancel", del_ok, NULL, NULL);
}
static void delteam_ok(void *ud, DSLObj w) {
    (void)ud; (void)w; if (!W.open || !W.lib || W.sel_team < 0) return;
    char tid[17]; if (!mu_team_id_at(W.lib, (size_t)W.sel_team, tid) && !mu_team_delete(W.lib, tid) && !save_lib()) set_msg("Team deleted. Its units stay in My Units.", MU_C_GOLD);
    W.sel_team = -1; W.page = 0; refresh();
}
static void on_delteam(void *ud, DSLObj w) {
    (void)ud; (void)w; if (W.sel_team < 0) return;
    U->notice("Delete this team?\nIts units stay in My Units.", "Delete team", "Cancel", delteam_ok, NULL, NULL);
}
static void on_page(void *ud, DSLObj w) {
    (void)w; int d = (int)(intptr_t)ud, pages = W.per_page ? (W.nids + W.per_page - 1) / W.per_page : 1;
    W.page += d; if (W.page < 0) W.page = 0; if (W.page >= pages) W.page = pages ? pages - 1 : 0;
    refresh();
}

/* ------------------------------------------------------------------ drawing */
static void draw_left(void) {
    if (W.left) U->destroy(W.left);
    W.left = U->scroll(W.win, 20, W.ry, 200, W.rh);
    W.nteam_btn = 0;
    if (!W.left) return;
    float y = 0; char t[200];
    snprintf(t, sizeof t, "%sAll units (%zu)%s", W.sel_team < 0 ? "<color=#D9BE8F>" : "", W.lib ? mu_count(W.lib) : (size_t)0, W.sel_team < 0 ? "</color>" : "");
    W.all_btn = mu_button(W.left, 0, y, 192, 40, t, on_team, (void *)(intptr_t)-1); y += 44;
    size_t nt = W.lib ? mu_team_count(W.lib) : 0;
    for (size_t i = 0; i < nt && W.nteam_btn < 64; i++) {
        char nm[200], ids[MU_TEAM_MAX][17]; int n = mu_team_at(W.lib, i, nm, sizeof nm, ids);
        int sel = W.sel_team == (int)i;
        snprintf(t, sizeof t, "%s%s (%d)%s", sel ? "<color=#D9BE8F>" : "", nm, n, sel ? "</color>" : "");
        DSLObj b = mu_button(W.left, 0, y, 192, 40, t, on_team, (void *)(intptr_t)i);
        W.team_btn[W.nteam_btn++] = b; y += 44;
    }
    U->scroll_height(W.left, y);
}
static void draw_cards(void) {
    if (W.right) U->destroy(W.right);
    W.right = U->panel(W.win, W.rx, W.ry, W.rw, W.rh, MU_C_BAND);
    W.ncard = 0;
    if (!W.right) return;
    cm_raycast(W.right, false);
    mu_drawn_reset();
    if (!W.lib) { mu_text(W.right, 16, 16, W.rw - 32, 60, W.err, 15, DSL_TXT_WRAP, MU_C_BAD); return; }
    if (!W.nids) {
        mu_text(W.right, 16, 16, W.rw - 32, 90, W.sel_team < 0
            ? "No units yet. On the wiki, open the Unit Builder, copy the link of a unit or a team, then press \"Add from wiki link\"."
            : "This team has no units.", 16, DSL_TXT_WRAP, MU_C_SUB);
        return;
    }
    W.per_page = (int)((W.rh - 8) / (MU_CARD_H + 8)); if (W.per_page < 1) W.per_page = 1; if (W.per_page > 16) W.per_page = 16;
    int pages = (W.nids + W.per_page - 1) / W.per_page; if (W.page >= pages) W.page = pages - 1;
    for (int k = 0; k < W.per_page; k++) {
        int i = W.page * W.per_page + k; if (i >= W.nids) break;
        float y = 8 + k * (MU_CARD_H + 8);
        wk_build b; mu_card c; char e[300];
        memcpy(W.card_id[W.ncard], W.ids[i], 17);
        W.tick[W.ncard] = U->toggle(W.right, 8, y + (MU_CARD_H - 36) / 2, 36, "", is_ticked(W.ids[i]), on_tick, (void *)(intptr_t)W.ncard);
        if (!mu_build_of(W.lib, W.ids[i], &b, e, sizeof e) && !mu_card_of(&b, &c)) {
            char nm[64]; name_of(W.ids[i], nm, sizeof nm);
            W.card[W.ncard] = mu_card_draw(W.right, 52, y, W.rw - 60, &c, nm, 0, NULL, NULL, 0);
        } else {
            DSLObj p = U->panel(W.right, 52, y, W.rw - 60, MU_CARD_H, MU_C_ROW);
            mu_text(p, 12, 10, W.rw - 84, MU_CARD_H - 20, e, 14, DSL_TXT_WRAP, MU_C_BAD);
            W.card[W.ncard] = p;
        }
        W.ncard++;
    }
}
static void refresh(void) {
    if (!W.open) return;
    list_ids();
    draw_left();
    draw_cards();
    int pages = W.per_page ? (W.nids + W.per_page - 1) / W.per_page : 1; if (pages < 1) pages = 1;
    char t[64]; snprintf(t, sizeof t, "%d selected", W.nticked); U->set_text(W.t_sel, t);
    snprintf(t, sizeof t, "%d / %d", W.page + 1, pages); U->set_text(W.t_page, t);
    U->set_enabled(W.b_prev, W.page > 0); U->set_enabled(W.b_next, W.page + 1 < pages);
    U->set_enabled(W.b_team, W.nticked >= 1); U->set_enabled(W.b_rename, W.nticked == 1);
    U->set_enabled(W.b_delete, W.nticked >= 1);
    if (W.b_edit) U->set_enabled(W.b_edit, W.nticked == 1);
    U->set_active(W.b_delteam, W.sel_team >= 0);
}

/* the window's widgets; again (same state) when the kit's screen layer changes size: on the very first window of a run
   the layer is laid out one frame later, and the game window or the UI scale can change while it is open */
static int build_ui(void) {
    float w, h;
    W.win = mu_window("My Units", 1100, 720, &w, &h);
    if (!W.win) { mu_log("my units: kit not ready"); return -1; }
    W.w = w; W.h = h; W.left = W.right = NULL; W.ncard = W.nteam_btn = 0;
    W.lw = W.lh = 0; cm_rect_size(cm_parent(W.win), &W.lw, &W.lh); W.scale = mu_ui_scale();
    /* top bar */
    float x = 20, y = 56;
    W.b_add = mu_button(W.win, x, y, 220, 40, "Add from wiki link", on_add, NULL); x += 230;
    U->tooltip(W.b_add, "Add from wiki link", "Copy a unit or team link in the wiki's Unit Builder, then press here: it is read from the clipboard.");
    W.b_new = NULL;
    if (mu_builder_present()) { W.b_new = mu_button(W.win, x, y, 150, 40, "New unit", on_new, NULL); x += 160; }
    W.b_close = mu_button(W.win, w - 130, y, 110, 40, "Close", on_close, NULL);
    W.status = mu_text(W.win, x, y, w - 140 - x, 40, "", 14, DSL_TXT_LEFT, MU_C_SUB);
    /* paste box (only when the clipboard holds no link) */
    W.box = U->panel(W.win, 20, y + 44, w - 40, 52, MU_C_BAND);
    if (W.box) {
        W.box_field = U->field(W.box, 8, 6, w - 40 - 16 - 250, 40, "Paste a wiki builder link", "", on_box_add, NULL);
        W.box_add = mu_button(W.box, w - 40 - 250, 6, 120, 40, "Add", on_box_add, NULL);
        W.box_cancel = mu_button(W.box, w - 40 - 125, 6, 117, 40, "Cancel", on_box_cancel, NULL);
        U->set_active(W.box, 0);
    }
    /* lists */
    W.ry = y + 104; W.rh = h - W.ry - 64; W.rx = 232; W.rw = w - W.rx - 20;
    /* bottom bar */
    float by = h - 54;
    W.bottom = U->panel(W.win, 20, by, w - 40, 44, 0x00000000u);
    if (W.bottom) {
        cm_raycast(W.bottom, false);
        int nb = mu_builder_present() ? 5 : 4;                      /* Edit only with the Unit Builder */
        float bx = 8, bw = (w - 40 - 200 - 140 - 8) / nb;
        W.t_sel = mu_text(W.bottom, bx, 0, 128, 44, "", 14, DSL_TXT_LEFT, MU_C_SUB); bx += 136;
        W.b_team = mu_button(W.bottom, bx, 2, bw - 6, 40, "Save as team", on_save_team, NULL); bx += bw;
        W.b_rename = mu_button(W.bottom, bx, 2, bw - 6, 40, "Rename", on_rename, NULL); bx += bw;
        W.b_edit = NULL;
        if (nb == 5) { W.b_edit = mu_button(W.bottom, bx, 2, bw - 6, 40, "Edit", on_edit, NULL); bx += bw; }
        W.b_delete = mu_button(W.bottom, bx, 2, bw - 6, 40, "Delete", on_delete, NULL); bx += bw;
        W.b_delteam = mu_button(W.bottom, bx, 2, bw - 6, 40, "Delete team", on_delteam, NULL); bx += bw;
        W.b_prev = mu_button(W.bottom, w - 40 - 200, 2, 44, 40, "<", on_page, (void *)(intptr_t)-1);
        W.t_page = mu_text(W.bottom, w - 40 - 152, 0, 104, 44, "", 14, DSL_TXT_CENTER, MU_C_SUB);
        W.b_next = mu_button(W.bottom, w - 40 - 44, 2, 44, 40, ">", on_page, (void *)(intptr_t)1);
    }
    W.rename_row = U->panel(W.win, 20, by, w - 40, 44, MU_C_BAND);
    if (W.rename_row) {
        mu_text(W.rename_row, 8, 0, 70, 44, "Name", 14, DSL_TXT_LEFT, MU_C_SUB);
        W.rename_field = U->field(W.rename_row, 80, 2, w - 40 - 80 - 260, 40, "Unit name", "", on_rename_ok, NULL);
        W.b_rename_ok = mu_button(W.rename_row, w - 40 - 250, 2, 120, 40, "Save", on_rename_ok, NULL);
        mu_button(W.rename_row, w - 40 - 125, 2, 120, 40, "Cancel", on_rename_cancel, NULL);
        U->set_active(W.rename_row, 0);
    }
    show_box(W.box_on);
    if (W.msg[0]) U->set_text(W.status, W.msg);
    return 0;
}
void mu_window_open(void) {
    U = mu_kit(); if (!U) return;
    if (W.open) close_win();
    W.page = 0; W.nticked = 0; W.sel_team = -1; W.msg[0] = 0; W.box_on = 0;
    { char p[1200] = ""; cm_m m = cm_il_method("UnityEngine", "Application", "get_persistentDataPath", 0, NULL);
      if (m.fn) { cm_il_utf8(CM_CALL(m, Obj, const void *)(m.mi), p, sizeof p); if (p[0]) mu_set_data_dir(p); } }
    load_lib();
    if (build_ui()) return;
    W.open = 1;
    mu_tick_add(poll_fn);
    if (!W.lib) set_msg(W.err, MU_C_BAD);
    refresh();
    mu_log("my units: window open (%d units, %zu teams)", W.lib ? (int)mu_count(W.lib) : -1, W.lib ? mu_team_count(W.lib) : (size_t)0);
}
int mu_window_is_open(void) { return W.open; }
void mu_window_close(void) { close_win(); }

/* ------------------------------------------------------------------ tests (ext/units13/driver "mu:<name>") */
DSLObj mu_window_test_find(const char *n) {
    if (!W.open) return NULL;
    static const struct { const char *n; DSLObj *o; } map[] = {
        { "add", &W.b_add }, { "new", &W.b_new }, { "close", &W.b_close }, { "box", &W.box }, { "box_field", &W.box_field },
        { "save_team", &W.b_team }, { "rename", &W.b_rename }, { "edit", &W.b_edit }, { "delete", &W.b_delete },
        { "delete_team", &W.b_delteam }, { "prev", &W.b_prev }, { "next", &W.b_next }, { "rename_field", &W.rename_field }, { "rename_ok", &W.b_rename_ok },
        { "status", &W.status }, { "box_add", &W.box_add }, { "box_cancel", &W.box_cancel }, { "win", &W.win }, { "all", &W.all_btn }, { "right", &W.right } };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(n, map[i].n)) return *map[i].o;
    int k;
    if (sscanf(n, "tick%d", &k) == 1 && k >= 0 && k < W.ncard) return W.tick[k];
    if (sscanf(n, "card%d", &k) == 1 && k >= 0 && k < W.ncard) return W.card[k];
    if (sscanf(n, "team%d", &k) == 1 && k >= 0 && k < W.nteam_btn) return W.team_btn[k];
    return NULL;
}
static void jesc(char *o, size_t cap, const char *s) { size_t n = 0; for (; *s && n + 3 < cap; s++) { if (*s == '"' || *s == '\\') o[n++] = '\\'; if ((unsigned char)*s < 0x20) continue; o[n++] = *s; } o[n] = 0; }
size_t mu_window_test_state(char *out, size_t cap) {
    char m[600]; jesc(m, sizeof m, W.msg);
    return (size_t)snprintf(out, cap, "{\"open\":%d,\"units\":%d,\"teams\":%d,\"sel_team\":%d,\"shown\":%d,\"cards\":%d,\"ticked\":%d,"
                            "\"page\":%d,\"per_page\":%d,\"box\":%d,\"link\":%d,\"msg\":\"%s\",\"builder\":%d,\"w\":%.0f,\"h\":%.0f}",
                            W.open, W.lib ? (int)mu_count(W.lib) : -1, W.lib ? (int)mu_team_count(W.lib) : -1, W.sel_team, W.nids,
                            W.ncard, W.nticked, W.page, W.per_page, W.box_on, mul_poll(NULL), m, mu_builder_present(), W.w, W.h);
}
