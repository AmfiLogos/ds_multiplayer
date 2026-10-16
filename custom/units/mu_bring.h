// mu_bring.h - Bring Units: ONE window for the Foreign Expedition hub and for PvP (PLAN_V13 section 4, AM-2).
// Tabs "From a save" / "My Units", Compact Plus cards with the game's own icons, a per-player cap, "New unit" (opens the
// Unit Builder when it is in the build; its Save comes back here with the new unit ticked). Built into custom-modes.dylib
// (solo); the callers (custom/fe, custom/pvp) call it from custom-modes-mp.dylib, which links custom-modes.dylib.
// Everything runs on the game's main thread; `done` is called between frames, once, after the window closed.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MU_SRC_SAVE = 1, MU_SRC_LIBRARY = 2 };            /* request.sources: one or both */

typedef struct mu_pick {
    int source;                  /* MU_SRC_SAVE or MU_SRC_LIBRARY */
    char save_file[1024];        /* MU_SRC_SAVE: full path of the save file the unit is in */
    char unit_guid[40];          /* MU_SRC_SAVE: the unit's guid in that save (36 chars + 0) */
    char lib_id[64];             /* MU_SRC_LIBRARY: the My Units unit id (myunits.h mu_unit.id) */
    char name[96];               /* display name (both) */
    int level;                   /* both */
} mu_pick;

/* "From a save": the caller says which saves to offer. NULL list_saves = the game's own Saves folder of this player
   (read only), newest first. A save the caller lists is read only, never written by this window. */
typedef struct mu_save_ref { char path[1024]; char name[128]; char when[32]; } mu_save_ref;

typedef struct mu_bring_req {
    const char *title;           /* window title, e.g. "Bring Units" (NULL = "Bring Units") */
    const char *subtitle;        /* one line under the title (rules, e.g. "Up to 4 units. Death: Off"), or NULL */
    int max_units;               /* per player, >= 1 */
    int sources;                 /* MU_SRC_SAVE | MU_SRC_LIBRARY; the first tab shown = the first allowed source */
    const mu_pick *ticked;       /* picks to show as already ticked (re-opening the window), or NULL */
    int nticked;
    int (*list_saves)(mu_save_ref *out, int max, void *ud);   /* optional, see above */
    /* closed: OK = picks (n >= 0, n <= max_units), Cancel / Escape / closed by mu_bring_close = n == -1 */
    void (*done)(const mu_pick *picks, int n, void *ud);
    void *ud;
} mu_bring_req;

/* 0 = the window opened; -1 = another Bring Units window is open, the widget kit is missing, or bad arguments */
int  mu_bring_open(const mu_bring_req *req);
void mu_bring_close(void);       /* closes it like Cancel (done(NULL, -1)) */
int  mu_bring_is_open(void);

#ifdef __cplusplus
}
#endif
