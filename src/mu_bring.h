// mu_bring.h - Bring Units: the card window of Foreign Expedition and PvP (owner 2026-10-20: "port the Bring units card
// window into the multiplayer mod"). From ds_custommodes units/mu_bring.h (removed there at 5454e03). Left: the player's
// saves; right: Compact Plus cards (the game's own icons) with a tick each; a per-player cap. With Units Laboratory
// installed (E3, owner 2026-10-20) a Saves / My Units switch and a "Build a unit" button (Unit Builder) come in; without it
// the window has saves only. Needs the loader's widget kit (mu_ui_init with a host that has one): without it mu_bring_open
// returns -1 and the caller shows its text list. Everything runs on the game's main thread; `done` is called between
// frames, once, after the window closed.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mu_pick {
    char save_file[1024];        /* full path of the save file the unit is in */
    char unit_guid[40];          /* the unit's guid in that save (36 chars + 0) */
    char name[96];               /* display name */
    int level;
    char lib_id[17];             /* E3: a Units Laboratory My Units unit (its id; save_file empty), "" = a save unit */
} mu_pick;

/* the saves to offer. NULL list_saves = the game's own Saves folder of this player (read only), newest first. A save the
   caller lists is read only, never written by this window. */
typedef struct mu_save_ref { char path[1024]; char name[128]; char when[32]; } mu_save_ref;

typedef struct mu_bring_req {
    const char *title;           /* window title (NULL = "Bring Units") */
    const char *subtitle;        /* one line under the title (rules, e.g. "Up to 4 units. Death: Off"), or NULL */
    int max_units;               /* per player, >= 1 */
    const mu_pick *ticked;       /* picks to show as already ticked (re-opening the window), or NULL */
    int nticked;
    int (*list_saves)(mu_save_ref *out, int max, void *ud);   /* optional, see above */
    /* closed: Bring = picks (1 <= n <= max_units), Cancel / Escape / mu_bring_close = n == -1 */
    void (*done)(const mu_pick *picks, int n, void *ud);
    void *ud;
} mu_bring_req;

/* 0 = the window opened; -1 = another Bring Units window is open, the widget kit is missing, or bad arguments */
int  mu_bring_open(const mu_bring_req *req);
void mu_bring_close(void);       /* closes it like Cancel (done(NULL, -1)) */
int  mu_bring_is_open(void);
int  mu_bring_hit(float x, float y);   /* M7: screen point (pixels, y up) on the open window: our HUD under it leaves it alone */
int  mu_bring_available(void);   /* 1 = the loader's widget kit is there (the card window can open) */
/* E3 (owner 2026-10-20): Units Laboratory, when this player has it (found at run time, no link): the window gets a source
   switch Saves / My Units and a "Build a unit" button; a My Units pick travels as its full entry (others need nothing). */
int   mu_ul_present(void);
char *mu_ul_entry_json(const char *id, const char *player_uid, char *err, size_t errn);   /* malloc'd (mu_ul_free) */
void  mu_ul_free(char *s);

/* tests (rig commands mpm_bring_state / mpm_bring_close; the test driver's "bring:<name>" targets) */
void *mu_bring_test_find(const char *name);
size_t mu_bring_test_state(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
