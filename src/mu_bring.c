// mu_bring.c - the Bring Units card window (mu_bring.h). From ds_custommodes units/mu_bring.c (removed there at 5454e03),
// saves only (owner 2026-10-20: units in a multiplayer mode come only from the player's own saves). Owner 2026-10-15: "when I
// save some units and I can select bring to pvp for expedition I have no way to actually select them"; the old list was
// "basic overflow of text and you don't have any icons for the units". Left: the saves; right: Compact Plus cards
// (mu_card_draw, the game's own icons), a tick per card, a per-player cap. Read only: a save is only read (mu_save.c).
#include "mu_bring.h"
#include "mu_ui.h"
#include "mu_save.h"
#include "cm_il.h"
#include "ds_i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>

static const DSLUi *U;
#define NMAX 400          /* units listed on the right (one save) */
#define SMAX 64           /* saves in the left column */
#define PMAX 64           /* picks */

static struct {
    int open;
    mu_bring_req req; char title[128], sub[300];
    DSLObj win, t_sub, hdr, status, t_picked, left, right, b_prev, b_next, t_page, b_cancel, b_bring;
    DSLObj left_btn[SMAX]; int nleft;
    DSLObj tick[16], card[16]; int card_item[16], ncard;
    float w, h, rx, ry, rw, rh;
    mu_save_ref saves[SMAX]; int nsaves, sel_save;
    mu_save_unit su[NMAX]; int nsu; char su_err[400]; int su_for;   /* units of saves[su_for] (-1 none loaded) */
    mu_pick picks[PMAX]; int npicks, max;                            /* picks (survive save and page changes) */
    int page, per_page, pages;
    char msg[400]; uint32_t msg_col;
    int esc_prev;
    float lw, lh;                              /* the screen layer size the window was built for */
} B = { .su_for = -1 };

/* the done call, between frames, after the window closed */
static struct { int pending; void (*fn)(const mu_pick *, int, void *); void *ud; mu_pick p[PMAX]; int n; } D;

static void refresh(void);
static void bar_update(void);
static int build_ui(void);
static void tick_fn(void);

int mu_bring_available(void) { return mu_kit() != NULL; }

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
    if (!o) snprintf(out, cap, "%s", TR("Save"));
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
        if (!stat(files[i], &st)) { time_t t = st.st_mtime; struct tm *tm = localtime(&t); if (tm) strftime(s->when, sizeof s->when, "%Y-%m-%d %H:%M", tm); }
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
        if (!B.su[i].name[0]) snprintf(B.su[i].name, sizeof B.su[i].name, "%s", TR("Unit"));
    }
}

/* ------------------------------------------------------------------ picks */
static int pick_find(const char *path, const char *guid) {
    for (int i = 0; i < B.npicks; i++) if (!strcmp(B.picks[i].save_file, path) && !strcmp(B.picks[i].unit_guid, guid)) return i;
    return -1;
}
static void pick_remove(int i) { if (i < 0 || i >= B.npicks) return; memmove(&B.picks[i], &B.picks[i + 1], (size_t)(B.npicks - i - 1) * sizeof *B.picks); B.npicks--; }
static void refuse_msg(void) {
    char m[200]; snprintf(m, sizeof m, TRN("You can bring %d unit at most. Untick one first.", "You can bring %d units at most. Untick one first.", B.max), B.max);
    set_msg(m, MU_C_BAD);
}
/* item k of the right list: is it picked? */
static int item_pick(int k) { return k < B.nsu && B.sel_save >= 0 ? pick_find(B.saves[B.sel_save].path, B.su[k].guid) : -1; }

/* ------------------------------------------------------------------ closing and done */
static void close_win(void) {
    if (!B.open) return;
    B.open = 0;
    if (U && B.win) U->destroy(B.win);
    B.win = B.left = B.right = NULL; B.ncard = B.nleft = 0;
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
static void on_left(void *ud, DSLObj w) {
    (void)w; B.page = 0; B.sel_save = (int)(intptr_t)ud; set_msg("", MU_C_SUB); refresh();
}
static void on_tick(void *ud, DSLObj w) {
    int c = (int)(intptr_t)ud; if (c < 0 || c >= B.ncard) return;
    int k = B.card_item[c], on = U->toggle_get(w), pi = item_pick(k);
    if (!on) { pick_remove(pi); set_msg("", MU_C_SUB); bar_update(); return; }
    if (pi >= 0) return;
    if (B.npicks >= B.max) { U->toggle_set(w, 0); refuse_msg(); return; }
    if (B.sel_save < 0 || k >= B.nsu) { U->toggle_set(w, 0); return; }
    mu_pick *p = &B.picks[B.npicks]; memset(p, 0, sizeof *p);
    snprintf(p->save_file, sizeof p->save_file, "%s", B.saves[B.sel_save].path);
    snprintf(p->unit_guid, sizeof p->unit_guid, "%s", B.su[k].guid);
    snprintf(p->name, sizeof p->name, "%s", B.su[k].name);
    p->level = B.su[k].level;
    B.npicks++;
    set_msg("", MU_C_SUB);
    bar_update();
}
static void on_page(void *ud, DSLObj w) {
    (void)w; B.page += (int)(intptr_t)ud;
    if (B.page >= B.pages) B.page = B.pages - 1;
    if (B.page < 0) B.page = 0;
    refresh();
}

/* ------------------------------------------------------------------ drawing */
static void left_row(float y, const char *label, int sel, int ud, const char *tip_t, const char *tip_b) {
    char t[300]; snprintf(t, sizeof t, "%s%s%s", sel ? "<color=#D9BE8F>" : "", label, sel ? "</color>" : "");
    DSLObj b = mu_button(B.left, 0, y, 192, 40, t, on_left, (void *)(intptr_t)ud);
    if (b) { cm_text_ellipsis(b); if (tip_t) U->tooltip(b, tip_t, tip_b); }
    if (B.nleft < SMAX) B.left_btn[B.nleft++] = b;
}
static void draw_left(void) {
    if (B.left) U->destroy(B.left);
    B.left = U->scroll(B.win, 20, B.ry, 200, B.rh);
    B.nleft = 0;
    if (!B.left) return;
    float y = 0;
    for (int i = 0; i < B.nsaves; i++, y += 44) {
        char body[160] = ""; if (B.saves[i].when[0]) snprintf(body, sizeof body, TR("Saved %s"), B.saves[i].when);
        left_row(y, B.saves[i].name, i == B.sel_save, i, B.saves[i].name, body[0] ? body : " ");
    }
    if (!B.nsaves) { mu_text(B.left, 4, 0, 184, 80, TR("No saves found."), 15, DSL_TXT_WRAP, MU_C_SUB); y = 80; }
    U->scroll_height(B.left, y);
}
static void draw_cards(void) {
    if (B.right) U->destroy(B.right);
    B.right = U->panel(B.win, B.rx, B.ry, B.rw, B.rh, MU_C_BAND);
    B.ncard = 0;
    mu_drawn_reset();
    if (!B.right) return;
    cm_raycast(B.right, false);
    load_save_units();
    int n = B.nsu; const char *empty = NULL; uint32_t ecol = MU_C_SUB;
    if (!B.nsaves) empty = TR("No saves to bring units from.");
    else if (B.su_err[0]) { empty = B.su_err; ecol = MU_C_BAD; }
    else if (!n) empty = TR("This save has no units.");
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
        if (!mu_save_build_ex(B.saves[B.sel_save].path, B.su[k].guid, &b, &x, e, sizeof e) && !mu_card_of(&b, &c)) {
            const char *bar[8]; for (int i = 0; i < x.nbar && i < 8; i++) bar[i] = x.bar[i];
            card = mu_card_draw(B.right, 52, y, B.rw - 60, &c, B.su[k].name, B.su[k].level, B.su[k].portrait, bar, x.nbar);
        }
        if (!card) {
            card = U->panel(B.right, 52, y, B.rw - 60, MU_CARD_H, MU_C_ROW);
            if (card) mu_text(card, 12, 10, B.rw - 84, MU_CARD_H - 20, e[0] ? e : TR("This unit cannot be shown."), 14, DSL_TXT_WRAP, MU_C_BAD);
        }
        B.card[B.ncard++] = card;
    }
}
static void bar_update(void) {
    if (!B.open) return;
    char t[120]; snprintf(t, sizeof t, TR("Picked %d / %d"), B.npicks, B.max); U->set_text(B.t_picked, t);
    snprintf(t, sizeof t, "%d / %d", B.page + 1, B.pages); U->set_text(B.t_page, t);
    U->set_enabled(B.b_prev, B.page > 0); U->set_enabled(B.b_next, B.page + 1 < B.pages);
    U->set_enabled(B.b_bring, B.npicks > 0);
}
static void refresh(void) {
    if (!B.open) return;
    draw_left();
    draw_cards();
    bar_update();
}

/* ------------------------------------------------------------------ open */
int mu_bring_open(const mu_bring_req *req) {
    U = mu_kit();
    if (!U || B.open || !req || req->max_units < 1) return -1;
    if (D.pending) { mu_tick_remove(tick_fn); fire_done(); }         /* the last window's answer goes first */
    B.req = *req;
    snprintf(B.title, sizeof B.title, "%s", req->title && *req->title ? req->title : TR("Bring Units")); dsi_utf8_fix_end(B.title);
    snprintf(B.sub, sizeof B.sub, "%s", req->subtitle ? req->subtitle : ""); dsi_utf8_fix_end(B.sub);
    B.req.title = B.title; B.req.subtitle = B.sub;
    B.max = req->max_units > PMAX ? PMAX : req->max_units;
    B.npicks = 0;
    for (int i = 0; req->ticked && i < req->nticked && B.npicks < B.max; i++) B.picks[B.npicks++] = req->ticked[i];
    B.req.ticked = NULL; B.req.nticked = 0;
    B.page = 0; B.pages = 1; B.sel_save = 0; B.su_for = -1; B.msg[0] = 0; B.msg_col = MU_C_SUB;
    if (build_ui()) { mu_log("bring units: kit not ready"); return -1; }
    load_saves();
    B.esc_prev = 1;                                                   /* a key already down does not count */
    mu_tick_add(tick_fn);
    refresh();
    mu_log("bring units: open (max %d, %d saves, %d ticked; %.0fx%.0f on a %.0fx%.0f layer)", B.max, B.nsaves, B.npicks, B.w, B.h, B.lw, B.lh);
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
    B.t_sub = mu_text(B.win, 20, 46, w - 40, 24, B.sub, 15, DSL_TXT_CENTER, MU_C_SUB);
    float x = 20, y = 76;                                             /* top row: the source's name, then the message */
    B.hdr = mu_text(B.win, x, y, 200, 40, TR("From a save"), 19, DSL_TXT_LEFT, MU_C_GOLD); x += 210;
    B.status = mu_text(B.win, x, y, w - 20 - x, 40, "", 14, DSL_TXT_LEFT, MU_C_SUB);
    B.ry = y + 50; B.rh = h - B.ry - 64; B.rx = 232; B.rw = w - B.rx - 20;
    float by = h - 54;                                                /* bottom: Picked n / max, pages, Cancel, Bring */
    DSLObj bot = U->panel(B.win, 20, by, w - 40, 44, 0x00000000u);
    if (bot) {
        cm_raycast(bot, false);
        float bw = w - 40;
        B.t_picked = mu_text(bot, 0, 0, 200, 44, "", 16, DSL_TXT_LEFT, MU_C_GOLD);
        float px = 212 + (bw - 212 - 290 - 200) / 2; if (px < 212) px = 212;
        B.b_prev = mu_button(bot, px, 2, 44, 40, "<", on_page, (void *)(intptr_t)-1);
        B.t_page = mu_text(bot, px + 48, 0, 104, 44, "", 14, DSL_TXT_CENTER, MU_C_SUB);
        B.b_next = mu_button(bot, px + 156, 2, 44, 40, ">", on_page, (void *)(intptr_t)1);
        B.b_cancel = mu_button(bot, bw - 290, 2, 140, 40, TR("Cancel"), on_cancel, NULL);
        B.b_bring = mu_button(bot, bw - 140, 2, 140, 40, TR("Bring"), on_bring, NULL);
    }
    if (B.msg[0]) set_msg(B.msg, B.msg_col);
    return 0;
}
void mu_bring_close(void) { finish(0); }
int  mu_bring_is_open(void) { return B.open; }

/* ------------------------------------------------------------------ tests (driver "bring:<name>", rig bring_state) */
void *mu_bring_test_find(const char *n) {
    if (!B.open || !n) return NULL;
    static const struct { const char *n; DSLObj *o; } map[] = {
        { "bring", &B.b_bring }, { "cancel", &B.b_cancel }, { "next", &B.b_next }, { "prev", &B.b_prev }, { "win", &B.win },
        { "right", &B.right }, { "status", &B.status }, { "picked", &B.t_picked } };
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
    const mu_drawn *d; int nd = mu_drawn_list(&d), icons = 0, sprites = mu_drawn_sprites();
    for (int i = 0; i < nd; i++) if (!d[i].is_text && d[i].card >= 0) icons++;
    char files[1600] = ""; size_t fo = 0;                              /* the listed save files (names only) */
    for (int i = 0; i < B.nsaves && fo + 140 < sizeof files; i++) {
        const char *f = strrchr(B.saves[i].path, '/'); f = f ? f + 1 : B.saves[i].path;
        { const char *b = strrchr(f, '\\'); if (b) f = b + 1; }
        char e[260]; jesc(e, sizeof e, f); fo += (size_t)snprintf(files + fo, sizeof files - fo, "%s\"%.120s\"", i ? "," : "", e);
    }
    return (size_t)snprintf(out, cap, "{\"save_files\":[%s],\"open\":%d,\"kit\":%d,\"picked\":%d,\"max\":%d,\"left_rows\":%d,\"cards\":%d,\"card_icons\":%d,\"card_sprites\":%d,"
                            "\"page\":%d,\"pages\":%d,\"msg\":\"%s\",\"saves\":%d,\"sel_save\":%d,\"units\":%d,\"w\":%.0f,\"h\":%.0f}",
                            files, B.open, mu_kit() != NULL, B.npicks, B.max, B.open ? B.nleft : 0, B.open ? B.ncard : 0, B.open ? icons : 0, B.open ? sprites : 0,
                            B.page, B.pages, m, B.nsaves, B.sel_save, B.nsu, B.w, B.h);
}
