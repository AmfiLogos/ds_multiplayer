/* pvp_build - a decoded wiki build (pvp_wiki.h) written as a unit closure + manifest, the same entry shape as
 * fe_export_entry (fe/fe_save.c), so a wiki unit goes the brought-units way (pvp_units.c: mail, FE_START, join point).
 * Pure C (no game code): facts from the game's own tables (pvp_unit_tables.h), rules from the game code (comments cite
 * the RVAs, build 25846389). Tests: build_test/ (make -C build_test test). */
#pragma once
#include "pvp_wiki.h"
#include "fe/fe_json.h"

/* The entry {closure, manifest}, or NULL with err filled (the link asks for something the game cannot hold: a race
 * with no look, an item in the wrong slot, a skill outside the unit's trees, a level over the cap, ...).
 * notes (may be NULL): one line per thing the save cannot carry as the builder shows it (manual non-major stats,
 * hunger / exhaustion, ...), for the player and the difference list. */
jv *pb_entry(const wk_build *b, const char *player_uid, char *notes, size_t notesn, char *err, size_t errn);

/* the six majors the save will hold as 10 + Generated + Progression + Permanent (before the game re-applies
 * affecters and gear), for tests */
void pb_stored_majors(const wk_build *b, double out[6]);

/* 1 when the import folds this skill (builder passive model) as a permanent grant; stat = its first stat name */
int pb_folded_skill(const char *key, char *stat, size_t n);
