// mu_ui.h - shared pieces of the My Units and Bring Units windows: a game-style window on the kit's screen layer that fits
// the screen at every UI scale, texts that never spill (ellipsis), game icons, the Compact Plus card, a frame tick.
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

/* the Unit Builder's door (lane builder, PLAN_V13 section 4): NULL until that lane is in the build */
#ifdef _WIN32
#define MU_WEAK_IMPORT __attribute__((weak))
#else
#define MU_WEAK_IMPORT __attribute__((weak_import))      /* + -Wl,-U,_cm_builder_open (build_module.sh) */
#endif
extern void cm_builder_open(const char *unit_id_or_null, void (*saved)(const char *unit_id, void *ud), void *ud) MU_WEAK_IMPORT;
int mu_builder_present(void);

const DSLUi *mu_kit(void);
void mu_ui_init(const DSLHost *host, DSLPlugin *self);    /* cm_units_init */
void mu_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* the window: a panel centred on the kit's screen layer, w x h clamped to the layer minus a margin. title in Cinzel.
   Returns the panel (NULL = kit not ready); *w / *h get the size used. */
float mu_ui_scale(void);
DSLObj mu_window(const char *title, float want_w, float want_h, float *w, float *h);
/* a single-line text that ends in "..." when it does not fit (never spills); flags DSL_TXT_*; WRAP = a wrapping box
   (no ellipsis, the caller sizes it) */
DSLObj mu_text(DSLObj parent, float x, float y, float w, float h, const char *s, float size, int flags, uint32_t rgba);
/* a kit button whose label ends in "..." when it does not fit */
DSLObj mu_button(DSLObj parent, float x, float y, float w, float h, const char *label, dsl_ui_cb cb, void *ud);
/* a game icon (cm_il.h CM_ICON_*), square s; tip = hover title (NULL none). dim = drawn faded (not invested).
   No sprite for the key: a small dark square (the slot is kept) */
DSLObj mu_icon(DSLObj parent, float x, float y, float s, int kind, const char *key, int dim, const char *tip);
/* the game's name of a key (TEXTKEY_<key>_NAME, then the key without its AFFECTER_ / prefix) */
void mu_name_of(const char *key, char *out, size_t cap);

/* Compact Plus card (owner 2026-10-15) drawn at x, y in parent, width w; returns its root panel, *h the height used.
   name / level: what to show (a save unit's localised name; NULL = the card's own). portrait_key overrides the card's
   avatar (a save's own face). bar: the skill bar to show (NULL = the card's own bar). */
#define MU_CARD_H 132.f
DSLObj mu_card_draw(DSLObj parent, float x, float y, float w, const mu_card *c, const char *name, int level,
                    const char *portrait_key, const char *const *bar, int nbar);

/* frame tick: fn runs on the game's main thread between frames every ~ms while registered (mu_clip.c, OS timer) */
int  mu_tick_add(void (*fn)(void));
void mu_tick_remove(void (*fn)(void));
int  mu_os_timer(void (*fn)(void), int ms);               /* mu_clip.c: the OS main-thread timer behind mu_tick */

/* test support (overflow test): every text / icon drawn into cards since the last reset, with its card */
typedef struct mu_drawn { DSLObj o; int card, is_text; } mu_drawn;
int  mu_drawn_list(const mu_drawn **out);
DSLObj mu_drawn_card(int i);
void mu_drawn_reset(void);
