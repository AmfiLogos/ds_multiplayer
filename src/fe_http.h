/* fe_http - HTTPS GET to dungeonsettlers.wiki only: the base template the Foreign Expedition hub and the PvP world start from.
 * Moved with Foreign Expedition / PvP from ds_custommodes fights/cm_http.c + the template part of fights/sg_cache.c.
 * Mac: libcurl through dlopen of the system /usr/lib/libcurl.4.dylib (never linked: a game without it still loads).
 * Windows: WinHTTP. Redirects are not followed. The body grows as needed, up to max_bytes.
 * Test hook: env DSCM_WIKI_DIR=<dir> answers https://dungeonsettlers.wiki/<path> from <dir>/<path> (query string
 * dropped; '?' kept as "_q_" + query when that file exists), no network. */
#pragma once
#include <stddef.h>

#define FE_WIKI "https://dungeonsettlers.wiki"

typedef struct feh_resp { int status; char *body; size_t n; } feh_resp;

/* 0 = an HTTP answer arrived (any status; body NUL-terminated), -1 = no answer (err: "no internet", "timed out" ...) */
int  feh_get(const char *url, int timeout_ms, size_t max_bytes, feh_resp *r, char *err, size_t errn);
void feh_free(feh_resp *r);

/* the wiki's base template (base_<build>.json): from the cache <data>/Mods/multiplayer/wiki/saves/ (else the copy an older
   Units Laboratory kept in <data>/Mods/custom-modes/wiki/saves/), else downloaded once into the cache.
   0 = *text (malloc, NUL-terminated) + *len; -1 + err. pdp = the game's persistentDataPath. */
int fe_wiki_template(const char *pdp, const char *file, char **text, size_t *len, char *err, size_t errn);
