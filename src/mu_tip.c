// Copy of ds_custommodes units/mu_tip.c at 095d27a, for the multiplayer mod's Bring Units card window (no link to Units Laboratory).
// mu_tip.c - the game's own hover card on My Units / Bring Units icons (owner 2026-10-17: "hovering an item / skill /
// rune icon must show the game's own full hover card ... not just a plain name box"). The card is the game's TooltipUI
// (Refactor.UI, a StaticUI on the title and in the world) showing its own Tooltip_Item / Tooltip_Skill / Tooltip_Affecter
// for the real key: TooltipUI.ShowTooltipAtScreenPosition(Guid, TooltipType, key, top-left, TalentType, TooltipContext) at
// the icon's right edge (the game's own screen clamp keeps a tall item card on the screen), same frame, fonts and texts as
// the game's inventory and skill bar. No "Pin" (hold) line: there is no hold key outside a world. Called through
// il2cpp_runtime_invoke, so a game exception (a key the game does not know) is caught and logged, never a crash.
// Hover = the mouse inside the icon's rect (RectTransformUtility.RectangleContainsScreenPoint), polled on the main thread
// (mu_tick); the icons keep raycastTarget off, so clicks still reach what is under them.
#include "mu_ui.h"
#include "cm_il.h"
#include <stdio.h>
#include <string.h>

typedef struct { float x, y; } V2t;
typedef struct { float x, y, z; } V3t;
typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } Guidt;
static const Guidt TIP_ID = { 0x6d757469u, 0x7073, 0x0001, { 'c', 'm', 'u', 'n', 'i', 't', 's', 1 } };   /* our one session */
enum { TT_SKILL = 1, TT_AFFECTER = 2, TT_ITEM = 3 };

typedef struct { uintptr_t h; int type; char key[96]; } Tip;
#define NTIP 512
static Tip tips[NTIP]; static int ntips;
static int shown = -1;                         /* index in tips of the card shown, -1 none */
static uintptr_t h_view, h_shown_rt;
static int state = 0;                          /* 0 not set up, 1 ok, -1 off (a method is missing) */
static cm_m m_show, m_hide, m_mouse, m_contains, m_inparent, m_root, m_mode, m_cam, m_override, m_order, m_active, m_addcomp,
            m_corners, m_w2s, m_finds, m_tid, m_nohold;
static void *k_view, *k_canvas, *k_raycaster, *k_ctx, *k_v3, *k_tb;
static int32_t ctx_size, f_hidehold, f_canhold;
static Obj (*array_new)(void *klass, uintptr_t n);
static Obj (*invoke)(const void *mi, void *obj, void **params, Obj *exc);
static int32_t (*value_size)(void *klass, uint32_t *align);
static int ntested;                            /* test: hovers that showed a card */
static char last_err[200];

static int setup(void) {
    if (state) return state > 0;
    state = -1;
    const DSLHost *H = mu_host(); if (!H) return 0;
    *(void **)&invoke = H->il2cpp_fn("il2cpp_runtime_invoke");
    *(void **)&value_size = H->il2cpp_fn("il2cpp_class_value_size");
    *(void **)&array_new = H->il2cpp_fn("il2cpp_array_new");
    const char *RU = "Refactor.UI", *UE = "UnityEngine";
    k_view = cm_il_class(RU, "TooltipUI"); k_ctx = cm_il_class(RU, "TooltipContext");
    k_canvas = cm_il_class(UE, "Canvas"); k_raycaster = cm_il_class("UnityEngine.UI", "GraphicRaycaster");
    m_show = cm_il_method(RU, "TooltipUI", "ShowTooltipAtScreenPosition", 6, NULL);
    k_v3 = cm_il_class(UE, "Vector3"); k_tb = cm_il_class(RU, "Tooltip_Base");
    m_corners = cm_il_method(UE, "RectTransform", "GetWorldCorners", 1, NULL);
    m_w2s = cm_il_method(UE, "RectTransformUtility", "WorldToScreenPoint", 2, "UnityEngine.Camera,UnityEngine.Vector3");
    m_finds = cm_il_method(UE, "Object", "FindObjectsOfType", 1, "System.Type");
    m_tid = cm_il_method(RU, "Tooltip_Base", "get_TooltipID", 0, NULL);
    m_nohold = cm_il_method(RU, "Tooltip_Base", "DisableHoldLayout", 0, NULL);
    f_canhold = k_tb ? cm_il_field(k_tb, "_canShowHoldLayout") : -1;
    m_hide = cm_il_method(RU, "TooltipUI", "HideToolTip", 1, NULL);
    m_mouse = cm_il_method(UE, "Input", "get_mousePosition", 0, NULL);
    m_contains = cm_il_method(UE, "RectTransformUtility", "RectangleContainsScreenPoint", 3, "UnityEngine.RectTransform,UnityEngine.Vector2,UnityEngine.Camera");
    m_inparent = cm_il_method(UE, "Component", "GetComponentInParent", 1, "System.Type");
    m_root = cm_il_method(UE, "Canvas", "get_rootCanvas", 0, NULL);
    m_mode = cm_il_method(UE, "Canvas", "get_renderMode", 0, NULL);
    m_cam = cm_il_method(UE, "Canvas", "get_worldCamera", 0, NULL);
    m_override = cm_il_method(UE, "Canvas", "set_overrideSorting", 1, NULL);
    m_order = cm_il_method(UE, "Canvas", "set_sortingOrder", 1, NULL);
    m_active = cm_il_method(UE, "GameObject", "get_activeInHierarchy", 0, NULL);
    m_addcomp = cm_il_method(UE, "GameObject", "AddComponent", 1, "System.Type");
    int32_t o = k_ctx ? cm_il_field(k_ctx, "<HideHoldLayout>k__BackingField") : -1;
    f_hidehold = o >= 0x10 ? o - 0x10 : -1;                            /* value type: minus the object header */
    ctx_size = k_ctx && value_size ? value_size(k_ctx, NULL) : 0;
    if (!invoke || !k_view || !m_show.mi || !m_hide.mi || !m_mouse.fn || !m_contains.fn || !m_inparent.fn || !m_root.fn || !m_mode.fn
        || !m_active.fn || !m_corners.fn || !array_new || !k_v3 || ctx_size <= 0 || ctx_size > 0x100 || f_hidehold < 0 || f_hidehold >= ctx_size) {
        mu_log("my units: game hover cards off (TooltipUI %p, show %p, context size %d, hide-hold %d)", k_view, m_show.fn, ctx_size, f_hidehold);
        return 0;
    }
    state = 1;
    return 1;
}
static Obj view(void) {
    Obj v = cm_il_hget(h_view);
    if (cm_alive(v)) return v;
    v = cm_find(k_view, true);
    cm_il_hset(&h_view, v);
    return v;
}
/* the tooltip view above the My Units window (kit layer 30900, notices 32040+), as the kit does for its own tips */
static void raise_view(Obj v) {
    if (!k_canvas || !m_override.fn || !m_order.fn) return;
    Obj cv = CM_CALL(m_inparent, Obj, Obj, Obj, const void *)(v, cm_il_type_obj(k_canvas), m_inparent.mi);
    if (!cm_alive(cv)) return;
    Obj root = CM_CALL(m_root, Obj, Obj, const void *)(cv, m_root.mi);
    if (root == cv) {                                                  /* the view sits in the root canvas: its own canvas */
        Obj g = cm_go(v); if (!g || !m_addcomp.fn) return;
        cv = cm_comp(g, k_canvas);
        if (!cv) { cv = CM_CALL(m_addcomp, Obj, Obj, Obj, const void *)(g, cm_il_type_obj(k_canvas), m_addcomp.mi);
                   if (cv && k_raycaster && !cm_comp(g, k_raycaster)) CM_CALL(m_addcomp, Obj, Obj, Obj, const void *)(g, cm_il_type_obj(k_raycaster), m_addcomp.mi); }
        if (!cm_alive(cv)) return;
    }
    CM_CALL(m_override, void, Obj, bool, const void *)(cv, true, m_override.mi);
    CM_CALL(m_order, void, Obj, int, const void *)(cv, 32050, m_order.mi);
}
static int call(cm_m m, Obj self, void **args, const char *what) {
    Obj exc = NULL;
    invoke(m.mi, self, args, &exc);
    if (exc) { snprintf(last_err, sizeof last_err, "%s threw", what); mu_log("my units: game hover card: %s", last_err); return -1; }
    return 0;
}
static void hide(void) {
    if (shown < 0) return;
    shown = -1; cm_il_hfree(h_shown_rt); h_shown_rt = 0;
    Obj v = view(); if (!v) return;
    Guidt g = TIP_ID; void *a[1] = { &g };
    call(m_hide, v, a, "HideToolTip");
}
static Obj cam_of(Obj rt);
/* the icon's rect in screen pixels (x0, y0, x1, y1; y up), 0 ok */
static int screen_rect(Obj rt, float r[4]) {
    static uintptr_t h_arr; Obj arr = cm_il_hget(h_arr);
    if (!arr) { arr = array_new(k_v3, 4); if (!arr) return -1; h_arr = cm_il_hnew(arr); }
    CM_CALL(m_corners, void, Obj, Obj, const void *)(rt, arr, m_corners.mi);
    V3t *c = (V3t *)((char *)arr + 0x20);
    Obj cam = cam_of(rt);
    for (int i = 0; i < 4; i++) if (cam && m_w2s.fn) { V2t s2 = CM_CALL(m_w2s, V2t, Obj, V3t, const void *)(cam, c[i], m_w2s.mi); c[i].x = s2.x; c[i].y = s2.y; }
    r[0] = r[2] = c[0].x; r[1] = r[3] = c[0].y;
    for (int i = 1; i < 4; i++) { if (c[i].x < r[0]) r[0] = c[i].x; if (c[i].x > r[2]) r[2] = c[i].x; if (c[i].y < r[1]) r[1] = c[i].y; if (c[i].y > r[3]) r[3] = c[i].y; }
    return 0;
}
/* the shown card (the pooled Tooltip_Base with our id): no "Pin" / hold line, as the kit does for its own tips */
static uintptr_t h_card; static int fit_checks, nmoved; static V2t shown_pos; static float shown_icon_x0;
static void no_hold(void) {
    cm_il_hset(&h_card, NULL);
    if (!m_finds.fn || !m_tid.fn || !k_tb) return;
    Obj arr = CM_CALL(m_finds, Obj, Obj, const void *)(cm_il_type_obj(k_tb), m_finds.mi);
    uintptr_t n = arr ? *(uintptr_t *)((char *)arr + 0x18) : 0;
    for (uintptr_t k = 0; k < n; k++) {
        Obj t = ((Obj *)((char *)arr + 0x20))[k];
        if (!cm_alive(t)) continue;
        Guidt g = CM_CALL(m_tid, Guidt, Obj, const void *)(t, m_tid.mi);
        if (memcmp(&g, &TIP_ID, sizeof g)) continue;
        if (f_canhold >= 0) CM_AT(t, f_canhold, bool) = false;
        if (m_nohold.fn) CM_CALL(m_nohold, void, Obj, const void *)(t, m_nohold.mi);
        cm_il_hset(&h_card, t);
        return;
    }
}
static int show_at(int i, V2t pos) {
    Obj v = view();
    uint8_t ctx[0x100]; memset(ctx, 0, sizeof ctx); ctx[f_hidehold] = 1;   /* no "hold for details" line: no hold key here */
    Guidt g = TIP_ID; int type = tips[i].type, talent = 0;
    void *a[6] = { &g, &type, cm_il_str(tips[i].key), &pos, &talent, ctx };
    if (!v || call(m_show, v, a, tips[i].key)) return -1;
    shown_pos = pos;
    no_hold();
    return 0;
}
static void show(int i) {
    Obj v = view(), rt = cm_il_hget(tips[i].h);
    if (!v || !rt) return;
    if (shown >= 0) hide();
    raise_view(v);
    float r[4]; if (screen_rect(rt, r)) return;
    /* the card's top-left: right of the icon, level with its top */
    if (show_at(i, (V2t){ r[2] + 6, r[3] })) { tips[i].type = 0; return; }   /* the game cannot show this key: plain icon from now */
    shown = i; cm_il_hset(&h_shown_rt, rt); ntested++;
    shown_icon_x0 = r[0]; fit_checks = 100;                            /* ~5 s: a slow frame lays the card out late */
}
/* the card gets its size in the next frames: a tall card (an item with its recipe) that runs off the bottom goes up,
   one off the right edge goes to the icon's left. The game clamps nothing for a screen position. */
static void fit(void) {
    if (shown < 0 || fit_checks <= 0) return;
    fit_checks--;
    static cm_m sw, sh; if (!sw.fn) { sw = cm_il_method("UnityEngine", "Screen", "get_width", 0, NULL); sh = cm_il_method("UnityEngine", "Screen", "get_height", 0, NULL); }
    Obj card = cm_il_hget(h_card); float c[4];
    if (!sw.fn || !sh.fn || !cm_alive(card) || screen_rect(cm_tr(card), c) || c[3] - c[1] < 2) return;
    float W = (float)CM_CALL(sw, int, const void *)(sw.mi), Hh = (float)CM_CALL(sh, int, const void *)(sh.mi);
    V2t p = shown_pos;
    if (c[1] < 4) p.y += 4 - c[1];                                     /* y is up: the bottom is below the screen */
    if (p.y > Hh - 4) p.y = Hh - 4;
    if (c[2] > W - 4) p.x = shown_icon_x0 - 6 - (c[2] - c[0]);
    if (p.x < 4) p.x = 4;
    if (p.x == shown_pos.x && p.y == shown_pos.y) return;
    fit_checks = 0; nmoved++;
    { Obj v = view(); Guidt g = TIP_ID; void *a[1] = { &g }; if (v) call(m_hide, v, a, "HideToolTip"); }   /* a show with a live id keeps its place */
    show_at(shown, p);
}
static Obj cam_of(Obj rt) {
    Obj cv = CM_CALL(m_inparent, Obj, Obj, Obj, const void *)(rt, cm_il_type_obj(k_canvas), m_inparent.mi);
    if (!cm_alive(cv)) return NULL;
    Obj root = CM_CALL(m_root, Obj, Obj, const void *)(cv, m_root.mi);
    if (!cm_alive(root) || CM_CALL(m_mode, int, Obj, const void *)(root, m_mode.mi) == 0 || !m_cam.fn) return NULL;   /* overlay */
    return CM_CALL(m_cam, Obj, Obj, const void *)(root, m_cam.mi);
}
static void tick(void) {
    if (state <= 0) return;
    for (int i = ntips - 1; i >= 0; i--) {                             /* forget icons that are gone (window rebuilt / closed) */
        if (cm_alive(cm_il_hget(tips[i].h))) continue;
        if (shown == i) { shown = -1; cm_il_hfree(h_shown_rt); h_shown_rt = 0; Obj v = view(); if (v) { Guidt g = TIP_ID; void *a[1] = { &g }; call(m_hide, v, a, "HideToolTip"); } }
        cm_il_hfree(tips[i].h);
        if (shown == ntips - 1) shown = i;
        tips[i] = tips[--ntips];
    }
    if (!ntips && shown < 0) return;
    V3t m = CM_CALL(m_mouse, V3t, const void *)(m_mouse.mi);
    int hit = -1; Obj cam = NULL; int cam_done = 0;
    for (int i = 0; i < ntips && hit < 0; i++) {
        if (!tips[i].type) continue;
        Obj rt = cm_il_hget(tips[i].h), g = cm_go(rt);
        if (!g || !CM_CALL(m_active, bool, Obj, const void *)(g, m_active.mi)) continue;
        if (!cam_done) { cam = cam_of(rt); cam_done = 1; }
        if (CM_CALL(m_contains, bool, Obj, V2t, Obj, const void *)(rt, (V2t){ m.x, m.y }, cam, m_contains.mi)) hit = i;
    }
    if (hit >= 0 && hit != shown) show(hit);
    else if (hit < 0 && shown >= 0) hide();
    else fit();
}

int mu_tip_game(DSLObj target, int kind, const char *key) {
    if (!target || !key || !*key || !setup()) return 0;
    int type = kind == CM_ICON_ITEM ? TT_ITEM : kind == CM_ICON_SKILL ? TT_SKILL : kind == CM_ICON_AFFECTER ? TT_AFFECTER : 0;
    if (!type || ntips >= NTIP) return 0;
    Tip *t = &tips[ntips++];
    t->h = cm_il_hnew(target); t->type = type; snprintf(t->key, sizeof t->key, "%s", key);
    mu_tick_add(tick);
    return 1;
}
void mu_tip_hide(void) { if (state > 0) hide(); }
/* an icon about to be destroyed: its entry goes now (a window that rebuilds a page would hold the old and the new icons
   until the next tick, and the table is full sooner) */
void mu_tip_forget(DSLObj target) {
    for (int i = ntips - 1; i >= 0; i--) {
        if (cm_il_hget(tips[i].h) != (Obj)target) continue;
        if (shown == i) hide();
        cm_il_hfree(tips[i].h);
        if (shown == ntips - 1) shown = i;
        tips[i] = tips[--ntips];
        return;
    }
}
/* test: the hover icons drawn (in order) and the card shown */
DSLObj mu_tip_test_target(int i) { return i >= 0 && i < ntips ? (DSLObj)cm_il_hget(tips[i].h) : NULL; }
size_t mu_tip_test_state(char *out, size_t cap) {
    float c[4] = { 0 }; Obj card = shown >= 0 ? cm_il_hget(h_card) : NULL;
    if (cm_alive(card)) screen_rect(cm_tr(card), c);
    return (size_t)snprintf(out, cap, "\"tip\":{\"on\":%d,\"icons\":%d,\"shown\":\"%s\",\"type\":%d,\"cards\":%d,\"err\":\"%s\",\"card\":[%.0f,%.0f,%.0f,%.0f],\"moved\":%d,\"fit\":%d}",
                            state, ntips, shown >= 0 ? tips[shown].key : "", shown >= 0 ? tips[shown].type : 0, ntested, last_err, c[0], c[1], c[2], c[3],
                            nmoved, fit_checks);
}
