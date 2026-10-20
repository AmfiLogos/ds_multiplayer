/* fe_http - see fe_http.h. The libcurl / WinHTTP calls follow src/units wk_http.c (C1), with a growing body. */
#include "fe_http.h"
#include "ds_i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t n, cap, max; int over; } sink;
static int sink_add(sink *s, const char *d, size_t L) {
    if (s->n + L > s->max) { s->over = 1; return -1; }
    if (s->n + L + 1 > s->cap) {
        size_t nc = s->cap ? s->cap : 65536;
        while (nc < s->n + L + 1) nc *= 2;
        char *np = realloc(s->p, nc);
        if (!np) { s->over = 1; return -1; }
        s->p = np; s->cap = nc;
    }
    memcpy(s->p + s->n, d, L); s->n += L; s->p[s->n] = 0;
    return 0;
}

static int wiki_url(const char *url) {
    size_t k = strlen(FE_WIKI);
    return !strncmp(url, FE_WIKI, k) && (url[k] == '/' || url[k] == 0);
}

/* DSCM_WIKI_DIR: the wiki served from a folder (tests) */
static int dir_get(const char *dir, const char *url, size_t max_bytes, feh_resp *r) {
    const char *path = url + strlen(FE_WIKI);
    char p[2048], q[2048];
    const char *qm = strchr(path, '?');
    size_t pl = qm ? (size_t)(qm - path) : strlen(path);
    snprintf(p, sizeof p, "%s%.*s", dir, (int)pl, path);
    FILE *f = NULL;
    if (qm) { snprintf(q, sizeof q, "%s_q_%s", p, qm + 1); f = fopen(q, "rb"); }
    if (!f) f = fopen(p, "rb");
    if (!f) { r->status = 404; r->body = calloc(1, 1); return 0; }
    sink s = { 0 }; s.max = max_bytes;
    char buf[65536]; size_t got;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) if (sink_add(&s, buf, got)) break;
    fclose(f);
    if (s.over) { free(s.p); r->status = 413; r->body = calloc(1, 1); return 0; }
    r->status = 200; r->body = s.p ? s.p : calloc(1, 1); r->n = s.n;
    return 0;
}

void feh_free(feh_resp *r) { if (r) { free(r->body); memset(r, 0, sizeof *r); } }

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

static int net_get(const char *url, int timeout_ms, size_t max_bytes, feh_resp *r, char *err, size_t errn) {
    const char *h = url + 8, *p = strchr(h, '/'); if (!p) p = h + strlen(h);
    wchar_t host[256], path[4096];
    int hl = (int)(p - h); if (hl > 255) hl = 255;
    MultiByteToWideChar(CP_UTF8, 0, h, hl, host, 255); host[hl] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, *p ? p : "/", -1, path, 4095)) { snprintf(err, errn, "%s", TR("bad url")); return -1; }
    int rc = -1; HINTERNET s = NULL, c = NULL, q = NULL;
    sink b = { 0 }; b.max = max_bytes;
    s = WinHttpOpen(L"DungeonSettlersMod/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"DungeonSettlersMod/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { snprintf(err, errn, TR("could not connect (error %lu)"), GetLastError()); goto out; }
    WinHttpSetTimeouts(s, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) { snprintf(err, errn, TR("could not connect (error %lu)"), GetLastError()); goto out; }
    q = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!q) { snprintf(err, errn, TR("could not connect (error %lu)"), GetLastError()); goto out; }
    DWORD dis = WINHTTP_DISABLE_REDIRECTS; WinHttpSetOption(q, WINHTTP_OPTION_DISABLE_FEATURE, &dis, sizeof dis);
#ifdef WINHTTP_OPTION_DECOMPRESSION
    DWORD dec = WINHTTP_DECOMPRESSION_FLAG_ALL; WinHttpSetOption(q, WINHTTP_OPTION_DECOMPRESSION, &dec, sizeof dec);
#endif
    if (!WinHttpSendRequest(q, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(q, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_WINHTTP_TIMEOUT) snprintf(err, errn, "%s", TR("timed out"));
        else if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED) snprintf(err, errn, "%s", TR("no internet (name not resolved)"));
        else if (e == ERROR_WINHTTP_CANNOT_CONNECT) snprintf(err, errn, "%s", TR("could not connect"));
        else snprintf(err, errn, TR("connection error %lu"), e);
        goto out;
    }
    DWORD st = 0, sl = sizeof st;
    WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX);
    for (;;) {
        char buf[65536]; DWORD got = 0;
        if (!WinHttpReadData(q, buf, sizeof buf, &got)) { snprintf(err, errn, TR("the download broke off (error %lu)"), GetLastError()); goto out; }
        if (!got) break;
        if (sink_add(&b, buf, got)) { snprintf(err, errn, "%s", TR("the wiki sent more data than expected")); goto out; }
    }
    r->status = (int)st; r->body = b.p ? b.p : calloc(1, 1); r->n = b.n; b.p = NULL;
    rc = 0;
out:
    free(b.p);
    if (q) WinHttpCloseHandle(q);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return rc;
}

#else /* Mac */
#include <dlfcn.h>

/* libcurl option numbers are its stable ABI (curl.h CURLOPTTYPE_* + n) */
enum { C_URL = 10002, C_WRITEDATA = 10001, C_USERAGENT = 10018, C_WRITEFN = 20011, C_FOLLOW = 52, C_NOSIGNAL = 99,
       C_TIMEOUT_MS = 155, C_CONNECT_MS = 156, C_PROTOCOLS = 181, C_ACCEPT_ENCODING = 10102 };
#define C_INFO_CODE 2097154                                              /* CURLINFO_RESPONSE_CODE */
static size_t w_cb(char *d, size_t sz, size_t nm, void *u) {
    size_t L = sz * nm;
    return sink_add(u, d, L) ? 0 : L;                                   /* 0 = abort the transfer */
}
static void *curl_lib; static int curl_tried;
static int net_get(const char *url, int timeout_ms, size_t max_bytes, feh_resp *r, char *err, size_t errn) {
    if (!curl_tried) { curl_tried = 1; curl_lib = dlopen("/usr/lib/libcurl.4.dylib", RTLD_NOW | RTLD_LOCAL); }
    if (!curl_lib) { snprintf(err, errn, "%s", TR("the system's download library is missing")); return -1; }
    void *(*init)(void) = (void *(*)(void))dlsym(curl_lib, "curl_easy_init");
    int (*setopt)(void *, int, ...) = (int (*)(void *, int, ...))dlsym(curl_lib, "curl_easy_setopt");
    int (*perform)(void *) = (int (*)(void *))dlsym(curl_lib, "curl_easy_perform");
    int (*getinfo)(void *, int, ...) = (int (*)(void *, int, ...))dlsym(curl_lib, "curl_easy_getinfo");
    void (*cleanup)(void *) = (void (*)(void *))dlsym(curl_lib, "curl_easy_cleanup");
    const char *(*strerr)(int) = (const char *(*)(int))dlsym(curl_lib, "curl_easy_strerror");
    if (!init || !setopt || !perform || !getinfo || !cleanup) { snprintf(err, errn, "%s", TR("the system's download library is missing")); return -1; }
    void *c = init(); if (!c) { snprintf(err, errn, "%s", TR("the download could not start")); return -1; }
    sink b = { 0 }; b.max = max_bytes;
    setopt(c, C_URL, url); setopt(c, C_FOLLOW, 0L); setopt(c, C_NOSIGNAL, 1L);
    setopt(c, C_TIMEOUT_MS, (long)timeout_ms); setopt(c, C_CONNECT_MS, (long)(timeout_ms < 10000 ? timeout_ms : 10000));
    setopt(c, C_PROTOCOLS, 2L);                                          /* CURLPROTO_HTTPS only */
    setopt(c, C_ACCEPT_ENCODING, "");                                    /* gzip / br as the server likes: 3 MB -> ~0.3 MB */
    setopt(c, C_USERAGENT, "DungeonSettlersMod/1");
    setopt(c, C_WRITEFN, w_cb); setopt(c, C_WRITEDATA, &b);
    int e = perform(c); long code = 0;
    if (e) {
        if (b.over) snprintf(err, errn, "%s", TR("the wiki sent more data than expected"));
        else if (e == 6) snprintf(err, errn, "%s", TR("no internet (name not resolved)"));
        else if (e == 28) snprintf(err, errn, "%s", TR("timed out"));
        else snprintf(err, errn, "%s", strerr ? strerr(e) : TR("connection error"));
        cleanup(c); free(b.p);
        return -1;
    }
    getinfo(c, C_INFO_CODE, &code);
    cleanup(c);
    r->status = (int)code; r->body = b.p ? b.p : calloc(1, 1); r->n = b.n;
    return 0;
}
#endif

int feh_get(const char *url, int timeout_ms, size_t max_bytes, feh_resp *r, char *err, size_t errn) {
    memset(r, 0, sizeof *r);
    if (!wiki_url(url)) { snprintf(err, errn, TR("only %s is allowed"), FE_WIKI); return -1; }
    const char *dir = getenv("DSCM_WIKI_DIR");
    if (dir && *dir) return dir_get(dir, url, max_bytes, r);
    return net_get(url, timeout_ms, max_bytes, r, err, errn);
}

/* ---- the base template cache (from ds_custommodes fights/sg_cache.c sgc_template) */
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define feh_mkdir1(p) _mkdir(p)
#else
#define feh_mkdir1(p) mkdir(p, 0755)
#endif
#define FEH_TEMPLATE_MAX ((size_t)64 << 20)
static void feh_mkdirs(const char *path) {
    char p[1100]; snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++) if (*s == '/' || *s == '\\') { char k = *s; *s = 0; feh_mkdir1(p); *s = k; }
    feh_mkdir1(p);
}
static char *feh_slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = L >= 0 ? malloc((size_t)L + 1) : NULL;
    if (b && fread(b, 1, (size_t)L, f) != (size_t)L) { free(b); b = NULL; }
    if (b) { b[L] = 0; if (n) *n = (size_t)L; }
    fclose(f);
    return b;
}
static int feh_complete(const char *s, size_t n) {
    size_t a = 0; while (a < n && isspace((unsigned char)s[a])) a++;
    while (n > a && isspace((unsigned char)s[n - 1])) n--;
    return n > a + 1 && s[a] == '{' && s[n - 1] == '}';
}
static int feh_good_name(const char *f) {
    if (!f || !*f || strlen(f) > 60) return 0;
    for (const char *p = f; *p; p++) if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.') return 0;
    return !strstr(f, "..");
}
int fe_wiki_template(const char *pdp, const char *file, char **text, size_t *len, char *err, size_t errn) {
    *text = NULL; *len = 0;
    if (!feh_good_name(file)) { snprintf(err, errn, "bad template name %.80s", file); return -1; }
    char dir[1100], p[1200]; size_t n = 0; char *t;
    snprintf(p, sizeof p, "%s/Mods/custom-modes/wiki/saves/%s", pdp, file);       /* an older Units Laboratory's copy */
    snprintf(dir, sizeof dir, "%s/Mods/multiplayer/wiki/saves", pdp);
    char q[1200]; snprintf(q, sizeof q, "%s/%s", dir, file);
    const char *cand[2] = { q, p };
    for (int i = 0; i < 2; i++) {
        t = feh_slurp(cand[i], &n);
        if (t && feh_complete(t, n)) { *text = t; *len = n; return 0; }
        free(t);
    }
    char url[512]; snprintf(url, sizeof url, FE_WIKI "/assets/saves/%s", file);
    feh_resp r;
    if (feh_get(url, 120000, FEH_TEMPLATE_MAX, &r, err, errn)) return -1;
    if (r.status != 200 || !feh_complete(r.body, r.n)) {
        snprintf(err, errn, "%s", r.status == 200 ? TR("a file from the wiki arrived incomplete") : TR("the wiki is missing a file for this fight"));
        feh_free(&r); return -1;
    }
    feh_mkdirs(dir);
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.part", q);                    /* write <file>.part, then rename over <file> */
    FILE *f = fopen(tmp, "wb");
    if (f) { int ok = fwrite(r.body, 1, r.n, f) == r.n; ok &= fclose(f) == 0;
#ifdef _WIN32
             if (ok) remove(q);
#endif
             if (!ok || rename(tmp, q)) remove(tmp); }                          /* a cache that cannot be written still serves this run */
    *text = r.body; *len = r.n;
    return 0;
}
