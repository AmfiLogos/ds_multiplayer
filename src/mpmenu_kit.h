// mpmenu_kit.h - the Multiplayer menu's building blocks, shared with the Custom Modes windows (custom/fe/fe_menu.c,
// custom/pvp/pvp_ui.c). M4 split (research/MASTER_PLAN.md 2.2 B): those windows were #included into mpmenu.c; now they are
// compiled in custom-modes-mp.dylib and use these exported helpers of multiplayer.dylib (two-level link, see
// custom/build_module.sh). Types, class kinds, sprite ids and colours as in mpmenu.c before the split (moved, not changed).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rig.h"
#include "mpmenu.h"

/* class kinds for typed calls (mm_is) */
enum { K_RT, K_TR, K_COMP, K_GO, K_TMP, K_FIELD, K_GR, K_IMG, K_BEH, K_SEL, K_SMB, K_SMV, K_TMV, K_CNV, K_N };
int mm_is(Obj o, int k);

/* ---------------------------------------------------------------- Unity / il2cpp (RVAs as ui_game.c, ~/ds_multibar game.h) */
static inline Obj u_str(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }
static inline uintptr_t u_gc_new(Obj o) { return o ? F(0x6F9540, uintptr_t (*)(Obj, bool))(o, false) : 0; }
static inline Obj u_gc_get(uintptr_t h) { return h ? F(0x6F9598, Obj (*)(uintptr_t))(h) : NULL; }
static inline void u_gc_free(uintptr_t h) { if (h) F(0x6F95E4, void (*)(uintptr_t))(h); }
static inline bool u_alive(Obj o) { return o && F(0x50FAF88, bool (*)(Obj, void *))(o, NULL); }
static inline Obj u_go(Obj comp) { return (mm_is(comp, K_COMP) ? F(0x50ED970, Obj (*)(Obj, void *))(comp, NULL) : NULL); }
static inline Obj u_tr(Obj comp) { return (mm_is(comp, K_COMP) ? F(0x50ED864, Obj (*)(Obj, void *))(comp, NULL) : NULL); }
static inline void u_go_active(Obj go, bool v) { if (go) (mm_is(go, K_GO) ? F(0x50F3520, void (*)(Obj, bool, void *))(go, v, NULL) : (void)0); }
static inline bool u_go_active_self(Obj go) { return go && (mm_is(go, K_GO) ? F(0x50F3638, bool (*)(Obj, void *))(go, NULL) : false); }
static inline void u_active(Obj comp, bool v) { if (u_alive(comp) && u_go_active_self(u_go(comp)) != v) u_go_active(u_go(comp), v); }
static inline bool u_on(Obj comp) { return u_alive(comp) && u_go_active_self(u_go(comp)); }
static inline Obj u_instantiate(Obj orig, Obj parent_tr) { return F(0x50FC30C, Obj (*)(Obj, Obj, void *))(orig, parent_tr, NULL); }
static inline Obj u_tr_parent(Obj t) { return (mm_is(t, K_TR) ? F(0x510992C, Obj (*)(Obj, void *))(t, NULL) : NULL); }
static inline int u_sibling(Obj t) { return (mm_is(t, K_TR) ? F(0x510F070, int (*)(Obj, void *))(t, NULL) : 0); }
static inline void u_set_sibling(Obj t, int i) { (mm_is(t, K_TR) ? F(0x510EE44, void (*)(Obj, int, void *))(t, i, NULL) : (void)0); }
typedef struct { float x, y; } V2;
typedef struct { float x, y, w, h; } RectF2;
typedef struct { float x, y, z; } V3;
typedef struct { float r, g, b, a; } Col;

/* stock sprites by name (assets_scan) */
enum { S_FRAME, S_COG, S_TOG_ON, S_TOG_OFF, S_PREV, S_NEXT, S_TGL_N, S_TGL_S, S_RECRUIT, S_BOTTOM, S_BOX, S_DIV2, S_DIV, S_SLOT1, S_SLOT2, S_NBTN, S_CHK_N, S_CHK_C, S_N };

/* colours */
static const Col C_MAIN = { 0.8f, 0.745f, 0.722f, 1 }, C_SUB = { 0.651f, 0.565f, 0.522f, 1 }, C_WHITE = { 1, 1, 1, 1 },
                 C_ORANGE = { 0.918f, 0.624f, 0.239f, 1 };                  /* #ccbeb8, #a69085, -, #ea9f3d */
/* approved mockups (dsdesign project: Main / LobbyGuest / HostPageA / HostPageB), owner 2026-10-13. Mockup px = canvas units. */
static const Col C_BAND = { 0.176f, 0.165f, 0.192f, 1 }, C_ROW1 = { 0.212f, 0.196f, 0.227f, 1 }, C_ROW2 = { 0.169f, 0.157f, 0.184f, 1 },
                 C_LINE = { 0.388f, 0.239f, 0.2f, 1 }, C_COPPER = { 0.714f, 0.514f, 0.353f, 1 }, C_GOLD = { 0.851f, 0.745f, 0.561f, 1 },
                 C_START = { 0.918f, 0.851f, 0.812f, 1 }, C_UNLIT = { 0.29f, 0.27f, 0.314f, 1 };

/* ---------------------------------------------------------------- exported helpers (mpmenu.c) */
void *class_by_name(const char *ns, const char *name);
Obj find_view_x(uintptr_t *h, void **kcache, const char *ns, const char *name, bool inactive);
Obj title_view(void);
Obj esc_view(void);
void popup_activate(int type);
Obj image_of(Obj comp);
void str_utf8(Obj s, char *out, size_t cap);
void utf16_to_utf8(Obj s, char *out, size_t cap);
void tmp_set(Obj t, const char *s);
bool das_key_on(Obj t);                          /* DASText._useTextKey (by name: the offset differs on Windows) */
void das_key_set(Obj t, bool on);
void das_key_clear(Obj t);                       /* _useTextKey off and _textKey null: our own text stays */
void txt_style(Obj t, int cinzel, float size, int align, int wrap);
void txt_color(Obj t, Col c);
void img_set(Obj im, Obj sprite, Col c);
void place(Obj comp, float x, float y, float w, float h);
int rect_of(Obj comp, float r[4]);
Obj mk_img(Obj tpl_img, Obj parent_tr, int sprite, Col c, float x, float y, float w, float h);
Obj mk_txt(Obj tpl_txt, Obj parent_tr, int cinzel, float size, int align, int wrap, float x, float y, float w, float h, Col c);
Obj mk_button(Obj src, Obj parent, int esc, int sprite, Col tint, float x, float y, float w, float h, float fs, const char *label, Col tc);
Obj btn_label(Obj b, int esc);
Obj first_image(Obj tr, int depth);
Obj eb(int id);
Obj portrait_sprite(const char *name);
int tip_try(Obj o, const char *title, const char *body);
void assets_scan_now(void);

/* ---------------------------------------------------------------- exported state (mpmenu.c) */
extern mpm_model M;
extern int64_t frames;
extern int scan_done;
extern char my_name[64];
extern char tip_now[64];
extern uintptr_t h_tb[MPB_COUNT];                       /* title clones by button id */
extern uintptr_t h_lwr[MPB_COUNT];                      /* Esc-menu (lobby) clones by button id */
extern uintptr_t h_lw_root;                             /* the lobby window */

/* ---------------------------------------------------------------- menu hooks (Custom Modes windows in this menu)
   mp_api register_menu (mp_api.h). Every callback may be NULL. Called in registration order at the place the
   included windows were called before the split. */
typedef struct mp_menu_hooks {
    int  (*click)(int c);                                /* a queued click id (pending_click), before the model step: 1 = taken */
    int  (*escape)(void);                                /* Escape (mpmenu_escape_ours): a click id to queue, 0 = not ours */
    void (*action)(int a);                               /* model actions (MPA_*) before the menu's own */
    void (*frame)(const mpm_obs *o);                     /* after title / Esc sync */
    int  (*lobby_bring)(void);                           /* the lobby's Bring Units button shows (lobby states only) */
    int  (*lobby_name)(int player, const char *name, char *out, size_t cap);   /* lobby row name text: 1 = out replaces it */
    void (*tips)(void);                                  /* tooltips of its widgets */
    void (*frame_late)(const mpm_obs *o);                /* end of the menu frame */
    int  (*press)(Obj btn);                              /* SubUI_SystemMenuButton.OnPressButton: 1 = its button */
    char *(*exec)(const char *cmd, const char *line);    /* mpm_* test commands (NULL = not its command) */
} mp_menu_hooks;
int mpmenu_register_hooks(const mp_menu_hooks *h);
int mpmenu_register_esc_button(const char *label, const char *tip, int (*visible)(void), void (*click)(void));
