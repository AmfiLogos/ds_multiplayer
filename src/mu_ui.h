// mu_ui.h - pieces of the Bring Units card window (mu_bring.c): a game-style window on the loader kit's screen layer that
// fits the screen at every UI scale, texts that never spill (ellipsis), game icons, the Compact Plus card, a frame tick.
// Copy of ds_custommodes units/mu_ui.h (Units Laboratory keeps its own for My Units; no link between the two images).
#pragma once
#include "dsl_sdk.h"
#include "mu_card.h"

/* colours (mpmenu_kit.h's palette, approved mockups) as dsl_ui rgba */
#define MU_C_MAIN   0xCCBEB8FFu
#define MU_C_SUB    0xA69085FFu
#define MU_C_GOLD   0xD9BE8FFFu
#define MU_C_COPPER 0xB6835AFFu
#define MU_C_ROW    0x36323AFFu
#define MU_C_ROW2   0x2B2830FFu
#define MU_C_BAND   0x2D2A31FFu
#define MU_C_BAD    0xE06A5AFFu
#define MU_C_BODY   0x222025FFu                 /* window body (mpmenu.c lobby h_lw_body) */
#define MU_C_WELL   0x161419FFu                 /* the card list's background */

const DSLUi *mu_kit(void);
const DSLHost *mu_host(void);
void mu_ui_init(const DSLHost *host, DSLPlugin *self);    /* the loader host of this module (mp_cards_host); NULL kit = no card window */
void mu_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* the window: a panel centred on the kit's screen layer, w x h clamped to the layer minus a margin. title in Cinzel.
   Returns the panel (NULL = kit not ready); *w / *h get the size used. */
float mu_ui_scale(void);
DSLObj mu_window(const char *title, float want_w, float want_h, float *w, float *h);
/* a plain colour rect, no sprite, clicks through (rgba 0: invisible, a holder for other widgets) */
DSLObj mu_plate(DSLObj parent, float x, float y, float w, float h, uint32_t rgba);
/* a single-line text that ends in "..." when it does not fit (never spills); flags DSL_TXT_*; WRAP = a wrapping box
   (no ellipsis, the caller sizes it) */
DSLObj mu_text(DSLObj parent, float x, float y, float w, float h, const char *s, float size, int flags, uint32_t rgba);
/* a kit button whose label ends in "..." when it does not fit */
DSLObj mu_button(DSLObj parent, float x, float y, float w, float h, const char *label, dsl_ui_cb cb, void *ud);
/* a game icon (cm_il.h CM_ICON_*), square s; tip = hover title (NULL none). dim = drawn faded (not invested).
   No sprite for the key: a small dark square (the slot is kept) */
DSLObj mu_icon(DSLObj parent, float x, float y, float s, int kind, const char *key, int dim, const char *tip);
/* mu_tip.c: the game's own hover card (TooltipUI: Tooltip_Item / _Skill / _Affecter) on target for a game key. 1 = done,
   0 = no card for this kind (CM_ICON_TREE / _PORTRAIT) or the game's tooltip is not there (caller: the kit's name box) */
int  mu_tip_game(DSLObj target, int kind, const char *key);
void mu_tip_hide(void);
void mu_tip_forget(DSLObj target);                         /* before the icon is destroyed (a page built again) */
DSLObj mu_tip_test_target(int i);
size_t mu_tip_test_state(char *out, size_t cap);
/* the game's name of a key (TEXTKEY_<key>_NAME, then the key without its AFFECTER_ / prefix) */
void mu_name_of(const char *key, char *out, size_t cap);

/* Compact Plus card (owner 2026-10-15) drawn at x, y in parent, width w; returns its root panel, *h the height used.
   name / level: what to show (a save unit's localised name; NULL = the card's own). portrait_key overrides the card's
   avatar (a save's own face). bar: the skill bar to show (NULL = the card's own bar). */
#define MU_CARD_H 128.f                        /* 4 cards fit the My Units list at UI scale 1 (owner 2026-10-17) */
DSLObj mu_card_draw(DSLObj parent, float x, float y, float w, const mu_card *c, const char *name, int level,
                    const char *portrait_key, const char *const *bar, int nbar);
/* compact card for the Bring Units window (owner 2026-10-20: "about 8 units at once instead of 3"): one row of
   MU_CARD_CH: face, name, "Lv n  race / background / weapon", then two icon rows (worn items | skill bar; branches |
   milestones | passives | inscriptions | soul stones | traits), "+N" when a row is full. No in-game character picture. */
#define MU_CARD_CH 58.f
DSLObj mu_card_draw_compact(DSLObj parent, float x, float y, float w, const mu_card *c, const char *name, int level,
                            const char *portrait_key, const char *const *bar, int nbar);

/* frame tick: fn runs on the game's main thread between frames every ~ms while registered (mu_clip.c, OS timer) */
int  mu_tick_add(void (*fn)(void));
void mu_tick_remove(void (*fn)(void));
int  mu_os_timer(void (*fn)(void), int ms);               /* mu_timer.c: the OS main-thread timer behind mu_tick */

/* test support (overflow test): every text / icon drawn into cards since the last reset, with its card */
typedef struct mu_drawn { DSLObj o; int card, is_text; } mu_drawn;
int  mu_drawn_list(const mu_drawn **out);
DSLObj mu_drawn_card(int i);
void mu_drawn_reset(void);
int  mu_drawn_sprites(void);                               /* card icons drawn with a game sprite since the reset */
void mp_cards_host(const DSLHost *host, DSLPlugin *self);  /* see mu_ui.c */
