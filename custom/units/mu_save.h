/* mu_save - the units of a Dungeon Settlers save, as wiki builds (wk_build), for the "From a save" tab of the Bring
 * Units window: mu_card_of() then draws each one as a Compact Plus card. Pure C (fe_json + the wiki tables), no game.
 *
 * Which units: the same rule as Foreign Expedition (fe_game.c fe_list_units, fe_save.c fe_export_unit): every guid in
 * EntityContainerSaveData.UnitContainerSaveData.<side>.PlayerGuidList (all sides, container order) that has an entity
 * in EntityComponentSaveData.EntityComponents. A guid listed twice is listed once.
 * Save -> build: a C port of the wiki builder's own save import (~/dungeonsettlersdb/site/builder/builder.js
 * dsbExtractUnit + dsbSaveUnitToBuild + dsbItemRolls + dsbResolvePortrait), then wk_from_json(), so a save unit gets the
 * same wk_build as the builder's "import a save" makes for it. Keys this wiki data does not know are skipped and counted.
 * One parsed save is cached per path (path + size + mtime), so a list and several builds parse the file once.
 * Thread safe (one lock). Notes and test: test/save/NOTES.md. */
#pragma once
#include <stddef.h>
#include "pvp_wiki.h"

typedef struct mu_save_unit { char guid[40]; char name[96]; int level; char race[64]; char portrait[96]; } mu_save_unit;

/* name = the build's name: a custom name, or the game's default name text key (TEXTKEY_PLAYERNAME_...) that the UI
 * localizes. level = the save's level. race = AFFECTER_ key. portrait = face sprite key ("" none). */
int  mu_save_units(const char *save_path, mu_save_unit *out, int max, char *err, size_t errn);  /* count, or -1 + err */
int  mu_save_build(const char *save_path, const char *guid, wk_build *out, char *err, size_t errn); /* 0 ok */
void mu_save_cache_clear(void);

/* What a wk_build cannot carry, and what was skipped. bar = the unit's own skill bar (PlayerUnitsSaveData.QuickSlots,
 * empty slots left out): mu_card_of computes a bar from the learned skills, the UI may show this one instead.
 * unknown = keys skipped because this wiki data does not have them (items, trees, skills, bar skills, inscriptions,
 * soul stones, race); unknown_keys = the first ones, comma separated. */
typedef struct mu_save_extra {
    int nbar; char bar[8][64];
    int unknown; char unknown_keys[400];
} mu_save_extra;
int  mu_save_build_ex(const char *save_path, const char *guid, wk_build *out, mu_save_extra *x, char *err, size_t errn);

/* The save files of a folder, newest first (the fe_game.c fe_list_saves name filter: *.json, not mp_sync*, MPFE_*,
 * *SaveHeaderCache*). out[i] = full path. No file is parsed here. Returns the count (0 when the folder is missing). */
int  mu_save_files(const char *dir, char (*out)[1024], int max);
