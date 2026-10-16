/* wk_link - every wiki builder link form, read in one place (used by Bring Units, My Units and the PvP button).
 * Forms: ...?team=CODE / #team=CODE / &team=CODE, ...#b=~CODE (one-unit compact), ...#b=<base64url JSON build>, a bare
 * CODE, and the short links https://dungeonsettlers.wiki[/<lang>]/t/<id> and /build/<id>.
 * A short link costs ONE HTTPS GET to dungeonsettlers.wiki (no other host), redirects NOT followed: the answer's
 * Location header (302), else the <meta http-equiv="refresh" content="0; url=..."> of a preview-card page, must point
 * at dungeonsettlers.wiki /builder; that long link is then decoded here. Limit 5 s.
 * HTTP: Mac = libcurl (dlopen of the system /usr/lib/libcurl.4.dylib) or CFNetwork, Windows = WinHTTP (wk_http.c). */
#pragma once
#include <stddef.h>
#include "pvp_wiki.h"

enum { WKL_OK = 0, WKL_EMPTY, WKL_NOT_LINK, WKL_BAD, WKL_OFFLINE, WKL_UNKNOWN, WKL_SERVER, WKL_OTHER_DATA };

/* 1 when the text holds something this reader takes (any form above), else 0. For "is there a link on the clipboard?" */
int wkl_looks_like_link(const char *text);
/* 1 when it is a short link; url = the https URL to ask (normalised: host dungeonsettlers.wiki, path /t/<id> etc.) */
int wkl_short_url(const char *text, char *url, size_t n);
/* the long link a short-link answer points at (Location or meta refresh), checked: dungeonsettlers.wiki, /builder.
   0 ok, -1 none / another host */
int wkl_target_of(const char *location, const char *body, char *out, size_t n);

/* decode any form; short links go to the network (blocking, <= timeout_ms). Returns the unit count, or -1 with *code
   (WKL_*) and a player-facing message in err. fp mismatch (other wiki data) is an error here (WKL_OTHER_DATA). */
int wkl_resolve(const char *text, wk_build *out, int max, int timeout_ms, int *code, char *err, size_t errn);
/* the same without the network (tests / a long link): a short link gives WKL_OFFLINE */
int wkl_decode_offline(const char *text, wk_build *out, int max, int *code, char *err, size_t errn);

/* platform HTTP GET, no redirect following. 0 = got an answer (status filled), -1 = no answer (err: why).
   location/body may be NULL. */
int wk_http_get(const char *url, int timeout_ms, int *status, char *location, size_t locn, char *body, size_t bodyn, char *err, size_t errn);
/* "libcurl", "CFNetwork", "WinHTTP" (what wk_http_get uses on this machine), "" = none loaded yet */
const char *wk_http_backend(void);
/* probe every backend this platform has, one line each into out (the in-game proof: rig command wk_http_probe) */
void wk_http_probe(const char *url, char *out, size_t n);
