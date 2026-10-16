# Lane units13: interfaces other lanes call (PLAN_V13 section 4)

## Build lists (custom/build_module.sh)
- `custom/<folder>/sources.txt`: .c files (paths relative to custom/) built into **custom-modes.dylib** (solo, no
  multiplayer link). `custom/<folder>/sources_mp.txt`: built into **custom-modes-mp.dylib** (links multiplayer AND
  custom-modes.dylib, so FE / PvP call My Units directly). One path per line; `#` lines and blank lines skipped. A folder
  with a list is also an include folder (-I<folder>). Edit only your own folder's lists; build_module.sh is lane units'.
- Today: fe/sources_mp.txt, pvp/sources_mp.txt, units/sources_mp.txt (pvp_units, pvp_wiki, pvp_build), home/sources.txt.

## Home (custom/home/cm_home.h)
- `int cm_home_add(const char *id, const char *label, const char *tip, int order, void (*open)(void));`
  Orders: My Units 10, Unit Builder 20, Gallery 30, Test Fights 40. tip NULL = no hover card. open() runs on the main
  thread between frames; the page stays under your window.
- Your entry point, called by cm_module.c at dsl_plugin_init (the widget kit is up):
  `void cm_units_init(const DSLHost *, DSLPlugin *)`, `cm_builder_init`, `cm_gallery_init`, `cm_fights_init`.
  cm_module.c has a weak default for each; your strong definition replaces it at link time (no flags needed).
- `const DSLHost *cm_host(void); DSLPlugin *cm_self(void);` the host / module handle.
- Tests: `cm_test_register(prefix, find, state)`: your widgets by name for the test driver (ext/units13/driver,
  "<prefix>:<name>") and a JSON state; `cm_test_target`, `cm_test_state`.

## Bring Units (custom/units/mu_bring.h) - FEP calls it
```c
enum { MU_SRC_SAVE = 1, MU_SRC_LIBRARY = 2 };
typedef struct mu_pick { int source; char save_file[1024]; char unit_guid[40]; char lib_id[64]; char name[96]; int level; } mu_pick;
typedef struct mu_save_ref { char path[1024]; char name[128]; char when[32]; } mu_save_ref;
typedef struct mu_bring_req {
    const char *title; const char *subtitle; int max_units; int sources;
    const mu_pick *ticked; int nticked;
    int (*list_saves)(mu_save_ref *out, int max, void *ud);          /* NULL = the game's own Saves, newest first */
    void (*done)(const mu_pick *picks, int n, void *ud);              /* n == -1: cancelled */
    void *ud;
} mu_bring_req;
int  mu_bring_open(const mu_bring_req *req);   /* 0 ok, -1 already open / no kit / bad args */
void mu_bring_close(void);
int  mu_bring_is_open(void);
```
A MU_SRC_LIBRARY pick is a custom unit: it must never enter a regular save (FE guard: manifest source "library" +
library_id). Use myunits.h (mu_load / mu_entries) to turn lib ids into hub entries.

## My Units store + decoder (custom/units, after step 2)
myunits.h (mu_load / mu_add_build / mu_team_* / mu_rename / mu_delete), wk_link.h (wkl_resolve / wkl_decode_offline:
the one rule for old links). FIG and BLD call these, never their own decoder.
