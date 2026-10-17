// ui_tip.c - our explanatory tooltips in the game's own tooltip window (owner lan_v9). Dylib only, build 25846389 arm64.
// RVAs: the MAC dump (~/ds_skill_truth/dump_25846389/dump.cs), never win/25846389/dump (Windows addresses).
// The game's hover help (HUD chips, panel info icons, building options) is TooltipUI (Refactor.UI, a StaticUI) showing a
// pooled Tooltip_ETC: _titleText +0x180, _subtitleText +0x188, _subtitleLayout +0x190, _desText +0x198, _flavorText
// +0x1A0, laid out and placed by Tooltip_Base (language width, DelayedAlignment, screen clamp). Its Show(key) reads
// TEXTKEY_<key>_NAME / _DESC from the localisation table, so for our texts:
//   - we ask TooltipUI.ShowTooltipAtScreenPosition 0xF4A648 for type ETC (0) with the marker key MP_KEY, at the mouse or at
//     an element's corner (computed here: the callerRect overload 0xF4A244 places in the tooltip's own canvas);
//   - one hook on Tooltip_ETC.Show 0xF3D8B8: for MP_KEY it runs the stock Show with a real key (BASE_KEY, so every stock
//     layout step runs), then puts our title and body in the two texts and turns the subtitle and flavour lines off.
// Same frame, same fonts and colours as the game's own tooltips.
// Placement (owner v12 item 3): a card for an element inside a panel goes OUTSIDE that panel, at its right edge, top
// aligned with the element; no room on the right = outside the panel's left edge. Never over the panel (it covered the
// next button). The panel: panel_of (the widest box drawn around the element, narrower than 70% of the screen). A hook on
// Tooltip_Base.SetTooltipAlignment 0xF387DC moves our card right after the stock alignment (no flash); unstick() keeps it
// there. Mouse-placed cards (game_tip_show) keep the stock placement. TooltipContext.HideHoldLayout (+0xA0) is set: no
// "hold key" line (our texts have no links).
#include "ui_tip.h"
#include "ui_model.h"
#include "rig.h"
#include "ds_i18n.h"
#include "ui_utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <math.h>

void *ord_debug_class(const char *ns, const char *name) __attribute__((weak_import));

typedef struct { float x, y; } V2;
typedef struct { uint8_t b[0xC0]; } TipCtx;                  /* Refactor.UI.TooltipContext (by value: a pointer to a copy) */
static int shown, placed_by_us; static double routine_since;   /* (defined below) */
#define MP_KEY   "@mp_tip"
#define BASE_KEY "Shared_Gold"                               /* any stock ETC key: "Gold" / "The basic currency..." */
static const Guid TIP_ID = { 0x6d70746970756931ull, 0x0000000000000001ull };   /* our one tooltip session */

static inline Obj u_str(const char *s) { return (Obj)ui_str_new(F(0x6F9738, void *(*)(const char *)), s); }   /* broken UTF-8: a repaired copy (ui_utf8.h) */
static inline uintptr_t u_gc_new(Obj o) { return o ? F(0x6F9540, uintptr_t (*)(Obj, bool))(o, false) : 0; }
static inline Obj u_gc_get(uintptr_t h) { return h ? F(0x6F9598, Obj (*)(uintptr_t))(h) : NULL; }
static inline void u_gc_free(uintptr_t h) { if (h) F(0x6F95E4, void (*)(uintptr_t))(h); }
static inline bool u_alive(Obj o) { return o && F(0x50FAF88, bool (*)(Obj, void *))(o, NULL); }
static inline Obj u_go(Obj comp) { return u_alive(comp) ? F(0x50ED970, Obj (*)(Obj, void *))(comp, NULL) : NULL; }
static inline void u_go_active(Obj go, bool v) { if (u_alive(go)) F(0x50F3520, void (*)(Obj, bool, void *))(go, v, NULL); }
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static int is_class(Obj o, const char *ns, const char *name) {       /* il2cpp_class_is_assignable_from */
    void *k = ord_debug_class ? ord_debug_class(ns, name) : NULL, *oc = u_alive(o) ? F(0x6F96C0, void *(*)(Obj))(o) : NULL;
    return k && oc && F(0x6F8BE0, bool (*)(void *, void *))(k, oc);
}
static void tmp_set(Obj t, const char *s) {                  /* TMP_Text.text setter: vtable slot 66 */
    if (!is_class(t, "TMPro", "TMP_Text")) return;
    void *k = *(void **)t;
    (*(void (**)(Obj, Obj, void *))((char *)k + 0x558))(t, u_str(s), *(void **)((char *)k + 0x560));
}
static int str_is(Obj s, const char *lit) {                  /* il2cpp String == ASCII literal */
    if (!s) return 0;
    int32_t n = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < n; i++) if (!lit[i] || c[i] != (uint8_t)lit[i]) return 0;
    return lit[n] == 0;
}
static Obj tooltip_view(void) {                              /* the scene's TooltipUI (Object.FindObjectOfType(Type, inactive)) */
    static uintptr_t h;
    Obj v = u_gc_get(h);
    if (u_alive(v)) return v;
    void *k = ord_debug_class ? ord_debug_class("Refactor.UI", "TooltipUI") : NULL;
    if (!k) return NULL;
    Obj t = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k));
    v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(t, true, NULL);
    u_gc_free(h); h = u_gc_new(u_alive(v) ? v : NULL);
    return u_alive(v) ? v : NULL;
}

/* the player's "Show tooltips on hover": ui_game's getter when exported, else mp_ui.json (re-read once a second) */
static int tips_on(void) {
    static int (*getter)(void); static unsigned tries;             /* uxgame (ui_game.c): uiset.tooltips, if exported */
    if (!getter && tries++ % 120 == 0) getter = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_tooltips_enabled");
    if (getter) return getter();
    static int on = 1; static double at;
    double t = now_s(); if (t - at < 1) return on; at = t;
    Obj d = F(0x507B74C, Obj (*)(void *))(NULL);              /* Application.persistentDataPath (rig: $DSRIG_DIR/data) */
    char p[1024] = "", path[1100];
    if (d) { int32_t n = AT(d, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)d + 0x14); int i = 0;
             for (; i < n && i < (int)sizeof p - 1; i++) p[i] = c[i] < 0x80 ? (char)c[i] : '?'; p[i] = 0; }
    snprintf(path, sizeof path, "%s/mp_ui.json", p);
    ui_settings s; ui_settings_default(&s);
    FILE *f = fopen(path, "r");
    if (f) { char b[512]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0; ui_settings_parse(b, &s); }
    return on = s.tooltips;
}

/* ---------------------------------------------------------------- the hook */
static char cur_title[160], cur_body[700];
static void (*o_etc_show)(Obj, Obj, void *);
static uintptr_t h_last_tip;                                 /* last Tooltip_ETC we filled (probe) */
static void hk_etc_show(Obj self, Obj key, void *mi) {
    if (!str_is(key, MP_KEY)) { o_etc_show(self, key, mi); return; }
    o_etc_show(self, u_str(BASE_KEY), mi);                   /* every stock step: widths, dividers, sections */
    tmp_set(AT(self, 0x180, Obj), cur_title);
    tmp_set(AT(self, 0x198, Obj), cur_body);
    u_go_active(AT(self, 0x190, Obj), false);                /* subtitle line */
    Obj fl = AT(self, 0x1A0, Obj);
    if (u_alive(fl)) { tmp_set(fl, ""); u_go_active(u_go(fl), false); }
    F(0xF379A8, void (*)(Obj, void *))(self, NULL);          /* Tooltip_Base.HideLastDivider (no flavour section below) */
    AT(self, 0x7C, bool) = false;                            /* _canShowHoldLayout: no ";; Pin" chip (ours hide on leave) */
    F(0xF38A00, void (*)(Obj, void *))(self, NULL);          /* Tooltip_Base.DisableHoldLayout */
    u_gc_free(h_last_tip); h_last_tip = u_gc_new(self);
}
/* build guard: the first instruction of every game function we call, read from GameAssembly 25846389 (Mac). A different
   build (or a wrong address) = no tooltips at all, never a call into the wrong code. */
static int build_ok(void) {
    static int ok = -1;
    if (ok >= 0) return ok;
    static const struct { uintptr_t rva; uint32_t ins; } want[] = { { 0xF3D8B8, 0xa9bc5ff8 }, { 0xF4A244, 0xd10483ff },
                                                                     { 0xF4A648, 0xd104c3ff }, { 0xF4AA04, 0xd10103ff },
                                                                     { 0xF387DC, 0x6dba2beb } };
    ok = 1;
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++)
        if (*(const uint32_t *)(ds_base + want[i].rva) != want[i].ins) { ok = 0; rig_log("tip: game function %#lx does not match build 25846389: tooltips off", (unsigned long)want[i].rva); }
    return ok;
}
static int scene_ready(void) { int sc = scene(); return (sc == 1 || sc == 2) && !busy(); }   /* title / campaign, not loading */
static int hooked;
static void (*o_set_align)(Obj, float, void *);
static void hk_set_align(Obj self, float y_off, void *mi);
static int ensure_hook(void) {
    if (!build_ok()) return 0;
    if (!hooked) {
        hooked = rig_hook(0xF3D8B8, (void *)hk_etc_show, (void **)&o_etc_show, "tip Tooltip_ETC.Show") == 0 ? 1 : -1;
        if (hooked > 0 && rig_hook(0xF387DC, (void *)hk_set_align, (void **)&o_set_align, "tip Tooltip_Base.SetTooltipAlignment") != 0)
            rig_log("tip: alignment hook failed: cards keep the stock place until the frame check moves them");
    }
    return hooked > 0;
}

/* ---------------------------------------------------------------- show / hide */
static int shown; static double until; static uintptr_t h_rect;
static int64_t owner_frame = -1, last_call_frame = -1; static unsigned switches;
static float tip_x, tip_y; static double shown_at; static int placed_by_us; static unsigned self_placed;   /* stuck-alignment fallback */    /* one tooltip per frame: the first caller of a frame keeps it */
static int test_pos; static float test_x, test_y;                 /* tests: a fixed screen point instead of the mouse */
static void tip_ctx(TipCtx *c) { memset(c, 0, sizeof *c); c->b[0xA0] = 1; }   /* HideHoldLayout */
typedef struct { float x, y, z; } V3;
typedef struct { float x, y, w, h; } R4;
static Obj type_of(const char *ns, const char *name) {
    void *k = ord_debug_class ? ord_debug_class(ns, name) : NULL;
    return k ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k)) : NULL;
}
static Obj canvas_near(Obj comp) {                           /* the nearest Canvas above a component (itself included) */
    static uintptr_t h; Obj t = u_gc_get(h);
    if (!t) { t = type_of("UnityEngine", "Canvas"); h = u_gc_new(t); }
    Obj c = t && u_alive(comp) ? F(0x50EE21C, Obj (*)(Obj, Obj, void *))(comp, t, NULL) : NULL;   /* GetComponentInParent(Type) */
    return u_alive(c) ? c : NULL;
}
static Obj canvas_of(Obj comp) {                             /* the root Canvas above a component */
    Obj c = canvas_near(comp);
    return c ? F(0x5476FAC, Obj (*)(Obj, void *))(c, NULL) : NULL;                                   /* rootCanvas */
}
static Obj tr_parent(Obj t) { return is_class(t, "UnityEngine", "Transform") ? F(0x510992C, Obj (*)(Obj, void *))(t, NULL) : NULL; }
/* a RectTransform's drawn rect in screen pixels (x0, y0, x1, y1; y up), on any canvas (overlay: world = pixels; camera mode:
   through the canvas camera). No rotation in our UIs: two corners are enough. */
static int scr_rect(Obj rt, float q[4]) {
    if (!is_class(rt, "UnityEngine", "RectTransform")) return 0;
    R4 r = F(0x5106978, R4 (*)(Obj, void *))(rt, NULL);                                              /* RectTransform.rect (local) */
    V3 a = F(0x510E0AC, V3 (*)(Obj, V3, void *))(rt, (V3){ r.x, r.y, 0 }, NULL);                     /* Transform.TransformPoint */
    V3 b = F(0x510E0AC, V3 (*)(Obj, V3, void *))(rt, (V3){ r.x + r.w, r.y + r.h, 0 }, NULL);
    Obj cv = canvas_of(rt);
    Obj cam = cv && F(0x5475018, int (*)(Obj, void *))(cv, NULL) != 0 ? F(0x5477610, Obj (*)(Obj, void *))(cv, NULL) : NULL;   /* overlay: no camera */
    V2 sa = F(0x5474610, V2 (*)(Obj, V3, void *))(u_alive(cam) ? cam : NULL, a, NULL);             /* RectTransformUtility.WorldToScreenPoint */
    V2 sb = F(0x5474610, V2 (*)(Obj, V3, void *))(u_alive(cam) ? cam : NULL, b, NULL);
    q[0] = sa.x < sb.x ? sa.x : sb.x; q[2] = sa.x < sb.x ? sb.x : sa.x;
    q[1] = sa.y < sb.y ? sa.y : sb.y; q[3] = sa.y < sb.y ? sb.y : sa.y;
    return 1;
}
static void obj_name(Obj o, char *out, size_t cap) {
    Obj s = u_alive(o) ? F(0x50FB198, Obj (*)(Obj, void *))(o, NULL) : NULL; size_t i = 0;
    if (s) { int32_t n = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
             for (; i < (size_t)n && i + 1 < cap; i++) out[i] = c[i] < 0x80 && c[i] >= 0x20 && c[i] != '"' && c[i] != '\\' ? (char)c[i] : '?'; }
    out[i] = 0;
}
/* the panel an element sits in (screen pixels into pq; none: the element itself). Candidates, all drawing something (a
   Graphic), active, narrower than 70% of the screen:
     - an ancestor that holds the element horizontally (title column Img_BackGroundBox, our Rules / lobby / Recent Worlds boxes);
     - a sibling of the element or of one of its ancestors that holds the whole element (the HUD Players panel and its
       settings box: their rows hang on size-0 holders, the box image is a sibling).
   The widest candidate wins. Scanning stops at the first full-screen ancestor (its children are scanned, not its parents). */
static void panel_of(Obj rect, const float eq[4], float pq[4]) {
    static uintptr_t hg; Obj gty = u_gc_get(hg);
    if (!gty) { gty = type_of("UnityEngine.UI", "Graphic"); hg = u_gc_new(gty); }
    float sw = (float)F(0x5092C94, int (*)(void *))(NULL), best = -1;
    memcpy(pq, eq, 4 * sizeof(float));
    #define GRAPHIC(t) (gty && u_alive(F(0x50EDA7C, Obj (*)(Obj, Obj, void *))((t), gty, NULL)))   /* Component.GetComponent(Type) */
    int d = 0, scanned = 0;
    for (Obj t = tr_parent(rect); t && d < 24; t = tr_parent(t), d++) {
        float q[4];
        if (!scr_rect(t, q)) break;
        int full = q[2] - q[0] >= 0.7f * sw;                        /* a full-screen layer: the panel is below it */
        int n = F(0x510EAE4, int (*)(Obj, void *))(t, NULL);       /* Transform.childCount: the siblings of the level below */
        for (int i = 0; i < n && scanned < 600; i++, scanned++) {
            Obj k = F(0x510FC60, Obj (*)(Obj, int, void *))(t, i, NULL);   /* Transform.GetChild */
            float r[4];
            if (!scr_rect(k, r) || r[2] - r[0] >= 0.7f * sw || r[2] - r[0] <= best) continue;
            if (r[0] > eq[0] + 1 || r[2] < eq[2] - 1 || r[1] > eq[1] + 1 || r[3] < eq[3] - 1) continue;   /* holds the whole element */
            Obj go = u_go(k);
            if (!u_alive(go) || !F(0x50F3724, bool (*)(Obj, void *))(go, NULL) || !GRAPHIC(k)) continue;   /* activeInHierarchy */
            memcpy(pq, r, sizeof r); best = r[2] - r[0];
        }
        if (full) break;
        if (q[2] - q[0] < 1 || q[3] - q[1] < 1 || q[2] - q[0] <= best) continue;   /* a holder without a size / narrower */
        if (q[0] > eq[0] + 1 || q[2] < eq[2] - 1) continue;         /* does not hold the element */
        if (GRAPHIC(t)) { memcpy(pq, q, sizeof q); best = q[2] - q[0]; }
    }
    #undef GRAPHIC
}
static int tgt_on;                                            /* the current card is anchored to an element's panel */
static float tgt_panel[4], tgt_elem[4], tgt_top;              /* screen px: panel, element, the card's top */
static char tgt_side[8];                                      /* "right" / "left" / "inside" (no room on either side) */
static float placed_x, placed_y;                              /* where place_card put the card's top-left (screen px) */
static int anchor_of(Obj rect, float *x, float *y) {          /* first guess for the stock call: right of the panel */
    float eq[4];
    if (!scr_rect(rect, eq)) return 0;
    memcpy(tgt_elem, eq, sizeof eq);
    panel_of(rect, eq, tgt_panel);
    float sw = (float)F(0x5092C94, int (*)(void *))(NULL);
    tgt_top = eq[3];
    *x = tgt_panel[2] + (sw * 0.005f > 2 ? sw * 0.005f : 2); *y = tgt_top;
    tgt_on = 1; snprintf(tgt_side, sizeof tgt_side, "right");
    return 1;
}
/* the card's top-left: right of the panel, else left of it, top at the element's top; kept on screen. Pivot top-left. */
static void place_card(Obj t) {
    Obj r = AT(t, 0x140, Obj);                                                        /* Tooltip_Base._rect */
    if (!tgt_on || !is_class(r, "UnityEngine", "RectTransform")) return;
    R4 rc = F(0x5106978, R4 (*)(Obj, void *))(r, NULL);                            /* rect (local) */
    V3 ls = F(0x510F4F8, V3 (*)(Obj, void *))(r, NULL);                            /* lossyScale */
    float w = rc.w * ls.x, h = rc.h * ls.y, sw = (float)F(0x5092C94, int (*)(void *))(NULL), sh = (float)F(0x5092CD4, int (*)(void *))(NULL);
    if (w < 1 || h < 1) return;                                                     /* not laid out yet */
    float gap = sw * 0.005f > 2 ? sw * 0.005f : 2, x = tgt_panel[2] + gap, y = tgt_top;
    if (x + w <= sw) snprintf(tgt_side, sizeof tgt_side, "right");
    else if (tgt_panel[0] - gap - w >= 0) { x = tgt_panel[0] - gap - w; snprintf(tgt_side, sizeof tgt_side, "left"); }
    else { snprintf(tgt_side, sizeof tgt_side, "inside");                           /* no room either side: the wider side, on screen */
           x = sw - tgt_panel[2] >= tgt_panel[0] ? sw - w : 0; if (x < 0) x = 0; }
    if (y - h < 0) y = h; if (y > sh) y = sh;
    V2 pv = F(0x5107200, V2 (*)(Obj, void *))(r, NULL);                            /* RectTransform.pivot */
    if (pv.x != 0 || pv.y != 1) F(0x51072F4, void (*)(Obj, V2, void *))(r, (V2){ 0, 1 }, NULL);
    F(0x5109B04, void (*)(Obj, V3, void *))(r, (V3){ x, y, 0 }, NULL);             /* Transform.position (overlay canvas: px) */
    placed_x = x; placed_y = y;
}
static int ours(Obj t) { return u_alive(t) && !memcmp((char *)t + 0x6C, &TIP_ID, sizeof TIP_ID); }   /* Tooltip_Base.TooltipID */
static void hk_set_align(Obj self, float y_off, void *mi) {
    o_set_align(self, y_off, mi);
    if (shown && tgt_on && ours(self)) place_card(self);
}
/* above our own windows (Teams 32000, panel 31000): once per view. The TooltipUI's OWN canvas (override sorting, stock
   10020), never the root UI_Canvas: raising the root (first version) put the whole game HUD over the Teams window. */
static void on_top(Obj view) {
    static uintptr_t done; if (done && u_gc_get(done) == view) return;
    Obj cv = canvas_near(view);
    if (!u_alive(cv)) return;
    F(0x5476208, void (*)(Obj, bool, void *))(cv, true, NULL);                                      /* overrideSorting */
    F(0x547640C, void (*)(Obj, int, void *))(cv, 32050, NULL);                                      /* sortingOrder */
    u_gc_free(done); done = u_gc_new(view);
}
static void show_core(const char *title, const char *body, Obj rect) {
    if (!title || !body || !scene_ready() || !tips_on() || !ensure_hook()) { if (shown) game_tip_hide(); return; }
    Obj v = tooltip_view();
    if (!v) return;
    int same_text = shown && !strcmp(title, cur_title) && !strcmp(body, cur_body);
    /* two callers in one frame (mpmenu, ui_game, ui_teams each hover-test their own elements) would hide and re-show the
       stock tooltip every frame; it then never finished its stock layout pass and stayed invisible (gamelane 04:45) */
    if (shown && !same_text && owner_frame == rig_frames) return;
    owner_frame = rig_frames;
    int same = same_text && u_gc_get(h_rect) == rect;
    until = now_s() + 0.2; last_call_frame = rig_frames;
    if (same && rect) return;                                /* an element: placed once */
    if (same) { static double last; if (now_s() - last < 0.05) return; last = now_s(); }   /* the mouse: follow it */
    if (shown && !same_text) { game_tip_hide(); switches++; }   /* another element: the stock session keeps its old text otherwise */
    snprintf(cur_title, sizeof cur_title, "%s", title); dsi_utf8_fix_end(cur_title);   /* a long translation: cut at a character */
    snprintf(cur_body, sizeof cur_body, "%s", body); dsi_utf8_fix_end(cur_body);
    u_gc_free(h_rect); h_rect = u_gc_new(rect);
    on_top(v);
    float x, y;
    tgt_on = 0;
    if (!rect || !anchor_of(rect, &x, &y)) {
        struct { float x, y, z; } m = F(0x51AD4DC, __typeof__(m) (*)(void *))(NULL);   /* Input.mousePosition */
        x = m.x; y = m.y;
        if (test_pos) { x = test_x; y = test_y; }
    }
    TipCtx c; tip_ctx(&c);
    F(0xF4A648, void (*)(Obj, Guid, int, Obj, V2, int, TipCtx, void *))(v, TIP_ID, 0, u_str(MP_KEY), (V2){ x, y }, 0, c, NULL);
    shown = 1; tip_x = x; tip_y = y; shown_at = now_s(); placed_by_us = 0; routine_since = 0;
}
__attribute__((visibility("default"))) int game_tip_ready(void) { return build_ok() && scene_ready() && tooltip_view() != NULL; }   /* other lanes: dlsym "game_tip_ready" */
void game_tip_show(const char *title, const char *body) { show_core(title, body, NULL); }
void game_tip_show_rect(const char *title, const char *body, void *rect) { show_core(title, body, u_alive((Obj)rect) ? (Obj)rect : NULL); }
void game_tip_hide(void) {
    if (!shown) return;
    shown = 0;                                               /* first: a throw inside the stock hide must not repeat every frame */
    if (!scene_ready()) { cur_title[0] = cur_body[0] = 0; u_gc_free(h_rect); h_rect = 0; return; }   /* loading: the scene clear hides it */
    Obj v = tooltip_view();
    if (v && build_ok()) F(0xF4AA04, void (*)(Obj, Guid, void *))(v, TIP_ID, NULL);   /* TooltipUI.HideToolTip(Guid) */
    shown = 0; cur_title[0] = cur_body[0] = 0; u_gc_free(h_rect); h_rect = 0; tgt_on = 0;
}
/* The stock Tooltip_Base.DelayedAlignment parks the window off-screen, then waits each frame for CanAlignTooltip
   (0xF38A24) before SetTooltipAlignment. Sometimes it never comes true (gamelane 2026-10-14: 7 of 13 frames, window
   left at -1931,-1640, _alignmentRoutine still set). After 0.35 s we stop that routine and place the window
   ourselves: top-left corner at the target point, kept inside the screen (the TooltipUI canvas is screen-space
   overlay: world = screen pixels). */
static double routine_since;                                  /* when the current DelayedAlignment run was first seen */
static void place(Obj t) {
    Obj r = AT(t, 0x140, Obj);                                                        /* Tooltip_Base._rect */
    if (!is_class(r, "UnityEngine", "RectTransform")) return;
    F(0xF38364, void (*)(Obj, void *))(t, NULL);                                    /* Tooltip_Base.StopAlignmentRoutine */
    if (tgt_on) { place_card(t); placed_by_us = 1; self_placed++; return; }
    F(0x51072F4, void (*)(Obj, V2, void *))(r, (V2){ 0, 1 }, NULL);                /* RectTransform.pivot: top-left */
    R4 rc = F(0x5106978, R4 (*)(Obj, void *))(r, NULL);                            /* rect (local) */
    V3 ls = F(0x510F4F8, V3 (*)(Obj, void *))(r, NULL);                            /* lossyScale */
    float w = rc.w * ls.x, h = rc.h * ls.y, sw = (float)F(0x5092C94, int (*)(void *))(NULL), sh = (float)F(0x5092CD4, int (*)(void *))(NULL);
    float x = tip_x, y = tip_y;
    if (x + w > sw) x = sw - w; if (x < 0) x = 0;
    if (y - h < 0) y = h; if (y > sh) y = sh;
    F(0x5109B04, void (*)(Obj, V3, void *))(r, (V3){ x, y, 0 }, NULL);             /* Transform.position */
    placed_by_us = 1; self_placed++;
}
/* every frame while shown: a DelayedAlignment run that has not finished after 0.3 s and 4 frames is stopped and the window placed
   by us; checked again every frame (a safety net: the 2-frame stock alignment normally finishes by itself) */
static void unstick(void) {
    Obj t = u_gc_get(h_last_tip);
    if (!u_alive(t)) return;
    if (AT(t, 0x170, Obj) == NULL) {                         /* aligned: an anchored card stays outside its panel */
        routine_since = 0;
        if (tgt_on && ours(t)) {
            Obj r = AT(t, 0x140, Obj);
            V3 p = is_class(r, "UnityEngine", "Transform") ? F(0x5109A08, V3 (*)(Obj, void *))(r, NULL) : (V3){ placed_x, placed_y, 0 };   /* Transform.position */
            if (fabsf(p.x - placed_x) > 0.5f || fabsf(p.y - placed_y) > 0.5f) place_card(t);
        }
        return;
    }
    double now = now_s(); static int64_t since_frame;
    if (!routine_since) { routine_since = now; since_frame = rig_frames; return; }
    if (now - routine_since >= 0.3 && rig_frames - since_frame >= 4) { place(t); routine_since = 0; }   /* the stock run needs 2 frames */
}
/* Hide when the pointer left: no show call for a whole frame AND 0.2 s. Time alone was wrong: at 4-5 fps one frame
   lasts more than 0.2 s, so every frame hid the tooltip and the next show built a new one whose 2-frame stock
   alignment (DelayedAlignment) never finished: the window stayed parked off-screen (1512x982 copies, 2026-10-14). */
void game_tip_frame(void) {
    if (!shown) return;
    if (now_s() > until && rig_frames > last_call_frame + 1) game_tip_hide(); else unstick();
}   /* MUST run every frame (ui_frame): it is the only hide on leave */

/* ---------------------------------------------------------------- test socket */
char *ui_tip_exec(const char *cmd, const char *line) {
    if (strcmp(cmd, "ui_tip")) return NULL;
    char act[16] = "show", title[160] = "", body[700] = "", key[96] = "";
    jstr(line, "action", act, sizeof act); jstr(line, "title", title, sizeof title); jstr(line, "body", body, sizeof body);
    int rc = 0;
    double tx, ty;
    if (jnum(line, "px", &tx) && jnum(line, "py", &ty)) { test_pos = 1; test_x = (float)tx; test_y = (float)ty; } else test_pos = 0;
    if (!strcmp(act, "show")) { game_tip_hide(); show_core(title, body, NULL); until = now_s() + 3600; }   /* tests: stays until hide */
    else if (!strcmp(act, "hide")) game_tip_hide();
    else if (!strcmp(act, "stock") && jstr(line, "key", key, sizeof key)) {                 /* a stock ETC tooltip (reference frame) */
        Obj v = tooltip_view(); TipCtx c; tip_ctx(&c); double x = 760, y = 500; jnum(line, "px", &x); jnum(line, "py", &y);
        if (v && build_ok()) F(0xF4A648, void (*)(Obj, Guid, int, Obj, V2, int, TipCtx, void *))(v, TIP_ID, 0, u_str(key), (V2){ (float)x, (float)y }, 0, c, NULL); else rc = -1;
        shown = 1; until = now_s() + 3600;
    } else if (!strcmp(act, "view")) {                       /* {ns, cls}: show at that stock view's RectTransform (placement test) */
        char ns[64] = "Refactor.UI", cls[64] = ""; jstr(line, "ns", ns, sizeof ns); jstr(line, "cls", cls, sizeof cls);
        Obj t = cls[0] ? type_of(ns, cls) : NULL, o = t ? F(0x50FD97C, Obj (*)(Obj, bool, void *))(t, false, NULL) : NULL;
        Obj tr = u_alive(o) ? F(0x50ED864, Obj (*)(Obj, void *))(o, NULL) : NULL;                   /* Component.transform */
        double hold = 0; jnum(line, "hold", &hold);
        if (tr) { show_core(title, body, tr); if (hold > 0) until = now_s() + hold; } else rc = -3;   /* else hides 0.2 s later (game_tip_frame) */
    } else if (strcmp(act, "probe") && strcmp(act, "chain")) rc = -2;
    Buf b = { 0 };
    bput(&b, "{\"ok\":%s,\"rc\":%d,\"view\":%d,\"hooked\":%d,\"tips_on\":%d,\"shown\":%d,\"filled\":%d", rc ? "false" : "true", rc,
         tooltip_view() != NULL, hooked, tips_on(), shown, u_alive(u_gc_get(h_last_tip)));
    { Obj t = u_gc_get(h_last_tip), go = u_go(t), r = u_alive(t) ? AT(t, 0x140, Obj) : NULL;   /* the drawn state of the last filled tooltip */
      V2 pos = is_class(r, "UnityEngine", "RectTransform") ? F(0x5106E38, V2 (*)(Obj, void *))(r, NULL) : (V2){ 0, 0 };
      V3 sc = is_class(r, "UnityEngine", "Transform") ? F(0x510AEFC, V3 (*)(Obj, void *))(r, NULL) : (V3){ 0, 0, 0 };
      bput(&b, ",\"tip_go\":[%d,%d],\"tip_pos\":[%.0f,%.0f],\"tip_scale\":%.2f,\"align_routine\":%d,\"use_screen\":%d,\"switches\":%u,\"self_placed\":%u}",
           u_alive(go) ? F(0x50F3638, bool (*)(Obj, void *))(go, NULL) : -1, u_alive(go) ? F(0x50F3724, bool (*)(Obj, void *))(go, NULL) : -1,
           pos.x, pos.y, sc.x, u_alive(t) && AT(t, 0x170, Obj) != NULL, u_alive(t) ? AT(t, 0x160, bool) : -1, switches, self_placed); }
    { Obj t = u_gc_get(h_last_tip), r = u_alive(t) ? AT(t, 0x140, Obj) : NULL; float q[4] = { 0, 0, 0, 0 }; int on = 0;   /* drawn rect, screen px */
      if (shown && is_class(r, "UnityEngine", "RectTransform")) {
          R4 rc = F(0x5106978, R4 (*)(Obj, void *))(r, NULL);
          V3 a0 = F(0x510E0AC, V3 (*)(Obj, V3, void *))(r, (V3){ rc.x, rc.y, 0 }, NULL), a1 = F(0x510E0AC, V3 (*)(Obj, V3, void *))(r, (V3){ rc.x + rc.w, rc.y + rc.h, 0 }, NULL);
          float sw = (float)F(0x5092C94, int (*)(void *))(NULL), sh = (float)F(0x5092CD4, int (*)(void *))(NULL);
          q[0] = a0.x; q[1] = a0.y; q[2] = a1.x; q[3] = a1.y;
          on = a1.x > a0.x && a1.y > a0.y && a0.x >= -1 && a0.y >= -1 && a1.x <= sw + 1 && a1.y <= sh + 1; }
      b.p[--b.n] = 0;                                        /* reopen the object */
      bput(&b, ",\"screen_rect\":[%.0f,%.0f,%.0f,%.0f],\"on_screen\":%d", q[0], q[1], q[2], q[3], on);
      /* owner v12 item 3: the card's place against the element's panel (screen px, y up). tip_xy = the card's top-left. */
      bput(&b, ",\"anchored\":%d,\"side\":\"%s\",\"panel_rect\":[%.0f,%.0f,%.0f,%.0f],\"elem_rect\":[%.0f,%.0f,%.0f,%.0f],\"tip_xy\":[%.0f,%.0f],\"screen\":[%.0f,%.0f]}",
           tgt_on, tgt_on ? tgt_side : "", tgt_panel[0], tgt_panel[1], tgt_panel[2], tgt_panel[3], tgt_elem[0], tgt_elem[1], tgt_elem[2], tgt_elem[3],
           q[0], q[3], (float)F(0x5092C94, int (*)(void *))(NULL), (float)F(0x5092CD4, int (*)(void *))(NULL)); }
    if (!strcmp(act, "chain")) {                              /* debug: the element's ancestors with their screen rects */
        static uintptr_t hg; Obj gty = u_gc_get(hg); if (!gty) { gty = type_of("UnityEngine.UI", "Graphic"); hg = u_gc_new(gty); }
        b.p[--b.n] = 0; bput(&b, ",\"chain\":[");
        int d = 0;
        for (Obj t = u_gc_get(h_rect); t && d < 16; t = tr_parent(t), d++) {
            float q[4] = { 0, 0, 0, 0 }; char nm[64]; scr_rect(t, q); obj_name(t, nm, sizeof nm);
            Obj g = gty ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(t, gty, NULL) : NULL;
            bput(&b, "%s{\"name\":\"%s\",\"rect\":[%.0f,%.0f,%.0f,%.0f],\"graphic\":%d}", d ? "," : "", nm, q[0], q[1], q[2], q[3], u_alive(g));
        }
        bput(&b, "]}");
    }
    return b.p;
}
