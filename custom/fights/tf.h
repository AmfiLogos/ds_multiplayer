/* tf - the Test Fights window (Custom Modes page > Test Fights; owner 2026-10-15: "The custom fights and the boss
 * fights should just follow the way it's done inside of the wiki so that people can do it directly inside of the
 * game"). The wiki's "Test this team in-game" panel, in a game window: the squad (My Units: units or a team, or a
 * Gallery build), What do you want (DPS Check / Boss fight / Custom fight), Which fight / room (+ Magic Saturation for
 * the Abyss, monsters for a custom fight), Difficulty, Battle kits -> Start: the wiki's own save generator (sg_js) on
 * the worker thread, the test world written into the Saves folder (hidden from every Load list: tf_worlds.h), then the
 * game's own load. */
#pragma once
#include "dsl_sdk.h"

void tf_open(void);
/* from the Gallery: open Test Fights with this squad (JSON array of the builder's builds, as stored) */
void tf_open_with_builds(const char *builds_json, const char *title, const char *const *names, int n);
