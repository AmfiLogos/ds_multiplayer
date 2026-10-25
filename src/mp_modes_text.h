/* cm_mp_text.h - player text of custom-modes-mp.dylib (fe/, pvp/): the string tables (i18n/ds_i18n.h) and a few helpers.
 * The tables, the language folder (<this dylib's folder>/lang) and the per-frame language poll live in cm_mp.c.
 *  - cmt_set(t, s):        tmp_set with a whole UTF-8 character at the end (a cut character blanks the whole label).
 *  - cmt_game(key, en):    the game's own text for a TEXTKEY (TextKeyExtensions.GetText, current language); en when missing.
 *  - cmt_level / cmt_day:   "Level 5" / "Day 12" with the game's own words and format.
 *  - cmt_msg(en, out, n):  a message made in English elsewhere (fe_save.c errors, codes kept English on purpose, see
 *                         NOTES_i18n.md) shown in the current language when it matches a known pattern; else as is.
 *  - cmt_gen():       +1 on every language change (re-label things that are set once). */
#pragma once
#include <stddef.h>
#include "ds_i18n.h"

void cmt_set(void *tmp_text, const char *s);
const char *cmt_game(const char *textkey, const char *en);
void cmt_level(char *out, size_t cap, int level);
void cmt_day(char *out, size_t cap, int day);
const char *cmt_msg(const char *en, char *out, size_t cap);
unsigned cmt_gen(void);
