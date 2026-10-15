/* cm_http - HTTPS GET to dungeonsettlers.wiki only (Custom Modes: Test Fights scripts + templates, Gallery API).
 * Mac: libcurl through dlopen of the system /usr/lib/libcurl.4.dylib (never linked: a game without it still loads).
 * Windows: WinHTTP. Redirects are not followed. The body grows as needed, up to max_bytes.
 * Test hook: env DSCM_WIKI_DIR=<dir> answers https://dungeonsettlers.wiki/<path> from <dir>/<path> (query string
 * dropped; '?' kept as "_q_" + query when that file exists), no network. */
#pragma once
#include <stddef.h>

#define CM_WIKI "https://dungeonsettlers.wiki"

typedef struct cmh_resp { int status; char *body; size_t n; } cmh_resp;

/* 0 = an HTTP answer arrived (any status; body NUL-terminated), -1 = no answer (err: "no internet", "timed out" ...) */
int  cmh_get(const char *url, int timeout_ms, size_t max_bytes, cmh_resp *r, char *err, size_t errn);
void cmh_free(cmh_resp *r);
