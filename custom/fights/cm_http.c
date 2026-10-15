/* cm_http - see cm_http.h. The libcurl / WinHTTP calls follow src/units wk_http.c (C1), with a growing body. */
#include "cm_http.h"
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
    size_t k = strlen(CM_WIKI);
    return !strncmp(url, CM_WIKI, k) && (url[k] == '/' || url[k] == 0);
}

/* DSCM_WIKI_DIR: the wiki served from a folder (tests) */
static int dir_get(const char *dir, const char *url, size_t max_bytes, cmh_resp *r) {
    const char *path = url + strlen(CM_WIKI);
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

void cmh_free(cmh_resp *r) { if (r) { free(r->body); memset(r, 0, sizeof *r); } }

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

static int net_get(const char *url, int timeout_ms, size_t max_bytes, cmh_resp *r, char *err, size_t errn) {
    const char *h = url + 8, *p = strchr(h, '/'); if (!p) p = h + strlen(h);
    wchar_t host[256], path[4096];
    int hl = (int)(p - h); if (hl > 255) hl = 255;
    MultiByteToWideChar(CP_UTF8, 0, h, hl, host, 255); host[hl] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, *p ? p : "/", -1, path, 4095)) { snprintf(err, errn, "bad url"); return -1; }
    int rc = -1; HINTERNET s = NULL, c = NULL, q = NULL;
    sink b = { 0 }; b.max = max_bytes;
    s = WinHttpOpen(L"DungeonSettlersMod/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"DungeonSettlersMod/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { snprintf(err, errn, "WinHttpOpen failed (%lu)", GetLastError()); goto out; }
    WinHttpSetTimeouts(s, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) { snprintf(err, errn, "WinHttpConnect failed (%lu)", GetLastError()); goto out; }
    q = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!q) { snprintf(err, errn, "WinHttpOpenRequest failed (%lu)", GetLastError()); goto out; }
    DWORD dis = WINHTTP_DISABLE_REDIRECTS; WinHttpSetOption(q, WINHTTP_OPTION_DISABLE_FEATURE, &dis, sizeof dis);
#ifdef WINHTTP_OPTION_DECOMPRESSION
    DWORD dec = WINHTTP_DECOMPRESSION_FLAG_ALL; WinHttpSetOption(q, WINHTTP_OPTION_DECOMPRESSION, &dec, sizeof dec);
#endif
    if (!WinHttpSendRequest(q, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(q, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_WINHTTP_TIMEOUT) snprintf(err, errn, "timed out");
        else if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED) snprintf(err, errn, "no internet (name not resolved)");
        else if (e == ERROR_WINHTTP_CANNOT_CONNECT) snprintf(err, errn, "could not connect");
        else snprintf(err, errn, "WinHTTP error %lu", e);
        goto out;
    }
    DWORD st = 0, sl = sizeof st;
    WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX);
    for (;;) {
        char buf[65536]; DWORD got = 0;
        if (!WinHttpReadData(q, buf, sizeof buf, &got)) { snprintf(err, errn, "read failed (%lu)", GetLastError()); goto out; }
        if (!got) break;
        if (sink_add(&b, buf, got)) { snprintf(err, errn, "answer larger than %zu bytes", max_bytes); goto out; }
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
static int net_get(const char *url, int timeout_ms, size_t max_bytes, cmh_resp *r, char *err, size_t errn) {
    if (!curl_tried) { curl_tried = 1; curl_lib = dlopen("/usr/lib/libcurl.4.dylib", RTLD_NOW | RTLD_LOCAL); }
    if (!curl_lib) { snprintf(err, errn, "libcurl not loadable"); return -1; }
    void *(*init)(void) = (void *(*)(void))dlsym(curl_lib, "curl_easy_init");
    int (*setopt)(void *, int, ...) = (int (*)(void *, int, ...))dlsym(curl_lib, "curl_easy_setopt");
    int (*perform)(void *) = (int (*)(void *))dlsym(curl_lib, "curl_easy_perform");
    int (*getinfo)(void *, int, ...) = (int (*)(void *, int, ...))dlsym(curl_lib, "curl_easy_getinfo");
    void (*cleanup)(void *) = (void (*)(void *))dlsym(curl_lib, "curl_easy_cleanup");
    const char *(*strerr)(int) = (const char *(*)(int))dlsym(curl_lib, "curl_easy_strerror");
    if (!init || !setopt || !perform || !getinfo || !cleanup) { snprintf(err, errn, "libcurl: missing functions"); return -1; }
    void *c = init(); if (!c) { snprintf(err, errn, "curl_easy_init failed"); return -1; }
    sink b = { 0 }; b.max = max_bytes;
    setopt(c, C_URL, url); setopt(c, C_FOLLOW, 0L); setopt(c, C_NOSIGNAL, 1L);
    setopt(c, C_TIMEOUT_MS, (long)timeout_ms); setopt(c, C_CONNECT_MS, (long)(timeout_ms < 10000 ? timeout_ms : 10000));
    setopt(c, C_PROTOCOLS, 2L);                                          /* CURLPROTO_HTTPS only */
    setopt(c, C_ACCEPT_ENCODING, "");                                    /* gzip / br as the server likes: 3 MB -> ~0.3 MB */
    setopt(c, C_USERAGENT, "DungeonSettlersMod/1");
    setopt(c, C_WRITEFN, w_cb); setopt(c, C_WRITEDATA, &b);
    int e = perform(c); long code = 0;
    if (e) {
        if (b.over) snprintf(err, errn, "answer larger than %zu bytes", max_bytes);
        else if (e == 6) snprintf(err, errn, "no internet (name not resolved)");
        else if (e == 28) snprintf(err, errn, "timed out");
        else snprintf(err, errn, "%s", strerr ? strerr(e) : "curl error");
        cleanup(c); free(b.p);
        return -1;
    }
    getinfo(c, C_INFO_CODE, &code);
    cleanup(c);
    r->status = (int)code; r->body = b.p ? b.p : calloc(1, 1); r->n = b.n;
    return 0;
}
#endif

int cmh_get(const char *url, int timeout_ms, size_t max_bytes, cmh_resp *r, char *err, size_t errn) {
    memset(r, 0, sizeof *r);
    if (!wiki_url(url)) { snprintf(err, errn, "only %s is allowed", CM_WIKI); return -1; }
    const char *dir = getenv("DSCM_WIKI_DIR");
    if (dir && *dir) return dir_get(dir, url, max_bytes, r);
    return net_get(url, timeout_ms, max_bytes, r, err, errn);
}
