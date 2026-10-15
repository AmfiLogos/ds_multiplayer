/* myunits - "My Units": the player's unit library, shared by the launcher's Unit Builder and the game. Pure C (no game
 * code), so the launcher format tests and the game read the same file the same way.
 * File: ds-launcher-units v1 (~/ds_launcher/builder/NOTES.md "My units file"):
 *   { "format": "ds-launcher-units", "version": 1, "game_build": "...",
 *     "units": [ { "id": "16 hex", "name": "...", "created": ms, "updated": ms, "build": { builder JSON build } } ],
 *     "teams": [ { "id": "16 hex", "name": "...", "unit_ids": ["..."] } ] }
 * Units are stored by KEY (the wiki builder's JSON: race / item / skill keys), never by link index.
 * Where: ONE file for every play copy and the launcher, in the game's normal data folder:
 *   Mac:     ~/Library/Application Support/CanOpener/Dungeon Settlers/Mods/units/library.json (the real home: a play
 *            copy runs with CFFIXED_USER_HOME, so its persistentDataPath is NOT used)
 *   Windows: <persistentDataPath>\Mods\units\library.json (AppData\LocalLow\CanOpener\Dungeon Settlers)
 *   env DSMP_UNITS_LIB = the file (tests, rig copies).
 * Writes: temp + rename, the previous file kept as library.json.bak (same as the launcher's store.rs).
 * Owner rule: a library unit only ever goes into a custom mode as a COPY (PvP, the expedition hub); the library entry is
 * never changed by a match or a hub run, and a custom unit never enters a regular save (fe_save.c fe_apply_results
 * refuses manifests with source "wiki" / a zero home campaign). */
#pragma once
#include <stddef.h>
#include "pvp_wiki.h"
#include "fe/fe_json.h"

#define MU_FORMAT "ds-launcher-units"
#define MU_VERSION 1
#define MU_TEAM_MAX WK_MAX_UNITS

typedef struct mu_info { char id[17], name[64], race[64], portrait[96]; int lvl; } mu_info;

/* game: Application.persistentDataPath of the running copy (Windows uses it; Mac only as a fallback) */
void mu_set_data_dir(const char *persistent_data_path);
/* the library file path (env DSMP_UNITS_LIB first) */
void mu_lib_path(char *out, size_t n);

/* the library (a new empty one when the file does not exist), or NULL with err filled (damaged / newer version) */
jv  *mu_load(char *err, size_t errn);
int  mu_save(const jv *lib, char *err, size_t errn);                  /* 0 ok; temp + rename, .bak kept */

size_t mu_count(const jv *lib);
int  mu_info_at(const jv *lib, size_t i, mu_info *out);               /* 0 ok */
const jv *mu_find(const jv *lib, const char *id);                    /* the unit object, or NULL */
size_t mu_team_count(const jv *lib);
/* team i: name and up to MU_TEAM_MAX unit ids (ids of units no longer in the library are left out). returns the count */
int  mu_team_at(const jv *lib, size_t i, char *name, size_t namen, char ids[][17]);

/* adds n units (decoded from a wiki link) and, when n >= 1, a team of them named `team_name` (NULL/"" = a name from
   the units). ids_out (may be NULL) gets the new unit ids. Returns n, or -1 with err filled. Does NOT save. */
int  mu_add_team(jv *lib, const wk_build *b, int n, const char *team_name, char ids_out[][17], char *err, size_t errn);

/* the owner's My Units tab (flow F): one unit from a builder JSON build (Unit Builder / Gallery "Save to My Units"):
   checked with wk_from_json first (a build this game data cannot hold is refused). name NULL/"" = the build's own.
   0 ok (id_out filled), -1 + err. Does NOT save. */
int  mu_add_build(jv *lib, const jv *build, const char *name, char id_out[17], char *err, size_t errn);
int  mu_rename(jv *lib, const char *id, const char *name);            /* 0 ok, -1 no such unit */
/* removes the unit and its place in every team; a team left with no unit goes too. 0 ok, -1 no such unit */
int  mu_delete(jv *lib, const char *id);
/* "Save as team": a new team of these library ids (1..MU_TEAM_MAX, all present). 0 ok (tid_out filled), -1 + err */
int  mu_team_new(jv *lib, const char *name, char ids[][17], int n, char tid_out[17], char *err, size_t errn);
int  mu_team_delete(jv *lib, const char *tid);                       /* the units stay. 0 ok, -1 no such team */
int  mu_team_id_at(const jv *lib, size_t i, char out[17]);           /* 0 ok */

/* a library unit as wk_build (wk_from_json, pvp_wiki.h) */
int  mu_build_of(const jv *lib, const char *id, wk_build *out, char *err, size_t errn);

/* entries {closure, manifest} (pvp_build.c pb_entry) for these library ids, for PvP / the expedition hub: copies only.
   manifest.source stays "wiki" (same unit kind), plus manifest.library_id. NULL + err on the first unit that fails. */
jv  *mu_entries(const jv *lib, char ids[][17], int n, const char *player_uid, char *notes, size_t notesn, char *err, size_t errn);
