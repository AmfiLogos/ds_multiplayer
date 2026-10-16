/* wk_http - one HTTPS GET for the short wiki links (wk_link.h), redirects NOT followed.
 * Mac: CFNetwork (CFHTTPStream on this thread's run loop, with a deadline) or libcurl (dlopen of the system
 *      /usr/lib/libcurl.4.dylib, never linked: a game without it still loads). Order: env DSMP_HTTP=curl|cf, else the
 *      order the in-game probe chose (see ext/myunits/NOTES.md).
 * Windows: WinHTTP (WINHTTP_DISABLE_REDIRECTS). */
#include "wk_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *backend = "";
const char *wk_http_backend(void) { return backend; }

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

static int win_get(const char *url, int timeout_ms, int *status, char *loc, size_t locn, char *body, size_t bodyn, char *err, size_t errn) {
    if (strncmp(url, "https://", 8)) { snprintf(err, errn, "https only"); return -1; }
    const char *h = url + 8, *p = strchr(h, '/'); if (!p) p = h + strlen(h);
    wchar_t host[256], path[2048];
    MultiByteToWideChar(CP_UTF8, 0, h, (int)(p - h), host, 255); host[p - h < 255 ? p - h : 255] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, *p ? p : "/", -1, path, 2047)) { snprintf(err, errn, "bad url"); return -1; }
    int rc = -1; HINTERNET s = NULL, c = NULL, r = NULL;
    s = WinHttpOpen(L"DungeonSettlersMod/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"DungeonSettlersMod/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { snprintf(err, errn, "WinHttpOpen failed (%lu)", GetLastError()); goto out; }
    WinHttpSetTimeouts(s, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) { snprintf(err, errn, "WinHttpConnect failed (%lu)", GetLastError()); goto out; }
    r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r) { snprintf(err, errn, "WinHttpOpenRequest failed (%lu)", GetLastError()); goto out; }
    DWORD dis = WINHTTP_DISABLE_REDIRECTS; WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &dis, sizeof dis);
    if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) {
        DWORD e = GetLastError();
        snprintf(err, errn, e == ERROR_WINHTTP_TIMEOUT ? "timed out" : e == ERROR_WINHTTP_NAME_NOT_RESOLVED ? "no internet (name not resolved)" : e == ERROR_WINHTTP_CANNOT_CONNECT ? "could not connect" : "WinHTTP error %lu", e);
        goto out;
    }
    DWORD st = 0, sl = sizeof st;
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX);
    *status = (int)st;
    if (loc && locn) {
        wchar_t wl[2048]; DWORD ll = sizeof wl; loc[0] = 0;
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, wl, &ll, WINHTTP_NO_HEADER_INDEX))
            WideCharToMultiByte(CP_UTF8, 0, wl, -1, loc, (int)locn, NULL, NULL);
    }
    if (body && bodyn) {
        size_t k = 0; DWORD got = 0;
        while (k + 1 < bodyn && WinHttpReadData(r, body + k, (DWORD)(bodyn - 1 - k), &got) && got) k += got;
        body[k] = 0;
    }
    rc = 0;
out:
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); if (s) WinHttpCloseHandle(s);
    return rc;
}
int wk_http_get(const char *url, int timeout_ms, int *status, char *loc, size_t locn, char *body, size_t bodyn, char *err, size_t errn) {
    backend = "WinHTTP"; if (loc && locn) loc[0] = 0; if (body && bodyn) body[0] = 0;
    return win_get(url, timeout_ms, status, loc, locn, body, bodyn, err, errn);
}
void wk_http_probe(const char *url, char *out, size_t n) {
    int st = 0; char loc[512] = "", e[200] = ""; static char body[4096];
    int rc = win_get(url, 5000, &st, loc, sizeof loc, body, sizeof body, e, sizeof e);
    snprintf(out, n, "WinHTTP: %s status %d location %.200s%s%s\n", rc ? "FAIL" : "ok", st, loc, e[0] ? " err " : "", e);
}

#else /* Mac */
#include <dlfcn.h>
#include <time.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>

/* ---- libcurl through dlopen (the option numbers are libcurl's stable ABI: curl.h CURLOPTTYPE_* + n) */
enum { C_URL = 10002, C_WRITEDATA = 10001, C_HEADERDATA = 10029, C_USERAGENT = 10018, C_WRITEFN = 20011, C_HEADERFN = 20079,
       C_FOLLOW = 52, C_NOSIGNAL = 99, C_TIMEOUT_MS = 155, C_CONNECT_MS = 156, C_PROTOCOLS = 181 };
#define C_INFO_CODE 2097154                                              /* CURLINFO_LONG + 2: CURLINFO_RESPONSE_CODE */
typedef struct { char *p; size_t n, cap; } sink;
static size_t w_cb(char *d, size_t sz, size_t nm, void *u) {
    sink *s = u; size_t L = sz * nm;
    if (s->p && s->n + 1 < s->cap) { size_t k = L < s->cap - 1 - s->n ? L : s->cap - 1 - s->n; memcpy(s->p + s->n, d, k); s->n += k; s->p[s->n] = 0; }
    return L;
}
static size_t h_cb(char *d, size_t sz, size_t nm, void *u) {
    sink *s = u; size_t L = sz * nm;
    if (s->p && L > 9 && !strncasecmp(d, "location:", 9)) {
        const char *v = d + 9; size_t k = L - 9; while (k && (*v == ' ' || *v == '\t')) { v++; k--; }
        while (k && (v[k - 1] == '\r' || v[k - 1] == '\n' || v[k - 1] == ' ')) k--;
        if (k >= s->cap) k = s->cap - 1; memcpy(s->p, v, k); s->p[k] = 0;
    }
    return L;
}
static void *curl_lib; static int curl_tried;
static int curl_get(const char *url, int timeout_ms, int *status, char *loc, size_t locn, char *body, size_t bodyn, char *err, size_t errn) {
    if (!curl_tried) { curl_tried = 1; curl_lib = dlopen("/usr/lib/libcurl.4.dylib", RTLD_NOW | RTLD_LOCAL); }
    if (!curl_lib) { snprintf(err, errn, "libcurl not loadable: %s", dlerror() ? dlerror() : "?"); return -1; }
    void *(*init)(void) = (void *(*)(void))dlsym(curl_lib, "curl_easy_init");
    int (*setopt)(void *, int, ...) = (int (*)(void *, int, ...))dlsym(curl_lib, "curl_easy_setopt");
    int (*perform)(void *) = (int (*)(void *))dlsym(curl_lib, "curl_easy_perform");
    int (*getinfo)(void *, int, ...) = (int (*)(void *, int, ...))dlsym(curl_lib, "curl_easy_getinfo");
    void (*cleanup)(void *) = (void (*)(void *))dlsym(curl_lib, "curl_easy_cleanup");
    const char *(*strerr)(int) = (const char *(*)(int))dlsym(curl_lib, "curl_easy_strerror");
    if (!init || !setopt || !perform || !getinfo || !cleanup) { snprintf(err, errn, "libcurl: missing functions"); return -1; }
    void *c = init(); if (!c) { snprintf(err, errn, "curl_easy_init failed"); return -1; }
    char lb[2] = ""; sink sb = { body, 0, body ? bodyn : 0 }, sh = { loc ? loc : lb, 0, loc ? locn : sizeof lb };
    setopt(c, C_URL, url); setopt(c, C_FOLLOW, 0L); setopt(c, C_NOSIGNAL, 1L);
    setopt(c, C_TIMEOUT_MS, (long)timeout_ms); setopt(c, C_CONNECT_MS, (long)timeout_ms);
    setopt(c, C_PROTOCOLS, 2L);                                          /* CURLPROTO_HTTPS only */
    setopt(c, C_USERAGENT, "DungeonSettlersMod/1");
    setopt(c, C_WRITEFN, w_cb); setopt(c, C_WRITEDATA, &sb); setopt(c, C_HEADERFN, h_cb); setopt(c, C_HEADERDATA, &sh);
    int r = perform(c); long code = 0;
    if (r) { snprintf(err, errn, "%s", strerr ? strerr(r) : "curl error"); cleanup(c); return -1; }
    getinfo(c, C_INFO_CODE, &code); *status = (int)code;
    cleanup(c);
    return 0;
}

/* ---- CFNetwork: the stream on this thread's run loop, run until done or the deadline */
typedef struct { int done, failed; char *body; size_t n, cap; } cfctx;
static void cf_cb(CFReadStreamRef s, CFStreamEventType ev, void *u) {
    cfctx *x = u;
    if (ev & kCFStreamEventHasBytesAvailable) {
        UInt8 tmp[4096]; CFIndex k = CFReadStreamRead(s, tmp, sizeof tmp);
        if (k > 0 && x->body && x->n + 1 < x->cap) { size_t m = (size_t)k < x->cap - 1 - x->n ? (size_t)k : x->cap - 1 - x->n; memcpy(x->body + x->n, tmp, m); x->n += m; x->body[x->n] = 0; }
    }
    if (ev & kCFStreamEventEndEncountered) x->done = 1;
    if (ev & kCFStreamEventErrorOccurred) { x->done = 1; x->failed = 1; }
}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static int cf_get(const char *url, int timeout_ms, int *status, char *loc, size_t locn, char *body, size_t bodyn, char *err, size_t errn) {
    CFURLRef u = CFURLCreateWithBytes(NULL, (const UInt8 *)url, (CFIndex)strlen(url), kCFStringEncodingUTF8, NULL);
    if (!u) { snprintf(err, errn, "bad url"); return -1; }
    CFHTTPMessageRef rq = CFHTTPMessageCreateRequest(NULL, CFSTR("GET"), u, kCFHTTPVersion1_1);
    CFHTTPMessageSetHeaderFieldValue(rq, CFSTR("User-Agent"), CFSTR("DungeonSettlersMod/1"));
    CFReadStreamRef s = CFReadStreamCreateForHTTPRequest(NULL, rq);
    CFRelease(rq); CFRelease(u);
    if (!s) { snprintf(err, errn, "CFReadStreamCreateForHTTPRequest failed"); return -1; }
    CFReadStreamSetProperty(s, kCFStreamPropertyHTTPShouldAutoredirect, kCFBooleanFalse);
    cfctx x = { 0, 0, body, 0, body ? bodyn : 0 };
    CFStreamClientContext cc = { 0, &x, NULL, NULL, NULL };
    CFReadStreamSetClient(s, kCFStreamEventHasBytesAvailable | kCFStreamEventEndEncountered | kCFStreamEventErrorOccurred, cf_cb, &cc);
    CFStringRef mode = CFSTR("dsmp.wk_http");                           /* a private mode: nothing else of the game runs inside */
    CFReadStreamScheduleWithRunLoop(s, CFRunLoopGetCurrent(), mode);
    int rc = -1;
    if (!CFReadStreamOpen(s)) { snprintf(err, errn, "could not open the connection"); goto out; }
    CFAbsoluteTime end = CFAbsoluteTimeGetCurrent() + timeout_ms / 1000.0;
    while (!x.done) {
        double left = end - CFAbsoluteTimeGetCurrent(); if (left <= 0) break;
        CFRunLoopRunInMode(mode, left < 0.25 ? left : 0.25, true);
    }
    if (!x.done) { snprintf(err, errn, "timed out"); goto out; }
    if (x.failed) {
        CFErrorRef e = CFReadStreamCopyError(s); long code = e ? (long)CFErrorGetCode(e) : 0;
        char dom[80] = ""; if (e) { CFStringRef d = CFErrorGetDomain(e); if (d) CFStringGetCString(d, dom, sizeof dom, kCFStringEncodingUTF8); CFRelease(e); }
        snprintf(err, errn, "%s (%s %ld)", code == -1009 || code == 8 || code == 12 ? "no internet" : "connection failed", dom, code);
        goto out;
    }
    {
        CFHTTPMessageRef rs = (CFHTTPMessageRef)CFReadStreamCopyProperty(s, kCFStreamPropertyHTTPResponseHeader);
        if (!rs) { snprintf(err, errn, "no HTTP answer"); goto out; }
        *status = (int)CFHTTPMessageGetResponseStatusCode(rs);
        if (loc && locn) {
            CFStringRef l = CFHTTPMessageCopyHeaderFieldValue(rs, CFSTR("Location"));
            loc[0] = 0; if (l) { CFStringGetCString(l, loc, (CFIndex)locn, kCFStringEncodingUTF8); CFRelease(l); }
        }
        CFRelease(rs);
        rc = 0;
    }
out:
    CFReadStreamSetClient(s, 0, NULL, NULL);
    CFReadStreamUnscheduleFromRunLoop(s, CFRunLoopGetCurrent(), mode);
    CFReadStreamClose(s); CFRelease(s);
    return rc;
}
#pragma clang diagnostic pop

/* order: DSMP_HTTP, else CFNetwork first (already in the game process: Unity's own web requests use it), libcurl
   second (see ext/myunits/NOTES.md "HTTP inside the game" for the in-game probe) */
int wk_http_get(const char *url, int timeout_ms, int *status, char *loc, size_t locn, char *body, size_t bodyn, char *err, size_t errn) {
    const char *pref = getenv("DSMP_HTTP");
    int curl_first = pref && !strcmp(pref, "curl");
    char e1[200] = "";
    for (int k = 0; k < 2; k++) {
        int use_curl = (k == 0) == curl_first;
        if (pref && k == 1 && (!strcmp(pref, "curl") || !strcmp(pref, "cf"))) break;   /* forced: one backend only */
        if (loc && locn) loc[0] = 0; if (body && bodyn) body[0] = 0;
        int r = use_curl ? curl_get(url, timeout_ms, status, loc, locn, body, bodyn, e1, sizeof e1)
                         : cf_get(url, timeout_ms, status, loc, locn, body, bodyn, e1, sizeof e1);
        backend = use_curl ? "libcurl" : "CFNetwork";
        if (!r) return 0;
        /* a network answer of "no internet" / timeout is the same for both: do not wait twice */
        if (strstr(e1, "timed out") || strstr(e1, "no internet") || strstr(e1, "Timeout") || strstr(e1, "resolve")) break;
    }
    snprintf(err, errn, "%s", e1);
    return -1;
}
void wk_http_probe(const char *url, char *out, size_t n) {
    size_t o = 0; static char body[8192];
    for (int k = 0; k < 2; k++) {
        int st = 0; char loc[512] = "", e[200] = "";
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        int rc = k ? curl_get(url, 5000, &st, loc, sizeof loc, body, sizeof body, e, sizeof e) : cf_get(url, 5000, &st, loc, sizeof loc, body, sizeof body, e, sizeof e);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
        o += (size_t)snprintf(out + o, o < n ? n - o : 0, "%s: %s status %d in %.0f ms, location %.160s, body %zu bytes%s%s\n",
                              k ? "libcurl" : "CFNetwork", rc ? "FAIL" : "ok", st, ms, loc, strlen(body), e[0] ? ", err " : "", e);
        if (o >= n) break;
    }
}
#endif
