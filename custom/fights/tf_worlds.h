/* tf_worlds - test worlds (the saves Test Fights writes) never show in a Load list (flow I): the title's Load list, the
 * New Campaign screen and the multiplayer host list all go through the loader's load-list filter (dsl_sdk.h
 * loadlist_filter), which hides every save of a campaign GUID listed in <game data>/Mods/custom-modes/test_worlds.json.
 * Autosaves the game makes inside a test fight carry the same GUID, so they are hidden too.
 * Test worlds are throwaway copies made from the wiki's templates: back on the title after the fight their saves are
 * removed and the game refreshes its save list and the title's Continue (back to the player's own latest world). Writing a new test world also removes the files of older ones left over (game closed inside a fight).
 * Nothing of the player's own worlds is ever touched. */
#pragma once
#include "dsl_sdk.h"
#include <stddef.h>

int  tfw_init(const DSLHost *host, DSLPlugin *self);   /* registers the filter. 0 ok */
/* the save text from sg_generate -> <game data>/Saves/<SaveName>.json, listed as a test world. name_out = SaveName. 0 ok */
int  tfw_write(const char *save, size_t n, char *name_out, size_t name_n, char *err, size_t errn);
int  tfw_is_test(const char *guid, const char *save_name);
void tfw_loading(void);                                 /* our load is starting: keep the new test world on this title */
