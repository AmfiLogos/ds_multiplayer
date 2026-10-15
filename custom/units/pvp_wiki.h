/* pvp_wiki - the DS wiki builder's team share link, decoded in C (M3b). Pure C + zlib, no game code.
 * Wire format: ~/dungeonsettlersdb/site/builder/builder.js dsbEncBuild / _decodeBlob (v1 = base64url of the raw LEB128
 * blob, v2 = base64url of the deflate-raw compressed blob; blob = ver, fp hi, fp lo, unit count, units).
 * Index tables: pvp_wiki_tables.h (generated from the wiki data by tools/gen_wiki_tables.js; WK_FP = their fingerprint).
 * Unlike the browser, an index outside the tables is an ERROR here (the browser drops it): a unit built on every copy
 * must be exactly what the link says or nothing. Tests: wiki_test/ (make -C wiki_test test). */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define WK_MAX_UNITS 8
#define WK_MAX_LIST 64
#define WK_SLOTS 16

typedef struct wk_build {
    int race, bg, lvl;                 /* bg -1 = none */
    int tal[6];                        /* talent tier index (WK_TIER) per major (WK_MAJ order) */
    int offer, roll, advanced, add_shield, hide_head;
    int ninds, inds[WK_MAX_LIST];      /* WK_IND indices */
    int ntrees, trees[WK_MAX_LIST];    /* WK_TREE */
    int nlearned, learned[WK_MAX_LIST];/* WK_SKILL */
    int ninsc, insc[WK_MAX_LIST];      /* WK_INSC */
    int eq[WK_SLOTS];                  /* WK_ITEM index per WK_SLOT, -1 empty */
    int ntag[WK_SLOTS]; struct { int idx; float v; } tag[WK_SLOTS][8];
    char name[64];                     /* "" = none */
    int portrait;                      /* index into the race's pool, -1 none */
    int nman; struct { int stat; float v; } man[WK_MAX_LIST];
    int has_mb; float mb[6];           /* save-derived major growth */
    int npstk; struct { int skill, st; } pstk[WK_MAX_LIST];
    int nsouls, souls[WK_MAX_LIST];
    int nsaved_inds, saved_inds[WK_MAX_LIST];
    int nsaved_insc, saved_insc[WK_MAX_LIST];
    int nacq, acq[WK_MAX_LIST];
    int has_elix, elix[6];
    int ncond, cond[8];                /* per condition group: option index + 1, 0 none */
} wk_build;

/* `text` = a full builder URL (…?team=CODE or #team=CODE) or the bare CODE. Returns the number of units (>= 1) or -1
 * with err filled. *fp_mismatch = 1 when the link was made with other wiki data (indices may point at other rows). */
int  wk_decode(const char *text, wk_build *out, int max, int *fp_mismatch, char *err, size_t errn);
/* the decoded team as JSON with the KEY NAMES (same shape as builder.js dsbDecBuild, for tests and logs). malloc'd. */
char *wk_json(const wk_build *b, int n);
/* names for the build (from the tables) */
const char *wk_race_key(int i);
const char *wk_item_key(int i);
const char *wk_slot_name(int i);
uint64_t wk_hash(const wk_build *b, int n);   /* FNV over the decoded fields (every copy must get the same value) */
