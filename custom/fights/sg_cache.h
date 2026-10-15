/* sg_cache - the wiki files Test Fights needs, downloaded from dungeonsettlers.wiki at first use and kept on disk
 * (never shipped, never in the repo):
 *   <root>/builder/data.js, builder.js, savegen.js   the builder core + save generator (one wiki deploy, kept together)
 *   <root>/saves/<name>_<tag>.json                   save templates (base, boss_golem, boss_sentinel, boss_abyss)
 * <root> = <game data folder>/Mods/custom-modes/wiki (the caller decides; see sgc_default_root).
 * Scripts: used from the cache; fetched again when older than max_age_s (and the wiki answers). A failed refresh
 * keeps the cached set. The three files are swapped together and only when they agree on the build tag.
 * Templates: the name carries the build tag, so a cached file never goes stale; checked complete before use. */
#pragma once
#include <stddef.h>
#include "sg_js.h"

typedef struct sgc {
    char root[1024];
    int  timeout_ms;          /* per request; default 30000 */
} sgc;

void sgc_init(sgc *c, const char *root);

/* <persistent data path>/Mods/custom-modes/wiki (same base folder as Mods/units/library.json) */
void sgc_default_root(const char *persistent_data_path, char *out, size_t n);

/* The scripts, malloc'd into *out (free with sgc_sources_free). max_age_s < 0: never refresh a present cache.
 * status (optional) says what happened ("downloaded", "cached", "offline: using the copy from <date>").
 * 0 ok, -1 none available (err). */
int  sgc_scripts(sgc *c, long max_age_s, sg_sources *out, char *status, size_t sn, char *err, size_t errn);
void sgc_sources_free(sg_sources *s);

/* sg_template_fn for sg_open (user = the sgc): from the cache, else downloaded and cached. */
int  sgc_template(void *user, const char *file, char **text, size_t *len, char *err, size_t errn);

/* The build tag inside data.js ("savegen":{"tag":"25846389"...}) and savegen.js (TAG: '25846389'). 0 ok. */
int  sgc_tag_of_data(const char *data_js, size_t n, char *out, size_t on);
int  sgc_tag_of_savegen(const char *sg_js, size_t n, char *out, size_t on);
