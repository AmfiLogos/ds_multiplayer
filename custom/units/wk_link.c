/* wk_link - every wiki builder link form in one reader (see wk_link.h). Pure C; the network is wk_http.c. */
#include "wk_link.h"
#include "fe/fe_json.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST "dungeonsettlers.wiki"

static void pct_decode(const char *s, size_t n, char *o, size_t on) {
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < on; i++) {
        if (s[i] == '%' && i + 2 < n && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            char h[3] = { s[i + 1], s[i + 2], 0 }; o[k++] = (char)strtol(h, NULL, 16); i += 2;
        } else o[k++] = s[i];
    }
    o[k] = 0;
}
static size_t token_len(const char *s, const char *stop) {
    size_t n = 0; while (s[n] && !strchr(stop, s[n]) && !isspace((unsigned char)s[n])) n++; return n;
}
static const char *after(const char *t, const char *pre) { const char *p = strstr(t, pre); return p ? p + strlen(pre) : NULL; }
static int is_code(const char *t) {                                      /* a bare team code: base64url, no spaces */
    size_t n = 0; while (isspace((unsigned char)*t)) t++;
    for (; t[n] && !isspace((unsigned char)t[n]); n++) if (!isalnum((unsigned char)t[n]) && t[n] != '-' && t[n] != '_') return 0;
    while (t[n]) { if (!isspace((unsigned char)t[n])) return 0; n++; }
    return n >= 16;
}

int wkl_short_url(const char *text, char *url, size_t n) {
    const char *h = text ? strstr(text, HOST) : NULL;
    if (!h) return 0;
    if (h > text && (isalnum((unsigned char)h[-1]) || h[-1] == '-') && !(h - text >= 4 && !strncmp(h - 4, "www.", 4))) return 0;   /* evil-dungeonsettlers.wiki */
    const char *p = h + strlen(HOST);
    if (*p == ':') return 0;                                             /* a port: not the wiki */
    if (*p != '/') return 0;
    /* optional language prefix: /xx/ or /xx-yy/ or /zht/ */
    const char *q = p + 1; size_t L = 0; while (isalpha((unsigned char)q[L]) || q[L] == '-') L++;
    if (L >= 2 && L <= 5 && q[L] == '/' && strncmp(q, "build", 5)) p = q + L;
    const char *kind = !strncmp(p, "/t/", 3) ? "t" : !strncmp(p, "/build/", 7) ? "build" : NULL;
    if (!kind) return 0;
    const char *id = p + (kind[0] == 't' ? 3 : 7);
    size_t il = token_len(id, "?#&/\"'<>");
    if (!il || il > 120) return 0;
    for (size_t i = 0; i < il; i++) if (!isalnum((unsigned char)id[i]) && !strchr("-_.~%", id[i])) return 0;
    snprintf(url, n, "https://" HOST "/%s/%.*s", kind, (int)il, id);
    return 1;
}

int wkl_looks_like_link(const char *t) {
    if (!t) return 0;
    char u[300];
    return strstr(t, "team=") || strstr(t, "#b=") || wkl_short_url(t, u, sizeof u) || is_code(t);
}

/* "&amp;" etc. in an attribute value */
static void html_unesc(char *s) {
    char *o = s;
    for (char *p = s; *p; ) {
        if (*p == '&') {
            if (!strncmp(p, "&amp;", 5)) { *o++ = '&'; p += 5; continue; }
            if (!strncmp(p, "&#38;", 5)) { *o++ = '&'; p += 5; continue; }
            if (!strncmp(p, "&quot;", 6)) { *o++ = '"'; p += 6; continue; }
            if (!strncmp(p, "&#39;", 5)) { *o++ = '\''; p += 5; continue; }
            if (!strncmp(p, "&lt;", 4)) { *o++ = '<'; p += 4; continue; }
            if (!strncmp(p, "&gt;", 4)) { *o++ = '>'; p += 4; continue; }
        }
        *o++ = *p++;
    }
    *o = 0;
}
/* the target must be the wiki's builder: a relative "/…builder…" or https://(www.)dungeonsettlers.wiki/… */
static int target_ok(const char *u, char *out, size_t n) {
    while (isspace((unsigned char)*u)) u++;
    const char *path = NULL;
    if (u[0] == '/' && u[1] != '/') path = u;
    else if (!strncmp(u, "https://" HOST "/", 8 + strlen(HOST) + 1)) path = u + 8 + strlen(HOST);
    else if (!strncmp(u, "https://www." HOST "/", 12 + strlen(HOST) + 1)) path = u + 12 + strlen(HOST);
    if (!path) return -1;
    const char *b = strstr(path, "/builder");
    const char *q = strpbrk(path, "?#");
    if (!b || (q && b > q)) return -1;                                  /* "/builder" must be in the path */
    snprintf(out, n, "https://" HOST "%s", path);
    return 0;
}
int wkl_target_of(const char *loc, const char *body, char *out, size_t n) {
    if (loc && *loc) return target_ok(loc, out, n);
    if (!body) return -1;
    /* <meta http-equiv="refresh" content="0; url=/builder?team=…"> */
    for (const char *p = body; (p = strstr(p, "<meta")) != NULL; p++) {
        const char *end = strchr(p, '>'); if (!end) break;
        char tag[8192]; size_t tl = (size_t)(end - p); if (tl >= sizeof tag) continue;
        memcpy(tag, p, tl); tag[tl] = 0;
        char low[8192]; for (size_t i = 0; i <= tl; i++) low[i] = (char)tolower((unsigned char)tag[i]);
        if (!strstr(low, "http-equiv=\"refresh\"") && !strstr(low, "http-equiv=refresh") && !strstr(low, "http-equiv='refresh'")) continue;
        char *c = strstr(low, "content="); if (!c) continue;
        c += 8; char quote = *c == '"' || *c == '\'' ? *c++ : 0;
        char *uu = strstr(c, "url="); if (!uu) continue;
        size_t off = (size_t)(uu + 4 - low); char *val = tag + off;
        char *e = quote ? strchr(val, quote) : val + strcspn(val, " >"); if (e) *e = 0;
        html_unesc(val);
        return target_ok(val, out, n);
    }
    return -1;
}

static int set_err(int *code, int c, char *err, size_t errn, const char *msg) { if (code) *code = c; snprintf(err, errn, "%s", msg); return -1; }

/* every form but the short link */
static int decode_long(const char *t, wk_build *out, int max, int *code, char *err, size_t errn) {
    char e[300] = ""; int fpm = 0, k;
    const char *v;
    static _Thread_local char buf[16384];                              /* per thread: mu_link reads on a worker */
    if ((v = after(t, "?team=")) || (v = after(t, "#team=")) || (v = after(t, "&team="))) {
        size_t n = token_len(v, "&#"); pct_decode(v, n, buf, sizeof buf);
        k = wk_decode(buf, out, max, &fpm, e, sizeof e);
    } else if ((v = after(t, "#b="))) {
        size_t n = token_len(v, "&#"); pct_decode(v, n, buf, sizeof buf);
        if (buf[0] == '~') k = wk_decode(buf + 1, out, max, &fpm, e, sizeof e);
        else {                                                            /* the full one-unit link: base64url of the build JSON */
            size_t bl = strlen(buf), on = 0; unsigned char *js = malloc(bl * 3 / 4 + 4); uint32_t acc = 0; int bits = 0; k = -1;
            for (size_t i = 0; i < bl; i++) {
                int c = buf[i], x = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '-' || c == '+' ? 62 : c == '_' || c == '/' ? 63 : c == '=' ? -2 : -1;
                if (x == -2) break;
                if (x < 0) { on = 0; break; }
                acc = acc << 6 | (uint32_t)x; bits += 6; if (bits >= 8) { bits -= 8; js[on++] = (unsigned char)(acc >> bits); }
            }
            char pe[200]; jv *j = on ? jparse((const char *)js, on, pe, sizeof pe) : NULL; free(js);
            if (!j) snprintf(e, sizeof e, "this one-unit link is damaged or cut short");
            else { k = wk_from_json(j, &out[0], e, sizeof e) ? -1 : 1; jfree(j); }
            if (max < 1) k = -1;
        }
    } else if (is_code(t)) k = wk_decode(t, out, max, &fpm, e, sizeof e);
    else return set_err(code, WKL_NOT_LINK, err, errn, "This is not a wiki builder link. Copy a team or unit link on the wiki builder first.");
    if (k < 1 && strncmp(buf, "{", 1)) {                               /* an old link: its indices point at other rows */
        int fp = wk_code_fp(buf[0] == '~' ? buf + 1 : buf);
        if (fp >= 0 && fp != wk_fp()) return set_err(code, WKL_OTHER_DATA, err, errn, "This link was made with other wiki data (another game version): make it again on the wiki.");
    }
    if (k < 1) { if (code) *code = WKL_BAD; snprintf(err, errn, "This link is damaged or cut short (%s). Copy it again.", e[0] ? e : "?"); return -1; }
    if (fpm) return set_err(code, WKL_OTHER_DATA, err, errn, "This link was made with other wiki data (another game version): make it again on the wiki.");
    if (code) *code = WKL_OK;
    return k;
}

int wkl_decode_offline(const char *text, wk_build *out, int max, int *code, char *err, size_t errn) {
    if (!text) text = "";
    while (isspace((unsigned char)*text)) text++;
    if (!*text) return set_err(code, WKL_EMPTY, err, errn, "No link. Copy a team or unit link on the wiki builder first.");
    char u[300];
    if (!strstr(text, "team=") && !strstr(text, "#b=") && wkl_short_url(text, u, sizeof u))
        return set_err(code, WKL_OFFLINE, err, errn, "This short link needs the internet. Or open it on the wiki and copy the long link.");
    return decode_long(text, out, max, code, err, errn);
}

int wkl_resolve(const char *text, wk_build *out, int max, int timeout_ms, int *code, char *err, size_t errn) {
    char u[300];
    if (!text || strstr(text, "team=") || strstr(text, "#b=") || !wkl_short_url(text, u, sizeof u))
        return wkl_decode_offline(text, out, max, code, err, errn);
    int st = 0; char loc[2048] = "", e[300] = ""; static _Thread_local char body[65536];
    body[0] = 0;
    if (wk_http_get(u, timeout_ms, &st, loc, sizeof loc, body, sizeof body, e, sizeof e)) {
        if (code) *code = WKL_OFFLINE;
        snprintf(err, errn, "This short link needs the internet (%s). Or open it on the wiki and copy the long link.", e[0] ? e : "no answer");
        return -1;
    }
    if (st == 404) return set_err(code, WKL_UNKNOWN, err, errn, "This short link does not exist (removed?).");
    if (st == 410) return set_err(code, WKL_UNKNOWN, err, errn, "This short link was removed on the wiki.");
    if (st >= 300 && st < 400 && strstr(loc, "/builds/removed"))          /* a hidden gallery build (api/shorten.js) */
        return set_err(code, WKL_UNKNOWN, err, errn, "This build was removed on the wiki.");
    char tgt[16384];
    if ((st == 301 || st == 302 || st == 303 || st == 307 || st == 308) ? wkl_target_of(loc, NULL, tgt, sizeof tgt) : st == 200 ? wkl_target_of(NULL, body, tgt, sizeof tgt) : -1) {
        if (code) *code = WKL_SERVER;
        snprintf(err, errn, "The wiki gave an unexpected answer for this short link (HTTP %d). Try again later, or open it on the wiki and copy the long link.", st);
        return -1;
    }
    return decode_long(tgt, out, max, code, err, errn);
}
