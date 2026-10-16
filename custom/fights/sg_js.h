/* sg_js - Test Fights engine: the wiki's own save generator (dungeonsettlers.wiki builder/savegen.js + the builder
 * core builder.js + its data.js) run inside QuickJS, so a test fight made in the game is the save the wiki's
 * "Test this team in-game" panel makes for the same choices (owner 2026-10-15: "follow the way it's done inside of
 * the wiki"). The three scripts and the save templates come from the wiki at first use (sg_cache.h), never shipped.
 *
 * Not thread-safe: one engine is used from one thread at a time. QuickJS checks its own stack limit (1 MB default),
 * so the calling thread needs a stack above that (a game worker thread: give it 4 MB).
 * Measured on the dev Mac (test/run_eq.sh): open 0.04 s, one fight 0.06-0.10 s, 26 fights in one engine: 1.7 s, peak 222 MB resident.
 * Every call that runs JS has a deadline (default 60 s), so a broken script cannot hang the game. */
#pragma once
#include <stddef.h>

typedef struct sg_sources {                  /* the wiki's scripts, as downloaded (UTF-8; need not end in NUL) */
    const char *data_js;    size_t data_n;     /* /builder/data.js    (window.DSB, the game tables) */
    const char *builder_js; size_t builder_n;  /* /builder/builder.js (the stat / skill / link core) */
    const char *savegen_js; size_t savegen_n;  /* /builder/savegen.js (the save generator) */
} sg_sources;

/* Template provider: `file` is the template's file name as the wiki serves it ("boss_golem_25846389.json").
 * Fill *text (malloc'd, freed by the engine) and *len; return 0, or -1 with err filled. */
typedef int (*sg_template_fn)(void *user, const char *file, char **text, size_t *len, char *err, size_t errn);

/* Tests only: fixed clock + seeded Math.random (+ no crypto), so two engines can be compared byte for byte.
 * The same JS (test/det_prelude.js) is loaded by the node reference. */
typedef struct sg_det { unsigned seed; double now_ms; const char *js; } sg_det;   /* js = det_prelude.js text */

typedef struct sg_engine sg_engine;

sg_engine *sg_open(const sg_sources *src, sg_template_fn tpl, void *tpl_user, const sg_det *det, char *err, size_t errn);
void       sg_close(sg_engine *e);
void       sg_set_deadline_ms(sg_engine *e, int ms);   /* per call; default 60000 */

/* The Test Fights tab's choices and the wiki panel's defaults (sg_glue.js CMF.panel), JSON, malloc'd. */
char *sg_panel(sg_engine *e, char *err, size_t errn);

typedef struct sg_result {
    char  *file;     size_t file_n;   /* the save, the exact text the wiki's download writes (JSON, indent 2) */
    char  *filename;                  /* the wiki's file name: "<Team>_<Fight>_<Difficulty>__AutoSave_<guid>_0.json" */
    char  *meta;                      /* JSON {problems:[], notes:[], warnings:[], added, roster, room, clan} */
    int    problems;                  /* count of problems: > 0 = the wiki would NOT hand the file out; neither do we */
} sg_result;

/* opts: JSON { mode: "dps"|"boss"|"arena"|"base", boss: "golem"|"sentinel"|"abyss", builds: [stored builds],
 *              combat, mgmt, monsters: [{k,n}], supplies: {key: amount}, kits: {id: true}, kitInsc, msat, team }
 * Missing fields take the wiki panel's defaults. 0 ok (r filled; check r->problems), -1 error (err filled). */
int  sg_generate(sg_engine *e, const char *opts_json, sg_result *r, char *err, size_t errn);
void sg_result_free(sg_result *r);

/* A gallery item's stored build (the `blob` of /api/gallery?id=<id>&full=1) -> JSON array of builds, by the
 * builder's own decoders. malloc'd, NULL on error. */
char *sg_decode_blob(sg_engine *e, const char *blob, char *err, size_t errn);

/* The template file a fight needs ("boss_golem_25846389.json"), from the loaded data. 0 ok. */
int sg_template_file(sg_engine *e, const char *mode, const char *boss, char *out, size_t n);

/* Raw DEFLATE (RFC 1951) inflate, used for the builder's v2 links (tinf). malloc'd, NULL on bad data. */
unsigned char *sg_inflate_raw(const unsigned char *in, size_t n, size_t *out_n);
