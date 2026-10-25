// mpmenu.c - the in-game Multiplayer menu (build 25846389, arm64, dylib only). Rules: mpmenu_model.c.
//  Title screen: clones of the stock "Credits" button (Multiplayer, Host, host settings, Back, guest status + Cancel).
//  Host flow: settings on the title -> the stock load list or new-campaign screen -> once the campaign is in, the
//  session starts and the lobby panel opens: the stock Esc menu (SystemMenuView, popup 1) with its buttons hidden,
//  a text line (players + settings) and clones Invite friends / Start / Leave multiplayer.
//  Guest: after a Steam invite is accepted, the title shows "Joining the host's game..." + Cancel.
//  In game: the Esc menu gets "Invite friends" (host) and "Leave multiplayer".
// Clicks: two hooks, SubUI_SystemMenuButton.OnPressButton (Esc-menu clones) and SubUI_TitleMenuButton.OnPointerClick
// (title clones; the ui lane gave the title screen to this file). Views are found with FindObjectOfType (no Init hooks).
#include "mpmenu_kit.h"
#include "mpmenu.h"
#include "mp_fog.h"
#include "mp_game.h"
#include "mp_steam.h"
#include "rig.h"
#include "ui_model.h"
#include "ds_i18n.h"                                     /* TR / TRN, language files, UTF-8 cuts (one DSI_IMPLEMENTATION: ds_i18n.c) */
#define DSMC_TR(s) TR(s)                                 /* ds_modcheck.h: the hostmods box in the player's language */
#include "net/ds_modcheck.h"                             /* "Get the host's mods": plan JSON -> box text, DSMP_REJOIN line */
#include "mp_api.h"                                      /* mod chat lines (chat_line, mp_modchat.c) */
#include "extaddr.h"                                     /* the host's internet address for the lobby (background lookup) */
#include "upnpport.h"                                    /* the router port for friends outside the home (UPnP, background) */
#include "fe_ui_api.h"                                  /* fe_mode_get: the hub lobby's hint line */
#include "mp_worldsave.h"                                /* mp_hub_guid: the hub is never saved, tagged or listed */
#include "mp_solo.h"                                     /* the single-player copy of a world new to multiplayer */
#include "mp_esc.h"                                       /* Escape for our windows through the loader kit (kit 0.1.28) */
#include "ui_tip.h"                                      /* game_tip_show_rect: our tooltips in the game's own window */                                    /* ui_player_color, ui_conn_level / ui_conn_color (one indicator) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreGraphics/CoreGraphics.h>

/* Typed calls are class-checked (lead 2026-10-13, after a Graphic.set_color on a Transform crashed a copy): mm_is(o, K_x)
 * = il2cpp_object_get_class 0x6F96C0 + il2cpp_class_is_assignable_from 0x6F8BE0; a mismatch is logged once and the call
 * is skipped (its default value is used). */
static const struct { const char *ns, *name; } mm_kinds[K_N] = {
    { "UnityEngine", "RectTransform" },
    { "UnityEngine", "Transform" },
    { "UnityEngine", "Component" },
    { "UnityEngine", "GameObject" },
    { "TMPro", "TMP_Text" },
    { "TMPro", "TMP_InputField" },
    { "UnityEngine.UI", "Graphic" },
    { "UnityEngine.UI", "Image" },
    { "UnityEngine", "Behaviour" },
    { "UnityEngine.UI", "Selectable" },
    { "Refactor.UI", "SubUI_SystemMenuButton" },
    { "Refactor.UI", "SystemMenuView" },
    { "Refactor.UI", "TitleMenuView" },
    { "Refactor.UI", "CommonNoticeView" }
};
void *class_by_name(const char *ns, const char *name);
int mm_is(Obj o, int k) {
    static void *kc[K_N]; static int warned[K_N];
    if (!o || !F(0x50FAF88, bool (*)(Obj, void *))(o, NULL)) return 0;    /* destroyed (scene load): its calls throw NRE (u_alive) */
    if (!kc[k]) kc[k] = class_by_name(mm_kinds[k].ns, mm_kinds[k].name);
    void *oc = F(0x6F96C0, void *(*)(Obj))(o);
    if (kc[k] && oc && F(0x6F8BE0, bool (*)(void *, void *))(kc[k], oc)) return 1;
    if (!warned[k]) { warned[k] = 1; rig_log("mpmenu: typed call skipped: a %s where %s.%s was expected", oc ? *(const char **)((char *)oc + 0x10) : "?", mm_kinds[k].ns, mm_kinds[k].name); }
    return 0;
}
/* ---------------------------------------------------------------- Unity / il2cpp (RVAs as ui_game.c, ~/ds_multibar game.h) */
int rect_of(Obj comp, float r[4]);
static double mono_s(void);
static const char *palette_color(int idx);
static const char *cls_name(Obj o) { return o ? *(const char **)((char *)*(void **)o + 0x10) : ""; }
/* DASText._useTextKey / _textKey by name: Mac 0x7C0 / 0x7C8, Windows 0x7C8 / 0x7D0 (MSVC puts no subclass field in the
   base's tail padding). The Mac 0x7C0 on Windows cleared the low byte of TextMeshProUGUI.materialIndexPairs: v0.1.21 F1,
   a guest crashed at the title load (Unity's asset GC walked a chat text of the chat canvas kept across scenes). */
static int das_off(int k) {                                                 /* 0 _useTextKey, 1 _textKey, 2 _text; -1 = not found */
    static int off[3] = { -2, -2, -2 }; static const char *const nm[3] = { "_useTextKey", "_textKey", "_text" };
    if (off[k] == -2) {
        void *c = class_by_name("Refactor.UI", "DASText"); if (!c) return -1;   /* not loaded yet: again next time */
        void *f = F(0x6F8C14, void *(*)(void *, const char *))(c, nm[k]);      /* il2cpp_class_get_field_from_name */
        off[k] = f ? (int)F(0x6F949C, size_t (*)(void *))(f) : -1;            /* il2cpp_field_get_offset */
        if (off[k] < 0) rig_log("mpmenu: DASText.%s not found: its text key is left as is", nm[k]);
        else rig_log("mpmenu: DASText.%s at %#x", nm[k], off[k]);
    }
    return off[k];
}
static int das_at(Obj t, int k) { return t && !strcmp(cls_name(t), "DASText") ? das_off(k) : -1; }
bool das_key_on(Obj t) { int o = das_at(t, 0); return o > 0 && AT(t, o, bool); }
void das_key_set(Obj t, bool on) { int o = das_at(t, 0); if (o > 0) AT(t, o, bool) = on; }
void das_key_clear(Obj t) { das_key_set(t, false); int o = das_at(t, 1); if (o > 0) AT(t, o, Obj) = NULL; }   /* our text, no key */
void das_text_keep(Obj t, Obj s) { int o = das_at(t, 2); if (o > 0) F(0x6F95C8, void (*)(Obj, void **, Obj))(t, (void **)((char *)t + o), s); }   /* _text = s (write barrier) */
/* TMP_Text.text: virtual slots 65 (get, +0x548) / 66 (set, +0x558) */
static Obj tmp_get(Obj t) {
    if (!u_alive(t) || !mm_is(t, K_TMP)) return NULL;
    void *k = *(void **)t;
    return (*(Obj (**)(Obj, void *))((char *)k + 0x548))(t, *(void **)((char *)k + 0x550));
}
/* il2cpp_string_new returns an EMPTY string for UTF-8 that is not valid (research/i18n_plan.md 4): a label cut by bytes
 * inside a character (snprintf into a fixed buffer) would be blank. The broken end is dropped first, on a copy. */
static Obj u_str_ok(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s); char sb[1024], *b = n < sizeof sb ? sb : malloc(n + 1);
    if (!b) return u_str(s);
    memcpy(b, s, n + 1); dsi_utf8_fix_end(b);
    Obj r = u_str(b);
    if (b != sb) free(b);
    return r;
}
static void fit_text(Obj t, const char *s);
static void fit_dirty(Obj t);
static void fit_style(Obj t, float size, int wrap);
void tmp_set(Obj t, const char *s) {
    if (!u_alive(t) || !mm_is(t, K_TMP)) return;
    das_key_set(t, false);                                                  /* DASText._useTextKey: keep our raw text */
    void *k = *(void **)t;
    (*(void (**)(Obj, Obj, void *))((char *)k + 0x558))(t, u_str_ok(s), *(void **)((char *)k + 0x560));
    fit_text(t, s);
}
static void tmp_set_obj(Obj t, Obj s) {                                     /* the same, with an il2cpp string as is */
    if (!u_alive(t) || !mm_is(t, K_TMP)) return;
    void *k = *(void **)t;
    (*(void (**)(Obj, Obj, void *))((char *)k + 0x558))(t, s, *(void **)((char *)k + 0x560));
    fit_dirty(t);
}
void utf16_to_utf8(Obj s, char *out, size_t cap);
/* copy of an il2cpp string (read-back). i18n: whole UTF-8 characters (it was ASCII with '?': a translated stock label or a
 * Korean / Chinese world name read back and written again came out as "???") */
void str_utf8(Obj s, char *out, size_t cap) { utf16_to_utf8(s, out, cap); }
/* class lookup over the loaded assemblies (il2cpp API exports) + FindObjectOfType(Type, includeInactive) */
void *class_by_name(const char *ns, const char *name);
Obj image_of(Obj comp) {                                            /* GetComponent(typeof(UnityEngine.UI.Image)) */
    static void *k; if (!k) k = class_by_name("UnityEngine.UI", "Image");
    if (!k || !u_alive(comp)) return NULL;
    Obj ty = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k));
    return (mm_is(comp, K_COMP) ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(comp, ty, NULL) : NULL);
}

void *class_by_name(const char *ns, const char *name) {
    void *dom = F(0x6F91F4, void *(*)(void))();
    size_t n = 0;
    void **as = F(0x6F9200, void **(*)(void *, size_t *))(dom, &n);
    for (size_t i = 0; as && i < n; i++) {
        void *img = F(0x6F8BBC, void *(*)(void *))(as[i]);
        void *k = img ? F(0x6F8BF4, void *(*)(void *, const char *, const char *))(img, ns, name) : NULL;
        if (k) return k;
    }
    return NULL;
}
/* fastboot_mp (2026-10-17): an ABSENT view is looked up again at most every FV_RETRY_MS (was 15 frames) per class
   name + mode (was per call site). FindObjectOfType walks the scene's objects and ran every frame for views the scene does not have (the title
   view in the world, the Esc menu before it was first opened, the HUD on the title): a big part of this module's main-
   thread time during a world load. A found view keeps its cached handle (unchanged). Lookups stay immediate during a rig
   command (mpmenu_fv_kick from rig_exec), for 10 frames after a key / mouse press (Input.anyKeyDown, read in
   mpmenu_frame; without that icall: always immediate, as before) and inside find_view_now (actions that open a view).
   UI only: no sim state. */
#define FV_RETRY_MS 2000.0    /* fpsfix 2026-10-20: was 15 frames (14 lookups/s per absent view at 210 fps, 2.5-4 ms each) */
#define FV_SITES 64
static int64_t fv_free_until = INT64_MAX;                         /* immediate lookups while rig_frames < this */
static int fv_now_depth;
static long fv_kicks[2];
static void fv_kick_n(int n) { fv_kicks[n == 10]++; if (fv_free_until != INT64_MAX && fv_free_until < rig_frames + n) fv_free_until = rig_frames + n; }
void mpmenu_fv_kick(void) { fv_kick_n(10); }
/* fpsfix: an absent view is looked up again after FV_RETRY_MS (wall clock, not frames), and at most one throttled
   lookup runs per frame (the others wait a frame): no frame pays for several FindObjectOfType walks. Immediate lookups
   (a kick): 10 frames after a rig command or a stock popup opening (ui_game hk_activate_view), 3 frames after a key /
   mouse press. A new scene or the end of a load only makes every absent view due again (still one per frame). */
static int64_t fv_budget_frame = -1;
static double fv_clock(void);
static int fv_kicked(void) { return rig_frames < fv_free_until || fv_now_depth; }
static int fv_may_retry(double next) {                             /* 1 = look it up now */
    if (fv_kicked()) return 1;
    if (fv_clock() < next || fv_budget_frame == rig_frames) return 0;
    fv_budget_frame = rig_frames; return 1;
}
/* absent views by class name + mode ('a' active only, 'i' with inactive, '*' active then inactive): an absent class is
   absent for every caller (some callers pass a handle on the stack, several handles look for one class) */
static struct { char key[96]; double next; } fv_miss[FV_SITES];
static int fv_rr;
static int fv_slot(const char *ns, const char *name, char mode, char *key, size_t cap) {   /* index or -1 */
    snprintf(key, cap, "%s.%s/%c", ns, name, mode);
    for (int i = 0; i < FV_SITES; i++) if (fv_miss[i].key[0] && !strcmp(fv_miss[i].key, key)) return i;
    return -1;
}
static void fv_missed(int i, const char *key) {
    if (i < 0) {                                                       /* a free slot, else round robin */
        for (i = 0; i < FV_SITES && fv_miss[i].key[0]; i++) {}
        if (i == FV_SITES) { i = fv_rr; fv_rr = (fv_rr + 1) % FV_SITES; }
        snprintf(fv_miss[i].key, sizeof fv_miss[i].key, "%s", key);
    }
    fv_miss[i].next = fv_clock() + FV_RETRY_MS;
}
static Obj find_view_lookup(uintptr_t *h, void **kcache, const char *ns, const char *name, bool inactive);
static double fv_clock(void);
static double fv_ms_sum, fv_ms_max; static long fv_lookups;
static struct { char key[80]; long n; double ms; } fv_cnt[96]; static int fv_ncnt;
static void fv_count(const char *ns, const char *name, double ms) {
    char key[80]; snprintf(key, sizeof key, "%s.%s", ns, name);
    int i; for (i = 0; i < fv_ncnt && strcmp(fv_cnt[i].key, key); i++) {}
    if (i == fv_ncnt) { if (fv_ncnt == 96) return; snprintf(fv_cnt[fv_ncnt++].key, 80, "%s", key); }
    fv_cnt[i].n++; fv_cnt[i].ms += ms;
}
const char *mpmenu_fv_counts(void) {
    static char o[4096]; size_t k = 0; o[0] = 0;
    for (int i = 0; i < fv_ncnt && k < sizeof o - 120; i++) k += (size_t)snprintf(o + k, sizeof o - k, "%s[\"%s\",%ld,%.1f]", i ? "," : "", fv_cnt[i].key, fv_cnt[i].n, fv_cnt[i].ms);
    return o;
}
Obj find_view_x(uintptr_t *h, void **kcache, const char *ns, const char *name, bool inactive) {
    Obj v = u_gc_get(*h);
    if (u_alive(v)) return v;
    char key[96]; int i = fv_slot(ns, name, inactive ? 'i' : 'a', key, sizeof key);
    if (i >= 0 && !fv_may_retry(fv_miss[i].next)) return NULL;       /* retried later */
    v = find_view_lookup(h, kcache, ns, name, inactive);
    if (v) { if (i >= 0) fv_miss[i].key[0] = 0; }
    else fv_missed(i, key);
    return v;
}
/* fpsfix (2026-10-20, player report "the mod lowers FPS even in single player"): the ui lanes' find_view (ui_game.c,
   ui_teams.c: active object first, then an inactive one) ran FindObjectOfType twice per call for every view the scene
   does not have, 10 to 60 times a second (pause view, quick slots, banner, Esc menu on the title): half of the main
   thread on the title, 18% in a world (macOS sample, rig 2026-10-20). Same throttle as find_view_x, keyed by class
   name (an absent class is absent for every caller; some callers pass a handle on the stack). */
Obj mpmenu_find_any(uintptr_t *h, const char *ns, const char *name) {
    Obj v = u_gc_get(*h);
    if (u_alive(v)) return v;
    char key[96]; int i = fv_slot(ns, name, '*', key, sizeof key);
    if (i >= 0 && !fv_may_retry(fv_miss[i].next)) return NULL;       /* retried later */
    double t0 = fv_clock();
    void *k = class_by_name(ns, name);
    Obj tobj = k ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k)) : NULL;
    v = tobj ? F(0x50FD97C, Obj (*)(Obj, bool, void *))(tobj, false, NULL) : NULL;   /* an active one first (scene copies) */
    if (tobj && !u_alive(v)) v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(tobj, true, NULL);
    double d = fv_clock() - t0; fv_ms_sum += d; fv_lookups++; if (d > fv_ms_max) fv_ms_max = d; fv_count(ns, name, d);
    u_gc_free(*h); *h = u_alive(v) ? u_gc_new(v) : 0;
    if (u_alive(v)) { if (i >= 0) fv_miss[i].key[0] = 0; return v; }
    fv_missed(i, key);
    return NULL;
}
static double fv_clock(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
static Obj find_view_now(uintptr_t *h, void **k, const char *ns, const char *name, bool inactive) {
    fv_now_depth++; Obj v = find_view_x(h, k, ns, name, inactive); fv_now_depth--; return v;
}
static void fv_input_frame(void) {                                    /* once per frame: a key / mouse press opens views */
    static bool (*anykey)(void); static int looked;
    if (!looked) {
        looked = 1; anykey = (bool (*)(void))F(0x6F8B90, void *(*)(const char *))("UnityEngine.Input::get_anyKeyDown()");
        rig_log("mpmenu: view lookups %s", anykey ? "throttled while absent (every 2 s, one per frame; immediate after input / a popup)" : "immediate (Input.anyKeyDown not available)");
        if (anykey) fv_free_until = 0;
    }
    if (anykey && anykey()) fv_kick_n(3);
    static int was_scene = -2, was_busy = -1;                         /* a new scene / the end of a load: its views are due */
    int sc = mf ? scene() : -1, bz = mf ? busy() : 1;
    if (sc != was_scene || (was_busy == 1 && !bz)) for (int i = 0; i < FV_SITES; i++) fv_miss[i].next = 0;
    was_scene = sc; was_busy = bz;
}
static Obj find_view_lookup(uintptr_t *h, void **kcache, const char *ns, const char *name, bool inactive) {
    if (!*kcache) *kcache = class_by_name(ns, name);
    if (!*kcache) return NULL;
    Obj tobj = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(*kcache));   /* class -> Type object */
    double t0 = fv_clock();
    Obj v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(tobj, inactive, NULL);
    { double d = fv_clock() - t0; fv_ms_sum += d; fv_lookups++; if (d > fv_ms_max) fv_ms_max = d; fv_count(ns, name, d); }
    u_gc_free(*h); *h = u_alive(v) ? u_gc_new(v) : 0;
    return u_alive(v) ? v : NULL;
}
static Obj find_view(uintptr_t *h, void **k, const char *ns, const char *name) { return find_view_x(h, k, ns, name, false); }

/* ---------------------------------------------------------------- other lanes */
/* steamnet's mp_steam.h (in mp_game.c); mp_game_host / mp_game_join for direct TCP (and the test rig, which has no Steam). */
int ui_parse_address(const char *in, char *host, size_t host_cap, uint16_t *port, uint16_t def_port);   /* ui_model.c */

/* ---------------------------------------------------------------- state */
mpm_model M;
/* Custom Modes hooks (mpmenu_register_hooks, mpmenu_kit.h): the windows that were included here before the M4 split */
#define MAX_HOOKS 8
static mp_menu_hooks hooks[MAX_HOOKS]; static int nhooks;
static int host_failed, pending_click;
/* title view: TitleMenuView _continueButton +0x30 .. _quitButton +0x60, _loadCampaignButton +0x40, _creditButton +0x50 */
static uintptr_t h_title; static void *k_title;
uintptr_t h_tb[MPB_COUNT];                       /* title clones (SubUI_TitleMenuButton, _buttonName +0x130) */
static uintptr_t h_title_built;                         /* view the clones belong to */
static int stock_hidden; static unsigned stock_was; static uintptr_t h_stock_view; static void stock_set(Obj v, int hide);
static uintptr_t h_field, h_cs; static void *k_cs;           /* address field: clone of the new-campaign name field */
/* Esc menu: SystemMenuView _returnToGameButton +0x48 .. _quitToTitleSceneButton +0x78, _version DASText +0x80 */
static uintptr_t h_esc; static void *k_esc;
static uintptr_t h_eb[MPB_COUNT];                       /* Esc-menu clones (SubUI_SystemMenuButton, _buttonName +0x28) */
static uintptr_t h_etext, h_ebg, h_esc_built;
static uintptr_t h_etitle, h_ereason;                    /* lobby title (Cinzel), Esc "why Load is off" line (seed-line clone) */
static int ph_sibling = -1;                              /* the Esc divider (PlaceHolder) index before the lobby moved it */
static int layout_done, lobby_boxed;
static char layout_log[512];
static int esc_lobby_mode;
/* ---------------------------------------------------------------- layout v2 kit (research/ux_layout_v2.md) */
/* Methods by name (il2cpp_class_get_method_from_name 0x6F8C1C; call through MethodInfo->methodPointer), stock assets by
 * name over everything loaded (Resources.FindObjectsOfTypeAll), and small builders for images / texts / rows. Sizes are
 * the canvas's own units (1663x1080 reference). */
static void *mth(const char *ns, const char *cls, const char *name, int argc) {
    void *k = class_by_name(ns, cls);
    void *mi = k ? F(0x6F8C1C, void *(*)(void *, const char *, int))(k, name, argc) : NULL;
    if (!mi) rig_log("mpmenu: method %s.%s.%s/%d not found", ns, cls, name, argc);
    return mi;
}
#define MCALL(mi, R, ...) ((R (*)(__VA_ARGS__))(*(void **)(mi)))
static Obj type_obj(const char *ns, const char *cls) { void *k = class_by_name(ns, cls); return k ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k)) : NULL; }
static Obj comp_of(Obj any, const char *ns, const char *cls) {             /* GetComponent(typeof(ns.cls)) on any component */
    Obj ty = type_obj(ns, cls);
    return ty && u_alive(any) ? (mm_is(any, K_COMP) ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(any, ty, NULL) : NULL) : NULL;
}
/* stock assets by name: sprites of the v2 spec, the Cinzel font. Scanned once, again (assets_scan) while a needed one is missing */
static const char *spr_name[S_N] = { "topLineButton_Frame1", "ITEM_CogWheel", "ListToggle_On", "ListToggle_Off", "ArrrowIcon_Previous",
    "ArrrowIcon_Next", "ToggleButton_Normal", "ToggleButton_Selected", "RecruitButton", "BottomButton", "Box", "Devider_Detail2",
    "Icon_Devider", "Slot1", "Slot2", "Button_Normal", "CheckBox_Normal", "CheckBox_Checked" };
static uintptr_t h_spr[S_N], h_cinzel, h_cog_built;      /* h_cog_built: the runtime cog (cog_built), not the stock one */
int64_t frames;
static Obj popup_prefab(int type);
static void cog_from_bundles(void);
static int cog_built(void);
void assets_scan_now(void);
static int64_t scan_last = -100000;
int scan_done; static int scans;
/* fpsfix (2026-10-20): one scan walks every loaded sprite (16 000+ in a world; ~5 ms on the title, longer in a world)
   and ran every 300 frames up to 30 times, in the world too (ITEM_CogWheel is often not loaded: it kept the scans
   going). Now: the cog is optional (a pixel cog stands in; title_build / lw_build still look for it), and the timer
   scans at most 3 times per scene, the first 60 frames after the scene settles, then every 600 frames while a needed
   sprite is missing; never during a load. */
static void assets_scan(void) {
    static int at_scene = -1, n_here; static int64_t since;
    int sc = scene();
    if (sc != at_scene) { at_scene = sc; n_here = 0; since = frames; }
    if (busy()) { since = frames; return; }
    if (scan_done || n_here >= 3 || frames - (n_here ? scan_last : since) < (n_here ? 600 : 60)) return;
    n_here++; assets_scan_now();
}
static double scan_ms_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
static double scan_ms_sum, scan_ms_max, pscan_ms_sum, pscan_ms_max; static int pscans;
const char *mpmenu_scan_stats(void) {
    static char o[300];
    snprintf(o, sizeof o, "{\"assets\":[%d,%.1f,%.1f],\"portrait\":[%d,%.1f,%.1f],\"done\":%d,\"view_lookups\":[%ld,%.1f,%.1f],\"kicks\":[%ld,%ld]}", scans, scan_ms_sum, scan_ms_max, pscans, pscan_ms_sum, pscan_ms_max, scan_done, fv_lookups, fv_ms_sum, fv_ms_max, fv_kicks[0], fv_kicks[1]);
    return o;
}
static void assets_scan_body(void);
void assets_scan_now(void) {
    double t0 = scan_ms_now(); assets_scan_body(); double d = scan_ms_now() - t0;
    scan_ms_sum += d; if (d > scan_ms_max) scan_ms_max = d;
}
/* CheckBox_Normal comes in 5 x 5 (Settings, see-through corners: a round ring when drawn big), 10 x 10 and 12 x 12: the
   load-list box takes the 10 x 10 one, the same square as CheckBox_Checked (owner 2026-10-20: square boxes) */
static int chk_size_ok(int k, Obj sp) {
    if (k != S_CHK_N && k != S_CHK_C) return 1;
    static void *mr; if (!mr) mr = mth("UnityEngine", "Sprite", "get_rect", 0);
    if (!mr) return 1;
    RectF2 r = MCALL(mr, RectF2, Obj, const void *)(sp, mr);
    return r.w > 9.5f && r.w < 10.5f && r.h > 9.5f && r.h < 10.5f;
}
static void assets_scan_body(void) {
    scan_last = frames; scans++;
    popup_prefab(8); popup_prefab(2); popup_prefab(13);                     /* load the Recruit / Expedition / Task Planner prefabs: their sprites too */
    static void *mi; if (!mi) mi = mth("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
    if (!mi) return;
    int miss = 0, found = 0;
    Obj arr = MCALL(mi, Obj, Obj, const void *)(type_obj("UnityEngine", "Sprite"), mi);
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
    for (int i = 0; i < n; i++) {
        Obj sp = AT((char *)arr + 0x20, i * 8, Obj); char nm[64] = "";
        str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sp, NULL), nm, sizeof nm);
        for (int k = 0; k < S_N; k++) if (!h_spr[k] && !strcmp(nm, spr_name[k]) && chk_size_ok(k, sp)) { h_spr[k] = u_gc_new(sp); found++; }
    }
    if (!h_cinzel) {
        Obj fa = MCALL(mi, Obj, Obj, const void *)(type_obj("TMPro", "TMP_FontAsset"), mi);
        int nf = fa ? (int)AT(fa, 0x18, uintptr_t) : 0;
        for (int i = 0; i < nf && !h_cinzel; i++) {
            Obj f = AT((char *)fa + 0x20, i * 8, Obj); char nm[64] = "";
            str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(f, NULL), nm, sizeof nm);
            if (!strcmp(nm, "Cinzel-Bold SDF")) h_cinzel = u_gc_new(f);
        }
    }
    if (!h_spr[S_COG]) cog_from_bundles();
    char missing[256] = ""; size_t o = 0;
    for (int k = 0; k < S_N; k++) if (!h_spr[k]) { miss += k != S_COG; o += (size_t)snprintf(missing + o, sizeof missing - (o < sizeof missing ? o : sizeof missing), " %s", spr_name[k]); }   /* the cog: optional (pixel cog) */
    if (!h_cinzel) miss++;
    rig_log("mpmenu: assets scan (%d sprites loaded): %d new, missing%s%s", n, found, missing[0] ? missing : " none", h_cinzel ? "" : " Cinzel");
    if (!miss) scan_done = 1;
}
static Obj spr(int k) {
    Obj o = u_gc_get(h_spr[k]);
    if (o && !u_alive(o)) { u_gc_free(h_spr[k]); h_spr[k] = 0; scan_done = 0; o = NULL; }   /* unloaded with a scene: find it again */
    return o;
}
/* ITEM_CogWheel built at run time from its own 16x16 PNG (copied from the game's icon bundle, research/ux_shots/design/
 * sprites/ITEM_CogWheel.png), for screens where the stock sprite is not loaded yet (title screen). new Texture2D(16, 16,
 * RGBA32, no mips) + ImageConversion.LoadImage, filterMode Point, Sprite.Create (pivot centre, 100 px per unit). */
static const unsigned char cog_png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0xf3, 0xff, 0x61, 0x00, 0x00, 0x00, 0xd1, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0xa5, 0x93, 0x31, 0x0e, 0x83, 0x30, 0x0c, 0x45, 0x5f, 0xaa, 0x1e, 0xa4, 0xaa, 0xd4, 0x85, 0x95, 0x81, 0x2b, 0x20, 0x6e, 0xc0, 0x3d, 0xb2, 0x23, 0xf6, 0xdc, 0x83, 0x1b, 0x20, 0xae, 0xd0, 0xa1, 0x6b, 0xaf, 0x93, 0x0e, 0xc5, 0xa9, 0x71, 0xd2, 0x52, 0x09, 0x4b, 0x11, 0x20, 0xfb, 0xdb, 0xff, 0x7f, 0x13, 0x77, 0xbd, 0xdc, 0x38, 0x12, 0xa7, 0x43, 0x68, 0xe0, 0xbc, 0x93, 0x8f, 0xea, 0xdd, 0xfd, 0xc3, 0x20, 0xaa, 0x03, 0x40, 0x18, 0xfc, 0xcf, 0x7c, 0x26, 0x21, 0x0c, 0x5e, 0x40, 0xb1, 0x6f, 0x3b, 0x00, 0xd6, 0x67, 0x54, 0xb9, 0x0f, 0x2d, 0x63, 0x62, 0xec, 0xdb, 0x8e, 0xba, 0xa9, 0xca, 0x82, 0x80, 0xc7, 0xfd, 0xc9, 0xb4, 0xcc, 0x49, 0x92, 0x65, 0xe0, 0xd6, 0x64, 0x2a, 0x96, 0x23, 0xa1, 0xc1, 0xb0, 0x35, 0x31, 0x2a, 0xba, 0x7a, 0x52, 0x0a, 0xc5, 0x4c, 0x3c, 0x70, 0x9b, 0x2d, 0x58, 0x7d, 0x52, 0x44, 0xc1, 0x54, 0x3f, 0x06, 0x32, 0x09, 0x7e, 0x0c, 0x1b, 0xba, 0x66, 0x5a, 0x62, 0x26, 0x60, 0x2b, 0xc1, 0x01, 0x4c, 0xcb, 0x1c, 0xeb, 0xa6, 0xca, 0x8c, 0x94, 0x6f, 0xeb, 0x41, 0x71, 0x0b, 0x1a, 0x60, 0xa7, 0xdb, 0x26, 0x5f, 0xff, 0x44, 0xa1, 0xd9, 0xb7, 0x5d, 0x32, 0x53, 0x9a, 0xeb, 0xc8, 0x1a, 0x28, 0xe7, 0x37, 0xe6, 0xf1, 0x5e, 0x71, 0xb4, 0xf5, 0x56, 0x82, 0x8d, 0xdd, 0xbb, 0xb0, 0x77, 0x99, 0x8a, 0x20, 0x1d, 0x2f, 0x27, 0x01, 0x57, 0x55, 0x1a, 0x6c, 0x3c, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
static int cog_built(void) {
    static void *mload, *mfilter, *mcreate; static int tried;
    if (!mload && !tried) {
        tried = 1;
        mload = mth("UnityEngine", "ImageConversion", "LoadImage", 2);
        mfilter = mth("UnityEngine", "Texture", "set_filterMode", 1);
        mcreate = mth("UnityEngine", "Sprite", "Create", 4);
    }
    if (!mload || !mcreate) return 0;
    void *kb = class_by_name("System", "Byte");
    Obj bytes = kb ? F(0x6F8BA8, Obj (*)(void *, size_t))(kb, sizeof cog_png) : NULL;   /* il2cpp_array_new */
    if (!bytes) return 0;
    memcpy((char *)bytes + 0x20, cog_png, sizeof cog_png);
    Obj tex = obj_new(0x6655970);                                           /* Texture2D */
    F(0x50C4E5C, void (*)(Obj, int, int, int, bool, void *))(tex, 16, 16, 4, false, NULL);   /* (w, h, RGBA32, mipChain) */
    bool ok = MCALL(mload, bool, Obj, Obj, const void *)(tex, bytes, mload);
    if (mfilter) MCALL(mfilter, void, Obj, int, const void *)(tex, 0, mfilter);         /* Point: crisp pixel art */
    { static void *mapply; if (!mapply) mapply = mth("UnityEngine", "Texture2D", "Apply", 0);   /* upload to the GPU (LoadImage alone left it blank on screen) */
      if (mapply) MCALL(mapply, void, Obj, const void *)(tex, mapply); }
    Obj sp = ok ? MCALL(mcreate, Obj, Obj, RectF2, V2, float, const void *)(tex, (RectF2){ 0, 0, 16, 16 }, (V2){ 0.5f, 0.5f }, 100.0f, mcreate) : NULL;
    if (u_alive(sp)) { h_spr[S_COG] = u_gc_new(sp); u_gc_free(h_cog_built); h_cog_built = u_gc_new(sp); }
    rig_log("mpmenu: ITEM_CogWheel built from its PNG (%zu bytes): load %d, sprite %s", sizeof cog_png, ok, u_alive(sp) ? "ok" : "FAILED");
    return u_alive(sp);
}
/* ITEM_CogWheel when no loaded object uses it yet (title screen): ask every loaded AssetBundle for it by name */
static void cog_from_bundles(void) {
    static void *mall, *mload;
    if (!mall) { mall = mth("UnityEngine", "AssetBundle", "GetAllLoadedAssetBundles_Native", 0); mload = mth("UnityEngine", "AssetBundle", "LoadAsset", 2);
                 if (!mload) mload = mth("UnityEngine", "AssetBundle", "LoadAsset_Internal", 2); }
    if (!mall || !mload) return;
    Obj arr = MCALL(mall, Obj, const void *)(mall);
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
    Obj ty = type_obj("UnityEngine", "Sprite");
    for (int i = 0; i < n && !h_spr[S_COG]; i++) {
        Obj bu = AT((char *)arr + 0x20, i * 8, Obj);
        Obj sp = u_alive(bu) ? MCALL(mload, Obj, Obj, Obj, Obj, const void *)(bu, u_str("ITEM_CogWheel"), ty, mload) : NULL;
        if (u_alive(sp)) h_spr[S_COG] = u_gc_new(sp);
    }
    rig_log("mpmenu: ITEM_CogWheel from %d loaded asset bundles: %s", n, h_spr[S_COG] ? "found" : "not found");
}
/* a sprite that is not loaded yet: a plain stock-colour rect instead of Unity's white (the cog: nothing) */
static void img_default_material(Obj im) {               /* a runtime texture: the default UI material, not the template's */
    static void *mm; if (!mm) mm = mth("UnityEngine.UI", "Graphic", "set_material", 1);
    if (mm && mm_is(im, K_GR)) MCALL(mm, void, Obj, Obj, const void *)(im, NULL, mm);
}
/* The cog drawn from plain rects, pixel by pixel (73 runs, 3 colours, from ITEM_CogWheel.png), for screens where the
 * stock sprite is not loaded (title). A runtime Texture2D + Sprite.Create loaded fine (pixels read back) but drew nothing
 * on this UI (tested 2026-10-13), so the art is rebuilt from the UI's own plain rects, which always draw. */
static const struct { uint8_t x, y, w, c; } cog_runs[] = { {7,2,3,0}, {3,3,3,0}, {7,3,1,0}, {8,3,1,2}, {9,3,1,0}, {11,3,3,0}, {3,4,1,0}, {4,4,2,2}, {6,4,1,0}, {7,4,1,1}, {8,4,1,2}, {9,4,1,1}, {10,4,1,0}, {11,4,2,2}, {13,4,1,0}, {3,5,1,0}, {4,5,1,1}, {5,5,7,2}, {12,5,1,1}, {13,5,1,0}, {4,6,1,0}, {5,6,2,2}, {7,6,3,1}, {10,6,2,2}, {12,6,1,0}, {2,7,2,0}, {4,7,1,1}, {5,7,1,2}, {6,7,1,1}, {7,7,3,0}, {10,7,1,1}, {11,7,1,2}, {12,7,3,0}, {2,8,1,0}, {3,8,3,2}, {6,8,1,1}, {7,8,1,0}, {9,8,1,0}, {10,8,1,1}, {11,8,3,2}, {14,8,1,0}, {2,9,2,0}, {4,9,1,1}, {5,9,1,2}, {6,9,1,1}, {7,9,3,0}, {10,9,1,1}, {11,9,1,2}, {12,9,1,1}, {13,9,2,0}, {4,10,1,0}, {5,10,2,2}, {7,10,3,1}, {10,10,2,2}, {12,10,1,0}, {3,11,1,0}, {4,11,2,1}, {6,11,5,2}, {11,11,2,1}, {13,11,1,0}, {3,12,1,0}, {4,12,2,1}, {6,12,2,0}, {8,12,1,1}, {9,12,2,0}, {11,12,2,1}, {13,12,1,0}, {3,13,3,0}, {7,13,1,0}, {8,13,1,1}, {9,13,1,0}, {11,13,3,0}, {7,14,3,0} };
static const Col cog_cols[] = { {0.133f,0.125f,0.145f,1.000f}, {0.482f,0.408f,0.435f,1.000f}, {0.682f,0.624f,0.600f,1.000f} };
Obj mk_img(Obj tpl_img, Obj parent_tr, int sprite, Col c, float x, float y, float w, float h);
static Obj mk_pixel_cog(Obj tpl_img, Obj parent_tr, float x, float y, float size) {
    Obj box = mk_img(tpl_img, parent_tr, -1, (Col){ 0, 0, 0, 0 }, x, y, size, size);
    if (!box) return NULL;
    float px = size / 16;
    for (size_t i = 0; i < sizeof cog_runs / sizeof cog_runs[0]; i++)
        mk_img(tpl_img, u_tr(box), -1, cog_cols[cog_runs[i].c], (cog_runs[i].x + cog_runs[i].w / 2.0f - 8) * px, (8 - cog_runs[i].y - 0.5f) * px,
               cog_runs[i].w * px, px);
    return box;
}
static Col spr_fallback(int k) {
    switch (k) {
    case S_BOX: return (Col){ 0.133f, 0.125f, 0.145f, 1 };
    case S_RECRUIT: return (Col){ 0.353f, 0.063f, 0.051f, 1 };
    case S_COG: case S_PREV: case S_NEXT: case S_TOG_ON: case S_TOG_OFF: return (Col){ 0, 0, 0, 0 };
    default: return (Col){ 0.165f, 0.149f, 0.173f, 1 };                  /* #2a262c: buttons, frames */
    }
}
/* RectTransform: centre anchors + pivot, size, position from the parent's centre (y up) */
void place(Obj comp, float x, float y, float w, float h) {
    Obj rt = u_tr(comp); if (!mm_is(rt, K_RT)) return;
    F(0x5106B64, void (*)(Obj, V2, void *))(rt, (V2){ 0.5f, 0.5f }, NULL);  /* anchorMin */
    F(0x5106D48, void (*)(Obj, V2, void *))(rt, (V2){ 0.5f, 0.5f }, NULL);  /* anchorMax */
    F(0x51072F4, void (*)(Obj, V2, void *))(rt, (V2){ 0.5f, 0.5f }, NULL);  /* pivot */
    F(0x5107110, void (*)(Obj, V2, void *))(rt, (V2){ w, h }, NULL);        /* sizeDelta */
    F(0x5106F2C, void (*)(Obj, V2, void *))(rt, (V2){ x, y }, NULL);        /* anchoredPosition */
}
static void stretch(Obj comp) {                                             /* fill the parent */
    Obj rt = u_tr(comp); if (!mm_is(rt, K_RT)) return;
    F(0x5106B64, void (*)(Obj, V2, void *))(rt, (V2){ 0, 0 }, NULL); F(0x5106D48, void (*)(Obj, V2, void *))(rt, (V2){ 1, 1 }, NULL);
    F(0x51072F4, void (*)(Obj, V2, void *))(rt, (V2){ 0.5f, 0.5f }, NULL);
    F(0x5107110, void (*)(Obj, V2, void *))(rt, (V2){ 0, 0 }, NULL); F(0x5106F2C, void (*)(Obj, V2, void *))(rt, (V2){ 0, 0 }, NULL);
}
static void row_height(Obj comp, float w, float h) {                       /* a title-column row: size + its LayoutElement */
    Obj rt = u_tr(comp);
    if (mm_is(rt, K_RT)) F(0x5107110, void (*)(Obj, V2, void *))(rt, (V2){ w, h }, NULL);
    Obj le = comp_of(comp, "UnityEngine.UI", "LayoutElement");
    static void *mp, *mm2;
    if (u_alive(le)) {
        if (!mp) { mp = mth("UnityEngine.UI", "LayoutElement", "set_preferredHeight", 1); mm2 = mth("UnityEngine.UI", "LayoutElement", "set_minHeight", 1); }
        if (mp) MCALL(mp, void, Obj, float, const void *)(le, h, mp);
        if (mm2) MCALL(mm2, void, Obj, float, const void *)(le, h, mm2);
    }
}
static void img_default_material(Obj im);
void img_set(Obj im, Obj sprite, Col c) {                          /* sprite (sliced) + tint; never a click target */
    if (!mm_is(im, K_IMG)) return;
    if (sprite && sprite == u_gc_get(h_cog_built)) img_default_material(im);   /* runtime sprite: default UI material */
    static void *mt; if (!mt) mt = mth("UnityEngine.UI", "Image", "set_type", 1);
    F(0x5257600, void (*)(Obj, Obj, void *))(im, sprite, NULL);
    if (mt) MCALL(mt, void, Obj, int, const void *)(im, sprite ? 1 : 0, mt);   /* Sliced (plain rect: Simple) */
    F(0x52602F8, void (*)(Obj, Col, void *))(im, c, NULL);
    F(0x5260348, void (*)(Obj, bool, void *))(im, false, NULL);
}
/* Text look (owner 2026-10-24: "not the same as the rest of the menus"; it replaces the 2026-10-13 plain-material rule):
 * every text we make or restyle takes the game's look for its role, as measured in the game's own menus (title column,
 * Options, Load Campaign): body text, buttons, labels = the language's body font + its SoftOutline16 material
 * ("Lora-Regular SDF OutLine16"); section heads = the title font + its SoftOutline16 material; window titles (24 and up, as
 * the game's Options / Rename titles) and text fields = the font's own plain material. mpm_look_material() decides. */
static Obj tr_find(Obj t, const char *path);
/* ---- i18n: the game's language and its fonts (research/i18n_plan.md 2, 4). Refactor.Setting.LanguageSetting, by name (Mac
 * and Windows): GetCurrentLanguage() (static, polled each frame: lang_poll) and GetFont(FontStyleType) (EasyRead 0 = body,
 * VeryDecorative 2 = titles; the language's own fonts: Lora / Cinzel, or the zh-Hans, zh-Hant, ja ones). */
static void *ls_klass, *ls_lang_mi, *ls_font_mi, *ls_mat_mi; static int ls_tries; static int64_t ls_last_try = -100000;
static int ls_ready(void) {                            /* class + methods found, its statics allocated */
    if (!ls_klass && ls_tries < 10 && frames - ls_last_try >= 300) {
        ls_last_try = frames; ls_tries++;
        ls_klass = class_by_name("Refactor.Setting", "LanguageSetting");
        if (ls_klass) { ls_lang_mi = F(0x6F8C1C, void *(*)(void *, const char *, int))(ls_klass, "GetCurrentLanguage", 0);
                        ls_font_mi = F(0x6F8C1C, void *(*)(void *, const char *, int))(ls_klass, "GetFont", 1);
                        ls_mat_mi = F(0x6F8C1C, void *(*)(void *, const char *, int))(ls_klass, "GetMaterial", 2); }
        rig_log("mpmenu: i18n: LanguageSetting %s (GetCurrentLanguage %s, GetFont %s, GetMaterial %s)", ls_klass ? "found" : "NOT found",
                ls_lang_mi ? "ok" : "missing", ls_font_mi ? "ok" : "missing", ls_mat_mi ? "ok" : "missing");
    }
    return ls_klass && *(void **)((char *)ls_klass + 0xB8) != NULL;       /* Il2CppClass.static_fields */
}
static Obj font_material(Obj font) {                   /* the font asset's own default material (TMP_Asset.m_Material: no outline) */
    static void *mm; static int fo = -1;
    if (!mm) mm = mth("TMPro", "TMP_Asset", "get_material", 0);
    if (!mm && fo < 0) { void *ka = class_by_name("TMPro", "TMP_Asset"), *fld = ka ? F(0x6F8C14, void *(*)(void *, const char *))(ka, "m_Material") : NULL;
                          fo = fld ? (int)F(0x6F949C, size_t (*)(void *))(fld) : 0; }
    Obj m = !u_alive(font) ? NULL : mm ? MCALL(mm, Obj, Obj, const void *)(font, mm) : fo > 0 ? AT(font, fo, Obj) : NULL;
    return u_alive(m) ? m : NULL;
}
static uintptr_t h_lfont[3]; static unsigned lfont_gen = ~0u;
static Obj lang_font(int style) {                      /* 0 body, 2 titles; NULL: not available (then the old sources) */
    if (lfont_gen != dsi_generation()) { for (int i = 0; i < 3; i++) { u_gc_free(h_lfont[i]); h_lfont[i] = 0; } lfont_gen = dsi_generation(); }
    Obj f = u_gc_get(h_lfont[style]);
    if (u_alive(f)) return f;
    if (!ls_ready() || !ls_font_mi) return NULL;
    if (!*(void **)*(void **)((char *)ls_klass + 0xB8)) return NULL;      /* static _fontSO (+0x0): not loaded by the game yet */
    f = MCALL(ls_font_mi, Obj, int32_t, const void *)(style, ls_font_mi);
    if (!u_alive(f)) return NULL;
    u_gc_free(h_lfont[style]); h_lfont[style] = u_gc_new(f);
    char nm[96] = ""; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(f, NULL), nm, sizeof nm);
    rig_log("mpmenu: i18n: %s font for %s: %s", style ? "title" : "body", dsi_code(), nm);
    return f;
}
static Obj title_font(void) { Obj f = lang_font(2); if (!f) f = u_gc_get(h_cinzel); return u_alive(f) ? f : NULL; }   /* the scan's Cinzel: fallback */
/* the SoftOutline16 material of the language's body (title 0) or title (title 1) font: LanguageSetting.GetMaterial(
 * FontStyleType, FontMaterialType.SoftOutline16); NULL: not available (then the plain material) */
static uintptr_t h_lmat[2]; static unsigned lmat_gen = ~0u;
static Obj lang_mat16(int title) {
    if (lmat_gen != dsi_generation()) { for (int i = 0; i < 2; i++) { u_gc_free(h_lmat[i]); h_lmat[i] = 0; } lmat_gen = dsi_generation(); }
    Obj m = u_gc_get(h_lmat[title]);
    if (u_alive(m)) return m;
    if (!ls_ready() || !ls_mat_mi || !*(void **)*(void **)((char *)ls_klass + 0xB8)) return NULL;   /* _fontSO not loaded yet */
    m = MCALL(ls_mat_mi, Obj, int32_t, int32_t, const void *)(title ? 2 : 0, 1, ls_mat_mi);
    if (!u_alive(m)) return NULL;
    u_gc_free(h_lmat[title]); h_lmat[title] = u_gc_new(m);
    char nm[96] = ""; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(m, NULL), nm, sizeof nm);
    rig_log("mpmenu: look: %s text material for %s: %s", title ? "title" : "body", dsi_code(), nm);
    return m;
}
/* the game's material for a text in font f at size (head: a section head, whatever its size): the language's body font
 * -> its SoftOutline16; its title font -> SoftOutline16 for heads and under 24, the plain material for window titles (24
 * and up); any other font (chat fallback faces) -> its own plain material (the game has no preset for it) */
Obj mpm_look_material(Obj f, float size, int head) {
    if (!u_alive(f)) return NULL;
    Obj m = NULL;
    if (f == lang_font(0)) m = lang_mat16(0);
    else if (f == title_font() && (head || size < 24)) m = lang_mat16(1);
    return m ? m : font_material(f);
}
static void txt_mat(Obj t, Obj m) {                                         /* TMP_Text.fontSharedMaterial */
    static void *ms; if (!ms) ms = mth("TMPro", "TMP_Text", "set_fontSharedMaterial", 1);
    if (u_alive(m) && ms && mm_is(t, K_TMP)) MCALL(ms, void, Obj, Obj, const void *)(t, m, ms);
}
/* Body font = the language's EasyRead font (GetFont 0); fallback the stock window body text, CommonNoticeView
 * Panel_Notice/Txt_Notice (Lora 16). body_material() = that font's own plain material (text fields); body texts take
 * mpm_look_material (SoftOutline16). Re-fetched when the language changes. */
static uintptr_t h_body_mat, h_body_font; static unsigned body_gen = ~0u;
static Obj body_material(void) {
    Obj m = u_gc_get(h_body_mat);
    if (u_alive(m) && body_gen == dsi_generation()) return m;
    u_gc_free(h_body_mat); u_gc_free(h_body_font); h_body_mat = h_body_font = 0;
    Obj font = lang_font(0); const char *how = "LanguageSetting.GetFont(EasyRead)";
    if (!font) {
        static uintptr_t hv; static void *kv;
        Obj cn = find_view_x(&hv, &kv, "Refactor.UI", "CommonNoticeView", true);
        Obj nt = cn ? tr_find(u_tr(cn), "Panel_Notice/Txt_Notice") : NULL;
        static void *k; if (!k) k = class_by_name("TMPro", "TMP_Text");
        Obj ty = k ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k)) : NULL;
        Obj t = nt && ty ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(nt, ty, NULL) : NULL;
        font = u_alive(t) ? F(0x4F0A59C, Obj (*)(Obj, void *))(t, NULL) : NULL; how = "CommonNoticeView Txt_Notice";
    }
    m = font_material(font);
    if (m) { h_body_mat = u_gc_new(m); h_body_font = u_gc_new(font); }
    if (body_gen != dsi_generation()) { body_gen = dsi_generation(); char nm[96] = ""; if (m) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(m, NULL), nm, sizeof nm);
                                        rig_log("mpmenu: body text (%s): font from %s, plain material (fields) \"%s\"", dsi_code(), m ? how : "NOT found", nm); }
    return m;
}
static void txt_body_as(Obj t, int field) {
    if (!mm_is(t, K_TMP)) return;
    Obj m = body_material(); if (!m) return;
    Obj f = u_gc_get(h_body_font); if (u_alive(f)) F(0x4F0A5A4, void (*)(Obj, Obj, void *))(t, f, NULL);
    Obj g = field || !u_alive(f) ? NULL : mpm_look_material(f, 0, 0);
    txt_mat(t, g ? g : m);
}
static void txt_body(Obj t) { txt_body_as(t, 0); }                         /* the language's body font, the game's body look */
static void txt_field(Obj t) { txt_body_as(t, 1); }                        /* a text field: the plain material (as the game's) */
static int lang_cjk(void) { const char *c = dsi_code(); return c && (!strncmp(c, "ja", 2) || !strncmp(c, "zh", 2) || !strncmp(c, "ko", 2)); }   /* see fit_min */
void txt_style(Obj t, int cinzel, float size, int align, int wrap) {
    if (!mm_is(t, K_TMP)) return;
    if (size < 14 && lang_cjk()) size = 14;                                 /* CJK floor (fit_min) */
    Obj f = cinzel ? title_font() : NULL;                                  /* titles: the language's title font (Cinzel in Latin languages) */
    if (u_alive(f)) { F(0x4F0A5A4, void (*)(Obj, Obj, void *))(t, f, NULL); txt_mat(t, mpm_look_material(f, size, 0)); }   /* head / window title */
    F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, false, NULL);
    F(0x4F0B340, void (*)(Obj, float, void *))(t, size, NULL);
    F(0x4F0B7BC, void (*)(Obj, int, void *))(t, align, NULL);              /* 513 middle-left, 514 centre, 516 middle-right */
    F(0x4F0BA48, void (*)(Obj, int, void *))(t, wrap, NULL);               /* 0 no wrap, 1 wrap */
    F(0x4F0BB74, void (*)(Obj, int, void *))(t, 0, NULL);
    F(0x4F0C254, void (*)(Obj, bool, void *))(t, true, NULL);
    F(0x5260348, void (*)(Obj, bool, void *))(t, false, NULL);             /* raycastTarget off */
    fit_style(t, size, wrap);                                               /* too long for its rect: auto size (text fit) */
}
void txt_color(Obj t, Col c) { if (mm_is(t, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(t, c, NULL); }
/* ---------------------------------------------------------------- text fit (ext/i18n/CJK_FIT.md)
 * How the game fits text: short labels and buttons in a fixed box use TMP auto size (min about 0.75 x max, small buttons
 * down to 8-18); long text keeps its size and its box grows (ContentSizeFitter, preferred height). It never changes the
 * size per language. Here: every text styled by txt_style keeps its design size. At the end of the menu frame, a text
 * whose content or rect changed is measured at that size (TMP preferredWidth / preferredHeight). It fits: as before,
 * no auto size (so English that fits looks exactly as before). It does not fit: auto size from the design size down
 * to fit_min() (0.75 x, the game's), and a one-line label wraps when its rect holds two lines. Still too long at
 * 0.75: the game's way for long text, the text keeps its design size and wraps to more lines (the line grows; it
 * draws past its rect, centred); a box we own asks txt_fit_h() for the height its text needs and grows. */
#define FIT_N 2048                                                           /* open addressing by object address */
typedef struct { Obj t; uintptr_t h; float size, w, h_, qw, qh, qr, qs; uint32_t hash, qhash; uint8_t wrap, on, dirty, multi, stock, qk, ell; } fit_e;   /* q*: txt_fit_h / _w cache */
typedef struct { float x, y, w, h; } fit_rect;
static fit_e fit_tab[FIT_N];
static int fit_count;
static void *m_fit_pw, *m_fit_ph, *m_fit_rect, *m_fit_live, *m_fit_pv;
/* ja / zh / ko (UX pass 2026-10-21, lead's rule): CJK below ~14 at the design size is unreadable at 960 x 540 (kanji become
   blobs), so the fit never shrinks them below 14 (it wraps or the box grows instead) and txt_style draws them at 14 at least */
static float fit_min(float size) {                                           /* the game's 0.75 */
    float m = size * 0.75f; m = (float)(int)(m + 0.5f); m = m < 8 ? (size < 8 ? size : 8) : m;
    if (lang_cjk() && m < 14) m = size < 14 ? size : 14;
    else if (m < 12) m = size < 12 ? size : 12;                                /* Latin / Cyrillic: never below 12 (~5 px at 960 x 540) */
    return m;
}
static int fit_methods(void) {
    if (!m_fit_pw) { m_fit_pw = mth("TMPro", "TMP_Text", "get_preferredWidth", 0); m_fit_ph = mth("TMPro", "TMP_Text", "get_preferredHeight", 0);
                     m_fit_rect = mth("UnityEngine", "RectTransform", "get_rect", 0); m_fit_live = mth("UnityEngine", "GameObject", "get_activeInHierarchy", 0);
                     m_fit_pv = mth("TMPro", "TMP_Text", "GetPreferredValues", 2); }   /* (float width, float height): the only 2-argument one */
    return m_fit_pw && m_fit_ph && m_fit_rect && m_fit_live;
}
typedef struct { float x, y; } fit_v2;
/* the text as styled at this size (no auto size), wrap on/off; its preferred width / height at width w (<= 0: its rect) */
static void fit_set(Obj t, int autosize, float size, int wrap) {
    F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, autosize != 0, NULL); F(0x4F0B340, void (*)(Obj, float, void *))(t, size, NULL);
    F(0x4F0BA48, void (*)(Obj, int, void *))(t, wrap, NULL);
}
static fit_v2 fit_pref(Obj t, float w) {
    if (w > 0 && m_fit_pv) return MCALL(m_fit_pv, fit_v2, Obj, float, float, const void *)(t, w, 100000.f, m_fit_pv);
    return (fit_v2){ MCALL(m_fit_pw, float, Obj, const void *)(t, m_fit_pw), MCALL(m_fit_ph, float, Obj, const void *)(t, m_fit_ph) };
}
static fit_e *fit_find(Obj t, int add) {
    if (!t) return NULL;
    size_t i = ((uintptr_t)t >> 4) * 2654435761u % FIT_N, tomb = (size_t)-1;
    for (int n = 0; n < FIT_N; n++, i = (i + 1) % FIT_N) {
        fit_e *e = &fit_tab[i];
        if (e->t == t) return e;
        if (e->t == (Obj)1) { if (tomb == (size_t)-1) tomb = i; continue; }
        if (!e->t) {
            if (!add) return NULL;
            if (fit_count >= FIT_N * 3 / 4) return NULL;                     /* full: that text keeps the old way (no fit) */
            if (tomb != (size_t)-1) e = &fit_tab[tomb];
            memset(e, 0, sizeof *e); e->t = t; e->h = u_gc_new(t); fit_count++;
            return e;
        }
    }
    return NULL;
}
static void fit_drop(fit_e *e) { u_gc_free(e->h); memset(e, 0, sizeof *e); e->t = (Obj)1; fit_count--; }
static uint32_t fit_hash(const char *s) { uint32_t h = 2166136261u; for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u; return h; }
/* txt_style: design size + wrap of this text; a text styled again the same way keeps its current fit (no flicker) */
static void fit_style(Obj t, float size, int wrap) {
    fit_e *e = fit_find(t, 1); if (!e) return;
    F(0x4F0B68C, void (*)(Obj, float, void *))(t, size, NULL);              /* fontSizeMax: the design size */
    F(0x4F0B634, void (*)(Obj, float, void *))(t, fit_min(size), NULL);     /* fontSizeMin */
    if (e->size == size && e->wrap == (wrap != 0) && e->on) {
        if (e->on == 4) { fit_set(t, 0, size, 1); return; }                 /* grows: design size, more lines */
        F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, true, NULL);
        if (e->on >= 2) F(0x4F0BA48, void (*)(Obj, int, void *))(t, e->on == 2, NULL);
        return;
    }
    e->size = size; e->wrap = wrap != 0; e->on = 0; e->dirty = 1;
}
static void fit_dirty(Obj t) { fit_e *e = fit_find(t, 0); if (e) e->dirty = 1; }
static void fit_text(Obj t, const char *s) {
    fit_e *e = fit_find(t, 0); if (!e) return;
    uint32_t h = fit_hash(s); if (h != e->hash) { e->hash = h; e->dirty = 1; e->multi = s && strchr(s, '\n') != NULL; }
}
/* does the text fit w x h at the smallest size (fit_min)? A one-line label may use two lines when h holds them (two).
   Leaves the text at that size; the caller sets it back. */
static int fit_at_min(fit_e *e, Obj t, float w, float h, int *two) {
    float mn = fit_min(e->size);
    *two = !e->wrap && !e->multi && h >= 2.7f * mn;
    fit_set(t, 0, mn, e->wrap || *two);
    if (e->wrap || *two) return fit_pref(t, w).y <= h + 2;
    return fit_pref(t, 0).x <= w + 1;
}
static void fit_one(fit_e *e, Obj t, float w, float h) {
    if (w < 4 || h < 4) return;                                              /* not placed yet */
    if (e->on) fit_set(t, 0, e->size, e->wrap);                              /* measure as styled */
    int over;
    if (!e->wrap) over = MCALL(m_fit_pw, float, Obj, const void *)(t, m_fit_pw) > w + 1;
    else over = MCALL(m_fit_ph, float, Obj, const void *)(t, m_fit_ph) > h + 2;   /* a second line in a one-line button runs out of its frame */
    e->on = 0;
    if (over) {
        float mn = fit_min(e->size); int two = 0;
        float pw1 = MCALL(m_fit_pw, float, Obj, const void *)(t, m_fit_pw);  /* one line at the design size (wrap off: no wrap width) */
        int fits = fit_at_min(e, t, w, h, &two);
        fit_set(t, 0, e->size, e->wrap);
        char tx[64] = ""; str_utf8(tmp_get(t), tx, sizeof tx);
        if (!fits && e->ell && !e->wrap) {                                  /* a one-line text (txt_one_line): 0.75, one line, "..." */
            e->on = 5; fit_set(t, 0, mn, 0); F(0x4F0BB74, void (*)(Obj, int, void *))(t, 1, NULL);   /* overflowMode Ellipsis */
            rig_log("mpmenu: text fit: \"%s\" is too long for %.0fx%.0f even at %.0f: one line at %.0f, ends in ...", tx, w, h, mn, mn);
            return;
        }
        if (!fits) {                                                         /* the game's long text: design size, more lines */
            e->on = 4; fit_set(t, 0, e->size, 1);
            rig_log("mpmenu: text fit: \"%s\" is too long for %.0fx%.0f even at %.0f: size %.0f, wraps (%.0f high)", tx, w, h, mn, e->size, fit_pref(t, w).y);
            return;
        }
        e->on = two ? 2 : 1;                                                 /* 2: a one-line label that may use two lines */
        /* 3: a wrapping text in a box made for one line (a button) that fits one line at the smaller size: one line, as
           the game's buttons (a second line ran over the frame of Host from a save) */
        if (e->wrap && !e->multi && h < 2.56f * e->size && pw1 * mn / e->size <= w) e->on = 3;
        F(0x4F0B634, void (*)(Obj, float, void *))(t, mn, NULL);
        F(0x4F0B68C, void (*)(Obj, float, void *))(t, e->size, NULL);
        if (e->on >= 2) F(0x4F0BA48, void (*)(Obj, int, void *))(t, e->on == 2, NULL);
        F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, true, NULL);
        rig_log("mpmenu: text fit: \"%s\" is too long for %.0fx%.0f: auto size %.0f-%.0f%s", tx, w, h, mn, e->size, e->on == 2 ? ", two lines" : e->on == 3 ? ", one line" : "");
    }
}
/* once per frame (end of mpmenu_frame): changed texts now; every text's rect every 15 frames (a later layout change) */
static void fit_frame(void) {
    if (!fit_count) return;
    if (!fit_methods()) return;
    int all = frames % 15 == 0;
    for (int i = 0; i < FIT_N; i++) {
        fit_e *e = &fit_tab[i];
        if (!e->t || e->t == (Obj)1 || (!e->dirty && !all)) continue;
        Obj t = u_gc_get(e->h);
        if (!u_alive(t)) { fit_drop(e); continue; }
        Obj g = u_go(t);
        if (!g || !MCALL(m_fit_live, bool, Obj, const void *)(g, m_fit_live)) {                         /* measured once it shows */
            if (e->stock) {                                                  /* a stock box closed: its own settings again */
                if (e->on) { F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, false, NULL); F(0x4F0B340, void (*)(Obj, float, void *))(t, e->size, NULL);
                             F(0x4F0BA48, void (*)(Obj, int, void *))(t, e->wrap, NULL); }
                fit_drop(e);
            }
            continue;
        }
        Obj rt = u_tr(t); if (!mm_is(rt, K_RT)) continue;
        fit_rect r = MCALL(m_fit_rect, fit_rect, Obj, const void *)(rt, m_fit_rect);
        if (!e->dirty && !e->stock && r.w == e->w && r.h == e->h_) continue;   /* a stock text: the game may set it too */
        int first = e->dirty; e->w = first ? -1 : r.w; e->h_ = r.h; e->dirty = 0;   /* -1: measured again at the next full pass
                                                                                    (TMP may see a new rect one frame late) */
        fit_one(e, t, r.w, r.h);
    }
}
/* for the game-side files (ui_game.c, ui_teams.c) and Custom Modes texts styled their own way: same fit */
void txt_fit(Obj t, float size, int wrap) { if (mm_is(t, K_TMP)) fit_style(t, size, wrap); }
/* a one-line text in a fixed box (after txt_style / mk_txt): too long even at 0.75 = one line at 0.75 that ends in "..."
   (the game's ellipsis), never more lines past its box (Bring Units cards: gear lines ran out of their card) */
void txt_one_line(Obj t) { fit_e *e = mm_is(t, K_TMP) ? fit_find(t, 1) : NULL; if (e) { e->ell = 1; e->dirty = 1; } }
void txt_fit_text(Obj t, const char *s) { fit_text(t, s); }
/* the fit's current settings again (after a measure) */
static void fit_apply(fit_e *e, Obj t) {
    if (!e->on || e->on == 4) { fit_set(t, 0, e->size, e->on == 4 ? 1 : e->wrap); return; }
    F(0x4F0BA48, void (*)(Obj, int, void *))(t, e->on == 2 ? 1 : e->on == 3 ? 0 : e->wrap, NULL);
    F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, true, NULL);
}
/* the height a box we own needs for text t laid out at w x h (its design box): h when the text fits there at its design
   size or auto-sized down to 0.75; else its height at the design size, wrapped at w (the game's long text: the box grows,
   the text keeps its size). Its caller grows the box (and moves what is stacked under it). Cached per text / box. */
float txt_fit_h(Obj t, float w, float h) {
    if (!mm_is(t, K_TMP) || w < 4 || h < 4 || !fit_methods()) return h;
    fit_e *e = fit_find(t, 0); if (!e || e->size < 1) return h;
    char tx[1024] = ""; str_utf8(tmp_get(t), tx, sizeof tx); uint32_t hs = fit_hash(tx);
    if (e->qr > 0 && e->qk == 1 && e->qhash == hs && e->qw == w && e->qh == h && e->qs == e->size) return e->qr;
    fit_set(t, 0, e->size, e->wrap);
    int over = !e->wrap ? fit_pref(t, 0).x > w + 1 : fit_pref(t, w).y > h + 2, two = 0;
    float r = h;
    if (over && !fit_at_min(e, t, w, h, &two)) { fit_set(t, 0, e->size, 1); float ph = fit_pref(t, w).y; if (ph > h) r = (float)(int)(ph + 0.99f); }
    fit_apply(e, t);
    e->qhash = hs; e->qw = w; e->qh = h; e->qr = r; e->qs = e->size; e->qk = 1;
    return r;
}
/* the same for a one-line box that can only grow wider (a chip): w when the text fits on one line at its design size or
   at 0.75, else its one-line width at the design size */
float txt_fit_w(Obj t, float w, float h) {
    if (!mm_is(t, K_TMP) || w < 4 || h < 4 || !fit_methods()) return w;
    fit_e *e = fit_find(t, 0); if (!e || e->size < 1) return w;
    char tx[512] = ""; str_utf8(tmp_get(t), tx, sizeof tx); uint32_t hs = fit_hash(tx);
    if (e->qr > 0 && e->qk == 2 && e->qhash == hs && e->qw == w && e->qh == h && e->qs == e->size) return e->qr;
    fit_set(t, 0, e->size, 0);
    float pw = fit_pref(t, 0).x, r = w;
    if (pw > w + 1 && pw * fit_min(e->size) / e->size > w + 1) r = (float)(int)(pw + 0.99f);
    fit_apply(e, t);
    e->qhash = hs; e->qw = w; e->qh = h; e->qr = r; e->qs = e->size; e->qk = 2;
    return r;
}
/* the height of text t at its design size, wrapped at width w (as the loader kit's DSLUi.text_height): for a box that
   grows to its text rather than auto-size it (0: not measured) */
float txt_text_h(Obj t, float w) {
    if (!mm_is(t, K_TMP) || w < 4 || !fit_methods()) return 0;
    fit_e *e = fit_find(t, 0); if (!e || e->size < 1) return 0;
    fit_set(t, 0, e->size, 1); float ph = fit_pref(t, w).y; fit_apply(e, t);
    return ph;
}
/* the stock notice box text (CommonNoticeView Txt_Notice: Lora 16 in a fixed 520 x 109 rect; the game never sizes it):
   while our text is up it auto-sizes when too long; when the box closes, its stock settings come back */
void txt_fit_stock(Obj t) {
    if (!mm_is(t, K_TMP)) return;
    fit_e *e = fit_find(t, 0);
    if (e) { e->dirty = 1; return; }
    if (F(0x4F0B5D4, bool (*)(Obj, void *))(t, NULL)) return;                /* the game auto-sizes it already */
    float fs = F(0x4F0B338, float (*)(Obj, void *))(t, NULL);
    if (fs < 4) return;
    fit_style(t, fs, 1);
    e = fit_find(t, 0); if (e) e->stock = 1;
}
/* new image / text under parent_tr, from a template (an Image without children, a TMP label) */
Obj mk_img(Obj tpl_img, Obj parent_tr, int sprite, Col c, float x, float y, float w, float h) {
    if (!u_alive(tpl_img) || !u_alive(parent_tr)) return NULL;
    Obj im = u_instantiate(tpl_img, parent_tr);
    if (!u_alive(im)) return NULL;
    u_active(im, true);
    img_set(im, sprite >= 0 ? spr(sprite) : NULL, sprite >= 0 && !spr(sprite) ? spr_fallback(sprite) : c);
    place(im, x, y, w, h);
    return im;
}
Obj mk_txt(Obj tpl_txt, Obj parent_tr, int cinzel, float size, int align, int wrap, float x, float y, float w, float h, Col c) {
    if (!u_alive(tpl_txt) || !u_alive(parent_tr)) return NULL;
    Obj t = u_instantiate(tpl_txt, parent_tr);
    if (!u_alive(t)) return NULL;
    u_active(t, true);
    if (!cinzel) txt_body(t);
    txt_style(t, cinzel, size, align, wrap); txt_color(t, c);
    place(t, x, y, w, h);
    return t;
}
/* sibling order on the title column (layout v2 §2a); the address field goes right after LABEL_ADDR, the name field
 * after LABEL_NAME. The selector arrows are children of their rows (arrow_ids), not column entries. */
static const int title_ids[] = { MPB_MULTIPLAYER, MPB_HEADER, MPB_LABEL_ADDR, MPB_LABEL_NAME, MPB_INFO,
                                 MPB_FRIEND0, MPB_FRIEND1, MPB_FRIEND2, MPB_FRIEND3, MPB_FRIEND4, MPB_FRIEND5, MPB_SETTINGS,
                                 MPB_DIVIDER, MPB_HOST, MPB_HOST_SAVE, MPB_HOST_NEW, MPB_HOST_FE, MPB_HOST_C1, MPB_HOST_C2, MPB_HOST_C3, MPB_JOIN_DIV, MPB_JOIN_HDR, MPB_JOIN_ADDR, MPB_JOIN_FRIEND, MPB_FE_RESLIST, MPB_LOAD_SAVE, MPB_HOST_START, MPB_NEW_CAMPAIGN, MPB_CONNECT, MPB_WATCH, MPB_CANCEL_JOIN, MPB_BACK };
/* the Rules panel's buttons (title: SubUI_TitleMenuButton clones registered in h_tb; lobby: in h_lwb) */
static const int arrow_ids[] = { MPB_PAUSE_PREV, MPB_PAUSE_NEXT, MPB_SPEED_PREV, MPB_SPEED_NEXT, MPB_GEAR_PREV, MPB_GEAR_NEXT,
                                 MPB_UNITS_PREV, MPB_UNITS_NEXT, MPB_POP_PREV, MPB_POP_NEXT, MPB_EVENTS_PREV, MPB_EVENTS_NEXT, MPB_ADMIN_PREV, MPB_ADMIN_NEXT,
                                 MPB_FOG_PREV, MPB_FOG_NEXT, MPB_SPECT_PREV, MPB_SPECT_NEXT, MPB_RULES_DONE };   /* 2 per rules row, in row order */
#define NA (int)(sizeof arrow_ids / sizeof arrow_ids[0])
#define HIST_MAX 12
typedef struct { char key[128], world[64], how[8], addr[96], host[48], players[240], units[200], portraits[400]; long long day, when; uint64_t steamid; } hist_t;
static hist_t hist[HIST_MAX]; static int nhist, hist_loaded;
static uintptr_t h_title_cog, h_title_more, h_title_sum, h_title_box, h_title_pcog;
/* host choice buttons: the small second line (mpm_choice_note) and the label's own place, to move it up over the line */
static uintptr_t h_cnote[MPM_CHOICES]; static V2 cnote_ap0[MPM_CHOICES]; static float cnote_x[MPM_CHOICES];
static mpm_choices hcl;                                                     /* (defined with the host choice code below) */
/* Recent worlds panel (mockup "Multiplayer page: recent worlds"): beside the column on the Multiplayer page */
static uintptr_t h_rw_root, h_rw_l1[6], h_rw_l2[6], h_rw_ul[6], h_rw_u[6], h_rw_note, h_rw_band, h_rw_line, h_rw_title;
static uintptr_t h_rw_pf[6][6], h_rw_pi[6][6], h_rw_pp[6][6], h_rw_pn[6][6];   /* + the unit's name beside its box */   /* "Your units": 30x30 frames (copper edge), initial, portrait image */
/* a portrait sprite by name, if loaded now (FindObjectsOfTypeAll once per title visit); NULL -> the initial */
Obj portrait_sprite(const char *name) {
    static struct { char n[64]; uintptr_t h; } cache[48]; static int ncache; static int64_t scanned = -1;
    if (!name[0]) return NULL;
    for (int i = 0; i < ncache; i++) if (!strcmp(cache[i].n, name)) { Obj o = u_gc_get(cache[i].h); return u_alive(o) ? o : NULL; }
    /* the game's own portrait table: UnitVisualDataManager._db (+0x20) -> UnitVisualDatabase.GetPortraitSprite(key, false)
       0xE51A58 (loads a portrait of a unit that is not in this world, e.g. one from another save) */
    static uintptr_t h_uvm; static void *k_uvm;
    Obj uvm = find_view_x(&h_uvm, &k_uvm, "Refactor", "UnitVisualDataManager", true), db = uvm ? AT(uvm, 0x20, Obj) : NULL;
    Obj got = db ? F(0xE51A58, Obj (*)(Obj, Obj, bool, void *))(db, u_str(name), false, NULL) : NULL;
    if (got && ncache < 48) { snprintf(cache[ncache].n, 64, "%s", name); cache[ncache++].h = u_gc_new(got); return got; }
    if (scanned >= 0 && frames - scanned < 600) return NULL;               /* not loaded: look again in ~20 s */
    scanned = frames;
    double t0 = scan_ms_now(); pscans++; Obj hit = NULL;
    static void *mi; if (!mi) mi = mth("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
    Obj arr = mi ? MCALL(mi, Obj, Obj, const void *)(type_obj("UnityEngine", "Sprite"), mi) : NULL;
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
    for (int i = 0; i < n && !hit; i++) {
        Obj sp = AT((char *)arr + 0x20, i * 8, Obj); char nm[64] = "";
        str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sp, NULL), nm, sizeof nm);
        if (!strcmp(nm, name) && ncache < 48) { snprintf(cache[ncache].n, 64, "%s", nm); cache[ncache++].h = u_gc_new(sp); hit = sp; }
    }
    double d = scan_ms_now() - t0; pscan_ms_sum += d; if (d > pscan_ms_max) pscan_ms_max = d;
    return hit;
}
static float rw_h;
static const int hist_ids[] = { MPB_HIST0, MPB_HIST1, MPB_HIST2, MPB_HIST3, MPB_HIST4, MPB_HIST5, MPB_HISTX0, MPB_HISTX1, MPB_HISTX2,
                                MPB_HISTX3, MPB_HISTX4, MPB_HISTX5, MPB_HIST_PREV, MPB_HIST_NEXT,
                                MPB_HISTW0, MPB_HISTW1, MPB_HISTW2, MPB_HISTW3, MPB_HISTW4, MPB_HISTW5 };   /* + Watch per row */   /* Rules box: cog, "›", summary, frame image */
typedef struct { uintptr_t root, val[MPM_RULE_ROWS], note, desc, row[MPM_RULE_ROWS]; float pw, ph; int tip_row; } rpanel;   /* the Rules panel (approved mockup "Host page B") */
static rpanel rp_title, rp_lobby;
/* Foreign Expedition rules panel (board FESettingsA "Rules"): rows with a line under each, shown instead of the Rules
   panel when the host page's World is Foreign Expedition. Row 4 (Gear) is fixed: no arrows. */
typedef struct { uintptr_t root, val[MPM_FE_ROWS], desc[MPM_FE_ROWS], row[MPM_FE_ROWS]; int tip_row; } fepanel;
static fepanel rp_fe;
static const int fe_arrows[MPM_FE_ROWS][2] = { { MPB_FED_PREV, MPB_FED_NEXT }, { MPB_FEX_PREV, MPB_FEX_NEXT }, { MPB_FEL_PREV, MPB_FEL_NEXT },
                                               { MPB_FEU_PREV, MPB_FEU_NEXT }, { 0, 0 }, { MPB_FER_PREV, MPB_FER_NEXT } };                  /* value texts of the 3 selector rows; the settings row's toggle image */
static const int esc_ids[] = { MPB_ESC_INVITE, MPB_LEAVE };   /* in game; the lobby has its own window (v2 §2b) */
#define NT (int)(sizeof title_ids / sizeof title_ids[0])
#define NE (int)(sizeof esc_ids / sizeof esc_ids[0])

Obj title_view(void) { return find_view(&h_title, &k_title, "Refactor.UI", "TitleMenuView"); }
Obj esc_view(void) { return find_view(&h_esc, &k_esc, "Refactor.UI", "SystemMenuView"); }
static Obj popups(void) {                               /* MainFlow._eventHandler -> CampaignLoadingEventHandler -> _popUpUIs */
    Obj eh = mf ? AT(mf, 0x50, Obj) : NULL, cl = eh ? AT(eh, 0x28, Obj) : NULL;
    return cl ? AT(cl, 0xF0, Obj) : NULL;
}
static bool popup_open(int type) { Obj p = popups(); return p && !strcmp(cls_name(p), "PopUpViewContainer") && F(0xF62D64, bool (*)(Obj, int, void *))(p, type, NULL); }
void popup_activate(int type) { Obj p = popups(); if (p && !strcmp(cls_name(p), "PopUpViewContainer")) F(0xF62554, void (*)(Obj, int, void *))(p, type, NULL); }

/* ---------------------------------------------------------------- observations */
static int rules_close_frames;                          /* frames after the lobby Rules closed: the closing Escape stays ours */
static int teams_close_frames;                          /* same after the teams lane's window closed */
static int teams_modal(void);
static int chat_typing, chat_close_frames, chat_scroll, chat_fade_s = 12, chat_show_join = 1, chat_on = 1;
static int chat_read;                                    /* the open box in its read state (owner 2026-10-25): no text field, the game keeps its keys */
#define CH_W0 470                                        /* the open chat box: default = smallest size (canvas units) */
#define CH_H0 270
static float chat_x = 0, chat_y = 0;                     /* offset of the box's bottom-left from the default place (canvas units), remembered */
static float chat_w = CH_W0, chat_h = CH_H0;             /* the open box's size (owner 2026-10-25: corner / bottom drag), remembered */
static int fields_edited, test_escape;                   /* test_escape: mpm_escape (tests: no keyboard in a hidden copy) */                               /* title_sync: a field's text changed (read in the next observe) */
static void modset_notice(void);
static void observe(mpm_obs *o) {
    memset(o, 0, sizeof *o);
    o->scene = scene();
    Obj c = cdc();
    o->campaign_ready = o->scene == 2 && c && AT(c, 0x10, int32_t) != -1 && !busy() && tick() > 0;
    Obj tv = o->scene == 1 ? title_view() : NULL;
    o->title_visible = tv && u_on(tv);
    /* SaveLoadUI or CreateCampaign: the title view stays active under the new-campaign screen, so without popup 7 a
       Cancel there left the menu in PICK_NEW and the next single-player load was hosted (Foctopus 2026-10-20) */
    o->picker_open = o->scene == 1 && (popup_open(5) || popup_open(7));
    { Obj dd = o->scene == 2 && c ? AT(c, 0x40, Obj) : NULL; o->tutorial = dd && AT(dd, 0x10, int32_t) == 7; }   /* CampaignDataContainer dungeon data: region 7 = Tutorial */
    o->loading = busy();
    static int (*load_pending)(void); static int lp_looked;                /* mpglue: an mp load / reload / retry runs */
    if (!lp_looked) { lp_looked = 1; load_pending = (int (*)(void))dlsym(RTLD_DEFAULT, "mp_game_load_pending"); }
    if (load_pending && load_pending()) o->loading = 1;                    /* never tear the session down during it */
    Obj ev = o->scene == 2 ? esc_view() : NULL;
    o->esc_open = ev && u_on(ev);
    { static int (*ar)(void); static int looked; if (!looked) { looked = 1; ar = (int (*)(void))dlsym(RTLD_DEFAULT, "mp_game_autorejoin"); }
      o->rejoining = ar && ar(); }
    mp_steam_info in; mp_steam_state(&in);
    if (in.state == MP_STEAM_ENDED) { o->failed = 1; snprintf(o->msg, sizeof o->msg, "%s", mp_steam_end_text()); o->end = mp_steam_end_code(); modset_notice(); }   /* Steam join refused for mods */
    else if (in.is_host) o->role = 1;
    else if (in.state == MP_STEAM_JOINING || in.state == MP_STEAM_IN_GAME) {
        o->role = 2;
        o->guest_loading = in.state == MP_STEAM_JOINING && in.player >= 0;           /* welcomed, waiting for / loading the save */
        o->host_window = in.phase == MP_PH_HOST_WINDOW;
    }
    if (o->role == 2) snprintf(o->host_name, sizeof o->host_name, "%s", in.host_name);
    { mp_session *ss = mp_game_session(); o->spectator = ss && mp_is_spectator(ss); }
    o->steam = in.steam;
    if (o->scene == 1 || o->scene == 2) {                                   /* Escape = Back on the title pages; closes the lobby Rules */
        static bool (*keydown)(int); static int looked;
        if (!looked) { looked = 1; keydown = (bool (*)(int))F(0x6F8B90, void *(*)(const char *))("UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)");
                       rig_log("mpmenu: Escape key check %s", keydown ? "ready (Input.GetKeyDownInt)" : "NOT available"); }
        o->escape = ((keydown && keydown(27)) || test_escape) && !chat_typing && !chat_close_frames ? 1 : 0;   /* KeyCode.Escape (the open chat takes it: chatfix A6) */
        if (o->escape && mpesc_took()) { o->escape = 0; rig_log("mpmenu: Escape taken by the kit for one of our windows (no Back, no other close)"); }   /* one press, one close */
        test_escape = 0;
    }
    if (host_failed) o->failed = 1;
    mp_session *s = o->role == 2 ? mp_game_session() : NULL;               /* guest: is the host still in the lobby? */
    for (int i = 0, n = s ? dsnet_hold_count(mp_client(s)) : 0, r; i < n; i++)
        if (dsnet_hold_get(mp_client(s), i, &r, NULL, NULL) == 0 && r == DSNET_HOLD_LOBBY) o->host_lobby = 1;
}

/* ---------------------------------------------------------------- title clones */
static Obj tb(int id) { Obj b = u_gc_get(h_tb[id]); return u_alive(b) ? b : NULL; }
/* text fields (address on the Join page, player name on the Host and Join pages): clones of a stock TMP_InputField.
 * Sources, best first: the load list's save-name field (SaveLoadView +0xB8), the new-campaign name field
 * (SubUI_CampaignSetting +0x30), any TMP_InputField. Popups are made on first use: if none exists yet, the load list
 * (popup 5) is opened for a moment, its field taken, and it is closed. Their listeners are runtime ones: not cloned. */
static uintptr_t h_anyf, h_slv, h_namefield; static void *k_anyf, *k_slv;
static int primed;
char my_name[64];                                                   /* current name (typed / file / Steam / "Player 4821") */
static Obj field_source(const char **from) {
    Obj slv = find_view_now(&h_slv, &k_slv, "Refactor", "SaveLoadView", true), fld = slv ? AT(slv, 0xB8, Obj) : NULL;   /* immediate: self-paced (fields_find) */
    *from = "the load list's name field";
    if (!u_alive(fld)) { Obj cs = find_view_now(&h_cs, &k_cs, "Refactor.UI", "SubUI_CampaignSetting", true); fld = cs ? AT(cs, 0x30, Obj) : NULL; *from = "the new-campaign name field"; }
    if (!u_alive(fld)) { fld = find_view_now(&h_anyf, &k_anyf, "TMPro", "TMP_InputField", true); *from = "a scene TMP_InputField"; }
    return u_alive(fld) ? fld : NULL;
}
Obj first_image(Obj tr, int depth);
/* The text area (TMP_InputField.textViewport, "Text Area", stretched): the stock save-name field keeps 35.8 units on
 * its left for the magnifier its "Search" sprite draws, 10 on its right (prefab: offset x 12.9, size -45.8, y -0.5 / -13;
 * SaveLoadUI and RenameUnitUI). Our frames have no magnifier, so that gap only pushed the text right (the address hint cut
 * at 216 of 262; the chat's typed text ~36 in). Here in_x on both sides; in_y < 0: the source's height and vertical place. */
static void field_area(Obj f, float in_x, float in_y) {
    Obj ta = tr_find(u_tr(f), "Text Area");
    if (!mm_is(ta, K_RT)) return;
    V2 sd = F(0x510701C, V2 (*)(Obj, void *))(ta, NULL), ap = F(0x5106E38, V2 (*)(Obj, void *))(ta, NULL);   /* sizeDelta, anchoredPosition */
    F(0x5106B64, void (*)(Obj, V2, void *))(ta, (V2){ 0, 0 }, NULL); F(0x5106D48, void (*)(Obj, V2, void *))(ta, (V2){ 1, 1 }, NULL);
    F(0x5107110, void (*)(Obj, V2, void *))(ta, (V2){ -2 * in_x, in_y < 0 ? sd.y : -2 * in_y }, NULL);
    F(0x5106F2C, void (*)(Obj, V2, void *))(ta, (V2){ 0, in_y < 0 ? ap.y : 0 }, NULL);
}
static Obj field_make(Obj src, Obj parent, const char *placeholder, const char *text) {
    Obj f = u_instantiate(src, parent);
    if (!u_alive(f)) return NULL;
    Obj ref = tb(MPB_CONNECT);                                               /* frame source: a menu button */
    /* the source field's background sprite "Search" draws a magnifier: use the menu button's frame (Button_Normal),
     * a little darker so it reads as a field (lead 2026-10-13) */
    { Obj fim = image_of(f), bim = ref ? first_image(u_tr(ref), 0) : NULL;
      Obj spr = u_alive(bim) ? (mm_is(bim, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(bim, NULL) : NULL) : NULL;
      if (u_alive(fim) && u_alive(spr)) {
          (mm_is(fim, K_IMG) ? F(0x5257600, void (*)(Obj, Obj, void *))(fim, spr, NULL) : (void)0);
          (mm_is(fim, K_GR) ? F(0x52602F8, void (*)(Obj, Col, void *))(fim, (Col){ 0.6f, 0.58f, 0.6f, 1 }, NULL) : (void)0);   /* darker: an input */
      }
      rig_log("mpmenu: field frame %s", u_alive(fim) && u_alive(spr) ? "= menu button frame (no magnifier)" : "unchanged (no image / sprite)"); }
    row_height(f, 262, 44);                                                 /* v2 §2a: input 262x44, Lora 18, centred vertically */
    field_area(f, 10, -1);                                                  /* the stock right inset on both sides */
    { static void *mtc; if (!mtc) mtc = mth("TMPro", "TMP_InputField", "get_textComponent", 0);
      Obj tc = mtc && mm_is(f, K_FIELD) ? MCALL(mtc, Obj, Obj, const void *)(f, mtc) : NULL;
      if (u_alive(tc)) { txt_field(tc); txt_style(tc, 0, 18, 513, 0); txt_color(tc, C_MAIN); } }
    (mm_is(f, K_FIELD) ? F(0x4EFC2D8, void (*)(Obj, int, void *))(f, 64, NULL) : (void)0);                   /* characterLimit */
    (mm_is(f, K_FIELD) ? F(0x4EFC75C, void (*)(Obj, int, void *))(f, 0, NULL) : (void)0);                    /* contentType Standard */
    Obj ph = (mm_is(f, K_FIELD) ? F(0x4EFB9CC, Obj (*)(Obj, void *))(f, NULL) : NULL);                    /* placeholder ("Save File Name" in the source) */
    if (u_alive(ph) && strstr(cls_name(ph), "Text")) { tmp_set(ph, placeholder); txt_field(ph); txt_style(ph, 0, 16, 513, 0); txt_color(ph, C_SUB); }
    (mm_is(f, K_FIELD) ? F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str_ok(text), NULL) : (void)0);       /* SetTextWithoutNotify */
    u_active(f, false);
    return f;
}
static void fields_find(Obj parent) {
    static int64_t last_try = -1000;
    if (u_alive(u_gc_get(h_field)) && u_alive(u_gc_get(h_namefield))) return;
    if (frames - last_try < 3) return;
    last_try = frames;
    const char *from; Obj src = field_source(&from);
    if (!src) {
        if (primed < 40) { if (!primed) { popup_activate(5); rig_log("mpmenu: opening the load list once to get a text field"); } primed++; return; }
        M.no_field = 1; return;                                            /* gave up: the address comes from the clipboard */
    }
    if (!u_alive(u_gc_get(h_field))) {
        char clip[128] = ""; str_utf8(F(0x518AEA0, Obj (*)(void *))(NULL), clip, sizeof clip);   /* prefill: clipboard, if it is an address */
        char hh[128]; uint16_t pp = 0;
        int is_addr = clip[0] && strchr(clip, '.') && !ui_parse_address(clip, hh, sizeof hh, &pp, MPM_DEFAULT_PORT);
        Obj f = field_make(src, parent, TR("Address, like 192.168.1.20:47777"), is_addr ? clip : "");
        u_gc_free(h_field); h_field = f ? u_gc_new(f) : 0;
        rig_log("mpmenu: address field cloned from %s%s", from, is_addr ? " (prefilled from the clipboard)" : "");
    }
    if (!u_alive(u_gc_get(h_namefield))) {
        Obj f = field_make(src, parent, TR("Your name"), my_name);
        u_gc_free(h_namefield); h_namefield = f ? u_gc_new(f) : 0;
    }
    if (primed && primed < 40) { Obj p = popups(); if (p && popup_open(5)) F(0xF62A88, void (*)(Obj, int, void *))(p, 5, NULL); }   /* DeactivateView(5) */
    primed = 40; M.no_field = 0;
}
static void field_text(uintptr_t h, char *out, size_t cap) {
    out[0] = 0; Obj f = u_gc_get(h);
    if (u_alive(f)) str_utf8((mm_is(f, K_FIELD) ? F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL) : NULL), out, cap);   /* TMP_InputField.text */
}
/* lane pvprules: a text field like the address field, for other windows of the menu (PvP Rules box "Save as preset...").
   NULL when no stock TMP_InputField exists yet. Made inactive, 262 x 44: the caller places and shows it. */
Obj mpmenu_field_new(Obj parent, const char *placeholder, const char *text) {
    const char *from = ""; Obj src = field_source(&from);
    Obj f = src ? field_make(src, parent, placeholder, text) : NULL;
    rig_log("mpmenu: extra text field %s %s", f ? "cloned from" : "not made: no source", from);
    return f;
}
void mpmenu_field_get(Obj f, char *out, size_t cap) {
    if (cap) out[0] = 0;
    if (u_alive(f)) str_utf8((mm_is(f, K_FIELD) ? F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL) : NULL), out, cap);   /* TMP_InputField.text */
}
void mpmenu_field_put(Obj f, const char *s) {
    if (u_alive(f)) (mm_is(f, K_FIELD) ? F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str_ok(s), NULL) : (void)0);   /* SetTextWithoutNotify */
}
/* text-only line (labels, status): the button clone with every Image under it switched off (Behaviour.enabled), so
 * no frame shows (ux #16). Clicks on it reach our hook and do nothing. */
static void images_off(Obj tr, int depth) {
    Obj im = image_of(tr);
    if (im) (mm_is(im, K_BEH) ? F(0x50ECEA0, void (*)(Obj, bool, void *))(im, false, NULL) : (void)0);
    if (depth >= 4) return;
    int k = (mm_is(tr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(tr, NULL) : 0);
    for (int i = 0; i < k; i++) images_off((mm_is(tr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(tr, i, NULL) : NULL), depth + 1);
}
static int fields_seen;                                 /* last texts below are valid (reset when the fields are rebuilt) */
static char seen_addr[256], seen_name[64];
/* a clickable clone: esc = SubUI_SystemMenuButton (lobby), else SubUI_TitleMenuButton (title). Sprite on its background
 * (sliced, stretched), label centred. The caller registers it for clicks. */
Obj mk_button(Obj src, Obj parent, int esc, int sprite, Col tint, float x, float y, float w, float h, float fs, const char *label, Col tc) {
    if (!u_alive(src) || !u_alive(parent)) return NULL;
    Obj b = u_instantiate(src, parent);
    if (!u_alive(b)) return NULL;
    if (esc) { if (mm_is(b, K_SMB)) F(0xF2DB60, void (*)(Obj, Obj, void *))(b, NULL, NULL); AT(b, 0x30, Obj) = NULL; }
    else AT(b, 0x140, Obj) = NULL;
    u_active(b, true);
    Obj bg = first_image(u_tr(b), 0);
    if (bg && sprite >= 0 && !spr(sprite) && sprite == S_NBTN) {}       /* keep the clone's own frame (it is the menu button) */
    else if (bg) img_set(bg, sprite >= 0 ? spr(sprite) : NULL, sprite >= 0 && !spr(sprite) ? spr_fallback(sprite) : tint);
    if (bg) { stretch(bg); if (mm_is(bg, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bg, true, NULL); }
    Obj l = AT(b, esc ? 0x28 : 0x130, Obj);
    if (u_alive(l)) { txt_body(l); txt_style(l, 0, fs, 514, 0); txt_color(l, tc); stretch(l); tmp_set(l, label);
                      if (w >= 40 && mm_is(u_tr(l), K_RT)) F(0x5107110, void (*)(Obj, V2, void *))(u_tr(l), (V2){ w >= 120 ? -24 : -16, 0 }, NULL); }   /* 12 (small: 8) free each side: a long label fits inside the frame, never edge to edge */
    place(b, x, y, w, h);
    return b;
}
Obj btn_label(Obj b, int esc) { return b ? AT(b, esc ? 0x28 : 0x130, Obj) : NULL; }
/* Rules panel: band "Rules" (Cinzel 24), rows [label  < value >], note, Done. Centre (cx, cy) in the parent.
 * Narrow (host page) 430 x 520: one column. Wide (lobby, RP_WIDE_W x RP_WIDE_H): Pause..Units left, Population..Admin right,
 * so it fits inside the lobby body under the title and above Start/Leave. */
#define RP_WIDE_W 830
#define RP_WIDE_H 376                                            /* 9 rules rows: the right column has 5 */
static void destroy_go(Obj comp);
static void rp_build(rpanel *p, Obj parent, Obj src, int esc, Obj tpl_img, Obj tpl_txt, float cx, float cy, const char *note, uintptr_t *reg, int wide) {
    float pw = wide ? RP_WIDE_W : 430, ph = wide ? RP_WIDE_H : 588;     /* narrow: 9 rows (+ Shared fog, Spectators); host page: always open, no Done */
    #define PX(x) ((x) - pw / 2)
    #define PY(y) (ph / 2 - (y))
    #define RCOL(i) (wide && (i) >= MPM_RULE_POP ? 1 : 0)
    #define RTOP(i) (wide ? 62 + 46 * ((i) - (RCOL(i) ? MPM_RULE_POP : 0)) + (RCOL(i) && (i) > MPM_RULE_POP ? 22 : 0) \
                          : 68 + 50 * (i) + ((i) > MPM_RULE_POP ? 22 : 0))     /* the Population row has a description line under it */
    #define RX(i) (wide ? (RCOL(i) ? 422 : 14) : 18)                            /* row left edge; rows are 394 wide */
    float rh = wide ? 44 : 48;
    float end = (wide && RTOP(MPM_RULE_POP - 1) > RTOP(MPM_RULE_ROWS - 1) ? RTOP(MPM_RULE_POP - 1) : RTOP(MPM_RULE_ROWS - 1)) + rh;   /* the longer column */
    /* the note under the rows: one line of 12 in English; a longer one (ru, de, fr, ja: two lines) gets a taller box at
       the same size, as the game does for long text (it was auto-sized down to 8; English stays 22 high). Measured on a throwaway copy, so the
       panel can grow before it is built. Narrow: the box starts 7 under the rows and grows down (the panel too, if needed). */
    float nw = wide ? 600 : 394, nh = 22;
    { Obj m = mk_txt(tpl_txt, parent, 0, 12, 513, 1, 0, 0, nw, 22, C_SUB); if (m) { tmp_set(m, note); float th = txt_text_h(m, nw); if (th > nh + 2) nh = (float)(int)(th + 0.99f); u_active(m, false); destroy_go(m); } }
    if (!wide && end + 7 + nh + 8 > ph) ph = end + 7 + nh + 8;
    Obj root = mk_img(tpl_img, parent, S_BOX, C_WHITE, cx, cy, pw, ph);
    if (!root) return;
    Obj R = u_tr(root);
    u_gc_free(p->root); p->root = u_gc_new(root);
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(wide ? 24 : 27), pw - 4, wide ? 44 : 50);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(wide ? 46 : 52), pw - 4, 1);
    Obj t = mk_txt(tpl_txt, R, 1, 24, 514, 0, 0, PY(wide ? 24 : 27), 400, 40, C_MAIN); tmp_set(t, TR("Rules"));
    for (int i = 0; i < MPM_RULE_ROWS; i++) {
        float yc = PY(RTOP(i) + rh / 2), x0 = RX(i);
        Obj rb = mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, PX(x0 + 197), yc, 394, rh);
        u_gc_free(p->row[i]); p->row[i] = rb ? u_gc_new(rb) : 0;
        /* label 12..140 (the longest word, ru "администратора", fits: no break inside a word), arrows at 158 and 378, value
           176..356 between them (8 from each); both 40 high: a long label or value takes 2 lines at its size, not 1 shrunk */
        Obj lb = mk_txt(tpl_txt, R, 0, 15, 513, 0, PX(x0 + 76), yc, 128, 40, C_SUB); tmp_set(lb, mpm_rule_label(i));
        Obj v = mk_txt(tpl_txt, R, 0, 16, 514, 0, PX(x0 + 266), yc, 180, 40, C_MAIN);
        { typedef struct { float x, y, z, w; } mg4; static void *mmg; if (!mmg) mmg = mth("TMPro", "TMP_Text", "set_margin", 1);   /* the value 6 inside each side, */
          if (mmg && v) MCALL(mmg, void, Obj, mg4, const void *)(v, (mg4){ 6, 0, 6, 0 }, mmg); }   /* as TMP margins (the fit measures them): de "Geteilt, eigene Einheiten" touched both arrows */
        u_gc_free(p->val[i]); p->val[i] = v ? u_gc_new(v) : 0;
        for (int a = 0; a < 2; a++) {
            Obj b = mk_button(src, R, esc, S_NBTN, C_WHITE, PX(x0 + (a ? 378 : 158)), yc, 28, 28, 16, a ? "\xE2\x80\xBA" : "\xE2\x80\xB9", C_MAIN);
            int id = arrow_ids[2 * i + a];
            if (reg) { u_gc_free(reg[id]); reg[id] = b ? u_gc_new(b) : 0; }
        }
    }
    { Obj ds = mk_txt(tpl_txt, R, 0, 12, 513, 0, PX(RX(MPM_RULE_POP) + 197), PY(RTOP(MPM_RULE_POP) + rh + 11), 380, 20, C_SUB);   /* under Population */
      u_gc_free(p->desc); p->desc = ds ? u_gc_new(ds) : 0; }
    Obj nt = mk_txt(tpl_txt, R, 0, 12, 513, 1, wide ? PX(14 + 300) : 0, wide ? PY(end + 40) : PY(end + 7 + nh / 2), nw, nh, C_SUB); tmp_set(nt, note);
    u_gc_free(p->note); p->note = nt ? u_gc_new(nt) : 0;
    Obj d = mk_button(src, R, esc, S_NBTN, C_WHITE, PX(pw - 88), PY(end + (wide ? 40 : 59 + nh - 22)), 140, 34, 16, TR("Done"), C_MAIN);
    if (reg) { u_gc_free(reg[MPB_RULES_DONE]); reg[MPB_RULES_DONE] = d ? u_gc_new(d) : 0; }
    p->pw = pw; p->ph = ph; p->tip_row = -1;
    u_active(root, false);
    #undef PX
    #undef PY
    #undef RTOP
    #undef RCOL
    #undef RX
}
/* ---- our tooltips in the game's own tooltip window (ui_tip.c, owner lan_v9; texts research/tooltips_v9.md 3.1-3.5,
 * 3.7, 3.13, 3.14). Each frame the topmost element under the mouse that has a tooltip shows it (game_tip_show_rect);
 * ui_tip hides it 0.2 s after the last call and follows "Show tooltips on hover". Tests name the element by title. */
static int tip_ready(void) { return game_tip_ready(); }                      /* build matches + the scene has its TooltipUI */
static char test_hover[64];                                                  /* test: hover the element with this title */
char tip_now[64];                                                     /* test status: the title shown this frame */
static int tip_hit;
static inline bool u_on_tree(Obj comp) { Obj g = u_alive(comp) ? u_go(comp) : NULL; return g && mm_is(g, K_GO) && F(0x50F3724, bool (*)(Obj, void *))(g, NULL); }   /* activeInHierarchy */
int mu_bring_is_open(void);                                           /* mu_bring.c: the Bring Units card window */
int tip_try(Obj o, const char *title, const char *body) {
    if (tip_hit || !o || !u_on_tree(o)) return 0;
    if (mu_bring_is_open()) return 0;                                       /* our menus' tips (mouse-in-rect) never through the card window */
    int hov;
    if (test_hover[0]) hov = !strcmp(test_hover, title);
    else { float r[4]; V3 m = F(0x51AD4DC, V3 (*)(void *))(NULL); hov = rect_of(o, r) && m.x >= r[0] && m.x <= r[2] && m.y >= r[1] && m.y <= r[3]; }
    if (!hov) return 0;
    tip_hit = 1; snprintf(tip_now, sizeof tip_now, "%s", title);
    if (tip_ready()) game_tip_show_rect(title, body, u_tr(o));
    return 1;
}
static void rp_tooltip(rpanel *p) {                                          /* Rules rows (3.3) */
    p->tip_row = -1;
    for (int i = 0; i < MPM_RULE_ROWS; i++) if (tip_try(u_gc_get(p->row[i]), mpm_rule_tip_title(i), mpm_rule_tip(i))) { p->tip_row = i; return; }
}
static void rp_update(rpanel *p, const mpm_settings *st, int show) {
    Obj root = u_gc_get(p->root);
    if (!u_alive(root)) return;
    u_active(root, show);
    if (!show) return;
    for (int i = 0; i < MPM_RULE_ROWS; i++) { char v[64]; mpm_rule_value(st, i, v, sizeof v); Obj t = u_gc_get(p->val[i]); if (u_alive(t)) tmp_set(t, v); }
    Obj ds = u_gc_get(p->desc); if (u_alive(ds)) tmp_set(ds, mpm_population_desc(st->population));
}

static void fe_build(fepanel *p, Obj parent, Obj src, Obj tpl_img, Obj tpl_txt, float cx, float cy) {
    const float pw = 430, rh = 48, step = 74, end = 68 + step * MPM_FE_ROWS, ph = end + 40;   /* host page: always open, no Done */
    #define PX(x) ((x) - pw / 2)
    #define PY(y) (ph / 2 - (y))
    Obj root = mk_img(tpl_img, parent, S_BOX, C_WHITE, cx, cy, pw, ph);
    if (!root) return;
    Obj R = u_tr(root);
    u_gc_free(p->root); p->root = u_gc_new(root);
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(27), pw - 4, 50);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(52), pw - 4, 1);
    Obj t = mk_txt(tpl_txt, R, 1, 24, 514, 0, 0, PY(27), 400, 40, C_MAIN); tmp_set(t, TR("Expedition Rules"));
    for (int i = 0; i < MPM_FE_ROWS; i++) {
        float top = 62 + step * i, yc = PY(top + rh / 2);
        Obj rb = mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, PX(18 + 197), yc, 394, rh);
        u_gc_free(p->row[i]); p->row[i] = rb ? u_gc_new(rb) : 0;
        Obj lb = mk_txt(tpl_txt, R, 0, 15, 513, 0, PX(18 + 76), yc, 128, 40, C_SUB); tmp_set(lb, mpm_fe_label(i));   /* as the Rules rows */
        Obj v = mk_txt(tpl_txt, R, 0, 16, 514, 0, PX(18 + 266), yc, 180, 40, i == 4 ? C_SUB : C_MAIN);
        u_gc_free(p->val[i]); p->val[i] = v ? u_gc_new(v) : 0;
        Obj ds = mk_txt(tpl_txt, R, 0, 12, 513, 0, PX(18 + 197), PY(top + rh + 11), 390, 20, C_SUB);
        u_gc_free(p->desc[i]); p->desc[i] = ds ? u_gc_new(ds) : 0;
        for (int a = 0; a < 2 && fe_arrows[i][0]; a++) {
            Obj b = mk_button(src, R, 0, S_NBTN, C_WHITE, PX(18 + (a ? 378 : 158)), yc, 28, 28, 16, a ? "\xE2\x80\xBA" : "\xE2\x80\xB9", C_MAIN);
            int id = fe_arrows[i][a]; u_gc_free(h_tb[id]); h_tb[id] = b ? u_gc_new(b) : 0;
        }
    }
    Obj nt = mk_txt(tpl_txt, R, 0, 12, 513, 1, 0, PY(end + 14), 394, 22, C_SUB); tmp_set(nt, TR("Visitors see these rules before they choose units."));
    Obj d = mk_button(src, R, 0, S_NBTN, C_WHITE, PX(pw - 88), PY(end + 55), 140, 34, 16, TR("Done"), C_MAIN);
    u_gc_free(h_tb[MPB_FE_DONE]); h_tb[MPB_FE_DONE] = d ? u_gc_new(d) : 0;
    p->tip_row = -1;
    u_active(root, false);
    #undef PX
    #undef PY
}
static void fe_update(fepanel *p, const mpm_settings *st, int show) {
    Obj root = u_gc_get(p->root);
    if (!u_alive(root)) return;
    u_active(root, show);
    if (!show) return;
    for (int i = 0; i < MPM_FE_ROWS; i++) {
        char v[64]; mpm_fe_value(st, i, v, sizeof v);
        Obj t = u_gc_get(p->val[i]); if (u_alive(t)) tmp_set(t, v);
        Obj d = u_gc_get(p->desc[i]); if (u_alive(d)) tmp_set(d, mpm_fe_desc(st, i));
    }
}
static uintptr_t h_cp_root, h_cp_panel;                 /* host choice panel (draw_page): box + the empty RectTransform given to the mod */
static Obj empty_rt(Obj parent_tr, const char *name) {    /* new GameObject(name) + RectTransform, under parent (its layer) */
    void *kg = class_by_name("UnityEngine", "GameObject"); Obj ty = type_obj("UnityEngine", "RectTransform");
    Obj go = kg && ty ? F(0x6F96CC, Obj (*)(void *))(kg) : NULL;                        /* il2cpp_object_new */
    if (!go) return NULL;
    F(0x50F4674, void (*)(Obj, Obj, void *))(go, u_str(name), NULL);                  /* GameObject..ctor(string) */
    if (!u_alive(go)) return NULL;
    Obj rt = F(0x50F2B90, Obj (*)(Obj, Obj, void *))(go, ty, NULL);                   /* GameObject.AddComponent(Type) */
    if (!mm_is(rt, K_RT)) { rig_log("mpmenu: host choice panel: no RectTransform"); return NULL; }
    F(0x50F3204, void (*)(Obj, int, void *))(go, F(0x50F3118, int (*)(Obj, void *))(u_go(parent_tr), NULL), NULL);   /* the parent's layer */
    F(0x510B2C4, void (*)(Obj, Obj, bool, void *))(rt, parent_tr, false, NULL);       /* Transform.SetParent(parent, false) */
    return rt;
}
/* title-column rows as built (row, its frame image, its label rect), for rows that grow to a label still too long at 0.75 */
static float trow_base[MPB_COUNT], trow_bgh[MPB_COUNT], trow_lw[MPB_COUNT], trow_lh[MPB_COUNT], trow_cur[MPB_COUNT];
static void trow_note(int id, Obj b) {
    if (id < 0 || id >= MPB_COUNT) return;
    Obj rt = u_tr(b), bg = first_image(rt, 0), l = AT(b, 0x130, Obj);
    trow_base[id] = trow_cur[id] = mm_is(rt, K_RT) ? F(0x510701C, V2 (*)(Obj, void *))(rt, NULL).y : 0;   /* sizeDelta */
    trow_bgh[id] = bg && mm_is(u_tr(bg), K_RT) ? F(0x510701C, V2 (*)(Obj, void *))(u_tr(bg), NULL).y : 0;   /* 0: stretched */
    trow_lw[id] = trow_lh[id] = 0;
    if (mm_is(l, K_TMP) && fit_methods() && mm_is(u_tr(l), K_RT)) { fit_rect r = MCALL(m_fit_rect, fit_rect, Obj, const void *)(u_tr(l), m_fit_rect); trow_lw[id] = r.w; trow_lh[id] = r.h; }
}
/* each frame: a row whose label does not fit even at 0.75 grows to the label's height at its design size (+8 above and
   below); the column's layout moves the rows under it down. A label that fits (all English) keeps the stock row. */
/* note: a choice row's second line (CN_H high at its design size; more when it wraps at 0.75): a row that grows for its
   label adds the line's height, a stock row only what the line needs beyond CN_H (the stock row holds label + line) */
#define CN_H 16
static void trow_fit(int id, Obj b, float note) {
    if (id < 0 || id >= MPB_COUNT || id == MPB_SETTINGS || trow_base[id] < 30 || trow_lw[id] < 4 || trow_lh[id] < 4) return;
    float need = txt_fit_h(AT(b, 0x130, Obj), trow_lw[id], trow_lh[id]);
    float want = need > trow_lh[id] + 1 && need + 16 > trow_base[id] ? need + (id == MPB_HEADER ? 36 : 16) + note : trow_base[id] + (note > CN_H ? note - CN_H : 0);   /* a 2-line page title: 18 above and below (the label under it touched it) */
    if (want == trow_cur[id]) return;
    row_height(b, 262, want);
    Obj bg = first_image(u_tr(b), 0);
    if (bg && trow_bgh[id] > 1 && mm_is(u_tr(bg), K_RT)) { V2 sd = F(0x510701C, V2 (*)(Obj, void *))(u_tr(bg), NULL); sd.y = trow_bgh[id] + want - trow_base[id]; F(0x5107110, void (*)(Obj, V2, void *))(u_tr(bg), sd, NULL); }
    rig_log("mpmenu: text fit: title row %d grows to %.0f (label needs %.0f of %.0f)", id, want, need, trow_lh[id]);
    trow_cur[id] = want;
}
static void title_build(Obj v) {
    Obj src = AT(v, 0x50, Obj), after = AT(v, 0x40, Obj);
    if (!u_alive(src) || !u_alive(after)) { rig_log("mpmenu: title buttons not found"); return; }
    Obj parent = u_tr_parent(u_tr(src));
    int at = u_sibling(u_tr(after)) + 1;
    if (!scan_done || !h_spr[S_COG]) assets_scan_now();
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x130, Obj);
    for (int i = 0; i < NT; i++) {
        int id = title_ids[i];
        Obj b = u_instantiate(src, parent);
        if (!u_alive(b)) { rig_log("mpmenu: title clone failed"); return; }
        AT(b, 0x140, Obj) = NULL;                                           /* _onButtonClicked: no stock action */
        Obj l = AT(b, 0x130, Obj);
        txt_style(l, 0, 22, 514, 1);                                        /* design #8: the stock menu size 22 (Credits is 24) */
        int plain = id == MPB_LABEL_ADDR || id == MPB_LABEL_NAME || id == MPB_INFO || id == MPB_HEADER || id == MPB_DIVIDER || id == MPB_HOST || id == MPB_JOIN_HDR || id == MPB_JOIN_DIV;
        if (plain) images_off(u_tr(b), 0);
        if (plain || id == MPB_SETTINGS) txt_body(l);                      /* text rows: the body font, the game's body look */
        /* approved mockup "Host page A": subtitle, Your name, field, Rules box, line, Host from a save (primary), ... Back */
        if (id == MPB_HEADER) { row_height(b, 262, 30); txt_style(l, 0, 18, 514, 0); txt_color(l, C_SUB); }   /* the page title: as "Host a game" / "Join a game" */
        if (id == MPB_LABEL_ADDR || id == MPB_LABEL_NAME) { row_height(b, 262, 18); txt_style(l, 0, 14, 513, 0); txt_color(l, C_SUB); }
        if (id == MPB_INFO) { row_height(b, 262, 40); txt_style(l, 0, 14, 514, 1); }
        if (id == MPB_DIVIDER) { row_height(b, 262, 17); mk_img(tpl_img, u_tr(b), -1, C_LINE, 0, 0, 262, 1); }
        if (id == MPB_BACK) {                                               /* text-only look, but its frame stays the click target */
            txt_body(l);                                                    /* stock row height: the stock gap to the row above (owner v12 item 4) */ txt_style(l, 0, 18, 514, 0); txt_color(l, C_SUB);
            Obj bg = first_image(u_tr(b), 0);
            if (bg && mm_is(bg, K_GR)) { F(0x52602F8, void (*)(Obj, Col, void *))(bg, (Col){ 1, 1, 1, 0 }, NULL); F(0x5260348, void (*)(Obj, bool, void *))(bg, true, NULL); }
        }
        if (id == MPB_LOAD_SAVE || id == MPB_HOST_START) {                  /* the main path: the red primary style */
            Obj bg = first_image(u_tr(b), 0); if (bg) img_set(bg, spr(S_RECRUIT), spr(S_RECRUIT) ? C_WHITE : spr_fallback(S_RECRUIT));
            if (bg && mm_is(bg, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bg, true, NULL);
            txt_color(l, C_START);
        }
        if (id == MPB_HOST || id == MPB_JOIN_HDR) { row_height(b, 262, 30); txt_style(l, 0, 18, 514, 0); txt_color(l, C_SUB); }   /* "Host a game" / "Join a game": the line over their choices */
        if (id == MPB_JOIN_DIV) {                                           /* the game's own divider (the column's PlaceHolder, also under Back) */
            row_height(b, 262, 17);
            Obj ph = tr_find(parent, "PlaceHolder"), d = ph ? u_instantiate(ph, u_tr(b)) : NULL;
            if (d) place(d, 0, 0, 55, 9); else mk_img(tpl_img, u_tr(b), -1, C_LINE, 0, 0, 262, 1);
        }
        /* owner v12 item 4: the stock row height (the clone keeps the stock button's size), so the column's spacing gives the
           stock gap. A smaller row (40 before) left the stock frame image at full size: the frames touched. */
        if (id == MPB_HOST_SAVE || id == MPB_HOST_NEW || mpm_choice_of_btn(id) >= 0 || id == MPB_JOIN_ADDR || id == MPB_JOIN_FRIEND || id == MPB_FE_RESLIST)
            txt_style(l, 0, 19, 514, 0);                                    /* the whole page one size (fr "Join a friend" + its note spilled at 22) */
        if (id == MPB_SETTINGS) {                                           /* [cog] Rules  ›  + summary under it */
            row_height(b, 262, 104);
            Obj bg = first_image(u_tr(b), 0); if (bg) { stretch(bg); u_gc_free(h_title_box); h_title_box = u_gc_new(bg); }
            txt_style(l, 0, 18, 513, 0); txt_color(l, C_MAIN);
            Obj cg = mk_img(tpl_img, u_tr(b), S_COG, C_WHITE, -108, 18, 22, 22);
            u_gc_free(h_title_cog); h_title_cog = cg ? u_gc_new(cg) : 0;
            Obj pc = mk_pixel_cog(tpl_img, u_tr(b), -108, 18, 22);
            u_gc_free(h_title_pcog); h_title_pcog = pc ? u_gc_new(pc) : 0;
            Obj mo = mk_txt(tpl_txt, u_tr(b), 0, 18, 514, 0, 113, 18, 20, 30, C_SUB); tmp_set(mo, "\xE2\x80\xBA");
            u_gc_free(h_title_more); h_title_more = mo ? u_gc_new(mo) : 0;
            Obj sm = mk_txt(tpl_txt, u_tr(b), 0, 12, 513, 1, 0, -18, 238, 60, C_SUB);   /* up to 4 lines inside the box (owner lan_v8: it spilled) */
            u_gc_free(h_title_sum); h_title_sum = sm ? u_gc_new(sm) : 0;
        }
        /* the label 12 inside each side of the frame: a long label (de "Aus einem Spielstand hosten") fits inside it */
        /* as TMP margins: a smaller rect did not hold (round 2 shots: the label still 262 wide) */
        if (id != MPB_SETTINGS && mm_is(l, K_TMP)) { typedef struct { float x, y, z, w; } mg4; static void *mmg; if (!mmg) mmg = mth("TMPro", "TMP_Text", "set_margin", 1);
                                                   if (mmg) MCALL(mmg, void, Obj, mg4, const void *)(l, (mg4){ 12, 0, 12, 0 }, mmg); }
        trow_note(id, b);
        { int k = mpm_choice_of_btn(id);                                    /* host choice: its small second line (hidden until it has one) */
          if (k >= 0) {
              Obj lr = u_tr(l);
              cnote_ap0[k] = mm_is(lr, K_RT) ? F(0x5106E38, V2 (*)(Obj, void *))(lr, NULL) : (V2){ 0, 0 };   /* anchoredPosition */
              cnote_x[k] = (trow_lw[id] > 64 ? trow_lw[id] : 262) - 24;        /* the label's width inside its margins */
              /* the game's small text: the language's body font (LanguageSetting.GetFont EasyRead, its body look), 12, the
                 secondary colour; one line, auto size down to 0.75 when too long (txt_style) */
              Obj nt = mk_txt(tpl_txt, u_tr(b), 0, 12, 514, 0, 0, -14, cnote_x[k], CN_H, C_SUB);
              if (nt) u_active(nt, false);
              u_gc_free(h_cnote[k]); h_cnote[k] = nt ? u_gc_new(nt) : 0;
              rig_log("mpmenu: choice %d note line: row %.0f, label %.0f x %.0f at (%.0f, %.0f)", k, trow_base[id], trow_lw[id], trow_lh[id], cnote_ap0[k].x, cnote_ap0[k].y);
          } }
        u_set_sibling(u_tr(b), at + i);
        u_active(b, false);
        u_gc_free(h_tb[id]); h_tb[id] = u_gc_new(b);
    }
    /* Recent worlds: 760 wide at the mockup's left 470 / top 130 (1512 x 982); 6 rows of 66, built once, filled per page */
    {
        rw_h = 56 + 12 + 6 * 72 + 40;
        Obj root = mk_img(tpl_img, u_tr(v), S_BOX, C_WHITE, 103, 397 - rw_h / 2, 760, rw_h);
        if (root) {
            Obj R = u_tr(root);
            u_gc_free(h_rw_root); h_rw_root = u_gc_new(root);
            #define RY(y) (rw_h / 2 - (y))
            Obj bd = mk_img(tpl_img, R, -1, C_BAND, 0, RY(28), 756, 54); u_gc_free(h_rw_band); h_rw_band = bd ? u_gc_new(bd) : 0;
            Obj ln = mk_img(tpl_img, R, -1, C_COPPER, 0, RY(56), 756, 1); u_gc_free(h_rw_line); h_rw_line = ln ? u_gc_new(ln) : 0;
            Obj t = mk_txt(tpl_txt, R, 1, 26, 514, 0, 0, RY(28), 700, 44, C_MAIN); u_gc_free(h_rw_title); h_rw_title = t ? u_gc_new(t) : 0;
            for (int i = 0; i < 6; i++) {
                float y = RY(68 + 72 * i + 33);
                Obj b = mk_button(src, R, 0, S_NBTN, i % 2 ? (Col){ 0.85f, 0.85f, 0.85f, 1 } : C_WHITE, 0, y, 736, 66, 16, "", C_MAIN);
                if (!b) continue;
                u_gc_free(h_tb[MPB_HIST0 + i]); h_tb[MPB_HIST0 + i] = u_gc_new(b);
                Obj B = u_tr(b);
                Obj l1 = mk_txt(tpl_txt, B, 0, 18, 513, 0, -368 + 14 + 230, 12, 460, 28, C_MAIN);
                Obj l2 = mk_txt(tpl_txt, B, 0, 13, 513, 0, -368 + 14 + 190, -14, 380, 22, C_SUB);
                Obj ul = mk_txt(tpl_txt, B, 0, 12, 516, 0, 368 - 80 - 110, 12, 220, 20, C_SUB);
                Obj un = mk_txt(tpl_txt, B, 0, 13, 516, 0, 368 - 80 - 110, -12, 220, 22, C_MAIN);
                for (int k = 0; k < 6; k++) {                               /* each unit: [portrait or initial] Name (lead 2026-10-13) */
                    float px = 368 - 90 - 15 - 96 * (5 - k) - 66;           /* 40 left of the old place: room for Watch */
                    Obj fr = mk_img(tpl_img, B, -1, C_COPPER, px, -10, 30, 30);
                    if (fr) mk_img(tpl_img, u_tr(fr), -1, (Col){ 0.29f, 0.231f, 0.212f, 1 }, 0, 0, 28, 28);   /* #4a3b36 */
                    Obj it = fr ? mk_txt(tpl_txt, u_tr(fr), 0, 15, 514, 0, 0, 0, 28, 28, C_MAIN) : NULL;
                    Obj pi = fr ? mk_img(tpl_img, u_tr(fr), -1, C_WHITE, 0, 0, 28, 28) : NULL;
                    if (pi) u_active(pi, false);                                /* shown only with a loaded portrait */
                    u_gc_free(h_rw_pf[i][k]); h_rw_pf[i][k] = fr ? u_gc_new(fr) : 0; u_gc_free(h_rw_pi[i][k]); h_rw_pi[i][k] = it ? u_gc_new(it) : 0;
                    u_gc_free(h_rw_pp[i][k]); h_rw_pp[i][k] = pi ? u_gc_new(pi) : 0;
                    Obj nmt = mk_txt(tpl_txt, B, 0, 12, 513, 0, px + 15 + 4 + 31, -10, 62, 20, C_MAIN);
                    u_gc_free(h_rw_pn[i][k]); h_rw_pn[i][k] = nmt ? u_gc_new(nmt) : 0;
                }
                u_gc_free(h_rw_l1[i]); h_rw_l1[i] = l1 ? u_gc_new(l1) : 0; u_gc_free(h_rw_l2[i]); h_rw_l2[i] = l2 ? u_gc_new(l2) : 0;
                u_gc_free(h_rw_ul[i]); h_rw_ul[i] = ul ? u_gc_new(ul) : 0; u_gc_free(h_rw_u[i]); h_rw_u[i] = un ? u_gc_new(un) : 0;
                Obj x = mk_button(src, B, 0, S_NBTN, C_WHITE, 368 - 22, 15, 26, 26, 15, "x", C_SUB);   /* remove: a small framed "x", own click */
                if (x) { u_gc_free(h_tb[MPB_HISTX0 + i]); h_tb[MPB_HISTX0 + i] = u_gc_new(x); }
                Obj w = mk_button(src, B, 0, S_NBTN, C_WHITE, 368 - 10 - 40, -16, 80, 26, 14, TR("Watch"), C_MAIN);   /* spectator rejoin (owner 2026-10-14) */
                if (w) { u_gc_free(h_tb[MPB_HISTW0 + i]); h_tb[MPB_HISTW0 + i] = u_gc_new(w); }
            }
            Obj nt = mk_txt(tpl_txt, R, 0, 12, 513, 0, -380 + 16 + 250, RY(rw_h - 20), 500, 20, C_SUB);
            if (nt) tmp_set(nt, TR("Click a world to reconnect the same way. The x button removes it from this list."));
            u_gc_free(h_rw_note); h_rw_note = nt ? u_gc_new(nt) : 0;
            Obj pv = mk_button(src, R, 0, S_NBTN, C_WHITE, 380 - 16 - 160, RY(rw_h - 20), 100, 26, 14, TR("\xE2\x80\xB9 Newer"), C_MAIN);
            Obj nx = mk_button(src, R, 0, S_NBTN, C_WHITE, 380 - 16 - 52, RY(rw_h - 20), 100, 26, 14, TR("Older \xE2\x80\xBA"), C_MAIN);
            if (pv) { u_gc_free(h_tb[MPB_HIST_PREV]); h_tb[MPB_HIST_PREV] = u_gc_new(pv); }
            if (nx) { u_gc_free(h_tb[MPB_HIST_NEXT]); h_tb[MPB_HIST_NEXT] = u_gc_new(nx); }
            #undef RY
            u_active(root, false);
        }
    }
    /* the Rules panel opens to the right of the column (mockup: left 470, top 312 at 1512 x 982 -> canvas centre offset) */
    /* owner v12 item 5: always open on the host page, no Done; the top stays where it was (the panels lost their Done row) */
    rp_build(&rp_title, u_tr(v), src, 0, tpl_img, tpl_txt, -78, -89 + 26, TR("Saved with this world. New worlds start with your last rules."), h_tb, 0);
    fe_build(&rp_fe, u_tr(v), src, tpl_img, tpl_txt, -78, -89 + 26);
    /* a register_host_choice2 choice's own panel (draw_page): a box where the Rules panel sits, an empty RectTransform inside */
    { Obj root = mk_img(tpl_img, u_tr(v), S_BOX, C_WHITE, -78, -89 + 26, 430, 588), pnl = root ? empty_rt(u_tr(root), "DSMP_HostChoicePanel") : NULL;
      u_gc_free(h_cp_root); h_cp_root = root ? u_gc_new(root) : 0; u_gc_free(h_cp_panel); h_cp_panel = pnl ? u_gc_new(pnl) : 0;
      if (pnl) { stretch(pnl); F(0x5107110, void (*)(Obj, V2, void *))(pnl, (V2){ -24, -24 }, NULL); }   /* 12 inside each edge */
      if (root) u_active(root, false); }
    u_gc_free(h_field); h_field = 0; u_gc_free(h_namefield); h_namefield = 0; primed = 0; fields_seen = 0;
    u_gc_free(h_title_built); h_title_built = u_gc_new(v);
    if (stock_hidden && u_gc_get(h_stock_view) == v) stock_set(v, 0);       /* rebuilt while our page hid them: shown as they were */
    stock_hidden = 0;
    rig_log("mpmenu: title clones added at sibling %d", at);
}
/* our own menu page hides the 7 stock title buttons; Back shows again only those that were shown (the game hides
   Continue when there is no save to continue: showing all 7 put it back and the menu changed height, owner 2026-10-17) */
static void stock_set(Obj v, int hide) {
    for (int i = 0; i < 7; i++) {
        Obj b = AT(v, 0x30 + 8 * i, Obj);
        if (hide) { if (u_on(b)) stock_was |= 1u << i; else stock_was &= ~(1u << i); u_active(b, false); }
        else if (stock_was >> i & 1) u_active(b, true);
    }
    stock_hidden = hide; u_gc_free(h_stock_view); h_stock_view = hide ? u_gc_new(v) : 0;
}
static void title_sync(const mpm_obs *o) {
    if (o->scene != 1) return;                                              /* no TitleMenuView lookup in the world */
    Obj v = title_view();
    if (!v) return;
    if (u_gc_get(h_title_built) != v || !tb(MPB_MULTIPLAYER)) title_build(v);
    uint64_t vis = mpm_visible(&M, o);
    int sub = vis & ~(1ull << MPB_MULTIPLAYER) ? 1 : 0;                        /* our own menu page: hide the stock buttons */
    if (sub != stock_hidden) stock_set(v, sub);
    for (int i = 0; i < NT; i++) {
        Obj b = tb(title_ids[i]); if (!b) continue;
        int on = title_ids[i] < 64 ? (int)((vis >> title_ids[i]) & 1) : mpm_is_visible(&M, o, title_ids[i]);   /* Watch: id 68 */
        u_active(b, on);
        if (on) { char l[512]; mpm_label(&M, title_ids[i], l, sizeof l); tmp_set(AT(b, 0x130, Obj), l); }
    }
    float cnh[MPM_CHOICES] = { 0 };                                         /* host choices: second line on, its height (0 = none) */
    for (int k = 0; k < MPM_CHOICES; k++) {
        Obj b = tb(mpm_choice_btn(k)), nt = u_gc_get(h_cnote[k]);
        const char *n = !b || !u_on(b) ? NULL : k < hcl.n ? mpm_choice_note(hcl.c[k].id) : NULL;
        if (!u_alive(nt)) continue;
        u_active(nt, n != NULL);
        if (!n) continue;
        tmp_set(nt, n);
        float h = txt_fit_h(nt, cnote_x[k], CN_H);                            /* CN_H, or more when it wraps even at 0.75 */
        if (h <= CN_H && lang_cjk()) { float th = txt_text_h(nt, cnote_x[k]); if (th > h && th < 2 * CN_H) h = (float)(int)(th + 0.99f); }   /* ja 14: one line is ~20 high (it sat on the frame) */
        cnh[k] = h > CN_H ? h : CN_H;
    }
    for (int i = 0; i < NT; i++) { Obj b = tb(title_ids[i]); int k = mpm_choice_of_btn(title_ids[i]);   /* rows grow to a long label */
                                   if (b && u_on(b)) trow_fit(title_ids[i], b, k >= 0 ? cnh[k] : 0); }
    for (int k = 0; k < MPM_CHOICES; k++) {                                   /* label up by half the line, the line under it */
        Obj b = tb(mpm_choice_btn(k)), l = b ? AT(b, 0x130, Obj) : NULL, nt = u_gc_get(h_cnote[k]);
        if (!b || !u_on(b) || !mm_is(u_tr(l), K_RT)) continue;
        int id = mpm_choice_btn(k);
        float H = trow_cur[id] > 1 ? trow_cur[id] : trow_base[id], up = cnh[k] > 0 ? cnh[k] / 2 - 1 : 0;
        F(0x5106F2C, void (*)(Obj, V2, void *))(u_tr(l), (V2){ cnote_ap0[k].x, cnote_ap0[k].y + up }, NULL);
        if (cnh[k] > 0 && u_alive(nt)) place(nt, 0, -H / 2 + 6 + cnh[k] / 2, cnote_x[k], cnh[k]);
    }
    if (M.state == MPM_TITLE_HOST) {                                        /* Rules box: closed = button + summary (mockup A), open = button (B) */
        int open = M.settings_open; Obj b = tb(MPB_SETTINGS), l = b ? AT(b, 0x130, Obj) : NULL;
        if (b) row_height(b, 262, open ? 52 : 104);
        float yb = open ? 0 : 30;
        if (u_alive(l)) { place(l, 0, yb, 262, 44); txt_style(l, 0, open ? 20 : 18, 513, 0); txt_color(l, C_MAIN); { char rl[96]; snprintf(rl, sizeof rl, "<margin-left=46>%s</margin>", TR("Rules")); tmp_set(l, rl); } }
        Obj cg = u_gc_get(h_title_cog); if (u_alive(cg)) { img_set(cg, spr(S_COG), C_WHITE); u_active(cg, spr(S_COG) != NULL); place(cg, -108, yb, 22, 22); }
        Obj pc = u_gc_get(h_title_pcog); if (u_alive(pc)) { u_active(pc, spr(S_COG) == NULL); place(pc, -108, yb, 22, 22); }   /* stock not loaded: pixel cog */
        Obj mo = u_gc_get(h_title_more); if (u_alive(mo)) { place(mo, 113, yb, 20, 30); txt_color(mo, open ? C_GOLD : C_SUB); }
        Obj sm = u_gc_get(h_title_sum); if (u_alive(sm)) { char t[512]; mpm_settings_summary(&M.set, t, sizeof t); tmp_set(sm, t); u_active(sm, !open); }
        Obj bx = u_gc_get(h_title_box); if (u_alive(bx) && mm_is(bx, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(bx, open ? (Col){ 1.25f, 1.2f, 1.2f, 1 } : C_WHITE, NULL);   /* open: lighter */
        if (bx && mm_is(bx, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bx, true, NULL);
    }
    /* owner v12 item 5: the Rules rows in their own panel beside the host page, always open (no cog click, no Done) */
    rp_update(&rp_title, &M.set, M.state == MPM_TITLE_HOST && !M.set.fe && M.choice == 0);   /* a host choice: its panel or the FE rules */
    fe_update(&rp_fe, &M.set, M.state == MPM_TITLE_HOST && M.set.fe);
    u_active(tb(MPB_RULES_DONE), false); u_active(tb(MPB_FE_DONE), false);
    /* Recent worlds (Multiplayer page) */
    Obj rw = u_gc_get(h_rw_root);
    int rwon = M.state == MPM_TITLE_MENU && M.nhist > 0;
    if (u_alive(rw)) u_active(rw, rwon);
    if (rwon && u_alive(rw) && frames % 10 == 0) {
        int shown = 0;
        for (int i = 0; i < 6; i++) {
            int k = M.hist_page * 6 + i; Obj b = tb(MPB_HIST0 + i);
            u_active(b, k < nhist);
            if (k >= nhist || !b) continue;
            shown++;
            hist_t *h = &hist[k]; char t[600], pl[400] = ""; size_t o = 0;
            u_active(tb(MPB_HISTW0 + i), strcmp(h->how, "host") != 0);        /* a world this copy hosted: no Watch */
            { char dy[64]; snprintf(dy, sizeof dy, TR("Day %lld"), h->day);
              snprintf(t, sizeof t, "%s  <size=72%%><color=#A69085>%s</color></size>", h->world[0] ? h->world : TR("A world"), dy); }
            tmp_set(u_gc_get(h_rw_l1[i]), t);
            char tmp[240]; snprintf(tmp, sizeof tmp, "%s", h->players); char *save = NULL;
            for (char *tok = strtok_r(tmp, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {   /* "Name|id" (older: "Name|#colour") */
                char *bar = strchr(tok, '|'); if (bar) *bar = 0;
                const char *col = bar && bar[1] >= '0' && bar[1] <= '9' ? palette_color(atoi(bar + 1)) : "#CCBEB8";   /* old hex / none: neutral */
                char nm1[64]; { int cp = 0; size_t q = 0, cut = 0;                 /* a long name: 15 characters + "..." (it filled 2 lines) */
                  for (; tok[q]; q++) if (((unsigned char)tok[q] & 0xC0) != 0x80 && ++cp == 16) cut = q;
                  if (cut && cp > 16 && cut < sizeof nm1 - 4) snprintf(nm1, sizeof nm1, "%.*s\xE2\x80\xA6", (int)cut, tok); else snprintf(nm1, sizeof nm1, "%s", tok); }
                o += (size_t)snprintf(pl + o, sizeof pl - (o < sizeof pl ? o : sizeof pl), "%s<color=%s>%s</color>%s", o ? TR(", ") : "", col, nm1,
                                      !strcmp(h->how, "host") || strcmp(tok, h->host) ? "" : TR(" (host)"));
            }
            if (!strcmp(h->how, "host")) { if (pl[0]) snprintf(t, sizeof t, TR("You hosted \xC2\xB7 with %s"), pl); else snprintf(t, sizeof t, "%s", TR("You hosted")); }
            else if (!strcmp(h->how, "steam")) { if (pl[0]) snprintf(t, sizeof t, TR("Steam \xC2\xB7 with %s"), pl); else snprintf(t, sizeof t, "%s", TR("Steam")); }
            else { if (pl[0]) snprintf(t, sizeof t, TR("IP %s \xC2\xB7 with %s"), h->addr, pl); else snprintf(t, sizeof t, TR("IP %s"), h->addr); }
            tmp_set(u_gc_get(h_rw_l2[i]), t);
            { char lab[96] = ""; int cnt = 0; for (const char *q = h->units; *q; q++) if (*q == ',') cnt++; if (h->units[0]) cnt++;
              if (cnt > 3) snprintf(lab, sizeof lab, TR("Your units (+%d)"), cnt - 3); else if (cnt) snprintf(lab, sizeof lab, "%s", TR("Your units"));
              tmp_set(u_gc_get(h_rw_ul[i]), lab); tmp_set(u_gc_get(h_rw_u[i]), ""); }   /* "[box] Name" for 3, "+N" in the label */
            { char ub[200]; snprintf(ub, sizeof ub, "%s", h->units); char *sv = NULL, *nm[6]; int nn = 0;
              for (char *tk = strtok_r(ub, ",", &sv); tk && nn < 6; tk = strtok_r(NULL, ",", &sv)) { while (*tk == ' ') tk++; nm[nn++] = tk; }
              char pb[400]; snprintf(pb, sizeof pb, "%s", h->portraits); char *sv2 = NULL, *pn[6] = { "", "", "", "", "", "" }; int np = 0;
              for (char *p0 = pb, *cm; np < 6 && p0; p0 = cm ? cm + 1 : NULL) { cm = strchr(p0, ','); if (cm) *cm = 0; pn[np++] = p0; }
              (void)sv2;
              int shownu = nn > 3 ? 3 : nn;                                 /* room for 3 "[box] Name"; more: "+N" */
              for (int q = 0; q < 6; q++) {                                 /* right-aligned: the last box is the rightmost */
                  int idx = q - (6 - shownu); Obj fr = u_gc_get(h_rw_pf[i][q]), nt = u_gc_get(h_rw_pn[i][q]);
                  u_active(fr, idx >= 0); u_active(nt, idx >= 0);
                  if (idx >= 0) { char nb[64]; snprintf(nb, sizeof nb, "%s%s", nm[idx], idx == shownu - 1 && nn > 3 ? "" : ""); tmp_set(nt, nb); }
                  if (idx >= 0) { char ini[8] = ""; unsigned char c0 = (unsigned char)nm[idx][0]; int L = c0 < 0x80 ? 1 : c0 < 0xE0 ? 2 : c0 < 0xF0 ? 3 : 4;
                                  snprintf(ini, sizeof ini, "%.*s", L, nm[idx]); tmp_set(u_gc_get(h_rw_pi[i][q]), ini);
                                  Obj ps = idx < np ? portrait_sprite(pn[idx]) : NULL, pim = u_gc_get(h_rw_pp[i][q]);   /* the portrait when loaded */
                                  if (pim) { u_active(pim, ps != NULL); if (ps) img_set(pim, ps, C_WHITE); }
                                  u_active(u_gc_get(h_rw_pi[i][q]), ps == NULL); }
              } }
        }
        for (int i = 0; i < 6; i++) if (M.hist_page * 6 + i >= nhist) u_active(tb(MPB_HISTW0 + i), false);
        u_active(tb(MPB_HIST_PREV), M.hist_page > 0); u_active(tb(MPB_HIST_NEXT), (M.hist_page + 1) * 6 < nhist);
        /* the panel's height follows the rows shown; a row grows by what its players line needs beyond one line, the footer
           by what its note needs (ja at its 14 floor: both wrapped onto the borders) */
        /* the players line ends 8 before the leftmost unit box (it ran under the boxes); a wrapped line gets 8 more under it
           (at 960 x 540 its second line sat on the row's bottom border) */
        float rh[6], h2[6], w2[6], rows = 0;
        for (int i = 0; i < 6; i++) {
            int nu = 0; for (int k = 0; k < 6; k++) if (u_on(u_gc_get(h_rw_pf[i][k]))) nu++;
            w2[i] = nu ? 368 - 90 - 15 - 96 * (nu - 1) - 66 - 15 - 8 - (-368 + 14) : 380;
            Obj l2 = u_gc_get(h_rw_l2[i]); h2[i] = i < shown && l2 ? txt_fit_h(l2, w2[i], 22) : 22; if (h2[i] < 22) h2[i] = 22;
            rh[i] = 66 + h2[i] - 22 + (h2[i] > 23 ? 8 : 0); if (i < shown) rows += rh[i] + 6;
        }
        if (!shown) rows = 72;
        Obj rnt = u_gc_get(h_rw_note); float nw = M.hist_page > 0 || (M.hist_page + 1) * 6 < nhist ? 500 : 728;   /* no Newer / Older: the full width */
        float nh = rnt ? txt_fit_h(rnt, nw, 20) : 20; if (nh < 20) nh = 20;
        float fh = 40 + nh - 20, H = 56 + 12 + rows + fh;
        place(rw, 103, 397 - H / 2, 760, H);
        place(u_gc_get(h_rw_band), 0, H / 2 - 28, 756, 54); place(u_gc_get(h_rw_line), 0, H / 2 - 56, 756, 1);
        Obj tt = u_gc_get(h_rw_title); if (tt) { place(tt, 0, H / 2 - 28, 700, 44); tmp_set(tt, TR("Recent Worlds")); }
        { float y = 68;
          for (int i = 0; i < 6; i++) {                                     /* row parts from the row's top (as built for 66) */
              Obj b = tb(MPB_HIST0 + i); if (!b) continue;
              float T = rh[i] / 2;
              place(b, 0, H / 2 - (y + T), 736, rh[i]); y += rh[i] + 6;
              place(u_gc_get(h_rw_l1[i]), -368 + 14 + 230, T - 21, 460, 28);
              place(u_gc_get(h_rw_l2[i]), -368 + 14 + w2[i] / 2, T - 36 - h2[i] / 2, w2[i], h2[i]);
              place(u_gc_get(h_rw_ul[i]), 368 - 80 - 110, T - 23, 220, 20);   /* 2 lower than built: off the frame's top (ja) */
              place(u_gc_get(h_rw_u[i]), 368 - 80 - 110, T - 45, 220, 22);
              for (int k = 0; k < 6; k++) { float px = 368 - 90 - 15 - 96 * (5 - k) - 66;
                  place(u_gc_get(h_rw_pf[i][k]), px, T - 45, 30, 30); place(u_gc_get(h_rw_pn[i][k]), px + 15 + 4 + 31, T - 45, 62, 20); }
              if (tb(MPB_HISTX0 + i)) place(tb(MPB_HISTX0 + i), 368 - 22, T - 18, 26, 26);
              if (tb(MPB_HISTW0 + i)) place(tb(MPB_HISTW0 + i), 368 - 10 - 40, T - 49, 80, 26);
          } }
        place(rnt, -380 + 16 + nw / 2, -H / 2 + fh / 2, nw, nh);
        if (tb(MPB_HIST_PREV)) place(tb(MPB_HIST_PREV), 380 - 16 - 160, -H / 2 + fh / 2, 100, 26);
        if (tb(MPB_HIST_NEXT)) place(tb(MPB_HIST_NEXT), 380 - 16 - 52, -H / 2 + fh / 2, 100, 26);
    }
    int join = M.state == MPM_TITLE_JOIN, hostp = M.state == MPM_TITLE_HOST;
    if ((join || hostp) && tb(MPB_MULTIPLAYER)) fields_find(u_tr_parent(u_tr(tb(MPB_MULTIPLAYER))));
    Obj af = u_gc_get(h_field), nf = u_gc_get(h_namefield);
    if ((join || hostp) && frames % 10 == 0 && tb(MPB_LABEL_ADDR) && tb(MPB_LABEL_NAME)) {   /* order: label, field, label, field, buttons */
        Obj la = u_tr(tb(MPB_LABEL_ADDR)), ln = u_tr(tb(MPB_LABEL_NAME));
        if (u_alive(af) && u_sibling(u_tr(af)) != u_sibling(la) + 1) u_set_sibling(u_tr(af), u_sibling(la) + 1);
        if (u_alive(nf) && u_sibling(u_tr(nf)) != u_sibling(ln) + 1) u_set_sibling(u_tr(nf), u_sibling(ln) + 1);
    }
    u_active(af, join);
    u_active(nf, join || hostp);
    if ((join || hostp) && u_on(nf)) { char nm[64]; field_text(h_namefield, nm, sizeof nm); if (nm[0]) snprintf(my_name, sizeof my_name, "%s", nm); }   /* the title scene goes away on load: keep the typed name */
    if (join || hostp) {                                                    /* a changed text clears an old error (ux #16) */
        char ta[256], tn[64]; field_text(h_field, ta, sizeof ta); field_text(h_namefield, tn, sizeof tn);
        if (fields_seen && (strcmp(ta, seen_addr) || strcmp(tn, seen_name))) fields_edited = 1;
        snprintf(seen_addr, sizeof seen_addr, "%s", ta); snprintf(seen_name, sizeof seen_name, "%s", tn); fields_seen = 1;
    } else fields_seen = 0;
}
void mpmenu_title_init(void *view) { (void)view; h_title_built = 0; }      /* stock InitOnActivate ran: rebuild on next frame */
int mpmenu_title_click(void *btn) {
    for (int i = 0; i < NT; i++) if (btn && btn == tb(title_ids[i])) { pending_click = title_ids[i]; return 1; }
    for (int i = 0; i < NA; i++) if (btn && btn == tb(arrow_ids[i])) { pending_click = arrow_ids[i]; return 1; }
    for (int i = 0; i < (int)(sizeof hist_ids / sizeof hist_ids[0]); i++) if (btn && btn == tb(hist_ids[i])) { pending_click = hist_ids[i]; return 1; }
    for (int i = MPB_FED_PREV; i < MPB_COUNT; i++) if (btn && btn == tb(i)) { pending_click = i; return 1; }   /* Foreign Expedition panels / windows */
    return 0;
}
static void (*o_title_click)(Obj, Obj, void *);
static void h_title_click(Obj btn, Obj ev, void *mi) {                     /* SubUI_TitleMenuButton.OnPointerClick */
    if (mpmenu_title_click(btn)) return;                                    /* our clone: no stock action */
    o_title_click(btn, ev, mi);
}

/* ---------------------------------------------------------------- Esc-menu clones / lobby panel */
/* stock parts (design report 2026-10-13, section 2.3): prefabs from UIFactory (+0x20 prefab table, 0xD3236C by PopUpType),
 * Transform.Find 0x510F3C0, Image.sprite get 0x5266F04 / set 0x5257600, TMP_Text.font get 0x4F0A59C / set 0x4F0A5A4 */
static Obj tr_find(Obj t, const char *path) { return u_alive(t) ? (mm_is(t, K_TR) ? F(0x510F3C0, Obj (*)(Obj, Obj, void *))(t, u_str(path), NULL) : NULL) : NULL; }
static Obj popup_prefab(int type) {
    static uintptr_t h; static void *k;
    Obj fac = find_view_x(&h, &k, "Refactor.Main.UI", "UIFactory", true), so = fac ? AT(fac, 0x20, Obj) : NULL;
    return so ? F(0xD3236C, Obj (*)(Obj, int, void *))(so, type, NULL) : NULL;
}
Obj first_image(Obj tr, int depth) {                              /* the button's background (root has none) */
    Obj im = image_of(tr); if (im) return im;
    if (depth >= 3) return NULL;
    int k = (mm_is(tr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(tr, NULL) : 0);
    for (int i = 0; i < k; i++) { Obj c = first_image((mm_is(tr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(tr, i, NULL) : NULL), depth + 1); if (c) return c; }
    return NULL;
}
Obj eb(int id) { Obj b = u_gc_get(h_eb[id]); return u_alive(b) ? b : NULL; }
/* owner lan_v8: our Esc buttons (and the reason line) make the column taller downward, so the menu sat low (seed lines
 * near the screen bottom). The group (logo + version, buttons, seed lines) moves up by half of what we add: centred as
 * the stock menu. Moved: the lowest common parent of those parts, or, if that is the view itself (its dim layer), each of
 * its children holding a part. The applied shift is kept in local units (a resolution change keeps it right). */
#define ESC_MOVE_MAX 6
static uintptr_t h_emove[ESC_MOVE_MAX]; static int n_emove; static float emove_base[ESC_MOVE_MAX];   /* stock local y */
static void esc_shift_reset(void) { for (int i = 0; i < n_emove; i++) { u_gc_free(h_emove[i]); h_emove[i] = 0; } n_emove = 0; }
static V3 tr_lpos(Obj t) { return mm_is(t, K_TR) ? F(0x51074F4, V3 (*)(Obj, void *))(t, NULL) : (V3){ 0, 0, 0 }; }
static int is_under(Obj t, Obj anc) { for (int d = 0; t && d < 32; d++, t = u_tr_parent(t)) if (t == anc) return 1; return 0; }
static void esc_shift_find(Obj v, Obj holder) {
    Obj parts[4] = { u_tr(AT(v, 0x80, Obj)), holder, u_tr(AT(v, 0x88, Obj)), u_tr(AT(v, 0x90, Obj)) };
    Obj lca = parts[1];
    while (lca) { int all = 1; for (int i = 0; i < 4; i++) if (parts[i] && !is_under(parts[i], lca)) all = 0; if (all) break; lca = u_tr_parent(lca); }
    if (!lca) return;
    if (lca != u_tr(v)) { emove_base[n_emove] = tr_lpos(lca).y; h_emove[n_emove++] = u_gc_new(lca); return; }
    for (int i = 0; i < 4; i++) {                                           /* the view itself: its children that hold a part */
        Obj c = parts[i]; if (!c) continue;
        while (c && u_tr_parent(c) != lca) c = u_tr_parent(c);
        int dup = 0; for (int k = 0; k < n_emove; k++) if (u_gc_get(h_emove[k]) == c) dup = 1;
        if (c && !dup && n_emove < ESC_MOVE_MAX) { emove_base[n_emove] = tr_lpos(c).y; h_emove[n_emove++] = u_gc_new(c); }
    }
}
/* add_world: height our parts add (world units). Each mover's local y = its stock y + half of that, in its parent's units */
static float esc_up;                                                        /* screen px the Esc column goes up more (esc_sync: UI scale 1.5) */
static void esc_shift_apply(float add_world) {
    for (int i = 0; i < n_emove; i++) {
        Obj t = u_gc_get(h_emove[i]), pa = t ? u_tr_parent(t) : NULL; if (!mm_is(t, K_TR) || !mm_is(pa, K_TR)) continue;
        V3 ls = F(0x510F4F8, V3 (*)(Obj, void *))(pa, NULL); if (ls.y <= 0) continue;
        V3 p = tr_lpos(t); float want = emove_base[i] + add_world / 2 / ls.y;
        if (p.y - want > 0.25f || want - p.y > 0.25f) { p.y = want; F(0x5107724, void (*)(Obj, V3, void *))(t, p, NULL); }
    }
}
/* an Esc-menu button clone: its label 8 in from each side, shrinking to fit (ru "Выйти из мультиплеера" ran past the frame) */
static void esc_label_fit(Obj b) {
    Obj l = u_alive(b) ? AT(b, 0x28, Obj) : NULL; if (!mm_is(l, K_TMP)) return;
    float fs = F(0x4F0B338, float (*)(Obj, void *))(l, NULL);              /* TMP_Text.fontSize: the stock size */
    if (fs >= 4) txt_fit(l, fs, 0);
    if (mm_is(u_tr(l), K_RT)) { V2 sd = F(0x510701C, V2 (*)(Obj, void *))(u_tr(l), NULL); sd.x -= 16; F(0x5107110, void (*)(Obj, V2, void *))(u_tr(l), sd, NULL); }
}
static void esc_build(Obj v) {
    Obj src = AT(v, 0x70, Obj), before = AT(v, 0x78, Obj), ver = AT(v, 0x80, Obj);   /* clone "Report bug", place before "Quit" */
    if (!u_alive(src) || !u_alive(before)) { rig_log("mpmenu: Esc-menu buttons not found"); return; }
    Obj parent = u_tr_parent(u_tr(src));
    int at = u_sibling(u_tr(before));
    for (int i = 0; i < NE; i++) {
        Obj b = u_instantiate(src, parent);
        if (!u_alive(b)) { rig_log("mpmenu: Esc clone failed"); return; }
        (mm_is(b, K_SMB) ? F(0xF2DB60, void (*)(Obj, Obj, void *))(b, NULL, NULL) : (void)0);            /* Init(null): adds the clone's own onClick listener */
        AT(b, 0x30, Obj) = NULL;                                            /* (runtime listeners are not cloned) */
        esc_label_fit(b);
        u_set_sibling(u_tr(b), at + i);
        u_active(b, false);
        u_gc_free(h_eb[esc_ids[i]]); h_eb[esc_ids[i]] = u_gc_new(b);
    }
    esc_shift_reset(); esc_shift_find(v, parent);
    rig_log("mpmenu: Esc menu centring moves %d part(s)", n_emove);
    if (u_alive(ver)) {
        /* lobby info: a clone of the version line, OUTSIDE the button holder's layout group (parent = the holder's
         * parent, Transform_ScaleAdjust), behind it a dark panel cloned from the menu's letterbox image */
        Obj holder = u_tr_parent(u_tr(src)), area = u_tr_parent(holder);
        Obj lb = NULL;
        int k = (mm_is(u_tr(v), K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(u_tr(v), NULL) : 0);
        for (int i = 0; i < k && !lb; i++) {
            Obj c = (mm_is(u_tr(v), K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(v), i, NULL) : NULL); char nm[64] = "";
            str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(c, NULL), nm, sizeof nm);
            if (strstr(nm, "LetterBox")) lb = c;
        }
        /* frame: the stock notice box (CommonNoticeView Panel_Notice/Img_BackGroundBox, sprite Box), its buttons off;
         * fallback the letterbox image, tinted */
        static uintptr_t h_cnv2; static void *k_cnv2;
        Obj cn = find_view_x(&h_cnv2, &k_cnv2, "Refactor.UI", "CommonNoticeView", true);
        Obj box = cn ? tr_find(u_tr(cn), "Panel_Notice/Img_BackGroundBox") : NULL;
        Obj bg = u_alive(box) ? u_instantiate(box, area) : u_alive(lb) ? u_instantiate(lb, area) : NULL;
        if (u_alive(box) && u_alive(bg)) {
            int nc = (mm_is(u_tr(bg), K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(u_tr(bg), NULL) : 0);
            for (int i = 0; i < nc; i++) u_go_active(u_go((mm_is(u_tr(bg), K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(bg), i, NULL) : NULL)), false);
        }
        lobby_boxed = u_alive(box) && u_alive(bg);
        Obj t = u_instantiate(ver, area);
        Obj tt = u_instantiate(ver, area);                                  /* title: Cinzel-Bold from the Recruit popup's title */
        Obj rp = popup_prefab(8), rt = rp ? tr_find(u_tr(rp), "Txt_Notice") : NULL, rtt = NULL;
        if (u_alive(rt)) { static void *kt; if (!kt) kt = class_by_name("TMPro", "TMP_Text");
            Obj ty = kt ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(kt)) : NULL;
            rtt = ty ? (mm_is(rt, K_COMP) ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(rt, ty, NULL) : NULL) : NULL; }
        Obj cinzel = title_font();                                          /* i18n: the language's title font; else the popup's (Cinzel) */
        if (!cinzel) cinzel = u_alive(rtt) ? (mm_is(rtt, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(rtt, NULL) : NULL) : NULL;
        if (u_alive(tt) && u_alive(cinzel)) (mm_is(tt, K_TMP) ? F(0x4F0A5A4, void (*)(Obj, Obj, void *))(tt, cinzel, NULL) : (void)0);
        if (u_alive(tt)) u_active(tt, false);
        u_gc_free(h_etitle); h_etitle = u_alive(tt) ? u_gc_new(tt) : 0;
        /* Start = primary action: the Recruit button's sprite (RecruitButton, red) on the clone's background; the label keeps the
         * menu size of the other buttons (lead 2026-10-13) */
        Obj rb = rp ? tr_find(u_tr(rp), "Img_BackGroundBox/CandidateDetailPanel/Img_RecruitButton") : NULL;
        Obj rspr = u_alive(rb) && image_of(rb) ? (mm_is(image_of(rb), K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(image_of(rb), NULL) : NULL) : NULL;
        Obj sb = eb(MPB_START), sim = sb ? first_image(u_tr(sb), 0) : NULL;
        if (u_alive(sim) && u_alive(rspr)) (mm_is(sim, K_IMG) ? F(0x5257600, void (*)(Obj, Obj, void *))(sim, rspr, NULL) : (void)0);
        rig_log("mpmenu: lobby parts: notice box %p, Cinzel %p, RecruitButton sprite %p on Start %p", box, cinzel, rspr, sim);
        if (u_alive(bg)) { u_active(bg, false); { Obj im = image_of(bg); if (im) (mm_is(im, K_GR) ? F(0x5260348, void (*)(Obj, bool, void *))(im, false, NULL) : (void)0); } }
        if (u_alive(t)) u_active(t, false);
        u_gc_free(h_etext); h_etext = u_alive(t) ? u_gc_new(t) : 0;
        u_gc_free(h_ebg); h_ebg = u_alive(bg) ? u_gc_new(bg) : 0;
        layout_done = 0;
    }
    Obj seed = AT(v, 0x88, Obj);                                            /* design #7: why Load is off, a seed-style line */
    if (u_alive(seed)) {
        Obj r = u_instantiate(seed, u_tr_parent(u_tr(seed)));
        if (u_alive(r)) { { char lr[200]; snprintf(lr, sizeof lr, "<color=#A69085>%s</color>", TR("Leave multiplayer to load another world")); tmp_set(r, lr); }
            /* one line: as wide as its text (the seed line's width wrapped de "zu laden" alone onto a second line, onto the seed) */
            if (mm_is(r, K_TMP) && fit_methods() && mm_is(u_tr(r), K_RT)) {
                float fs = F(0x4F0B338, float (*)(Obj, void *))(r, NULL), fl = lang_cjk() ? 14 : 12;   /* the seed's 12: ja unreadable at 960 */
                if (fs > 1 && fs < fl) { F(0x4F0B5DC, void (*)(Obj, bool, void *))(r, false, NULL); F(0x4F0B340, void (*)(Obj, float, void *))(r, fl, NULL); }
                V2 sd = F(0x510701C, V2 (*)(Obj, void *))(u_tr(r), NULL), s0 = sd; float pw = fit_pref(r, 0).x + 8;
                if (pw > sd.x) sd.x = pw > 640 ? 640 : pw;
                float ph = fit_pref(r, sd.x).y; if (ph > sd.y) sd.y = (float)(int)(ph + 0.99f);   /* 14 is taller than the seed's rect */
                if (sd.x != s0.x || sd.y != s0.y) F(0x5107110, void (*)(Obj, V2, void *))(u_tr(r), sd, NULL); }
            u_active(r, false); }
        u_gc_free(h_ereason); h_ereason = u_alive(r) ? u_gc_new(r) : 0;
    }
    ph_sibling = -1;
    u_gc_free(h_esc_built); h_esc_built = u_gc_new(v);
    esc_lobby_mode = 0;
    rig_log("mpmenu: Esc-menu clones added at sibling %d", at);
}
static void esc_load_rule(Obj v);
static int in_session(void);
static void lw_sync(Obj v, int lobby);
static void esc_sync(const mpm_obs *o) {
    Obj v = o->scene == 2 ? esc_view() : NULL;
    if (!v || !u_on(v)) return;
    if (u_gc_get(h_esc_built) != v || !eb(MPB_LEAVE)) esc_build(v);
    esc_load_rule(v);
    mpm_obs oo = *o; oo.esc_open = 1;
    uint64_t vis = mpm_visible(&M, &oo);
    int lobby = M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY;
    if (lobby != esc_lobby_mode) {                                          /* lobby: the stock buttons go away */
        for (int off = 0x48; off <= 0x78; off += 8) if (off != 0x58) u_active(AT(v, off, Obj), !lobby);
        esc_lobby_mode = lobby;
    }
    for (int i = 0; i < NE; i++) {
        Obj b = eb(esc_ids[i]); if (!b) continue;
        int on = (vis >> esc_ids[i]) & 1;
        u_active(b, on);
        if (on) { char l[512]; mpm_label(&M, esc_ids[i], l, sizeof l); tmp_set(AT(b, 0x28, Obj), l); }
    }
    /* in game: our buttons grow the menu downward; the stock seed lines (Txt_CampaignSeed +0x88, Txt_DungeonSeed +0x90)
     * move down by as many button steps as we added, so they never sit between buttons */
    {   /* centre: half of what we add (our visible buttons, one button step each, and the reason line) goes up */
        float add = 0, r[4]; Obj ref = AT(v, 0x48, Obj), rl = u_gc_get(h_ereason);
        if (!lobby && u_on(ref) && rect_of(ref, r)) {
            Obj nx = AT(v, 0x50, Obj); float r2[4];
            float step = u_on(nx) && rect_of(nx, r2) ? r[1] - r2[1] : (r[3] - r[1]) * 1.25f;   /* Return -> Save: one step */
            for (int i = 0; i < NE; i++) if (u_on(eb(esc_ids[i]))) add += step;
            float rr[4]; if (in_session() && u_alive(rl) && rect_of(rl, rr)) add += rr[3] - rr[1] + 2;
        }
        esc_shift_apply(lobby ? 0 : add);   /* the lobby hides the column: stock place (the esc_up lift is off: at UI scale 1.5 it pushed the host menu logo off the top) */
    }
    if (!lobby) {
        /* the seed lines go right under the lowest visible button (stock ones and ours), keeping their own spacing */
        float low = 1e9f, bh = 0, high = -1e9f, r[4];
        for (int off = 0x48; off <= 0x78; off += 8) if (off != 0x58) { Obj b = AT(v, off, Obj); if (u_on(b) && rect_of(b, r)) { if (r[1] < low) { low = r[1]; bh = r[3] - r[1]; } if (r[3] > high) high = r[3]; } }
        for (int i = 0; i < NE; i++) { Obj b = eb(esc_ids[i]); if (u_on(b) && rect_of(b, r)) { if (r[1] < low) { low = r[1]; bh = r[3] - r[1]; } if (r[3] > high) high = r[3]; } }
        Obj l0 = AT(v, 0x88, Obj), l1 = AT(v, 0x90, Obj); float s0[4];
        Obj rl = u_gc_get(h_ereason); int show_r = in_session() && u_alive(rl);
        u_active(rl, show_r);
        float cursor = low - bh * 0.3f;
        if (show_r && low < 1e8f) {                                         /* design #7: the reason line first, then the seeds */
            float rr[4];
            if (rect_of(rl, rr)) {
                float d = cursor - rr[3];
                if (d < -0.5f || d > 0.5f) { V3 p = (mm_is(u_tr(rl), K_TR) ? F(0x5109A08, V3 (*)(Obj, void *))(u_tr(rl), NULL) : (V3){0, 0, 0}); p.y += d; (mm_is(u_tr(rl), K_TR) ? F(0x5109B04, void (*)(Obj, V3, void *))(u_tr(rl), p, NULL) : (void)0); }
                cursor = rr[1] + d - 4;                                    /* a small gap before the seed lines */
            }
        }
        if (low < 1e8f && u_alive(l0) && rect_of(l0, s0)) {
            float top = s0[3], r1[4];
            if (u_on(l1) && rect_of(l1, r1) && r1[3] > top) top = r1[3];
            float delta = cursor - top;                                    /* < 0: move down; > 0: move up to sit close */
            if (delta < -0.5f || delta > 0.5f)
                for (int k = 0; k < 2; k++) {
                    Obj l = AT(v, 0x88 + 8 * k, Obj); if (!u_alive(l)) continue;
                    V3 p = (mm_is(u_tr(l), K_TR) ? F(0x5109A08, V3 (*)(Obj, void *))(u_tr(l), NULL) : (V3){0, 0, 0}); p.y += delta;
                    (mm_is(u_tr(l), K_TR) ? F(0x5109B04, void (*)(Obj, V3, void *))(u_tr(l), p, NULL) : (void)0);
                }
        }
        /* UI scale 1.5: the column with our buttons ran to the window's bottom (the reason line 2 px above it, the seed line
           off it at 1280 x 720): the column goes up (esc_up, next frame) until the lowest line is 3% of the height above the
           bottom, as far as the room above it allows. At scale 1 the lines are well above it: esc_up stays 0. */
        float bot = 1e9f, sh = (float)F(0x5092CD4, int (*)(void *))(NULL);  /* Screen.height */
        for (int k = 0; k < 2; k++) { Obj l = AT(v, 0x88 + 8 * k, Obj); if (u_on(l) && rect_of(l, r) && r[1] < bot) bot = r[1]; }
        if (show_r && u_on(rl) && rect_of(rl, r) && r[1] < bot) bot = r[1];
        if (low < 1e8f && bot < 1e8f && high > -1e8f && sh > 0) {
            float m = sh * 0.03f, want = m - (bot - esc_up), room = sh - m - (high - esc_up);
            if (want > room) want = room;
            if (want < 0) want = 0;
            if (want - esc_up > 0.5f || esc_up - want > 0.5f) esc_up = want;
        }
    }
    if (lobby) u_active(u_gc_get(h_ereason), false);
    lw_sync(v, lobby);
}
/* ---------------------------------------------------------------- lobby window (approved mockups "Lobby A" / "Lobby guest") */
/* A framed window on the Esc menu's root (its dim layer stays behind; its own column, Transform_ScaleAdjust, is hidden):
 * title band, hint strip, two columns (Players + Invite | Rules with the cog), bottom bar Leave left / Start right.
 * Host 880 x 740, guest 880 x 640. Buttons are SubUI_SystemMenuButton clones (the click hook knows them, h_lwb). The cog
 * opens the Rules panel (same as the host page) over the window. Mockup px from the window's top-left -> centre units. */
enum { LW_START, LW_INVITE, LW_COPY, LW_LEAVE, LW_GEAR, LW_BRING, LW_QUIT, LW_SHOWADDR, LW_NB };   /* LW_LEAVE = "Turn off multiplayer" (host only) */
static const int lw_ids[LW_NB] = { MPB_START, MPB_INVITE, MPB_COPY_ADDR, MPB_LEAVE, MPB_LOBBY_SETTINGS, MPB_FE_BRINGWIN, MPB_LOBBY_QUIT, MPB_SHOW_ADDR };
/* owner 2026-10-20: in a Steam lobby the address block is hidden until "Show address for friends without Steam" */
static int lw_addr_shown;
uintptr_t h_lwr[MPB_COUNT];                       /* the lobby Rules panel's arrows + Done, by button id */
#define LW_ROWS 4
enum { T_TITLE, T_HINT, T_GUEST, T_PLAYERS, T_INV, T_RULES, T_INV_ADDR, T_INV_NOTE, T_RULES_NOTE, T_NOTE, T_CONN,
       T_R0L, T_R1L, T_R2L, T_R3L, T_R4L, T_R5L, T_R6L, T_R7L, T_R8L, T_R0V, T_R1V, T_R2V, T_R3V, T_R4V, T_R5V, T_R6V, T_R7V, T_R8V, T_NT };
_Static_assert(T_R0V - T_R0L == MPM_RULE_ROWS, "one label + one value text per rules row");
uintptr_t h_lw_root; static uintptr_t h_lw_view, h_lw_area, h_lwb[LW_NB], h_lwt[T_NT];
static uintptr_t h_lw_rowbg[LW_ROWS], h_lw_dot[LW_ROWS], h_lw_name[LW_ROWS], h_lw_stat[LW_ROWS], h_lw_tagbox[LW_ROWS][2], h_lw_bar[LW_ROWS][3], h_lw_x[LW_ROWS];
static uintptr_t h_lw_lines[6], h_lw_addrbox[2], h_lw_rrow[MPM_RULE_ROWS], h_lw_cog, h_lw_guestdot, h_lw_frame, h_lw_bbar, h_lw_bline, h_lw_body, h_lw_pcog, h_lw_rshade;
static char lw_rowname[LW_ROWS][32];                    /* row names (tooltip titles) */
static int lw_host_built = -1, lw_level[LW_ROWS];   /* connection level shown per row (-1 none), for the read-back */
static int64_t copied_until;
static int inet_state;                                   /* extaddr_get state for the lobby (direct_refresh) */
static int up_state;                                     /* upnpport_get state for the lobby (direct_refresh) */
static char lobby_addr[96];                              /* the address in the lobby box = what Copy copies */
static float lw_h = 740;
static float lw_k = 1;                                  /* the window's scale to fit the screen (lw_texts) */
static char lobby_err[512]; static double lobby_err_until;   /* a refused lobby Start (host choice), shown in the hint line */
#define C_WARN_HEX "#F16071"
static float lw_note_h;                                 /* steaminv: height of the note under Steam invite (ui_friends.c) */
static float lw_addr_h = 40 + 10 + 52;                  /* the address block (box + note) or its "Show address" button */
int ui_friends_invite_click(void); float ui_friends_lobby_note(float x, float y, float w, int steam);
int mp_game_conn(int player, char *addr, size_t n);      /* mp_game.c (conninfo): enum dsnet_via + the address this copy may show */
/* conninfo (owner 2026-10-20): the connection words of a players row, for the lobby and ui_game.c's Players panel.
   Host's own row: "Steam lobby" while its Steam lobby is up, else its address (internet when known, else local).
   Host about a guest: "Steam" or "Direct (ip:port)". Guest: "Steam" / "Direct"; the host's row adds the address it joined
   by address. Another guest's address is never shown (only the host has it). 0 = nothing to say. */
__attribute__((visibility("default"))) int mpmenu_conn_words(int player, char *out, size_t n) {
    if (!out || !n) return 0;
    out[0] = 0;
    char a[96]; int v = mp_game_conn(player, a, sizeof a);
    if (v == DSNET_VIA_HOST) {
        mp_steam_info in; mp_steam_state(&in);
        if (in.steam && in.lobby && in.lobby != ~0ull) snprintf(out, n, "%s", TR("Steam lobby"));
        else if (inet_state == EXTADDR_OK && M.internet[0]) snprintf(out, n, "%s", M.internet);
        else snprintf(out, n, "%s", M.direct);
    } else if (v == DSNET_VIA_STEAM) snprintf(out, n, "%s", TR("Steam"));
    else if (v == DSNET_VIA_DIRECT) { if (a[0]) snprintf(out, n, TR("Direct (%s)"), a); else snprintf(out, n, "%s", TR("Direct")); }
    return out[0] != 0;
}
static Obj lwt(int k) { Obj t = u_gc_get(h_lwt[k]); return u_alive(t) ? t : NULL; }
#define LX(x) ((x) - 440.0f)
#define LY(y) (lw_h / 2 - (y))
/* bottom bar, left part (mockup px): Quit 32..172, Turn off multiplayer 184..424 (DE / RU labels shrink to fit); Start 616..848 */
#define LW_QUIT_X 32.0f
#define LW_QUIT_W 140.0f
#define LW_OFF_X 184.0f
#define LW_OFF_W 240.0f
#define LW_MID_X 520.0f                   /* host: the space between Turn off multiplayer and Start (Bring units, the loading note) */
#define LW_MID_W 172.0f
static void lw_build(Obj v, int host) {
    if (!scan_done || !h_spr[S_COG]) assets_scan_now();
    Obj src = AT(v, 0x70, Obj);                                             /* Report bug: button template */
    if (!u_alive(src)) return;
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x28, Obj);
    lw_h = 740; lw_k = 1;             /* layout space (top-based); the frame's real height follows the content (lw_texts) */
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, 880, lw_h);   /* invisible container */
    if (!root) { rig_log("mpmenu: lobby window: no frame"); return; }
    Obj R = u_tr(root);
    { Obj fr = mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, 880, lw_h); u_gc_free(h_lw_frame); h_lw_frame = fr ? u_gc_new(fr) : 0; }
    { Obj bd = mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 1 }, 0, 0, 876, 10); u_gc_free(h_lw_body); h_lw_body = bd ? u_gc_new(bd) : 0; }   /* #222025: one body colour (the Box sprite's inner panel has a fixed lower band) */
    u_gc_free(h_lw_root); h_lw_root = u_gc_new(root);
    u_gc_free(h_lw_view); h_lw_view = u_gc_new(v);
    u_gc_free(h_lw_area); h_lw_area = u_gc_new(u_tr_parent(u_tr_parent(u_tr(src))));   /* Transform_ScaleAdjust */
    lw_host_built = host;
    #define T(k, cz, sz, al, wr, x, y, w, h, c) { Obj t_ = mk_txt(tpl_txt, R, cz, sz, al, wr, x, y, w, h, c); u_gc_free(h_lwt[k]); h_lwt[k] = t_ ? u_gc_new(t_) : 0; }
    #define KEEP(hh, o) { Obj o_ = (o); u_gc_free(hh); hh = o_ ? u_gc_new(o_) : 0; }
    /* title band 0-72, hint strip 72-122 (guest: 72-126 with a dot) */
    mk_img(tpl_img, R, -1, C_BAND, 0, LY(36), 876, 70);
    mk_img(tpl_img, R, -1, C_COPPER, 0, LY(72), 876, 1);
    T(T_TITLE, 1, 34, 514, 0, 0, LY(36), 820, 60, C_MAIN);
    mk_img(tpl_img, R, -1, C_ROW1, 0, LY(host ? 97 : 99), 876, host ? 49 : 53);
    mk_img(tpl_img, R, -1, C_ROW2, 0, LY(host ? 122 : 126), 876, 1);
    if (host) T(T_HINT, 0, 16, 513, 0, 0, LY(97), 816, 30, C_SUB)
    else { KEEP(h_lw_guestdot, mk_txt(tpl_txt, R, 0, 26, 514, 0, LX(37), LY(99), 20, 30, C_GOLD)); T(T_GUEST, 0, 18, 513, 0, LX(54 + 300), LY(99), 600, 30, C_MAIN);
           T(T_CONN, 0, 14, 513, 0, LX(32 + 198), 0, 396, 24, C_SUB); }  /* conninfo: under the players (placed in lw_texts) */
    /* section headers: Cinzel 20 gold between two lines (lines placed at refresh, once the text width is known) */
    T(T_PLAYERS, 1, 20, 514, 0, LX(32 + 198), LY(159), 200, 28, C_GOLD);
    T(T_RULES, 1, 20, 514, 0, LX(452 + 198), LY(159), 200, 28, C_GOLD);
    if (host) T(T_INV, 1, 20, 514, 0, LX(32 + 198), LY(345), 200, 28, C_GOLD);
    for (int k = 0; k < 6; k++) KEEP(h_lw_lines[k], mk_img(tpl_img, R, -1, C_LINE, 0, 0, 10, 1));
    /* players: 4 row slots, 44 high from y 182 */
    for (int i = 0; i < LW_ROWS; i++) {
        float y = LY(182 + 44 * i + 22);
        KEEP(h_lw_rowbg[i], mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, LX(32 + 198), y, 396, 44));
        KEEP(h_lw_dot[i], mk_txt(tpl_txt, R, 0, 26, 514, 0, LX(32 + 19), y, 20, 30, C_MAIN));
        KEEP(h_lw_name[i], mk_txt(tpl_txt, R, 0, 16, 513, 0, LX(32 + 36 + 120), y, 240, 30, C_MAIN));
        KEEP(h_lw_tagbox[i][0], mk_img(tpl_img, R, -1, C_LINE, 0, y, 40, 20));
        KEEP(h_lw_tagbox[i][1], mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, 0, y, 38, 18));
        KEEP(h_lw_stat[i], mk_txt(tpl_txt, R, 0, 12, 514, 0, 0, y, 60, 20, C_SUB));
        for (int k = 0; k < 3; k++) {                                       /* bars 4 wide, 6 / 11 / 16 high, gap 3, bottom-aligned */
            float bh = k == 0 ? 6 : k == 1 ? 11 : 16;
            KEEP(h_lw_bar[i][k], mk_img(tpl_img, R, -1, C_UNLIT, LX(428 - 14 - 18 + 2 + 7 * k), y - 8 + bh / 2, 4, bh));
        }
        Obj x = mk_txt(tpl_txt, R, 0, 14, 514, 0, LX(428 - 14 - 9), y, 20, 20, (Col){ 0.945f, 0.376f, 0.443f, 1 });
        if (x) tmp_set(x, "x");
        KEEP(h_lw_x[i], x);
    }
    /* invite (host): Steam invite (full width), address box + Copy, note */
    Obj b;
    if (host) {
        b = mk_button(src, R, 1, S_NBTN, C_WHITE, LX(32 + 198), 0, 396, 40, 16, TR("Steam invite"), C_MAIN); KEEP(h_lwb[LW_INVITE], b);
        KEEP(h_lw_addrbox[0], mk_img(tpl_img, R, -1, C_LINE, 0, 0, 292, 40));
        KEEP(h_lw_addrbox[1], mk_img(tpl_img, R, -1, (Col){ 0.106f, 0.098f, 0.122f, 1 }, 0, 0, 290, 38));
        T(T_INV_ADDR, 0, 16, 513, 0, 0, 0, 268, 30, C_MAIN);
        b = mk_button(src, R, 1, S_NBTN, C_WHITE, 0, 0, 96, 40, 16, TR("Copy"), C_MAIN); KEEP(h_lwb[LW_COPY], b);
        T(T_INV_NOTE, 0, 12, 513, 1, 0, 0, 396, 34, C_SUB);
        b = mk_button(src, R, 1, S_NBTN, C_WHITE, LX(32 + 198), 0, 396, 30, 14, TR("Show address for friends without Steam"), C_SUB); KEEP(h_lwb[LW_SHOWADDR], b);
    } else { u_gc_free(h_lwb[LW_SHOWADDR]); h_lwb[LW_SHOWADDR] = 0; }
    /* rules (read-only rows; the host's cog opens the Rules panel) */
    for (int i = 0; i < MPM_RULE_ROWS; i++) {
        float y = LY(188 + 40 * i + 20);
        KEEP(h_lw_rrow[i], mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, LX(452 + 198), y, 396, 40));
        T(T_R0L + i, 0, 16, 513, 0, LX(452 + 14 + 100), y, 200, 30, C_SUB);
        T(T_R0V + i, 0, 16, 516, 0, LX(848 - 14 - 110), y, 220, 30, C_MAIN);
    }
    T(T_RULES_NOTE, 0, 12, 513, 0, LX(452 + 198), LY(188 + 40 * MPM_RULE_ROWS + 16), 396, 20, C_SUB);
    if (host) {
        b = mk_button(src, R, 1, S_NBTN, C_WHITE, LX(848 - 16), LY(159), 32, 32, 16, "", C_MAIN); KEEP(h_lwb[LW_GEAR], b);
        if (b) KEEP(h_lw_cog, mk_img(tpl_img, u_tr(b), S_COG, C_WHITE, 0, 0, 20, 20));
        if (b) KEEP(h_lw_pcog, mk_pixel_cog(tpl_img, u_tr(b), 0, 0, 20));
    }
    /* bottom bar (placed at the content's height in lw_texts) */
    KEEP(h_lw_bbar, mk_img(tpl_img, R, -1, C_BAND, 0, LY(lw_h - 38), 876, 74));
    KEEP(h_lw_bline, mk_img(tpl_img, R, -1, C_COPPER, 0, LY(lw_h - 76), 876, 1));
    /* owner 2026-10-20: Quit (end the session, back to the title) left; host: Turn off multiplayer (keep playing alone) beside it */
    b = mk_button(src, R, 1, S_NBTN, C_WHITE, LX(LW_QUIT_X + LW_QUIT_W / 2), LY(lw_h - 38), LW_QUIT_W, 34, 16, host ? TR("Quit to title") : TR("Leave"), C_MAIN); KEEP(h_lwb[LW_QUIT], b);   /* guest: leaves the host's game (to the title) */
    if (host) { b = mk_button(src, R, 1, S_NBTN, C_WHITE, LX(LW_OFF_X + LW_OFF_W / 2), LY(lw_h - 38), LW_OFF_W, 34, 16, TR("Turn off multiplayer"), C_MAIN); KEEP(h_lwb[LW_LEAVE], b); }
    else { u_gc_free(h_lwb[LW_LEAVE]); h_lwb[LW_LEAVE] = 0; }
    if (host) { b = mk_button(src, R, 1, S_RECRUIT, C_WHITE, LX(848 - 116), LY(lw_h - 38), 232, 44, 18, mpm_word("TEXTKEY_Common_Start", TR_NOOP("Start")), C_START); KEEP(h_lwb[LW_START], b); }
    b = mk_button(src, R, 1, S_NBTN, C_WHITE, host ? LX(LW_MID_X) : LX(848 - 116), LY(lw_h - 38), host ? LW_MID_W : 206, 34, 16, TR("Bring units"), C_MAIN); KEEP(h_lwb[LW_BRING], b);   /* Foreign Expedition */
    if (b) u_active(b, false);
    T(T_NOTE, 0, 14, 514, 0, host ? LX(LW_MID_X) : 0, LY(lw_h - 38), host ? LW_MID_W : 300, 24, C_ORANGE);
    #undef T
    #undef KEEP
    if (host) { Obj sh = mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 1 }, 0, 0, 876, 10);   /* hides the columns behind the open Rules */
                u_gc_free(h_lw_rshade); h_lw_rshade = sh ? u_gc_new(sh) : 0; u_active(sh, false); }
    if (host) rp_build(&rp_lobby, R, src, 1, tpl_img, tpl_txt, 0, 0, TR("Saved with this world. New worlds start with your last rules."), h_lwr, 1);
    u_active(root, false);
    rig_log("mpmenu: lobby window built (%s; frame %s, Cinzel %s, cog %s)", host ? "host" : "guest", spr(S_BOX) ? "Box" : "plain", h_cinzel ? "yes" : "no", spr(S_COG) ? "yes" : "no");
}
static float txt_w(Obj t) { if (!mm_is(t, K_TMP)) return 0; V2 sz = F(0x510701C, V2 (*)(Obj, void *))(u_tr(t), NULL); F(0x5107110, void (*)(Obj, V2, void *))(u_tr(t), (V2){ 4000, sz.y }, NULL);
    float w = F(0x4F0D81C, float (*)(Obj, void *))(t, NULL); F(0x5107110, void (*)(Obj, V2, void *))(u_tr(t), sz, NULL); return w; }
/* a section header line pair around a centred title (x0 = column left, w = column width, right = space kept for a cog) */
static void lw_header_lines(int k0, Obj t, float x0, float w, float y, float right) {
    float tw = txt_w(t), cx = x0 + (w - right) / 2, l1 = cx - tw / 2 - 10 - x0, r0 = cx + tw / 2 + 10, l2 = x0 + w - right - r0;
    Obj a = u_gc_get(h_lw_lines[k0]), b = u_gc_get(h_lw_lines[k0 + 1]);
    if (a) { place(a, LX(x0 + l1 / 2), y, l1 > 0 ? l1 : 0, 1); u_active(a, true); }
    if (b) { place(b, LX(r0 + l2 / 2), y, l2 > 0 ? l2 : 0, 1); u_active(b, true); }
    place(t, LX(cx), y, tw + 4, 28);
}
static Col hexcol(const char *h) { unsigned r = 0, g = 0, b = 0; sscanf(h[0] == '#' ? h + 1 : h, "%02x%02x%02x", &r, &g, &b); return (Col){ r / 255.f, g / 255.f, b / 255.f, 1 }; }
static void lw_texts(void) {
    int host = M.state == MPM_LOBBY;
    mp_steam_info in; mp_steam_state(&in);
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    char b[1024]; const char *hn = M.host[0] ? M.host : TR("the host");   /* the lobby note: 3 lines, up to ~500 bytes in Russian (a 256 cut the Windows Allow line) */
    if (host) tmp_set(lwt(T_TITLE), TR("Multiplayer Lobby")); else { snprintf(b, sizeof b, TR("%s's Lobby"), hn); tmp_set(lwt(T_TITLE), b); }
    if (host && lobby_err[0] && mono_s() < lobby_err_until) {                 /* a refused Start: 10 s in the hint line, red */
        char e[600]; snprintf(e, sizeof e, "<color=" C_WARN_HEX ">%s</color>", lobby_err); tmp_set(lwt(T_HINT), e);
    } else { fe_mode_info fm;                                              /* the hub (owner 2026-10-21): what it is for */
        tmp_set(lwt(T_HINT), fe_mode_get(&fm) ? TR("Bring units, then press Start. In the hub: co-op expeditions or PvP matches.") : TR("Share your address, then press Start when everyone is in.")); }
    snprintf(b, sizeof b, TR("Waiting for %s to start"), hn); tmp_set(lwt(T_GUEST), b);
    if (!host && lwt(T_CONN)) {                                            /* conninfo: "Connected to <host> through Steam" / "... to <ip:port>" */
        char a[96]; int v = mp_game_conn(0, a, sizeof a);
        if (v == DSNET_VIA_STEAM) snprintf(b, sizeof b, TR("Connected to %s through Steam"), hn);
        else if (v == DSNET_VIA_DIRECT && a[0]) snprintf(b, sizeof b, TR("Connected to %s"), a);
        else b[0] = 0;
        tmp_set(lwt(T_CONN), b);
    }
    if (u_gc_get(h_lw_guestdot)) tmp_set(u_gc_get(h_lw_guestdot), "\xE2\x80\xA2");
    int n = in.nplayers > 0 ? in.nplayers : 1;
    snprintf(b, sizeof b, TR("Players (%d)"), n); tmp_set(lwt(T_PLAYERS), b); tmp_set(lwt(T_RULES), TR("Rules")); tmp_set(lwt(T_INV), TR("Invite"));
    /* players */
    char loading[64] = "";
    for (int i = 0; i < LW_ROWS; i++) {
        int shown = i < n && i < LW_ROWS;
        if (!shown) lw_rowname[i][0] = 0;
        Obj nm = u_gc_get(h_lw_name[i]), st = u_gc_get(h_lw_stat[i]), dot = u_gc_get(h_lw_dot[i]), x = u_gc_get(h_lw_x[i]);
        Obj tb0 = u_gc_get(h_lw_tagbox[i][0]), tb1 = u_gc_get(h_lw_tagbox[i][1]);
        u_active(u_gc_get(h_lw_rowbg[i]), shown); u_active(nm, shown); u_active(dot, shown); u_active(st, shown);
        int lvl = -1, tag = 0;                                              /* tag: 1 "Host" (boxed), 2 "You" */
        float y = LY(182 + 44 * i + 22);
        if (shown) {
            int id = in.nplayers > 0 ? in.players[i].id : 0, me = in.nplayers > 0 ? in.player : 0;
            const char *nmn = in.nplayers > 0 && in.players[i].name[0] && strcmp(in.players[i].name, "player") ? in.players[i].name : in.nplayers > 0 ? TR("Player") : my_name;
            char rowt[400]; snprintf(rowt, sizeof rowt, "%s", nmn); snprintf(lw_rowname[i], sizeof lw_rowname[i], "%s", nmn);
            /* line 1 the name; line 2 what a mode says ("Choosing units...": it wrapped the name over 3 lines) and how the
               player is connected */
            char mode[160] = "";
            for (int k = 0; k < nhooks; k++) { char t2[160];                /* Custom Modes (Foreign Expedition): "2 units brought" / "Choosing units..." */
                if (hooks[k].lobby_name && hooks[k].lobby_name(id, nmn, t2, sizeof t2)) {
                    size_t ln = strlen(nmn);
                    if (!strncmp(t2, nmn, ln)) { const char *r2 = t2 + ln; while (*r2 == ' ') r2++; snprintf(mode, sizeof mode, "%s", r2); }
                    else snprintf(rowt, sizeof rowt, "%s", t2);
                    break; } }
            { char cw[128] = ""; size_t rl = strlen(rowt); mpmenu_conn_words(id, cw, sizeof cw);
              if ((mode[0] || cw[0]) && rl < sizeof rowt)
                  snprintf(rowt + rl, sizeof rowt - rl, "\n%s%s<size=%d%%><color=#A69085>%s</color></size>", mode, mode[0] && cw[0] ? "  " : "", 75, cw); }   /* 75 % also for CJK: 88 % wrapped the PvP host row to 3 lines over the next row; was: 12; ja / zh / ko 14 */
            tmp_set(nm, rowt);
            { char d[48]; snprintf(d, sizeof d, "<color=%s>\xE2\x80\xA2</color>", ui_player_color(id)); tmp_set(dot, d); }   /* the player's colour */
            int ping = 0, behind = 0, state = DSNET_NET_IN_SYNC;
            if (c && in.nplayers > 0) dsnet_player_net(c, id, &ping, &behind, &state);
            int cs = state == DSNET_NET_IN_SYNC ? 0 : state == DSNET_NET_LOADING ? 1 : state == DSNET_NET_RESYNCING ? 2 : -1;   /* ui_conn_level's states */
            if (state == DSNET_NET_LOADING && !loading[0]) snprintf(loading, sizeof loading, "%s", nmn);
            tag = id == 0 ? 1 : id == me ? 2 : 0;
            if (id != me) lvl = state == DSNET_NET_ABSENT ? 0 : ui_conn_level(ping, behind, cs);
            tmp_set(st, tag == 1 ? TR("Host") : tag == 2 ? TR("You") : "");
            float right = 428 - 14 - (lvl >= 0 ? 18 + 12 : 0);              /* tag left of the bars */
            float tw = lang_cjk() ? 56 : 50, th = lang_cjk() ? 24 : 20;    /* ja "ホスト" at 14: 2 more each side (the ink touched the box) */
            place(st, LX(right - 24), y, tw - 2, th);                      /* 48: "ホスト" / "あなた" at 14 */
            place(tb0, LX(right - 24), y, tw, th); place(tb1, LX(right - 24), y, tw - 4, th - 4);   /* a 2-unit edge: 1 was lost at 960 x 540 */
            float nr = (tag ? right - 24 - tw / 2 : right) - 10;           /* the name up to 10 before the tag / the bars */
            place(nm, LX((68 + nr) / 2), y, nr - 68, 40);
        }
        u_active(tb0, shown && tag); u_active(tb1, shown && tag);         /* "Host" and "You" both boxed */
        for (int k = 0; k < 3; k++) {
            Obj bar = u_gc_get(h_lw_bar[i][k]);
            u_active(bar, shown && lvl >= 1);
            if (shown && lvl >= 1) img_set(bar, NULL, k < lvl ? hexcol(ui_conn_color(lvl)) : C_UNLIT);
        }
        u_active(x, shown && lvl == 0);
        lw_level[i] = shown ? lvl : -1;
    }
    /* header lines + invite block position (after the visible rows) */
    float rows_end = 182 + 44 * (n < LW_ROWS ? n : LW_ROWS);
    lw_header_lines(0, lwt(T_PLAYERS), 32, 396, LY(159), 0);
    if (!host && lwt(T_CONN)) place(lwt(T_CONN), LX(32 + 198), LY(rows_end + 20), 396, 24);
    lw_header_lines(2, lwt(T_RULES), 452, 396, LY(159), host ? 42 : 0);
    u_active(u_gc_get(h_lwb[LW_GEAR]), host);
    if (host) {
        float hy = rows_end + 18 + 13;
        lw_header_lines(4, lwt(T_INV), 32, 396, LY(hy), 0);
        float y1 = hy + 13 + 10 + 20;                                       /* Steam invite 40 high */
        Obj ib = u_gc_get(h_lwb[LW_INVITE]); if (ib) place(ib, LX(32 + 198), LY(y1), 396, 40);
        lw_note_h = ui_friends_lobby_note(LX(32 + 198), LY(y1 + 20 + 4 + 10), 396, in.steam);   /* steaminv: "Your friend must open ... first." */
        float y2 = y1 + 20 + 10 + 20 + lw_note_h;
        int addr_on = !in.steam || lw_addr_shown;                          /* Steam lobby: Steam invite first, the address on request */
        { Obj sa = u_gc_get(h_lwb[LW_SHOWADDR]); u_active(sa, !addr_on); if (sa) place(sa, LX(32 + 198), LY(y2 - 5), 396, 30); }
        u_active(u_gc_get(h_lw_addrbox[0]), addr_on); u_active(u_gc_get(h_lw_addrbox[1]), addr_on); u_active(lwt(T_INV_ADDR), addr_on);
        u_active(u_gc_get(h_lwb[LW_COPY]), addr_on); u_active(lwt(T_INV_NOTE), addr_on);
        lw_addr_h = addr_on ? 40 + 10 + 52 : 30;
        place(u_gc_get(h_lw_addrbox[0]), LX(32 + 146), LY(y2), 292, 40); place(u_gc_get(h_lw_addrbox[1]), LX(32 + 146), LY(y2), 290, 38);
        place(lwt(T_INV_ADDR), LX(32 + 12 + 134), LY(y2), 268, 30);
        Obj cb = u_gc_get(h_lwb[LW_COPY]); if (cb) place(cb, LX(428 - 48), LY(y2), 96, 40);
        /* owner 2026-10-20: the box shows the INTERNET address (friends outside the home) with the router's external port
           (upnpport: opened automatically / already forwarded), the note says how friends outside reach it + the local one */
        { const char *port = strrchr(M.direct, ':'); const char *pn = port ? port + 1 : "47777";
          int inet = inet_state == EXTADDR_OK && M.internet[0];
          int up_ok = up_state == UPNPP_OPENED || up_state == UPNPP_ALREADY;
          snprintf(lobby_addr, sizeof lobby_addr, "%s", inet ? M.internet : M.direct);
          tmp_set(lwt(T_INV_ADDR), lobby_addr[0] ? lobby_addr : "...");
          char l1[640];
          if (inet_state == EXTADDR_PRIVATE) snprintf(l1, sizeof l1, "%s", in.steam ? TR("Friends outside your home cannot join by address: your internet provider shares one internet address between many homes. Use Steam invite.")
                                                                              : TR("Friends outside your home cannot join by address: your internet provider shares one internet address between many homes."));
          else if (inet_state == EXTADDR_PENDING || inet_state == EXTADDR_IDLE || up_state == UPNPP_PENDING) snprintf(l1, sizeof l1, "%s", TR("Looking for your internet address..."));
          else if (inet && up_state == UPNPP_OPENED) snprintf(l1, sizeof l1, TR("Friends outside your home: %s (opened automatically on your router)."), M.internet);
          else if (inet && up_state == UPNPP_ALREADY) snprintf(l1, sizeof l1, TR("Friends outside your home: %s (your router already forwards this port)."), M.internet);
          else if (up_ok) snprintf(l1, sizeof l1, in.steam ? TR("Your router opened port %s, but your internet address is unknown. Friends outside your home: use Steam invite.")
                                                           : TR("Your router opened port %s, but your internet address is unknown."), M.internet[0] ? strrchr(M.internet, ':') + 1 : pn);
          else snprintf(l1, sizeof l1, in.steam ? TR("Your router did not open a port. For friends outside your home, use Steam invite, or forward port %s on your router to this computer (port forwarding).")
                                                : TR("Your router did not open a port. For friends outside your home, forward port %s on your router to this computer (port forwarding)."), pn);
          size_t bl = (size_t)snprintf(b, sizeof b, "%s\n", l1);
          if (bl < sizeof b) bl += (size_t)snprintf(b + bl, sizeof b - bl, TR("Same home: %s"), M.direct[0] ? M.direct : "...");
#ifdef _WIN32
          if (bl < sizeof b) snprintf(b + bl, sizeof b - bl, "\n%s", TR("If Windows asks to allow Dungeon Settlers on networks, click Allow."));
#endif
          tmp_set(lwt(T_INV_NOTE), b); }
        /* the note as tall as its text needs (3 to 5 lines: it ran over the address box and the bottom bar in Russian, and
           on Windows with the Allow line); the window grows with it */
        { float th = addr_on ? txt_text_h(lwt(T_INV_NOTE), 396) : 0, nh = th > 1 ? (float)(int)(th + 2.99f) : 52;   /* its design size: auto-sized it was unreadable at 960 x 540 */
          if (addr_on) lw_addr_h = 40 + 10 + nh;
          place(lwt(T_INV_NOTE), LX(32 + 198), LY(y2 + 20 + 10 + nh / 2), 396, nh); }
        if (cb) tmp_set(AT(cb, 0x28, Obj), frames < copied_until ? TR("Copied") : TR("Copy"));
        Obj ib2 = u_gc_get(h_lwb[LW_INVITE]);                               /* both ways in one app: Steam invite + the address */
        if (ib2) { char iv[200]; if (in.steam) snprintf(iv, sizeof iv, "%s", TR("Steam invite"));
                  else snprintf(iv, sizeof iv, "<color=#A69085>%s <size=80%%>\xC2\xB7 %s</size></color>", TR("Steam invite"), TR("Needs the Steam version"));
                  tmp_set(AT(ib2, 0x28, Obj), iv); }
    }
    /* rules: host = what he set (sent as DSNET_RULES), guest = dsnet's */
    mpm_settings ss = M.set;
    if (c) { dsnet_rules r; dsnet_rules_get(c, &r); if (!host) { ss.pause_mode = r.pause_mode; ss.speedup = r.allow_speedup; ss.gear = r.gear_mode; ss.units = r.units_per_player; ss.events = r.event_choice; ss.population = r.population; ss.fog = r.shared_fog; }
             ss.admin = dsnet_admin_mode(c); ss.spectators = dsnet_spectators_allowed(c); ss.watching = __builtin_popcount(dsnet_spectator_mask(c)); }
    int nrow = 0;
    for (int i = 0; i < MPM_RULE_ROWS; i++) {
        u_active(u_gc_get(h_lw_rrow[i]), true); u_active(lwt(T_R0L + i), true); u_active(lwt(T_R0V + i), true);
        char v[64]; mpm_rule_value(&ss, i, v, sizeof v); tmp_set(lwt(T_R0L + i), mpm_rule_label(i)); tmp_set(lwt(T_R0V + i), v);
        float y = LY(188 + 40 * nrow + 20);
        place(u_gc_get(h_lw_rrow[i]), LX(452 + 198), y, 396, 40); place(lwt(T_R0L + i), LX(452 + 14 + 100), y, 200, 30); place(lwt(T_R0V + i), LX(848 - 14 - 110), y, 220, 30);
        Obj rb = u_gc_get(h_lw_rrow[i]); if (rb) img_set(rb, NULL, nrow % 2 ? C_ROW2 : C_ROW1);
        nrow++;
    }
    tmp_set(lwt(T_RULES_NOTE), host ? TR("Only the host can change the rules.") : TR("Set by the host."));
    place(lwt(T_RULES_NOTE), LX(452 + 198), LY(188 + 40 * nrow + 16), 396, 20);
    if (loading[0]) snprintf(b, sizeof b, TR("%s is still loading"), loading); else b[0] = 0;
    tmp_set(lwt(T_NOTE), b);
    Obj cg = u_gc_get(h_lw_cog); if (cg) { img_set(cg, spr(S_COG), C_WHITE); u_active(cg, spr(S_COG) != NULL); }
    u_active(u_gc_get(h_lw_pcog), spr(S_COG) == NULL);
    /* height from the content: no empty band under the columns; the window stays centred */
    float left = host ? rows_end + 18 + 26 + 10 + 40 + 10 + lw_addr_h + lw_note_h : rows_end + 36, right = 188 + 40 * nrow + 8 + 20;
    float H = (left > right ? left : right) + 24 + 76;
    place(u_gc_get(h_lw_frame), 0, LY(H / 2), 880, H);
    { float top = host ? 123 : 127, bot = H - 77; place(u_gc_get(h_lw_body), 0, LY((top + bot) / 2), 876, bot - top); }
    if (!host) { u_active(u_gc_get(h_lw_lines[4]), false); u_active(u_gc_get(h_lw_lines[5]), false); }   /* no Invite header */
    place(u_gc_get(h_lw_bbar), 0, LY(H - 38), 876, 74); place(u_gc_get(h_lw_bline), 0, LY(H - 76), 876, 1);
    Obj qb = u_gc_get(h_lwb[LW_QUIT]); if (qb) place(qb, LX(LW_QUIT_X + LW_QUIT_W / 2), LY(H - 38), LW_QUIT_W, 34);
    Obj lb = u_gc_get(h_lwb[LW_LEAVE]); if (lb) place(lb, LX(LW_OFF_X + LW_OFF_W / 2), LY(H - 38), LW_OFF_W, 34);
    Obj sb = u_gc_get(h_lwb[LW_START]); if (sb) place(sb, LX(848 - 116), LY(H - 38), 232, 44);
    { Obj bb = u_gc_get(h_lwb[LW_BRING]); if (bb) place(bb, host ? LX(LW_MID_X) : LX(848 - 116), LY(H - 38), host ? LW_MID_W : 206, 34); }   /* UX pass: with the bar (it stayed at the first height, under the window) */
    if (host) place(lwt(T_NOTE), LX(LW_MID_X), LY(H - 38), LW_MID_W, 30); else place(lwt(T_NOTE), 0, LY(H - 38), 300, 24);
    /* UX pass 2026-10-21: the window fits the game window. The Esc view carries the user's UI scale (up to 1.75 on the
       1080-high canvas): the lobby could be taller than the screen, Start cut off. Measured in pixels, scaled down about
       the window's centre with 16 px free on each side, never up. Its buttons are Unity UI buttons: clicks follow. */
    { Obj fr = u_gc_get(h_lw_frame), lr = u_gc_get(h_lw_root); float r[4];
      float sw = (float)F(0x5092C94, int (*)(void *))(NULL), sh = (float)F(0x5092CD4, int (*)(void *))(NULL);   /* Screen.width / height */
      if (fr && lr && sw > 0 && sh > 0 && rect_of(fr, r) && r[2] > r[0] && r[3] > r[1]) {
          float fw = (r[2] - r[0]) / lw_k, fh = (r[3] - r[1]) / lw_k, k = (sw - 32) / fw;
          if ((sh - 32) / fh < k) k = (sh - 32) / fh;
          if (k > 1) k = 1; if (k < 0.3f) k = 0.3f;
          if (k - lw_k > 0.002f || lw_k - k > 0.002f) {
              lw_k = k; F(0x510AFF8, void (*)(Obj, V3, void *))(u_tr(lr), (V3){ k, k, 1 }, NULL);   /* localScale */
              rig_log("mpmenu: lobby window %.0fx%.0f px at scale 1 on a %.0fx%.0f screen: scale %.3f", fw, fh, sw, sh, k);
          } } }
    place(u_gc_get(h_lw_root), 0, (H / 2 - lw_h / 2) * lw_k, 880, lw_h);
    { float top = host ? 123 : 127, bot = H - 77;                          /* inside the body: never over the title or Start */
      if (u_gc_get(rp_lobby.root)) place(u_gc_get(rp_lobby.root), 0, LY((top + bot) / 2), RP_WIDE_W, RP_WIDE_H);
      Obj sh = u_gc_get(h_lw_rshade); if (sh) { place(sh, 0, LY((top + bot) / 2), 876, bot - top); u_active(sh, host && M.lobby_settings_open); } }
    rp_update(&rp_lobby, &ss, host && M.lobby_settings_open);
}
#undef LX
#undef LY
static void lw_sync(Obj v, int lobby) {
    Obj root = u_gc_get(h_lw_root);
    int host = M.state == MPM_LOBBY;
    if (!host) lw_addr_shown = 0;                                           /* hidden again in the next lobby */
    if (lobby && (!u_alive(root) || u_gc_get(h_lw_view) != v || lw_host_built != host)) {
        if (u_alive(root)) u_active(root, false);
        lw_build(v, host); root = u_gc_get(h_lw_root);
    }
    if (!u_alive(root)) return;
    Obj area = u_gc_get(h_lw_area);
    int was = u_on(root);
    u_active(root, lobby); u_active(area, !lobby);
    if (lobby && (!was || frames % 10 == 0)) lw_texts();
    { static int was_open; int op = M.state == MPM_LOBBY && M.lobby_settings_open;
      if (was_open && !op) rules_close_frames = 3; else if (rules_close_frames > 0) rules_close_frames--; was_open = op; }
}
static void (*o_esc_press)(Obj, void *);
static int escb_press(Obj btn);
static void h_esc_press(Obj btn, void *mi) {                                /* SubUI_SystemMenuButton.OnPressButton */
    for (int k = 0; k < nhooks; k++) if (hooks[k].press && hooks[k].press(btn)) return;   /* Custom Modes buttons (PvP) */
    if (escb_press(btn)) return;                                            /* mp_api register_esc_button */
    for (int i = 0; i < NE; i++) if (btn && btn == eb(esc_ids[i])) { pending_click = esc_ids[i]; return; }
    if (btn && btn == u_gc_get(h_lwb[LW_SHOWADDR])) { lw_addr_shown = 1; lw_texts(); rig_log("mpmenu: lobby: address shown"); return; }
    for (int i = 0; i < LW_NB; i++) if (btn && btn == u_gc_get(h_lwb[i])) { pending_click = lw_ids[i]; return; }
    for (int i = 0; i < MPB_COUNT; i++) if (h_lwr[i] && btn == u_gc_get(h_lwr[i])) { pending_click = i; return; }
    o_esc_press(btn, mi);
}

/* ---------------------------------------------------------------- clipboard (test copies never touch the real one) */
/* Rig / test copies (DSRIG_BATCH, DSRIG_HIDEWIN, DSRIG_SPACE or DSMP_TEST set): GUIUtility.systemCopyBuffer get/set are
 * hooked to <copy folder>/clipboard.txt, for every caller in the game (owner rule 2026-10-12). Live app: real clipboard. */
static int test_mode(void) { return getenv("DSRIG_BATCH") || getenv("DSRIG_HIDEWIN") || getenv("DSRIG_SPACE") || getenv("DSMP_TEST"); }
static Obj (*o_clip_get)(void *);
static void (*o_clip_set)(Obj, void *);
static Obj h_clip_get(void *mi) {
    (void)mi; char p[1200], b[1024] = ""; snprintf(p, sizeof p, "%s/clipboard.txt", rig_dir);
    FILE *f = fopen(p, "r"); if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
    return u_str(b);
}
static void h_clip_set(Obj v, void *mi) {
    (void)mi; char p[1200], b[1024]; snprintf(p, sizeof p, "%s/clipboard.txt", rig_dir);
    str_utf8(v, b, sizeof b);
    FILE *f = fopen(p, "w"); if (f) { fputs(b, f); fclose(f); }
}

/* player name when none is set (no Steam persona, no mp.json): mpm_default_name ("Player" and four digits, UX review M1) */
static void default_name(char *name, size_t cap) { mpm_fill_name(name, cap); }

/* ---------------------------------------------------------------- remembered settings (owner 2026-10-13)
 * <app data folder>/mp_menu.json  {"pause":1,"speedup":1,"gear":0,"name":"The owner"}: the last used host settings + name.
 * <app data folder>/mp_worlds.txt one line per world played in multiplayer: "<campaign guid> <pause> <speedup> <gear>".
 * The app data folder (DSRIG_DIR) is shared by every lan_vN build, so both survive app updates. A world's own line
 * is its stored settings (loading it uses them); a new world gets the last used ones. The load list tags these worlds. */
static void app_path(char *p, size_t cap, const char *f) { snprintf(p, cap, "%s/%s", rig_dir, f); }
static void prefs_load(void) {
    char p[1200], b[1024] = ""; app_path(p, sizeof p, "mp_menu.json");
    FILE *f = fopen(p, "r"); if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
    double v;
    if (jnum(b, "pause", &v) && v >= 0 && v <= 2) M.set.pause_mode = (int)v;
    if (jnum(b, "speedup", &v)) M.set.speedup = v != 0;
    if (jnum(b, "gear", &v) && v >= 0 && v <= 2) M.set.gear = (int)v;
    if (jnum(b, "units", &v) && (v == 1 || v == 2 || v == 3 || v == 4 || v == 6)) M.set.units = (int)v;
    if (jnum(b, "admin", &v)) M.set.admin = v != 0;
    if (jnum(b, "events", &v) && v >= 0 && v <= 3) M.set.events = (int)v;
    if (jnum(b, "population", &v) && v >= 0 && v <= 3) M.set.population = (int)v;
    if (jnum(b, "spectators", &v)) M.set.spectators = v != 0;
    if (jnum(b, "fog", &v)) M.set.fog = v != 0;
    /* "fe" (World) is not restored: the Host page opens at "Your save" (the expedition rules below are) */
    /* fe_death saved before 2026-10-15 was the old default (On), not a choice: kept only from files with "fe_v" 2 (death Off default) */
    { double fv = 0; if (jnum(b, "fe_v", &fv) && fv >= 2 && jnum(b, "fe_death", &v)) M.set.fe_death = v != 0; }
    if (jnum(b, "fe_xp", &v)) M.set.fe_xp = v != 0;
    if (jnum(b, "fe_loot", &v)) M.set.fe_loot = v != 0;
    if (jnum(b, "fe_recruit", &v)) M.set.fe_recruit = v != 0;
    if (jnum(b, "chat_fade", &v) && v >= 3 && v <= 120) chat_fade_s = (int)v;
    if (jnum(b, "chat_join_lines", &v)) chat_show_join = v != 0;
    if (jnum(b, "chat_on", &v)) chat_on = v != 0;
    if (jnum(b, "chat_x", &v)) chat_x = (float)v;
    if (jnum(b, "chat_y", &v)) chat_y = (float)v;
    if (jnum(b, "chat_w", &v) && v >= CH_W0 && v <= 8000) chat_w = (float)v;    /* shown clamped to the screen (ch_geo) */
    if (jnum(b, "chat_h", &v) && v >= CH_H0 && v <= 8000) chat_h = (float)v;
    {   char rn[64] = ""; double nv = 0; jstr(b, "name", rn, sizeof rn); jnum(b, "name_v", &nv);
        const char *st = dsnet_steam_load(NULL) == 0 ? dsnet_steam_my_name() : "";
        mpm_start_name(my_name, sizeof my_name, rn, (int)nv, st);
    }
    rig_log("mpmenu: remembered settings pause %d speed-up %d gear %d, name \"%s\"%s", M.set.pause_mode, M.set.speedup, M.set.gear, my_name, b[0] ? "" : " (defaults)");
}
static void prefs_save(void) {
    char p[1200]; app_path(p, sizeof p, "mp_menu.json");
    FILE *f = fopen(p, "w"); if (!f) return;
    Buf b = {0}; bput(&b, "{\"pause\":%d,\"speedup\":%d,\"gear\":%d,\"units\":%d,\"admin\":%d,\"events\":%d,\"population\":%d,\"fog\":%d,\"spectators\":%d,\"fe\":%d,\"fe_death\":%d,\"fe_xp\":%d,\"fe_loot\":%d,\"fe_recruit\":%d,\"fe_v\":2,\"chat_fade\":%d,\"chat_join_lines\":%d,\"chat_on\":%d,\"chat_x\":%.0f,\"chat_y\":%.0f,\"chat_w\":%.0f,\"chat_h\":%.0f,\"name\":",
        M.set.pause_mode, M.set.speedup, M.set.gear, M.set.units, M.set.admin, M.set.events, M.set.population, M.set.fog, M.set.spectators, M.set.fe, M.set.fe_death, M.set.fe_xp, M.set.fe_loot, M.set.fe_recruit, chat_fade_s, chat_show_join, chat_on, chat_x, chat_y, chat_w, chat_h); bstr(&b, my_name); bput(&b, ",\"name_v\":2}\n");
    fputs(b.p, f); fclose(f); free(b.p);
}
static int world_get(const char *guid, mpm_settings *out) {
    char p[1200], line[256]; app_path(p, sizeof p, "mp_worlds.txt");
    FILE *f = fopen(p, "r"); if (!f) return 0;
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        char g[64]; int a, b2, c, u = 3, ad = 0, ev = 0, po = 0, fg = 0, sp = 1;    /* units, admin, events, population, fog, spectators: 5th-10th fields (older: 3, 0, 0, 0, 0, 1) */
        if (sscanf(line, "%63s %d %d %d %d %d %d %d %d %d", g, &a, &b2, &c, &u, &ad, &ev, &po, &fg, &sp) >= 4 && !strcmp(g, guid)) { out->pause_mode = a; out->speedup = b2; out->gear = c; out->units = u; out->admin = ad; out->events = ev; out->population = po; out->fog = fg != 0; out->spectators = sp != 0; found = 1; }
    }
    fclose(f); return found;
}
static void world_put(const char *guid, const mpm_settings *st) {
    if (mp_hub_guid(guid)) return;                                          /* a hub is made new each session: nothing to keep */
    char p[1200], tmp[1300], line[256]; app_path(p, sizeof p, "mp_worlds.txt"); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *in = fopen(p, "r"), *out = fopen(tmp, "w"); if (!out) { if (in) fclose(in); return; }
    while (in && fgets(line, sizeof line, in)) { char g[64]; if (sscanf(line, "%63s", g) == 1 && strcmp(g, guid)) fputs(line, out); }
    fprintf(out, "%s %d %d %d %d %d %d %d %d %d\n", guid, st->pause_mode, st->speedup, st->gear, st->units, st->admin, st->events, st->population, st->fog, st->spectators);
    if (in) fclose(in);
    fclose(out); rename(tmp, p);
}
static int campaign_guid(char *out, size_t cap) {                          /* ClanDataContainer.CampaignGuid (campaign data +0x50, +0x10) = the save header's */
    Obj c = cdc(), cl = c ? AT(c, 0x50, Obj) : NULL; if (!cl) return 0;
    Guid g = AT(cl, 0x10, Guid); if (!g.lo && !g.hi) return 0;
    char gs[40]; guid_str(g, gs); snprintf(out, cap, "%s", gs); return 1;
}

/* load list: saves of worlds with the Multiplayer tag (mp_tags.txt, owner 2026-10-15) get it after their title
 * (SubUI_SaveFileButton.SetData post-hook). The world row has the full tag (h_case_init). */
static int tag_get(const char *guid, char names[][32]);
static void tag_slot_seen(Obj slot, const char *guid);
static void (*o_slot_set)(Obj, const void *, bool, Obj, void *);
static void h_slot_set(Obj self, const void *hdr, bool inter, Obj act, void *mi) {
    o_slot_set(self, hdr, inter, act, mi);
    if (!hdr || !self) return;
    Guid g; memcpy(&g, hdr, 16);                                           /* CampaignSaveHeader.CampaignGuid @0x0 (header by reference) */
    char gs[40]; guid_str(g, gs); char nm[MPM_TAG_NAMES][32];
    tag_slot_seen(self, gs);
    if (tag_get(gs, nm) < 0) return;
    Obj t = AT(self, 0x38, Obj);                                            /* _titleText */
    char cur[256] = ""; str_utf8(tmp_get(t), cur, sizeof cur);
    char tg[96]; snprintf(tg, sizeof tg, "  <color=#8FC4FF>[%s]</color>", TR("Multiplayer"));
    if (strstr(cur, tg) || strstr(cur, "<color=#8FC4FF>[")) return;
    char nt[320]; snprintf(nt, sizeof nt, "%s%s", cur, tg);
    tmp_set(t, nt);
}

/* owner rule: no loading another world during a multiplayer session (host or guest). The Esc menu's Load button is
 * disabled with a note; its click and the quick-load key do nothing. Leaving multiplayer restores them. */
/* host choices under "Host a game" (mpmenu_register_host_choice / _choice2, below; up to 4) */
static mpm_choices hcl;
static mpm_choice *cur_choice(void) { return M.choice > 0 && M.choice <= hcl.n ? &hcl.c[M.choice - 1] : NULL; }

static int in_session(void) { return mp_game_session() != NULL; }
static void (*o_esc_load)(Obj, void *);
static void h_esc_load(Obj v, void *mi) { if (in_session()) { rig_log("mpmenu: Load blocked during multiplayer"); return; } o_esc_load(v, mi); }
static Obj (*o_quickload)(Obj, Obj, void *);
static Obj h_quickload(Obj self, Obj msg, void *mi) { if (in_session()) { rig_log("mpmenu: quick load blocked during multiplayer"); return NULL; } return o_quickload(self, msg, mi); }
/* design #7: the stock label stays ("Load Campaign"), the button is greyed (SetInteractable, stock tint); the reason is a
 * seed-style line under the buttons (esc_sync, h_ereason). Older builds swapped the label: restored if seen. */
static char load_label[128];
static void esc_load_rule(Obj v) {
    Obj lb = AT(v, 0x60, Obj), t = u_alive(lb) ? AT(lb, 0x28, Obj) : NULL;  /* _loadCampaignButton, _buttonName */
    if (!u_alive(t)) return;
    char cur[160] = ""; str_utf8(tmp_get(t), cur, sizeof cur);
    if ((strstr(cur, "Leave multiplayer") || strstr(cur, TR("Leave multiplayer"))) && load_label[0]) tmp_set(t, load_label);   /* label of an older build (English then) */
    /* the stock tint alone is hard to see (design run 8): the label also takes the secondary colour #a69085 */
    static int off;                                                         /* only undo our own greying (stock rules stay) */
    if (in_session()) {
        if (!strstr(cur, "#A69085")) { snprintf(load_label, sizeof load_label, "%s", cur); char g[200]; snprintf(g, sizeof g, "<color=#A69085>%s</color>", load_label); tmp_set(t, g); }
        (mm_is(lb, K_SMB) ? F(0xF2DC14, void (*)(Obj, bool, void *))(lb, false, NULL) : (void)0); off = 1;   /* SubUI_SystemMenuButton.SetInteractable */
    } else if (off) { tmp_set(t, load_label); (mm_is(lb, K_SMB) ? F(0xF2DC14, void (*)(Obj, bool, void *))(lb, true, NULL) : (void)0); off = 0; }
}

/* ---------------------------------------------------------------- direct address */
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
/* the ONE "Same home" address (owner 2026-10-20): en0, else the first real LAN adapter. VPN / virtual adapters (Tailscale
   utun, bridge, vmnet, Docker, Hyper-V: ui_addr_kind, the Windows getifaddrs shim names them so), link-local 169.254/16
   and carrier NAT 100.64/10 (Tailscale) only when nothing else is up. */
static void lan_ip(char *out, size_t cap) {
    struct ifaddrs *ifs = NULL, *i; snprintf(out, cap, "127.0.0.1");
    if (getifaddrs(&ifs)) return;
    for (int pass = 0; pass < 3; pass++)
        for (i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
            uint32_t a = ntohl(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
            int lan = ui_addr_kind(i->ifa_name) == 0 && a >> 16 != 0xA9FE && (a & 0xFFC00000u) != 0x64400000u;
            if (pass == 0 && (strcmp(i->ifa_name, "en0") || !lan)) continue;
            if (pass == 1 && !lan) continue;
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, (socklen_t)cap);
            freeifaddrs(ifs); return;
        }
    freeifaddrs(ifs);
}
/* public IPv4 as Steam sees it: ISteamUser::GetPublicIP (flat API, SDK 1.62), from the game's own libsteam_api. No web lookup. */
typedef struct { union { uint32_t v4; uint8_t v6[16]; } a; int32_t type; } SteamIP;   /* SteamIPAddress_t; type 0 = IPv4, 1 = IPv6 */
static int steam_public_ip(char *out, size_t cap) {
    static void *(*user)(void); static SteamIP (*getip)(void *); static int tried;
    if (!tried) {
        tried = 1;
        for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++) {
            const char *img = _dyld_get_image_name(i);
            if (!img || !strstr(img, "libsteam_api.dylib")) continue;
            void *lib = dlopen(img, RTLD_NOLOAD | RTLD_NOW);
            if (lib) { user = (void *(*)(void))dlsym(lib, "SteamAPI_SteamUser_v023"); getip = (SteamIP (*)(void *))dlsym(lib, "SteamAPI_ISteamUser_GetPublicIP"); }
            break;
        }
        rig_log("mpmenu: Steam public IP call %s", user && getip ? "found" : "not available");
    }
    void *u = user && getip ? user() : NULL;
    if (!u) return 0;
    SteamIP ip = getip(u);
    if (ip.type != 0 || !ip.a.v4) return 0;
    snprintf(out, cap, "%u.%u.%u.%u", ip.a.v4 >> 24, ip.a.v4 >> 16 & 255, ip.a.v4 >> 8 & 255, ip.a.v4 & 255);
    return 1;
}
static void direct_refresh(void) {
    mp_session *s = mp_game_session(); uint16_t p = s ? mp_server_port(s) : 0;
    static mp_session *last_s; static uint16_t last_p;                     /* one internet lookup per host session */
    if (s != last_s || p != last_p) { extaddr_reset(); last_s = s; last_p = p; }
    if (!p) { M.direct[0] = M.internet[0] = 0; inet_state = EXTADDR_IDLE; up_state = UPNPP_IDLE; return; }
    char ip[64], pub[64]; const char *via = ""; lan_ip(ip, sizeof ip);
    snprintf(M.direct, sizeof M.direct, "%s:%u", ip, p);
    extaddr_start();                                                        /* no-op once started (own thread, never this frame) */
    int was = inet_state; inet_state = extaddr_get(pub, sizeof pub, &via);
    if (inet_state == EXTADDR_UNKNOWN && steam_public_ip(pub, sizeof pub)) { inet_state = EXTADDR_OK; via = "steam"; }
    uint16_t ext = 0; int was_up = up_state; up_state = upnpport_get(&ext);
    if (inet_state == EXTADDR_OK) snprintf(M.internet, sizeof M.internet, "%s:%u", pub, (up_state == UPNPP_OPENED || up_state == UPNPP_ALREADY) && ext ? ext : p);
    else M.internet[0] = 0;
    if (inet_state != was && inet_state >= EXTADDR_OK)
        rig_log("mpmenu: internet address %s (via %s)", inet_state == EXTADDR_OK ? "found" : inet_state == EXTADDR_PRIVATE ? "private (provider NAT)" : "unknown", via);
    if (up_state != was_up && up_state >= UPNPP_OPENED)
        rig_log("mpmenu: router port %s (listen %u, external %u)", up_state == UPNPP_OPENED ? "opened automatically" : up_state == UPNPP_ALREADY ? "already forwarded"
                                                                     : up_state == UPNPP_OFF ? "UPnP off" : "not opened", p, ext);
}
static char hist_host[48];                               /* reconnecting from a Recent worlds row: that host's name */
static char hist_world[64];                              /* ... and that row's world name (F17 refusal text) */
static void expect_world(const char *gid) {              /* core (F17): the next join is for this world ("" = any) */
    static void (*fn)(const char *); static int looked; if (!looked) { looked = 1; fn = (void (*)(const char *))dlsym(RTLD_DEFAULT, "mp_game_expect_world"); }
    if (fn) fn(gid);
}
static int other_world_text(const char *host) {          /* 1 = the last refusal was "another world" (M.msg set) */
    static int (*rej)(void); static const char *(*ow)(void); static int looked;
    if (!looked) { looked = 1; rej = (int (*)(void))dlsym(RTLD_DEFAULT, "mp_game_last_reject"); ow = (const char *(*)(void))dlsym(RTLD_DEFAULT, "mp_game_other_world"); }
    if (!rej || rej() != 7) return 0;                    /* DSNET_REJ_OTHER_WORLD */
    const char *w = ow ? ow() : "";
    const char *hh = host[0] ? host : TR("The host"), *nw = hist_world[0] ? hist_world : TR("this one");
    if (w && w[0]) snprintf(M.msg, sizeof M.msg, TR("%s is playing another world now (%s), not %s."), hh, w, nw);
    else snprintf(M.msg, sizeof M.msg, TR("%s is playing another world now, not %s."), hh, nw);
    return 1;
}
static char joined_how[8], joined_addr[96];               /* how this guest joined: "ip" + address, or "steam" */
static uint64_t joined_steamid;
/* Join by address (ux #1): the TCP connect runs on dsnet's helper thread (dsnet_connect_*), limited to 8 s; the page
 * shows "Connecting to <address>..." with Cancel meanwhile. Once connected, the socket is handed to mp_game_join. */
static dsnet_connector *conn;
static mp_config conn_cfg;
static double conn_t0;
static double now_ms_(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
/* ---------------------------------------------------------------- "Get the host's mods" (v0.1.14, hostmods CONTRACT)
 * After a DSNET_REJ_MODSET refusal, with a loader that has dsl_hostmods_plan / _get / _state (dlsym): the stock notice
 * box lists the host's gameplay mods next to this copy's (differences marked) with ONE button "Get the host's mods"
 * (none when a host mod is not one of our official mods) and Close. The click hands the host's lines + DSMP_REJOIN
 * (how this guest joined) to the loader; it downloads, installs and switches the mods, and the new process joins the
 * host once (rejoin_frame). An older loader (no symbol): the text-only notice as before. */
enum { HM_ROWS = 6 };                                        /* differing rows shown in the box (the stock box is small) */
static struct {
    int pending;            /* the box waits for the title to be calm */
    int can_fix, vanilla, tries;
    int go;                 /* the button was clicked: start once the stock box has closed */
    int busy, wait_box_shown, st_shown, pct_shown, auto_done;   /* go counts the frames it waits */
    double shown_ms;
    int names_pending; double plan_ms;   /* the loader still fetches a missing mod's name: ask again, show by 4 s at most */
    char host[DSNET_MODLINES_MAX], rejoin[300], text[2048];
} HM;
static int (*hm_plan)(const char *, int, char *, size_t), (*hm_get)(const char *, int, const char *, char *, size_t), (*hm_state)(int *, char *, size_t);
static int hm_test, hm_rejoined;     /* hm_test: DSMP_HOSTMODS_TEST=1 / mpm_hostmods {test:1}; hm_rejoined: this process came from DSMP_REJOIN */
static void *hm_sym(const char *n) {
#ifdef _WIN32
    HMODULE b = GetModuleHandleA("baselib.dll");    /* Windows: the loader is the baselib.dll proxy; the dlfcn shim only sees this DLL */
    void *p = b ? (void *)GetProcAddress(b, n) : NULL;
    if (p) return p;
#endif
    return dlsym(RTLD_DEFAULT, n);
}
void *mpmenu_loader_sym(const char *n) { return hm_sym(n); }        /* mp_game.c: a loader export (Windows: baselib.dll) */
static int hostmods_loader(void) {                                   /* 1 = the loader has the hostmods API */
    static int looked;
    if (!looked) {
        looked = 1;
        hm_plan = (int (*)(const char *, int, char *, size_t))hm_sym("dsl_hostmods_plan");
        hm_get = (int (*)(const char *, int, const char *, char *, size_t))hm_sym("dsl_hostmods_get");
        hm_state = (int (*)(int *, char *, size_t))hm_sym("dsl_hostmods_state");
        dsmc_set_setting_text((dsmc_setting_fn)hm_sym("dsl_setting_text"));   /* the box's settings: labels and values in the player's language */
        const char *t = getenv("DSMP_HOSTMODS_TEST"); if (t && !strcmp(t, "1")) hm_test = 1;
        rig_log("mpmenu: hostmods: loader API %s%s", hm_plan && hm_get && hm_state ? "found" : "not found (older loader: text notice)", hm_test ? ", test hook on" : "");
    }
    return hm_plan && hm_get && hm_state;
}
/* the plan into HM.text / can_fix / names_pending; returns the loader's answer (<= 0 = no box) */
static int hostmods_replan(void) {
    static char json[32768]; static dsmc_plan pl;
    json[0] = 0;
    int r = hm_plan(HM.host, HM.vanilla, json, sizeof json);
    if (r <= 0 || dsmc_plan_parse(json, &pl) <= 0) { rig_log("mpmenu: hostmods: plan %d (%.200s): text notice", r, json); return r > 0 ? 0 : r; }
    dsmc_hostmods_text(&pl, HM.vanilla, HM.text, sizeof HM.text, HM_ROWS); dsi_utf8_fix_end(HM.text);
    HM.can_fix = pl.can_fix != 0; HM.names_pending = pl.names_pending != 0;
    return r;
}
/* the box from the loader's plan (1 = it will show; 0 = use the text notice) */
static int hostmods_prepare(void) {
    if (!hostmods_loader()) return 0;
    static int (*mh)(int *, char *, size_t); static int (*rl)(uint64_t, char *, size_t); static int looked;
    if (!looked) { looked = 1; mh = (int (*)(int *, char *, size_t))dlsym(RTLD_DEFAULT, "mp_game_modset_host"); rl = (int (*)(uint64_t, char *, size_t))dlsym(RTLD_DEFAULT, "mp_game_rejoin_line"); }
    if (!mh || !mh(&HM.vanilla, HM.host, sizeof HM.host)) return 0;
    int r = hostmods_replan();
    if (r <= 0) return 0;
    HM.plan_ms = now_ms_();
    if (!rl || !rl(joined_steamid, HM.rejoin, sizeof HM.rejoin)) HM.rejoin[0] = 0;
    HM.pending = 1; HM.tries = 0; HM.go = 0;
    rig_log("mpmenu: hostmods: plan %d differences, can_fix %d, names pending %d, rejoin %s", r, HM.can_fix, HM.names_pending, HM.rejoin[0] ? HM.rejoin : "(none)");
    return 1;
}
static void modset_notice(void) {                                    /* wrong gameplay mods: the notice box names them */
    static int (*fn)(char *, size_t); static int looked;
    if (!looked) { looked = 1; fn = (int (*)(char *, size_t))dlsym(RTLD_DEFAULT, "mp_game_modset_text"); }
    static char t[sizeof M.notice];
    if (!fn || !fn(t, sizeof t)) return;                              /* once per refusal (and its log line) */
    if (hostmods_prepare()) return;                                   /* the hostmods box instead */
    snprintf(M.notice, sizeof M.notice, "%s", t);
}
static void join_refused(int rc, const char *addr) {
    static int (*last_reject)(void); static int looked;                 /* mpglue: enum dsnet_reject of the last failed start */
    if (!looked) { looked = 1; last_reject = (int (*)(void))dlsym(RTLD_DEFAULT, "mp_game_last_reject"); }
    int r = rc && last_reject ? last_reject() : 0;
    const char *w = dsnet_reject_text(r);                               /* the same wording as mp_game's reject_text (ux #22) */
    if (r == DSNET_REJ_MODSET) { snprintf(M.msg, sizeof M.msg, "%s", TR("Could not join: your gameplay mods are not the same as the host's")); modset_notice(); }
    else if (*w) snprintf(M.msg, sizeof M.msg, TR("Could not join: %s"), w);
    else if (rc) snprintf(M.msg, sizeof M.msg, TR("%s answered, but the game did not start. Try again."), addr);
}
static void join_by_address(void) {
    char txt[256] = "";
    Obj f = u_gc_get(h_field);
    if (u_alive(f)) str_utf8((mm_is(f, K_FIELD) ? F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL) : NULL), txt, sizeof txt);       /* TMP_InputField.text */
    else str_utf8(F(0x518AEA0, Obj (*)(void *))(NULL), txt, sizeof txt);                       /* clipboard */
    mp_config c; mp_config_defaults(&c); c.host = 0; c.need_save = 1; c.spectator = M.watch;   /* Connect / Watch */
    char p[1200]; snprintf(p, sizeof p, "%s/mp.json", rig_dir);
    mp_config fj; if (mp_config_load(p, &fj) == 0) snprintf(c.name, sizeof c.name, "%s", fj.name);
    uint16_t port = 0;
    if (ui_parse_address(txt, c.host_addr, sizeof c.host_addr, &port, MPM_DEFAULT_PORT)) {
        char tq[64]; snprintf(tq, sizeof tq, "%.40s", txt); dsi_utf8_fix_end(tq);
        snprintf(M.msg, sizeof M.msg, TR("\"%s\" is not an address. It looks like 192.168.1.20:47777"), tq); return;
    }
    c.port = port;
    char nm[64]; field_text(h_namefield, nm, sizeof nm); if (nm[0]) snprintf(my_name, sizeof my_name, "%s", nm);
    snprintf(c.name, sizeof c.name, "%s", my_name); default_name(c.name, sizeof c.name);
    if (conn) dsnet_connect_free(conn);
    expect_world(""); hist_world[0] = 0;                                  /* any world (only a Recent worlds row asks for one) */
    conn_cfg = c; conn_t0 = now_ms_();
    conn = dsnet_connect_start(c.host_addr, port, DSNET_CONNECT_TIMEOUT_MS);
    snprintf(M.connecting, sizeof M.connecting, "%s:%u", c.host_addr, port);
    rig_log("mpmenu: join by address %s%s: connecting (limit %d ms)", M.connecting, c.spectator ? " to watch" : "", DSNET_CONNECT_TIMEOUT_MS);
}
static void lanblock_box(const char *text);                                /* the same text in the stock box + Open Settings */
static void join_poll(void) {                                              /* each frame: never waits */
    if (!conn) return;
    if (!M.connecting[0]) { dsnet_connect_free(conn); conn = NULL; rig_log("mpmenu: connect cancelled"); return; }
    int r = dsnet_connect_poll(conn);
    if (!r) return;
    char addr[96]; snprintf(addr, sizeof addr, "%s", M.connecting);
    M.connecting[0] = 0;
    if (r == 1) dsnet_connect_adopt(conn);
    dsnet_connect_free(conn); conn = NULL;
    double took = now_ms_() - conn_t0;
    rig_log("mpmenu: connect to %s: %d after %.0f ms", addr, r, took);
    int lan_blocked = 0;
#ifdef __APPLE__
    /* macOS 15+ Local Network privacy: connect() to a local-network address fails at once (EHOSTUNREACH, ~10 ms);
       an absent LAN host takes seconds. Then the player needs the setting, not "check the address". */
    lan_blocked = r == DSNET_CONN_UNREACHABLE && took < 1000 && mpm_lan_addr(conn_cfg.host_addr);
#endif
    if (lan_blocked) {
        snprintf(M.msg, sizeof M.msg, "%s", TR("macOS blocked the local network for Dungeon Settlers. Allow it in System Settings > Privacy & Security > Local Network."));
        rig_log("mpmenu: connect to %s: local network blocked by macOS (rc %d after %.0f ms)", addr, r, took);
        lanblock_box(M.msg);
    }
    else if (r == DSNET_CONN_TIMEOUT) snprintf(M.msg, sizeof M.msg, TR("No answer from %s. Check the address, and ask the host to open port %u on the router."), addr, conn_cfg.port);
    else if (r == DSNET_CONN_REFUSED) snprintf(M.msg, sizeof M.msg, TR("Nobody is hosting at %s. Check the address and the port."), addr);
    else if (r == DSNET_CONN_BAD_ADDRESS) snprintf(M.msg, sizeof M.msg, TR("Unknown address: %s. Check the address."), addr);
    else if (r < 0) snprintf(M.msg, sizeof M.msg, TR("Cannot reach %s. Check the address and your network."), addr);
    if (r != 1 && hist_host[0]) { if (!lan_blocked) snprintf(M.msg, sizeof M.msg, TR("%s is not hosting this world right now."), hist_host); hist_host[0] = 0; }   /* a Recent worlds row */
    if (r != 1) { expect_world(""); hist_world[0] = 0; }                  /* no join: the expectation does not carry over */
    if (r != 1) return;
    int rc = mp_game_join(&conn_cfg);                                     /* uses the adopted socket: no connect wait */
    join_refused(rc, addr);
    if (rc && hist_host[0] && !other_world_text(hist_host)) snprintf(M.msg, sizeof M.msg, TR("%s is not hosting this world right now."), hist_host);
    hist_host[0] = 0; hist_world[0] = 0;
    if (!rc) { prefs_save(); snprintf(joined_how, sizeof joined_how, "ip"); snprintf(joined_addr, sizeof joined_addr, "%s", addr); }
    rig_log("mpmenu: join by address %s rc %d as \"%s\"", addr, rc, conn_cfg.name);
}

/* ---------------------------------------------------------------- host rules (DSNET_RULES) */
/* The host's settings are the session's rules: sent at host start (dsnet's WELCOME has no unit limit field in
 * mp_config) and on each change in the lobby. Changes made elsewhere (the in-game panel's cog, ui lane) come back
 * through rules_sync: M.set follows dsnet and the world's line in mp_worlds.txt is rewritten. */
static int64_t rules_sent_frame = -100000;
static void rules_send(const char *why) {
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    dsnet_rules r = { M.set.pause_mode, M.set.speedup, M.set.gear, M.set.units, M.set.events, M.set.population, M.set.fog };
    int rc = c ? dsnet_set_rules(c, &r) : -1;
    rules_sent_frame = frames;
    if (c && !strcmp(why, "host start") && M.set.admin) { uint8_t one = 1; dsnet_submit(c, DSNET_ADMIN_MODE, 0, &one, 1); }   /* host page "Admin mode: on" */
    char gid[64] = ""; prefs_save(); if (campaign_guid(gid, sizeof gid)) world_put(gid, &M.set);
    rig_log("mpmenu: rules (%s): pause %d speed-up %d gear %d units %d (rc %d)", why, r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player, rc);
}
int pvp_pause_mode_saved(void) __attribute__((weak_import));               /* pvp_game.c: >= 0 while a match turns player pauses off */
static void rules_sync(const mpm_obs *o) {
    if (o->role != 1 || frames % 60 || frames - rules_sent_frame < 120) return;   /* host; our own change has landed */
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    if (!c) return;
    dsnet_rules r; dsnet_rules_get(c, &r);
    if (pvp_pause_mode_saved) { int sv = pvp_pause_mode_saved(); if (sv >= 0) r.pause_mode = sv; }   /* the match's "no pause" is never remembered (a quit mid-match kept it) */
    int adm = dsnet_admin_mode(c), spa = dsnet_spectators_allowed(c) != 0;
    if (r.pause_mode == M.set.pause_mode && (r.allow_speedup != 0) == (M.set.speedup != 0) && r.gear_mode == M.set.gear && r.units_per_player == M.set.units && r.event_choice == M.set.events && r.population == M.set.population && r.shared_fog == M.set.fog && adm == M.set.admin
        && spa == (M.set.spectators != 0)) return;
    M.set.spectators = spa;                                                 /* the in-game cog (ui lane) or the box */
    M.set.pause_mode = r.pause_mode; M.set.speedup = r.allow_speedup; M.set.gear = r.gear_mode; M.set.units = r.units_per_player; M.set.events = r.event_choice; M.set.population = r.population; M.set.fog = r.shared_fog; M.set.admin = adm;
    char gid[64] = ""; prefs_save(); if (campaign_guid(gid, sizeof gid)) world_put(gid, &M.set);
    rig_log("mpmenu: rules changed in play: pause %d speed-up %d gear %d units %d (world saved)", r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player);
}

/* ---------------------------------------------------------------- host lobby hold (ux #14) */
/* The world waits while the host is in the lobby: a dsnet LOBBY hold (0 game ticks on every peer, every pause mode),
 * on at host start, off at Start. Guests see it (observe: host_lobby) and get the waiting panel. */
static void lobby_hold(int on) {
    mp_session *s = mp_game_session();
    int rc = s ? dsnet_hold(mp_client(s), on, DSNET_HOLD_LOBBY, NULL) : -1;
    rig_log("mpmenu: lobby hold %s (rc %d)", on ? "on" : "off", rc);
}

/* ---------------------------------------------------------------- session ended: stock notice box (ux #9) */
/* CommonNoticeView (StaticUI): Show_Confirm(text, onConfirm) 0xF65498, _noticeText +0x30, _txtConfirm +0x40. Shown once
 * the title is up; if the box cannot be found, the text goes to the Multiplayer button line as before. */
static uintptr_t h_cnv; static void *k_cnv;
static int notice_tries, title_calm;
static void lbox_grow(Obj v);
static void notice_frame(const mpm_obs *o) {
    /* the title (~3 s calm), or the world for a failed start / join that stays there (MPM_FAILED: the host's world, or
       a guest's own world); never in the world for a session that ended, since the guest goes back to the title then */
    static int calm_scene;
    if (o->scene != calm_scene) { calm_scene = o->scene; title_calm = 0; }
    title_calm = (o->scene == 1 && o->title_visible && !o->loading) || (o->scene == 2 && M.state == MPM_FAILED && o->campaign_ready && !o->loading) ? title_calm + 1 : 0;
    if (M.notice[0] && title_calm && o->scene == 2)                         /* a world pop-up (tutorial, story event) draws over the box: */
        for (int i = 2; i < 30; i++) if (popup_open(i)) { title_calm = 0; break; }   /* show it once they stay closed */
    if (HM.pending) M.notice[0] = 0;                                        /* the hostmods box says it (hostmods_frame) */
    if (!M.notice[0] || title_calm < 90) return;                            /* the loading curtain is gone (~3 s of title, or of the world) */
    if (M.confirm_leave) return;                                            /* never over the Leave box */
    Obj v = find_view_x(&h_cnv, &k_cnv, "Refactor.UI", "CommonNoticeView", true);
    if (!v) {
        if (++notice_tries < 120) return;
        snprintf(M.msg, sizeof M.msg, "%.*s", (int)strcspn(M.notice, ".\n"), M.notice);
        rig_log("mpmenu: no notice box: \"%s\" goes to the button line", M.msg);
        M.notice[0] = 0; notice_tries = 0; return;
    }
    (mm_is(v, K_CNV) ? F(0xF65498, void (*)(Obj, Obj, Obj, void *))(v, u_str_ok(M.notice), NULL, NULL) : (void)0);   /* Show_Confirm */
    tmp_set(AT(v, 0x30, Obj), M.notice);                                   /* raw text (not a text key) */
    txt_fit_stock(AT(v, 0x30, Obj));                                        /* a long text auto-sizes in the stock box */
    tmp_set(AT(v, 0x40, Obj), TR("OK"));
    u_go_active(u_go(v), true);
    lbox_grow(v);                                                           /* a text too long for the stock box: it grows */
    rig_log("mpmenu: notice box: %s", M.notice);
    M.notice[0] = 0; notice_tries = 0;
}

/* ---------------------------------------------------------------- host "Leave multiplayer": confirm first (lan_v10, ux_flow F7) */
/* The host's Leave ends every guest's session, so the stock CommonNoticeView asks first (Show_Select 0xF652A8, null
 * actions; _txtConfirm +0x40 primary right "End for everyone", _txtCancle +0x50 "Cancel"). The ui lane's
 * OnClick_Confirm / OnClick_Cancle hooks hand clicks on a box that is not theirs to mpmenu_notice_click (dlsym). If
 * another box takes the view (its text changes) or it closes another way, it counts as Cancel. */
enum { BX_NONE, BX_LEAVE, BX_INVITE, BX_NOSAVE, BX_JOINING, BX_SPECT_OFF, BX_UNTAG, BX_LANBLOCK, BX_HOSTMODS, BX_HOSTMODS_INFO, BX_REJOIN, BX_SOLO };   /* SOLO: "Keep a single-player copy of this world?" */   /* REJOIN: "Connection lost, rejoining..." + Cancel (mp_game_autorejoin) */   /* HOSTMODS: the host's mods + "Get the host's mods"; _INFO: its progress / failure */   /* SPECT_OFF: "Remove the spectators now?"; UNTAG: "Remove the multiplayer tag?" */             /* which of our boxes is up (one view, one at a time) */
static char lbox_text[2048]; static int lbox_up;                             /* lbox_up = BX_* */
static Obj lbox_view(void) { return find_view_x(&h_cnv, &k_cnv, "Refactor.UI", "CommonNoticeView", true); }
static Obj lbox_view_now(void) { return find_view_now(&h_cnv, &k_cnv, "Refactor.UI", "CommonNoticeView", true); }   /* showing a box */
static int lbox_ours(Obj v) {
    if (!lbox_up || !u_alive(v) || !u_on(v)) return 0;
    char t[2048] = ""; str_utf8(tmp_get(AT(v, 0x30, Obj)), t, sizeof t);
    char want[2048]; str_utf8(u_str_ok(lbox_text), want, sizeof want);       /* the same ASCII-safe form as the read-back */
    return !strcmp(t, want);
}
/* The stock box grows to our text (CommonNoticeView: Panel_Notice > Img_BackGroundBox, pivot top, 546 x 208, whose
 * GridLayoutGroup lays the button row from its top + padding; Txt_Notice beside it, 520 x 108.6, centred). A text too
 * long for that rect even at the fit engine's smallest size (txt_fit_h): the text rect, the box and the grid's top padding
 * grow by the overflow; the box half up, half down (it stays centred), so the text keeps its margins and the buttons stay
 * at the box's bottom, under the text. Capped at the screen (5% margin): the text then auto-sizes (fit engine). The stock
 * values come back as soon as the view shows another text or closes (lbox_fit_frame): the game's own notices as before. */
static struct { int on, pad_top; uintptr_t h_bg, h_tx; float bg_h, bg_y, tx_h, tx_y, extra; uint32_t hash; } LG;
static void *lg_pad, *lg_top, *lg_settop, *lg_mark, *lg_pivot;
static int lg_methods(void) {
    static int looked;
    if (!looked) { looked = 1; lg_pad = mth("UnityEngine.UI", "LayoutGroup", "get_padding", 0); lg_top = mth("UnityEngine", "RectOffset", "get_top", 0);
                   lg_settop = mth("UnityEngine", "RectOffset", "set_top", 1); lg_mark = mth("UnityEngine.UI", "LayoutRebuilder", "MarkLayoutForRebuild", 1);
                   lg_pivot = mth("UnityEngine", "RectTransform", "get_pivot", 0); }
    return lg_pad && lg_top && lg_settop && lg_mark && lg_pivot;
}
static Obj lg_padding(Obj bg) { Obj g = comp_of(bg, "UnityEngine.UI", "LayoutGroup"); return g && lg_methods() ? MCALL(lg_pad, Obj, Obj, const void *)(g, lg_pad) : NULL; }
static void lg_set(Obj rt, float h, float y) {                               /* sizeDelta.y, anchoredPosition.y */
    V2 sd = F(0x510701C, V2 (*)(Obj, void *))(rt, NULL), ap = F(0x5106E38, V2 (*)(Obj, void *))(rt, NULL);
    sd.y = h; ap.y = y;
    F(0x5107110, void (*)(Obj, V2, void *))(rt, sd, NULL); F(0x5106F2C, void (*)(Obj, V2, void *))(rt, ap, NULL);
}
static void lbox_restore(void) {
    if (!LG.on) return;
    LG.on = 0;
    Obj bg = u_gc_get(LG.h_bg), tx = u_gc_get(LG.h_tx);
    if (u_alive(tx) && mm_is(tx, K_RT)) lg_set(tx, LG.tx_h, LG.tx_y);
    if (u_alive(bg) && mm_is(bg, K_RT)) {
        lg_set(bg, LG.bg_h, LG.bg_y);
        Obj po = lg_padding(bg);
        if (po) MCALL(lg_settop, void, Obj, int, const void *)(po, LG.pad_top, lg_settop);
        MCALL(lg_mark, void, Obj, const void *)(bg, lg_mark);
    }
    u_gc_free(LG.h_bg); u_gc_free(LG.h_tx); LG.h_bg = LG.h_tx = 0;
    rig_log("mpmenu: notice box: stock size again (was %.0f higher)", LG.extra);
}
/* at UI scale 1.5 the stock box is wider than the free area right of the title column, and the column draws over its left
   part (the body text lost its first letters): on the title, the box moves right until it clears the column (+20% of the
   column's width, its frame); back to its place when it closes or shows another text (lbox_fit_frame) */
static struct { int on; uintptr_t h; float x; uint32_t hash; } LS;
static void lbox_unshift(void) {
    if (!LS.on) return;
    LS.on = 0;
    Obj p = u_gc_get(LS.h);
    if (u_alive(p) && mm_is(p, K_RT)) { V2 ap = F(0x5106E38, V2 (*)(Obj, void *))(p, NULL); ap.x = LS.x; F(0x5106F2C, void (*)(Obj, V2, void *))(p, ap, NULL); }
    u_gc_free(LS.h); LS.h = 0;
}
static void lbox_shift(Obj v) {
    lbox_unshift();
    Obj tv = title_view(); if (!u_alive(v) || !u_on(v) || !u_alive(tv) || !u_on(tv)) return;
    float cl = 1e9f, cr = -1e9f, r[4];                                       /* the title column: its visible buttons (stock + ours) */
    for (int i = 0; i < 7; i++) { Obj b = AT(tv, 0x30 + 8 * i, Obj); if (u_on(b) && rect_of(b, r)) { if (r[0] < cl) cl = r[0]; if (r[2] > cr) cr = r[2]; } }
    for (int i = 0; i < NT; i++) { Obj b = tb(title_ids[i]); if (u_on(b) && rect_of(b, r)) { if (r[0] < cl) cl = r[0]; if (r[2] > cr) cr = r[2]; } }
    if (cr < cl) return;
    Obj p = tr_find(u_tr(v), "Panel_Notice"), bg = tr_find(u_tr(v), "Panel_Notice/Img_BackGroundBox");
    float br[4]; if (!u_alive(p) || !mm_is(p, K_RT) || !u_alive(bg) || !mm_is(bg, K_RT) || !rect_of(bg, br)) return;
    float lim = cr + 0.2f * (cr - cl), dx = lim - br[0], sw = (float)F(0x5092C94, int (*)(void *))(NULL);   /* Screen.width */
    if (dx < 1) return;
    if (sw > 0 && br[2] + dx > sw * 0.98f) dx = sw * 0.98f - br[2];          /* never off the right edge */
    V2 bsd = F(0x510701C, V2 (*)(Obj, void *))(bg, NULL); float sc = bsd.x > 1 ? (br[2] - br[0]) / bsd.x : 0;   /* screen px per canvas unit */
    if (dx < 1 || sc <= 0) return;
    V2 ap = F(0x5106E38, V2 (*)(Obj, void *))(p, NULL);
    LS.x = ap.x; ap.x += dx / sc; F(0x5106F2C, void (*)(Obj, V2, void *))(p, ap, NULL);
    char rb[2048] = ""; str_utf8(tmp_get(AT(v, 0x30, Obj)), rb, sizeof rb); LS.hash = fit_hash(rb);
    LS.h = u_gc_new(p); LS.on = 1;
    rig_log("mpmenu: notice box: %.0f px right, clear of the title column (right edge %.0f)", dx, cr);
}
static void lbox_grow(Obj v) {
    lbox_restore();
    lbox_shift(v);
    if (!u_alive(v) || !u_on(v) || !fit_methods() || !lg_methods()) return;
    Obj t = AT(v, 0x30, Obj); if (!u_alive(t) || !mm_is(t, K_TMP)) return;
    Obj tx = u_tr(t), bg = tr_find(u_tr(v), "Panel_Notice/Img_BackGroundBox");
    if (!mm_is(tx, K_RT) || !u_alive(bg) || !mm_is(bg, K_RT)) return;
    Obj po = lg_padding(bg); if (!po) return;
    fit_rect r = MCALL(m_fit_rect, fit_rect, Obj, const void *)(tx, m_fit_rect);
    float need = txt_fit_h(t, r.w, r.h), extra = need > r.h + 1 ? (float)(int)(need - r.h + 4.99f) : 0;
    if (extra < 1) return;
    V2 bsd = F(0x510701C, V2 (*)(Obj, void *))(bg, NULL), bap = F(0x5106E38, V2 (*)(Obj, void *))(bg, NULL);
    V2 tsd = F(0x510701C, V2 (*)(Obj, void *))(tx, NULL), tap = F(0x5106E38, V2 (*)(Obj, void *))(tx, NULL);
    float br[4] = { 0 }; rect_of(bg, br);
    float sh = (float)F(0x5092CD4, int (*)(void *))(NULL), sc = bsd.y > 1 ? (br[3] - br[1]) / bsd.y : 0;   /* screen px per canvas unit */
    if (sc > 0 && sh > 0) {                                                  /* the box grows by extra / 2 each way: room on its tighter side */
        float room = br[1] - sh * 0.05f; if (sh * 0.95f - br[3] < room) room = sh * 0.95f - br[3];
        float mx = (float)(int)(2 * room / sc); if (mx < 0) mx = 0;
        if (extra > mx) { rig_log("mpmenu: notice box: %.0f higher needed, the screen has room for %.0f (the text auto-sizes)", extra, mx); extra = mx; }
        if (extra < 1) return;
    }
    V2 bpv = MCALL(lg_pivot, V2, Obj, const void *)(bg, lg_pivot), tpv = MCALL(lg_pivot, V2, Obj, const void *)(tx, lg_pivot);
    LG.bg_h = bsd.y; LG.bg_y = bap.y; LG.tx_h = tsd.y; LG.tx_y = tap.y; LG.extra = extra;
    LG.pad_top = MCALL(lg_top, int, Obj, const void *)(po, lg_top);
    lg_set(tx, tsd.y + extra, tap.y + extra * (tpv.y - 0.5f));               /* the same centre */
    lg_set(bg, bsd.y + extra, bap.y + extra * (bpv.y - 0.5f));               /* the same centre, extra / 2 more each way */
    MCALL(lg_settop, void, Obj, int, const void *)(po, LG.pad_top + (int)extra, lg_settop);   /* the button row: extra lower from the box's top */
    MCALL(lg_mark, void, Obj, const void *)(bg, lg_mark);
    char rb[2048] = ""; str_utf8(tmp_get(t), rb, sizeof rb); LG.hash = fit_hash(rb);
    LG.h_bg = u_gc_new(bg); LG.h_tx = u_gc_new(tx); LG.on = 1;
    rig_log("mpmenu: notice box: %.0f higher for its text (needs %.0f, rect %.0f)", extra, need, r.h);
}
static void lbox_fit_frame(void) {                                           /* the box closed or shows another text: stock size and place */
    if (!LG.on && !LS.on) return;
    Obj v = lbox_view(); char t[2048] = "";
    if (u_alive(v) && u_on(v)) str_utf8(tmp_get(AT(v, 0x30, Obj)), t, sizeof t);
    uint32_t h = t[0] ? fit_hash(t) : 0;
    if (LG.on && (!t[0] || h != LG.hash)) lbox_restore();
    if (LS.on && (!t[0] || h != LS.hash)) lbox_unshift();
}
static int leave_box_text(char *out, size_t cap) {             /* returns how many other players are in (they go back to the title) */
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    char names[200] = ""; size_t o = 0; int n = 0, me = c ? dsnet_player_id(c) : 0;
    uint32_t pm = c ? dsnet_present_mask(c) : 0;
    for (int p = 0; c && p < DSNET_MAX_PLAYERS; p++) {
        if (p == me || !(pm >> p & 1u)) continue;
        const char *nm = dsnet_player_name(c, p);
        o += (size_t)snprintf(names + o, o < sizeof names ? sizeof names - o : 0, "%s<color=%s>%s</color>", n ? TR(", ") : "", ui_player_color(p), nm && nm[0] ? nm : TR("A player"));
        n++;
    }
    char who[400];
    if (n) snprintf(who, sizeof who, TRN("%s goes back to the title.", "%s go back to the title.", n), names);
    int quit = M.confirm_leave == 2, lobby = M.state == MPM_LOBBY;           /* lobby: Quit (then the title) or Turn off multiplayer */
    snprintf(out, cap, "<size=150%%>%s</size>\n%s\n<size=88%%><color=#a69085>%s</color></size>",
             n ? TR("End the game for everyone?") : quit ? TR("Quit to the title?") : lobby ? TR("Turn off multiplayer?") : TR("Leave multiplayer?"),
             n ? who : TR("Nobody else is in this world now. Friends can no longer join."),
             quit ? TR("The world stays on your computer. You go back to the title.") : TR("The world stays on your computer. You keep playing it alone."));
    return n;
}
static void leave_box_show(void) {
    /* the stock box draws under the Esc menu (and the lobby, which lives on it): close that first; the box then stands
       alone over the world. Cancel in the lobby brings the lobby back (mpmenu_frame); in play the game goes on. */
    Obj ev = esc_view(); if (ev && u_on(ev) && mm_is(ev, K_SMV)) F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL);   /* SystemMenuView.OnReturnToGamePressed */
    Obj v = lbox_view_now();
    if (!u_alive(v) || !mm_is(v, K_CNV)) { pending_click = MPB_LEAVE_CONFIRM; rig_log("mpmenu: no notice box: Leave goes ahead"); return; }
    int others = leave_box_text(lbox_text, sizeof lbox_text);
    F(0xF652A8, void (*)(Obj, Obj, Obj, Obj, bool, void *))(v, u_str_ok(lbox_text), NULL, NULL, false, NULL);   /* Show_Select */
    tmp_set(AT(v, 0x30, Obj), lbox_text);
    txt_fit_stock(AT(v, 0x30, Obj));
    tmp_set(AT(v, 0x40, Obj), others ? TR("End for everyone") : M.confirm_leave == 2 ? TR("Quit") : M.state == MPM_LOBBY ? TR("Turn off") : TR("Leave"));   /* from the box's own case, not its text */
    tmp_set(AT(v, 0x50, Obj), mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")));
    { Obj nt = AT(v, 0x30, Obj); if (u_alive(nt) && mm_is(nt, K_TMP)) F(0x4F0B7BC, void (*)(Obj, int, void *))(nt, 514, NULL); }   /* centred, as the ui lane's boxes */
    u_go_active(u_go(v), true);
    lbox_grow(v);
    lbox_up = BX_LEAVE;
    rig_log("mpmenu: Leave box: %s", lbox_text);
}
static void leave_box_close(void) {                                         /* our box only: the stock Cancel (it closes the view) */
    Obj v = lbox_view();
    int ours = lbox_ours(v);
    lbox_up = 0;
    if (ours) F(0xF65638, void (*)(Obj, void *))(v, NULL);                    /* OnClick_Cancle (through the ui lane's hook: not theirs) */
}
/* mp_esc.c: one of our boxes is up (they lie over every window of ours; the windows step off the kit's Escape stack) */
int mpmenu_box_over(void) { return lbox_up != BX_NONE && lbox_ours(lbox_view()); }
/* Escape on our boxes (owner 2026-10-25). The game's Escape event reaches mpmenu_escape_ours (InputEventHandler.OnEvent hook,
 * ui_game.c), which presses the box's Cancel; but the title page's own Escape check (observe) also went Back on that same
 * press (a box over the Multiplayer page: the box and the page closed). With the loader kit's Escape (kit 0.1.28) the box is
 * on the kit's stack while it shows, title and world: it is on top (it shows last; our windows step off while it is up,
 * mp_esc.c), Escape = its Cancel as before, the game sees no press and our own checks skip it (mpesc_took). It is the stock
 * CommonNoticeView every box shares: dropped the frame our text leaves it, and the close checks it is still ours (else it
 * does nothing). Without that kit: as before. */
static void lbox_esc_close(void *ud);
static mpesc_win esc_lbox = { .name = "our notice box", .close = lbox_esc_close };
static void lbox_esc_close(void *ud) {
    (void)ud; Obj v = lbox_view();
    if (lbox_up != BX_NONE && lbox_ours(v)) { rig_log("mpmenu: Escape = Cancel in our box %d", lbox_up); F(0xF65638, void (*)(Obj, void *))(v, NULL); }
    mpesc_drop(&esc_lbox);
}
static void lbox_esc_frame(void) {
    Obj v = lbox_up != BX_NONE ? lbox_view() : NULL;
    mpesc_sync(&esc_lbox, v, v && lbox_ours(v), MPESC_BOX);
}
static void leave_box_frame(void) {
    if (lbox_up != BX_LEAVE) return;
    if (!M.confirm_leave) { leave_box_close(); return; }                     /* the model closed it (session ended, state change) */
    if (!lbox_ours(lbox_view())) { lbox_up = 0; pending_click = MPB_LEAVE_CANCEL; rig_log("mpmenu: Leave box gone (closed or replaced): cancel"); }
}
/* ui lane (dlsym, CommonNoticeView click hooks): 1 = the click was on our Leave box */
static void invite_click(int confirm);
static void tag_answer(int confirm);
__attribute__((visibility("default"))) int mpmenu_notice_click(int confirm) {
    if (lbox_up > BX_LEAVE && lbox_ours(lbox_view())) { invite_click(confirm); return 1; }   /* invite / joining boxes */
    if (!M.confirm_leave || !lbox_ours(lbox_view())) return 0;
    lbox_up = 0; pending_click = confirm ? MPB_LEAVE_CONFIRM : MPB_LEAVE_CANCEL;
    rig_log("mpmenu: Leave box (%s): %s", M.confirm_leave == 2 ? "Quit" : "Turn off", confirm ? "confirm" : "Cancel");
    return 1;
}
/* ---------------------------------------------------------------- invite during a single-player game (lan_v10, ux_flow F9; core API) */
/* An accepted invite while the player's own world runs waits for an answer (mp_game_invite_pending / _answer, dlsym):
 * box 1 "Join <host>? Your progress here is saved first." [Cancel] [Save and join] (answer 2); if the save fails, box 2
 * [Cancel] [Join anyway] (answer 1). Then, in the world, "Joining <host>'s game..." with Cancel (mp_steam_leave) until
 * the host's world is in (the title has its own line). Escape = Cancel. */
static int (*inv_pending)(char *, size_t), (*inv_answer)(int);
static char inv_host[64]; static int join_box_on, join_phase_shown = -1, nosave_ask;   /* nosave_ask: show the second box next frame */
static void box_show(int kind, const char *text, const char *yes, const char *no) {
    Obj ev = esc_view(); if (kind != BX_UNTAG && ev && u_on(ev) && mm_is(ev, K_SMV)) F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL);   /* not under the Esc menu (the load list stays) */
    Obj v = lbox_view_now();
    if (!u_alive(v) || !mm_is(v, K_CNV)) { rig_log("mpmenu: no notice box for: %s", text); return; }
    snprintf(lbox_text, sizeof lbox_text, "%s", text);
    if (no) F(0xF652A8, void (*)(Obj, Obj, Obj, Obj, bool, void *))(v, u_str_ok(lbox_text), NULL, NULL, false, NULL);   /* Show_Select */
    else F(0xF65498, void (*)(Obj, Obj, Obj, void *))(v, u_str_ok(lbox_text), NULL, NULL);                            /* Show_Confirm: one button */
    tmp_set(AT(v, 0x30, Obj), lbox_text);
    txt_fit_stock(AT(v, 0x30, Obj));
    tmp_set(AT(v, 0x40, Obj), yes);
    if (no) tmp_set(AT(v, 0x50, Obj), no);
    { Obj nt = AT(v, 0x30, Obj); if (u_alive(nt) && mm_is(nt, K_TMP)) F(0x4F0B7BC, void (*)(Obj, int, void *))(nt, 514, NULL); }
    u_go_active(u_go(v), true);
    lbox_grow(v);                                                           /* a text too long for the stock box: it grows (lbox_fit_frame: back) */
    lbox_up = kind;
    rig_log("mpmenu: box %d: %s", kind, text);
}
static void box_close(void) { Obj v = lbox_view(); int ours = lbox_ours(v); lbox_up = BX_NONE; if (ours) F(0xF65638, void (*)(Obj, void *))(v, NULL); }
/* macOS blocked the local network (join_poll): the message + "Open Settings", which opens Privacy & Security > Local Network.
 * The player clicked it, so a normal open (NSWorkspace openURL:). Test copies log it and open nothing (rig_quiet.inc). */
static void lanblock_box(const char *text) {
    box_show(BX_LANBLOCK, text, TR("Open Settings"), mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")));
}
static void lanblock_open_settings(void) {
#ifdef __APPLE__
    id s = ((id (*)(Class, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"),
                                                          "x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork");
    id url = s ? ((id (*)(Class, SEL, id))objc_msgSend)(objc_getClass("NSURL"), sel_registerName("URLWithString:"), s) : nil;
    id ws = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSWorkspace"), sel_registerName("sharedWorkspace"));
    BOOL ok = url && ws ? ((BOOL (*)(id, SEL, id))objc_msgSend)(ws, sel_registerName("openURL:"), url) : NO;
    rig_log("mpmenu: Open Settings (Local Network): %s", ok ? "opened" : "failed");
#endif
}
/* ---------------------------------------------------------------- host rule "Spectators" (owner 2026-10-14)
 * Outside the sim (dsnet P_SPECT_RULE, mp_set_spectators): on / off at once; off drops every spectator (BYE reason 2).
 * Off while some watch asks first (the stock box). Saved like the other rules (mp_menu.json, the world's line). */
static int spect_watching(void) {
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    return c ? __builtin_popcount(dsnet_spectator_mask(c)) : 0;
}
static void spect_apply(int allow) {
    mp_session *s = mp_game_session();
    int rc = s ? mp_set_spectators(s, allow) : -1;
    M.set.spectators = allow != 0; rules_sent_frame = frames;
    char gid[64] = ""; prefs_save(); if (campaign_guid(gid, sizeof gid)) world_put(gid, &M.set);
    rig_log("mpmenu: spectators %s (rc %d)", allow ? "allowed" : "not allowed", rc);
}
__attribute__((visibility("default"))) int mpmenu_spectators_off_ask(void) {
    int n = spect_watching();
    if (!n) { spect_apply(0); return 0; }
    char t[400];
    char hd[160]; snprintf(hd, sizeof hd, TRN("Remove the spectator now?", "Remove the %d spectators now?", n), n);
    snprintf(t, sizeof t, "<size=150%%>%s</size>\n%s", hd, TR("They go back to the title. Nobody can watch until you allow spectators again."));
    box_show(BX_SPECT_OFF, t, TR("Remove"), mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")));
    if (lbox_up != BX_SPECT_OFF) { spect_apply(0); return 0; }            /* no box could be shown: the host asked for it, apply */
    return 1;
}
__attribute__((visibility("default"))) int mpmenu_spectators_set(int allow) {
    if (allow) { spect_apply(1); return 0; }
    return mpmenu_spectators_off_ask();
}
static void invite_answer(int choice) {
    if (!inv_answer) return;
    int rc = inv_answer(choice);
    rig_log("mpmenu: invite from %s: answer %d -> rc %d", inv_host, choice, rc);
    if (choice && !rc) join_box_on = 1;                                     /* the join runs: the Joining box */
    if (choice == 2 && rc == -2) {
        if (inv_pending && inv_pending(NULL, 0)) nosave_ask = 1;            /* the save failed: "Join anyway?" once this box has closed */
        else {
            char bd[400]; snprintf(bd, sizeof bd, TR("So you did not join %s's game. Save from the Esc menu, then ask %s to invite you again."), inv_host, inv_host);
            snprintf(M.notice, sizeof M.notice, "<size=150%%>%s</size>\n%s", TR("Could not save your game"), bd);
            M.state = MPM_FAILED; mp_steam_ack();
        }
    }
}
static void hostmods_click(int confirm);
static void solo_answer(int yes);
static void invite_click(int confirm) {
    int k = lbox_up; lbox_up = BX_NONE;
    if (k == BX_INVITE) invite_answer(confirm ? 2 : 0);
    else if (k == BX_NOSAVE) invite_answer(confirm ? 1 : 0);
    else if (k == BX_JOINING) { join_box_on = 0; mp_steam_leave(); rig_log("mpmenu: joining cancelled (box)"); }
    else if (k == BX_REJOIN) { static void (*cn)(void); if (!cn) cn = (void (*)(void))dlsym(RTLD_DEFAULT, "mp_game_autorejoin_cancel");
                               rig_log("mpmenu: rejoining cancelled (box)"); if (cn) cn(); }
    else if (k == BX_SPECT_OFF) { if (confirm) spect_apply(0); else rig_log("mpmenu: spectators stay (box: Cancel)"); }
    else if (k == BX_UNTAG) tag_answer(confirm);
    else if (k == BX_LANBLOCK && confirm) lanblock_open_settings();
    else if (k == BX_HOSTMODS) hostmods_click(confirm);
    else if (k == BX_SOLO) solo_answer(confirm);
}
static void invite_frame(const mpm_obs *o) {
    static int looked; if (!looked) { looked = 1; inv_pending = (int (*)(char *, size_t))dlsym(RTLD_DEFAULT, "mp_game_invite_pending"); inv_answer = (int (*)(int))dlsym(RTLD_DEFAULT, "mp_game_invite_answer"); }
    Obj v = lbox_view();
    if (lbox_up == BX_SPECT_OFF && !lbox_ours(v)) { lbox_up = BX_NONE; rig_log("mpmenu: spectators box gone: they stay"); }   /* closed another way = Cancel */
    if (lbox_up == BX_UNTAG && !lbox_ours(v)) { lbox_up = BX_NONE; tag_answer(0); }                                       /* closed another way = Cancel */
    if (lbox_up == BX_LANBLOCK && !lbox_ours(v)) lbox_up = BX_NONE;                                                      /* closed another way: nothing to do */
    if ((lbox_up == BX_INVITE || lbox_up == BX_NOSAVE || lbox_up == BX_JOINING) && !lbox_ours(v)) {   /* closed or taken by another box: Cancel */
        int k = lbox_up; lbox_up = BX_NONE;
        if (k == BX_JOINING) { if (join_box_on && frames % 30 == 0) join_phase_shown = -1; }   /* shown again below */
        else if (inv_pending && inv_pending(NULL, 0)) { invite_answer(0); rig_log("mpmenu: invite box gone: cancel"); }
    }
    if (nosave_ask && lbox_up == BX_NONE && !u_on(v)) {                     /* the first box's click has closed it: now the second one */
        nosave_ask = 0;
        if (inv_pending && inv_pending(NULL, 0)) {
            char t[400];
            char q[200]; snprintf(q, sizeof q, TR("Join %s's game anyway?"), inv_host);
            snprintf(t, sizeof t, "<size=150%%>%s</size>\n%s\n<size=88%%><color=#a69085>%s</color></size>", TR("Your game could not be saved"), q, TR("Unsaved progress in this world will be lost."));
            box_show(BX_NOSAVE, t, TR("Join anyway"), mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")));
        }
    }
    if (nosave_ask) return;
    if (inv_pending && o->scene == 2 && !M.confirm_leave && lbox_up == BX_NONE && inv_pending(inv_host, sizeof inv_host)) {
        char t[400];
        char hd[160], bd[200]; snprintf(hd, sizeof hd, TR("Join %s?"), inv_host); snprintf(bd, sizeof bd, TR("%s invited you to their world."), inv_host);
        snprintf(t, sizeof t, "<size=150%%>%s</size>\n%s\n<size=88%%><color=#a69085>%s</color></size>", hd, bd, TR("Your progress here is saved first, then this world closes."));
        box_show(BX_INVITE, t, TR("Save and join"), mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")));
    }
    /* "Connection lost, rejoining..." (mp_game.c auto rejoin): one Cancel button, instead of the Joining box */
    static int (*ar_state)(void); static int ar_looked, ar_shown;
    if (!ar_looked) { ar_looked = 1; ar_state = (int (*)(void))dlsym(RTLD_DEFAULT, "mp_game_autorejoin"); }
    int ar = ar_state ? ar_state() : 0;
    if (lbox_up == BX_REJOIN && !lbox_ours(v)) { lbox_up = BX_NONE; ar_shown = 0; }   /* closed another way: shown again below */
    if (ar) {
        join_box_on = 0; join_phase_shown = -1;
        if (lbox_up == BX_JOINING) box_close();
        int free_view = lbox_up == BX_NONE && !M.confirm_leave && !(u_alive(v) && u_on(v)) && frames % 15 == 0;   /* never over another box */
        if (o->scene == 2 && ar != ar_shown && (lbox_up == BX_REJOIN || free_view)) {
            char t[300]; snprintf(t, sizeof t, "<size=150%%>%s</size>\n<size=88%%><color=#a69085>%s</color></size>", TR("Connection lost, rejoining..."),
                                  ar == 2 ? TR("Loading the world") : TR("Connecting"));
            box_show(BX_REJOIN, t, mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")), NULL);
            ar_shown = lbox_up == BX_REJOIN ? ar : 0;
        }
        return;
    }
    if (lbox_up == BX_REJOIN) box_close();
    ar_shown = 0;
    /* "Joining <host>'s game..." in the world (a join started from the player's own world) */
    mp_steam_info in; mp_steam_state(&in);
    static int prev_role; int role = o->role;
    if (prev_role == 0 && role == 2 && o->scene == 2) join_box_on = 1;     /* any join that starts in the world */
    prev_role = role;
    if (join_box_on && (in.state != MP_STEAM_JOINING || o->scene != 2)) { join_box_on = 0; join_phase_shown = -1; if (lbox_up == BX_JOINING) box_close(); }
    if (join_box_on && lbox_up == BX_NONE && !M.confirm_leave && !(inv_pending && inv_pending(NULL, 0))) join_phase_shown = -1;
    if (join_box_on && (lbox_up == BX_NONE || lbox_up == BX_JOINING) && in.phase != join_phase_shown) {
        const char *h = in.host_name[0] ? in.host_name : inv_host[0] ? inv_host : TR("the host");
        char hd[160]; snprintf(hd, sizeof hd, TR("Joining %s's game..."), h);
        char t[300]; snprintf(t, sizeof t, "<size=150%%>%s</size>\n<size=88%%><color=#a69085>%s</color></size>", hd,
                              in.phase == MP_PH_LOADING ? TR("Loading the world") : in.phase == MP_PH_WAITING_SAVE ? TR("Waiting for the world")
                              : in.phase == MP_PH_HOST_WINDOW ? TR("Waiting for the host to close a window...") : TR("Connecting"));
        box_show(BX_JOINING, t, mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")), NULL);
        join_phase_shown = in.phase;
    }
}
static void center_message(const char *t) {                                 /* ui lane's big centred message (HelpNoticeView) */
    static void (*fn)(const char *); static int looked;
    if (!looked) { looked = 1; fn = (void (*)(const char *))dlsym(RTLD_DEFAULT, "ui_center_message"); }
    rig_log("mpmenu: message: %s", t);
    if (fn) fn(t);
}
static char last_message[200];                                              /* test read-back */
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(last_message, sizeof last_message, fmt, ap); va_end(ap); center_message(last_message); }

/* ---------------------------------------------------------------- rule changes are told to every player (lan_v10, ux_flow F12) */
/* Admin mode lets the host order every unit; Units per player changes what a player may take. Every guest watches the
 * session rules (dsnet, the same on all) once play started, and says what changed (the host made the change). */
static void rules_watch(const mpm_obs *o) {
    static int seen, adm0; static dsnet_rules r0;
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    if (!c || !o->campaign_ready || o->loading || dsnet_player_id(c) < 0) { seen = 0; return; }
    if (frames % 15) return;
    dsnet_rules r; dsnet_rules_get(c, &r); int adm = dsnet_admin_mode(c);
    /* the first 4 s of a session (welcome, save, rules at host start) and the lobby (its window shows the rules): no message */
    if (seen < 8 || M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY || dsnet_player_id(c) == 0) { r0 = r; adm0 = adm; seen++; return; }
    const char *hn = dsnet_player_name(c, 0); char host[48]; snprintf(host, sizeof host, "<color=%s>%s</color>", ui_player_color(0), hn && hn[0] ? hn : TR("The host"));
    if (adm != adm0) say(adm ? TR("%s turned Admin mode on: the host can control every unit.") : TR("%s turned Admin mode off: the host controls only their own units."), host);
    if (r.units_per_player != r0.units_per_player)
        say(TR("Units per player is now %d (host rule)."), r.units_per_player);
    if (r.pause_mode != r0.pause_mode) say(TR("Pause is now: %s (host rule)."), r.pause_mode == MPM_PAUSE_NONE ? TR("no pause") : r.pause_mode == MPM_PAUSE_ALL_VOTE ? TR("pause by vote") : TR("anyone can pause"));
    if ((r.allow_speedup != 0) != (r0.allow_speedup != 0)) say(TR("Speed-up is now %s (host rule)."), r.allow_speedup ? TR("on") : TR("off"));
    if (r.gear_mode != r0.gear_mode) say(TR("Gear is now: %s (host rule)."), r.gear_mode == MPM_GEAR_SHARED_ANY ? TR("shared, anyone equips any unit") : r.gear_mode == MPM_GEAR_OWN_STORAGE ? TR("own storage per player") : TR("shared, equip your own units"));
    if (r.event_choice != r0.event_choice) { mpm_settings t = { 0 }; t.events = r.event_choice; char v[64]; mpm_rule_value(&t, 5, v, sizeof v); say(TR("Event choices: %s (host rule)."), v); }
    if (r.population != r0.population) { mpm_settings t = { 0 }; t.population = r.population; char v[64]; mpm_rule_value(&t, 4, v, sizeof v); say(TR("Population: %s (host rule)."), v); }
    if (r.shared_fog != r0.shared_fog) say(TR("Shared fog of war: %s (host rule)."), r.shared_fog ? TR("On") : TR("Off"));
    r0 = r; adm0 = adm;
}

/* ---------------------------------------------------------------- "Host from a save": the stock load list, retitled (ux #18) */
/* SaveLoadView _saveLoadTitle +0x48 (DASText), _loadButton +0xA0 (DASButton, label = first TMP_Text below it).
 * Retitled while the menu picks a save to host; the stock texts come back when it closes. */
static uintptr_t h_pick_title, h_pick_btn; static char pick_title0[128], pick_btn0[64]; static bool pick_key0[2]; static int pick_on;
static Obj tmp_below(Obj tr, int depth) {
    static void *k; if (!k) k = class_by_name("TMPro", "TMP_Text");
    if (!k || !u_alive(tr)) return NULL;
    Obj ty = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k));
    Obj t = (mm_is(tr, K_COMP) ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(tr, ty, NULL) : NULL);
    if (u_alive(t) || depth >= 3) return u_alive(t) ? t : NULL;
    int n = (mm_is(tr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(tr, NULL) : 0);
    for (int i = 0; i < n; i++) { Obj c = tmp_below((mm_is(tr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(tr, i, NULL) : NULL), depth + 1); if (c) return c; }
    return NULL;
}
static void pick_frame(const mpm_obs *o) {
    int want = M.state == MPM_PICK_SAVE && o->picker_open;
    if (want && frames % 5 == 0) {
        Obj slv = find_view_x(&h_slv, &k_slv, "Refactor", "SaveLoadView", true);
        Obj t = slv ? AT(slv, 0x48, Obj) : NULL, lb = slv ? AT(slv, 0xA0, Obj) : NULL, bt = u_alive(lb) ? tmp_below(u_tr(lb), 0) : NULL;
        if (!u_alive(t)) return;
        if (!pick_on) {
            str_utf8(tmp_get(t), pick_title0, sizeof pick_title0); pick_key0[0] = das_key_on(t);
            pick_btn0[0] = 0; if (bt) { str_utf8(tmp_get(bt), pick_btn0, sizeof pick_btn0); pick_key0[1] = das_key_on(bt); }
            u_gc_free(h_pick_title); h_pick_title = u_gc_new(t); u_gc_free(h_pick_btn); h_pick_btn = bt ? u_gc_new(bt) : 0;
            pick_on = 1; rig_log("mpmenu: load list retitled (was \"%s\" / \"%s\")", pick_title0, pick_btn0);
        }
        char cur[128] = ""; str_utf8(tmp_get(t), cur, sizeof cur); if (strcmp(cur, TR("Host a world"))) tmp_set(t, TR("Host a world"));   /* our own text read back */
        if (bt) { str_utf8(tmp_get(bt), cur, sizeof cur); if (strcmp(cur, TRC("verb", "Host"))) tmp_set(bt, TRC("verb", "Host")); }
    } else if (!want && pick_on) {
        Obj t = u_gc_get(h_pick_title), bt = u_gc_get(h_pick_btn);
        if (u_alive(t)) { tmp_set(t, pick_title0); if (pick_key0[0]) das_key_set(t, true); }
        if (u_alive(bt)) { tmp_set(bt, pick_btn0); if (pick_key0[1]) das_key_set(bt, true); }
        u_gc_free(h_pick_title); u_gc_free(h_pick_btn); h_pick_title = h_pick_btn = 0; pick_on = 0;
        rig_log("mpmenu: load list titles restored");
    }
}

/* ---------------------------------------------------------------- Steam friends hosting (lan_v8) */
/* Steam friends playing this game who sit in a lobby (dsnet_steam_friends_in_game): one "Join <name>'s world" each. */
static uint64_t friend_lobby[6], friend_id[6];
static void friends_refresh(void) {
    dsnet_steam_friend f[32]; int n = M.steam ? dsnet_steam_friends_in_game(f, 32) : 0, k = 0;
    for (int i = 0; i < n && k < 6; i++) if (f[i].lobby) { friend_lobby[k] = f[i].lobby; friend_id[k] = f[i].steamid; snprintf(M.friends[k], sizeof M.friends[k], "%s", f[i].name); k++; }
    if (k != M.nfriends) rig_log("mpmenu: %d Steam friend(s) hosting", k);
    M.nfriends = k;
}

/* ---------------------------------------------------------------- Recent worlds (owner, lan_v8) */
/* <app data folder>/mp_history.json: a JSON array, one world per line, newest first, max 12. Written every ~10 s while
 * a session plays (so the last state survives a crash or a host leaving), for guests and hosts. Click = reconnect the
 * same way (TCP to the address, or Steam to that host's lobby); "x" removes a row. */
static void hist_save(void) {
    char p[1200], tmp[1300]; app_path(p, sizeof p, "mp_history.json"); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    fputs("[\n", f);
    for (int i = 0; i < nhist; i++) {
        hist_t *h = &hist[i]; Buf b = {0};
        bput(&b, "{\"key\":"); bstr(&b, h->key); bput(&b, ",\"world\":"); bstr(&b, h->world); bput(&b, ",\"day\":%lld,\"how\":", h->day); bstr(&b, h->how);
        bput(&b, ",\"addr\":"); bstr(&b, h->addr); bput(&b, ",\"host\":"); bstr(&b, h->host); bput(&b, ",\"steamid\":\"%llu\",\"players\":", (unsigned long long)h->steamid);
        bstr(&b, h->players); bput(&b, ",\"units\":"); bstr(&b, h->units); bput(&b, ",\"portraits\":"); bstr(&b, h->portraits);
        bput(&b, ",\"when\":%lld}%s\n", h->when, i + 1 < nhist ? "," : "");
        fputs(b.p, f); free(b.p);
    }
    fputs("]\n", f); fclose(f); rename(tmp, p);
}
static void hist_load(void) {
    hist_loaded = 1; nhist = 0;
    char p[1200], line[2048]; app_path(p, sizeof p, "mp_history.json");
    FILE *f = fopen(p, "r"); if (!f) return;
    while (nhist < HIST_MAX && fgets(line, sizeof line, f)) {
        if (line[0] != '{') continue;
        hist_t *h = &hist[nhist]; memset(h, 0, sizeof *h); double v; char sid[32] = "";
        if (!jstr(line, "key", h->key, sizeof h->key)) continue;
        jstr(line, "world", h->world, sizeof h->world); jstr(line, "how", h->how, sizeof h->how); jstr(line, "addr", h->addr, sizeof h->addr);
        jstr(line, "host", h->host, sizeof h->host); jstr(line, "players", h->players, sizeof h->players); jstr(line, "units", h->units, sizeof h->units); jstr(line, "portraits", h->portraits, sizeof h->portraits);
        if (jnum(line, "day", &v)) h->day = (long long)v; if (jnum(line, "when", &v)) h->when = (long long)v;
        if (jstr(line, "steamid", sid, sizeof sid)) h->steamid = strtoull(sid, NULL, 10);
        nhist++;
    }
    fclose(f);
}
/* the current session as a Recent worlds row (newest first) */
/* palette index -> "#RRGGBB" (ui_player_color without the session's slot -> uid mapping: the index is stored as is) */
static const char *palette_color(int idx) { return ui_palette_color(idx < 0 ? 0 : idx); }   /* the palette entry itself: no session slot, no local override */
static void hist_update(void) {
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL; Obj cd = cdc();
    if (!c || !cd) return;
    if (!hist_loaded) hist_load();
    mp_steam_info in; mp_steam_state(&in);
    hist_t e; memset(&e, 0, sizeof e);
    char gid[64] = ""; campaign_guid(gid, sizeof gid);
    Obj clan = AT(cd, 0x50, Obj); if (clan) str_utf8(AT(clan, 0x20, Obj), e.world, sizeof e.world);     /* ClanDataContainer.ClanName */
    Obj td = AT(cd, 0xA8, Obj); if (td) e.day = AT(td, 0x20, int64_t);                              /* CampaignDataContainer.TimeData (dump 25846389) .Day */
    if (in.is_host && mp_hub_guid(gid)) return;                             /* no hub row to host again: the hub is never saved */
    if (in.is_host) { snprintf(e.how, sizeof e.how, "host"); snprintf(e.addr, sizeof e.addr, "%s", M.direct); snprintf(e.host, sizeof e.host, "%s", my_name); }
    else if (!strcmp(joined_how, "ip")) { snprintf(e.how, sizeof e.how, "ip"); snprintf(e.addr, sizeof e.addr, "%s", joined_addr); snprintf(e.host, sizeof e.host, "%s", in.host_name); }
    else { snprintf(e.how, sizeof e.how, "steam"); e.steamid = joined_steamid ? joined_steamid : dsnet_steam_lobby_owner(in.lobby); snprintf(e.host, sizeof e.host, "%s", in.host_name); }
    if (!e.host[0]) snprintf(e.host, sizeof e.host, "the host");
    size_t o = 0;
    for (int i = 0; i < in.nplayers; i++) if (in.players[i].id != in.player)
        o += (size_t)snprintf(e.players + o, sizeof e.players - (o < sizeof e.players ? o : sizeof e.players), "%s%s|%d", o ? ";" : "", in.players[i].name, dsnet_player_color(c, in.players[i].id) >= 0 ? dsnet_player_color(c, in.players[i].id) : in.players[i].id);   /* the world's palette index (stable uid); none: the slot */
    uint8_t g[8][16]; int nu = dsnet_owned_list(c, dsnet_player_id(c), g, 8); o = 0; size_t op = 0;
    /* portraits: the shortcut bar's parts (SubUI_UnitShortCutParts: _owner +0x228, _portraitImage +0x140 (dump 25846389)): the sprite name */
    static void *kparts; if (!kparts) kparts = class_by_name("Refactor.UI", "SubUI_UnitShortCutParts");
    Obj parr = kparts ? F(0x50FCCC0, Obj (*)(Obj, void *))(F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(kparts)), NULL) : NULL;
    int nparts = parr ? (int)AT(parr, 0x18, uintptr_t) : 0;
    for (int i = 0; i < nu && i < 8; i++) {
        Guid gg; memcpy(&gg, g[i], 16); Obj u = find_unit(cur_map(), gg); if (!u && cur_map() != 0) u = find_unit(0, gg);
        char un[48] = ""; if (u) str_utf8(F(0xFA467C, Obj (*)(Obj, void *))(u, NULL), un, sizeof un);   /* GetDisplayName */
        if (!un[0]) continue;
        o += (size_t)snprintf(e.units + o, sizeof e.units - (o < sizeof e.units ? o : sizeof e.units), "%s%s", o ? ", " : "", un);
        char sn[64] = "";
        for (int k = 0; k < nparts && u; k++) {
            Obj pt = AT((char *)parr + 0x20, k * 8, Obj);
            if (!u_alive(pt) || AT(pt, 0x228, Obj) != u) continue;
            Obj im = AT(pt, 0x140, Obj), sp = mm_is(im, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(im, NULL) : NULL;
            if (u_alive(sp)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sp, NULL), sn, sizeof sn);
            break;
        }
        op += (size_t)snprintf(e.portraits + op, sizeof e.portraits - (op < sizeof e.portraits ? op : sizeof e.portraits), "%s%s", op ? "," : "", sn);
    }
    snprintf(e.key, sizeof e.key, "%s|%s|%s", gid, e.how, !strcmp(e.how, "steam") ? "" : e.addr);
    if (!strcmp(e.how, "steam")) { size_t kl = strlen(e.key); snprintf(e.key + kl, sizeof e.key - kl, "%llu", (unsigned long long)e.steamid); }
    e.when = (long long)time(NULL);
    int at = -1; for (int i = 0; i < nhist; i++) if (!strcmp(hist[i].key, e.key)) at = i;
    if (at < 0) at = nhist < HIST_MAX ? nhist++ : HIST_MAX - 1;
    memmove(&hist[1], &hist[0], sizeof hist[0] * at);                   /* newest first */
    hist[0] = e;
    M.nhist = nhist;
    hist_save();
}
/* hist_t.host "the host" is stored data (name unknown): shown in the player's language */
static const char *hist_shown_host(const char *h) { return !strcmp(h, "the host") ? TR("the host") : h; }
static void hist_join(int k) {
    if (k < 0 || k >= nhist) return;
    hist_t *h = &hist[k];
    if (!strcmp(h->how, "host")) {                                          /* F22: straight to the load list, as Host from a save */
        M.state = MPM_PICK_SAVE; M.picker_seen = 0; M.closed_frames = 0; M.msg[0] = 0;
        Obj tv = title_view(); if (tv && mm_is(tv, K_TMV)) F(0xE710B8, void (*)(Obj, void *))(tv, NULL);   /* TitleMenuView.OnLoadCampaignPressed */
        rig_log("mpmenu: recent world %s (hosted): the load list to host it", h->world); return;
    }
    /* F17 (core API, dlsym): the join is for this row's world; a host now running another world refuses it */
    { char gid[64]; snprintf(gid, sizeof gid, "%s", h->key); char *bar = strchr(gid, '|'); if (bar) *bar = 0;
      expect_world(gid); snprintf(hist_world, sizeof hist_world, "%s", h->world); }
    if (!strcmp(h->how, "steam")) {
        if (!M.steam) { snprintf(M.msg, sizeof M.msg, "%s", TR("This world was joined through Steam: needs the Steam version.")); return; }
        { static void (*next_spect)(int); static int looked;                /* core (dlsym): the next mp_game_join watches */
          if (!looked) { looked = 1; next_spect = (void (*)(int))dlsym(RTLD_DEFAULT, "mp_game_next_spectator"); }
          if (M.watch && !next_spect) { snprintf(M.msg, sizeof M.msg, "%s", TR("Spectators cannot join through Steam.")); return; }
          if (next_spect) next_spect(M.watch); }
        friends_refresh();
        for (int i = 0; i < M.nfriends; i++) if (friend_id[i] == h->steamid) {
            int rc = mp_steam_join(friend_lobby[i]);
            if (!rc) { snprintf(joined_how, sizeof joined_how, "steam"); joined_steamid = h->steamid; return; }
        }
        snprintf(M.msg, sizeof M.msg, TR("%s is not hosting this world right now."), hist_shown_host(h->host)); return;
    }
    /* IP: the same non-blocking connect as Join by address; "not hosting" if nobody answers there */
    mp_config cc; mp_config_defaults(&cc); cc.host = 0; cc.need_save = 1; uint16_t port = 0;
    if (ui_parse_address(h->addr, cc.host_addr, sizeof cc.host_addr, &port, MPM_DEFAULT_PORT)) { snprintf(M.msg, sizeof M.msg, TR("Bad address in this row: %s"), h->addr); return; }
    cc.port = port; snprintf(cc.name, sizeof cc.name, "%s", my_name); default_name(cc.name, sizeof cc.name);
    cc.spectator = M.watch;                                                 /* the row's Watch button */
    if (conn) dsnet_connect_free(conn);
    conn_cfg = cc; conn_t0 = now_ms_(); snprintf(hist_host, sizeof hist_host, "%s", hist_shown_host(h->host));
    conn = dsnet_connect_start(cc.host_addr, port, DSNET_CONNECT_TIMEOUT_MS);
    snprintf(M.connecting, sizeof M.connecting, "%s:%u", cc.host_addr, port);
    rig_log("mpmenu: recent world %s: reconnecting to %s", h->world, M.connecting);
}
/* ---------------------------------------------------------------- "Get the host's mods": the box, the loader's work, the rejoin */
static void hm_log_box(const char *text) {                                   /* one rig.log line (tests grep it): no tags, no newlines */
    char f[2048]; size_t o = 0; int tag = 0;
    for (const char *p = text; *p && o + 4 < sizeof f; p++) {
        if (*p == '<') { tag = 1; continue; }
        if (tag) { if (*p == '>') tag = 0; continue; }
        if (*p == '\n') { if (o) { f[o++] = ' '; f[o++] = '/'; f[o++] = ' '; } continue; }
        f[o++] = *p;
    }
    f[o] = 0;
    rig_log("mp: hostmods box: %s [button %s] [rejoin %s]", f, HM.can_fix ? "Get the host's mods" : "none", HM.rejoin[0] ? HM.rejoin : "none");
}
static void hm_info_box(const char *head, const char *body) {                /* progress / failure: one button, it only closes the box */
    char t[1200]; snprintf(t, sizeof t, "<size=130%%>%s</size>\n%s", head, body); dsi_utf8_fix_end(t);
    box_show(BX_HOSTMODS_INFO, t, TR("OK"), NULL);
}
static void hm_info_update(const char *head, const char *body) {             /* the progress box, its text changed in place */
    Obj v = lbox_view();
    if (lbox_up != BX_HOSTMODS_INFO || !lbox_ours(v)) return;
    snprintf(lbox_text, sizeof lbox_text, "<size=130%%>%s</size>\n%s", head, body); dsi_utf8_fix_end(lbox_text);
    tmp_set(AT(v, 0x30, Obj), lbox_text);
    lbox_grow(v);
}
static void hostmods_click(int confirm) {
    if (confirm && HM.can_fix) { HM.go = 1; rig_log("mp: hostmods: Get the host's mods"); }
    else rig_log("mp: hostmods box: closed");
}
static void hostmods_start(void) {
    char env[400] = "", why[300] = "";
    if (HM.rejoin[0]) snprintf(env, sizeof env, "DSMP_REJOIN=%s\n", HM.rejoin);
    int rc = hm_get(HM.host, HM.vanilla, env[0] ? env : NULL, why, sizeof why);
    rig_log("mp: hostmods get: rc %d%s%s (env %s)", rc, why[0] ? ": " : "", why, env[0] ? HM.rejoin : "none");
    if (rc) {
        snprintf(M.msg, sizeof M.msg, "%s", TR("Could not get the host's mods"));
        hm_info_box(TR("Could not get the host's mods"), why[0] ? why : TR("Your mods stay as they were."));
        return;
    }
    HM.busy = 1; HM.wait_box_shown = 0; HM.st_shown = -9; HM.pct_shown = -1;
}
static void hostmods_poll(void) {
    int pct = 0; char why[300] = "";
    int st = hm_state(&pct, why, sizeof why);
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    if (st == HM.st_shown && pct == HM.pct_shown) return;
    if (st != HM.st_shown) rig_log("mp: hostmods state %d (%d%%)%s%s", st, pct, why[0] ? ": " : "", why);
    HM.st_shown = st; HM.pct_shown = pct;
    if (st < 0) {
        HM.busy = 0;
        char b[600]; snprintf(b, sizeof b, "%s\n%s", why[0] ? why : TR("Something went wrong."), TR("Your mods stay as they were."));
        snprintf(M.msg, sizeof M.msg, "%s", TR("Could not get the host's mods"));
        hm_info_box(TR("Could not get the host's mods"), b);
        return;
    }
    char line[200];
    if (st == 2) snprintf(line, sizeof line, "%s", TR("Installing the host's mods..."));
    else if (st == 3) snprintf(line, sizeof line, "%s", TR("Loading the host's mods..."));
    else snprintf(line, sizeof line, TR("Downloading the host's mods... %d%%"), pct);
    snprintf(M.msg, sizeof M.msg, "%s", line);                              /* the Multiplayer line too (the box can be closed) */
    if (!HM.wait_box_shown && lbox_up == BX_NONE) { HM.wait_box_shown = 1; hm_info_box(TR("Get the host's mods"), line); }
    else hm_info_update(TR("Get the host's mods"), line);
}
static void hostmods_frame(void) {
    Obj v = lbox_view();
    if ((lbox_up == BX_HOSTMODS || lbox_up == BX_HOSTMODS_INFO) && !lbox_ours(v)) lbox_up = BX_NONE;   /* closed another way */
    if (HM.pending && HM.names_pending && frames % 10 == 0) {               /* a missing mod's name: ask the loader again */
        if (now_ms_() - HM.plan_ms > 4000) { HM.names_pending = 0; rig_log("mpmenu: hostmods: names not ready in 4 s: ids shown"); }
        else if (hostmods_replan() <= 0) HM.pending = 0;                    /* cannot happen with the same lines; no box then */
    }
    if (HM.pending && !HM.names_pending && title_calm >= 90 && !M.confirm_leave && lbox_up == BX_NONE) {
        box_show(BX_HOSTMODS, HM.text, HM.can_fix ? TR("Get the host's mods") : TR("Close"), HM.can_fix ? TR("Close") : NULL);
        if (lbox_up == BX_HOSTMODS) { HM.pending = 0; HM.shown_ms = now_ms_(); hm_log_box(HM.text); }
        else if (++HM.tries > 120) {                                        /* no notice box at all: the button line says it */
            HM.pending = 0; snprintf(M.msg, sizeof M.msg, "%s", TR("Could not join: your gameplay mods are not the same as the host's"));
            rig_log("mpmenu: hostmods: no notice box: message on the button line");
        }
    }
    /* test hook: the button 2 s after the box shows (never in a process that came from a rejoin: no loop) */
    if (hm_test && lbox_up == BX_HOSTMODS && HM.can_fix && !HM.auto_done && !hm_rejoined && now_ms_() - HM.shown_ms >= 2000 && lbox_ours(v)) {
        HM.auto_done = 1; rig_log("mp: hostmods test: button clicked");
        F(0xF65A18, void (*)(Obj, void *))(v, NULL);                        /* OnClick_Confirm (through the ui lane's hook) */
        if (lbox_up == BX_HOSTMODS) { lbox_up = BX_NONE; hostmods_click(1); }   /* the hook did not answer: the same click */
    }
    if (HM.go && (!(u_alive(v) && u_on(v)) || ++HM.go > 60)) { HM.go = 0; hostmods_start(); }   /* the stock box has closed (or 1 s) */
    if (HM.busy && frames % 6 == 0) hostmods_poll();
}

/* DSMP_REJOIN (mp_game_rejoin_take): once the title is up and calm, open the Multiplayer page and join that host once,
 * the same way as before (address + port, a Recent worlds row's world, or the Steam host); a refusal then is shown as
 * usual (the hostmods box again, no automatic click). */
static struct { int state; dsmc_rejoin r; char line[300]; double t0, next; } RJ;   /* state 0 not read, 1 waiting, 2 Steam: waiting for the host's lobby, 3 done */
static void rejoin_ip(void) {
    mp_config cc; mp_config_defaults(&cc); cc.host = 0; cc.need_save = 1;
    snprintf(cc.host_addr, sizeof cc.host_addr, "%s", RJ.r.addr); cc.port = (uint16_t)RJ.r.port;
    snprintf(cc.name, sizeof cc.name, "%s", my_name); default_name(cc.name, sizeof cc.name);
    cc.spectator = RJ.r.watch; M.watch = RJ.r.watch;
    if (conn) dsnet_connect_free(conn);
    conn_cfg = cc; conn_t0 = now_ms_();
    conn = dsnet_connect_start(cc.host_addr, cc.port, DSNET_CONNECT_TIMEOUT_MS);
    snprintf(M.connecting, sizeof M.connecting, "%s:%u", cc.host_addr, (unsigned)cc.port);
    rig_log("mp: rejoin: connecting to %s%s", M.connecting, cc.spectator ? " to watch" : "");
}
static int rejoin_steam(void) {                                             /* 1 = the join runs */
    static void (*next_spect)(int); static int looked;
    if (!looked) { looked = 1; next_spect = (void (*)(int))dlsym(RTLD_DEFAULT, "mp_game_next_spectator"); }
    friends_refresh();
    for (int i = 0; i < M.nfriends && i < 6; i++) if (friend_id[i] == RJ.r.steamid) {
        if (next_spect) next_spect(RJ.r.watch);
        expect_world(RJ.r.world);
        int rc = mp_steam_join(friend_lobby[i]);
        rig_log("mp: rejoin: Steam host %llu lobby %llu: rc %d", RJ.r.steamid, (unsigned long long)friend_lobby[i], rc);
        if (!rc) { snprintf(joined_how, sizeof joined_how, "steam"); joined_steamid = RJ.r.steamid; return 1; }
    }
    return 0;
}
static void rejoin_frame(const mpm_obs *o) {
    if (RJ.state == 3) return;
    if (RJ.state == 0) {
        static int (*take)(char *, size_t); static int (*seen)(void);
        take = (int (*)(char *, size_t))dlsym(RTLD_DEFAULT, "mp_game_rejoin_take"); seen = (int (*)(void))dlsym(RTLD_DEFAULT, "mp_game_rejoined");
        RJ.state = 3; if (seen && seen()) hm_rejoined = 1;
        if (take && take(RJ.line, sizeof RJ.line) && !dsmc_rejoin_parse(RJ.line, &RJ.r)) { RJ.state = 1; RJ.t0 = now_ms_(); hm_rejoined = 1; }
        return;
    }
    double now = now_ms_();
    if (now - RJ.t0 > 180000) { RJ.state = 3; rig_log("mp: rejoin: the title was never ready in 3 min: not joined"); return; }
    if (RJ.state == 2) {                                                    /* Steam: the host's lobby shows in the friends list */
        if (now < RJ.next) return;
        RJ.next = now + 1000;
        if (o->role || mp_game_session()) { RJ.state = 3; return; }
        if (rejoin_steam()) { RJ.state = 3; return; }
        if (now - RJ.t0 > 30000) {
            RJ.state = 3; expect_world("");
            snprintf(M.msg, sizeof M.msg, TR("%s is not hosting this world right now."), TR("The host"));
            rig_log("mp: rejoin: Steam host %llu not found hosting", RJ.r.steamid);
        }
        return;
    }
    int ready = o->scene == 1 && o->title_visible && !o->loading && title_calm >= 90 && !o->role && !conn && !mp_game_session()
             && lbox_up == BX_NONE && !M.notice[0] && !HM.pending && !HM.busy;
    if (!ready) return;
    if (M.state == MPM_IDLE || M.state == MPM_FAILED) { pending_click = MPB_MULTIPLAYER; return; }   /* the Multiplayer page first: status + Cancel */
    if (M.state != MPM_TITLE_MENU) return;
    rig_log("mp: rejoin: joining %s", RJ.line);
    if (!strcmp(RJ.r.how, "ip")) {
        RJ.state = 3;
        expect_world(RJ.r.world);
        hist_world[0] = 0; hist_host[0] = 0;
        if (RJ.r.world[0]) {                                                /* a Recent worlds row: its names for the refusal texts */
            if (!hist_loaded) hist_load();
            for (int i = 0; i < nhist; i++) {
                size_t gl = strlen(RJ.r.world);
                if (!strncmp(hist[i].key, RJ.r.world, gl) && hist[i].key[gl] == '|' && !strcmp(hist[i].how, "ip")) {
                    snprintf(hist_world, sizeof hist_world, "%s", hist[i].world); snprintf(hist_host, sizeof hist_host, "%s", hist_shown_host(hist[i].host)); break;
                }
            }
        }
        rejoin_ip();
        return;
    }
    if (!M.steam) {                                                         /* Steam not up yet in this process: wait for it */
        if (now - RJ.t0 > 30000) { RJ.state = 3; snprintf(M.msg, sizeof M.msg, "%s", TR("This world was joined through Steam: needs the Steam version.")); rig_log("mp: rejoin: no Steam"); }
        return;
    }
    RJ.state = 2; RJ.next = 0;
}
static void hist_del(int k) {
    if (k < 0 || k >= nhist) return;
    rig_log("mpmenu: recent world %s removed", hist[k].world);
    memmove(&hist[k], &hist[k + 1], sizeof hist[0] * (size_t)(nhist - k - 1)); nhist--; M.nhist = nhist;
    if (M.hist_page * 6 >= nhist && M.hist_page > 0) M.hist_page--;
    hist_save();
}

/* ---------------------------------------------------------------- chat (owner, lan_v8; mockup "Chat: idle, typing, history") */
/* Lines come from dsnet (outside the sim). Idle: the last lines bottom-left above the build bar, no frame, fading after
 * the fade time. Enter: a framed box with the history (wheel / PageUp-PageDown scroll) and "Say:" + the typed text;
 * Enter sends, Escape closes. While typing, the game's own key handling sees a focused text field
 * (KeyInputDispatcher.IsTextInputFocused hooked to true), so no hotkey fires. Right-click + drag moves it; the open box
 * also moves with a left drag on its title band or frame, and resizes with a left drag on its bottom-right corner (grip:
 * width + height) or its bottom edge (height) (owner 2026-10-25); place and size are remembered in mp_menu.json.
 * Read state (owner 2026-10-25: "can't pause to read the chat"): the same box with no text field focused, so the game
 * keeps its keys (Space pauses). It opens with the wheel over the chat lines, a left click on a chat line (not a local
 * one: those let clicks through), or Enter where the player cannot send (single player: no entry field at all); it stays
 * until Escape or its X. There the wheel scrolls, the box moves and resizes, and the scrollbar, the Top / Bottom buttons
 * and the bin work; Enter (in a session) or a click in the field types, a click on the history goes back to reading.
 * "/clear" and the bin empty this screen's history only (never sent); mp_api v4 chat_clears counts them. */
#define CH_HIST 3000                                                         /* mp_api v3: a DPS check writes a line per hit */
#define CH_DROP 300                                                          /* history full: the oldest 300 go in one move */
#define CH_IDLE 5
#define CH_BOX_LINES 60                                                      /* line objects of the open box: enough for its tallest size (972 high) */
#define CH_TEXT 640
static struct { char text[CH_TEXT]; double t; int link, id, font, missing, local; unsigned gen; float hi, hb; } chist[CH_HIST]; static int nch, ch_serial;
/* link: ui_game's id (pings), 0 = plain. id: serial (slots keep what they show). font: index in the chat fonts (-1 not
   chosen yet, gen = the fonts' generation); missing: characters no game font has, shown as '?'. hi / hb: measured text
   height in the idle lines (box width - 60: 410) / the open box (box width - 24: 446), 0 = not measured (ch_hi_w / ch_hb_w). local: 1 = a mp_api v3 chat_local line
   (never sent; MP_CHATL_CLEAR removes it; in single player the chat shows while one is in the history) */
static int ch_nlocal, ch_top_stale; static unsigned ch_local_total;   /* ch_top_stale: lines left the middle of the history (ch_top_count) */                       /* local lines in chist now / ever added (test read-back) */
static char chat_buf[DSNET_CHAT_MAX + 1];
static uintptr_t h_ch_view, h_ch_root, h_ch_box, h_ch_idle[CH_IDLE], h_ch_idlebg[CH_IDLE], h_ch_lines[CH_BOX_LINES], h_ch_input, h_ch_hint, h_ch_field, h_ch_typing, h_ch_typingbg;
/* the open box's parts placed by ch_layout for its size: history / footer / title bands, lines, "Chat", "Say:", the field's
   plates, the corner grip, and two clear strips (UI clicks) under the bottom edge and right of the corner: a wider grab */
enum { CL_BAND, CL_FOOT, CL_TBAND, CL_TLINE, CL_TITLE, CL_ILINE, CL_SAY, CL_FLINE, CL_FBG, CL_GRIP, CL_STRIPB, CL_STRIPR,
       CL_TRACK, CL_THUMB, CL_TOP, CL_BOT, CL_BIN, CL_CLOSE, CL_N };   /* scrollbar track + thumb, Top / Bottom, the bin, the X */
static uintptr_t h_ch_lay[CL_N]; static float ch_say_w = 40, ch_lay_w, ch_lay_h; static int ch_lay_ro = -1;
/* the icons' pixels (children of CL_TOP / CL_BOT / CL_BIN / CL_CLOSE when the X is drawn from pixels): their colour shows the hover */
#define CH_ICON_PX 24
static uintptr_t h_ch_icon[4][CH_ICON_PX]; static int n_ch_icon[4], ch_icon_hot = -1;
static char typing_shown[200];                           /* test status: the "is typing" line now shown */
void utf16_to_utf8(Obj s, char *out, size_t cap) {             /* typed text: keep accents etc. (str_utf8 is ASCII-only) */
    size_t n = 0; if (cap) out[0] = 0;
    if (!s || !cap) return;
    int32_t len = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        char b[4]; int k = u < 0x80 ? (b[0] = (char)u, 1) : u < 0x800 ? (b[0] = (char)(0xC0 | u >> 6), b[1] = (char)(0x80 | (u & 63)), 2)
              : u < 0x10000 ? (b[0] = (char)(0xE0 | u >> 12), b[1] = (char)(0x80 | (u >> 6 & 63)), b[2] = (char)(0x80 | (u & 63)), 3)
              : (b[0] = (char)(0xF0 | u >> 18), b[1] = (char)(0x80 | (u >> 12 & 63)), b[2] = (char)(0x80 | (u >> 6 & 63)), b[3] = (char)(0x80 | (u & 63)), 4);
        if (n + (size_t)k + 1 > cap) break;
        memcpy(out + n, b, (size_t)k); n += (size_t)k;
    }
    out[n] = 0;
}
static char test_chat_in[1024]; static int test_chat_enter, test_chat_escape, test_chat_open;
static bool (*o_textfocus)(void *);
static void chat_box_close(const char *why);
/* hotkeys off while typing, and while the lobby Rules panel is open (its Escape must not open the game's Esc menu) */
static int teams_modal(void) {                                             /* teams lane: Teams window / proposal summary / stock step open */
    static int (*f)(void); static int looked; if (!looked) { looked = 1; f = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_teams_modal_open"); }
    return f && f();
}
static bool h_textfocus(void *mi) { return chat_typing || chat_close_frames > 0 || (M.state == MPM_LOBBY && M.lobby_settings_open) || rules_close_frames > 0 || teams_modal() || teams_close_frames > 0 || o_textfocus(mi); }
/* Escape for our in-game boxes (ui lane's InputEventHandler.OnEvent(EscapeActionRequested) hook asks first, dlsym):
 * 1 = one of ours took it (chat typing: closes without sending; lobby Rules: closed), so the game's Esc menu stays shut */
__attribute__((visibility("default"))) int mpmenu_escape_ours(void) {
    rig_log("mpmenu: Escape asked (chat %d, lobby rules %d, rules closing %d, teams %d, teams closing %d)", chat_typing,
            M.state == MPM_LOBBY && M.lobby_settings_open, rules_close_frames, teams_modal(), teams_close_frames);
    if (chat_typing) { test_chat_escape = 1; rig_log("mpmenu: Escape closed the chat"); return 1; }
    if (M.confirm_leave) { pending_click = MPB_LEAVE_CANCEL; rig_log("mpmenu: Escape = Cancel in the Leave box"); return 1; }
    /* Custom Modes windows (FE), the Steam friends picker: the last registered first. The picker registers at the lobby, after
       FE, and opens over an FE window (results opened by themselves at the session start), so Escape closes the top one. */
    for (int k = nhooks - 1; k >= 0; k--) { int ec = hooks[k].escape ? hooks[k].escape() : 0; if (ec) { pending_click = ec; return 1; } }
    if (lbox_up > BX_LEAVE && lbox_ours(lbox_view())) {                    /* the box's own Cancel: the click hook answers it */
        rig_log("mpmenu: Escape = Cancel in our box"); F(0xF65638, void (*)(Obj, void *))(lbox_view(), NULL); return 1; }
    if (M.state == MPM_LOBBY && M.lobby_settings_open) { pending_click = MPB_RULES_DONE; rig_log("mpmenu: Escape closed the lobby Rules"); return 1; }
    if (rules_close_frames > 0) return 1;                                    /* the same Escape, seen by us first this frame */
    if (teams_modal() || teams_close_frames > 0) return 1;                  /* Teams closes itself on Escape (teams lane) */
    { static int (*tesc)(void); static int looked; if (!looked) { looked = 1; tesc = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_teams_escape_ours"); }
      if (tesc && tesc()) return 1; }                                       /* folds the expedition proposal panel (ux_flow F15) */
    if (chat_read) { chat_box_close("Escape"); return 1; }                   /* the chat in its read state (no kit Escape stack: older loader) */
    /* lobby (ux_flow F14): Escape would close the stock Esc view that carries the lobby, which comes back 0.5 s later
       (a blink). The lobby closes with Start or Leave, so Escape does nothing there. */
    if (M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY) { rig_log("mpmenu: Escape in the lobby: kept"); return 1; }
    return 0;
}
/* panel cog (ui lane, dlsym): fade seconds, join / leave lines, chat on / off; saved in mp_menu.json */
__attribute__((visibility("default"))) void mpmenu_chat_config(int fade_s, int show_join, int enabled) {
    if (fade_s >= 3 && fade_s <= 120) chat_fade_s = fade_s; chat_show_join = show_join != 0; chat_on = enabled != 0;
    if (!chat_on) chat_typing = chat_read = 0;
    prefs_save();
}
__attribute__((visibility("default"))) void mpmenu_chat_config_get(int *fade_s, int *show_join, int *enabled) {
    if (fade_s) *fade_s = chat_fade_s; if (show_join) *show_join = chat_show_join; if (enabled) *enabled = chat_on;
}
static double mono_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static int chat_shown_k[CH_IDLE + CH_BOX_LINES]; static uintptr_t *chat_shown_h[CH_IDLE + CH_BOX_LINES]; static int nchat_shown;   /* lines on screen this frame */
static void chat_add_as(const char *rich, int local) {
    if (nch == CH_HIST) {                                                   /* one move per CH_DROP lines, not one per line */
        for (int i = 0; i < CH_DROP; i++) ch_nlocal -= chist[i].local;
        memmove(&chist[0], &chist[CH_DROP], sizeof chist[0] * (CH_HIST - CH_DROP)); nch -= CH_DROP;
        nchat_shown = 0;                                                    /* the indices shown this frame moved */
    }
    snprintf(chist[nch].text, sizeof chist[nch].text, "%s", rich); dsi_utf8_fix_end(chist[nch].text);
    chist[nch].t = mono_s(); chist[nch].link = 0; chist[nch].id = ++ch_serial; chist[nch].font = -1; chist[nch].gen = 0; chist[nch].hi = chist[nch].hb = 0; chist[nch].missing = 0;
    chist[nch].local = local != 0; ch_nlocal += local != 0; nch++;
    /* a local line while the open box is scrolled up keeps the same lines in view (a DPS check adds many a second); so does
       any line while the player reads (read state, scrolled up); other lines jump to the newest, as before. A drop above
       moves the shown line and nch by the same count. */
    if ((local || (chat_read && !chat_typing)) && (chat_typing || chat_read) && chat_scroll > 0) { chat_scroll++; if (chat_scroll > nch - 1) chat_scroll = nch - 1; }
    else chat_scroll = 0;
}
/* "/clear" or the bin (owner 2026-10-25): this screen's whole history goes (every kind of line); nothing is sent, the other
   players' chat is untouched. chat_clears (mp_api v4) counts it, so a mod that re-sends its own lines (the DPS log after a
   filter change) can leave out what the player cleared. */
static unsigned ch_clears;
static void chat_clear_view(const char *how) {
    int n = nch;
    nch = 0; ch_nlocal = 0; nchat_shown = 0; chat_scroll = 0; ch_clears++; ch_top_stale = 1;
    rig_log("mpmenu: chat cleared by the player (%s): %d line(s) removed on this screen only, clears %u", how, n, ch_clears);
}
unsigned mpmenu_chat_clears(void) { return ch_clears; }               /* mp_api v4 chat_clears */
static int chat_is_clear_cmd(const char *t) {                           /* "/clear" (any case, spaces around) */
    while (*t == ' ') t++;
    for (const char *w = "/clear"; *w; w++, t++) if ((*t >= 'A' && *t <= 'Z' ? *t + 32 : *t) != *w) return 0;
    while (*t == ' ') t++;
    return *t == 0;
}
static void chat_add(const char *rich) { chat_add_as(rich, 0); }
/* MP_CHATL_CLEAR (and the title with no session): the local lines go, the others keep their order. The open box keeps
   the newest kept line at or above the one it showed at its bottom. */
static void chat_clear_local(void) {
    if (!ch_nlocal) return;
    int k0 = nch - 1 - chat_scroll, j = 0, newer = 0;
    for (int i = 0; i < nch; i++) {
        if (chist[i].local) continue;
        if (i > k0) newer++;
        if (j != i) chist[j] = chist[i];
        j++;
    }
    rig_log("mpmenu: chat: %d local line(s) removed, %d kept", nch - j, j);
    nch = j; ch_nlocal = 0; nchat_shown = 0; ch_top_stale = 1;
    chat_scroll = newer < nch ? newer : nch > 0 ? nch - 1 : 0;
}
/* ui lane (pings, lan_v10, dlsym): a system line in the chat, faded like any line. link != 0: a left click on the shown
   line (idle lines or the open box) calls ui_chat_link_click(link) (ui_game.c); it returns 0 when the ping is gone, and
   the line becomes plain text. Hover: the game-format tooltip "Ping". */
__attribute__((visibility("default"))) void mpmenu_chat_system_line(const char *rich, int link) {
    if (!rich) return;
    chat_add(rich); chist[nch - 1].link = link;
}
static int chat_link_clicks;                                                /* test read-back */
static int pt_in(Obj o, V3 m) { float r[4]; return u_on(o) && rect_of(o, r) && m.x >= r[0] && m.x <= r[2] && m.y >= r[1] && m.y <= r[3]; }
/* ui lane (dlsym, pings lan_v10): 1 = the mouse is over a shown chat line, the "is typing" line or the open chat box */
__attribute__((visibility("default"))) int mpmenu_chat_over(void) {
    V3 m = F(0x51AD4DC, V3 (*)(void *))(NULL);                              /* Input.mousePosition */
    if (pt_in(u_gc_get(h_ch_box), m) || pt_in(u_gc_get(h_ch_typingbg), m)) return 1;
    if (pt_in(u_gc_get(h_ch_lay[CL_STRIPB]), m) || pt_in(u_gc_get(h_ch_lay[CL_STRIPR]), m)) return 1;   /* the open box's resize strips */
    for (int i = 0; i < CH_IDLE; i++) if (pt_in(u_gc_get(h_ch_idlebg[i]), m)) return 1;
    return 0;
}
static void chat_esc(const char *in, char *out, size_t cap) {          /* no rich-text tags from players */
    size_t o = 0;
    for (; *in && o + 8 < cap; in++) { if (*in == '<') { memcpy(out + o, "<​", 4); o += 4; } else out[o++] = *in; }
    out[o] = 0;
}
/* mod lines (mp_api v2 chat_line, MP_API.md): "<Mod> mod:" in the system colour, then the escaped text */
static void mod_line(char *t, size_t cap, const char *who_rich, const char *mod, const char *text) {
    char m[MP_CHAT_MOD_MAX * 4 + 8], x[MP_CHAT_TEXT_LOCAL * 4 + 8], pre[200];
    chat_esc(mod, m, sizeof m); chat_esc(text, x, sizeof x);
    snprintf(pre, sizeof pre, TR("%s mod:"), m);
    snprintf(t, cap, "%s%s<i><color=#A69085>%s</color></i> %s", who_rich ? who_rich : "", who_rich ? " " : "", pre, x);
    dsi_utf8_fix_end(t);                                   /* a long escaped text is cut at the line size */
}
static mpc_bucket modch_rx[32]; static double modch_rx_logged;   /* per sender: 5 at once, then 1 a second */
static void chat_pull(dsnet_client *c) {
    dsnet_chat_msg m;
    while (dsnet_chat_poll(c, &m)) {
        char t[CH_TEXT], body[CH_TEXT - 140]; chat_esc(m.text, body, sizeof body);
        const char *col = ui_player_color(m.player);
        char mod[MP_CHAT_MOD_MAX + 1]; const char *mtext;
        if (m.kind == DSNET_CHAT_TEXT && !m.watching && mpc_decode(m.text, mod, sizeof mod, &mtext)) {   /* a mod's broadcast line */
            if (m.player < 0 || m.player >= 32 || !mpc_take(&modch_rx[m.player], mono_s(), 5, 1.0)) {
                if (mono_s() - modch_rx_logged > 5) { modch_rx_logged = mono_s(); rig_log("mpmenu: mod chat lines from player %d dropped (more than 1 a second)", m.player); }
                continue;
            }
            char who[96]; snprintf(who, sizeof who, "<color=%s>%s</color>", col, m.name[0] ? m.name : TR("Player"));
            mod_line(t, sizeof t, who, mod, mtext);
        }
        else if (m.kind == DSNET_CHAT_TEXT && m.watching)              /* a spectator's line: grey, "(watching)" */
        { char wn[96]; snprintf(wn, sizeof wn, TR("%s (watching):"), m.name[0] ? m.name : TR("Spectator"));
            snprintf(t, sizeof t, "<color=%s>%s</color> %s", UI_SPECTATOR_COLOR, wn, body); }
        else if (m.kind == DSNET_CHAT_TEXT) snprintf(t, sizeof t, "<color=%s>%s:</color> %s", col, m.name[0] ? m.name : TR("Player"), body);
        else if (m.kind == DSNET_CHAT_WATCHING || m.kind == DSNET_CHAT_STOPPED_WATCHING) {
            if (!chat_show_join) continue;
            char ln[200]; snprintf(ln, sizeof ln, m.kind == DSNET_CHAT_WATCHING ? TR("%s is watching") : TR("%s stopped watching"), m.name[0] ? m.name : TR("A spectator"));
            snprintf(t, sizeof t, "<i><color=#A69085>%s</color></i>", ln);
        } else {
            if (!chat_show_join || (m.kind == DSNET_CHAT_JOINED && m.player == dsnet_player_id(c))) continue;   /* not "you joined" */
            char ln[200]; snprintf(ln, sizeof ln, m.kind == DSNET_CHAT_LEFT ? TR("%s left the game") : m.kind == DSNET_CHAT_RECONNECTED ? TR("%s reconnected") : TR("%s joined the game"),
                                   m.name[0] ? m.name : TR("A player"));
            snprintf(t, sizeof t, "<i><color=#A69085>%s</color></i>", ln);
        }
        chat_add(t);
    }
}
/* mp_api v2 chat_line / dsmp_bridge_chat: limits per mod name (16 names, more share one): local 5 at once then 1 a
   second; broadcast 3 at once then 1 every 2 s (receivers also cut each sender to 1 a second) */
int mpmenu_mod_chat(const char *mod_name, const char *text, int flags) {
    char m[MP_CHAT_MOD_MAX + 1], x[MP_CHAT_TEXT_LOCAL + 1];
    int bc = flags == MP_CHAT_BROADCAST;
    if ((flags != MP_CHAT_LOCAL && !bc) || !mpc_clean(mod_name, m, sizeof m) || !mpc_clean(text, x, bc ? MP_CHAT_TEXT_BROADCAST + 1 : sizeof x)) return MP_CHAT_BAD_ARGS;
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL;
    if (!c) return MP_CHAT_NO_SESSION;
    if (bc && dsnet_is_spectator(c)) return MP_CHAT_SPECTATOR;
    static struct { char name[MP_CHAT_MOD_MAX + 1]; mpc_bucket lb, bb; } lim[17]; static int nlim;
    int k = 0; while (k < nlim && strcmp(lim[k].name, m)) k++;
    if (k == nlim) { if (nlim < 16) snprintf(lim[nlim++].name, sizeof lim[0].name, "%s", m); else k = 16; }
    if (bc ? !mpc_take(&lim[k].bb, mono_s(), 3, 2.0) : !mpc_take(&lim[k].lb, mono_s(), 5, 1.0)) return MP_CHAT_RATE_LIMITED;
    if (!bc) { char t[300]; mod_line(t, sizeof t, NULL, m, x); chat_add(t); return MP_CHAT_SHOWN; }
    char w[MP_CHAT_MOD_MAX + MP_CHAT_TEXT_BROADCAST + 4];
    if (mpc_encode(m, x, w, sizeof w) < 0) return MP_CHAT_BAD_ARGS;
    return dsnet_chat_send(c, w) ? MP_CHAT_NOT_SENT : MP_CHAT_SHOWN;      /* shown here when the server's copy comes back */
}
/* mp_api v3 chat_local (MP_API.md 2b; Units Laboratory's DPS check: a line per hit): this screen only, never sent, no rate
   limit, with or without a session. The text alone in the normal colour, escaped like the other lines. */
int mpmenu_chat_local(const char *mod_name, const char *text, int flags) {
    char m[MP_CHAT_MOD_MAX + 1], x[MP_CHAT_TEXT_CHATL + 1], t[MP_CHAT_TEXT_CHATL * 4 + 8];   /* the history slot is CH_TEXT (640) */
    int rich = (flags & MP_CHATL_RICH) != 0; flags &= ~MP_CHATL_RICH;      /* rich: a mod's own tags (this screen only, never sent) */
    if ((flags != MP_CHATL_LINE && flags != MP_CHATL_CLEAR) || !mpc_clean(mod_name, m, sizeof m)) return MP_CHAT_BAD_ARGS;
    if (flags == MP_CHATL_CLEAR) { chat_clear_local(); return MP_CHAT_SHOWN; }
    if (!mpc_clean(text, x, sizeof x)) return MP_CHAT_BAD_ARGS;
    if (!chat_on) return MP_CHAT_OFF;                                       /* "Chat: Off": nothing stored */
    if (rich) snprintf(t, sizeof t, "%s", x); else chat_esc(x, t, sizeof t);
    chat_add_as(t, 1); ch_local_total++;
    return MP_CHAT_SHOWN;
}
/* test read-back: "<local lines ever added>\n<local lines in the history now>\n", then each local line's stored text,
   oldest first, one per line; whole lines only (stops at the first that does not fit). Returns the bytes written. */
__attribute__((visibility("default"))) size_t mpmenu_chat_local_dump(char *out, size_t cap) {
    if (!out || !cap) return 0;
    int w = snprintf(out, cap, "%u\n%d\n", ch_local_total, ch_nlocal);
    if (w < 0 || (size_t)w >= cap) { out[0] = 0; return 0; }
    size_t n = (size_t)w;
    for (int i = 0; i < nch; i++) {
        if (!chist[i].local) continue;
        size_t l = strlen(chist[i].text);
        if (n + l + 2 > cap) break;
        memcpy(out + n, chist[i].text, l); n += l; out[n++] = '\n';
    }
    out[n] = 0;
    return n;
}
/* chatfix (owner 2026-10-20, v0.1.21 A6 + A7): the chat lives on its OWN screen canvas, kept across scenes
 * (DontDestroyOnLoad), so it works everywhere a session runs (lobby, connecting, guest waiting, preparation, PvP prep,
 * the world) and draws ABOVE the game's windows (Teams 32000, our panel's top layer 32020) but under the tooltip /
 * notice boxes (32040+, ui_tip.c and the loader kit). Scaler as the game's UI: 1920 x 1080 reference, by height, so
 * the canvas is 1080 high (the places below are the same canvas units as before). */
#define CH_SORT 32030
static uintptr_t h_ch_cv;
static int ch_cv_order = -1;
static Obj chat_parent(void) {
    Obj cv = u_gc_get(h_ch_cv);
    if (u_alive(cv)) return u_tr(cv);
    static int64_t tried = -1000; if (frames - tried < 60) return NULL;   /* a failed build: again in 2 s */
    tried = frames;
    void *kg = class_by_name("UnityEngine", "GameObject"); Obj tcv = type_obj("UnityEngine", "Canvas"), tsc = type_obj("UnityEngine.UI", "CanvasScaler"),
         tgr = type_obj("UnityEngine.UI", "GraphicRaycaster");
    static void *mmode, *morder, *msm, *mref, *mmatch, *mddol;
    if (!mmode) { mmode = mth("UnityEngine", "Canvas", "set_renderMode", 1); morder = mth("UnityEngine", "Canvas", "set_sortingOrder", 1);
                  msm = mth("UnityEngine.UI", "CanvasScaler", "set_uiScaleMode", 1); mref = mth("UnityEngine.UI", "CanvasScaler", "set_referenceResolution", 1);
                  mmatch = mth("UnityEngine.UI", "CanvasScaler", "set_matchWidthOrHeight", 1); mddol = mth("UnityEngine", "Object", "DontDestroyOnLoad", 1); }
    if (!kg || !tcv || !tsc || !tgr || !mmode || !morder) { rig_log("mpmenu: chat canvas: Unity types missing"); return NULL; }
    Obj go = F(0x6F96CC, Obj (*)(void *))(kg);                                  /* il2cpp_object_new */
    if (!go) return NULL;
    F(0x50F4674, void (*)(Obj, Obj, void *))(go, u_str("DSMP_Chat"), NULL);     /* GameObject..ctor(string) */
    if (!u_alive(go)) return NULL;
    F(0x50F3204, void (*)(Obj, int, void *))(go, 5, NULL);                      /* layer UI */
    cv = F(0x50F2B90, Obj (*)(Obj, Obj, void *))(go, tcv, NULL);                /* AddComponent(Canvas): RectTransform too */
    if (!u_alive(cv)) { rig_log("mpmenu: chat canvas: AddComponent(Canvas) failed"); return NULL; }
    MCALL(mmode, void, Obj, int, const void *)(cv, 0, mmode);                  /* ScreenSpaceOverlay */
    MCALL(morder, void, Obj, int, const void *)(cv, CH_SORT, morder); ch_cv_order = CH_SORT;
    Obj sc = F(0x50F2B90, Obj (*)(Obj, Obj, void *))(go, tsc, NULL);
    if (u_alive(sc) && msm && mref && mmatch) { MCALL(msm, void, Obj, int, const void *)(sc, 1, msm);   /* ScaleWithScreenSize */
        MCALL(mref, void, Obj, V2, const void *)(sc, (V2){ 1920, 1080 }, mref); MCALL(mmatch, void, Obj, float, const void *)(sc, 1.0f, mmatch); }
    F(0x50F2B90, Obj (*)(Obj, Obj, void *))(go, tgr, NULL);                     /* clicks on the plates are UI clicks */
    if (mddol) MCALL(mddol, void, Obj, const void *)(go, mddol);
    u_gc_free(h_ch_cv); h_ch_cv = u_gc_new(cv);
    rig_log("mpmenu: chat canvas %p (sorting order %d, kept across scenes)", cv, CH_SORT);
    return u_tr(cv);
}
/* chat fonts (A5): the language's body font first, then the game's own body fonts of the other scripts
 * (LanguageSetting.GetFont(EasyRead, Japanese / Chinese / Korean / Russian)). A line takes the first font that has all its
 * characters (TMP_FontAsset.HasCharacter with its fallbacks); a character no game font has (emoji) is shown as '?'
 * (TMP would draw an empty box). */
#define CH_FONTS 6
static uintptr_t h_chf[CH_FONTS]; static int nchf; static unsigned chf_gen;   /* 0 = not built */
static char chf_names[CH_FONTS][48];
static void chat_fonts(void) {
    if (chf_gen && chf_gen == dsi_generation() + 1 && nchf) return;
    body_material(); Obj b = u_gc_get(h_body_font);
    if (!u_alive(b)) return;
    for (int i = 0; i < nchf; i++) u_gc_free(h_chf[i]);
    nchf = 0; h_chf[nchf] = u_gc_new(b); str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(b, NULL), chf_names[nchf], sizeof chf_names[0]); nchf++;
    static void *mi2; if (!mi2 && ls_ready()) mi2 = F(0x6F8C1C, void *(*)(void *, const char *, int))(ls_klass, "GetFont", 2);
    static const int langs[] = { 7, 5, 6, 1, 4 };                              /* LanguageType Japanese, ChineseSimplified, ChineseTraditional, Korean, Russian */
    for (int l = 0; l < 5 && mi2 && ls_ready() && *(void **)*(void **)((char *)ls_klass + 0xB8); l++) {
        Obj f = MCALL(mi2, Obj, int32_t, int32_t, const void *)(0, langs[l], mi2);
        if (!u_alive(f) || nchf >= CH_FONTS) continue;
        int dup = 0; for (int i = 0; i < nchf; i++) dup |= u_gc_get(h_chf[i]) == f;
        if (dup) continue;
        h_chf[nchf] = u_gc_new(f); str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(f, NULL), chf_names[nchf], sizeof chf_names[0]); nchf++;
    }
    chf_gen = dsi_generation() + 1;
    rig_log("mpmenu: chat fonts: %d (%s, %s, %s, %s, %s, %s)", nchf, chf_names[0], nchf > 1 ? chf_names[1] : "-", nchf > 2 ? chf_names[2] : "-",
            nchf > 3 ? chf_names[3] : "-", nchf > 4 ? chf_names[4] : "-", nchf > 5 ? chf_names[5] : "-");
}
/* TMP_FontAsset.HasCharacter(char, fallbacks, add to a dynamic atlas). Never HasCharacter_Internal: it records the fonts
   it searched in a static set that only the public calls reset (a null set threw at the title; a stale one skipped the
   fallbacks: Japanese came out as '?'). A character above U+FFFF (emoji): the font's own table only. */
static bool ch_has(Obj font, uint32_t cp) {
    if (!u_alive(font)) return false;
    if (cp > 0xFFFF) return F(0x4EEE500, bool (*)(Obj, int32_t, void *))(font, (int32_t)cp, NULL);
    return F(0x4EEE580, bool (*)(Obj, uint16_t, bool, bool, void *))(font, (uint16_t)cp, true, true, NULL);
}
static uint32_t ch_utf8(const unsigned char *s, int *len) {                   /* one UTF-8 character (a bad byte = itself, 1 byte) */
    uint32_t c = s[0];
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    for (int i = 1; i < n; i++) if ((s[i] & 0xC0) != 0x80) { n = 1; break; }
    if (n == 2) c = (c & 0x1F) << 6 | (s[1] & 63);
    else if (n == 3) c = (c & 0x0F) << 12 | (s[1] & 63) << 6 | (s[2] & 63);
    else if (n == 4) c = (c & 0x07) << 18 | (s[1] & 63) << 12 | (s[2] & 63) << 6 | (s[3] & 63);
    *len = n; return c;
}
/* characters of rich text t that font lacks (tags skipped; "<" + U+200B is a player's literal '<'). fix: write them as
   '?' (joiners and variation selectors of emoji are dropped). Returns the count. */
static int ch_missing(char *t, Obj font, int fix) {
    char out[CH_TEXT]; size_t o = 0; int miss = 0;
    const unsigned char *s = (const unsigned char *)t;
    while (*s && o + 5 < sizeof out) {
        if (*s == '<' && !(s[1] == 0xE2 && s[2] == 0x80 && s[3] == 0x8B)) {     /* a tag: copied, never checked */
            const unsigned char *e = (const unsigned char *)strchr((const char *)s, '>');
            if (e) { size_t n = (size_t)(e - s + 1); if (o + n + 1 >= sizeof out) break; memcpy(out + o, s, n); o += n; s = e + 1; continue; }
        }
        int n; uint32_t cp = ch_utf8(s, &n);
        int skip = cp < 0x20 || cp == 0x200B;
        int joiner = cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F);
        if (!skip && !ch_has(font, cp)) { miss += !joiner; if (fix) { if (!joiner) out[o++] = '?'; s += n; continue; } }
        memcpy(out + o, s, (size_t)n); o += (size_t)n; s += n;
    }
    out[o] = 0;
    if (fix) snprintf(t, CH_TEXT, "%s", out);
    return miss;
}
static int ch_pick(char *t, int fix, int *missing) {                         /* the font for this text (index), '?' for the rest */
    chat_fonts(); if (!nchf) { if (missing) *missing = 0; return -1; }
    int best = 0, bm = 1 << 30;
    for (int i = 0; i < nchf; i++) { int m = ch_missing(t, u_gc_get(h_chf[i]), 0); if (m < bm) { bm = m; best = i; } if (!m) break; }
    if (bm > 0 && fix) ch_missing(t, u_gc_get(h_chf[best]), 1);
    if (missing) *missing = bm;
    return best;
}
static void ch_prepare(int k) {
    chat_fonts();
    if (!nchf || (chist[k].font >= 0 && chist[k].gen == chf_gen)) return;
    chist[k].font = ch_pick(chist[k].text, 1, &chist[k].missing); chist[k].gen = chf_gen; chist[k].hi = chist[k].hb = 0;
    if (chist[k].missing) rig_log("mpmenu: chat line %d: %d character(s) no game font has, shown as '?'", chist[k].id, chist[k].missing);
}
static void ch_font_set(Obj t, int fi, int field) {                          /* font + the game's look (a fallback face: plain) */
    if (!mm_is(t, K_TMP) || fi < 0 || fi >= nchf) return;
    if (fi == 0) { txt_body_as(t, field); return; }
    Obj f = u_gc_get(h_chf[fi]); if (!u_alive(f)) return;
    F(0x4F0A5A4, void (*)(Obj, Obj, void *))(t, f, NULL);
    txt_mat(t, field ? font_material(f) : mpm_look_material(f, 0, 0));
}
/* a chat text: wraps at its width, keeps its size (no text fit: the line grows instead, A1 / A2), no margins (A3) */
typedef struct { float x, y, z, w; } ch_v4;
static void ch_text_style(Obj t, float size) {
    if (!mm_is(t, K_TMP)) return;
    fit_e *e = fit_find(t, 0); if (e) fit_drop(e);
    fit_set(t, 0, size, 1);
    static void *mmg; if (!mmg) mmg = mth("TMPro", "TMP_Text", "set_margin", 1);
    if (mmg) MCALL(mmg, void, Obj, ch_v4, const void *)(t, (ch_v4){ 0, 0, 0, 0 }, mmg);
}
static float ch_measure(Obj t, float w) {                                     /* height of t's text wrapped at w */
    if (!fit_methods()) return 20;
    fit_v2 v = fit_pref(t, w);
    return v.y > 1 && v.y < 2000 ? v.y : 20;
}
/* a slot (idle line / box line) shows line k: text and font only when they changed */
static void ch_slot_show(Obj t, int *slot_id, int *slot_font, int k) {
    if (*slot_font != chist[k].font) { ch_font_set(t, chist[k].font, 0); *slot_font = chist[k].font; *slot_id = 0; }
    if (*slot_id != chist[k].id) { tmp_set(t, chist[k].text); *slot_id = chist[k].id; }
}
static int ch_idle_id[CH_IDLE], ch_idle_font[CH_IDLE], ch_box_id[CH_BOX_LINES], ch_box_font[CH_BOX_LINES], ch_typing_font = -1, ch_field_font = -1;
static int ch_box_oldest;                                                     /* the open box shows the oldest line: no more scrolling up */
static int ch_over_now, ch_wheel_blocked;                                     /* the pointer is over the chat (this frame); wheel reads blocked */
static float ch_line_w[2] = { CH_W0 - 60, CH_W0 - 24 };                      /* text width now: idle lines, open-box lines (test read-back) */
enum { CHD_NONE, CHD_MOVE, CHD_SIZE, CHD_HEIGHT, CHD_THUMB };
static int ch_drag, ch_drag_btn, ch_held[2]; static V3 ch_d0; static float ch_dsc;   /* a move / resize / thumb drag in progress: kind, button, start */
static unsigned ch_drags[5];                                                  /* test read-back: drags ended, per kind */
/* test only: a scripted pointer for the drag code above (set by the rig control channel's mpm_chat {drag}, which opens only in
   DSRIG_CTL=1 test copies; a player's game never sets it): 2 frames resting at (x0, y0), the press, n frames moving to (x1, y1),
   the release; screen pixels, y up, as Input.mousePosition. button -1: no press (a hover / the wheel only); roll: the wheel's
   y on each moving frame */
static struct { int on, button, i, n; float x0, y0, x1, y1, wheel; } ch_fk;
#ifdef TEAMS_MOUSE_TEST
static V2 ch_fake_wheel; static int ch_fake_wheel_n;                         /* test: a fake mouse wheel for n frames */
#endif
#define CH_C_TRACK   ((Col){ 0.141f, 0.118f, 0.122f, 1 })                   /* the game's ScrollBar_Base #241e1f */
#define CH_C_THUMB   ((Col){ 0.263f, 0.247f, 0.267f, 1 })                   /* ScrollBar_bar #433f44 */
#define CH_C_THUMBHI ((Col){ 0.341f, 0.333f, 0.349f, 1 })                   /* its top edge #575559 */
static uintptr_t h_ch_thumbhi;
/* a small icon of square pixels (the game's pixel look, like the grip): rows of '#' in a w x h grid, px units each, centred
   in its parent; kept in h_ch_icon[slot] so the hover can recolour it */
static void ch_icon(Obj tpl_img, Obj parent, int slot, const char *const *rows, int w, int h, float px) {
    for (int i = 0; i < n_ch_icon[slot]; i++) { u_gc_free(h_ch_icon[slot][i]); h_ch_icon[slot][i] = 0; }
    n_ch_icon[slot] = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; ) {
            if (rows[y][x] != '#') { x++; continue; }
            int x1 = x; while (x1 < w && rows[y][x1] == '#') x1++;
            Obj d = mk_img(tpl_img, u_tr(parent), -1, C_SUB, ((x + x1) / 2.0f - w / 2.0f) * px, (h / 2.0f - y - 0.5f) * px, (x1 - x) * px, px);
            if (d && n_ch_icon[slot] < CH_ICON_PX) h_ch_icon[slot][n_ch_icon[slot]++] = u_gc_new(d);
            x = x1;
        }
}
static void ch_icon_hover(int slot) {                                          /* the hovered icon in the main text colour */
    if (slot == ch_icon_hot) return;
    for (int s = 0; s < 4; s++) if (s == slot || s == ch_icon_hot)
        for (int i = 0; i < n_ch_icon[s]; i++) { Obj d = u_gc_get(h_ch_icon[s][i]); if (mm_is(d, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(d, s == slot ? C_MAIN : C_SUB, NULL); }
    ch_icon_hot = slot;
}
static void chat_build(Obj parent) {
    static uintptr_t hc; static void *kc;
    Obj cn = find_view_x(&hc, &kc, "Refactor.UI", "CommonNoticeView", true);
    Obj nt = cn ? tr_find(u_tr(cn), "Panel_Notice/Txt_Notice") : NULL, lb = cn ? tr_find(u_tr(cn), "Img_BlackLetterBox") : NULL;
    Obj tpl_txt = nt ? comp_of(nt, "TMPro", "TMP_Text") : NULL, tpl_img = lb ? image_of(lb) : NULL;
    if (!tpl_txt || !tpl_img) { rig_log("mpmenu: chat: templates not found"); return; }
    Obj root = mk_img(tpl_img, parent, -1, (Col){ 0, 0, 0, 0 }, 0, 0, 10, 10);
    if (!root) return;
    u_gc_free(h_ch_root); h_ch_root = u_gc_new(root); u_gc_free(h_ch_view); h_ch_view = u_gc_new(parent);
    Obj R = u_tr(root);
    for (int i = 0; i < CH_IDLE; i++) {                                    /* idle lines: a soft plate + the text */
        Obj bg = mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 0.7f }, 0, 0, 430, 26);
        Obj t = mk_txt(tpl_txt, R, 0, 16, 513, 1, 0, 0, 410, 26, C_MAIN); ch_text_style(t, 16); ch_idle_id[i] = 0; ch_idle_font[i] = -1;
        u_gc_free(h_ch_idlebg[i]); h_ch_idlebg[i] = bg ? u_gc_new(bg) : 0; u_gc_free(h_ch_idle[i]); h_ch_idle[i] = t ? u_gc_new(t) : 0;
        if (bg && mm_is(bg, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bg, true, NULL);   /* lead lan_v10: a click on a shown chat line is a UI click
                                                                                                   (IsPointerOverUI sees it), never a world click; the plate is off when the line has faded */
    }
    { Obj bg = mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 0.7f }, 0, 0, 430, 22);      /* "<Name> is typing..." */
      Obj t = mk_txt(tpl_txt, R, 0, 14, 513, 1, 0, 0, 410, 22, C_SUB); ch_text_style(t, 14); ch_typing_font = -1;
      u_gc_free(h_ch_typingbg); h_ch_typingbg = bg ? u_gc_new(bg) : 0; u_gc_free(h_ch_typing); h_ch_typing = t ? u_gc_new(t) : 0;
      if (bg && mm_is(bg, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bg, true, NULL);   /* a UI click too */
      u_active(bg, false); u_active(t, false); }
    Obj box = mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, CH_W0, CH_H0);
    if (box && mm_is(box, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(box, true, NULL);   /* the open box: UI clicks too */
    for (int i = 0; i < CL_N; i++) { u_gc_free(h_ch_lay[i]); h_ch_lay[i] = 0; }
    if (box) {
        Obj B = u_tr(box), o;
#define CH_KEEP(id, x) do { o = (x); h_ch_lay[id] = o ? u_gc_new(o) : 0; } while (0)
        /* one plain colour under the history and one under the input line: the stock "Box" sprite has a darker footer of
           its own (34 of its 104 px) that, stretched to this box, ended in the middle of the history (owner 2026-10-21).
           Built at the default size; ch_layout places every part for the box's size. */
        CH_KEEP(CL_BAND, mk_img(tpl_img, B, -1, C_BAND, 0, 0, 10, 10));
        CH_KEEP(CL_FOOT, mk_img(tpl_img, B, -1, (Col){ 0.133f, 0.125f, 0.145f, 1 }, 0, 0, 10, 10));
        CH_KEEP(CL_TBAND, mk_img(tpl_img, B, -1, C_BAND, 0, 0, 10, 10));
        CH_KEEP(CL_TLINE, mk_img(tpl_img, B, -1, C_LINE, 0, 0, 10, 1));
        CH_KEEP(CL_TITLE, mk_txt(tpl_txt, B, 1, 15, 513, 0, 0, 0, 120, 26, C_MAIN)); if (o) tmp_set(o, TR("Chat"));
        Obj hi = mk_txt(tpl_txt, B, 0, 11, 516, 0, 0, 0, 240, 22, C_SUB);
        if (hi) tmp_set(hi, TR("Right-click and drag to move"));
        u_gc_free(h_ch_hint); h_ch_hint = hi ? u_gc_new(hi) : 0;
        for (int i = 0; i < CH_BOX_LINES; i++) {
            Obj l = mk_txt(tpl_txt, B, 0, 15, 513, 1, 0, 0, CH_W0 - 24, 20, C_MAIN); ch_text_style(l, 15); ch_box_id[i] = 0; ch_box_font[i] = -1;
            if (l) u_active(l, false);
            u_gc_free(h_ch_lines[i]); h_ch_lines[i] = l ? u_gc_new(l) : 0;
        }
        CH_KEEP(CL_ILINE, mk_img(tpl_img, B, -1, C_LINE, 0, 0, 10, 1));
        /* "Say:" 40 wide, or as wide as its text ("Сказать:"); the field from 7 after it to 15 before the box's right edge */
        Obj say = mk_txt(tpl_txt, B, 0, 14, 513, 0, 0, 0, 40, 24, C_GOLD); if (say) tmp_set(say, TR("Say:"));
        float sw = say && fit_methods() ? fit_pref(say, 0).x + 2 : 0; ch_say_w = sw < 40 ? 40 : sw > 100 ? 100 : sw;
        h_ch_lay[CL_SAY] = say ? u_gc_new(say) : 0;
        CH_KEEP(CL_FLINE, mk_img(tpl_img, B, -1, C_LINE, 0, 0, 10, 30));
        CH_KEEP(CL_FBG, mk_img(tpl_img, B, -1, (Col){ 0.106f, 0.098f, 0.122f, 1 }, 0, 0, 10, 28));
        Obj in = mk_txt(tpl_txt, B, 0, 15, 513, 0, 0, 0, 10, 26, C_MAIN);
        u_gc_free(h_ch_input); h_ch_input = in ? u_gc_new(in) : 0;
        /* the real text field (the game's own input handling: any keyboard layout, accents): the Rename popup's field */
        Obj rp = popup_prefab(19), fs = rp ? tr_find(u_tr(rp), "Input_SaveFileName") : NULL, fsrc = fs ? comp_of(fs, "TMPro", "TMP_InputField") : NULL;
        Obj f = fsrc ? u_instantiate(fsrc, B) : NULL;
        if (u_alive(f)) {
            u_active(f, true); place(f, 0, 0, 10, 28);
            field_area(f, 8, 2);                                            /* the typed text from 8 in, as tall as the field (stock: 36 in, 15 high) */
            Obj fim = image_of(f); if (fim) img_set(fim, NULL, (Col){ 0, 0, 0, 0 });   /* our own field plate shows */
            if (mm_is(fim, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(fim, true, NULL);
            F(0x4EFC2D8, void (*)(Obj, int, void *))(f, DSNET_CHAT_MAX, NULL);                 /* characterLimit */
            static void *mtc; if (!mtc) mtc = mth("TMPro", "TMP_InputField", "get_textComponent", 0);
            Obj tc = mtc ? MCALL(mtc, Obj, Obj, const void *)(f, mtc) : NULL;
            if (u_alive(tc)) { txt_field(tc); txt_style(tc, 0, 15, 513, 0); txt_color(tc, C_MAIN); }
            ch_field_font = -1;
            Obj ph = F(0x4EFB9CC, Obj (*)(Obj, void *))(f, NULL); if (u_alive(ph) && strstr(cls_name(ph), "Text")) tmp_set(ph, "");
            F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(""), NULL);
            u_active(in, false);                                            /* the field draws the text and the caret */
        }
        u_gc_free(h_ch_field); h_ch_field = u_alive(f) ? u_gc_new(f) : 0;
        rig_log("mpmenu: chat text field %s", u_alive(f) ? "= the Rename popup's TMP_InputField" : "not found (typed keys only in tests)");
        /* the corner grip: six small squares in a triangle (the game's pixel look, like the cog), 14 x 14 in the corner */
        CH_KEEP(CL_GRIP, mk_img(tpl_img, B, -1, (Col){ 0, 0, 0, 0 }, 0, 0, 14, 14));
        if (o) { if (mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, false, NULL);
                 for (int a = 0; a < 3; a++) for (int c = 0; a + c < 3; c++) {
                     Obj d = mk_img(tpl_img, u_tr(o), -1, C_SUB, 5.5f - 5 * a, -5.5f + 5 * c, 3, 3);
                     if (d && mm_is(d, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(d, false, NULL); } }
        /* clear strips under the bottom edge and right of the corner: a press there is a UI click (IsPointerOverUI), never
           a world click, and grabs the edge / the corner (the box itself has only 9 free units under the field) */
        CH_KEEP(CL_STRIPB, mk_img(tpl_img, B, -1, (Col){ 0, 0, 0, 0 }, 0, 0, 10, 6));
        if (o && mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, true, NULL);
        CH_KEEP(CL_STRIPR, mk_img(tpl_img, B, -1, (Col){ 0, 0, 0, 0 }, 0, 0, 6, 20));
        if (o && mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, true, NULL);
        /* read state (owner 2026-10-25): the scrollbar in the game's colours (its ScrollBar_Base #241e1f track, ScrollBar_bar
           #433f44 handle with a #575559 top edge), Top / Bottom buttons at its ends, the bin left of the grip, the X in the
           title band. Plain images, never Selectables (a selected Button would take Space as Submit); clicks are read by
           chat_frame like the drags, and each is a raycast target (a UI click, never a world click). */
        CH_KEEP(CL_TRACK, mk_img(tpl_img, B, -1, CH_C_TRACK, 0, 0, 10, 10));
        if (o && mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, true, NULL);
        CH_KEEP(CL_THUMB, mk_img(tpl_img, B, -1, CH_C_THUMB, 0, 0, 6, 16));
        if (o && mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, true, NULL);
        { Obj hi2 = o ? mk_img(tpl_img, u_tr(o), -1, CH_C_THUMBHI, 0, 0, 4, 2) : NULL; u_gc_free(h_ch_thumbhi); h_ch_thumbhi = hi2 ? u_gc_new(hi2) : 0; }
        static const char *const ic_top[] = { "#####", ".....", "..#..", ".###.", "#####" }, *const ic_bot[] = { "#####", ".###.", "..#..", ".....", "#####" },
                                 *const ic_bin[] = { "..###..", "#######", ".#####.", ".#.#.#.", ".#.#.#.", ".#.#.#.", ".#.#.#.", ".#####." },
                                 *const ic_x[] = { "#...#", ".#.#.", "..#..", ".#.#.", "#...#" };
        CH_KEEP(CL_TOP, mk_img(tpl_img, B, -1, CH_C_TRACK, 0, 0, 14, 14));
        if (o) { if (mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, true, NULL); ch_icon(tpl_img, o, 0, ic_top, 5, 5, 2); }
        CH_KEEP(CL_BOT, mk_img(tpl_img, B, -1, CH_C_TRACK, 0, 0, 14, 14));
        if (o) { if (mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, true, NULL); ch_icon(tpl_img, o, 1, ic_bot, 5, 5, 2); }
        CH_KEEP(CL_BIN, mk_img(tpl_img, B, -1, (Col){ 0, 0, 0, 0 }, 0, 0, 16, 18));
        if (o) { if (mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, true, NULL); ch_icon(tpl_img, o, 2, ic_bin, 7, 8, 2); }
        /* the X: a copy of the game's own close button (the Task Planner window's CloseButton with its X, as the Teams
           window), 32 x 32 shown at 0.75; its Button off (never a Selectable: Space would Submit it), clicks read by
           chat_frame. Not found: a pixel X */
        { Obj pf = popup_prefab(13), cb = pf ? tr_find(u_tr(pf), "Img_BackPanel/CloseButton") : NULL, cl = cb ? u_instantiate(cb, B) : NULL;
          if (u_alive(cl)) {
              static void *men; if (!men) men = mth("UnityEngine", "Behaviour", "set_enabled", 1);
              Obj btn = comp_of(cl, "UnityEngine.UI", "Button"); if (u_alive(btn) && men) MCALL(men, void, Obj, bool, const void *)(btn, false, men);
              u_active(cl, true); place(cl, 0, 0, 32, 32);
              F(0x510AFF8, void (*)(Obj, V3, void *))(u_tr(cl), (V3){ 0.75f, 0.75f, 1 }, NULL);   /* localScale */
              Obj cim = image_of(cl); if (cim && mm_is(cim, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(cim, true, NULL);
              o = cl; h_ch_lay[CL_CLOSE] = u_gc_new(cl);
              for (int i = 0; i < n_ch_icon[3]; i++) { u_gc_free(h_ch_icon[3][i]); h_ch_icon[3][i] = 0; } n_ch_icon[3] = 0;
          } else {
              CH_KEEP(CL_CLOSE, mk_img(tpl_img, B, -1, (Col){ 0, 0, 0, 0 }, 0, 0, 22, 22));
              if (o) { ch_icon(tpl_img, o, 3, ic_x, 5, 5, 3); if (mm_is(o, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(o, true, NULL); }
          }
          rig_log("mpmenu: chat X = %s", u_alive(cl) ? "a copy of the game's CloseButton" : "pixel X (CloseButton not found)"); }
        ch_icon_hot = -1;
#undef CH_KEEP
        ch_lay_w = ch_lay_h = 0; ch_lay_ro = -1;                            /* ch_layout places them at the first frame */
        u_active(box, false);
    }
    u_gc_free(h_ch_box); h_ch_box = box ? u_gc_new(box) : 0;
    rig_log("mpmenu: chat built");
}
/* the open box's parts for its size W x H (canvas units, centre origin): the same places as the old fixed 470 x 270 box,
   measured from its edges (title band at the top, "Say:" + field at the bottom, the history between). ro (read only: the
   player cannot send, single player): no "Say:", no field, the history goes down to the bin and the grip. Right of the
   history: the scrollbar column (Top button, track, Bottom button); the X right in the title band. */
#define CH_HIST_TOP(hh)     ((hh) - 31)                                         /* the history band: under the title line */
#define CH_HIST_BOT(hh, ro) ((ro) ? -(hh) + 24 : -(hh) + 46)                    /* over the input line / over the bin and grip */
#define CH_LINE_X  (-6.0f)                                                     /* box lines: 12 in from the left, 24 from the right (the column) */
#define CH_LINE_DW 36.0f
static void ch_layout(float W, float H, int ro) {
    float hw = W / 2, hh = H / 2, sw = ch_say_w, ht = CH_HIST_TOP(hh), hb = CH_HIST_BOT(hh, ro);
    Obj o;
#define CH_P(id, x, y, w, h) do { o = u_gc_get(h_ch_lay[id]); if (o) place(o, x, y, w, h); } while (0)
#define CH_ON(id, on) do { o = u_gc_get(h_ch_lay[id]); if (o) u_active(o, on); } while (0)
    float bb = ro ? -hh + 3 : -hh + 46;                                       /* the history colour reaches the bottom when read only */
    CH_P(CL_BAND, 0, (ht + bb) / 2, W - 4, ht - bb);
    CH_P(CL_FOOT, 0, ((-hh + 46) + (-hh + 3)) / 2, W - 4, 43);
    CH_P(CL_TBAND, 0, hh - 16, W - 4, 30);
    CH_P(CL_TLINE, 0, hh - 31, W - 4, 1);
    CH_P(CL_TITLE, -hw + 12 + 60, hh - 16, 120, 26);
    { Obj hi = u_gc_get(h_ch_hint); if (hi) place(hi, hw - 32 - 120, hh - 16, 240, 22); }   /* right-aligned, left of the X */
    { Obj cx = u_gc_get(h_ch_lay[CL_CLOSE]); if (cx) place(cx, hw - 17, hh - 16, n_ch_icon[3] ? 22 : 32, n_ch_icon[3] ? 22 : 32); }   /* the game's X: 32 at scale 0.75 */
    CH_P(CL_ILINE, 0, -hh + 46, W - 4, 1);
    CH_P(CL_SAY, -hw + 12 + sw / 2, -hh + 23, sw, 24);
    float fl = -hw + 12 + sw + 7, fr = hw - 40, fw = fr - fl, fx = (fl + fr) / 2;   /* the field ends left of the bin */
    CH_P(CL_FLINE, fx, -hh + 23, fw, 30);
    CH_P(CL_FBG, fx, -hh + 23, fw - 2, 28);
    { Obj in = u_gc_get(h_ch_input), f = u_gc_get(h_ch_field);
      if (in) place(in, fx, -hh + 23, fw - 16, 26);
      if (u_alive(f)) { place(f, fx, -hh + 23, fw - 2, 28); u_active(f, !ro); } }
    CH_ON(CL_FOOT, !ro); CH_ON(CL_ILINE, !ro); CH_ON(CL_SAY, !ro); CH_ON(CL_FLINE, !ro); CH_ON(CL_FBG, !ro);
    CH_P(CL_BIN, hw - 29, ro ? -hh + 12 : -hh + 23, 16, 18);                  /* left of the grip */
    CH_P(CL_GRIP, hw - 10, -hh + 10, 14, 14);                              /* 3 from the corner; clear of the field (ends 15 in) */
    CH_P(CL_STRIPB, 0, -hh - 3, W, 6);
    CH_P(CL_STRIPR, hw + 3, -hh + 4, 6, 20);
    CH_P(CL_TOP, hw - 12, ht - 10, 14, 14);
    CH_P(CL_BOT, hw - 12, hb + 10, 14, 14);
    { float t0 = ht - 20, t1 = hb + 20; CH_P(CL_TRACK, hw - 12, (t0 + t1) / 2, 10, t0 - t1 > 4 ? t0 - t1 : 4); }   /* the thumb: chat_frame */
#undef CH_P
#undef CH_ON
    ch_lay_w = W; ch_lay_h = H; ch_lay_ro = ro;
}
/* where the open box is (canvas units: left, bottom, width, height; the canvas is 1080 high, 2 * hw wide): the remembered
   place and size, inside the screen. Size: 470 x 270 at least, 80 % of the canvas width and 90 % of its height at most.
   Place: 4 free on each side, 34 at the top for the "is typing" line over the open box. A place / size remembered on a
   bigger window shows clamped here and stays remembered until the next drag. */
typedef struct { float l, b, w, h; } ch_rect;
static float ch_max_w(float hw) { float m = 0.8f * 2 * hw; return m < CH_W0 ? CH_W0 : m; }
#define CH_MAX_H (0.9f * 1080)
static ch_rect ch_clamp(ch_rect g, float hw) {
    float mw = ch_max_w(hw);
    if (g.w > mw) g.w = mw; if (g.w < CH_W0) g.w = CH_W0;
    if (g.h > CH_MAX_H) g.h = CH_MAX_H; if (g.h < CH_H0) g.h = CH_H0;
    if (g.l > hw - 4 - g.w) g.l = hw - 4 - g.w; if (g.l < -hw + 4) g.l = -hw + 4;
    if (g.b > 540 - 34 - g.h) g.b = 540 - 34 - g.h; if (g.b < -540 + 4) g.b = -540 + 4;
    return g;
}
static ch_rect ch_geo(float hw) { return ch_clamp((ch_rect){ -hw + 130 + chat_x, -540 + 130 + chat_y, chat_w, chat_h }, hw); }
static ch_rect ch_g0;                                                          /* the box when the drag started */
static int dbg_istr_n, dbg_enter_n, dbg_a_n;
/* give the chat field the keyboard: the box must be active first (ActivateInputField ignores an inactive field: owner
 * lan_v8 "Enter opens the box but I can never type"), then select it in the EventSystem and activate it */
static void chat_focus(void) {
    Obj f = u_gc_get(h_ch_field), bx = u_gc_get(h_ch_box);
    if (!u_alive(f) || !bx) return;
    u_active(bx, true);
    static void *mfoc, *mcur, *mset, *ma;
    if (!mfoc) { mfoc = mth("TMPro", "TMP_InputField", "get_isFocused", 0); mcur = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
                 mset = mth("UnityEngine.EventSystems", "EventSystem", "SetSelectedGameObject", 1); ma = mth("TMPro", "TMP_InputField", "ActivateInputField", 0); }
    if (mfoc && MCALL(mfoc, bool, Obj, const void *)(f, mfoc)) return;     /* already has it */
    if (AT(f, 0x261, bool)) return;                                         /* m_ShouldActivateNextUpdate: activates in its LateUpdate */
    Obj es = mcur ? MCALL(mcur, Obj, const void *)(mcur) : NULL;
    if (u_alive(es) && mset) MCALL(mset, void, Obj, Obj, const void *)(es, u_go(f), mset);
    if (ma) MCALL(ma, void, Obj, const void *)(f, ma);
}
static float ch_wheel_y(void (*wheel)(V2 *)) {                               /* this frame's wheel (the injected icall: not blocked) */
    V2 d = { 0, 0 }; if (wheel) wheel(&d);
#ifdef TEAMS_MOUSE_TEST
    if (ch_fake_wheel_n > 0) d = ch_fake_wheel;
#endif
    return d.y;
}
/* A4 (owner 2026-10-20: the wheel scrolled the chat AND zoomed the map): while the pointer is over the chat, the game's
   managed Input.mouseScrollDelta reads 0. Its callers: MouseIdleState.DetectWheelInput (the camera zoom; on Windows inlined
   into MouseIdleState.HandleInput, which calls this getter too), EventSystem / UI scroll, Unity's debug UI. The chat reads
   the wheel through the icall directly, so it still scrolls. */
static V2 (*o_scroll_delta)(void *);
static V2 h_scroll_delta(void *mi) {
    V2 d = o_scroll_delta(mi);
#ifdef TEAMS_MOUSE_TEST
    if (ch_fake_wheel_n > 0) d = ch_fake_wheel;
#endif
    if ((d.x != 0 || d.y != 0) && ch_over_now && chat_on) { ch_wheel_blocked++; return (V2){ 0, 0 }; }
    return d;
}
/* the field gives the keyboard back, and leaves the EventSystem's selection: a selected TMP_InputField takes Space / Enter
   as Submit and focuses itself again (the game's Space must pause in the read state) */
static void chat_field_release(void) {
    Obj f = u_gc_get(h_ch_field); if (!u_alive(f)) return;
    static void *md1, *md0, *mcur, *msel, *mset;
    if (!mcur) { md1 = mth("TMPro", "TMP_InputField", "DeactivateInputField", 1); if (!md1) md0 = mth("TMPro", "TMP_InputField", "DeactivateInputField", 0);
                 mcur = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0); msel = mth("UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0);
                 mset = mth("UnityEngine.EventSystems", "EventSystem", "SetSelectedGameObject", 1); }
    if (md1) MCALL(md1, void, Obj, bool, const void *)(f, false, md1); else if (md0) MCALL(md0, void, Obj, const void *)(f, md0);
    Obj es = mcur ? MCALL(mcur, Obj, const void *)(mcur) : NULL, sel = u_alive(es) && msel ? MCALL(msel, Obj, Obj, const void *)(es, msel) : NULL;
    if (u_alive(sel) && sel == u_go(f) && mset) MCALL(mset, void, Obj, Obj, const void *)(es, NULL, mset);
}
/* typing ends (sent, Escape, the X, a click on the history): the others drop "is typing"; clear = the draft goes too */
static void chat_end_typing(dsnet_client *c, int clear) {
    chat_typing = 0; chat_close_frames = 2;
    if (c) dsnet_chat_typing(c, 0);
    Obj f = u_gc_get(h_ch_field);
    if (clear && u_alive(f)) F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(""), NULL);
    chat_field_release();
}
static void chat_box_close(const char *why) {
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL;
    int was = chat_typing || chat_read;
    if (chat_typing) chat_end_typing(c, 1);
    chat_read = 0;
    if (was) rig_log("mpmenu: chat box closed (%s)", why);
}
static void chat_esc_close(void *ud) { (void)ud; chat_box_close("Escape"); }
static mpesc_win esc_chat = { .name = "the chat", .close = chat_esc_close };   /* the read state on the kit's Escape stack */
/* how many of the oldest lines fit the history from its top: the Top button's place, and the scrollbar's range (nch minus
   it). A line never measured at this width is measured in a box line slot. Cached until the oldest line, the width, the
   height, the fonts or (when all lines fitted) the count change. */
static int ch_top_c, ch_top_id = -1, ch_top_n; static unsigned ch_top_gen; static float ch_top_bw, ch_top_av;
static int ch_top_count(float bw, float avail) {
    int id0 = nch ? chist[0].id : 0;
    if (!ch_top_stale && id0 == ch_top_id && bw == ch_top_bw && avail == ch_top_av && chf_gen == ch_top_gen && (ch_top_c < ch_top_n || nch == ch_top_n))
        return ch_top_c > nch ? nch : ch_top_c;
    float used = 0; int n = 0;
    for (int k = 0; k < nch && n < CH_BOX_LINES; k++) {
        ch_prepare(k);
        if (chist[k].hb <= 0) { Obj l = u_gc_get(h_ch_lines[n]); if (!l) break;
                                ch_slot_show(l, &ch_box_id[n], &ch_box_font[n], k); place(l, CH_LINE_X, 0, bw, 20); chist[k].hb = ch_measure(l, bw); }
        if (used + chist[k].hb > avail && n > 0) break;
        used += chist[k].hb + 2; n++;
    }
    ch_top_c = n; ch_top_id = id0; ch_top_n = nch; ch_top_bw = bw; ch_top_av = avail; ch_top_gen = chf_gen; ch_top_stale = 0;
    return n;
}
static int ch_want_top, ch_s0;                                                /* Top pressed (placed at the next layout); scroll when the thumb drag started */
static struct { float t1, L, T; int S, vis; } ch_sb;                          /* the scrollbar now: track bottom + length, thumb length, range, lines shown */
enum { CHB_NONE, CHB_CLOSE, CHB_TOP, CHB_BOT, CHB_BIN, CHB_THUMB, CHB_TRACK };
static int ch_hit(V3 m) {                                                      /* the box's button / scrollbar part under m */
    if (pt_in(u_gc_get(h_ch_lay[CL_CLOSE]), m)) return CHB_CLOSE;
    if (pt_in(u_gc_get(h_ch_lay[CL_TOP]), m)) return CHB_TOP;
    if (pt_in(u_gc_get(h_ch_lay[CL_BOT]), m)) return CHB_BOT;
    if (pt_in(u_gc_get(h_ch_lay[CL_BIN]), m)) return CHB_BIN;
    if (pt_in(u_gc_get(h_ch_lay[CL_THUMB]), m)) return CHB_THUMB;
    if (pt_in(u_gc_get(h_ch_lay[CL_TRACK]), m)) return CHB_TRACK;
    return CHB_NONE;
}
static void chat_frame(const mpm_obs *o) {
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL;
#ifdef TEAMS_MOUSE_TEST
    if (ch_fake_wheel_n > 0) ch_fake_wheel_n--;
#endif
    ch_over_now = 0;
    if (chat_close_frames > 0) chat_close_frames--;
    /* no session (mp_api v3 chat_local): the chat shows in a world while a local line is in the history or the player reads
       the box (the bin may have emptied it); on the title the local lines go (a caller that never cleared them). Otherwise:
       hidden, no keys taken, as before. */
    if (!c && ch_nlocal && o->scene == 1) chat_clear_local();
    if (!c && ((!ch_nlocal && !chat_read) || o->scene != 2)) {
        chat_typing = chat_read = 0; mpesc_drop(&esc_chat);
        Obj r = u_gc_get(h_ch_root), bx = u_gc_get(h_ch_box); if (u_alive(r)) u_active(r, false); if (u_alive(bx)) u_active(bx, false); return;
    }
    if (!c && chat_typing) { chat_end_typing(NULL, 1); rig_log("mpmenu: chat: the session ended while typing: read only now"); }
    if (c) chat_pull(c);
    /* A6: every scene of a session (title: connecting / waiting for the host; lobby; loading; the world; PvP prep), on our
       own canvas. The templates (CommonNoticeView, the Rename popup) exist from the title on. */
    Obj parent = chat_parent(); if (!parent) return;
    if (u_gc_get(h_ch_view) != parent || !u_alive(u_gc_get(h_ch_root))) {
        static int64_t tried = -1000; if (frames - tried < 60) return;     /* templates not loaded yet: again in 2 s */
        tried = frames;
        mpesc_drop(&esc_chat);
        chat_build(parent);
        if (!u_alive(u_gc_get(h_ch_root))) return;
        Obj f = u_gc_get(h_ch_field), bx = u_gc_get(h_ch_box);              /* rebuilt while typing: keep the draft open */
        if (chat_typing && u_alive(f) && bx) { u_active(bx, true); F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(chat_buf), NULL); chat_focus(); }
    }
    Obj root = u_gc_get(h_ch_root); if (!u_alive(root)) return;
    u_active(root, chat_on != 0);
    if (!chat_on) { chat_typing = chat_read = 0; mpesc_drop(&esc_chat); return; }
    { Obj cv = u_gc_get(h_ch_cv); if (u_alive(cv) && ch_cv_order != CH_SORT) { static void *mo; if (!mo) mo = mth("UnityEngine", "Canvas", "set_sortingOrder", 1);
                                                                          if (mo) { MCALL(mo, void, Obj, int, const void *)(cv, CH_SORT, mo); ch_cv_order = CH_SORT; } } }
    /* keys (icalls: Input.GetKeyDownInt / get_inputString; tests feed the same paths) */
    static bool (*kd)(int); static Obj (*istr)(void); static bool (*mb)(int); static void (*mpos)(V3 *); static void (*wheel)(V2 *); static int looked;
    if (!looked) { looked = 1; void *(*ri)(const char *) = F(0x6F8B90, void *(*)(const char *));
        kd = (bool (*)(int))ri("UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)"); istr = (Obj (*)(void))ri("UnityEngine.Input::get_inputString()");
        mb = (bool (*)(int))ri("UnityEngine.Input::GetMouseButton(System.Int32)"); mpos = (void (*)(V3 *))ri("UnityEngine.Input::get_mousePosition_Injected(UnityEngine.Vector3&)");
        wheel = (void (*)(V2 *))ri("UnityEngine.Input::get_mouseScrollDelta_Injected(UnityEngine.Vector2&)");
        rig_log("mpmenu: chat keys %s, text %s, mouse %s", kd ? "ok" : "MISSING", istr ? "ok" : "MISSING", mb && mpos ? "ok" : "MISSING"); }
    { char d[64] = ""; if (istr) str_utf8(istr(), d, sizeof d); dbg_istr_n += (int)strlen(d); if (kd && kd(13)) dbg_enter_n++; if (kd && kd(97)) dbg_a_n++; }   /* test status: what Unity's input saw */
    /* read only (owner 2026-10-25): with no session the player cannot send, so the box has no entry field and never takes the keys */
    int ro = !c;
    int enter = (kd && (kd(13) || kd(271))) || test_chat_enter, esc = (kd && kd(27)) || test_chat_escape;
    test_chat_enter = test_chat_escape = 0;
    if (!chat_typing) {
        /* A6: Enter opens the chat in the Esc menu too: the host's lobby IS that menu (popup_activate). Not while another
           text field has the keys (the game's, or one of ours: name / address on the title pages) */
        int other_text = o_textfocus && o_textfocus(NULL);
        if (!other_text) { static void *mcur, *msel; if (!mcur) { mcur = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0); msel = mth("UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0); }
            Obj es = mcur ? MCALL(mcur, Obj, const void *)(mcur) : NULL, sel = u_alive(es) && msel ? MCALL(msel, Obj, Obj, const void *)(es, msel) : NULL;
            Obj tin = type_obj("TMPro", "TMP_InputField");
            Obj fi = u_alive(sel) && tin && mm_is(sel, K_GO) ? F(0x50EDAA8, Obj (*)(Obj, Obj, void *))(sel, tin, NULL) : NULL;   /* GameObject.GetComponent(Type) */
            static void *mfoc; if (!mfoc) mfoc = mth("TMPro", "TMP_InputField", "get_isFocused", 0);
            if (u_alive(fi) && mfoc && MCALL(mfoc, bool, Obj, const void *)(fi, mfoc)) {
                if (fi != u_gc_get(h_ch_field)) other_text = 1;
                else if (chat_read && !ro) { chat_typing = 1; rig_log("mpmenu: chat: a click in the field: typing"); }   /* TMP's own click focused it */
                else chat_field_release();                                  /* focused with no box to type in: let go */
            } }
        if ((enter || test_chat_open) && !other_text && !chat_typing) {
            if (!ro) {
                int from_read = chat_read; chat_typing = 1; enter = 0;
                Obj f = u_gc_get(h_ch_field);
                if (!from_read) { chat_buf[0] = 0; chat_scroll = 0; if (u_alive(f)) F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(""), NULL); }   /* reading: the draft and the place stay */
                if (u_alive(f)) chat_focus();
            } else {                                                        /* read only: Enter shows / hides the box (no field) */
                chat_read = test_chat_open ? 1 : !chat_read; if (chat_read) chat_scroll = 0; enter = 0;
                rig_log("mpmenu: chat: Enter %s the read-only box", chat_read ? "opens" : "closes");
            }
        }
        test_chat_open = 0;
    }
    if (chat_typing) {
        Obj f = u_gc_get(h_ch_field);
        if (!enter && !esc) chat_focus();                                   /* keeps the keyboard while typing (a click on the world took it) */
        if (u_alive(f)) {                                                  /* the field holds the text; tests write into it */
            if (test_chat_in[0]) { char cur[DSNET_CHAT_MAX + 1]; utf16_to_utf8(F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL), cur, sizeof cur);
                                   char nw[1024]; snprintf(nw, sizeof nw, "%s%s", cur, test_chat_in); F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(nw), NULL); test_chat_in[0] = 0; }
            /* A5: the line is sent as UTF-8, at most DSNET_CHAT_MAX bytes (accented / Cyrillic letters are 2 bytes, CJK 3):
               a longer text is cut back in the field at a whole character, so what the player sees is what is sent */
            char all[1024]; utf16_to_utf8(F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL), all, sizeof all);
            snprintf(chat_buf, sizeof chat_buf, "%s", all); dsi_utf8_fix_end(chat_buf);
            if (strlen(all) > strlen(chat_buf)) {
                F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(chat_buf), NULL);
                static void *mcp; if (!mcp) mcp = mth("TMPro", "TMP_InputField", "MoveTextEnd", 1);
                if (mcp) MCALL(mcp, void, Obj, bool, const void *)(f, false, mcp);
            }
        }
        { static char last[DSNET_CHAT_MAX + 1]; if (strcmp(last, chat_buf)) { snprintf(last, sizeof last, "%s", chat_buf); if (chat_buf[0] && c) dsnet_chat_typing(c, 1);
              /* the typed text in a font that has its characters (Japanese typed into a Latin font showed boxes) */
              char tmp[CH_TEXT]; snprintf(tmp, sizeof tmp, "%s", chat_buf); int fi = ch_pick(tmp, 0, NULL);
              static void *mtc; if (!mtc) mtc = mth("TMPro", "TMP_InputField", "get_textComponent", 0);
              Obj tc = u_alive(f) && mtc ? MCALL(mtc, Obj, Obj, const void *)(f, mtc) : NULL;
              if (fi >= 0 && fi != ch_field_font && u_alive(tc)) { ch_font_set(tc, fi, 1); ch_field_font = fi; } } }   /* a key changed the line: "is typing" (dsnet sends at most 1/s) */
        char in[256] = ""; if (istr && !u_alive(f)) str_utf8(istr(), in, sizeof in);
        size_t L = strlen(in), T = strlen(test_chat_in); if (T && L + T < sizeof in) { memcpy(in + L, test_chat_in, T + 1); } test_chat_in[0] = 0;
        for (char *q = in; *q; q++) {
            size_t n = strlen(chat_buf);
            if (*q == '\b') { while (n && ((unsigned char)chat_buf[n - 1] & 0xC0) == 0x80) n--; if (n) n--; chat_buf[n] = 0; }
            else if (*q == '\n' || *q == '\r') enter = 1;
            else if ((unsigned char)*q >= 0x20 && n < DSNET_CHAT_MAX) { chat_buf[n] = *q; chat_buf[n + 1] = 0; }
        }
        if (enter || esc) {
            if (enter && chat_buf[0] && chat_is_clear_cmd(chat_buf)) chat_clear_view("/clear");   /* this screen only, never sent */
            else if (enter && chat_buf[0]) { int rc = c ? dsnet_chat_send(c, chat_buf) : -1;
                rig_log("mpmenu: chat %s (%zu bytes, rc %d)", c ? "sent" : "not sent (no session)", strlen(chat_buf), rc);
                if (rc) { char ln[300]; snprintf(ln, sizeof ln, "<i><color=#F16071>%s</color></i>", TR("Message not sent: no connection to the other players.")); chat_add(ln); } }   /* F18 */
            chat_end_typing(c, 1);                                          /* Escape leaves the field: a box opened for reading stays */
        }
        if (kd && kd(280) && !ch_box_oldest) chat_scroll += 3; if (kd && kd(281)) chat_scroll = chat_scroll > 3 ? chat_scroll - 3 : 0;   /* PageUp / PageDown (typing: the keys are ours) */
        int maxs = nch > 1 ? nch - 1 : 0; if (chat_scroll > maxs) chat_scroll = maxs;
    }
    int box_open = chat_typing || chat_read;
    /* place: bottom-left above the build bar, right of the resource list (canvas x 0..~105), plus the dragged offset.
     * The canvas is 1080 high; its width follows the screen (1663 at 1512x982, 1920 at 16:9). */
    static void *msw, *msh; if (!msw) { msw = mth("UnityEngine", "Screen", "get_width", 0); msh = mth("UnityEngine", "Screen", "get_height", 0); }
    int sw = msw ? MCALL(msw, int, const void *)(msw) : 0, shh = msh ? MCALL(msh, int, const void *)(msh) : 0;
    float hw = sw > 0 && shh > 0 ? 540.0f * sw / shh : 831;
    /* inside the screen (a place dragged on a wider window, or off the edge): ch_geo. bx / by = the box's centre */
    ch_rect g = ch_geo(hw);
    float bx = g.l + g.w / 2, by = g.b + g.h / 2, bw = g.w - CH_LINE_DW, iw = g.w - 60, pw = g.w - 40;   /* box lines / idle text / idle plates */
    /* heights measured at another width (the box was resized): measured again when shown */
    static float ch_hb_w = CH_W0 - CH_LINE_DW, ch_hi_w = CH_W0 - 60;
    if (bw != ch_hb_w) { for (int i = 0; i < nch; i++) chist[i].hb = 0; ch_hb_w = bw; }
    if (iw != ch_hi_w) { for (int i = 0; i < nch; i++) chist[i].hi = 0; ch_hi_w = iw; }
    ch_line_w[0] = iw; ch_line_w[1] = bw;
    Obj box = u_gc_get(h_ch_box);
    nchat_shown = 0;
    if (box) {
        u_active(box, box_open);
        place(box, bx, by, g.w, g.h);
        if (g.w != ch_lay_w || g.h != ch_lay_h || ro != ch_lay_ro) ch_layout(g.w, g.h, ro);
        if (box_open) {
            /* A1 / A2: the newest lines that fit whole between the title band (top 95) and the input line (bottom -85; read
               only: -107), each as high as its wrapped text, newest at the bottom; placed from the top down (owner 2026-10-21:
               no empty band above the rows; what is left over stays under the newest line). One line taller than the space:
               from the bottom. */
            float top = g.h / 2 - 40, bot = ro ? -g.h / 2 + 28 : -g.h / 2 + 50;
            float used = 0, avail = top - bot, hs[CH_BOX_LINES];
            int S = nch - ch_top_count(bw, avail); if (S < 0) S = 0;               /* the scroll that shows the oldest line at the top */
            if (ch_want_top) { chat_scroll = S; ch_want_top = 0; rig_log("mpmenu: chat: Top (scroll %d of %d lines)", S, nch); }
            if (chat_scroll > S) chat_scroll = S;
            int j = 0, k = nch - 1 - chat_scroll; ch_box_oldest = 0;
            for (; j < CH_BOX_LINES && k >= 0; j++, k--) {
                Obj l = u_gc_get(h_ch_lines[j]); if (!l) break;
                ch_prepare(k);
                ch_slot_show(l, &ch_box_id[j], &ch_box_font[j], k);
                if (chist[k].hb <= 0) { place(l, CH_LINE_X, 0, bw, 20); chist[k].hb = ch_measure(l, bw); }
                float h = chist[k].hb;
                if (used + h > avail && j > 0) break;                         /* the next older line does not fit whole */
                u_active(l, true); hs[j] = h; used += h + 2;
                if (nchat_shown < CH_IDLE + CH_BOX_LINES) { chat_shown_k[nchat_shown] = k; chat_shown_h[nchat_shown++] = &h_ch_lines[j]; }
            }
            { float y = used - 2 > avail ? bot + used - 2 : top;              /* top of the oldest shown line */
              for (int i = j - 1; i >= 0; i--) { place(u_gc_get(h_ch_lines[i]), CH_LINE_X, y - hs[i] / 2, bw, hs[i]); y -= hs[i] + 2; } }
            if (k < 0) ch_box_oldest = 1;
            for (int i = j; i < CH_BOX_LINES; i++) { Obj l = u_gc_get(h_ch_lines[i]); if (l) u_active(l, false); }
            /* the scrollbar: the thumb is as long as the shown part of the history, at its place (the newest at the bottom) */
            { float hh = g.h / 2, t1 = CH_HIST_BOT(hh, ro) + 20, L = (CH_HIST_TOP(hh) - 20) - t1; if (L < 4) L = 4;
              float T = S > 0 && j > 0 ? L * j / (float)(j + S) : L; if (T < 16) T = 16; if (T > L) T = L;
              float off = S > 0 ? (L - T) * (float)chat_scroll / (float)S : 0;
              Obj th = u_gc_get(h_ch_lay[CL_THUMB]); if (th) place(th, g.w / 2 - 12, t1 + off + T / 2, 6, T);
              Obj hi = u_gc_get(h_ch_thumbhi); if (hi) place(hi, 0, T / 2 - 1, 4, 2);
              ch_sb.t1 = t1; ch_sb.L = L; ch_sb.T = T; ch_sb.S = S; ch_sb.vis = j; }
            if (chat_typing && !u_alive(u_gc_get(h_ch_field))) {            /* no field: draw the text + a caret ourselves */
                char t[DSNET_CHAT_MAX + 64], e[DSNET_CHAT_MAX * 2 + 8]; chat_esc(chat_buf, e, sizeof e);
                snprintf(t, sizeof t, "%s%s", e, (frames / 15) % 2 ? "|" : " ");
                tmp_set(u_gc_get(h_ch_input), t);
            }
        }
    }
    /* "<Name> is typing..." for the others (owner 2026-10-14): one line in their colours; it takes the lowest idle slot
     * (the idle lines move up one) or sits above the open box */
    int me = c ? dsnet_player_id(c) : -1; float lift = 0;
    { char line[200] = ""; int k = 0, more = 0; size_t o = 0;
      for (int p = 0; c && p < DSNET_MAX_PLAYERS; p++) {                   /* no session: nobody else types */
          if (p == me || !dsnet_chat_typing_of(c, p)) continue;
          const char *nm = dsnet_player_name(c, p);
          static char snm[32]; if (!nm && dsnet_spectator_info(c, p, snm, NULL, NULL) == 0) nm = snm;   /* a spectator types too */
          if (!nm) continue;
          if (k == 3) { more = 1; break; }
          o += (size_t)snprintf(line + o, o < sizeof line ? sizeof line - o : 0, "%s<color=%s>%s</color>", k ? TR(", ") : "", ui_player_color(p), nm); k++;
      }
      if (k) { char who[200]; snprintf(who, sizeof who, "%s", line);
               snprintf(line, sizeof line, more ? TR("%s and others are typing...") : TRN("%s is typing...", "%s are typing...", k), who); }
      Obj t = u_gc_get(h_ch_typing), bg = u_gc_get(h_ch_typingbg);
      u_active(t, k > 0); u_active(bg, k > 0);
      snprintf(typing_shown, sizeof typing_shown, "%s", k ? line : "");
      if (k) {
          static char shown[200]; static float th = 18, tw;
          if (strcmp(shown, line) || ch_typing_font < 0 || tw != iw) { snprintf(shown, sizeof shown, "%s", line); char tmp[CH_TEXT]; snprintf(tmp, sizeof tmp, "%s", line);
              int fi = ch_pick(tmp, 1, NULL); if (fi >= 0 && fi != ch_typing_font) { ch_font_set(t, fi, 0); ch_typing_font = fi; }
              tmp_set(t, tmp); place(t, bx, 0, iw, 22); th = ch_measure(t, iw); tw = iw; }
          float ph = th + 6 > 22 ? th + 6 : 22;
          float y = box_open ? g.b + g.h + 4 + ph / 2 : g.b + ph / 2;
          place(t, bx, y, iw, th); place(bg, bx, y, pw, ph);
          if (!box_open) lift = ph + 4;
      }
    }
    /* idle lines: the newest at the bottom, each plate as high as its wrapped text (A1 / A2), fading after chat_fade_s */
    double now = mono_s();
    { float y = g.b + lift;                                                 /* bottom of the next plate */
      for (int i = CH_IDLE - 1; i >= 0; i--) {
        int k = nch - CH_IDLE + i; Obj t = u_gc_get(h_ch_idle[i]), bg = u_gc_get(h_ch_idlebg[i]);
        float a = 0;
        if (!box_open && k >= 0 && k < nch) { double age = now - chist[k].t; a = age < chat_fade_s ? 1 : age < chat_fade_s + 1 ? (float)(chat_fade_s + 1 - age) : 0; }
        u_active(t, a > 0); u_active(bg, a > 0);
        if (a > 0) {
            /* a local line (mp_api v3, e.g. a DPS log line every hit) never takes a click: the game's buttons under the idle
               lines stay usable (the unit panel's Inspect button sat under them); other lines stay UI clicks (lan_v10) */
            if (bg && mm_is(bg, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bg, !chist[k].local || chist[k].link, NULL);
            ch_prepare(k);
            ch_slot_show(t, &ch_idle_id[i], &ch_idle_font[i], k);
            if (chist[k].hi <= 0) { place(t, bx, 0, iw, 26); chist[k].hi = ch_measure(t, iw); }
            float th = chist[k].hi, ph = th + 6 > 26 ? th + 6 : 26;
            place(t, bx, y + ph / 2, iw, th); place(bg, bx, y + ph / 2, pw, ph);
            y += ph + 4;
            if (nchat_shown < CH_IDLE + CH_BOX_LINES) { chat_shown_k[nchat_shown] = k; chat_shown_h[nchat_shown++] = &h_ch_idle[i]; }
            if (mm_is(t, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(t, (Col){ C_MAIN.r, C_MAIN.g, C_MAIN.b, a }, NULL);
            if (mm_is(bg, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(bg, (Col){ 0.133f, 0.125f, 0.145f, 0.7f * a }, NULL);
        }
      } }
    ch_over_now = mpmenu_chat_over();                                       /* A4: Input.mouseScrollDelta reads 0 for the game while set */
    /* the pointer this frame: the real mouse (Input icalls), or the test's scripted one (ch_fk: rig control channel only):
       2 frames resting at (x0, y0), the press, n frames moving to (x1, y1) with the wheel at `wheel`, the release */
    V3 mp = { 0, 0, 0 }; int held[2] = { 0, 0 }; float wy = 0, br[4] = { 0 };
    int have_ptr = ch_fk.on || (mb && mpos);
    if (ch_fk.on) {
        int i = ch_fk.i++, n = ch_fk.n;
        float t = i <= 2 ? 0 : i >= 3 + n ? 1 : (float)(i - 2) / n;
        mp.x = ch_fk.x0 + (ch_fk.x1 - ch_fk.x0) * t; mp.y = ch_fk.y0 + (ch_fk.y1 - ch_fk.y0) * t;
        if (ch_fk.button >= 0) held[ch_fk.button] = i >= 2 && i < 3 + n;
        if (i >= 3 && i < 3 + n) wy = ch_fk.wheel;
        if (i >= 3 + n) ch_fk.on = 0;
    } else if (have_ptr) { mpos(&mp); held[0] = mb(0); held[1] = mb(1); wy = ch_wheel_y(wheel); }
    int down0 = held[0] && !ch_held[0], down1 = held[1] && !ch_held[1], consumed = 0;
    ch_held[0] = held[0]; ch_held[1] = held[1];
    if (box) rect_of(box, br);                                              /* placed every frame, shown or not */
    float sc = br[2] > br[0] ? (br[2] - br[0]) / g.w : 0;                   /* screen px per canvas unit */
    int over_box = box_open && u_on(box) && mp.x >= br[0] && mp.x <= br[2] && mp.y >= br[1] && mp.y <= br[3], over_idle = 0;
    if (!box_open) for (int i = 0; i < CH_IDLE; i++) over_idle |= pt_in(u_gc_get(h_ch_idlebg[i]), mp);
    /* linked lines (pings): hover = tooltip, left click = ui_chat_link_click */
    for (int i = 0; have_ptr && i < nchat_shown; i++) {
        int k = chat_shown_k[i]; float r[4]; Obj t = u_gc_get(*chat_shown_h[i]);
        if (!chist[k].link || !u_on(t) || !rect_of(t, r) || mp.x < r[0] || mp.x > r[2] || mp.y < r[1] || mp.y > r[3]) continue;
        if (tip_ready()) game_tip_show(TR("Ping"), TR("Click to move your camera there."));
        if (down0) {
            static int (*fn)(int); static int looked2; if (!looked2) { looked2 = 1; fn = (int (*)(int))dlsym(RTLD_DEFAULT, "ui_chat_link_click"); }
            int acted = fn ? fn(chist[k].link) : 0; chat_link_clicks++; consumed = 1;
            rig_log("mpmenu: chat link %d clicked: %s", chist[k].link, acted ? "camera moved" : "ping gone (plain line now)");
            if (!acted) chist[k].link = 0;
        }
        break;
    }
    /* the box's buttons: hover = the icon in the main colour + the game's tooltip; a left press acts */
    int hit = box_open && have_ptr && !ch_drag ? ch_hit(mp) : CHB_NONE;
    ch_icon_hover(hit == CHB_TOP ? 0 : hit == CHB_BOT ? 1 : hit == CHB_BIN ? 2 : hit == CHB_CLOSE ? 3 : -1);
    if (tip_ready()) {
        if (hit == CHB_TOP) game_tip_show(TR("Oldest Messages"), TR("Jump to the top of the chat history."));
        else if (hit == CHB_BOT) game_tip_show(TR("Newest Messages"), TR("Jump to the newest message."));
        else if (hit == CHB_BIN) game_tip_show(TR("Clear Chat"), ro ? TR("Removes the messages on this screen only.")
                                                                    : TR("Removes the messages on this screen only. The other players keep theirs. You can also type /clear."));
    }
    if (!consumed && down0 && hit) {
        consumed = 1;
        switch (hit) {
        case CHB_CLOSE: chat_box_close("its X"); break;
        case CHB_TOP: ch_want_top = 1; break;
        case CHB_BOT: chat_scroll = 0; rig_log("mpmenu: chat: Bottom"); break;
        case CHB_BIN: chat_clear_view("the bin"); break;
        case CHB_THUMB: ch_drag = CHD_THUMB; ch_drag_btn = 0; ch_d0 = mp; ch_s0 = chat_scroll; ch_dsc = sc; break;
        case CHB_TRACK: {                                                   /* a page toward the press */
            Obj th = u_gc_get(h_ch_lay[CL_THUMB]); float q[4] = { 0 }; rect_of(th, q);
            int page = ch_sb.vis > 1 ? ch_sb.vis - 1 : 1;
            if (mp.y > q[3]) chat_scroll = chat_scroll + page > ch_sb.S ? ch_sb.S : chat_scroll + page;
            else if (mp.y < q[1]) chat_scroll = chat_scroll > page ? chat_scroll - page : 0;
            break; }
        }
    }
    /* the wheel: scrolls the open box (typing: anywhere, as before; reading: over the box); over the chat lines it opens the
       box for reading (owner 2026-10-25: "scroll with the wheel" without typing) */
    if (wy != 0 && !ch_drag) {
        if (box_open && (chat_typing || over_box)) { if (wy > 0 && !ch_box_oldest) chat_scroll++; else if (wy < 0 && chat_scroll > 0) chat_scroll--; }
        else if (!box_open && over_idle && nch) { chat_read = 1; chat_scroll = wy > 0 && nch > 1 ? 1 : 0; rig_log("mpmenu: chat: the wheel over the chat lines opens it for reading"); }
    }
    /* move / resize (owner 2026-10-25), remembered in mp_menu.json at the release:
       - right-click + drag on the open box or the newest idle line: move (as before);
       - left drag on the open box's title band or frame: move; on its bottom-right corner (the grip): width + height; on its
         bottom edge: height. The top-left corner stays where it is while resizing.
       A drag starts only with a press ON the box or its two clear strips: they are UI graphics (raycast targets), so the
       game's IsPointerOverUI sees the press and never turns it into a world click. The same in the read state. */
    if (have_ptr && box) {
        if (!ch_drag && sc > 0 && !consumed) {
            int kind = 0, btn = 0;
            float ux = (mp.x - br[0]) / sc, uy = (mp.y - br[1]) / sc;         /* canvas units from the box's bottom-left */
            if (down0 && box_open && u_on(box)) {
                int in = ux >= 0 && ux <= g.w && uy >= 0 && uy <= g.h;
                if (in || pt_in(u_gc_get(h_ch_lay[CL_STRIPB]), mp) || pt_in(u_gc_get(h_ch_lay[CL_STRIPR]), mp)) {
                    if (ux >= g.w - 15 && uy <= 16) kind = CHD_SIZE;        /* the grip corner (right of / under the field) */
                    else if (uy <= 8) kind = CHD_HEIGHT;                    /* the bottom edge: under the field */
                    else if (uy >= g.h - 31 || ux <= 3 || ux >= g.w - 3) kind = CHD_MOVE;   /* the title band, the frame */
                    else if (chat_typing && uy > (ro ? 24 : 46) && ux < g.w - 19) {   /* a click on the history: back to reading */
                        chat_read = 1; chat_end_typing(c, 0); consumed = 1;
                        rig_log("mpmenu: chat: a click on the history: reading (the game has its keys, the draft stays)");
                    }
                }
            } else if (down1) {
                float q[4] = { 0 }; int ok = 0;
                if (box_open) ok = u_on(box) && rect_of(box, q); else { Obj t0 = u_gc_get(h_ch_idlebg[CH_IDLE - 1]); ok = u_on(t0) && rect_of(t0, q); }
                if (ok && mp.x >= q[0] && mp.x <= q[2] && mp.y >= q[1] && mp.y <= q[3]) { kind = CHD_MOVE; btn = 1; }
            } else if (down0 && !box_open) {                                /* a left click on a chat line (not a local one: those let clicks through) */
                for (int i = 0; i < CH_IDLE; i++) { int k = nch - CH_IDLE + i;
                    if (k >= 0 && k < nch && !chist[k].local && !chist[k].link && pt_in(u_gc_get(h_ch_idlebg[i]), mp)) {
                        chat_read = 1; chat_scroll = 0; rig_log("mpmenu: chat: a click on a chat line opens it for reading"); break; } }
            }
            if (kind) { ch_drag = kind; ch_drag_btn = btn; ch_d0 = mp; ch_g0 = g; ch_dsc = sc;
                        rig_log("mpmenu: chat %s started (%s button)", kind == CHD_MOVE ? "move" : kind == CHD_SIZE ? "resize" : "height", btn ? "right" : "left"); }
        } else if (ch_drag == CHD_THUMB) {
            if (held[0] && ch_sb.S > 0 && ch_sb.L > ch_sb.T) {               /* the thumb follows the pointer: up = older lines */
                float q = (mp.y - ch_d0.y) / ch_dsc / (ch_sb.L - ch_sb.T) * ch_sb.S; int s = ch_s0 + (int)(q >= 0 ? q + 0.5f : q - 0.5f);   /* no libm here */
                chat_scroll = s < 0 ? 0 : s > ch_sb.S ? ch_sb.S : s;
            } else if (!held[0]) { ch_drags[CHD_THUMB]++; ch_drag = 0; rig_log("mpmenu: chat: thumb released at scroll %d of %d", chat_scroll, ch_sb.S); }
        } else if (ch_drag && held[ch_drag_btn]) {
            float dx = (mp.x - ch_d0.x) / ch_dsc, dy = (mp.y - ch_d0.y) / ch_dsc, top = ch_g0.b + ch_g0.h;
            ch_rect n = ch_g0;
            if (ch_drag == CHD_MOVE) { n.l += dx; n.b += dy; n = ch_clamp(n, hw); }
            else {                                                          /* the top-left corner stays; never past the screen */
                if (ch_drag == CHD_SIZE) { float mw = ch_max_w(hw), room = hw - 4 - n.l; n.w = ch_g0.w + dx;
                    if (n.w > mw) n.w = mw; if (n.w > room) n.w = room; if (n.w < CH_W0) n.w = CH_W0; }
                float roomh = top - (-540 + 4); n.h = ch_g0.h - dy;
                if (n.h > CH_MAX_H) n.h = CH_MAX_H; if (n.h > roomh) n.h = roomh; if (n.h < CH_H0) n.h = CH_H0;
                n.b = top - n.h;
            }
            chat_x = n.l - (-hw + 130); chat_y = n.b - (-540 + 130); chat_w = n.w; chat_h = n.h;
        } else if (ch_drag) {
            ch_drags[ch_drag]++; prefs_save();
            rig_log("mpmenu: chat %s to %.0f,%.0f size %.0f x %.0f", ch_drag == CHD_MOVE ? "moved" : "resized", chat_x, chat_y, chat_w, chat_h);
            ch_drag = 0;
        }
    }
    /* Escape (owner rule 2026-10-25): the read state is a window on the kit's stack (Escape = its X); while typing the field
       has the key (Escape leaves the field first) */
    { Obj bx2 = u_gc_get(h_ch_box); mpesc_sync(&esc_chat, bx2, chat_read && !chat_typing && !chat_close_frames && u_on(bx2), 0); }
}

/* a register_host_choice2 choice with draw_page: called each frame while its host page shows (panel = the empty RectTransform
   in the box beside the column), draw_page(NULL) once when that page closes (Back, Start -> loading, another page) */
static int cp_drawn;                                     /* 1 + the choice whose draw_page got the panel last frame; 0 none */
static unsigned cp_calls, cp_null_calls;                 /* test read-back */
static void choice_page_frame(void) {
    mpm_choice *hc = M.choice > 0 && M.choice <= hcl.n ? &hcl.c[M.choice - 1] : NULL;
    Obj root = u_gc_get(h_cp_root), pnl = u_gc_get(h_cp_panel);
    int on = M.state == MPM_TITLE_HOST && hc && hc->v2 && hc->draw_page && u_alive(root) && u_alive(pnl);
    if (cp_drawn && cp_drawn != (on ? M.choice : 0)) {
        mpm_choice *d = cp_drawn <= hcl.n ? &hcl.c[cp_drawn - 1] : NULL;
        if (d && d->draw_page) { d->draw_page(NULL); cp_null_calls++; }
        cp_drawn = 0;
    }
    if (u_alive(root)) u_active(root, on);
    if (on) { hc->draw_page(pnl); cp_drawn = M.choice; cp_calls++; }
}

/* ---------------------------------------------------------------- actions */
void ui_notice(const char *text) __attribute__((weak_import));   /* ui_game.c */
static void run_actions(int a, const mpm_obs *o) {
    for (int k = 0; k < nhooks; k++) if (hooks[k].action) hooks[k].action(a);   /* Custom Modes (FE: MPA_FE_BRING, MPA_FE_RESULTS) */
    Obj tv = title_view();
    if (a & (MPA_OPEN_LOAD_LIST | MPA_OPEN_NEW)) host_failed = 0;
    if ((a & MPA_OPEN_LOAD_LIST) && tv) (mm_is(tv, K_TMV) ? F(0xE710B8, void (*)(Obj, void *))(tv, NULL) : (void)0);   /* TitleMenuView.OnLoadCampaignPressed */
    if ((a & MPA_OPEN_NEW) && tv) (mm_is(tv, K_TMV) ? F(0xE71020, void (*)(Obj, void *))(tv, NULL) : (void)0);        /* TitleMenuView.OnNewGamePressed */
    if ((a & MPA_HOST_START) && o->role == 1) rig_log("mpmenu: a host session already runs (mp.json?): using it");
    else if (a & MPA_HOST_START) {
        mp_steam_info in; mp_steam_state(&in);
        char gid[64] = ""; mpm_settings ws;
        if (campaign_guid(gid, sizeof gid) && world_get(gid, &ws)) {
            /* the World choice (Your save / Foreign Expedition) and the expedition rules come from the Host page, never
               from the world's stored line (dive 2026-10-15: a world played before in normal mode turned FE off) */
            mpm_settings pg = M.set; M.set = ws;
            M.set.fe = pg.fe; M.set.fe_death = pg.fe_death; M.set.fe_xp = pg.fe_xp; M.set.fe_loot = pg.fe_loot; M.set.fe_recruit = pg.fe_recruit;
            rig_log("mpmenu: world %s: its stored settings (World %s kept from the Host page)", gid, pg.fe ? "Foreign Expedition" : "Your save");
        }
        { char nm[64]; field_text(h_namefield, nm, sizeof nm); if (nm[0]) snprintf(my_name, sizeof my_name, "%s", nm); }   /* name typed on the Host page */
        mp_config c; mp_config_defaults(&c);
        char p[1200]; snprintf(p, sizeof p, "%s/mp.json", rig_dir);
        mp_config f; if (mp_config_load(p, &f) == 0) { snprintf(c.name, sizeof c.name, "%s", f.name); c.port = f.port; }
        c.host = 1; c.allow_speedup = M.set.speedup; c.allow_pause = M.set.pause_mode != MPM_PAUSE_NONE;
        c.pause_mode = M.set.pause_mode; c.gear_mode = M.set.gear; c.port = mpm_host_port();
        c.allow_spectators = M.set.spectators;
        if (my_name[0]) snprintf(c.name, sizeof c.name, "%s", my_name);
        else if (!in.steam) default_name(c.name, sizeof c.name);
        int rc = in.steam ? mp_steam_host("", &c)                           /* the campaign just loaded; TCP listens too */
                          : mp_game_host(&c);                               /* no Steam: direct TCP only */
        if (rc == -2 && !in.steam) { c.port = 0; rc = mp_game_host(&c); }   /* port taken: any free port */
        host_failed = rc != 0;
        if (!rc) { prefs_save(); if (gid[0]) world_put(gid, &M.set); lobby_hold(1); rules_send("host start"); }
        rig_log("mpmenu: host start (%s) rc %d (pause %d speed-up %d gear %d)", in.steam ? "Steam" : "TCP", rc, M.set.pause_mode, M.set.speedup, M.set.gear);
    }
    if (a & MPA_JOIN_ADDR) join_by_address();
    if ((a & MPA_JOIN_CANCEL) && conn) { dsnet_connect_free(conn); conn = NULL; rig_log("mpmenu: connect cancelled"); }
    if (a & MPA_FE_START) {                                                 /* build the hub with the Expedition Rules, then start */
        char err[512] = "";
        mpm_choice *hc = cur_choice();                                       /* the old-API choice this session was hosted from */
        for (int k = 0; (!hc || !hc->on_start) && k < hcl.n; k++) if (!hcl.c[k].v2 && hcl.c[k].on_start) hc = &hcl.c[k];   /* else the first one that can */
        int rc = hc && hc->on_start ? hc->on_start(M.set.fe_death, M.set.fe_xp, M.set.fe_loot, M.set.fe_recruit, err, sizeof err) : -1;   /* the registered start (Custom Modes: Foreign Expedition); 0 = started */
        if (!hc || !hc->on_start) snprintf(err, sizeof err, "%s", TR("No mod offers this choice."));
        rig_log("mpmenu: Foreign Expedition start rc %d %s", rc, err);
        if (rc == 0) { M.state = MPM_PLAYING; run_actions(MPA_LOBBY_HIDE | MPA_START_GAME, o); }
        else if (ui_notice) ui_notice(err);
    }
    if (a & MPA_CHOICE_PAGE) {                                              /* host page Start of a register_host_choice2 choice */
        char err[512] = ""; mpm_choice *hc = cur_choice();
        int rc = hc && hc->page_start ? hc->page_start(err, sizeof err) : -1;
        if (!hc || !hc->page_start) snprintf(err, sizeof err, "%s", TR("No mod offers this choice."));
        rig_log("mpmenu: host choice \"%s\" page start rc %d %s", hc ? hc->id : "?", rc, err);
        { char nm[64]; field_text(h_namefield, nm, sizeof nm); if (nm[0]) snprintf(my_name, sizeof my_name, "%s", nm); }   /* name typed on the page */
        mpm_page_started(&M, rc, err);                                        /* 0: wait for the load like Your save (HOST_LOADING -> LOBBY) */
    }
    if (a & MPA_CHOICE_LOBBY) {                                             /* lobby Start of a session hosted from such a choice */
        char err[512] = ""; mpm_choice *hc = cur_choice();
        int rc = hc && hc->lobby_start ? hc->lobby_start(err, sizeof err) : -1;
        if (!hc || !hc->lobby_start) snprintf(err, sizeof err, "%s", TR("No mod offers this choice."));
        rig_log("mpmenu: host choice \"%s\" lobby start rc %d %s", hc ? hc->id : "?", rc, err);
        if (rc == 0) { M.state = MPM_PLAYING; run_actions(MPA_LOBBY_HIDE | MPA_START_GAME, o); }
        else {                                                              /* UX pass: in the lobby's own hint line (the top alert sat over the window's edge) */
            snprintf(lobby_err, sizeof lobby_err, "%s", err[0] ? err : TR("This mode could not start.")); lobby_err_until = mono_s() + 10;
            if (!u_alive(lwt(T_HINT)) && ui_notice) ui_notice(lobby_err);
        }
    }
    if (a & MPA_START_GAME) lobby_hold(0);
    if (a & MPA_RULES) rules_send("lobby");
    if (a & MPA_SPECTATORS) mpmenu_spectators_set(!M.set.spectators);       /* lobby Rules arrow (off: the box when someone watches) */
    if (a & MPA_ADMIN) {                                                    /* lobby window: host admin mode on / off */
        mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
        uint8_t b = c && !dsnet_admin_mode(c);
        int rc = c ? dsnet_submit(c, DSNET_ADMIN_MODE, 0, &b, 1) : -1;
        M.set.admin = b; rules_sent_frame = frames;
        char gid[64] = ""; prefs_save(); if (campaign_guid(gid, sizeof gid)) world_put(gid, &M.set);
        rig_log("mpmenu: admin mode -> %d (rc %d)", b, rc);
    }
    if (a & MPA_COPY_ADDR) { copied_until = frames + 60; const char *ca = lobby_addr[0] ? lobby_addr : M.direct; F(0x518AFE0, void (*)(Obj, void *))(u_str(ca), NULL); rig_log("mpmenu: copied the lobby address"); }
    if (a & MPA_INVITE) {                                                   /* F18: a failed overlay is said, not only logged */
        int rc = ui_friends_invite_click() ? 0 : mp_steam_invite(); rig_log("mpmenu: invite rc %d", rc);   /* steaminv: overlay off = friends picker (ui_friends.c) */
        if (rc) say("%s", TR("The Steam invite window did not open (Steam overlay off?). Use Copy direct address instead."));
    }
    if (a & MPA_CONFIRM_LEAVE) leave_box_show();
    if (a & MPA_LEAVE_BOX_CLOSE) leave_box_close();
    if (a & MPA_LEAVE) {                                                    /* guest: straight to the title (mp_game) */
        int quit = (a & MPA_QUIT_TITLE) != 0;                               /* lobby Quit: everyone (the host too) goes to the title */
        int host_alone = o->role == 1 && o->scene == 2 && !quit;            /* the host stays in the world, single player */
        hist_update();                                                      /* Recent worlds: the state at leaving */
        static void (*leave_title)(void), (*quit_title)(void); static int looked;
        if (!looked) { looked = 1; leave_title = (void (*)(void))dlsym(RTLD_DEFAULT, "mp_game_leave_to_title"); quit_title = (void (*)(void))dlsym(RTLD_DEFAULT, "mp_game_quit_to_title"); }
        if (quit && quit_title) quit_title(); else if (leave_title) leave_title(); else mp_steam_leave();
        host_failed = 0; rig_log("mpmenu: left the session%s", quit ? " (Quit: to the title)" : "");
        if (host_alone) say("%s", TR("Multiplayer ended. You are playing alone now; the world stays on your computer."));
    }
    if (a & MPA_FRIENDS) friends_refresh();
    if (a & MPA_HIST_JOIN) hist_join(M.hist_pick);
    if (a & MPA_HIST_DEL) hist_del(M.hist_pick);
    if (a & MPA_JOIN_FRIEND) {
        expect_world("");
        int k = M.friend_pick, rc = k >= 0 && k < M.nfriends ? mp_steam_join(friend_lobby[k]) : -1;
        if (!rc) { snprintf(joined_how, sizeof joined_how, "steam"); joined_steamid = friend_id[k]; }
        if (rc) snprintf(M.msg, sizeof M.msg, TR("Could not join %s's world (Steam). Try again, or use Join by address."), k >= 0 && k < 6 ? M.friends[k] : TR("a friend"));
        rig_log("mpmenu: join friend %d (lobby %llu): rc %d", k, (unsigned long long)(k >= 0 && k < 6 ? friend_lobby[k] : 0), rc);
    }
    if (a & MPA_LOBBY_SHOW) { popup_activate(1); rig_log("mpmenu: lobby panel opened"); }
    if (a & MPA_LOBBY_HIDE) {
        Obj ev = esc_view(); Obj back = ev ? AT(ev, 0x48, Obj) : NULL;
        if (ev && u_on(ev)) (mm_is(ev, K_SMV) ? F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL) : (void)0);  /* SystemMenuView.OnReturnToGamePressed */
        (void)back; esc_lobby_mode = -1;
    }
    (void)o;
}

/* every element of ours with a tooltip, topmost first (research/tooltips_v9.md) */
static void tips_frame(const mpm_obs *o) {
    game_tip_frame();                                                        /* auto-hide (harmless if ui_frame also calls it) */
    tip_hit = 0; tip_now[0] = 0;
    if (!test_hover[0] && !tip_ready()) return;
    #define TIP(obj, t, b) tip_try((obj), (t), (b))
    /* chat box (3.13): no tooltip. The box is open only while its field has the focus, and a tooltip there covered the
       text being typed (lead 2026-10-14); its header already says "Right-click and drag to move". */
    /* Rules panels (3.3) */
    rpanel *rps[2] = { &rp_lobby, &rp_title };
    for (int k = 0; k < 2; k++) if (u_on_tree(u_gc_get(rps[k]->root))) {
        rp_tooltip(rps[k]);
        TIP(k ? tb(MPB_RULES_DONE) : u_gc_get(h_lwr[MPB_RULES_DONE]), TR("Done"), TR("Closes the Rules."));
    }
    if (u_on_tree(u_gc_get(rp_fe.root))) {                                  /* Foreign Expedition rules (board FESettingsA) */
        rp_fe.tip_row = -1;
        for (int i = 0; i < MPM_FE_ROWS; i++) if (TIP(u_gc_get(rp_fe.row[i]), mpm_fe_label(i), mpm_fe_tip(i))) { rp_fe.tip_row = i; break; }
        TIP(tb(MPB_FE_DONE), TR("Done"), TR("Closes the rules."));
    }
    int lobby_rules = M.state == MPM_LOBBY && M.lobby_settings_open, host = M.state == MPM_LOBBY;
    /* lobby window (3.5, 3.7) */
    if (M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY) {
        TIP(u_gc_get(h_lwb[LW_START]), mpm_word("TEXTKEY_Common_Start", TR_NOOP("Start")), TR("Starts the game for everyone in the lobby. Players who join later catch up with the running game."));
        TIP(u_gc_get(h_lwb[LW_QUIT]), TR("Quit"), M.state == MPM_LOBBY ? TR("Asks first, then ends the game for everyone and goes back to the title. The world stays on your computer.")
                                                                        : TR("You leave and return to the title; your units stay reserved for you."));
        TIP(u_gc_get(h_lwb[LW_LEAVE]), TR("Turn Off Multiplayer"), TR("Asks first, then ends the game for everyone. You stay in this world and keep playing alone."));
        if (!lobby_rules) {
            TIP(u_gc_get(h_lwb[LW_GEAR]), TR("Rules"), TR("Shows how this world plays. Only the host can change them."));
            TIP(lwt(T_RULES), TR("Rules"), TR("Shows how this world plays. Only the host can change them."));
            TIP(u_gc_get(h_lwb[LW_INVITE]), TR("Steam Invite"), TR("Opens Steam's invite window so you can invite friends without sharing your address. Needs the Steam version."));
            TIP(u_gc_get(h_lwb[LW_COPY]), TR("Copy Address"), TR("Copies your address so you can send it to a friend. They paste it in Join by address."));
            TIP(u_gc_get(h_lw_addrbox[0]), TR("Copy Address"), TR("Copies your address so you can send it to a friend. They paste it in Join by address."));
            TIP(lwt(T_PLAYERS), TR("Players"), TR("Everyone in this world. The bars show each player's connection."));
            for (int i = 0; i < LW_ROWS; i++) {
                static const char *lv[4] = { TR_NOOP("Disconnected. The game waits, or the host keeps playing their units."), TR_NOOP("Slow connection, or the game is loading or catching up."),
                                             TR_NOOP("Slow connection, or catching up with the game."), TR_NOOP("Good connection. In sync.") };
                if (lw_level[i] >= 0 && lw_level[i] <= 3) for (int k = 0; k < 3; k++) TIP(u_gc_get(h_lw_bar[i][k]), lw_rowname[i], TR(lv[lw_level[i]]));
                if (lw_rowname[i][0]) TIP(u_gc_get(h_lw_rowbg[i]), lw_rowname[i], TR("The colour is this player's colour everywhere: names above units, portraits, Teams and chat."));
            }
        }
    }
    (void)host;
    /* Esc menu (3.14) */
    Obj ev = o->scene == 2 ? esc_view() : NULL;
    if (ev && u_on(ev) && M.state != MPM_LOBBY && M.state != MPM_GUEST_LOBBY) {
        TIP(eb(MPB_ESC_INVITE), TR("Steam Invite"), TR("Opens Steam's invite window so you can invite friends into this world."));
        TIP(eb(MPB_LEAVE), TR("Leave Multiplayer"), TR("Host: asks first, then ends the game for everyone; the world stays on your computer and you keep playing alone. Guest: you return to the title; your units stay reserved for you."));
        if (in_session()) { char lc[128]; snprintf(lc, sizeof lc, "%s", load_label[0] ? load_label : mpm_word("TEXTKEY_SystemMenu_LoadCampaign", TR_NOOP("Load Campaign")));   /* the stock button's own label */
                            TIP(AT(ev, 0x60, Obj), lc, TR("Leave multiplayer to load another world.")); }
    }
    if (o->scene != 1) return;
    /* Recent worlds (3.1) */
    for (int i = 0; i < 6; i++) TIP(tb(MPB_HISTW0 + i), TR("Watch"), TR("Watch the game: you see everything and can chat and ping, but you own no units and cannot pause or vote."));   /* before its row's own tip */
    for (int i = 0; i < 6; i++) {
        TIP(tb(MPB_HISTX0 + i), TR("Remove from List"), TR("Removes this world from Recent Worlds. The world itself and its save are not deleted."));
        TIP(u_gc_get(h_rw_ul[i]), TR("Your Units"), TR("The units you controlled in this world when you last left. They stay reserved for you until you come back."));
        TIP(tb(MPB_HIST0 + i), TR("Reconnect"), TR("Joins this world again the same way as last time (Steam or the IP address). Your units come back to you. A world you hosted opens the load list, to host it again."));
    }
    TIP(tb(MPB_HIST_PREV), TR("More Worlds"), TR("Shows the next page of Recent Worlds.")); TIP(tb(MPB_HIST_NEXT), TR("More Worlds"), TR("Shows the next page of Recent Worlds."));
    TIP(u_gc_get(h_rw_title), TR("Recent Worlds"), TR("Worlds you played in multiplayer, newest first, with the day, the players and your units."));
    /* title column (3.1, 3.2, 3.4) */
    /* Multiplayer: no tooltip (owner 2026-10-15) */
    TIP(tb(MPB_HOST_SAVE), TR("Your Save"), TR("Open one of your saves to other players. Friends join your world and play with your units. The world stays on your computer."));
    TIP(tb(MPB_HOST_NEW), TR("New Save"), TR("Start a new campaign and open it to other players as soon as it is created."));
    for (int k = 0; k < hcl.n; k++) TIP(tb(mpm_choice_btn(k)), hcl.c[k].label, hcl.c[k].tip);
    TIP(tb(MPB_HOST_START), mpm_word("TEXTKEY_Common_Start", TR_NOOP("Start")), TR("Prepares the world for this mode and opens the lobby. Friends join you there."));
    TIP(tb(MPB_JOIN_ADDR), TR("Join by Address"), TR("Join a friend's world with their address. Use it without Steam, or on the same home network."));
    TIP(tb(MPB_JOIN_FRIEND), TR("Join a Friend on Steam"), TR("Lists the Steam friends who are hosting a world right now. Click one to join. Needs the Steam version of the game."));
    for (int i = 0; i < 6; i++) { char t[256];                                 /* the friend's name from the model (not read out of the label) */
        if (i < M.nfriends && M.friends[i][0]) snprintf(t, sizeof t, TR("Join %s's World"), M.friends[i]); else snprintf(t, sizeof t, "%s", TR("Join a Friend's World"));
        dsi_utf8_fix_end(t);
        TIP(tb(MPB_FRIEND0 + i), t, TR("Joins this friend's world through Steam. The world loads from the host; your own saves are not touched.")); }
    TIP(tb(MPB_SETTINGS), TR("Rules"), TR("How this multiplayer world plays: pause, speed, gear, units and events. Saved with the world; new worlds start with your last rules."));
    TIP(tb(MPB_LOAD_SAVE), TR("Host from a Save"), TR("Pick one of your saves to open to other players. The world stays on your computer, and only you save it."));
    TIP(tb(MPB_NEW_CAMPAIGN), TR("Host a New Campaign"), TR("Start a new campaign and open it to other players as soon as it is created."));
    TIP(u_gc_get(h_namefield), TR("Your Name"), TR("The name other players see: in the player list, above your units and in chat."));
    TIP(u_gc_get(h_field), TR("Host Address"), TR("The address the host shares from their lobby, for example 192.168.1.20:47777. Outside your home network, the host must open the port on their router."));
    TIP(tb(MPB_CONNECT), TR("Connect"), TR("Connects to the host and loads their world. It stops trying after 8 seconds without an answer."));
    TIP(tb(MPB_WATCH), TR("Watch"), TR("Watch the game: you see everything and can chat and ping, but you own no units and cannot pause or vote."));
    TIP(tb(MPB_CANCEL_JOIN), mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")), M.state == MPM_GUEST_JOINING || M.state == MPM_GUEST_LOADING ? TR("Stops joining and returns to the title. Escape does the same.")
                                                                                                    : TR("Stops connecting. You stay on this page. Escape does the same."));
    TIP(tb(MPB_BACK), mpm_word("TEXTKEY_Common_Back", TR_NOOP("Back")), TR("Returns to the previous menu."));
    #undef TIP
}
/* per-player fog (mp_fog.c): follow the host rule and the owner table; then the module's own frame work */
static void fog_poll(void) {
    static int shared = -1; static uint64_t owners;
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    int sh = 1; uint64_t u = 0, it = 0; int nu = 0, ni = 0;
    if (c) { dsnet_rules r; dsnet_rules_get(c, &r); sh = r.shared_fog; dsnet_owner_hashes(c, &u, &it, &nu, &ni); }
    if (sh != shared) { shared = sh; mp_fog_set_shared(sh); }
    if (u != owners) { owners = u; mp_fog_owner_changed(); }
    mp_fog_frame();
}
/* ---------------------------------------------------------------- Multiplayer world tag (owner 2026-10-15, PLAN.md)
 * <app data folder>/mp_tags.txt (mpm_tag_*): a world gets the tag the first time another player is in it with us (host
 * and guests each tag their own copy), with the names of everyone who was. Shown on the load list's world row
 * (SubUI_SettlerNameCase.Init post-hook) and its save rows (h_slot_set). "Remove tag" on the row asks, then deletes the
 * line; it comes back only when a friend joins that world again. The game's save is never touched. */
void utf16_to_utf8(Obj s, char *out, size_t cap);
static void tag_path(char *p, size_t cap) { app_path(p, cap, "mp_tags.txt"); }
static void tag_seed(void) {                                                /* once, no file yet: worlds in Recent worlds with other players */
    static int done; if (done) return; done = 1;
    char p[1200]; tag_path(p, sizeof p);
    FILE *f = fopen(p, "r"); if (f) { fclose(f); return; }
    if (!hist_loaded) hist_load();
    f = fopen(p, "a"); if (f) fclose(f);                                    /* the file now exists: seeded once, even with no rows */
    int seeded = 0;
    for (int i = nhist - 1; i >= 0; i--) {                                  /* oldest first: names keep the order they came in */
        if (!hist[i].players[0]) continue;
        char gid[64] = ""; snprintf(gid, sizeof gid, "%.*s", (int)strcspn(hist[i].key, "|"), hist[i].key);
        char pl[240]; snprintf(pl, sizeof pl, "%s", hist[i].players);
        const char *nm[MPM_TAG_NAMES]; int n = 0;
        nm[n++] = hist[i].host; if (strcmp(hist[i].host, my_name)) nm[n++] = my_name;
        for (char *e = strtok(pl, ";"); e && n < MPM_TAG_NAMES; e = strtok(NULL, ";")) { e[strcspn(e, "|")] = 0; nm[n++] = e; }
        if (gid[0] && strcmp(hist[i].host, "the host")) seeded += mpm_tag_add(p, gid, nm, n);
        else if (gid[0]) seeded += mpm_tag_add(p, gid, nm + 1, n - 1);       /* host name unknown: the others only */
    }
    rig_log("mpmenu: mp_tags.txt created (%d world(s) from Recent worlds)", seeded);
}
static int tag_get(const char *guid, char names[][32]) {
    tag_seed(); char p[1200]; tag_path(p, sizeof p);
    return mpm_tag_get(p, guid, names, MPM_TAG_NAMES);
}
/* in a session, every 60 frames once the world is in: 2+ players present -> every present name goes on the world's tag */
static void tag_frame(const mpm_obs *o) {
    if (frames % 60 != 30 || !o->campaign_ready || o->loading) return;
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    if (!c || mp_is_spectator(s) || dsnet_player_id(c) < 0) return;
    uint32_t pm = dsnet_present_mask(c);
    if (__builtin_popcount(pm) < 2) return;
    char gid[64] = ""; if (!campaign_guid(gid, sizeof gid) || mp_hub_guid(gid)) return;   /* no tag on a hub: it is never saved */
    const char *nm[DSNET_MAX_PLAYERS]; char buf[DSNET_MAX_PLAYERS][32]; int n = 0;
    for (int p = 0; p < DSNET_MAX_PLAYERS; p++) {                           /* slot order: the host first */
        if (!(pm >> p & 1u)) continue;
        const char *x = dsnet_player_name(c, p); if (!x || !x[0]) continue;
        snprintf(buf[n], 32, "%s", x); nm[n] = buf[n]; n++;
    }
    tag_seed(); char p[1200]; tag_path(p, sizeof p);
    if (mpm_tag_add(p, gid, nm, n)) rig_log("mpmenu: Multiplayer tag on world %s (%d players present)", gid, n);
}

/* ---------------------------------------------------------------- single-player copy (owner 2026-10-25, mp_solo.c)
 * "when logging in multiplayer into a world for the first time, i should have the option to create a backup of it which
 * stays single player". Host from Your save (or a Recent worlds host row): once the world is open and before the session
 * starts (the model waits in HOST_LOADING while obs.solo_hold), a world never played with others (no Multiplayer tag) and
 * never asked before (<app data>/mp_solo.txt) asks once: Yes = its newest save becomes a NEW single-player world in the same
 * folder (new guid, "<name> (solo)", the original's mods in the loader's worlds.json, no tag), then the session starts with
 * the original; No = the session starts. Not for New save, the hub or a Foreign Expedition pick (no world of the player's is
 * played there). A guest is never asked: its game keeps no save of the host's world (mp_game.c h_save, mp_sync deleted). */
void fe_new_guid(char out[37]);                                             /* fe_save.c */
const char *fe_game_saves_dir(char *b, size_t n);                           /* fe_game.c: the normal game's Saves folder (tests: none) */
enum { SOLO_OFF, SOLO_CHECK, SOLO_ASK };
static int solo_st, solo_shows; static int64_t solo_t0;
static char solo_guid[40], solo_world[160];
static uint64_t solo_asked_n, solo_made_n;                                  /* test read-back (mpm_status) */
static void solo_path(char *p, size_t cap) { app_path(p, cap, "mp_solo.txt"); }
static int solo_dirs(char d[2][1100]) {
    snprintf(d[0], sizeof d[0], "%s/data/Saves", rig_dir);
    return fe_game_saves_dir(d[1], sizeof d[1]) ? 2 : 1;
}
static void solo_suffix(char *pre, char *post, size_t cap) {               /* "%s (solo)" in the player's language, around the name */
    const char *f = TR("%s (solo)"), *k = strstr(f, "%s");
    if (!k) { snprintf(pre, cap, "%s", ""); snprintf(post, cap, "%s", " (solo)"); return; }
    snprintf(pre, cap, "%.*s", (int)(k - f), f); snprintf(post, cap, "%s", k + 2);
}
/* the loader's worlds.json: the copy gets the original's mods. dsl_world_copy_mods (loader kit 0.1.27+, Mac + Windows);
   an older Mac loader: its dsl_world_mods / dsl_world_set_mods (the Mods window's own calls). 0 ok. */
static int solo_copy_mods(const char *from, const char *to) {
    int (*cp)(const char *, const char *) = (int (*)(const char *, const char *))mpmenu_loader_sym("dsl_world_copy_mods");
    if (cp) return cp(from, to);
#ifndef _WIN32
    int (*get)(const char *, const char **, int) = (int (*)(const char *, const char **, int))mpmenu_loader_sym("dsl_world_mods");
    int (*set)(const char *, const char *const *, int) = (int (*)(const char *, const char *const *, int))mpmenu_loader_sym("dsl_world_set_mods");
    if (get && set) {
        const char *ids[64]; char keep[64][64]; const char *kp[64];
        int n = get(from, ids, 64);
        for (int i = 0; i < n; i++) { snprintf(keep[i], sizeof keep[i], "%s", ids[i]); kp[i] = keep[i]; }   /* the loader's buffer is static */
        return set(to, kp, n);
    }
#endif
    return -2;                                                              /* no loader (test copies): nothing to copy */
}
static int solo_make(char cg[37], char *name, size_t nn) {
    char d[2][1100]; int nd = solo_dirs(d); const char *dp[2] = { d[0], d[1] }; mps_save sv;
    if (!mps_newest(dp, nd, solo_guid, &sv)) { rig_log("mpmenu: single-player copy: no save of world %s any more", solo_guid); return -1; }
    do fe_new_guid(cg); while (mp_hub_guid(cg));
    char pre[96], post[96], out[1400], err[300] = ""; solo_suffix(pre, post, sizeof pre);
    snprintf(name, nn, "%s%s%s", pre, solo_world, post); dsi_utf8_fix_end(name);
    if (mps_copy(sv.path, cg, pre, post, out, sizeof out, err, sizeof err)) { rig_log("mpmenu: single-player copy of %s FAILED: %s", sv.path, err); return -1; }
    int mrc = solo_copy_mods(solo_guid, cg);
    Obj ld = mf ? AT(mf, 0x180, Obj) : NULL;                                 /* the Load list reads the new save */
    if (ld) F(0x812AB8, void (*)(Obj, void *))(ld, NULL);                    /* CampaignSaveLoader.TryRefreshSave */
    solo_made_n++;
    rig_log("mpmenu: single-player copy of world %s: %s -> %s (world %s); mods in worlds.json: %s", solo_guid, sv.path, out, cg,
            !mrc ? "copied" : mrc == -2 ? "no loader" : "NOT copied (the copy takes the mods it is first opened with)");
    return 0;
}
static void solo_answer(int yes) {
    if (solo_st != SOLO_ASK) return;
    solo_st = SOLO_OFF;
    char p[1200], cg[37] = "", name[260] = ""; solo_path(p, sizeof p);
    if (yes && solo_make(cg, name, sizeof name)) {                         /* not written: asked again next time */
        if (ui_notice) ui_notice(TR("The single-player copy could not be made. Your world is not changed."));
        return;
    }
    mps_asked_put(p, solo_guid, yes, cg);
    rig_log("mpmenu: single-player copy box for world %s: %s", solo_guid, yes ? "Yes" : "No");
    if (yes && ui_notice) { char t[400]; snprintf(t, sizeof t, TR("Single-player copy made: %s"), name); dsi_utf8_fix_end(t); ui_notice(t); }
}
/* before the model step: the world is open (HOST_LOADING), first time? */
static void solo_obs(mpm_obs *o) {
    if (solo_st && M.state != MPM_HOST_LOADING) { if (lbox_up == BX_SOLO) box_close(); solo_st = SOLO_OFF; }   /* failed / left */
    if (solo_st == SOLO_CHECK && o->campaign_ready) {                       /* the same frame the model would start the lobby */
        solo_st = SOLO_OFF;
        char g[64] = "", nm[MPM_TAG_NAMES][32], p[1200]; solo_path(p, sizeof p);
        if (o->tutorial || !campaign_guid(g, sizeof g)) return;               /* the tutorial never hosts (model) */
        char d[2][1100]; int nd = solo_dirs(d); const char *dp[2] = { d[0], d[1] }; mps_save sv;
        const char *why = mp_hub_guid(g) ? "a hub" : tag_get(g, nm) >= 0 ? "it has the Multiplayer tag" : mps_asked(p, g) ? "asked before"
                        : !mps_newest(dp, nd, g, &sv) ? "no save of it on this computer" : NULL;
        if (why) { rig_log("mpmenu: single-player copy: not asked for world %s (%s)", g, why); return; }
        snprintf(solo_guid, sizeof solo_guid, "%s", g);
        Obj c = cdc(), clan = c ? AT(c, 0x50, Obj) : NULL; solo_world[0] = 0;
        if (clan) str_utf8(AT(clan, 0x20, Obj), solo_world, sizeof solo_world);   /* ClanDataContainer.ClanName */
        solo_st = SOLO_ASK; solo_shows = 0; solo_t0 = frames;
        rig_log("mpmenu: single-player copy: world %s (%s) is new to multiplayer, newest save %s: asking", g, solo_world, sv.path);
    }
    o->solo_hold = solo_st == SOLO_ASK;
}
/* the box: over nothing else; shown again if another box took the view (max 3 times); no box within 20 s = the session starts */
static void solo_frame(void) {
    if (solo_st != SOLO_ASK) return;
    Obj v = lbox_view();
    if (lbox_up == BX_SOLO && lbox_ours(v)) return;                         /* up: waiting for the click */
    if (lbox_up == BX_SOLO) { lbox_up = BX_NONE; rig_log("mpmenu: single-player copy box closed another way"); }
    if (solo_shows >= 3 || frames - solo_t0 > 1200) {
        solo_st = SOLO_OFF; rig_log("mpmenu: single-player copy box: no answer, the session starts (asked again next time)"); return;
    }
    if (lbox_up != BX_NONE || (u_alive(v) && u_on(v)) || M.confirm_leave) return;   /* another box first */
    char pre[96], post[96], copy[260], bd[400], sm[500], t[1400]; solo_suffix(pre, post, sizeof pre);
    snprintf(copy, sizeof copy, "%s%s%s", pre, solo_world, post); dsi_utf8_fix_end(copy);
    snprintf(bd, sizeof bd, TR("%s becomes a multiplayer world. A copy stays single-player, so you can go on with it alone."), solo_world);
    snprintf(sm, sizeof sm, TR("Yes makes a new world, \"%s\", from your newest save, in the same folder. You are asked once for each world."), copy);
    snprintf(t, sizeof t, "<size=150%%>%s</size>\n%s\n<size=88%%><color=#a69085>%s</color></size>", TR("Keep a single-player copy of this world?"), bd, sm);
    dsi_utf8_fix_end(t);
    box_show(BX_SOLO, t, mpm_word("TEXTKEY_CommonPopUp_Yes", TR_NOOP("Yes")), mpm_word("TEXTKEY_CommonPopUp_No", TR_NOOP("No")));
    solo_shows++;
    if (lbox_up == BX_SOLO) solo_asked_n++;
}
/* the load list's rows we decorated: world rows (case, guid, its stock name text string, our "Remove tag" text) and save rows */
/* world rows (SubUI_SettlerNameCase) and save rows we know, for every live load list. The title's Load list and the
   Multiplayer page's list are two views alive at once: 64 held 35 worlds in one list only (gate13 2026-10-16: 6 worlds
   of the Your save list had no entry, so the filter never hid them). A full table is logged once. */
#define TAG_ROWS 512
typedef struct { uintptr_t row, name0, rm; char guid[40]; int hid; } tag_row;   /* hid: a save row the list filter turned off (it was on) */
static tag_row tcase[TAG_ROWS], tslot[TAG_ROWS];
static char tag_pending[40];                                                /* the world the Remove box asks about */
static tag_row *tag_row_of(tag_row *t, Obj row) {
    tag_row *free_at = NULL;
    for (int i = 0; i < TAG_ROWS; i++) {
        Obj r = u_gc_get(t[i].row);
        if (r == row && u_alive(r)) return &t[i];
        if (!free_at && !u_alive(r)) free_at = &t[i];
    }
    if (free_at) { u_gc_free(free_at->row); u_gc_free(free_at->name0); u_gc_free(free_at->rm); memset(free_at, 0, sizeof *free_at); free_at->row = u_gc_new(row); }
    else { static int said; if (!said++) rig_log("mpmenu: world/save row table full (%d rows): a row is not filtered or tagged", TAG_ROWS); }
    return free_at;
}
static void tag_slot_seen(Obj slot, const char *guid) { tag_row *r = tag_row_of(tslot, slot); if (r) snprintf(r->guid, sizeof r->guid, "%s", guid); }
typedef struct { float x, y, w, h; } URect;
static float txt_pref_w(Obj t) { return mm_is(t, K_TMP) ? F(0x4F0D81C, float (*)(Obj, void *))(t, NULL) : 0; }   /* TMP_Text.preferredWidth */
/* the world row as the tag says: name + "Multiplayer · with names" + the "Remove tag" text, or the stock name alone */
static void tag_case_show(tag_row *r) {
    Obj cs = u_gc_get(r->row), nt = u_alive(cs) ? AT(cs, 0x28, Obj) : NULL; /* SubUI_SettlerNameCase._nameText */
    if (!u_alive(nt)) return;
    Obj name0 = u_gc_get(r->name0), rm = u_gc_get(r->rm);
    char nm[MPM_TAG_NAMES][32]; int n = tag_get(r->guid, nm);
    if (n < 0) {
        if (name0) tmp_set_obj(nt, name0);
        if (u_alive(rm)) u_active(rm, false);
        return;
    }
    char clan[200] = ""; utf16_to_utf8(name0, clan, sizeof clan);
    char who[200]; mpm_tag_names(nm, n, 3, who, sizeof who);
    char wt[300]; snprintf(wt, sizeof wt, TR("with %s"), who);
    char t[600]; snprintf(t, sizeof t, "%s  <size=75%%><color=#8FC4FF>%s</color><color=#A69085>  %s</color></size>", clan, TR("Multiplayer"), wt);
    tmp_set(nt, t);
    if (!u_alive(rm)) {
        rm = mk_txt(nt, u_tr(nt), 0, 14, 513, 0, 0, 0, 120, 24, C_SUB);
        if (!rm) return;
        { char rt[96]; snprintf(rt, sizeof rt, "<u>%s</u>", TR("Remove tag")); tmp_set(rm, rt); }
        F(0x5260348, void (*)(Obj, bool, void *))(rm, true, NULL);          /* raycastTarget on: the click stops on it (see below) */
        u_gc_free(r->rm); r->rm = u_gc_new(rm);
    }
    u_active(rm, true);
    /* placed in the name text's space, then moved under the row itself (world position kept, drawn last): under the name
     * text a click bubbled up to the row's open / close button and folded the row (menu_pair run2, 4a) */
    Obj rmt = u_tr(rm), ntt = u_tr(nt);
    if (F(0x510992C, Obj (*)(Obj, void *))(rmt, NULL) != ntt) F(0x510B2C4, void (*)(Obj, Obj, bool, void *))(rmt, ntt, false, NULL);   /* Transform.parent, SetParent */
    /* after the name's text, inside the name's rect (centre anchors of the name text, local units) */
    URect nr = F(0x5106978, URect (*)(Obj, void *))(u_tr(nt), NULL);
    int al = mm_is(nt, K_TMP) ? F(0x4F0B7AC, int (*)(Obj, void *))(nt, NULL) : 513;   /* TMP_Text.alignment: low bits 1 left, 2 centre, 4 right */
    float pw = txt_pref_w(nt), rw = txt_pref_w(rm) + 6, x;
    if (pw > nr.w) pw = nr.w;
    x = (al & 2) ? pw / 2 : (al & 4) ? nr.w / 2 : -nr.w / 2 + pw;
    place(rm, x + 14 + rw / 2, 0, rw, 24);
    F(0x510B2C4, void (*)(Obj, Obj, bool, void *))(rmt, u_tr(cs), true, NULL);
    F(0x510ED6C, void (*)(Obj, void *))(rmt, NULL);                         /* SetAsLastSibling */
}
static void (*o_case_init)(Obj, Obj, const void *, bool, int, Obj, Obj, Obj, void *);
static void h_case_init(Obj self, Obj clan, const void *hdr, bool open, int nb, Obj slots, Obj ng, Obj oc, void *mi) {
    o_case_init(self, clan, hdr, open, nb, slots, ng, oc, mi);
    if (!self || !hdr) return;
    Guid g; memcpy(&g, hdr, 16); char gs[40]; guid_str(g, gs);              /* CampaignSaveHeader.CampaignGuid (header by reference) */
    tag_row *r = tag_row_of(tcase, self); if (!r) return;
    Obj nt = AT(self, 0x28, Obj);
    snprintf(r->guid, sizeof r->guid, "%s", gs);
    u_gc_free(r->name0); r->name0 = u_alive(nt) ? u_gc_new(tmp_get(nt)) : 0;   /* the stock text, to put back */
    tag_case_show(r);
}
static void tag_answer(int confirm) {
    char p[1200]; tag_path(p, sizeof p);
    if (!confirm || !tag_pending[0]) { rig_log("mpmenu: Multiplayer tag kept (box: Cancel)"); tag_pending[0] = 0; return; }
    int had = mpm_tag_remove(p, tag_pending);
    rig_log("mpmenu: Multiplayer tag removed from world %s (%s)", tag_pending, had ? "done" : "there was none");
    for (int i = 0; i < TAG_ROWS; i++) if (!strcmp(tcase[i].guid, tag_pending) && u_alive(u_gc_get(tcase[i].row))) tag_case_show(&tcase[i]);
    for (int i = 0; i < TAG_ROWS; i++) {                                    /* its save rows: the stock title back */
        Obj sl = u_gc_get(tslot[i].row), tt = u_alive(sl) && !strcmp(tslot[i].guid, tag_pending) ? AT(sl, 0x38, Obj) : NULL;
        char cur[320] = ""; if (u_alive(tt)) utf16_to_utf8(tmp_get(tt), cur, sizeof cur);
        char *k = strstr(cur, "  <color=#8FC4FF>["); if (k) { *k = 0; tmp_set(tt, cur); }   /* our tag (h_slot_set), any language */
    }
    tag_pending[0] = 0;
}
static void tag_ask(tag_row *r) {
    char nm[MPM_TAG_NAMES][32], who[200], clan[200] = "", t[400]; int n = tag_get(r->guid, nm); if (n < 0) return;
    mpm_tag_names(nm, n, MPM_TAG_NAMES, who, sizeof who); utf16_to_utf8(u_gc_get(r->name0), clan, sizeof clan);
    snprintf(tag_pending, sizeof tag_pending, "%s", r->guid);
    char bd[400]; snprintf(bd, sizeof bd, TR("%s was played with %s."), clan, who);
    snprintf(t, sizeof t, "<size=150%%>%s</size>\n%s\n<size=88%%><color=#a69085>%s</color></size>", TR("Remove the multiplayer tag?"), bd, TR("The world itself does not change. The tag comes back if a friend joins it again."));
    box_show(BX_UNTAG, t, TR("Remove"), mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel")));
    if (lbox_up != BX_UNTAG) tag_answer(1);                                 /* no box could be shown: the player asked for it */
}
static tag_row *tag_rm_at(V3 m) {
    for (int i = 0; i < TAG_ROWS; i++) { Obj rm = u_gc_get(tcase[i].rm); if (u_alive(u_gc_get(tcase[i].row)) && pt_in(rm, m)) return &tcase[i]; }
    return NULL;
}
static void tag_click_frame(void) {
    if (lbox_up || !popup_open(5) || !F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) return;   /* Input.GetMouseButtonDown(0), load list open */
    tag_row *r = tag_rm_at(F(0x51AD4DC, V3 (*)(void *))(NULL));
    if (r) tag_ask(r);
}

/* ---------------------------------------------------------------- load list filter (owner 2026-10-15, MASTER_PLAN 3)
 * Single player (title Load, Esc Load): single-player worlds only; "Your save" on the Multiplayer page: multiplayer worlds only
 * (= the Multiplayer tag, mp_tags.txt). One toggle under the list shows the other kind too; it is off each time the list
 * opens. Picking a save for a Foreign Expedition shows every world. The world being played always shows. Hidden = the
 * world row and its save rows inactive, then the stock RefreshPanelSize. The save panel (Save) is never filtered.
 * Until the loader's LOAD-ROW API exists this file owns the SaveLoadView.RefreshLoadPanel hook (as the tag owns its two). */
static int lf_mode = MPM_LIST_SOLO, lf_other, lf_hidden, lf_shown, lf_applied;
/* the loader's filter panel (loader kit v0.1.23, its dsl_rows.c; owner 2026-10-24: filters on the right of the Load
 * Campaign window replace this box): on the single-player list the panel's Multiplayer box decides, so this filter shows
 * every world there and draws no box. Your save and the Expedition pick keep this filter as before. The loader asks two
 * exports below: dsl_loadlist_own (1 = this filter decides the open list) and dsl_loadlist_world_mp (the tag). */
static int lf_loader_panel(void) {
    static int v = -1;
    if (v < 0) { int (*f)(void) = (int (*)(void))mpmenu_loader_sym("dsl_loadlist_panel"); v = f && f() > 0; rig_log("mpmenu: load list: the loader's filter panel %s", v ? "decides the single-player list" : "not there (older loader): this box stays"); }
    return v;
}
static int lf_eff(int mode) { return mode == MPM_LIST_SOLO && lf_loader_panel() ? MPM_LIST_ALL : mode; }
static uintptr_t h_lf_slv, h_lf_tog, h_lf_box;                              /* the list view, our toggle text and its box */
static void lf_toggle_sync(Obj slv);
static tag_row *tag_row_find(tag_row *t, Obj row) {                        /* no new entry (tag_row_of adds one) */
    for (int i = 0; i < TAG_ROWS; i++) if (t[i].row && u_gc_get(t[i].row) == row && u_alive(row)) return &t[i];
    return NULL;
}
static void lf_apply(Obj slv, const char *why) {
    if (!u_alive(slv)) return;
    Obj list = AT(slv, 0xE0, Obj), arr = list ? AT(list, 0x10, Obj) : NULL;   /* SaveLoadView._campaignCases (List: _items, _size) */
    int n = list ? AT(list, 0x18, int) : 0, hid = 0, shown = 0;
    char hidden_guid[64][40]; int nh = 0;
    for (int i = 0; i < n && arr; i++) {
        Obj cs = AT(arr, 0x20 + 8 * i, Obj); if (!u_alive(cs)) continue;
        tag_row *r = tag_row_find(tcase, cs); if (!r || !r->guid[0]) continue;
        char nm[MPM_TAG_NAMES][32]; int tagged = tag_get(r->guid, nm) >= 0;
        int show = mpm_list_show(lf_eff(lf_mode), lf_other, tagged, AT(cs, 0xA0, bool));   /* _isCurrentCampaign */
        u_active(cs, show);
        if (show) shown++; else { hid++; if (nh < 64) snprintf(hidden_guid[nh++], 40, "%s", r->guid); }
    }
    for (int i = 0; i < TAG_ROWS; i++) {                                    /* save rows follow their world */
        Obj sl = u_gc_get(tslot[i].row); if (!u_alive(sl) || !tslot[i].guid[0]) continue;
        int h = 0; for (int k = 0; k < nh && !h; k++) h = !strcmp(hidden_guid[k], tslot[i].guid);
        if (h && u_go_active_self(u_go(sl))) { u_active(sl, false); tslot[i].hid = 1; }
        else if (!h && tslot[i].hid) { u_active(sl, true); tslot[i].hid = 0; }   /* only rows we turned off (a folded world keeps its own) */
    }
    F(0x85FBC8, void (*)(Obj, void *))(slv, NULL);                          /* SaveLoadView.RefreshPanelSize */
    lf_hidden = hid; lf_shown = shown; lf_applied++;
    u_gc_free(h_lf_slv); h_lf_slv = u_gc_new(slv);
    lf_toggle_sync(slv);
    rig_log("mpmenu: load list filter (%s): mode %d, other %d: %d shown, %d hidden", why, lf_mode, lf_other, shown, hid);
}
static void (*o_refresh_load)(Obj, Obj, void *);
static void h_refresh_load(Obj self, Obj set, void *mi) {
    o_refresh_load(self, set, mi);
    lf_mode = mpm_list_mode(&M);
    lf_apply(self, "list filled");
}
/* the toggle: CheckBox_Normal / _Checked box + "Show multiplayer worlds" in a band between the list and its bottom row (Cancel /
 * Remove File / Load). The band is made by raising the list's bottom edge (ScrollView_SettingData offsetMin) by LF_BAND
 * while the toggle shows; the stock edge comes back for the Save panel and a list with no toggle. Its parent is the
 * bottom row of the Load panel (Panel_LowerLoad), so the Save panel never shows it. Local units of the list's canvas. */
#define LF_BAND 44
static uintptr_t h_lf_sv; static V2 lf_sv_off0; static int lf_sv_up;
static void lf_band(Obj slv, int on) {
    Obj sv = u_gc_get(h_lf_sv);
    if (!u_alive(sv)) {
        if (!on || !u_alive(slv)) return;
        Obj t = tr_find(u_tr(slv), "Panel_SaveLoadUI/ScrollView_SettingData"); if (!mm_is(t, K_RT)) return;
        u_gc_free(h_lf_sv); h_lf_sv = u_gc_new(t); sv = t; lf_sv_up = 0;
    }
    if (on == lf_sv_up) return;
    if (on) lf_sv_off0 = F(0x51077B4, V2 (*)(Obj, void *))(sv, NULL);       /* RectTransform.offsetMin (stock) */
    F(0x510794C, void (*)(Obj, V2, void *))(sv, on ? (V2){ lf_sv_off0.x, lf_sv_off0.y + LF_BAND } : lf_sv_off0, NULL);
    lf_sv_up = on;
}
static void lf_toggle_sync(Obj slv) {
    Obj tog = u_gc_get(h_lf_tog), box = u_gc_get(h_lf_box), low = AT(slv, 0x98, Obj);   /* _lowLoadPanel */
    const char *lbl = mpm_list_toggle_label(lf_eff(lf_mode));
    if (!lbl[0]) { u_active(tog, false); u_active(box, false); lf_band(slv, 0); return; }
    if (!u_alive(tog)) {
        Obj tt = AT(slv, 0x48, Obj);                                        /* _saveLoadTitle (text template) */
        static uintptr_t hc; static void *kc;
        Obj cn = find_view_x(&hc, &kc, "Refactor.UI", "CommonNoticeView", true);
        Obj lb = cn ? tr_find(u_tr(cn), "Img_BlackLetterBox") : NULL, tpl_img = lb ? image_of(lb) : NULL;
        if (!u_alive(low) || !u_alive(tt)) return;
        tog = mk_txt(tt, u_tr(low), 0, 16, 513, 0, 0, 0, 400, 36, C_SUB);    /* middle left */
        if (!tog) return;
        das_key_set(tog, false);                                            /* our text, not a localisation key */
        F(0x5260348, void (*)(Obj, bool, void *))(tog, true, NULL);         /* raycastTarget: the click is ours */
        u_gc_free(h_lf_tog); h_lf_tog = u_gc_new(tog);
        box = tpl_img ? mk_img(tpl_img, u_tr(low), S_CHK_N, C_WHITE, 0, 0, 20, 20) : NULL;   /* 2x the 10 x 10 box: whole pixels */
        u_gc_free(h_lf_box); h_lf_box = box ? u_gc_new(box) : 0;
        rig_log("mpmenu: load list toggle built (box %s)", box ? "yes" : "no");
    }
    lf_band(slv, 1);
    URect lr = F(0x5106978, URect (*)(Obj, void *))(u_tr(low), NULL);       /* bottom row, local (centre = 0,0) */
    float y = lr.h / 2 + LF_BAND / 2.0f, x0 = -lr.w / 2 + 4;
    Obj sp = spr(lf_other ? S_CHK_C : S_CHK_N);
    if (box) { place(box, x0 + 14, y, 20, 20); img_set(box, sp, sp ? C_WHITE : C_SUB); u_active(box, true); }
    place(tog, x0 + 36 + 200, y, 400, 36);
    tmp_set(tog, lbl); u_active(tog, true);
}
static void lf_flip(void) {
    Obj slv = u_gc_get(h_lf_slv); if (!u_alive(slv)) return;
    lf_other = !lf_other;
    if (!lf_other) { F(0x860030, void (*)(Obj, void *))(slv, NULL); F(0x86047C, void (*)(Obj, void *))(slv, NULL); }   /* UnselectAllSlots, LockLoadButtons: no pick left in a hidden world */
    lf_apply(slv, "toggle");
}
static int lf_hit(V3 m) { return pt_in(u_gc_get(h_lf_tog), m) || pt_in(u_gc_get(h_lf_box), m); }
static void lf_frame(void) {
    static int was;
    int open = popup_open(5);
    if (was && !open) { lf_other = 0; lf_band(NULL, 0); }                   /* off again next time the list opens; stock list edge back */
    was = open;
    if (!open || lbox_up) return;
    Obj slv = u_gc_get(h_lf_slv);
    if (u_alive(slv) && frames % 15 == 0 && mpm_list_mode(&M) != lf_mode) { lf_mode = mpm_list_mode(&M); lf_apply(slv, "mode changed"); }
    if (F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) {                    /* mouse down on the toggle */
        V3 m = F(0x51AD4DC, V3 (*)(void *))(NULL);
        if (lf_hit(m)) lf_flip();
    }
}

/* the loader's hidden-worlds line (dsl_sdk.h dsl_loadlist_hidden): world rows this filter hides in the open Load list
   now; kind 1 = multiplayer worlds (single-player list), 2 = single-player worlds (Your save) */
__attribute__((visibility("default"))) int dsl_loadlist_hidden(int *kind) {
    if (kind) *kind = lf_mode == MPM_LIST_MP ? 2 : 1;
    return popup_open(5) && u_alive(u_gc_get(h_lf_slv)) ? lf_hidden : 0;
}
/* the loader's filter panel: 1 = this filter decides the open list now (Your save, the Expedition pick), so the panel stays
   away; 0 = the single-player list, the panel decides */
__attribute__((visibility("default"))) int dsl_loadlist_own(void) { return mpm_list_mode(&M) != MPM_LIST_SOLO; }
/* the loader (dsl_world.c): 1 = a temporary world it never registers in worlds.json: the hub, made new for each session and
   never saved (owner 2026-10-25, mp_worldsave.h) */
__attribute__((visibility("default"))) int dsl_world_temporary(const char *guid) { return mp_hub_guid(guid); }
/* the loader's filter panel: 1 = a Multiplayer world (its tag, mp_tags.txt) */
__attribute__((visibility("default"))) int dsl_loadlist_world_mp(const char *guid) {
    char nm[MPM_TAG_NAMES][32]; return guid && *guid && tag_get(guid, nm) >= 0;
}

/* ---------------------------------------------------------------- the third host choice (MASTER_PLAN 2.2 B register_host_choice) */
/* old API: one choice per id (same id = replace), its page = Load save + the Expedition Rules, lobby Start = on_start
   (MPA_FE_START). draw_page is kept but never called (as before). 1 = ok. */
__attribute__((visibility("default"))) int mpmenu_register_host_choice(const char *id, const char *label, const char *tip, void (*draw_page)(void *root), mpm_host_start_fn on_start) {
    mpm_choice fn = { .v2 = 0, .draw_page = draw_page, .on_start = on_start };
    int k = mpm_choice_register(&hcl, id, label, tip, &fn, 0);
    if (k < 0) { rig_log("mpmenu: host choice \"%s\" refused (%d registered)", id ? id : "", hcl.n); return 0; }
    rig_log("mpmenu: host choice \"%s\" registered (%s, place %d)", label, id, k + 1);
    return 1;
}
/* register_host_choice2 (mp_api.h mp_host_choice): page = Start + Back (page_start), the mod's own panel (draw_page) or the
   Expedition Rules open (draw_page NULL); lobby Start = lobby_start. 0 ok, -1 refused. */
__attribute__((visibility("default"))) int mpmenu_register_host_choice2(const char *id, const char *label, const char *tip, void (*draw_page)(void *panel),
                                                                        int (*page_start)(char *, size_t), int (*lobby_start)(char *, size_t)) {
    mpm_choice fn = { .v2 = 1, .draw_page = draw_page, .page_start = page_start, .lobby_start = lobby_start };
    int k = mpm_choice_register(&hcl, id, label, tip, &fn, 1);
    if (k < 0) { rig_log("mpmenu: host choice \"%s\" refused (5th choice or bad texts; %d registered)", id ? id : "", hcl.n); return -1; }
    rig_log("mpmenu: host choice \"%s\" registered (%s, API 2, place %d)", label, id, k + 1);
    return 0;
}
__attribute__((visibility("default"))) const char *mpmenu_host_choice_now(void) { return mpm_choice_now(&hcl, &M); }

/* Esc-menu buttons of a mod (mp_api register_esc_button): a Report bug clone above Leave multiplayer, in a multiplayer game */
#define MAX_ESCB 4
static struct { char label[48], tip[300]; int (*visible)(void); void (*click)(void); uintptr_t h; int pressed; } escb[MAX_ESCB]; static int nescb;
__attribute__((visibility("default"))) int mpmenu_register_esc_button(const char *label, const char *tip, int (*visible)(void), void (*click)(void)) {
    if (!label || !click || nescb >= MAX_ESCB) return -1;
    snprintf(escb[nescb].label, sizeof escb[nescb].label, "%s", label); snprintf(escb[nescb].tip, sizeof escb[nescb].tip, "%s", tip ? tip : "");
    escb[nescb].visible = visible; escb[nescb].click = click; nescb++;
    rig_log("mpmenu: Esc button \"%s\" registered", label);
    return 0;
}
static int escb_press(Obj btn) {
    for (int i = 0; i < nescb; i++) if (btn && escb[i].h && btn == u_gc_get(escb[i].h)) { escb[i].pressed = 1; return 1; }
    return 0;
}
static void escb_frame(const mpm_obs *o) {
    Obj v = o->scene == 2 ? esc_view() : NULL;
    int ingame = v && u_on(v) && mp_game_session() && M.state == MPM_PLAYING;
    for (int i = 0; i < nescb; i++) {
        if (escb[i].pressed) { escb[i].pressed = 0; escb[i].click(); }
        if (!v || !u_on(v)) continue;
        Obj b = u_gc_get(escb[i].h); if (!u_alive(b)) b = NULL;
        if (!b && eb(MPB_LEAVE)) {
            Obj src = AT(v, 0x70, Obj);
            b = u_alive(src) ? u_instantiate(src, u_tr_parent(u_tr(src))) : NULL;
            if (u_alive(b)) { if (mm_is(b, K_SMB)) F(0xF2DB60, void (*)(Obj, Obj, void *))(b, NULL, NULL); AT(b, 0x30, Obj) = NULL; esc_label_fit(b);
                              u_set_sibling(u_tr(b), u_sibling(u_tr(eb(MPB_LEAVE)))); u_gc_free(escb[i].h); escb[i].h = u_gc_new(b); }
            else b = NULL;
        }
        if (b) { int show = ingame && (!escb[i].visible || escb[i].visible()); u_active(b, show); if (show) { tmp_set(AT(b, 0x28, Obj), escb[i].label); if (escb[i].tip[0]) tip_try(b, escb[i].label, escb[i].tip); } }
    }
}

int mpmenu_register_hooks(const mp_menu_hooks *h) {
    if (!h || nhooks >= MAX_HOOKS) return -1;
    hooks[nhooks++] = *h; rig_log("mpmenu: menu hooks %d registered", nhooks);
    return 0;
}

/* ---------------------------------------------------------------- i18n (ds_i18n.h; research/i18n_plan.md)
 * Language files: <folder of multiplayer.dylib / .dll>/lang/<code>.json (DSMP_LANG_DIR overrides; none = English). The game's
 * language is polled once per frame (LanguageSetting.GetCurrentLanguage, by name). It can only change on the title screen
 * (Options), so on a change the title clones are rebuilt (labels and fonts); campaign windows are built in the language of
 * their scene. Other lanes compare dsi_generation() in their own update. Mod-registered labels (host choices, Esc buttons)
 * are drawn as registered: the mod registers them again, translated, when the language changes. */
static void lang_dir_init(void) {
    char d[1200] = ""; const char *env = getenv("DSMP_LANG_DIR");
    if (env && *env) snprintf(d, sizeof d, "%s", env);
    else { Dl_info di; if (dladdr((void *)lang_dir_init, &di) && di.dli_fname) { snprintf(d, sizeof d, "%s", di.dli_fname);
                         char *sl = strrchr(d, '/'); if (sl) snprintf(sl + 1, sizeof d - (size_t)(sl + 1 - d), "lang"); else d[0] = 0; } }
    dsi_set_dir(d);
    rig_log("mpmenu: i18n: language files in %s", d[0] ? d : "(none: English)");
}
/* a game word (mpm_word): TextKeyExtensions.GetText(key) in the game's language, cached per language; NULL = use ours */
static const char *game_word(const char *key, const char *en) {
    static struct { char key[64], text[128]; unsigned gen; } c[24]; static int n;
    (void)en;
    int sc = scene(); if (sc != 1 && sc != 2) return NULL;                 /* the text tables are loaded on the title and in a world */
    int k = 0; while (k < n && strcmp(c[k].key, key)) k++;
    if (k < n && c[k].gen == dsi_generation()) return c[k].text[0] ? c[k].text : NULL;
    static void *mi; static int tried; if (!mi && !tried) { tried = 1; mi = mth("Refactor", "TextKeyExtensions", "GetText", 1); }
    if (!mi) return NULL;
    if (k == n) { if (n == 24) return NULL; n++; }
    snprintf(c[k].key, sizeof c[k].key, "%s", key); c[k].gen = dsi_generation(); c[k].text[0] = 0;
    Obj s = MCALL(mi, Obj, Obj, const void *)(u_str(key), mi);
    utf16_to_utf8(s, c[k].text, sizeof c[k].text);
    if (!strncmp(c[k].text, "TextKey not found", 17) || !strcmp(c[k].text, key)) c[k].text[0] = 0;   /* missing: our English */
    rig_log("mpmenu: i18n: game word %s (%s) = \"%s\"", key, dsi_code(), c[k].text[0] ? c[k].text : "(not found)");
    return c[k].text[0] ? c[k].text : NULL;
}
static void destroy_go(Obj comp) { if (u_alive(comp)) { Obj g = u_go(comp); if (g) F(0x50FC8F8, void (*)(Obj, void *))(g, NULL); } }   /* Object.Destroy */
/* the language changed on the title: our title clones go (rebuilt by title_sync on this frame, in the new language and fonts) */
static void title_teardown(void) {
    Obj v = title_view();
    if (v && stock_hidden) stock_set(v, 0);
    stock_hidden = 0;
    for (int i = 0; i < NT; i++) destroy_go(tb(title_ids[i]));                /* column rows (their arrows / rows go with the panels) */
    destroy_go(u_gc_get(h_rw_root)); destroy_go(u_gc_get(rp_title.root)); destroy_go(u_gc_get(rp_fe.root)); destroy_go(u_gc_get(h_cp_root));
    destroy_go(u_gc_get(h_field)); destroy_go(u_gc_get(h_namefield));
    for (int i = 0; i < MPB_COUNT; i++) { u_gc_free(h_tb[i]); h_tb[i] = 0; }
    u_gc_free(h_title_built); h_title_built = 0;
    destroy_go(u_gc_get(h_lf_tog)); destroy_go(u_gc_get(h_lf_box)); u_gc_free(h_lf_tog); u_gc_free(h_lf_box); h_lf_tog = h_lf_box = 0;   /* load list toggle */
    for (int i = 0; i < TAG_ROWS; i++) if (tcase[i].rm) { destroy_go(u_gc_get(tcase[i].rm)); u_gc_free(tcase[i].rm); tcase[i].rm = 0; }   /* "Remove tag" (rows redo it) */
    cp_drawn = 0;
}
static void lang_poll(void) {
    if (!ls_ready() || !ls_lang_mi) return;                                 /* before il2cpp / the class: English */
    int v = MCALL(ls_lang_mi, int32_t, const void *)(ls_lang_mi);
    if (!dsi_set_lang(v)) return;
    rig_log("mpmenu: i18n: game language %d = %s (%d texts, %d dropped as unsafe)", v, dsi_code(), dsi_loaded(), dsi_dropped());
    if (scene() == 1 && h_title_built) title_teardown();
    if (!chat_typing) destroy_go(u_gc_get(h_ch_root));                     /* the chat (kept across scenes) is built again: "Chat", "Say:" */
}

void mpmenu_frame(void) {
    fog_poll();
    frames++;
    fv_input_frame();
    { static int was; int t = teams_modal(); if (was && !t) teams_close_frames = 3; else if (teams_close_frames > 0) teams_close_frames--; was = t; }   /* the Escape that closed Teams stays ours */
    if (!mf) return;
    lang_poll();                                                            /* i18n: the game's language (title Options) */
    join_poll();
    assets_scan();                                                          /* every 300 frames until all are found */
    mpm_obs o; observe(&o);
    solo_obs(&o);                                                           /* host from Your save: the single-player copy box first */
    o.edited = fields_edited; fields_edited = 0;
    int c = pending_click; pending_click = MPB_NONE;
    mpm_choices_to_model(&hcl, &M);
    for (int k = 0; k < nhooks && c != MPB_NONE; k++) if (hooks[k].click && hooks[k].click(c)) c = MPB_NONE;   /* Custom Modes windows (FE) */
    int st0 = M.state;
    int a = mpm_step(&M, &o, c);
    if (M.state != st0) rig_log("mpmenu: state %d -> %d (click %d)", st0, M.state, c);
    if (st0 == MPM_PICK_SAVE && M.state == MPM_HOST_LOADING && !M.set.fe) solo_st = SOLO_CHECK;   /* a world of the player's: first time? */
    if (o.failed && o.msg[0] && (M.state == MPM_FAILED || M.state == MPM_IDLE)) mp_steam_ack();   /* the message is in M.msg now */
    if (a & ~MPA_REFRESH) run_actions(a, &o);
    choice_page_frame();
    int lobby = M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY;
    if (lobby && o.campaign_ready && !o.esc_open && !(a & MPA_LOBBY_SHOW) && !M.confirm_leave && lbox_up != BX_SPECT_OFF && frames % 30 == 0) {   /* not under the Leave / spectators box */
        /* the lobby stays until Start / Leave, but never over another window: a stock popup (story, level-up...) or the
         * ui lane's own boxes (player disconnected: Keep playing / Wait) */
        static int (*ui_modal)(void); static int looked;
        if (!looked) { looked = 1; ui_modal = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_modal_open"); }
        Obj p = popups();
        int busy_ui = (p && !strcmp(cls_name(p), "PopUpViewContainer") && F(0xF62338, bool (*)(Obj, void *))(p, NULL)) || (ui_modal && ui_modal());
        if (!busy_ui) popup_activate(1);
    }
    if (lobby && o.esc_open && frames % 5 == 0) {                          /* a ui-lane box came up: the lobby steps aside */
        static int (*ui_modal2)(void); static int looked2;
        if (!looked2) { looked2 = 1; ui_modal2 = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_modal_open"); }
        Obj ev = esc_view();
        if (ui_modal2 && ui_modal2() && ev) { (mm_is(ev, K_SMV) ? F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL) : (void)0); rig_log("mpmenu: lobby closed for a ui box; it comes back after"); }
    }
    if (frames % 30 == 0) { mp_session *hs = mp_game_session(); upnpport_host(hs ? mp_server_port(hs) : 0); }   /* map / renew / delete (own thread) */
    if (M.state == MPM_LOBBY && frames % 30 == 0) direct_refresh();
    M.set.watching = o.role == 1 ? spect_watching() : 0;                    /* "Allowed · 2 watching" (Rules value) */
    rules_sync(&o);
    if (o.role && o.campaign_ready && !o.loading && frames % 300 == 150) hist_update();   /* Recent worlds: the session now */
    if (!hist_loaded && o.scene == 1) { hist_load(); M.nhist = nhist; }
    if (M.state == MPM_TITLE_FRIENDS && frames % 60 == 0) friends_refresh();
    title_sync(&o);
    esc_sync(&o);
    for (int k = 0; k < nhooks; k++) if (hooks[k].frame) hooks[k].frame(&o);
    { Obj lb = u_gc_get(h_lwb[LW_BRING]); int br = 0; for (int k = 0; k < nhooks && !br; k++) br = hooks[k].lobby_bring && hooks[k].lobby_bring();
      if (u_alive(lb)) u_active(lb, (M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY) && br); }
    notice_frame(&o);
    leave_box_frame();
    invite_frame(&o);
    solo_frame();
    hostmods_frame();
    lbox_fit_frame();
    lbox_esc_frame();                                                       /* our box on the kit's Escape stack while it shows */
    rejoin_frame(&o);
    tag_frame(&o);
    tag_click_frame();
    lf_frame();
    rules_watch(&o);
    pick_frame(&o);
    chat_frame(&o);
    tips_frame(&o);
    for (int k = 0; k < nhooks; k++) if (hooks[k].tips) hooks[k].tips();
    for (int k = 0; k < nhooks; k++) if (hooks[k].frame_late) hooks[k].frame_late(&o);
    if (nescb) escb_frame(&o);
    if (!o.role && !conn && !mp_game_session() && joined_how[0] && M.state != MPM_GUEST_JOINING && M.state != MPM_GUEST_LOADING) { joined_how[0] = 0; joined_steamid = 0; }   /* session over */
    fit_frame();                                                            /* last: every text of this frame is set and placed */
}

int mpmenu_gear_mode(void) { return mp_game_session() ? M.set.gear : -1; }
/* ui lane (ux #30): the player panel hides while the host's lobby or the guest's waiting panel is up */
__attribute__((visibility("default"))) int mpmenu_lobby_open(void) { return M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY; }

void mp_modes_start(void);
void mpmenu_install(void) {
    mp_fog_install();                   /* per-player fog hooks (core next 2026-10-15) */
    mpm_init(&M);
    lang_dir_init(); mpm_set_word_fn(game_word);                           /* i18n: English until the game's language is read */
    prefs_load();
    int bad = rig_hook(0xF2DC30, (void *)h_esc_press, (void **)&o_esc_press, "mpmenu SubUI_SystemMenuButton.OnPressButton");
    bad |= rig_hook(0xE6FF20, (void *)h_title_click, (void **)&o_title_click, "mpmenu SubUI_TitleMenuButton.OnPointerClick");
    bad |= rig_hook(0xF2EEA8, (void *)h_esc_load, (void **)&o_esc_load, "mpmenu SystemMenuView.OnLoadCampaignPressed (blocked in MP)");
    bad |= rig_hook(0xD6C800, (void *)h_quickload, (void **)&o_quickload, "mpmenu OnEvent(QuickLoadRequested) (blocked in MP)");
    bad |= rig_hook(0xF70150, (void *)h_slot_set, (void **)&o_slot_set, "mpmenu SubUI_SaveFileButton.SetData (MP tag)");
    bad |= rig_hook(0x85E50C, (void *)h_case_init, (void **)&o_case_init, "mpmenu SubUI_SettlerNameCase.Init (MP world tag)");
    bad |= rig_hook(0x85ED7C, (void *)h_refresh_load, (void **)&o_refresh_load, "mpmenu SaveLoadView.RefreshLoadPanel (single / multiplayer list)");
    bad |= rig_hook(0xD42BBC, (void *)h_textfocus, (void **)&o_textfocus, "mpmenu KeyInputDispatcher.IsTextInputFocused (chat typing: no hotkeys)");
    bad |= rig_hook(0x51AD584, (void *)h_scroll_delta, (void **)&o_scroll_delta, "mpmenu Input.get_mouseScrollDelta (0 over the chat: no camera zoom)");
    if (test_mode()) {
        bad |= rig_hook(0x518AEA0, (void *)h_clip_get, (void **)&o_clip_get, "mpmenu GUIUtility.get_systemCopyBuffer (test: file)");
        bad |= rig_hook(0x518AFE0, (void *)h_clip_set, (void **)&o_clip_set, "mpmenu GUIUtility.set_systemCopyBuffer (test: file)");
    }
    mp_modes_start();                   /* Foreign Expedition + PvP (mp_modes.c): host choices, hooks, sim commands */
    rig_log("mpmenu: installed (%s)%s", bad ? "HOOK FAILED" : "ok", test_mode() ? ", clipboard -> clipboard.txt" : "");
}

/* ---------------------------------------------------------------- socket commands (tests: read-back + simulated clicks) */
static const char *state_name(int s) {
    static const char *n[] = { "idle", "title_menu", "title_host", "pick_save", "pick_new", "host_loading", "lobby", "playing",
                               "guest_joining", "guest_loading", "failed", "title_join", "guest_lobby", "title_friends" };
    return s >= 0 && s <= MPM_TITLE_FRIENDS ? n[s] : "?";
}
static void put_btns(Buf *b, const char *key, const int *ids, int n, uintptr_t *hs, int txt_off) {
    bput(b, ",\"%s\":{", key);
    for (int i = 0; i < n; i++) {
        Obj x = u_gc_get(hs[ids[i]]); char t[200] = "";
        if (u_alive(x)) str_utf8(tmp_get(AT(x, txt_off, Obj)), t, sizeof t);
        bput(b, "%s\"%d\":{\"exists\":%s,\"active\":%s,\"text\":", i ? "," : "", ids[i], u_alive(x) ? "true" : "false", u_on(x) ? "true" : "false");
        bstr(b, t); bput(b, "}");
    }
    bput(b, "}");
}
/* ---------------------------------------------------------------- layout read-back (tests) */
int rect_of(Obj comp, float r[4]) {                                 /* screen-space x0,y0,x1,y1 (overlay canvas: world = pixels) */
    if (!u_alive(comp)) return 0;
    Obj arr = F(0x6F8BA8, Obj (*)(void *, size_t))(md(0x6656B28), 4);       /* new Vector3[4] */
    (mm_is(u_tr(comp), K_RT) ? F(0x51089D0, void (*)(Obj, Obj, void *))(u_tr(comp), arr, NULL) : (void)0);      /* RectTransform.GetWorldCorners */
    V3 *c = (V3 *)((char *)arr + 0x20);
    r[0] = r[2] = c[0].x; r[1] = r[3] = c[0].y;
    for (int i = 1; i < 4; i++) { if (c[i].x < r[0]) r[0] = c[i].x; if (c[i].x > r[2]) r[2] = c[i].x; if (c[i].y < r[1]) r[1] = c[i].y; if (c[i].y > r[3]) r[3] = c[i].y; }
    return 1;
}
/* ---- test input through AppKit (the same NSEvent path a real keyboard / mouse takes into Unity) */
static id ns_game_window(void) {
    id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    id wins = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("windows"));
    unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count")); id best = nil; double ba = 0;
    for (unsigned long i = 0; i < n; i++) {
        id w = ((id (*)(id, SEL, unsigned long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i);
        CGRect f = ((CGRect (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame"));
        if (f.size.width * f.size.height > ba) { ba = f.size.width * f.size.height; best = w; }
    }
    return best;
}
static void ns_deliver(id ev, const char *via) {
    if (!ev) return;
    if (!strcmp(via, "app")) {
        id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
        ((void (*)(id, SEL, id, BOOL))objc_msgSend)(app, sel_registerName("postEvent:atStart:"), ev, NO);
    } else { id w = ns_game_window(); if (w) ((void (*)(id, SEL, id))objc_msgSend)(w, sel_registerName("sendEvent:"), ev); }
}
static id ns_str(const char *s) { return ((id (*)(Class, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), s); }
static int mac_keycode(char c) {                                             /* US layout virtual key codes */
    static const struct { char c; unsigned char k; } t[] = {
        {'a',0},{'s',1},{'d',2},{'f',3},{'h',4},{'g',5},{'z',6},{'x',7},{'c',8},{'v',9},{'b',11},{'q',12},{'w',13},{'e',14},{'r',15},
        {'y',16},{'t',17},{'1',18},{'2',19},{'3',20},{'4',21},{'6',22},{'5',23},{'=',24},{'9',25},{'7',26},{'-',27},{'8',28},{'0',29},
        {']',30},{'o',31},{'u',32},{'[',33},{'i',34},{'p',35},{'l',37},{'j',38},{'\'',39},{'k',40},{';',41},{'\\',42},{',',43},{'/',44},
        {'n',45},{'m',46},{'.',47},{' ',49} };
    if (c >= 'A' && c <= 'Z') c += 32;
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (t[i].c == c) return t[i].k;
    return 49;
}
/* Characters: macOS hands typed text (insertText) only to the active app, which a hidden test copy never is; key codes
 * do reach Unity (GetKeyDown). So the test builds Unity Events (Event.KeyboardEvent) and gives them to the field's own
 * TMP_InputField.ProcessEvent: the handler OnUpdateSelected runs for every key event of the selected field. */
static int tmp_keys(const char *chars, const char *special) {
    Obj f = u_gc_get(h_ch_field); if (!u_alive(f)) return 0;
    static void *mke, *mpe; if (!mke) { mke = mth("UnityEngine", "Event", "KeyboardEvent", 1); mpe = mth("TMPro", "TMP_InputField", "ProcessEvent", 1); }
    if (!mke || !mpe) { rig_log("mpmenu: test keys: Event.KeyboardEvent %p / ProcessEvent %p missing", mke, mpe); return 0; }
    int n = 0, len = chars ? (int)strlen(chars) : 1;
    for (int i = 0; i < len; ) {                                             /* one UTF-8 character per event (accents, Cyrillic, CJK) */
        char one[16]; int cl = 1;
        if (chars) { ch_utf8((const unsigned char *)chars + i, &cl); memcpy(one, chars + i, (size_t)cl); one[cl] = 0; if (chars[i] == ' ') snprintf(one, sizeof one, "space"); } else snprintf(one, sizeof one, "%s", special);
        i += cl;
        Obj ev = MCALL(mke, Obj, Obj, const void *)(u_str(one), mke);
        if (!ev) continue;
        MCALL(mpe, void, Obj, Obj, const void *)(f, ev, mpe); n++;
    }
    return n;
}
/* chars: type each character (key down + up); else one special key (code). Returns events sent. */
static int key_inject(const char *chars, int code, const char *via) {
    id w = ns_game_window(); if (!w) return 0;
    long wn = ((long (*)(id, SEL))objc_msgSend)(w, sel_registerName("windowNumber"));
    id pi = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSProcessInfo"), sel_registerName("processInfo"));
    double ts = ((double (*)(id, SEL))objc_msgSend)(pi, sel_registerName("systemUptime"));
    int n = 0, len = chars ? (int)strlen(chars) : 1;
    for (int i = 0; i < len; ) {
        char one[8]; int kc, cl = 1;
        if (chars) { ch_utf8((const unsigned char *)chars + i, &cl); memcpy(one, chars + i, (size_t)cl); one[cl] = 0; kc = cl == 1 ? mac_keycode(chars[i]) : 49; i += cl; }
        else { kc = code; if (kc < 0) return 0; one[0] = kc == 36 ? '\r' : kc == 53 ? 0x1b : kc == 51 ? 0x7f : kc == 49 ? ' ' : 0; one[1] = 0; i++; }
        for (int up = 0; up < 2; up++) {
            id ev = ((id (*)(Class, SEL, unsigned long, CGPoint, unsigned long, double, long, id, id, id, BOOL, unsigned short))objc_msgSend)(
                objc_getClass("NSEvent"), sel_registerName("keyEventWithType:location:modifierFlags:timestamp:windowNumber:context:characters:charactersIgnoringModifiers:isARepeat:keyCode:"),
                up ? 11 : 10, CGPointZero, 0, ts, wn, nil, ns_str(one), ns_str(one), NO, (unsigned short)kc);
            ns_deliver(ev, via); n++;
        }
    }
    return n;
}
static int click_px(float x, float y, const char *via);
static int click_field(const char *via) {                                    /* left click in the middle of the chat text field */
    Obj f = u_gc_get(h_ch_field); float r[4] = { 0 }; if (!u_on(f) || !rect_of(f, r)) return 0;
    return click_px((r[0] + r[2]) / 2, (r[1] + r[3]) / 2, via);
}
static int click_px(float x, float y, const char *via) {                     /* a real left click at screen pixel (x, y), y up */
    id w = ns_game_window(); if (!w) return 0;
    long wn = ((long (*)(id, SEL))objc_msgSend)(w, sel_registerName("windowNumber"));
    double sc = ((double (*)(id, SEL))objc_msgSend)(w, sel_registerName("backingScaleFactor")); if (sc <= 0) sc = 1;
    CGPoint pt = { x / sc, y / sc };
    id pi = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSProcessInfo"), sel_registerName("processInfo"));
    double ts = ((double (*)(id, SEL))objc_msgSend)(pi, sel_registerName("systemUptime"));
    for (int up = 0; up < 2; up++) {
        id ev = ((id (*)(Class, SEL, unsigned long, CGPoint, unsigned long, double, long, id, long, long, float))objc_msgSend)(
            objc_getClass("NSEvent"), sel_registerName("mouseEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:clickCount:pressure:"),
            up ? 2 : 1, pt, 0, ts, wn, nil, 0, 1, up ? 0.0f : 1.0f);
        ns_deliver(ev, via);
    }
    rig_log("mpmenu: test click at %.0f,%.0f (points, scale %.1f)", pt.x, pt.y, sc);
    return 2;
}
static int tree_det;                                                        /* mpm_tree {detail:1}: RectTransform values + layout components */
static void tree(Buf *b, Obj tr, int depth, int *n) {
    if (!u_alive(tr) || *n > 200) return;
    char nm[96] = ""; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(tr, NULL), nm, sizeof nm);
    float r[4] = {0}; rect_of(tr, r);
    bput(b, "%s{\"d\":%d,\"name\":", (*n)++ ? "," : "", depth); bstr(b, nm);
    bput(b, ",\"on\":%d,\"rect\":[%.0f,%.0f,%.0f,%.0f]", u_go_active_self(u_go(tr)), r[0], r[1], r[2], r[3]);
    if (tree_det && mm_is(tr, K_RT)) {
        static void *ma0, *ma1, *mpv;
        if (!ma0) { ma0 = mth("UnityEngine", "RectTransform", "get_anchorMin", 0); ma1 = mth("UnityEngine", "RectTransform", "get_anchorMax", 0); mpv = mth("UnityEngine", "RectTransform", "get_pivot", 0); }
        V2 sd = F(0x510701C, V2 (*)(Obj, void *))(tr, NULL), ap = F(0x5106E38, V2 (*)(Obj, void *))(tr, NULL);
        V2 a0 = ma0 ? MCALL(ma0, V2, Obj, const void *)(tr, ma0) : (V2){ -1, -1 }, a1 = ma1 ? MCALL(ma1, V2, Obj, const void *)(tr, ma1) : (V2){ -1, -1 };
        V2 pv = mpv ? MCALL(mpv, V2, Obj, const void *)(tr, mpv) : (V2){ -1, -1 };
        bput(b, ",\"amin\":[%.2f,%.2f],\"amax\":[%.2f,%.2f],\"pivot\":[%.2f,%.2f],\"size\":[%.1f,%.1f],\"pos\":[%.1f,%.1f],\"comps\":[",
             a0.x, a0.y, a1.x, a1.y, pv.x, pv.y, sd.x, sd.y, ap.x, ap.y);
        static const char *cl[][2] = { { "UnityEngine.UI", "GridLayoutGroup" }, { "UnityEngine.UI", "VerticalLayoutGroup" }, { "UnityEngine.UI", "HorizontalLayoutGroup" },
                                       { "UnityEngine.UI", "ContentSizeFitter" }, { "UnityEngine.UI", "LayoutElement" }, { "UnityEngine.UI", "Image" },
                                       { "UnityEngine.UI", "Button" }, { "TMPro", "TMP_Text" }, { "UnityEngine", "Canvas" } };
        int c = 0; for (size_t i = 0; i < sizeof cl / sizeof cl[0]; i++) if (comp_of(tr, cl[i][0], cl[i][1])) { bput(b, "%s", c++ ? "," : ""); bstr(b, cl[i][1]); }
        bput(b, "]");
        Obj po = lg_padding(tr);
        if (po) { static void *mb, *mal; if (!mb) { mb = mth("UnityEngine", "RectOffset", "get_bottom", 0); mal = mth("UnityEngine.UI", "LayoutGroup", "get_childAlignment", 0); }
                  bput(b, ",\"pad_top\":%d,\"pad_bottom\":%d,\"align\":%d", MCALL(lg_top, int, Obj, const void *)(po, lg_top), mb ? MCALL(mb, int, Obj, const void *)(po, mb) : -1,
                       mal ? MCALL(mal, int, Obj, const void *)(comp_of(tr, "UnityEngine.UI", "LayoutGroup"), mal) : -1); }
    }
    bput(b, "}");
    if (depth >= 4) return;
    int k = (mm_is(tr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(tr, NULL) : 0);
    for (int i = 0; i < k; i++) tree(b, (mm_is(tr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(tr, i, NULL) : NULL), depth + 1, n);
}

/* UI screenshot (tests): screen-space overlay canvases are switched to camera mode on Camera.main for one render into
 * a screen-sized RenderTexture, then switched back. Same pixels as the overlay layout (camera rect = screen size). */
static char *png_out(Obj tex, const char *path, int w, int h, const char *how) {
    Obj png = F(0x51657D8, Obj (*)(Obj, void *))(tex, NULL);
    F(0x50FC8F8, void (*)(Obj, void *))(tex, NULL);
    if (!png) return strdup("{\"ok\":false,\"error\":\"EncodeToPNG null\"}");
    size_t nb = (size_t)AT(png, 0x18, uintptr_t);
    FILE *f = fopen(path, "wb"); if (!f) return strdup("{\"ok\":false,\"error\":\"cannot write\"}");
    fwrite((char *)png + 0x20, 1, nb, f); fclose(f);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"how\":\"%s\",\"w\":%d,\"h\":%d,\"bytes\":%zu}", how, w, h, nb); return b.p;
}
/* the window's back buffer (overlay UI included): RenderTexture.active = null, ReadPixels */
static char *backbuffer_shot(const char *path) {
    int w = F(0x5092C94, int (*)(void *))(NULL), h = F(0x5092CD4, int (*)(void *))(NULL);
    F(0x50CE6A4, void (*)(Obj, void *))(NULL, NULL);
    Obj tex = obj_new(0x6655970);
    F(0x50C4E5C, void (*)(Obj, int, int, int, bool, void *))(tex, w, h, 3, false, NULL);
    RectF2 r = { 0, 0, (float)w, (float)h };
    F(0x50C63B0, void (*)(Obj, RectF2, int, int, void *))(tex, r, 0, 0, NULL);
    F(0x50C5F34, void (*)(Obj, void *))(tex, NULL);
    return png_out(tex, path, w, h, "backbuffer");
}
static char *ui_shot(const char *path) {
    Obj cam = F(0x5083208, Obj (*)(void *))(NULL);                          /* Camera.main */
    if (!cam) return strdup("{\"ok\":false,\"error\":\"no Camera.main\"}");
    int w = F(0x5092C94, int (*)(void *))(NULL), h = F(0x5092CD4, int (*)(void *))(NULL);
    Obj tobj = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(md(0x664B018)));   /* typeof(Canvas) */
    Obj arr = F(0x50FCCC0, Obj (*)(Obj, void *))(tobj, NULL);               /* Object.FindObjectsOfType(Type) */
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0, nsw = 0;
    Obj sw[64]; Obj oldcam[64];
    for (int i = 0; i < n && nsw < 64; i++) {
        Obj cv = AT((char *)arr + 0x20, i * 8, Obj);
        if (!u_alive(cv) || !F(0x547521C, bool (*)(Obj, void *))(cv, NULL)) continue;          /* root canvases */
        if (F(0x5475018, int (*)(Obj, void *))(cv, NULL) != 0) continue;                        /* overlay only */
        oldcam[nsw] = F(0x5477610, Obj (*)(Obj, void *))(cv, NULL);
        F(0x547771C, void (*)(Obj, Obj, void *))(cv, cam, NULL);
        F(0x5475104, void (*)(Obj, int, void *))(cv, 1, NULL);              /* ScreenSpaceCamera */
        F(0x5475F18, void (*)(Obj, float, void *))(cv, F(0x507FC2C, float (*)(Obj, void *))(cam, NULL) + 0.5f, NULL);   /* planeDistance: just past the near plane */
        sw[nsw++] = cv;
    }
    static void (*render)(void *);
    if (!render) render = F(0x6F8B90, void *(*)(const char *))("UnityEngine.Camera::Render_Injected");
    Obj rt = F(0x50D2E38, Obj (*)(int, int, int, void *))(w, h, 24, NULL);
    F(0x5081CB8, void (*)(Obj, Obj, void *))(cam, rt, NULL);
    F(0x5477FEC, void (*)(void *))(NULL);                                    /* Canvas.ForceUpdateCanvases */
    if (render) render(AT(cam, 0x10, void *));
    F(0x50CE6A4, void (*)(Obj, void *))(rt, NULL);
    Obj tex = obj_new(0x6655970);
    F(0x50C4E5C, void (*)(Obj, int, int, int, bool, void *))(tex, w, h, 3, false, NULL);
    RectF2 r = { 0, 0, (float)w, (float)h };
    F(0x50C63B0, void (*)(Obj, RectF2, int, int, void *))(tex, r, 0, 0, NULL);
    F(0x50C5F34, void (*)(Obj, void *))(tex, NULL);
    F(0x50CE6A4, void (*)(Obj, void *))(NULL, NULL);
    F(0x5081CB8, void (*)(Obj, Obj, void *))(cam, NULL, NULL);
    F(0x50CFE6C, void (*)(Obj, void *))(rt, NULL);
    for (int i = 0; i < nsw; i++) { F(0x5475104, void (*)(Obj, int, void *))(sw[i], 0, NULL); F(0x547771C, void (*)(Obj, Obj, void *))(sw[i], oldcam[i], NULL); }
    F(0x5477FEC, void (*)(void *))(NULL);
    Obj png = F(0x51657D8, Obj (*)(Obj, void *))(tex, NULL);
    F(0x50FC8F8, void (*)(Obj, void *))(tex, NULL);
    if (!png) return strdup("{\"ok\":false,\"error\":\"EncodeToPNG null\"}");
    size_t nb = (size_t)AT(png, 0x18, uintptr_t);
    FILE *f = fopen(path, "wb"); if (!f) return strdup("{\"ok\":false,\"error\":\"cannot write\"}");
    fwrite((char *)png + 0x20, 1, nb, f); fclose(f);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"w\":%d,\"h\":%d,\"canvases\":%d,\"bytes\":%zu}", w, h, nsw, nb); return b.p;
}

/* ---- test only: dummy register_host_choice2 choices (mpm_testchoice). page_start queues the stock load of a save already in
   data/Saves (what a mod does), lobby_start says started; both can be told to fail. Counts read back by action "status". */
static struct { char save[256], page_err[120], lobby_err[120]; unsigned page_calls, lobby_calls, draw_calls, draw_null; int panel_rt; } tch;
static void tch_draw(void *panel) { if (!panel) { tch.draw_null++; return; } tch.draw_calls++; tch.panel_rt = mm_is((Obj)panel, K_RT); }
static int tch_page(char *err, size_t n) {
    tch.page_calls++;
    if (tch.page_err[0] || !tch.save[0] || !mf) { snprintf(err, n, "%s", tch.page_err[0] ? tch.page_err : "Test choice: no save given."); return -1; }
    Obj ev = obj_new(0x6651118);                                                      /* LoadCampaignRequested(save name) */
    F(0xDCFD78, void (*)(Obj, Obj, void *))(ev, u_str(tch.save), NULL);
    F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
    return 0;
}
static int tch_lobby(char *err, size_t n) {
    tch.lobby_calls++;
    if (tch.lobby_err[0]) { snprintf(err, n, "%s", tch.lobby_err); return -1; }
    return 0;
}
static char *testchoice_exec(const char *line) {
    char act[16] = "status", id[64] = "", label[96] = "", tip[400] = ""; double draw = 1;
    jstr(line, "action", act, sizeof act); jstr(line, "save", tch.save, sizeof tch.save);
    jstr(line, "page_err", tch.page_err, sizeof tch.page_err); jstr(line, "lobby_err", tch.lobby_err, sizeof tch.lobby_err);
    int rc = 0;
    if (!strcmp(act, "register")) {
        jstr(line, "id", id, sizeof id); jstr(line, "label", label, sizeof label); jstr(line, "tip", tip, sizeof tip); jnum(line, "draw", &draw);
        rc = mpmenu_register_host_choice2(id, label, tip, draw != 0 ? tch_draw : NULL, tch_page, tch_lobby);
    }
    Buf b = {0};
    bput(&b, "{\"ok\":true,\"rc\":%d,\"page_calls\":%u,\"lobby_calls\":%u,\"draw_calls\":%u,\"draw_null\":%u,\"panel_rt\":%d,\"menu_draw_calls\":%u,\"menu_null_calls\":%u,\"choice_now\":",
         rc, tch.page_calls, tch.lobby_calls, tch.draw_calls, tch.draw_null, tch.panel_rt, cp_calls, cp_null_calls);
    bstr(&b, mpmenu_host_choice_now());
    { float r[4] = { 0 }; Obj pnl = u_gc_get(h_cp_panel), root = u_gc_get(h_cp_root); if (u_on(root)) rect_of(pnl, r);
      bput(&b, ",\"panel_on\":%d,\"panel_rect\":[%.0f,%.0f,%.0f,%.0f],\"choices\":[", u_on(root), r[0], r[1], r[2], r[3]); }
    for (int k = 0; k < hcl.n; k++) { bput(&b, "%s{\"id\":", k ? "," : ""); bstr(&b, hcl.c[k].id); bput(&b, ",\"label\":"); bstr(&b, hcl.c[k].label);
                                      bput(&b, ",\"v2\":%d,\"own_page\":%d,\"button\":%d}", hcl.c[k].v2, hcl.c[k].v2 && hcl.c[k].draw_page != NULL, mpm_choice_btn(k)); }
    bput(&b, "]}");
    return b.p;
}
char *mpmenu_exec(const char *cmd, const char *line) {
    for (int k = 0; k < nhooks; k++) { char *r = hooks[k].exec ? hooks[k].exec(cmd, line) : NULL; if (r) return r; }   /* Custom Modes: mpm_fewin, mpm_pvp */
    if (!strcmp(cmd, "mpm_shot")) {
        char p[1024], how[32] = "camera"; if (!jstr(line, "path", p, sizeof p)) return strdup("{\"ok\":false}");
        jstr(line, "how", how, sizeof how);
        { const char *no = rig_path_refused(p, 1); if (no) return rig_path_err(no); }
        return !strcmp(how, "backbuffer") ? backbuffer_shot(p) : ui_shot(p);
    }
    if (!strcmp(cmd, "mpm_tree")) {                                         /* hierarchy of the Esc menu (layout debugging) */
        char wh[16] = ""; jstr(line, "what", wh, sizeof wh);                /* "load": the load list (SaveLoadView); "notice": the stock notice box */
        double dt = 0; jnum(line, "detail", &dt); tree_det = dt != 0;
        Obj v = !strcmp(wh, "load") ? find_view_x(&h_slv, &k_slv, "Refactor", "SaveLoadView", true) : !strcmp(wh, "title") ? title_view() : !strcmp(wh, "notice") ? lbox_view() : esc_view(); if (!v) return strdup("{\"ok\":false,\"error\":\"no view\"}");
        Buf b = {0}; int n = 0; bput(&b, "{\"ok\":true,\"screen\":[%d,%d],\"tree\":[", F(0x5092C94, int (*)(void *))(NULL), F(0x5092CD4, int (*)(void *))(NULL));
        tree(&b, u_tr(v), 0, &n); bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_click")) {                                        /* mpm_click {button: enum mpm_button} */
        double v; if (!jnum(line, "button", &v)) return strdup("{\"ok\":false,\"error\":\"need button\"}");
        pending_click = (int)v;
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_press")) {                                        /* real path: OnPointerClick / OnPressButton of the clone */
        double v; if (!jnum(line, "button", &v)) return strdup("{\"ok\":false,\"error\":\"need button\"}");
        int id = (int)v; Obj e = id > 0 && id < MPB_COUNT ? eb(id) : NULL;
        if (e) { (mm_is(e, K_SMB) ? F(0xF2DC30, void (*)(Obj, void *))(e, NULL) : (void)0); return strdup("{\"ok\":true,\"via\":\"OnPressButton\"}"); }
        for (int i = 0; i < LW_NB; i++) if (lw_ids[i] == id && u_on(u_gc_get(h_lwb[i]))) {    /* lobby window button: the real path */
            Obj w = u_gc_get(h_lwb[i]); (mm_is(w, K_SMB) ? F(0xF2DC30, void (*)(Obj, void *))(w, NULL) : (void)0);
            return strdup("{\"ok\":true,\"via\":\"OnPressButton (lobby window)\"}"); }
        if (id > 0 && id < MPB_COUNT && u_on(u_gc_get(h_lwr[id]))) {        /* lobby Rules panel */
            Obj w = u_gc_get(h_lwr[id]); (mm_is(w, K_SMB) ? F(0xF2DC30, void (*)(Obj, void *))(w, NULL) : (void)0);
            return strdup("{\"ok\":true,\"via\":\"OnPressButton (lobby rules)\"}"); }
        Obj t = id > 0 && id < MPB_COUNT ? tb(id) : NULL;
        if (t && mpmenu_title_click(t)) return strdup("{\"ok\":true,\"via\":\"title_click\"}");
        if (id > 0 && id < MPB_COUNT) { pending_click = id; return strdup("{\"ok\":true,\"via\":\"queued (the clone is being rebuilt)\"}"); }
        return strdup("{\"ok\":false,\"error\":\"no such button\"}");
    }
    if (!strcmp(cmd, "mpm_type")) {                                         /* type into the address (or "field":"name") field (test) */
        char t[256] = "", which[16] = ""; jstr(line, "text", t, sizeof t); jstr(line, "field", which, sizeof which);
        Obj f = u_gc_get(strcmp(which, "name") ? h_field : h_namefield);
        if (!u_alive(f)) { F(0x518AFE0, void (*)(Obj, void *))(u_str(t), NULL); return strdup("{\"ok\":true,\"via\":\"clipboard\"}"); }
        (mm_is(f, K_FIELD) ? F(0x4EFAE8C, void (*)(Obj, Obj, void *))(f, u_str(t), NULL) : (void)0); return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_testchoice")) return testchoice_exec(line);    /* test: dummy register_host_choice2 choices */
    if (!strcmp(cmd, "mpm_newcampaign")) {                                  /* test: what the new-campaign screen's Start sends */
        double cn = 0;
        if (jnum(line, "cancel", &cn) && cn != 0) {                         /* {cancel:1}: the screen's own Cancel button (CreateCampaignView.OnClickedCancelButton) */
            static uintptr_t h_ccv; static void *k_ccv; Obj v = find_view_x(&h_ccv, &k_ccv, "Refactor.UI", "CreateCampaignView", false);
            static void *mpress; if (!mpress) mpress = mth("UnityEngine.UI", "Button", "Press", 0);   /* _cancelButton +0x68: what a click runs */
            Obj cb = v ? AT(v, 0x68, Obj) : NULL; if (u_alive(cb) && mpress) MCALL(mpress, void, Obj, const void *)(cb, mpress);
            char o[64]; snprintf(o, sizeof o, "{\"ok\":%s,\"open\":%d}", v ? "true" : "false", popup_open(7)); return strdup(o);
        }
        if (jnum(line, "popups", &cn) && cn != 0) { char o[256] = "{\"ok\":true,\"open\":["; for (int i = 1; i < 32; i++) if (popup_open(i)) snprintf(o + strlen(o), sizeof o - strlen(o), "%d,", i); strcat(o, "0]}"); return strdup(o); }   /* {popups:1}: open stock pop-ups */
        if (jnum(line, "close", &cn) && cn != 0) { Obj p = popups(); if (p && popup_open((int)cn)) F(0xF62A88, void (*)(Obj, int, void *))(p, (int)cn, NULL); return strdup("{\"ok\":true}"); }   /* {close:N}: DeactivateView(N), as the pop-up's own close */
        if (jnum(line, "probe", &cn) && cn != 0) { char o[64]; snprintf(o, sizeof o, "{\"ok\":true,\"open\":%d}", popup_open(7)); return strdup(o); }
        char nm[64] = "MP Test", sd[32] = "12345"; double sc = 1, cd = 1, md = 1; jstr(line, "name", nm, sizeof nm); jstr(line, "seed", sd, sizeof sd); jnum(line, "scenario", &sc);
        double tut = 0; jnum(line, "combat", &cd); jnum(line, "mgmt", &md); jnum(line, "tutorial", &tut);   /* difficulties (default 1); Hell = 5; tutorial 1 = PlayTutorial */
        Obj ev = obj_new(0x664BC18);                                        /* CreateCampaignRequested */
        F(0xD61EDC, void (*)(Obj, Obj, int, int, int, Obj, bool, bool, bool, Obj, bool, void *))(ev, u_str(nm), (int)sc, (int)cd, (int)md, u_str(sd), false, false, tut != 0, u_str(""), false, NULL);
        F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_skipintro")) {                                    /* test: the player clicks through the new-campaign intro */
        if (!popup_open(25)) return strdup("{\"ok\":true,\"open\":false}");
        Obj ev = obj_new(0x664B6F8); F(0xDCF898, void (*)(Obj, void *))(ev, NULL);   /* CompleteNarrativeIntroRequested */
        F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
        return strdup("{\"ok\":true,\"open\":true}");
    }
    if (!strcmp(cmd, "mpm_slots")) {                                        /* load list slot titles (MP tag check) */
        static void *ks; if (!ks) ks = class_by_name("Refactor.UI", "SubUI_SaveFileButton");
        Obj ty = ks ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(ks)) : NULL;
        Obj arr = ty ? F(0x50FCCC0, Obj (*)(Obj, void *))(ty, NULL) : NULL;   /* FindObjectsOfType: active slots */
        int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"open\":%s,\"slots\":[", popup_open(5) ? "true" : "false");
        for (int i = 0; i < n; i++) {
            Obj sb = AT((char *)arr + 0x20, i * 8, Obj); char t[256] = "";
            if (u_alive(sb)) str_utf8(tmp_get(AT(sb, 0x38, Obj)), t, sizeof t);
            bput(&b, "%s", i ? "," : ""); bstr(&b, t);
        }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_tag")) {                                          /* test: Multiplayer tag rows of the load list; {click:i} (real:1 = a real mouse click); answer with mpm_box */
        double v; char via[16] = "window"; jstr(line, "via", via, sizeof via);
        int ci = jnum(line, "click", &v) ? (int)v : -1, real = jnum(line, "real", &v) && v != 0;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"open\":%s,\"box\":%d,\"pending\":", popup_open(5) ? "true" : "false", lbox_up); bstr(&b, tag_pending); bput(&b, ",\"rows\":[");
        int k = 0;
        for (int i = 0; i < TAG_ROWS; i++) {
            Obj cs = u_gc_get(tcase[i].row); if (!u_alive(cs) || !u_on(cs)) continue;
            Obj nt = AT(cs, 0x28, Obj), rm = u_gc_get(tcase[i].rm); char t[400] = ""; utf16_to_utf8(tmp_get(nt), t, sizeof t);
            float r[4] = { 0 }, nr[4] = { 0 }; int rmon = u_on(rm) && rect_of(rm, r); rect_of(nt, nr);
            char nm[MPM_TAG_NAMES][32]; int n = tag_get(tcase[i].guid, nm);
            bput(&b, "%s{\"i\":%d,\"guid\":", k++ ? "," : "", i); bstr(&b, tcase[i].guid); bput(&b, ",\"name\":"); bstr(&b, t);
            bput(&b, ",\"names\":%d,\"remove\":%d,\"rm_rect\":[%.0f,%.0f,%.0f,%.0f],\"name_rect\":[%.0f,%.0f,%.0f,%.0f],\"case_open\":%d}", n, rmon, r[0], r[1], r[2], r[3], nr[0], nr[1], nr[2], nr[3], AT(cs, 0xA1, bool));
            if (i == ci && rmon) { if (real) click_px((r[0] + r[2]) / 2, (r[1] + r[3]) / 2, via); else tag_ask(&tcase[i]); }
        }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_list")) {                                         /* test: load list filter; {flip:1} (real:1 = a real mouse click on the toggle); {close:1} closes the list */
        double v; char via[16] = "window"; jstr(line, "via", via, sizeof via);
        int flip = jnum(line, "flip", &v) && v != 0, real = jnum(line, "real", &v) && v != 0;
        Obj slv = u_gc_get(h_lf_slv), tog = u_gc_get(h_lf_tog), box = u_gc_get(h_lf_box);
        float r[4] = { 0 }, br[4] = { 0 }; int ton = u_on(tog) && rect_of(tog, r); if (u_on(box)) rect_of(box, br);
        if (flip && ton) { if (real) click_px((r[0] + r[2]) / 2, (r[1] + r[3]) / 2, via); else lf_flip(); }
        char sg[40] = "";
        if (jstr(line, "scroll_to", sg, sizeof sg) && sg[0] && u_alive(slv)) {    /* test: scroll until that world's row is inside the list's viewport */
            Obj vp = tr_find(u_tr(slv), "Panel_SaveLoadUI/ScrollView_SettingData/Viewport"), svo = tr_find(u_tr(slv), "Panel_SaveLoadUI/ScrollView_SettingData");
            Obj sr = svo ? comp_of(svo, "UnityEngine.UI", "ScrollRect") : NULL; float vr[4] = { 0 }; rect_of(vp, vr);
            Obj row = NULL; for (int i = 0; i < TAG_ROWS; i++) if (!strcmp(tcase[i].guid, sg) && u_alive(u_gc_get(tcase[i].row))) row = u_gc_get(tcase[i].row);
            for (int k = 0; sr && row && k <= 40; k++) {
                F(0x549A6F0, void (*)(Obj, float, void *))(sr, 1.0f - k / 40.0f, NULL);   /* ScrollRect.verticalNormalizedPosition */
                float rr[4]; if (rect_of(AT(row, 0x28, Obj), rr) && rr[1] >= vr[1] && rr[3] <= vr[3]) break;
            }
        }
        if (jnum(line, "close", &v) && v != 0) { Obj p = popups(); if (p && popup_open(5)) F(0xF62A88, void (*)(Obj, int, void *))(p, 5, NULL); }   /* DeactivateView(5) */
        char tt[160] = ""; if (u_alive(tog)) utf16_to_utf8(tmp_get(tog), tt, sizeof tt);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"open\":%s,\"mode\":%d,\"model_mode\":%d,\"other\":%d,\"shown\":%d,\"hidden\":%d,\"applied\":%d,\"toggle\":%d,\"toggle_text\":",
                          popup_open(5) ? "true" : "false", lf_mode, mpm_list_mode(&M), lf_other, lf_shown, lf_hidden, lf_applied, ton); bstr(&b, tt);
        bput(&b, ",\"toggle_rect\":[%.0f,%.0f,%.0f,%.0f],\"box_rect\":[%.0f,%.0f,%.0f,%.0f],\"cases\":[", r[0], r[1], r[2], r[3], br[0], br[1], br[2], br[3]);
        Obj list = u_alive(slv) ? AT(slv, 0xE0, Obj) : NULL, arr = list ? AT(list, 0x10, Obj) : NULL; int n = list ? AT(list, 0x18, int) : 0;
        for (int i = 0; i < n && arr; i++) {
            Obj cs = AT(arr, 0x20 + 8 * i, Obj); if (!u_alive(cs)) continue;
            tag_row *t = tag_row_find(tcase, cs); char nm[MPM_TAG_NAMES][32]; int tg = t ? tag_get(t->guid, nm) >= 0 : -1;
            bput(&b, "%s{\"guid\":", i ? "," : ""); bstr(&b, t ? t->guid : "");
            bput(&b, ",\"active\":%d,\"tagged\":%d,\"current\":%d,\"open\":%d}", u_go_active_self(u_go(cs)) ? 1 : 0, tg, AT(cs, 0xA0, bool) ? 1 : 0, AT(cs, 0xA1, bool) ? 1 : 0);
        }
        int sl_on = 0, sl_hid = 0; for (int i = 0; i < TAG_ROWS; i++) { Obj x = u_gc_get(tslot[i].row); if (u_alive(x)) { if (u_go_active_self(u_go(x))) sl_on++; if (tslot[i].hid) sl_hid++; } }
        bput(&b, "],\"slots_on\":%d,\"slots_hidden\":%d}", sl_on, sl_hid); return b.p;
    }
    if (!strcmp(cmd, "mpm_open_load")) {                                    /* test: the stock Load Campaign button (fills the list) */
        Obj tv = title_view(); if (!tv) return strdup("{\"ok\":false,\"error\":\"not on the title\"}");
        (mm_is(tv, K_TMV) ? F(0xE710B8, void (*)(Obj, void *))(tv, NULL) : (void)0); return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_quit_title")) {                                   /* test: Esc > Quit to title, confirmed */
        Obj v = esc_view(); if (!v) { popup_activate(1); v = esc_view(); }
        if (!v) return strdup("{\"ok\":false,\"error\":\"no Esc menu\"}");
        (mm_is(v, K_SMV) ? F(0xF2F234, void (*)(Obj, void *))(v, NULL) : (void)0);                        /* SystemMenuView.QuitToTitleSceneCallback */
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_esc_load")) {                                     /* test: press the Esc menu's Load */
        Obj v = esc_view(); if (!v) return strdup("{\"ok\":false}");
        (mm_is(v, K_SMV) ? F(0xF2EEA8, void (*)(Obj, void *))(v, NULL) : (void)0); return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_chat")) {                                         /* test: {open:1} {text:"..."} {enter:1} {escape:1} (no keyboard here) */
        double v; char t[1024];
        if (jnum(line, "open", &v)) test_chat_open = 1;
        if (jstr(line, "text", t, sizeof t)) snprintf(test_chat_in, sizeof test_chat_in, "%s", t);
        if (jnum(line, "enter", &v)) test_chat_enter = 1;
        if (jnum(line, "escape", &v)) test_chat_escape = 1;
        if (jstr(line, "probe", t, sizeof t)) {                             /* test: per chat font, characters it lacks (add: 0 / 1) */
            chat_fonts(); Buf b = {0}; bput(&b, "{\"ok\":true,\"fonts\":[");
            for (int i = 0; i < nchf; i++) { Obj f = u_gc_get(h_chf[i]); int m = 0, n = 0;
                for (const unsigned char *q = (const unsigned char *)t; *q; ) { int cl; uint32_t cp = ch_utf8(q, &cl); q += cl; n++; m += !ch_has(f, cp); }
                bput(&b, "%s{\"name\":", i ? "," : ""); bstr(&b, chf_names[i]); bput(&b, ",\"chars\":%d,\"missing\":%d}", n, m); }
            bput(&b, "]}"); return b.p;
        }
#ifdef TEAMS_MOUSE_TEST
        if (jnum(line, "wheel", &v)) { double n = 30; jnum(line, "frames", &n); ch_fake_wheel = (V2){ 0, (float)v }; ch_fake_wheel_n = (int)n; }   /* fake wheel (test build) */
#endif
        if (jstr(line, "local", t, sizeof t)) { Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", mpmenu_chat_local("Test", t, MP_CHATL_LINE)); return b.p; }   /* mp_api v3 chat_local */
        if (jnum(line, "clears", &v)) {                                     /* mp_api v4 chat_clears, read through the table a mod gets */
            const mp_api_table *a = mp_api_get(4); Buf b = {0};
            bput(&b, "{\"ok\":true,\"api\":%d,\"version\":%u,\"clears\":%d}", a != NULL, a ? a->version : 0,
                 a && a->size >= offsetof(mp_api_table, chat_clears) + sizeof a->chat_clears && a->chat_clears ? (int)a->chat_clears() : -1); return b.p; }
        if (jnum(line, "drag", &v)) {                                       /* scripted pointer for the move / resize code (ch_fk) */
            double x0 = 0, y0 = 0, x1 = 0, y1 = 0, bt = 0, n = 8, wh = 0;
            jnum(line, "x0", &x0); jnum(line, "y0", &y0); jnum(line, "x1", &x1); jnum(line, "y1", &y1); jnum(line, "button", &bt); jnum(line, "frames", &n); jnum(line, "roll", &wh);
            if (ch_fk.on || ch_drag) return strdup("{\"ok\":false,\"error\":\"a drag is running\"}");
            ch_fk.button = bt < 0 ? -1 : bt != 0; ch_fk.wheel = (float)wh; ch_fk.i = 0; ch_fk.n = n < 1 ? 1 : n > 600 ? 600 : (int)n;
            ch_fk.x0 = (float)x0; ch_fk.y0 = (float)y0; ch_fk.x1 = (float)x1; ch_fk.y1 = (float)y1; ch_fk.on = 1;
            return strdup("{\"ok\":true}");
        }
        if (jnum(line, "defocus", &v)) {                                    /* as if the player clicked the world: the field loses the focus */
            static void *mcur, *mset; if (!mcur) { mcur = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0); mset = mth("UnityEngine.EventSystems", "EventSystem", "SetSelectedGameObject", 1); }
            Obj es = mcur ? MCALL(mcur, Obj, const void *)(mcur) : NULL; if (u_alive(es) && mset) MCALL(mset, void, Obj, Obj, const void *)(es, NULL, mset);
        }
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_key")) {                                         /* test: REAL key / mouse events through AppKit -> Unity's own input */
        char t[1024] = "", k[32] = "", via[16] = "window"; double v;
        jstr(line, "via", via, sizeof via);
        int n = 0;
        if (jstr(line, "chars", t, sizeof t)) n += key_inject(t, 0, via) + tmp_keys(t, NULL);   /* key codes to Unity + the text to the field */
        if (jstr(line, "key", k, sizeof k)) n += key_inject(NULL, !strcmp(k, "return") ? 36 : !strcmp(k, "escape") ? 53 : !strcmp(k, "backspace") ? 51 : !strcmp(k, "space") ? 49 : -1, via)
                                                  + (!strcmp(k, "backspace") ? tmp_keys(NULL, "backspace") : 0);
        if (jnum(line, "click_field", &v)) n += click_field(via);
        char o[96]; snprintf(o, sizeof o, "{\"ok\":%s,\"events\":%d}", n > 0 ? "true" : "false", n); return strdup(o);
    }
    if (!strcmp(cmd, "mpm_find_text")) {                                    /* test: active TMP texts whose rect holds the point (y up, px) */
        double x = 0, y = 0; jnum(line, "px", &x); jnum(line, "py", &y);
        static void *mi; if (!mi) mi = mth("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
        Obj arr = mi ? MCALL(mi, Obj, Obj, const void *)(type_obj("TMPro", "TMP_Text"), mi) : NULL;
        int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0, k = 0;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"n\":%d,\"hits\":[", n);
        for (int i = 0; i < n && k < 20; i++) {
            Obj t = AT((char *)arr + 0x20, i * 8, Obj); float r[4];
            if (!u_alive(t) || !u_go_active_self(u_go(t)) || !rect_of(t, r) || x < r[0] || x > r[2] || y < r[1] || y > r[3]) continue;
            char path[400] = "", nm[64]; Obj tr = u_tr(t);
            for (int d = 0; tr && d < 10; d++, tr = u_tr_parent(tr)) { str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(tr, NULL), nm, sizeof nm); char tmp[400]; snprintf(tmp, sizeof tmp, "%s/%s", nm, path); snprintf(path, sizeof path, "%s", tmp); }
            char tx[120] = ""; Obj so = tmp_get(t); if (so) str_utf8(so, tx, sizeof tx);
            bput(&b, "%s{\"path\":", k++ ? "," : ""); bstr(&b, path); bput(&b, ",\"text\":"); bstr(&b, tx);
            bput(&b, ",\"rect\":[%.0f,%.0f,%.0f,%.0f]}", r[0], r[1], r[2], r[3]);
        }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_tip")) { test_hover[0] = 0; jstr(line, "title", test_hover, sizeof test_hover); return strdup("{\"ok\":true}"); }   /* test: hover the element with this title; none = real mouse */
    if (!strcmp(cmd, "mpm_escape")) { test_escape = 1; return strdup("{\"ok\":true}"); }   /* test: as if Escape was pressed */
    if (!strcmp(cmd, "mpm_fog")) { double v = -1; jnum(line, "watch", &v); return mp_fog_state(v < 0 ? -1 : v != 0); }   /* test: watcher rule + fog counts */
    if (!strcmp(cmd, "mpm_spectators")) {                                   /* test: {allow:0|1} what the in-game cog calls (box when off + watching) */
        double v = 1; jnum(line, "allow", &v);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"box\":%d,\"allowed\":%d,\"watching\":%d}", mpmenu_spectators_set(v != 0), M.set.spectators, spect_watching()); return b.p;
    }
    if (!strcmp(cmd, "mpm_cogtest")) {                                      /* test: the built cog on the big Rules box image */
        Obj bx = u_gc_get(h_title_box); if (bx) img_set(bx, spr(S_COG), C_WHITE);
        Obj cg = u_gc_get(h_title_cog); if (cg) { static void *mt; if (!mt) mt = mth("UnityEngine.UI", "Image", "set_type", 1); if (mt) MCALL(mt, void, Obj, int, const void *)(cg, 0, mt); place(cg, 0, 0, 80, 80); }
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_cogdbg")) {                                       /* test: the title cog image and its sprite */
        Obj cg = u_gc_get(h_title_cog), sp = spr(S_COG); float r[4] = {0}; if (cg) rect_of(cg, r);
        Obj isp = cg && mm_is(cg, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(cg, NULL) : NULL;
        static void *mtex, *men; if (!mtex) { mtex = mth("UnityEngine", "Sprite", "get_texture", 0); men = mth("UnityEngine", "Behaviour", "get_enabled", 0); }
        Obj tx = sp && mtex ? MCALL(mtex, Obj, Obj, const void *)(sp, mtex) : NULL;
        int en = cg && men ? MCALL(men, bool, Obj, const void *)(cg, men) : -1;
        static void *mrect, *mw, *mpix; if (!mrect) { mrect = mth("UnityEngine", "Sprite", "get_rect", 0); mw = mth("UnityEngine", "Texture", "get_width", 0); mpix = mth("UnityEngine", "Texture2D", "GetPixel", 2); }
        RectF2 sr = { 0 }; if (sp && mrect) sr = MCALL(mrect, RectF2, Obj, const void *)(sp, mrect);
        int tw = tx && mw ? MCALL(mw, int, Obj, const void *)(tx, mw) : -1;
        Col px = { -1, -1, -1, -1 }; if (tx && mpix) px = MCALL(mpix, Col, Obj, int, int, const void *)(tx, 3, 8, mpix);
        Col ic = { -1, -1, -1, -1 }; static void *mcol; if (!mcol) mcol = mth("UnityEngine.UI", "Graphic", "get_color", 0); if (cg && mcol) ic = MCALL(mcol, Col, Obj, const void *)(cg, mcol);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"img\":%d,\"active\":%d,\"enabled\":%d,\"sprite\":%d,\"img_sprite_same\":%d,\"texture\":%d,\"rect\":[%.0f,%.0f,%.0f,%.0f],"
             "\"sprite_rect\":[%.1f,%.1f,%.1f,%.1f],\"tex_w\":%d,\"px38\":[%.2f,%.2f,%.2f,%.2f],\"img_color\":[%.2f,%.2f,%.2f,%.2f]}",
             cg != NULL, u_on(cg), en, u_alive(sp), isp == sp, u_alive(tx), r[0], r[1], r[2], r[3], sr.x, sr.y, sr.w, sr.h, tw, px.r, px.g, px.b, px.a, ic.r, ic.g, ic.b, ic.a);
        return b.p;
    }
    if (!strcmp(cmd, "mpm_rules")) {                                        /* test: a rule change in play (what the panel cog sends) */
        mp_session *cs = mp_game_session(); double u = 0, f = 0;
        int hu = jnum(line, "units", &u), hf = jnum(line, "fog", &f);   /* units and/or fog (shared fog 0/1) */
        if (!cs || (!hu && !hf)) return strdup("{\"ok\":false}");
        dsnet_rules r; dsnet_rules_get(mp_client(cs), &r);
        if (hu) r.units_per_player = (int)u;
        if (hf) r.shared_fog = f != 0;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", dsnet_set_rules(mp_client(cs), &r)); return b.p;
    }
    if (!strcmp(cmd, "mpm_claim")) {                                        /* test: claim a unit for this player (mp_claim) */
        char gs[64]; Guid g; mp_session *cs = mp_game_session();
        if (!cs || !jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) return strdup("{\"ok\":false}");
        Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", mp_claim(cs, (const uint8_t *)&g)); return b.p;
    }
    if (!strcmp(cmd, "mpm_esc")) { popup_activate(1); return strdup("{\"ok\":true}"); }   /* open the Esc menu (test) */
    if (!strcmp(cmd, "mpm_return_game")) { Obj ev = esc_view(); if (ev && u_on(ev) && mm_is(ev, K_SMV)) F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL); return strdup("{\"ok\":true}"); }   /* test: Esc menu > Return to Game */
    if (!strcmp(cmd, "mpm_hostmods")) {                                     /* test: {test:1} = DSMP_HOSTMODS_TEST (auto click); state of the hostmods box */
        double v = -1; jnum(line, "test", &v); if (v >= 0) { hostmods_loader(); hm_test = v != 0; }
        char pf[1024] = "";                                                 /* {plan_file:<dsl_hostmods_plan JSON>, vanilla:0|1}: the box from that plan (layout tests) */
        if (jstr(line, "plan_file", pf, sizeof pf)) {
            { const char *no = rig_path_refused(pf, 0); if (no) return rig_path_err(no); }
            static char pj[32768]; static dsmc_plan pl; FILE *f = fopen(pf, "rb"); size_t nr = f ? fread(pj, 1, sizeof pj - 1, f) : 0; if (f) fclose(f); pj[nr] = 0;
            double va = 0; jnum(line, "vanilla", &va); hostmods_loader();
            if (dsmc_plan_parse(pj, &pl) <= 0) return strdup("{\"ok\":false,\"error\":\"bad plan\"}");
            HM.vanilla = va != 0; HM.host[0] = 0; HM.rejoin[0] = 0;
            dsmc_hostmods_text(&pl, HM.vanilla, HM.text, sizeof HM.text, HM_ROWS); dsi_utf8_fix_end(HM.text);
            HM.can_fix = pl.can_fix != 0; HM.names_pending = 0; HM.pending = 1; HM.tries = 0; HM.go = 0; HM.auto_done = 1;
        }
        Buf b = {0}; bput(&b, "{\"ok\":true,\"loader\":%d,\"test\":%d,\"pending\":%d,\"box\":%d,\"can_fix\":%d,\"busy\":%d,\"rejoined\":%d,\"rejoin_state\":%d,\"rejoin\":",
                          hostmods_loader(), hm_test, HM.pending, lbox_up == BX_HOSTMODS, HM.can_fix, HM.busy, hm_rejoined, RJ.state);
        bstr(&b, HM.rejoin); bput(&b, "}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_box")) {                                          /* test: a click on the stock notice box's button (the real OnClick, through the ui lane's hook) */
        char w[16] = ""; jstr(line, "action", w, sizeof w); Obj v = lbox_view();
        if (!u_alive(v) || !u_on(v)) return strdup("{\"ok\":false,\"why\":\"no box\"}");
        if (!strcmp(w, "confirm")) F(0xF65A18, void (*)(Obj, void *))(v, NULL); else F(0xF65638, void (*)(Obj, void *))(v, NULL);
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_conn")) {                                          /* conninfo read-back: each player's connection words;
                                                                                test copies only: {mark:id} on the host makes that guest's link count as
                                                                                a Steam bridge (the rig has no Steam: the same loopback mark the bridge sets) */
        double mk = -1; jnum(line, "mark", &mk);
        int marked = 0;
        if (mk >= 0 && test_mode()) { char a[96]; mp_game_conn((int)mk, a, sizeof a); const char *pc = strrchr(a, ':');
            if (pc) { dsnet_relay_port_mark((uint16_t)atoi(pc + 1), 1); marked = atoi(pc + 1); } }
        Buf b = {0}; bput(&b, "{\"ok\":true,\"marked\":%d,\"rows\":[", marked);
        for (int i = 0, k = 0; i < 8; i++) { char w[128], a[96]; int v = mp_game_conn(i, a, sizeof a); if (v == DSNET_VIA_UNKNOWN) continue;
            mpmenu_conn_words(i, w, sizeof w);
            bput(&b, "%s{\"player\":%d,\"via\":%d,\"words\":", k++ ? "," : "", i, v); bstr(&b, w); bput(&b, "}"); }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_fail_join")) {                                     /* test stand-in for a refused Steam-invite join (no Steam in the rig):
                                                                                the model sees "joining", then the session ending with {msg} */
        char why[160] = "", host[64] = ""; jstr(line, "msg", why, sizeof why); jstr(line, "host", host, sizeof host);
        mpm_obs o; observe(&o); o.role = 2; snprintf(o.host_name, sizeof o.host_name, "%s", host);
        if (M.state != MPM_GUEST_JOINING) mpm_step(&M, &o, MPB_NONE);
        o.role = 0; o.failed = 1; snprintf(o.msg, sizeof o.msg, "%s", why);
        o.end = !strcmp(why, "The host left the game") ? MPM_END_HOST_LEFT : !strcmp(why, "Connection to the host lost") ? MPM_END_LINK_LOST   /* test input: English */
              : !strcmp(why, "The host turned spectators off") ? MPM_END_SPECT_OFF : !strncmp(why, "Could not", 9) ? MPM_END_REFUSED : MPM_END_OTHER;
        int a = mpm_step(&M, &o, MPB_NONE); (void)a;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"state\":\"%s\"}", state_name(M.state)); return b.p;
    }
    if (!strcmp(cmd, "mpm_chat_sys")) {                                      /* test: mpmenu_chat_system_line {text, link} (what ui_game's pings call) */
        char t[300] = ""; double lk = 0; jstr(line, "text", t, sizeof t); jnum(line, "link", &lk);
        mpmenu_chat_system_line(t, (int)lk); return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_escape_ours")) { Buf b = {0}; bput(&b, "{\"ok\":true,\"taken\":%d}", mpmenu_escape_ours()); return b.p; }   /* test: Escape in game (the ui lane's hook asks this first) */
    if (!strcmp(cmd, "mpm_join_tcp")) {                                     /* test stand-in for an accepted Steam invite */
        char h[256] = "127.0.0.1"; double port = 0, v; jstr(line, "host", h, sizeof h); jnum(line, "port", &port);
        mp_config c; mp_config_defaults(&c); c.host = 0; snprintf(c.host_addr, sizeof c.host_addr, "%s", h); c.port = (uint16_t)port;
        if (jstr(line, "name", c.name, sizeof c.name)) {} (void)v;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", mp_game_join(&c)); return b.p;
    }
    if (!strcmp(cmd, "mpm_status")) {
        mpm_obs o; observe(&o);
        Buf b = {0};
        bput(&b, "{\"ok\":true,\"state\":\"%s\",\"settings\":{\"pause\":%d,\"speedup\":%d,\"gear\":%d,\"units\":%d,\"admin\":%d,\"events\":%d,\"population\":%d,\"fog\":%d,\"spectators\":%d,\"watching\":%d,\"fe\":%d,\"fe_death\":%d,\"fe_xp\":%d,\"fe_loot\":%d,\"fe_recruit\":%d},\"watch\":%d,\"spectator\":%d,\"msg\":", state_name(M.state),
             M.set.pause_mode, M.set.speedup, M.set.gear, M.set.units, M.set.admin, M.set.events, M.set.population, M.set.fog, M.set.spectators, M.set.watching, M.set.fe, M.set.fe_death, M.set.fe_xp, M.set.fe_loot, M.set.fe_recruit, M.watch, M.spect);
        bstr(&b, M.msg);
        { Obj bv = lbox_view(); char bt[400] = "", bc[64] = "", bx[64] = "";   /* lan_v10: Leave box, notice, last centred message */
          int bon = u_alive(bv) && u_on(bv);
          if (bon) { str_utf8(tmp_get(AT(bv, 0x30, Obj)), bt, sizeof bt); str_utf8(tmp_get(AT(bv, 0x40, Obj)), bc, sizeof bc); str_utf8(tmp_get(AT(bv, 0x50, Obj)), bx, sizeof bx); }
          bput(&b, ",\"confirm_leave\":%d,\"lbox_up\":%d,\"box\":{\"on\":%d,\"text\":", M.confirm_leave, lbox_up, bon); bstr(&b, bt);
          bput(&b, ",\"confirm\":"); bstr(&b, bc); bput(&b, ",\"cancel\":"); bstr(&b, bx); bput(&b, "},\"notice\":"); bstr(&b, M.notice);
          bput(&b, ",\"message\":"); bstr(&b, last_message); }
        bput(&b, ",\"solo\":{\"state\":%d,\"asked\":%llu,\"made\":%llu,\"world\":", solo_st, (unsigned long long)solo_asked_n, (unsigned long long)solo_made_n); bstr(&b, solo_guid); bput(&b, "}");
        bput(&b, ",\"obs\":{\"scene\":%d,\"ready\":%d,\"title\":%d,\"picker\":%d,\"role\":%d,\"guest_loading\":%d,\"failed\":%d,\"esc\":%d},\"visible\":%llu",
             o.scene, o.campaign_ready, o.title_visible, o.picker_open, o.role, o.guest_loading, o.failed, o.esc_open, (unsigned long long)mpm_visible(&M, &o));
        mp_session *ss = mp_game_session();
        bput(&b, ",\"port\":%u,\"host_port\":%u,\"direct\":", ss ? mp_server_port(ss) : 0, (unsigned)mpm_host_port()); bstr(&b, M.direct);
        bput(&b, ",\"inet_state\":%d,\"internet\":", inet_state); bstr(&b, M.internet);
        { uint16_t ue = 0; int us = upnpport_get(&ue); bput(&b, ",\"up_state\":%d,\"up_ext\":%u", us, (unsigned)ue); }   /* router port (upnpport.h) */
        Obj fl = u_gc_get(h_field); char ft[256] = "";
        if (u_alive(fl)) str_utf8((mm_is(fl, K_FIELD) ? F(0x4EFAE84, Obj (*)(Obj, void *))(fl, NULL) : NULL), ft, sizeof ft);
        bput(&b, ",\"field\":{\"exists\":%s,\"active\":%s,\"text\":", u_alive(fl) ? "true" : "false", u_on(fl) ? "true" : "false"); bstr(&b, ft); bput(&b, "}");
        char clip[128] = ""; str_utf8(F(0x518AEA0, Obj (*)(void *))(NULL), clip, sizeof clip);
        bput(&b, ",\"clipboard\":"); bstr(&b, clip);
        put_btns(&b, "title", title_ids, NT, h_tb, 0x130);
        put_btns(&b, "esc", esc_ids, NE, h_eb, 0x28);
        Obj t = u_gc_get(h_etext); char tt[1024] = "";
        if (u_alive(t)) str_utf8(tmp_get(t), tt, sizeof tt);
        bput(&b, ",\"lobby_text_active\":%s,\"lobby_text\":", u_on(t) ? "true" : "false"); bstr(&b, tt);
        { Obj ti = u_gc_get(h_etitle), rl = u_gc_get(h_ereason); char a1[160] = "", a2[160] = "";
          if (u_alive(ti)) str_utf8(tmp_get(ti), a1, sizeof a1); if (u_alive(rl)) str_utf8(tmp_get(rl), a2, sizeof a2);
          bput(&b, ",\"lobby_title\":"); bstr(&b, a1); bput(&b, ",\"lobby_title_active\":%d,\"esc_reason\":", u_on(ti)); bstr(&b, a2);
          bput(&b, ",\"esc_reason_active\":%d", u_on(rl)); }
        bput(&b, ",\"layout\":%s", layout_log[0] ? layout_log : "null");
        bput(&b, ",\"button_rects\":{");
        for (int i = 0, k = 0; i < NE; i++) { float r[4]; Obj x = eb(esc_ids[i]); if (u_on(x) && rect_of(x, r)) bput(&b, "%s\"%d\":[%.0f,%.0f,%.0f,%.0f]", k++ ? "," : "", esc_ids[i], r[0], r[1], r[2], r[3]); }
        bput(&b, "}");
        Obj ev2 = esc_view(); float sr[4];
        bput(&b, ",\"seed_rects\":[");
        for (int k = 0, kk = 0; ev2 && k < 2; k++) { Obj l = AT(ev2, 0x88 + 8 * k, Obj); if (u_on(l) && rect_of(l, sr)) bput(&b, "%s[%.0f,%.0f,%.0f,%.0f]", kk++ ? "," : "", sr[0], sr[1], sr[2], sr[3]); }
        bput(&b, "]");
        { Obj nf = u_gc_get(h_namefield); char nt[128] = ""; field_text(h_namefield, nt, sizeof nt);
          bput(&b, ",\"name_field\":{\"exists\":%s,\"active\":%s,\"text\":", u_alive(nf) ? "true" : "false", u_on(nf) ? "true" : "false"); bstr(&b, nt); bput(&b, "}"); }
        bput(&b, ",\"my_name\":"); bstr(&b, my_name);
        bput(&b, ",\"frames\":%lld,\"connecting\":", (long long)frames); bstr(&b, M.connecting);
        { Obj root = u_gc_get(h_lw_root); char t[1024];                    /* lobby window (v2 §2b) read-back (the note is longer than 256) */
          bput(&b, ",\"lw\":{\"active\":%d,\"area_active\":%d,\"settings_open\":%d,\"title\":", u_on(root), u_on(u_gc_get(h_lw_area)), M.lobby_settings_open);
          t[0] = 0; if (lwt(T_TITLE)) str_utf8(tmp_get(lwt(T_TITLE)), t, sizeof t); bstr(&b, t);
          bput(&b, ",\"guest\":"); t[0] = 0; if (lwt(T_GUEST) && u_on(lwt(T_GUEST))) str_utf8(tmp_get(lwt(T_GUEST)), t, sizeof t); bstr(&b, t);
          bput(&b, ",\"note\":"); t[0] = 0; if (lwt(T_NOTE)) str_utf8(tmp_get(lwt(T_NOTE)), t, sizeof t); bstr(&b, t);
          bput(&b, ",\"conn\":"); t[0] = 0; if (lwt(T_CONN) && u_on(lwt(T_CONN))) str_utf8(tmp_get(lwt(T_CONN)), t, sizeof t); bstr(&b, t);
          { float r1[4] = {0}, r2[4] = {0}; if (lwt(T_INV_ADDR) && u_on(lwt(T_INV_ADDR))) rect_of(lwt(T_INV_ADDR), r1); if (lwt(T_INV_NOTE) && u_on(lwt(T_INV_NOTE))) rect_of(lwt(T_INV_NOTE), r2);
            bput(&b, ",\"addr_rects\":[[%.0f,%.0f,%.0f,%.0f],[%.0f,%.0f,%.0f,%.0f]]", r1[0], r1[1], r1[2], r1[3], r2[0], r2[1], r2[2], r2[3]); }   /* test frames mask this Mac's addresses */
          { Obj ib3 = u_gc_get(h_lwb[LW_INVITE]); char it[160] = ""; if (u_on(ib3)) str_utf8(tmp_get(AT(ib3, 0x28, Obj)), it, sizeof it); bput(&b, ",\"invite\":"); bstr(&b, it); }
          bput(&b, ",\"addr\":"); t[0] = 0; if (lwt(T_INV_ADDR) && u_on(lwt(T_INV_ADDR))) str_utf8(tmp_get(lwt(T_INV_ADDR)), t, sizeof t); bstr(&b, t);
          bput(&b, ",\"inv_note\":"); t[0] = 0; if (lwt(T_INV_NOTE) && u_on(lwt(T_INV_NOTE))) str_utf8(tmp_get(lwt(T_INV_NOTE)), t, sizeof t); bstr(&b, t);   /* test: "open port N" line */
          bput(&b, ",\"rows\":[");
          for (int i = 0; i < LW_ROWS; i++) {
              char n[400] = "", st[64] = ""; int lit = 0;
              if (u_gc_get(h_lw_name[i])) str_utf8(tmp_get(u_gc_get(h_lw_name[i])), n, sizeof n);
              if (u_gc_get(h_lw_stat[i])) str_utf8(tmp_get(u_gc_get(h_lw_stat[i])), st, sizeof st);
              for (int k = 0; k < 3; k++) lit += u_on(u_gc_get(h_lw_bar[i][k]));
              bput(&b, "%s{\"name\":", i ? "," : ""); bstr(&b, n); bput(&b, ",\"status\":"); bstr(&b, st);
              bput(&b, ",\"bars_shown\":%d,\"level\":%d,\"x\":%d}", lit, lw_level[i], u_on(u_gc_get(h_lw_x[i])));
          }
          bput(&b, "],\"buttons\":{");
          for (int i = 0, k = 0; i < LW_NB; i++) { Obj x = u_gc_get(h_lwb[i]); float r[4]; if (u_on(x) && rect_of(x, r)) bput(&b, "%s\"%d\":[%.0f,%.0f,%.0f,%.0f]", k++ ? "," : "", lw_ids[i], r[0], r[1], r[2], r[3]); }
          float wr[4] = {0}; if (u_on(root)) rect_of(root, wr);
          bput(&b, "},\"rect\":[%.0f,%.0f,%.0f,%.0f]}", wr[0], wr[1], wr[2], wr[3]); }
        bput(&b, ",\"teams_modal\":%d", teams_modal());
        bput(&b, ",\"rules_panel\":{\"title\":%d,\"lobby\":%d,\"fe\":%d", u_on(u_gc_get(rp_title.root)), u_on(u_gc_get(rp_lobby.root)), u_on(u_gc_get(rp_fe.root)));
        { int rows = 0; for (int i = 0; i < MPM_RULE_ROWS; i++) rows += u_on_tree(u_gc_get(rp_title.row[i]));   /* owner v12 item 5: open, no Done */
          bput(&b, ",\"title_rows\":%d,\"done\":%d,\"fe_done\":%d}", rows, u_on_tree(tb(MPB_RULES_DONE)), u_on_tree(tb(MPB_FE_DONE))); }
        { Obj tv = title_view(); float r[4]; int k = 0;                    /* owner v12 item 4: the drawn frames of the column, screen px */
          bput(&b, ",\"frames\":{");
          for (int i = 0; i < NT; i++) { Obj x = tb(title_ids[i]), im = x ? first_image(u_tr(x), 0) : NULL;
              if (u_on(x) && rect_of(im ? im : x, r)) bput(&b, "%s\"%d\":[%.1f,%.1f,%.1f,%.1f]", k++ ? "," : "", title_ids[i], r[0], r[1], r[2], r[3]); }
          bput(&b, "},\"stock_frames\":["); k = 0;
          for (int off = 0x30; tv && off <= 0x60; off += 8) { Obj x = AT(tv, off, Obj), im = u_alive(x) ? first_image(u_tr(x), 0) : NULL;
              if (u_on(x) && rect_of(im ? im : x, r)) bput(&b, "%s[%.1f,%.1f,%.1f,%.1f]", k++ ? "," : "", r[0], r[1], r[2], r[3]); }
          bput(&b, "]"); }
        { Obj bk = tb(MPB_BACK), bg = bk ? first_image(u_tr(bk), 0) : NULL;
          static void *mr; if (!mr) mr = mth("UnityEngine.UI", "Graphic", "get_raycastTarget", 0);
          bput(&b, ",\"back_click_target\":%d", bg && mr && mm_is(bg, K_GR) ? (int)MCALL(mr, bool, Obj, const void *)(bg, mr) : -1); }
        { bput(&b, ",\"hist\":{\"n\":%d,\"panel\":%d,\"rows\":[", nhist, u_on(u_gc_get(h_rw_root)));
          for (int i = 0; i < nhist && i < 3; i++) {
              hist_t *h = &hist[i]; bput(&b, "%s{\"world\":", i ? "," : ""); bstr(&b, h->world); bput(&b, ",\"day\":%lld,\"how\":", h->day); bstr(&b, h->how);
              bput(&b, ",\"addr\":"); bstr(&b, h->addr); bput(&b, ",\"host\":"); bstr(&b, h->host); bput(&b, ",\"players\":"); bstr(&b, h->players);
              bput(&b, ",\"units\":"); bstr(&b, h->units); bput(&b, ",\"portraits\":"); bstr(&b, h->portraits);
              int shown = 0; for (int q = 0; q < 6; q++) shown += u_on(u_gc_get(h_rw_pp[i < 6 ? i : 0][q])); bput(&b, ",\"portraits_shown\":%d}", shown); }
          bput(&b, "]}"); }
        { bput(&b, ",\"chat\":{\"typing\":%d,\"buf\":", chat_typing); bstr(&b, chat_buf);
          int idle = 0; float a0 = 0; for (int i = 0; i < CH_IDLE; i++) if (u_on(u_gc_get(h_ch_idle[i]))) idle++;
          if (nch) { double age = mono_s() - chist[nch - 1].t; a0 = age < chat_fade_s ? 1 : age < chat_fade_s + 1 ? (float)(chat_fade_s + 1 - age) : 0; }
          bput(&b, ",\"lines\":%d,\"idle_shown\":%d,\"newest_alpha\":%.2f,\"box\":%d,\"hotkeys_blocked\":%d,\"last\":[", nch, idle, a0, u_on(u_gc_get(h_ch_box)),
               (int)F(0xD42BBC, bool (*)(void *))(NULL));
          for (int i = nch > 4 ? nch - 4 : 0, k = 0; i < nch; i++, k++) { bput(&b, "%s", k ? "," : ""); bstr(&b, chist[i].text); }
          bput(&b, "],\"over\":%d,\"link_clicks\":%d,\"links\":[", mpmenu_chat_over(), chat_link_clicks);
          for (int i = 0, w = 0; i < nchat_shown; i++) { int k = chat_shown_k[i]; float r[4]; Obj t = u_gc_get(*chat_shown_h[i]);
              if (chist[k].link && u_on(t) && rect_of(t, r)) bput(&b, "%s{\"link\":%d,\"rect\":[%.0f,%.0f,%.0f,%.0f]}", w++ ? "," : "", chist[k].link, r[0], r[1], r[2], r[3]); }
          float cr[4] = { 0 }; Obj cb = u_gc_get(h_ch_box); if (u_on(cb)) rect_of(cb, cr); else { Obj t0 = u_gc_get(h_ch_idlebg[CH_IDLE - 1]); if (u_on(t0)) rect_of(t0, cr); }
          static void *msw, *msh; if (!msw) { msw = mth("UnityEngine", "Screen", "get_width", 0); msh = mth("UnityEngine", "Screen", "get_height", 0); }
          { Obj f = u_gc_get(h_ch_field), sel = NULL; char sn[64] = "";
            static void *mcur, *msel, *mfoc, *mint; if (!mcur) { mcur = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0); msel = mth("UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0);
                mfoc = mth("TMPro", "TMP_InputField", "get_isFocused", 0); mint = mth("UnityEngine.UI", "Selectable", "IsInteractable", 0); }
            Obj es = mcur ? MCALL(mcur, Obj, const void *)(mcur) : NULL; if (u_alive(es) && msel) sel = MCALL(msel, Obj, Obj, const void *)(es, msel);
            if (u_alive(sel)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sel, NULL), sn, sizeof sn);
            char ft[DSNET_CHAT_MAX + 1] = ""; if (u_alive(f)) utf16_to_utf8(F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL), ft, sizeof ft);
            bput(&b, "],\"typing_line\":"); bstr(&b, typing_shown);
          bput(&b, ",\"field\":{\"alive\":%d,\"on\":%d,\"es\":%d,\"selected\":", u_alive(f), u_on(f), u_alive(es)); bstr(&b, sn);
            bput(&b, ",\"is_field\":%d,\"focused\":%d,\"allow_input\":%d,\"activate_next\":%d,\"interactable\":%d,\"readonly\":%d,\"text\":",
                 u_alive(f) && u_alive(sel) && sel == u_go(f), u_alive(f) && mfoc ? (int)MCALL(mfoc, bool, Obj, const void *)(f, mfoc) : -1,
                 u_alive(f) ? AT(f, 0x260, bool) : -1, u_alive(f) ? AT(f, 0x261, bool) : -1, u_alive(f) && mint ? (int)MCALL(mint, bool, Obj, const void *)(f, mint) : -1, u_alive(f) ? AT(f, 0x220, bool) : -1);
            bstr(&b, ft); bput(&b, ",\"input_chars\":%d,\"input_enters\":%d,\"input_a\":%d}", dbg_istr_n, dbg_enter_n, dbg_a_n); }
          bput(&b, ",\"rows\":[");
          for (int i = 0; i < nchat_shown; i++) { int k = chat_shown_k[i]; float r[4] = { 0 }, pr[4] = { 0 }; Obj t = u_gc_get(*chat_shown_h[i]); rect_of(t, r);
              int idle = chat_shown_h[i] >= &h_ch_idle[0] && chat_shown_h[i] < &h_ch_idle[CH_IDLE];
              if (idle) rect_of(u_gc_get(h_ch_idlebg[chat_shown_h[i] - &h_ch_idle[0]]), pr);
              float pv = 0; if (fit_methods() && u_on(t)) pv = fit_pref(t, ch_line_w[idle ? 0 : 1]).y;
              char tx[CH_TEXT] = ""; str_utf8(tmp_get(t), tx, sizeof tx);
              bput(&b, "%s{\"k\":%d,\"idle\":%d,\"rect\":[%.0f,%.0f,%.0f,%.0f],\"plate\":[%.0f,%.0f,%.0f,%.0f],\"h\":%.1f,\"pref_h\":%.1f,\"font\":%d,\"missing\":%d,\"text\":",
                   i ? "," : "", k, idle, r[0], r[1], r[2], r[3], pr[0], pr[1], pr[2], pr[3], idle ? chist[k].hi : chist[k].hb, pv, chist[k].font, chist[k].missing); bstr(&b, tx); bput(&b, "}"); }
          bput(&b, "],\"fonts\":[");
          for (int i = 0; i < nchf; i++) { bput(&b, "%s", i ? "," : ""); bstr(&b, chf_names[i]); }
          { Obj cv = u_gc_get(h_ch_cv); static void *mgo; if (!mgo) mgo = mth("UnityEngine", "Canvas", "get_sortingOrder", 0);
            bput(&b, "],\"canvas\":{\"alive\":%d,\"order\":%d},\"wheel_blocked\":%d,\"over_now\":%d,\"scroll\":%d,\"oldest\":%d", u_alive(cv),
                 u_alive(cv) && mgo ? MCALL(mgo, int, Obj, const void *)(cv, mgo) : -1, ch_wheel_blocked, ch_over_now, chat_scroll, ch_box_oldest);
            Obj cam = F(0x5083208, Obj (*)(void *))(NULL);                  /* Camera.main: the zoom (A4 test) */
            bput(&b, ",\"cam_ortho\":%.3f", u_alive(cam) ? F(0x50804FC, float (*)(Obj, void *))(cam, NULL) : -1.0f); }
          bput(&b, ",\"size\":[%.0f,%.0f],\"drag\":%d,\"fake\":%d,\"drags\":[%u,%u,%u],\"line_w\":[%.0f,%.0f]", chat_w, chat_h, ch_drag, ch_fk.on,
               ch_drags[CHD_MOVE], ch_drags[CHD_SIZE], ch_drags[CHD_HEIGHT], ch_line_w[0], ch_line_w[1]);
          { mp_session *rs = mp_game_session();                             /* read state (owner 2026-10-25): screen rects of its parts, px, y up */
            bput(&b, ",\"read\":%d,\"ro\":%d,\"clears\":%u,\"thumb_drags\":%u,\"esc_reg\":%d,\"sb\":{\"S\":%d,\"vis\":%d", chat_read, rs ? 0 : 1, ch_clears,
                 ch_drags[CHD_THUMB], esc_chat.id != NULL, ch_sb.S, ch_sb.vis);
            static const char *const pn[] = { "track", "thumb", "top", "bot", "bin", "close", "field" };
            for (int i = 0; i < 7; i++) { Obj po = i == 6 ? u_gc_get(h_ch_field) : u_gc_get(h_ch_lay[CL_TRACK + i]); float q[4] = { 0 };
                int on = u_on(po) && u_on(u_gc_get(h_ch_box)) && rect_of(po, q);
                bput(&b, ",\"%s\":%s", pn[i], on ? "" : "null"); if (on) bput(&b, "[%.1f,%.1f,%.1f,%.1f]", q[0], q[1], q[2], q[3]); }
            bput(&b, "}"); }
          bput(&b, ",\"fade\":%d,\"pos\":[%.0f,%.0f],\"rect\":[%.0f,%.0f,%.0f,%.0f],\"screen\":[%d,%d]}", chat_fade_s, chat_x, chat_y, cr[0], cr[1], cr[2], cr[3],
               msw ? MCALL(msw, int, const void *)(msw) : 0, msh ? MCALL(msh, int, const void *)(msh) : 0); }
        { rpanel *rp = u_on(u_gc_get(rp_lobby.root)) ? &rp_lobby : &rp_title;
          char *pr = ui_tip_exec("ui_tip", "{\"action\":\"probe\"}");          /* the game window's state (shown / tips_on) */
          bput(&b, ",\"tip\":{\"title\":"); bstr(&b, tip_now); bput(&b, ",\"rules_row\":%d,\"ready\":%d,\"game\":%s}", rp->tip_row, tip_ready(), pr ? pr : "null"); free(pr); }
        bput(&b, ",\"cog\":{\"title\":%d,\"title_pixel\":%d,\"lobby\":%d}", u_on(u_gc_get(h_title_cog)), u_on(u_gc_get(h_title_pcog)), u_on(u_gc_get(h_lw_cog)) || u_on(u_gc_get(h_lw_pcog)));
        { mp_session *as = mp_game_session(); bput(&b, ",\"admin\":%d", as ? dsnet_admin_mode(mp_client(as)) : -1);
          if (as) { dsnet_client *ac = mp_client(as); dsnet_rules r; dsnet_rules_get(ac, &r); int me = dsnet_player_id(ac);
                    bput(&b, ",\"rules\":{\"pause\":%d,\"speedup\":%d,\"gear\":%d,\"units\":%d,\"events\":%d,\"population\":%d},\"owned\":%d,\"refused\":%u",
                         r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player, r.event_choice, r.population, dsnet_owned_count(ac, me), dsnet_claim_refusals(ac, me)); } }
        bput(&b, ",\"notice_pending\":"); bstr(&b, M.notice);
        bput(&b, ",\"host_lobby\":%d,\"lobby_open\":%d", o.host_lobby, mpmenu_lobby_open());
        { mp_session *hs = mp_game_session(); int nl = 0, r;
          for (int i = 0, n = hs ? dsnet_hold_count(mp_client(hs)) : 0; i < n; i++) if (dsnet_hold_get(mp_client(hs), i, &r, NULL, NULL) == 0 && r == DSNET_HOLD_LOBBY) nl++;
          bput(&b, ",\"lobby_holds\":%d", nl); }
        { Obj cn = find_view_x(&h_cnv, &k_cnv, "Refactor.UI", "CommonNoticeView", true); char nt[400] = "";
          if (cn) str_utf8(tmp_get(AT(cn, 0x30, Obj)), nt, sizeof nt);
          bput(&b, ",\"notice_box\":{\"exists\":%d,\"active\":%d,\"text\":", cn != NULL, u_on(cn)); bstr(&b, nt); bput(&b, "}"); }
        { Obj slv = find_view_x(&h_slv, &k_slv, "Refactor", "SaveLoadView", true); char t1[128] = "", t2[64] = "";
          if (slv && u_alive(AT(slv, 0x48, Obj))) str_utf8(tmp_get(AT(slv, 0x48, Obj)), t1, sizeof t1);
          Obj lb = slv ? AT(slv, 0xA0, Obj) : NULL, bt = u_alive(lb) ? tmp_below(u_tr(lb), 0) : NULL; if (bt) str_utf8(tmp_get(bt), t2, sizeof t2);
          bput(&b, ",\"load_list\":{\"title\":"); bstr(&b, t1); bput(&b, ",\"button\":"); bstr(&b, t2); bput(&b, "}"); }
        { Obj ev3 = esc_view(); Obj lb = ev3 ? AT(ev3, 0x60, Obj) : NULL; char lt[160] = "";
          if (u_alive(lb)) str_utf8(tmp_get(AT(lb, 0x28, Obj)), lt, sizeof lt);
          Obj btn = u_alive(lb) ? AT(lb, 0x20, Obj) : NULL;
          bput(&b, ",\"esc_load\":{\"text\":"); bstr(&b, lt);
          bput(&b, ",\"interactable\":%d}", btn ? (int)(mm_is(btn, K_SEL) ? F(0x549C6AC, bool (*)(Obj, void *))(btn, NULL) : false) : -1); }
        Obj tv = title_view();
        bput(&b, ",\"stock_title\":[");
        for (int off = 0x30; tv && off <= 0x60; off += 8) bput(&b, "%s%d", off > 0x30 ? "," : "", u_on(AT(tv, off, Obj)));
        bput(&b, "]}");
        return b.p;
    }
    return NULL;
}
