// mu_ui.c - shared pieces of the My Units and Bring Units windows (mu_ui.h).
#include "mu_ui.h"
#include "cm_il.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const DSLHost *H; static DSLPlugin *ME; static const DSLUi *U;
const DSLUi *mu_kit(void) { return U; }
void mu_log(const char *fmt, ...) {
    if (!H) return;
    char b[1200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    H->log(ME, "%s", b);
}
void mu_ui_init(const DSLHost *host, DSLPlugin *self) { H = host; ME = self; U = dsl_ui(host); cm_il_init(host, self); }

/* ------------------------------------------------------------------ test record */
#define DMAX 2048
static mu_drawn dr[DMAX]; static int ndr; static DSLObj cards[256]; static int ncards, in_card = -1;
/* in_card: the card being drawn (its index), -1 = a window widget (checked for its own box only) */
static void rec(DSLObj o, int is_text) { if (o && ndr < DMAX) { dr[ndr].o = o; dr[ndr].card = in_card; dr[ndr].is_text = is_text; ndr++; } }
int mu_drawn_list(const mu_drawn **out) { *out = dr; return ndr; }
DSLObj mu_drawn_card(int i) { return i >= 0 && i < ncards ? cards[i] : NULL; }
void mu_drawn_reset(void) { ndr = 0; ncards = 0; in_card = -1; }
DSLObj mu_button(DSLObj parent, float x, float y, float w, float h, const char *label, dsl_ui_cb cb, void *ud) {
    if (!U) return NULL;
    DSLObj b = U->button(parent, x, y, w, h, label, cb, ud);
    if (b) { cm_text_ellipsis(b); rec(b, 1); }
    return b;
}

/* ------------------------------------------------------------------ texts, icons, names */
DSLObj mu_text(DSLObj parent, float x, float y, float w, float h, const char *s, float size, int flags, uint32_t rgba) {
    if (!U) return NULL;
    DSLObj t = U->text(parent, x, y, w, h, s ? s : "", size, flags, rgba);
    if (t && !(flags & DSL_TXT_WRAP)) cm_text_ellipsis(t);
    if (t) cm_raycast(t, false);
    rec(t, 1);
    return t;
}
void mu_name_of(const char *key, char *out, size_t cap) {
    if (!cap) return; out[0] = 0;
    if (!key || !*key) return;
    char k[160]; snprintf(k, sizeof k, "TEXTKEY_%s_NAME", key);
    if (!cm_gettext(k, out, cap)) return;
    const char *p = key; if (!strncmp(p, "AFFECTER_", 9)) p += 9;
    snprintf(out, cap, "%s", p);
    for (char *c = out; *c; c++) if (*c == '_') *c = ' ';
}
DSLObj mu_icon(DSLObj parent, float x, float y, float s, int kind, const char *key, int dim, const char *tip) {
    if (!U) return NULL;
    Obj sp = cm_icon(kind, key);
    DSLObj p = U->panel(parent, x, y, s, s, sp ? 0xFFFFFFFFu : 0x1E1C21FFu);
    if (!p) return NULL;
    if (sp) cm_set_sprite(p, sp, dim ? 0xFFFFFF59u : 0xFFFFFFFFu);
    cm_raycast(p, false);                                              /* clicks go to what is under the icon */
    if (tip && *tip) U->tooltip(p, tip, sp ? " " : key);
    rec(p, 0);
    return p;
}

/* ------------------------------------------------------------------ the window */
/* the game's UI scale (Settings > UI scale, UIScaleManager.Scale: 0.75 .. 1.4); 1 when unknown */
float mu_ui_scale(void) {
    static cm_m inst, get; if (!inst.fn) { inst = cm_il_method("Refactor", "UIScaleManager", "get_Instance", 0, NULL); get = cm_il_method("Refactor", "UIScaleManager", "get_Scale", 0, NULL); }
    Obj m = inst.fn ? CM_CALL(inst, Obj, const void *)(inst.mi) : NULL;
    float s = cm_alive(m) && get.fn ? CM_CALL(get, float, Obj, const void *)(m, get.mi) : 1.f;
    return s >= 0.5f && s <= 2.f ? s : 1.f;
}
typedef struct { float x, y, z; } V3s;
DSLObj mu_window(const char *title, float want_w, float want_h, float *w, float *h) {
    if (!U) return NULL;
    DSLObj p = U->panel(NULL, 0, 0, want_w, want_h, 0);
    if (!p) return NULL;
    float lw = 0, lh = 0;
    if (cm_rect_size(cm_parent(p), &lw, &lh) || lw < 100 || lh < 100) { lw = 1920; lh = 1080; }
    /* the window follows the game's UI scale like the game's own windows: drawn at its size x scale, so the room it may
       take (in its own units) is the layer / scale */
    float sc = mu_ui_scale(), aw = lw / sc - 40, ah = lh / sc - 40;
    float ww = want_w > aw ? aw : want_w, hh = want_h > ah ? ah : want_h;
    U->place(p, (lw - ww * sc) / 2, (lh - hh * sc) / 2, ww, hh);
    { static cm_m ls; if (!ls.fn) ls = cm_il_method("UnityEngine", "Transform", "set_localScale", 1, NULL);
      if (ls.fn) CM_CALL(ls, void, Obj, V3s, const void *)(p, (V3s){ sc, sc, 1 }, ls.mi); }
    U->text(p, 20, 10, ww - 40, 40, title, 26, DSL_TXT_CENTER | DSL_TXT_TITLE, 0);
    if (w) *w = ww;
    if (h) *h = hh;
    return p;
}

/* ------------------------------------------------------------------ Compact Plus card */
typedef struct { int kind; const char *key; int dim; } IconRef;
/* one row of icon groups; groups are separated by a gap; the row ends with "+N" when the rest does not fit */
static void icon_row(DSLObj card, float x, float y, float right, float s, const IconRef *ic, const int *group, int n) {
    float step = s + 3, cx = x; int last = -1;
    for (int i = 0; i < n; i++) {
        if (last >= 0 && group[i] != last) cx += 8;
        last = group[i];
        int more = n - i - 1;
        if (cx + s > right - (more ? 30 : 0)) {
            char t[16]; snprintf(t, sizeof t, "+%d", n - i);
            mu_text(card, cx, y, right - cx > 30 ? right - cx : 30, s, t, 13, DSL_TXT_LEFT, MU_C_SUB);
            return;
        }
        char nm[160]; mu_name_of(ic[i].key, nm, sizeof nm);
        mu_icon(card, cx, y, s, ic[i].kind, ic[i].key, ic[i].dim, nm);
        cx += step;
    }
}
DSLObj mu_card_draw(DSLObj parent, float x, float y, float w, const mu_card *c, const char *name, int level,
                    const char *portrait_key, const char *const *bar, int nbar) {
    if (!U || !c) return NULL;
    DSLObj card = U->panel(parent, x, y, w, MU_CARD_H, MU_C_ROW);
    if (!card) return NULL;
    cm_raycast(card, false);
    if (ncards < 256) { cards[ncards] = card; in_card = ncards++; }
    /* avatar + level */
    const char *face = portrait_key && *portrait_key ? portrait_key : c->avatar;
    char rn[96]; mu_name_of(c->race, rn, sizeof rn);
    if (face && cm_icon(CM_ICON_PORTRAIT, face)) mu_icon(card, 8, 8, 72, CM_ICON_PORTRAIT, face, 0, NULL);
    else mu_icon(card, 8, 8, 72, CM_ICON_AFFECTER, c->race, 0, rn);
    char lv[32]; snprintf(lv, sizeof lv, "Lv %d", level > 0 ? level : c->lvl);
    mu_text(card, 4, 82, 80, 24, lv, 14, DSL_TXT_CENTER, MU_C_GOLD);
    /* name, race / background / weapon */
    float x0 = 90, tw = w - x0 - 10;
    char nm[128];
    if (name && *name) snprintf(nm, sizeof nm, "%s", name);
    else snprintf(nm, sizeof nm, "%s", c->name[0] ? c->name : "Unit");
    mu_text(card, x0, 2, tw, 28, nm, 17, DSL_TXT_LEFT, MU_C_MAIN);
    char line[300], bg[96] = ""; if (c->bg) mu_name_of(c->bg, bg, sizeof bg);
    snprintf(line, sizeof line, "%s%s%s%s%s", rn, bg[0] ? "  \xC2\xB7  " : "", bg, c->weapon ? "  \xC2\xB7  " : "", c->weapon ? c->weapon : "");
    mu_text(card, x0, 28, tw, 22, line, 13, DSL_TXT_LEFT, MU_C_SUB);
    /* row A: worn items | skill bar */
    IconRef ic[MUC_MAX * 3]; int gr[MUC_MAX * 3], n = 0;
    for (int i = 0; i < c->neq && n < MUC_MAX; i++) { ic[n] = (IconRef){ CM_ICON_ITEM, c->eq[i].item, 0 }; gr[n++] = 0; }
    int nb = bar ? nbar : c->nbar;
    for (int i = 0; i < nb && n < MUC_MAX * 2; i++) { const char *k = bar ? bar[i] : c->bar[i]; if (k && *k) { ic[n] = (IconRef){ CM_ICON_SKILL, k, 0 }; gr[n++] = 1; } }
    icon_row(card, x0, 52, w - 8, 24, ic, gr, n);
    /* row B: branches ACTIVE | PASSIVE (dim = nothing learned in it) | banked milestones */
    n = 0;
    for (int i = 0; i < c->nactive; i++) { ic[n] = (IconRef){ CM_ICON_TREE, c->active[i].tree, !c->active[i].invested }; gr[n++] = 0; }
    for (int i = 0; i < c->npassive; i++) { ic[n] = (IconRef){ CM_ICON_TREE, c->passive[i].tree, !c->passive[i].invested }; gr[n++] = 1; }
    for (int i = 0; i < c->nstacks; i++) { ic[n] = (IconRef){ CM_ICON_SKILL, c->stacks[i].skill, !c->stacks[i].learned }; gr[n++] = 2; }
    icon_row(card, x0, 80, w - 8, 24, ic, gr, n);
    /* row C: passive skills | inscriptions | soul stones | traits acquired in play */
    n = 0;
    for (int i = 0; i < c->npass && n < MUC_MAX; i++) { ic[n] = (IconRef){ CM_ICON_SKILL, c->pass[i].skill, 0 }; gr[n++] = 0; }
    for (int i = 0; i < c->ninsc && n < MUC_MAX * 2; i++) { ic[n] = (IconRef){ CM_ICON_AFFECTER, c->insc[i], 0 }; gr[n++] = 1; }
    for (int i = 0; i < c->nsouls && n < MUC_MAX * 2 + WK_MAX_LIST; i++) { ic[n] = (IconRef){ CM_ICON_AFFECTER, c->souls[i], 0 }; gr[n++] = 2; }
    for (int i = 0; i < c->nacq && n < MUC_MAX * 3; i++) { ic[n] = (IconRef){ CM_ICON_AFFECTER, c->acq[i], 0 }; gr[n++] = 3; }
    icon_row(card, x0, 108, w - 8, 20, ic, gr, n);
    in_card = -1;
    return card;
}

/* ------------------------------------------------------------------ frame tick */
#define TKMAX 8
static void (*tks[TKMAX])(void); static int ntk, timer_on;
static void tick_all(void) { for (int i = 0; i < ntk; i++) if (tks[i]) tks[i](); }
int mu_tick_add(void (*fn)(void)) {
    for (int i = 0; i < ntk; i++) if (tks[i] == fn) return 0;
    if (ntk >= TKMAX) return -1;
    tks[ntk++] = fn;
    if (!timer_on) timer_on = mu_os_timer(tick_all, 50) == 0;
    return 0;
}
void mu_tick_remove(void (*fn)(void)) {
    for (int i = 0; i < ntk; i++) if (tks[i] == fn) { tks[i] = tks[--ntk]; return; }
}
