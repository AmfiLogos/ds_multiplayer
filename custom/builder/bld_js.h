/* bld_js - the Unit Builder's model: the wiki's own builder core (dungeonsettlers.wiki /builder/data.js + builder.js, from
 * the Test Fights cache, sg_cache.h) and bld_glue.js run in QuickJS (custom/fights' engine files), so every rule and every
 * number of a unit built in the game equals the wiki's builder (PLAN_V13 BLD route: "do not retype rules").
 *
 * Not thread-safe: one engine is used from one thread at a time, and QuickJS checks its stack against the thread that
 * opened it, so open and call it on the same thread (the builder's worker, bld_work.c: 8 MB stack).
 * Every call has a deadline (default 20 s), so a broken script cannot hang the game. */
#pragma once
#include <stddef.h>

typedef struct bld_engine bld_engine;

/* data.js / builder.js as downloaded (need not end in NUL). pre_js (tests only, NULL in the game): run first, e.g. a
   seeded Math.random. NULL + err on failure (a wiki builder this model cannot read says so in err). */
bld_engine *bld_open(const char *data_js, size_t data_n, const char *builder_js, size_t builder_n, const char *pre_js,
                     char *err, size_t errn);
void bld_close(bld_engine *e);
void bld_set_deadline_ms(bld_engine *e, int ms);

/* BLD.<fn>(arg) where arg is JSON text (NULL = no argument); the result (a Promise is settled) as JSON text, malloc'd.
   fn: "load" (a stored build or null -> state), "act" ({t:...} -> state), "state", "build", "link" (a JSON string),
   "computed", "tag". NULL + err on a JS error. */
char *bld_call(bld_engine *e, const char *fn, const char *arg_json, char *err, size_t errn);

/* bld_glue.js as bytes (bld_embed.c; NUL after the last byte) */
extern const unsigned char bld_glue_js[];
extern const unsigned int bld_glue_js_len;
