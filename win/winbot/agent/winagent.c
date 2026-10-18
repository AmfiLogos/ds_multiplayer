/* winagent.exe - Windows guest agent for winbot (see ../PROTOCOL.md, the wire contract).
   Opens N outbound TCP connections to the hub, sends a hello line on each, then serves one JSON request per line.
   GUI subsystem, never creates a window. Never deletes a file. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif

#define AGENT_VER 1
#define MAX_CONN 32
#define OUT_CAP (1024 * 1024)

/* ------------------------------------------------------------------ globals */
static char g_host[256], g_port[16], g_token[512];
static int g_nconn = 4;
static long long g_idle_min = 720;
static volatile LONG g_connected = 0;
static volatile LONG g_exiting = 0;
static volatile ULONGLONG g_last_active;
static SOCKET g_socks[MAX_CONN];
static CRITICAL_SECTION g_log_cs, g_inherit_cs, g_sock_cs;
static WCHAR g_logpath[MAX_PATH + 32];
static char g_computer[256], g_user[256];

/* ------------------------------------------------------------------ utf8 / utf16 */
static WCHAR *u8to16(const char *s) {
    if (!s) s = "";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = (WCHAR *)malloc((size_t)(n > 0 ? n : 1) * sizeof(WCHAR));
    if (n <= 0) { w[0] = 0; return w; }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
static char *w16to8n(const WCHAR *w, int wlen) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)(n > 0 ? n : 0) + 1);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, wlen, s, n, NULL, NULL);
    s[n > 0 ? n : 0] = 0;
    return s;
}
static char *w16to8(const WCHAR *w) { return w16to8n(w, (int)wcslen(w)); }
/* UTF-8 path -> wide, with %VAR% expanded */
static WCHAR *expand_path(const char *s) {
    WCHAR *w = u8to16(s);
    DWORD n = ExpandEnvironmentStringsW(w, NULL, 0);
    if (!n) return w;
    WCHAR *e = (WCHAR *)malloc((size_t)n * sizeof(WCHAR));
    if (!ExpandEnvironmentStringsW(w, e, n)) { free(e); return w; }
    free(w);
    return e;
}

/* ------------------------------------------------------------------ log */
static void logf_(const char *fmt, ...) {
    char buf[2048];
    SYSTEMTIME st; GetLocalTime(&st);
    int n = snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d [%lu] ", st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
    va_list ap; va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof buf - (size_t)n - 3, fmt, ap);
    va_end(ap);
    if (m < 0) m = 0;
    n += m; if (n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\r'; buf[n++] = '\n';
    EnterCriticalSection(&g_log_cs);
    HANDLE h = CreateFileW(g_logpath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(h, buf, (DWORD)n, &wr, NULL); CloseHandle(h); }
    LeaveCriticalSection(&g_log_cs);
}

/* ------------------------------------------------------------------ growable buffer + JSON writer */
typedef struct { char *p; size_t n, cap; } buf_t;
static void bput(buf_t *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 256;
        while (c < b->n + n + 1) c *= 2;
        b->p = (char *)realloc(b->p, c); b->cap = c;
    }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void bstr(buf_t *b, const char *s) { bput(b, s, strlen(s)); }
static void bfmt(buf_t *b, const char *fmt, ...) {
    char tmp[512]; va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n > 0) bput(b, tmp, (size_t)(n < (int)sizeof tmp ? n : (int)sizeof tmp - 1));
}
/* JSON string with escapes; invalid UTF-8 bytes become U+FFFD so the line stays valid UTF-8 */
static void bjstr_n(buf_t *b, const char *s, size_t n) {
    static const char hex[] = "0123456789abcdef";
    bput(b, "\"", 1);
    size_t i = 0, run = 0;
    const unsigned char *u = (const unsigned char *)s;
    while (i < n) {
        unsigned c = u[i];
        if (c >= 0x20 && c < 0x80 && c != '"' && c != '\\') { i++; run++; continue; }
        if (run) { bput(b, s + i - run, run); run = 0; }
        if (c < 0x80) {
            char e[8];
            if (c == '"') bput(b, "\\\"", 2);
            else if (c == '\\') bput(b, "\\\\", 2);
            else if (c == '\n') bput(b, "\\n", 2);
            else if (c == '\r') bput(b, "\\r", 2);
            else if (c == '\t') bput(b, "\\t", 2);
            else { e[0] = '\\'; e[1] = 'u'; e[2] = '0'; e[3] = '0'; e[4] = hex[c >> 4]; e[5] = hex[c & 15]; bput(b, e, 6); }
            i++; continue;
        }
        /* validate one UTF-8 sequence */
        int len = c >= 0xF0 && c <= 0xF4 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c <= 0xDF ? 2 : 0;
        if (c > 0xF4) len = 0;
        int ok = len > 0 && i + (size_t)len <= n;
        for (int k = 1; ok && k < len; k++) if ((u[i + k] & 0xC0) != 0x80) ok = 0;
        if (ok && len == 3) {
            if (c == 0xE0 && u[i + 1] < 0xA0) ok = 0;
            if (c == 0xED && u[i + 1] >= 0xA0) ok = 0;
        }
        if (ok && len == 4) {
            if (c == 0xF0 && u[i + 1] < 0x90) ok = 0;
            if (c == 0xF4 && u[i + 1] >= 0x90) ok = 0;
        }
        if (ok) { bput(b, s + i, (size_t)len); i += (size_t)len; }
        else { bput(b, "\xEF\xBF\xBD", 3); i++; }
    }
    if (run) bput(b, s + n - run, run);
    bput(b, "\"", 1);
}
static void bjstr(buf_t *b, const char *s) { bjstr_n(b, s ? s : "", s ? strlen(s) : 0); }
static void kstr(buf_t *b, const char *k, const char *v) { bfmt(b, ",\"%s\":", k); bjstr(b, v); }
static void kint(buf_t *b, const char *k, long long v) { bfmt(b, ",\"%s\":%lld", k, v); }
static void kbool(buf_t *b, const char *k, int v) { bfmt(b, ",\"%s\":%s", k, v ? "true" : "false"); }

/* ------------------------------------------------------------------ JSON reader (flat object) */
enum { JT_NONE, JT_STR, JT_NUM, JT_BOOL, JT_NULL, JT_ARR, JT_OTHER };
typedef struct { char *key; int type; char *s; long long i; int b; char **arr; int narr; } jval_t;
typedef struct { jval_t *v; int n; } jobj_t;

static void skip_ws(const char **p) { while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++; }
static void put_utf8(buf_t *b, unsigned cp) {
    char t[4]; int n;
    if (cp < 0x80) { t[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else if (cp < 0x10000) { t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 63)); t[2] = (char)(0x80 | (cp & 63)); n = 3; }
    else { t[0] = (char)(0xF0 | (cp >> 18)); t[1] = (char)(0x80 | ((cp >> 12) & 63)); t[2] = (char)(0x80 | ((cp >> 6) & 63)); t[3] = (char)(0x80 | (cp & 63)); n = 4; }
    bput(b, t, (size_t)n);
}
static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i]; v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v; return 1;
}
/* parses a JSON string at *p (which points at the opening quote); returns malloc'd UTF-8 or NULL */
static char *parse_str(const char **p) {
    if (**p != '"') return NULL;
    (*p)++;
    buf_t b = {0}; bput(&b, "", 0);
    for (;;) {
        char c = **p;
        if (!c) { free(b.p); return NULL; }
        if (c == '"') { (*p)++; return b.p; }
        if (c != '\\') {
            const char *s = *p;
            while (**p && **p != '"' && **p != '\\') (*p)++;
            bput(&b, s, (size_t)(*p - s));
            continue;
        }
        (*p)++;
        c = **p; (*p)++;
        switch (c) {
        case '"': bput(&b, "\"", 1); break;
        case '\\': bput(&b, "\\", 1); break;
        case '/': bput(&b, "/", 1); break;
        case 'b': bput(&b, "\b", 1); break;
        case 'f': bput(&b, "\f", 1); break;
        case 'n': bput(&b, "\n", 1); break;
        case 'r': bput(&b, "\r", 1); break;
        case 't': bput(&b, "\t", 1); break;
        case 'u': {
            unsigned cp, lo;
            if (!hex4(*p, &cp)) { free(b.p); return NULL; }
            *p += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && (*p)[0] == '\\' && (*p)[1] == 'u' && hex4(*p + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                *p += 6;
            } else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
            put_utf8(&b, cp);
            break;
        }
        default: free(b.p); return NULL;
        }
    }
}
static int skip_value(const char **p);
static int skip_compound(const char **p, char open, char close) {
    (*p)++;
    skip_ws(p);
    if (**p == close) { (*p)++; return 1; }
    for (;;) {
        skip_ws(p);
        if (open == '{') {
            char *k = parse_str(p); if (!k) return 0; free(k);
            skip_ws(p); if (**p != ':') return 0; (*p)++;
        }
        if (!skip_value(p)) return 0;
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == close) { (*p)++; return 1; }
        return 0;
    }
}
static int skip_value(const char **p) {
    skip_ws(p);
    char c = **p;
    if (c == '"') { char *s = parse_str(p); if (!s) return 0; free(s); return 1; }
    if (c == '{') return skip_compound(p, '{', '}');
    if (c == '[') return skip_compound(p, '[', ']');
    if (!strncmp(*p, "true", 4) || !strncmp(*p, "null", 4)) { *p += 4; return 1; }
    if (!strncmp(*p, "false", 5)) { *p += 5; return 1; }
    if (c == '-' || (c >= '0' && c <= '9')) { strtod(*p, (char **)p); return 1; }
    return 0;
}
static void jfree(jobj_t *o) {
    for (int i = 0; i < o->n; i++) {
        free(o->v[i].key); free(o->v[i].s);
        for (int k = 0; k < o->v[i].narr; k++) free(o->v[i].arr[k]);
        free(o->v[i].arr);
    }
    free(o->v); o->v = NULL; o->n = 0;
}
static int jparse(const char *text, jobj_t *o) {
    const char *p = text;
    o->v = NULL; o->n = 0;
    skip_ws(&p);
    if (*p != '{') return 0;
    p++; skip_ws(&p);
    if (*p == '}') return 1;
    for (;;) {
        skip_ws(&p);
        jval_t v; memset(&v, 0, sizeof v);
        v.key = parse_str(&p);
        if (!v.key) return 0;
        skip_ws(&p); if (*p != ':') { free(v.key); return 0; } p++;
        skip_ws(&p);
        char c = *p;
        if (c == '"') { v.type = JT_STR; v.s = parse_str(&p); if (!v.s) { free(v.key); return 0; } }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            v.type = JT_NUM;
            const char *s = p; char *e;
            double d = strtod(s, &e);
            long long ll = strtoll(s, NULL, 10);
            int isint = 1;
            for (const char *q = s; q < e; q++) if (*q == '.' || *q == 'e' || *q == 'E') isint = 0;
            v.i = isint ? ll : (long long)d;
            p = e;
        } else if (!strncmp(p, "true", 4)) { v.type = JT_BOOL; v.b = 1; p += 4; }
        else if (!strncmp(p, "false", 5)) { v.type = JT_BOOL; v.b = 0; p += 5; }
        else if (!strncmp(p, "null", 4)) { v.type = JT_NULL; p += 4; }
        else if (c == '[') {
            /* array of strings; other items are skipped */
            const char *save = p;
            v.type = JT_ARR;
            p++; skip_ws(&p);
            int ok = 1;
            if (*p == ']') p++;
            else for (;;) {
                skip_ws(&p);
                if (*p == '"') {
                    char *s = parse_str(&p); if (!s) { ok = 0; break; }
                    v.arr = (char **)realloc(v.arr, sizeof(char *) * (size_t)(v.narr + 1));
                    v.arr[v.narr++] = s;
                } else if (!skip_value(&p)) { ok = 0; break; }
                skip_ws(&p);
                if (*p == ',') { p++; continue; }
                if (*p == ']') { p++; break; }
                ok = 0; break;
            }
            (void)save;
            if (!ok) { free(v.key); for (int k = 0; k < v.narr; k++) free(v.arr[k]); free(v.arr); return 0; }
        } else if (c == '{') { v.type = JT_OTHER; if (!skip_value(&p)) { free(v.key); return 0; } }
        else { free(v.key); return 0; }
        o->v = (jval_t *)realloc(o->v, sizeof(jval_t) * (size_t)(o->n + 1));
        o->v[o->n++] = v;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') return 1;
        return 0;
    }
}
static jval_t *jget(jobj_t *o, const char *k) {
    for (int i = o->n - 1; i >= 0; i--) if (!strcmp(o->v[i].key, k)) return &o->v[i];
    return NULL;
}
static const char *jstr(jobj_t *o, const char *k) { jval_t *v = jget(o, k); return v && v->type == JT_STR ? v->s : NULL; }
static long long jint(jobj_t *o, const char *k, long long d) {
    jval_t *v = jget(o, k);
    if (!v) return d;
    if (v->type == JT_NUM) return v->i;
    if (v->type == JT_STR && v->s[0]) return strtoll(v->s, NULL, 10);
    return d;
}
static int jhas(jobj_t *o, const char *k) { jval_t *v = jget(o, k); return v && v->type != JT_NULL; }
static int jbool(jobj_t *o, const char *k, int d) {
    jval_t *v = jget(o, k);
    if (!v) return d;
    if (v->type == JT_BOOL) return v->b;
    if (v->type == JT_NUM) return v->i != 0;
    return d;
}

/* ------------------------------------------------------------------ base64 */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64enc(buf_t *b, const unsigned char *d, size_t n) {
    size_t outn = 4 * ((n + 2) / 3);
    if (b->n + outn + 1 > b->cap) { size_t c = b->n + outn + 1; b->p = (char *)realloc(b->p, c); b->cap = c; }
    char *o = b->p + b->n;
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        unsigned v = (unsigned)d[i] << 16 | (unsigned)d[i + 1] << 8 | d[i + 2];
        *o++ = B64[v >> 18]; *o++ = B64[(v >> 12) & 63]; *o++ = B64[(v >> 6) & 63]; *o++ = B64[v & 63];
    }
    if (i < n) {
        unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0);
        *o++ = B64[v >> 18]; *o++ = B64[(v >> 12) & 63];
        *o++ = i + 1 < n ? B64[(v >> 6) & 63] : '=';
        *o++ = '=';
    }
    b->n += outn; b->p[b->n] = 0;
}
static unsigned char *b64dec(const char *s, size_t *outn) {
    static signed char T[256];
    static int init = 0;
    if (!init) { memset(T, -1, sizeof T); for (int i = 0; i < 64; i++) T[(unsigned char)B64[i]] = (signed char)i; T['-'] = 62; T['_'] = 63; init = 1; }
    size_t n = strlen(s);
    unsigned char *o = (unsigned char *)malloc(n / 4 * 3 + 4);
    size_t k = 0; unsigned acc = 0; int bits = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '=') break;
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (T[c] < 0) { free(o); return NULL; }
        acc = acc << 6 | (unsigned)T[c]; bits += 6;
        if (bits >= 8) { bits -= 8; o[k++] = (unsigned char)(acc >> bits); }
    }
    *outn = k; return o;
}

/* ------------------------------------------------------------------ helpers */
static long long ft_unix(FILETIME ft) {
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    if (!u.QuadPart) return 0;
    return (long long)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}
static double now_unix(void) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
}
static void set_err(char *err, const char *what, DWORD code) {
    if (code) snprintf(err, 256, "%s (%lu)", what, (unsigned long)code);
    else snprintf(err, 256, "%s", what);
}
/* create every missing parent folder of a file path */
static void make_parents(const WCHAR *path) {
    size_t n = wcslen(path);
    WCHAR *t = (WCHAR *)malloc((n + 1) * sizeof(WCHAR));
    wcscpy(t, path);
    WCHAR *last = NULL;
    for (WCHAR *q = t; *q; q++) if (*q == L'\\' || *q == L'/') last = q;
    if (!last) { free(t); return; }
    *last = 0;
    for (WCHAR *q = t; *q; q++) {
        if ((*q == L'\\' || *q == L'/') && q > t && q[-1] != L':' && !(q == t + 1 && t[0] == L'\\')) {
            WCHAR c = *q; *q = 0; CreateDirectoryW(t, NULL); *q = c;
        }
    }
    CreateDirectoryW(t, NULL);
    free(t);
}
static int name_match(const WCHAR *exe, const WCHAR *want) {
    if (!_wcsicmp(exe, want)) return 1;
    size_t n = wcslen(want);
    if (n < 4 || _wcsicmp(want + n - 4, L".exe")) {
        size_t e = wcslen(exe);
        if (e == n + 4 && !_wcsnicmp(exe, want, n) && !_wcsicmp(exe + n, L".exe")) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ ops */
typedef int (*op_fn)(jobj_t *, buf_t *, char *);

static int op_ping(jobj_t *r, buf_t *b, char *err) {
    (void)r; (void)err;
    kint(b, "pid", (long long)GetCurrentProcessId());
    kint(b, "ver", AGENT_VER);
    bfmt(b, ",\"time\":%.3f", now_unix());
    kstr(b, "computer", g_computer);
    kstr(b, "user", g_user);
    return 1;
}

static int read_port_file(const char *pf, int *port, char *err) {
    WCHAR *w = expand_path(pf);
    HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE) { set_err(err, "port_file open failed", GetLastError()); return 0; }
    char t[64] = {0}; DWORD rd = 0;
    ReadFile(h, t, sizeof t - 1, &rd, NULL); CloseHandle(h);
    t[rd] = 0;
    char *q = t; while (*q && (*q < '0' || *q > '9')) q++;
    int p = atoi(q);
    if (p <= 0 || p > 65535) { set_err(err, "port_file holds no port", 0); return 0; }
    *port = p; return 1;
}
static int op_game(jobj_t *r, buf_t *b, char *err) {
    const char *line = jstr(r, "line");
    if (!line) { set_err(err, "line missing", 0); return 0; }
    int port = (int)jint(r, "port", 0);
    const char *pf = jstr(r, "port_file");
    if (!port && pf && !read_port_file(pf, &port, err)) return 0;
    if (port <= 0 || port > 65535) { set_err(err, "port or port_file missing", 0); return 0; }
    long long tmo = jint(r, "timeout_ms", 90000);
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)(tmo > 0 ? tmo : 0);
    SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) { set_err(err, "socket failed", (DWORD)WSAGetLastError()); return 0; }
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((unsigned short)port);
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    int ok = 0;
    if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) ok = 1;
    else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        LONGLONG left = (LONGLONG)(deadline - GetTickCount64());
        if (left < 0) left = 0;
        fd_set wf, ef; FD_ZERO(&wf); FD_ZERO(&ef); FD_SET(s, &wf); FD_SET(s, &ef);
        struct timeval tv = { (long)(left / 1000), (long)((left % 1000) * 1000) };
        int n = select(0, NULL, &wf, &ef, &tv);
        if (n > 0 && FD_ISSET(s, &wf)) ok = 1;
        else if (n == 0) { set_err(err, "game connect timeout", 0); closesocket(s); return 0; }
    }
    if (!ok) {
        int so = 0, sl = sizeof so; getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&so, &sl);
        set_err(err, "game connect failed", (DWORD)(so ? so : WSAGetLastError()));
        closesocket(s); return 0;
    }
    nb = 0; ioctlsocket(s, FIONBIO, &nb);
    int nd = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&nd, sizeof nd);
    size_t ln = strlen(line);
    char *out = (char *)malloc(ln + 2); memcpy(out, line, ln); out[ln] = '\n';
    size_t sent = 0;
    while (sent < ln + 1) {
        int k = send(s, out + sent, (int)(ln + 1 - sent), 0);
        if (k <= 0) { free(out); set_err(err, "game send failed", (DWORD)WSAGetLastError()); closesocket(s); return 0; }
        sent += (size_t)k;
    }
    free(out);
    buf_t rb = {0};
    for (;;) {
        LONGLONG left = (LONGLONG)(deadline - GetTickCount64());
        if (left <= 0) { free(rb.p); set_err(err, "game timeout", 0); closesocket(s); return 0; }
        fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
        struct timeval tv = { (long)(left / 1000), (long)((left % 1000) * 1000) };
        int n = select(0, &rf, NULL, NULL, &tv);
        if (n == 0) continue;
        if (n < 0) { free(rb.p); set_err(err, "game select failed", (DWORD)WSAGetLastError()); closesocket(s); return 0; }
        char tmp[8192];
        int k = recv(s, tmp, sizeof tmp, 0);
        if (k <= 0) {
            if (rb.n) break; /* closed without newline: return what came */
            free(rb.p); set_err(err, "game closed without answer", k < 0 ? (DWORD)WSAGetLastError() : 0); closesocket(s); return 0;
        }
        bput(&rb, tmp, (size_t)k);
        char *nl = memchr(rb.p, '\n', rb.n);
        if (nl) { rb.n = (size_t)(nl - rb.p); break; }
    }
    closesocket(s);
    if (rb.n && rb.p[rb.n - 1] == '\r') rb.n--;
    kint(b, "port", port);
    bstr(b, ",\"resp\":"); bjstr_n(b, rb.p ? rb.p : "", rb.n);
    free(rb.p);
    return 1;
}

/* --- run */
typedef struct { HANDLE h; buf_t data; volatile LONG done; } reader_t;
static DWORD WINAPI reader_thread(LPVOID arg) {
    reader_t *rd = (reader_t *)arg;
    char tmp[16384]; DWORD n;
    while (ReadFile(rd->h, tmp, sizeof tmp, &n, NULL) && n) {
        if (rd->data.n < OUT_CAP) bput(&rd->data, tmp, n > OUT_CAP - rd->data.n ? OUT_CAP - rd->data.n : n);
    }
    InterlockedExchange(&rd->done, 1);
    return 0;
}
static int env_name_len(const WCHAR *e) {
    const WCHAR *q = wcschr(e + (e[0] == L'=' ? 1 : 0), L'=');
    return q ? (int)(q - e) : (int)wcslen(e);
}
static int env_cmp(const void *a, const void *b) {
    const WCHAR *x = *(const WCHAR *const *)a, *y = *(const WCHAR *const *)b;
    int nx = env_name_len(x), ny = env_name_len(y);
    int c = CompareStringOrdinal(x, nx, y, ny, TRUE);
    return c - 2;
}
static WCHAR *build_env(jval_t *ev) {
    WCHAR *cur = GetEnvironmentStringsW();
    int n = 0, cap = 64;
    WCHAR **list = (WCHAR **)malloc(sizeof(WCHAR *) * (size_t)cap);
    for (WCHAR *q = cur; *q; q += wcslen(q) + 1) {
        if (n == cap) { cap *= 2; list = (WCHAR **)realloc(list, sizeof(WCHAR *) * (size_t)cap); }
        list[n++] = _wcsdup(q);
    }
    FreeEnvironmentStringsW(cur);
    for (int i = 0; i < ev->narr; i++) {
        WCHAR *kv = u8to16(ev->arr[i]);
        int kl = env_name_len(kv);
        if (!kl || !kv[kl]) { free(kv); continue; } /* needs K=... */
        int j;
        for (j = 0; j < n; j++) {
            int jl = env_name_len(list[j]);
            if (CompareStringOrdinal(list[j], jl, kv, kl, TRUE) == CSTR_EQUAL) { free(list[j]); list[j] = kv; break; }
        }
        if (j == n) {
            if (n == cap) { cap *= 2; list = (WCHAR **)realloc(list, sizeof(WCHAR *) * (size_t)cap); }
            list[n++] = kv;
        }
    }
    qsort(list, (size_t)n, sizeof(WCHAR *), env_cmp);
    size_t tot = 1;
    for (int i = 0; i < n; i++) tot += wcslen(list[i]) + 1;
    WCHAR *blk = (WCHAR *)malloc((tot + 1) * sizeof(WCHAR)), *o = blk;
    for (int i = 0; i < n; i++) { size_t l = wcslen(list[i]); memcpy(o, list[i], (l + 1) * sizeof(WCHAR)); o += l + 1; free(list[i]); }
    *o++ = 0; *o = 0;
    free(list);
    return blk;
}
static int op_run(jobj_t *r, buf_t *b, char *err) {
    const char *exe = jstr(r, "exe");
    if (!exe || !exe[0]) { set_err(err, "exe missing", 0); return 0; }
    const char *args = jstr(r, "args");
    const char *cwd = jstr(r, "cwd");
    jval_t *env = jget(r, "env");
    long long wait_ms = jint(r, "wait_ms", 0);
    int capture = jbool(r, "capture", 0) && wait_ms > 0;
    int show = jbool(r, "show", 0);

    WCHAR *wexe = expand_path(exe);
    WCHAR *wargs = u8to16(args ? args : "");
    size_t cl = wcslen(wexe) + wcslen(wargs) + 8;
    WCHAR *cmd = (WCHAR *)malloc(cl * sizeof(WCHAR));
    if (wargs[0]) swprintf(cmd, cl, L"\"%ls\" %ls", wexe, wargs);
    else swprintf(cmd, cl, L"\"%ls\"", wexe);
    WCHAR *wcwd = cwd && cwd[0] ? expand_path(cwd) : NULL;
    WCHAR *envblk = env && env->type == JT_ARR ? build_env(env) : NULL;

    STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    HANDLE rdh = NULL, wrh = NULL, nul = INVALID_HANDLE_VALUE;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | (show ? 0 : CREATE_NO_WINDOW);
    BOOL okc;
    if (capture) {
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, FALSE };
        if (!CreatePipe(&rdh, &wrh, &sa, 0)) {
            set_err(err, "CreatePipe failed", GetLastError());
            free(wexe); free(wargs); free(cmd); free(wcwd); free(envblk); return 0;
        }
        nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nul; si.hStdOutput = wrh; si.hStdError = wrh;
        /* only these two handles are inheritable, and only while this CreateProcess runs */
        EnterCriticalSection(&g_inherit_cs);
        SetHandleInformation(wrh, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        if (nul != INVALID_HANDLE_VALUE) SetHandleInformation(nul, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        okc = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, flags, envblk, wcwd, &si, &pi);
        DWORD e = GetLastError();
        SetHandleInformation(wrh, HANDLE_FLAG_INHERIT, 0);
        if (nul != INVALID_HANDLE_VALUE) SetHandleInformation(nul, HANDLE_FLAG_INHERIT, 0);
        LeaveCriticalSection(&g_inherit_cs);
        SetLastError(e);
    } else {
        EnterCriticalSection(&g_inherit_cs); /* an inheritable pipe of another thread must not leak in */
        okc = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, flags, envblk, wcwd, &si, &pi);
        DWORD e = GetLastError();
        LeaveCriticalSection(&g_inherit_cs);
        SetLastError(e);
    }
    DWORD cerr = GetLastError();
    free(wexe); free(wargs); free(cmd); free(wcwd); free(envblk);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (wrh) CloseHandle(wrh);
    if (!okc) {
        if (rdh) CloseHandle(rdh);
        set_err(err, "CreateProcess failed", cerr); return 0;
    }
    CloseHandle(pi.hThread);
    kint(b, "pid", (long long)pi.dwProcessId);
    if (wait_ms <= 0) { CloseHandle(pi.hProcess); return 1; }

    reader_t *rd = NULL; HANDLE rt = NULL;
    if (capture) {
        rd = (reader_t *)calloc(1, sizeof *rd);
        rd->h = rdh;
        rt = CreateThread(NULL, 0, reader_thread, rd, 0, NULL);
    }
    DWORD w = WaitForSingleObject(pi.hProcess, (DWORD)(wait_ms > 0x7FFFFFFF ? 0x7FFFFFFF : wait_ms));
    if (w == WAIT_OBJECT_0) {
        DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
        kint(b, "exit", (long long)(int)code);
    } else kbool(b, "timeout", 1);
    CloseHandle(pi.hProcess);
    if (capture) {
        /* a grandchild may hold the pipe open: give the reader 2 s, then cut it off */
        if (WaitForSingleObject(rt, w == WAIT_OBJECT_0 ? 2000 : 200) != WAIT_OBJECT_0) {
            CancelSynchronousIo(rt);
            WaitForSingleObject(rt, 1000);
        }
        int finished = rd->done;
        size_t n = rd->data.n;
        char *raw = (char *)malloc(n + 1); if (n) memcpy(raw, rd->data.p, n); raw[n] = 0;
        if (finished) { CloseHandle(rt); CloseHandle(rdh); free(rd->data.p); free(rd); }
        else CloseHandle(rt); /* reader still owns rd and rdh: leak them rather than free memory in use */
        int wl = n ? MultiByteToWideChar(CP_OEMCP, 0, raw, (int)n, NULL, 0) : 0;
        char *u8;
        if (wl > 0) {
            WCHAR *wb = (WCHAR *)malloc((size_t)wl * sizeof(WCHAR));
            MultiByteToWideChar(CP_OEMCP, 0, raw, (int)n, wb, wl);
            u8 = w16to8n(wb, wl); free(wb);
        } else { u8 = (char *)malloc(1); u8[0] = 0; }
        free(raw);
        size_t ul = strlen(u8);
        if (ul > OUT_CAP) { ul = OUT_CAP; while (ul && ((unsigned char)u8[ul] & 0xC0) == 0x80) ul--; }
        bstr(b, ",\"out\":"); bjstr_n(b, u8, ul);
        free(u8);
    }
    return 1;
}

/* --- ps / kill */
typedef struct { DWORD pid; HWND h; } win_t;
typedef struct { win_t *v; int n, cap; } winlist_t;
static BOOL CALLBACK enum_win(HWND h, LPARAM lp) {
    winlist_t *wl = (winlist_t *)lp;
    if (!IsWindowVisible(h)) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (wl->n == wl->cap) { wl->cap = wl->cap ? wl->cap * 2 : 64; wl->v = (win_t *)realloc(wl->v, sizeof(win_t) * (size_t)wl->cap); }
    wl->v[wl->n].pid = pid; wl->v[wl->n].h = h; wl->n++;
    return TRUE;
}
static int op_ps(jobj_t *r, buf_t *b, char *err) {
    const char *name = jstr(r, "name");
    WCHAR *wname = name && name[0] ? u8to16(name) : NULL;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { free(wname); set_err(err, "CreateToolhelp32Snapshot failed", GetLastError()); return 0; }
    winlist_t wl = {0};
    EnumWindows(enum_win, (LPARAM)&wl);
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    bstr(b, ",\"procs\":[");
    int first = 1;
    for (BOOL k = Process32FirstW(snap, &pe); k; k = Process32NextW(snap, &pe)) {
        if (wname && !name_match(pe.szExeFile, wname)) continue;
        char *pn = w16to8(pe.szExeFile);
        bfmt(b, "%s{\"pid\":%lu,\"ppid\":%lu,\"name\":", first ? "" : ",", (unsigned long)pe.th32ProcessID, (unsigned long)pe.th32ParentProcessID);
        bjstr(b, pn); free(pn);
        bstr(b, ",\"windows\":[");
        int wf = 1;
        for (int i = 0; i < wl.n; i++) {
            if (wl.v[i].pid != pe.th32ProcessID) continue;
            WCHAR t[512]; t[0] = 0; GetWindowTextW(wl.v[i].h, t, 512);
            RECT rc; memset(&rc, 0, sizeof rc); GetWindowRect(wl.v[i].h, &rc);
            char *tu = w16to8(t);
            bstr(b, wf ? "{\"title\":" : ",{\"title\":"); bjstr(b, tu); free(tu);
            bfmt(b, ",\"x\":%ld,\"y\":%ld,\"w\":%ld,\"h\":%ld,\"hung\":%s}", (long)rc.left, (long)rc.top,
                 (long)(rc.right - rc.left), (long)(rc.bottom - rc.top), IsHungAppWindow(wl.v[i].h) ? "true" : "false");
            wf = 0;
        }
        bstr(b, "]}");
        first = 0;
    }
    bstr(b, "]");
    CloseHandle(snap); free(wl.v); free(wname);
    return 1;
}
static int kill_pid(DWORD pid, DWORD *e) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) { *e = GetLastError(); return 0; }
    BOOL ok = TerminateProcess(h, 1);
    if (!ok) *e = GetLastError();
    CloseHandle(h);
    return ok ? 1 : 0;
}
static int op_kill(jobj_t *r, buf_t *b, char *err) {
    long long pid = jint(r, "pid", 0);
    const char *name = jstr(r, "name");
    DWORD self = GetCurrentProcessId();
    if (pid > 0) {
        if ((DWORD)pid == self) { set_err(err, "refusing to kill the agent", 0); return 0; }
        DWORD e = 0;
        if (!kill_pid((DWORD)pid, &e)) { set_err(err, "kill failed", e); return 0; }
        bfmt(b, ",\"killed\":[%lld]", pid);
        return 1;
    }
    if (!name || !name[0]) { set_err(err, "pid or name missing", 0); return 0; }
    WCHAR *wname = u8to16(name);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { free(wname); set_err(err, "CreateToolhelp32Snapshot failed", GetLastError()); return 0; }
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    bstr(b, ",\"killed\":[");
    int first = 1;
    for (BOOL k = Process32FirstW(snap, &pe); k; k = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == self || !name_match(pe.szExeFile, wname)) continue;
        DWORD e = 0;
        if (kill_pid(pe.th32ProcessID, &e)) { bfmt(b, "%s%lu", first ? "" : ",", (unsigned long)pe.th32ProcessID); first = 0; }
        else logf_("kill %lu failed (%lu)", (unsigned long)pe.th32ProcessID, (unsigned long)e);
    }
    bstr(b, "]");
    CloseHandle(snap); free(wname);
    return 1;
}

/* --- files */
static HANDLE open_read(const WCHAR *w) {
    return CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}
static int op_read(jobj_t *r, buf_t *b, char *err) {
    const char *path = jstr(r, "path");
    if (!path) { set_err(err, "path missing", 0); return 0; }
    long long off = jint(r, "offset", 0), mx = jint(r, "max", 4LL * 1024 * 1024);
    if (mx < 0) mx = 0;
    WCHAR *w = expand_path(path);
    HANDLE h = open_read(w); free(w);
    if (h == INVALID_HANDLE_VALUE) { set_err(err, "open failed", GetLastError()); return 0; }
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    FILETIME mt; GetFileTime(h, NULL, NULL, &mt);
    long long size = sz.QuadPart;
    long long start = off < 0 ? size + off : off;
    if (start < 0) start = 0;
    if (start > size) start = size;
    long long want = size - start; if (want > mx) want = mx;
    unsigned char *data = (unsigned char *)malloc((size_t)want + 1);
    LARGE_INTEGER li; li.QuadPart = start; SetFilePointerEx(h, li, NULL, FILE_BEGIN);
    DWORD got = 0; long long tot = 0;
    while (tot < want && ReadFile(h, data + tot, (DWORD)(want - tot > 0x1000000 ? 0x1000000 : want - tot), &got, NULL) && got) tot += got;
    CloseHandle(h);
    kint(b, "size", size); kint(b, "mtime", ft_unix(mt)); kint(b, "offset", start);
    bstr(b, ",\"b64\":\""); b64enc(b, data, (size_t)tot); bstr(b, "\"");
    free(data);
    return 1;
}
static int write_all(HANDLE h, const unsigned char *d, size_t n) {
    size_t done = 0; DWORD wr;
    while (done < n) { if (!WriteFile(h, d + done, (DWORD)(n - done > 0x1000000 ? 0x1000000 : n - done), &wr, NULL)) return 0; done += wr; }
    return 1;
}
static int op_write(jobj_t *r, buf_t *b, char *err) {
    const char *path = jstr(r, "path"), *s = jstr(r, "b64");
    if (!path) { set_err(err, "path missing", 0); return 0; }
    if (!s) s = "";
    size_t n = 0;
    unsigned char *d = b64dec(s, &n);
    if (!d) { set_err(err, "bad base64", 0); return 0; }
    WCHAR *w = expand_path(path);
    if (jbool(r, "mkdirs", 0)) make_parents(w);
    LARGE_INTEGER sz; sz.QuadPart = 0;
    if (jbool(r, "append", 0)) {
        HANDLE h = CreateFileW(w, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) { set_err(err, "open failed", GetLastError()); free(w); free(d); return 0; }
        int ok = write_all(h, d, n);
        DWORD e = GetLastError();
        GetFileSizeEx(h, &sz); CloseHandle(h);
        if (!ok) { set_err(err, "write failed", e); free(w); free(d); return 0; }
    } else {
        size_t l = wcslen(w);
        WCHAR *tmp = (WCHAR *)malloc((l + 16) * sizeof(WCHAR));
        swprintf(tmp, l + 16, L"%ls.winbot.tmp", w);
        HANDLE h = CreateFileW(tmp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) { set_err(err, "open tmp failed", GetLastError()); free(tmp); free(w); free(d); return 0; }
        int ok = write_all(h, d, n);
        DWORD e = GetLastError();
        if (ok) FlushFileBuffers(h);
        CloseHandle(h);
        if (!ok) { set_err(err, "write failed", e); free(tmp); free(w); free(d); return 0; }
        if (!MoveFileExW(tmp, w, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            set_err(err, "MoveFileEx failed", GetLastError()); free(tmp); free(w); free(d); return 0;
        }
        free(tmp);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(w, GetFileExInfoStandard, &fa)) { sz.HighPart = (LONG)fa.nFileSizeHigh; sz.LowPart = fa.nFileSizeLow; }
    }
    free(w); free(d);
    kint(b, "size", sz.QuadPart);
    return 1;
}
static int op_stat(jobj_t *r, buf_t *b, char *err) {
    const char *path = jstr(r, "path");
    if (!path) { set_err(err, "path missing", 0); return 0; }
    WCHAR *w = expand_path(path);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    BOOL ok = GetFileAttributesExW(w, GetFileExInfoStandard, &fa);
    DWORD e = GetLastError();
    free(w);
    if (!ok) {
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_INVALID_NAME) { kbool(b, "exists", 0); return 1; }
        set_err(err, "stat failed", e); return 0;
    }
    kbool(b, "exists", 1);
    kbool(b, "dir", (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    kint(b, "size", (long long)((unsigned long long)fa.nFileSizeHigh << 32 | fa.nFileSizeLow));
    kint(b, "mtime", ft_unix(fa.ftLastWriteTime));
    return 1;
}
static int op_ls(jobj_t *r, buf_t *b, char *err) {
    const char *path = jstr(r, "path");
    if (!path) { set_err(err, "path missing", 0); return 0; }
    WCHAR *w = expand_path(path);
    size_t l = wcslen(w);
    WCHAR *pat = (WCHAR *)malloc((l + 4) * sizeof(WCHAR));
    wcscpy(pat, w);
    if (l && pat[l - 1] != L'\\' && pat[l - 1] != L'/') wcscat(pat, L"\\");
    wcscat(pat, L"*");
    free(w);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    free(pat);
    if (h == INVALID_HANDLE_VALUE) { set_err(err, "ls failed", GetLastError()); return 0; }
    bstr(b, ",\"entries\":[");
    int first = 1;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        char *nm = w16to8(fd.cFileName);
        bstr(b, first ? "{\"name\":" : ",{\"name\":"); bjstr(b, nm); free(nm);
        bfmt(b, ",\"dir\":%s,\"size\":%lld,\"mtime\":%lld}", (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "true" : "false",
             (long long)((unsigned long long)fd.nFileSizeHigh << 32 | fd.nFileSizeLow), ft_unix(fd.ftLastWriteTime));
        first = 0;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    bstr(b, "]");
    return 1;
}
static int op_move(jobj_t *r, buf_t *b, char *err) {
    (void)b;
    const char *src = jstr(r, "src"), *dst = jstr(r, "dst");
    if (!src || !dst) { set_err(err, "src or dst missing", 0); return 0; }
    WCHAR *ws = expand_path(src), *wd = expand_path(dst);
    if (jbool(r, "mkdirs", 0)) make_parents(wd);
    BOOL ok = MoveFileExW(ws, wd, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    DWORD e = GetLastError();
    free(ws); free(wd);
    if (!ok) { set_err(err, "MoveFileEx failed", e); return 0; }
    return 1;
}
static int line_has_any(const char *s, size_t n, jval_t *any) {
    for (int i = 0; i < any->narr; i++) {
        size_t k = strlen(any->arr[i]);
        if (!k) return 1;
        if (k > n) continue;
        for (size_t j = 0; j + k <= n; j++) if (s[j] == any->arr[i][0] && !memcmp(s + j, any->arr[i], k)) return 1;
    }
    return 0;
}
static int op_wait_file(jobj_t *r, buf_t *b, char *err) {
    const char *path = jstr(r, "path");
    jval_t *any = jget(r, "any");
    if (!path) { set_err(err, "path missing", 0); return 0; }
    if (!any || any->type != JT_ARR || !any->narr) { set_err(err, "any missing", 0); return 0; }
    long long off = jint(r, "offset", 0), tmo = jint(r, "timeout_ms", 60000);
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)(tmo > 0 ? tmo : 0);
    WCHAR *w = expand_path(path);
    long long pos = -1;   /* start of the first unread line; -1 = not resolved yet */
    long long size = 0;
    buf_t lb = {0};
    for (;;) {
        HANDLE h = open_read(w);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz; GetFileSizeEx(h, &sz); size = sz.QuadPart;
            if (pos < 0) { pos = off < 0 ? size + off : off; if (pos < 0) pos = 0; }
            if (size < pos) pos = 0;   /* file got shorter: read it from the start */
            if (size > pos) {
                long long want = size - pos;
                if (want > 64LL * 1024 * 1024) want = 64LL * 1024 * 1024;
                lb.n = 0; bput(&lb, "", 0);
                if (lb.cap < (size_t)want + 1) { lb.p = (char *)realloc(lb.p, (size_t)want + 1); lb.cap = (size_t)want + 1; }
                LARGE_INTEGER li; li.QuadPart = pos; SetFilePointerEx(h, li, NULL, FILE_BEGIN);
                DWORD got; long long tot = 0;
                while (tot < want && ReadFile(h, lb.p + tot, (DWORD)(want - tot > 0x1000000 ? 0x1000000 : want - tot), &got, NULL) && got) tot += got;
                lb.n = (size_t)tot; lb.p[lb.n] = 0;
                size_t ls = 0;
                for (size_t i = 0; i < lb.n; i++) {
                    if (lb.p[i] != '\n') continue;
                    size_t le = i;
                    if (le > ls && lb.p[le - 1] == '\r') le--;
                    if (line_has_any(lb.p + ls, le - ls, any)) {
                        CloseHandle(h); free(w);
                        kbool(b, "found", 1);
                        bstr(b, ",\"line\":"); bjstr_n(b, lb.p + ls, le - ls);
                        kint(b, "offset", pos + (long long)i + 1);
                        kint(b, "size", size);
                        free(lb.p);
                        return 1;
                    }
                    ls = i + 1;
                }
                pos += (long long)ls;   /* a partial last line is read again on the next poll */
            }
            CloseHandle(h);
        }
        if (GetTickCount64() >= deadline) break;
        Sleep(100);
    }
    free(w); free(lb.p);
    kbool(b, "found", 0);
    kint(b, "offset", pos < 0 ? 0 : pos);
    kint(b, "size", size);
    return 1;
}

/* --- input */
static int op_click(jobj_t *r, buf_t *b, char *err) {
    (void)b;
    int x = (int)jint(r, "x", 0), y = (int)jint(r, "y", 0);
    const char *btn = jstr(r, "button");
    int right = btn && !strcmp(btn, "right");
    int dbl = jbool(r, "double", 0);
    if (!SetCursorPos(x, y)) { set_err(err, "SetCursorPos failed", GetLastError()); return 0; }
    INPUT in[4]; memset(in, 0, sizeof in);
    int n = 0;
    for (int k = 0; k < (dbl ? 2 : 1); k++) {
        in[n].type = INPUT_MOUSE; in[n].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN; n++;
        in[n].type = INPUT_MOUSE; in[n].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP; n++;
    }
    UINT sent = SendInput((UINT)n, in, sizeof(INPUT));
    if (sent != (UINT)n) { set_err(err, "SendInput failed", GetLastError()); return 0; }
    return 1;
}
static int op_key(jobj_t *r, buf_t *b, char *err) {
    (void)b;
    const char *text = jstr(r, "text");
    UINT sent, want;
    if (text) {
        WCHAR *w = u8to16(text);
        size_t l = wcslen(w);
        if (!l) { free(w); return 1; }
        INPUT *in = (INPUT *)calloc(l * 2, sizeof(INPUT));
        for (size_t i = 0; i < l; i++) {
            in[2 * i].type = INPUT_KEYBOARD; in[2 * i].ki.wScan = w[i]; in[2 * i].ki.dwFlags = KEYEVENTF_UNICODE;
            in[2 * i + 1].type = INPUT_KEYBOARD; in[2 * i + 1].ki.wScan = w[i]; in[2 * i + 1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        }
        want = (UINT)(l * 2);
        sent = SendInput(want, in, sizeof(INPUT));
        free(in); free(w);
    } else {
        if (!jhas(r, "vk")) { set_err(err, "vk or text missing", 0); return 0; }
        WORD vk = (WORD)jint(r, "vk", 0);
        INPUT in[2]; memset(in, 0, sizeof in);
        in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = vk;
        in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vk; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        want = 2;
        sent = SendInput(2, in, sizeof(INPUT));
    }
    if (sent != want) { set_err(err, "SendInput failed", GetLastError()); return 0; }
    return 1;
}

/* --- registry */
static int op_reg_get(jobj_t *r, buf_t *b, char *err) {
    const char *key = jstr(r, "key"), *name = jstr(r, "name");
    if (!key) { set_err(err, "key missing", 0); return 0; }
    WCHAR *wk = u8to16(key), *wn = u8to16(name ? name : "");
    HKEY hk;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, wk, 0, KEY_QUERY_VALUE, &hk);
    free(wk);
    if (rc != ERROR_SUCCESS) {
        free(wn);
        if (rc == ERROR_FILE_NOT_FOUND) { kbool(b, "exists", 0); return 1; }
        set_err(err, "RegOpenKeyEx failed", (DWORD)rc); return 0;
    }
    DWORD type = 0, sz = 0;
    rc = RegQueryValueExW(hk, wn, NULL, &type, NULL, &sz);
    if (rc != ERROR_SUCCESS) {
        RegCloseKey(hk); free(wn);
        if (rc == ERROR_FILE_NOT_FOUND) { kbool(b, "exists", 0); return 1; }
        set_err(err, "RegQueryValueEx failed", (DWORD)rc); return 0;
    }
    BYTE *data = (BYTE *)calloc(1, sz + 4);
    rc = RegQueryValueExW(hk, wn, NULL, &type, data, &sz);
    RegCloseKey(hk); free(wn);
    if (rc != ERROR_SUCCESS) { free(data); set_err(err, "RegQueryValueEx failed", (DWORD)rc); return 0; }
    kbool(b, "exists", 1);
    if (type == REG_DWORD && sz >= 4) { kstr(b, "type", "dword"); kint(b, "value", (long long)*(DWORD *)data); }
    else if (type == REG_SZ || type == REG_EXPAND_SZ) {
        size_t wl = sz / sizeof(WCHAR);
        while (wl && ((WCHAR *)data)[wl - 1] == 0) wl--;
        char *u = w16to8n((WCHAR *)data, (int)wl);
        kstr(b, "type", "sz"); kstr(b, "value", u); free(u);
    } else if (type == REG_QWORD && sz >= 8) { kstr(b, "type", "other"); kint(b, "value", *(long long *)data); }
    else { kstr(b, "type", "other"); bstr(b, ",\"value\":null"); }
    free(data);
    return 1;
}
static int reg_set_value(const WCHAR *key, const char *name, const char *type, jobj_t *r, char *err) {
    HKEY hk;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL);
    if (rc != ERROR_SUCCESS) { set_err(err, "RegCreateKeyEx failed", (DWORD)rc); return 0; }
    WCHAR *wn = u8to16(name ? name : "");
    if (!strcmp(type, "dword")) {
        DWORD v = (DWORD)jint(r, "value", 0);
        rc = RegSetValueExW(hk, wn, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    } else if (!strcmp(type, "sz")) {
        const char *s = jstr(r, "value");
        WCHAR *wv = u8to16(s ? s : "");
        rc = RegSetValueExW(hk, wn, 0, REG_SZ, (const BYTE *)wv, (DWORD)((wcslen(wv) + 1) * sizeof(WCHAR)));
        free(wv);
    } else { RegCloseKey(hk); free(wn); set_err(err, "type must be dword or sz", 0); return 0; }
    RegCloseKey(hk); free(wn);
    if (rc != ERROR_SUCCESS) { set_err(err, "RegSetValueEx failed", (DWORD)rc); return 0; }
    return 1;
}
static int op_reg_set(jobj_t *r, buf_t *b, char *err) {
    (void)b;
    const char *key = jstr(r, "key"), *name = jstr(r, "name"), *type = jstr(r, "type");
    if (!key) { set_err(err, "key missing", 0); return 0; }
    if (!type) type = jget(r, "value") && jget(r, "value")->type == JT_NUM ? "dword" : "sz";
    WCHAR *wk = u8to16(key);
    int ok = reg_set_value(wk, name, type, r, err);
    free(wk);
    return ok;
}
static int op_env_user(jobj_t *r, buf_t *b, char *err) {
    (void)b;
    const char *name = jstr(r, "name");
    if (!name || !name[0]) { set_err(err, "name missing", 0); return 0; }
    if (!reg_set_value(L"Environment", name, "sz", r, err)) return 0;
    DWORD_PTR res;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 1000, &res);
    return 1;
}

static const struct { const char *name; op_fn fn; } OPS[] = {
    {"ping", op_ping}, {"game", op_game}, {"run", op_run}, {"ps", op_ps}, {"kill", op_kill},
    {"read", op_read}, {"write", op_write}, {"stat", op_stat}, {"ls", op_ls}, {"move", op_move},
    {"wait_file", op_wait_file}, {"click", op_click}, {"key", op_key}, {"reg_get", op_reg_get},
    {"reg_set", op_reg_set}, {"env_user", op_env_user},
};

/* ------------------------------------------------------------------ connection */
static int send_all(SOCKET s, const char *p, size_t n) {
    while (n) {
        int k = send(s, p, (int)(n > 0x100000 ? 0x100000 : n), 0);
        if (k <= 0) return 0;
        p += k; n -= (size_t)k;
    }
    return 1;
}
static SOCKET hub_connect(void) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(g_host, g_port, &hints, &res) != 0) return INVALID_SOCKET;
    SOCKET s = INVALID_SOCKET;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        s = WSASocketW(a->ai_family, a->ai_socktype, a->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
        if (s == INVALID_SOCKET) continue;
        SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        closesocket(s); s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s != INVALID_SOCKET) {
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
        setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char *)&one, sizeof one);
    }
    return s;
}
static void do_exit(void) {
    InterlockedExchange(&g_exiting, 1);
    logf_("exit requested");
    EnterCriticalSection(&g_sock_cs);
    for (int i = 0; i < g_nconn; i++) if (g_socks[i] != INVALID_SOCKET) { shutdown(g_socks[i], SD_BOTH); closesocket(g_socks[i]); g_socks[i] = INVALID_SOCKET; }
    LeaveCriticalSection(&g_sock_cs);
    ExitProcess(0);
}
static void serve(SOCKET s, int ci) {
    buf_t in = {0};
    size_t scan = 0;
    char tmp[65536];
    for (;;) {
        char *nl = in.n > scan ? memchr(in.p + scan, '\n', in.n - scan) : NULL;
        if (!nl) {
            scan = in.n;
            int k = recv(s, tmp, sizeof tmp, 0);
            if (k <= 0) break;
            bput(&in, tmp, (size_t)k);
            continue;
        }
        size_t ll = (size_t)(nl - in.p);
        in.p[ll] = 0;
        jobj_t req; long long id = 0;
        char err[256] = {0};
        buf_t body = {0}; bput(&body, "", 0);
        const char *op = "?";
        int ok = 0, parsed = 0;
        ULONGLONG t0 = GetTickCount64();
        if (!jparse(in.p, &req)) { jfree(&req); set_err(err, "bad json", 0); }
        else {
            parsed = 1;
            id = jint(&req, "id", 0);
            const char *o = jstr(&req, "op");
            if (o) op = o;
            if (o && !strcmp(o, "exit")) {
                char line[96]; int n = snprintf(line, sizeof line, "{\"id\":%lld,\"ok\":true}\n", id);
                send_all(s, line, (size_t)n);
                logf_("conn %d op exit", ci);
                do_exit();
            }
            int found = 0;
            for (size_t i = 0; o && i < sizeof OPS / sizeof OPS[0]; i++)
                if (!strcmp(o, OPS[i].name)) { found = 1; ok = OPS[i].fn(&req, &body, err); break; }
            if (!found) set_err(err, o ? "unknown op" : "op missing", 0);
        }
        buf_t resp = {0};
        bfmt(&resp, "{\"id\":%lld,\"ok\":%s", id, ok ? "true" : "false");
        if (ok) bput(&resp, body.p, body.n);
        else kstr(&resp, "err", err);
        bput(&resp, "}\n", 2);
        logf_("conn %d op %s id %lld %s %llums%s%s", ci, op, id, ok ? "ok" : "FAIL", GetTickCount64() - t0, ok ? "" : ": ", ok ? "" : err);
        free(body.p);
        int sent = send_all(s, resp.p, resp.n);
        free(resp.p);
        if (parsed) jfree(&req);   /* op points into req until the log line above */
        /* drop the served line */
        size_t rest = in.n - ll - 1;
        memmove(in.p, in.p + ll + 1, rest); in.n = rest; in.p[in.n] = 0; scan = 0;
        if (!sent) break;
    }
    free(in.p);
}
static DWORD WINAPI conn_thread(LPVOID arg) {
    int ci = (int)(intptr_t)arg;
    int logged_wait = 0;
    while (!g_exiting) {
        SOCKET s = hub_connect();
        if (s == INVALID_SOCKET) {
            if (!logged_wait) { logf_("conn %d: hub %s:%s not reachable, retrying every 500 ms", ci, g_host, g_port); logged_wait = 1; }
            Sleep(500); continue;
        }
        logged_wait = 0;
        EnterCriticalSection(&g_sock_cs); g_socks[ci] = s; LeaveCriticalSection(&g_sock_cs);
        InterlockedIncrement(&g_connected);
        g_last_active = GetTickCount64();
        logf_("conn %d connected", ci);
        buf_t h = {0};
        bstr(&h, "{\"hello\":\"winagent\""); kint(&h, "ver", AGENT_VER); kstr(&h, "token", g_token);
        kint(&h, "pid", (long long)GetCurrentProcessId()); kint(&h, "conn", ci);
        kstr(&h, "computer", g_computer); kstr(&h, "user", g_user); bput(&h, "}\n", 2);
        if (send_all(s, h.p, h.n)) serve(s, ci);
        free(h.p);
        EnterCriticalSection(&g_sock_cs);
        if (g_socks[ci] == s) { closesocket(s); g_socks[ci] = INVALID_SOCKET; }
        LeaveCriticalSection(&g_sock_cs);
        InterlockedDecrement(&g_connected);
        g_last_active = GetTickCount64();
        logf_("conn %d disconnected", ci);
        Sleep(500);
    }
    return 0;
}

/* ------------------------------------------------------------------ main */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int show) {
    (void)hi; (void)hp; (void)cl; (void)show;
    InitializeCriticalSection(&g_log_cs);
    InitializeCriticalSection(&g_inherit_cs);
    InitializeCriticalSection(&g_sock_cs);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    DWORD tl = GetTempPathW(MAX_PATH, g_logpath);
    if (!tl || tl > MAX_PATH) wcscpy(g_logpath, L".\\");
    wcscat(g_logpath, L"winagent.log");

    HANDLE mtx = CreateMutexW(NULL, FALSE, L"Local\\dsm_winagent");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) { logf_("another copy runs: exit 3"); return 3; }

    int argc = 0;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) { logf_("usage: winagent.exe <host:port> <token> [connections] [idle_exit_minutes]"); return 2; }
    char *hp8 = w16to8(argv[1]);
    char *colon = strrchr(hp8, ':');
    if (!colon) { logf_("bad host:port"); return 2; }
    *colon = 0;
    snprintf(g_host, sizeof g_host, "%s", hp8);
    if (g_host[0] == '[') { memmove(g_host, g_host + 1, strlen(g_host)); char *e = strchr(g_host, ']'); if (e) *e = 0; }
    snprintf(g_port, sizeof g_port, "%s", colon + 1);
    free(hp8);
    char *tk = w16to8(argv[2]); snprintf(g_token, sizeof g_token, "%s", tk); free(tk);
    if (argc > 3) g_nconn = _wtoi(argv[3]);
    if (g_nconn < 1) g_nconn = 1;
    if (g_nconn > MAX_CONN) g_nconn = MAX_CONN;
    if (argc > 4) g_idle_min = _wtoi64(argv[4]);
    LocalFree(argv);

    WCHAR wb[256]; DWORD n = 256;
    if (GetComputerNameW(wb, &n)) { char *s = w16to8(wb); snprintf(g_computer, sizeof g_computer, "%s", s); free(s); }
    n = 256;
    if (GetUserNameW(wb, &n)) { char *s = w16to8(wb); snprintf(g_user, sizeof g_user, "%s", s); free(s); }
    SetProcessDPIAware();

    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd)) { logf_("WSAStartup failed"); return 2; }
    for (int i = 0; i < MAX_CONN; i++) g_socks[i] = INVALID_SOCKET;
    logf_("start pid %lu ver %d hub %s:%s connections %d idle_exit %lld min", GetCurrentProcessId(), AGENT_VER, g_host, g_port, g_nconn, g_idle_min);
    g_last_active = GetTickCount64();
    for (int i = 0; i < g_nconn; i++) {
        HANDLE t = CreateThread(NULL, 0, conn_thread, (LPVOID)(intptr_t)i, 0, NULL);
        if (t) CloseHandle(t);
    }
    for (;;) {
        Sleep(1000);
        if (g_connected > 0) g_last_active = GetTickCount64();
        else if (g_idle_min > 0 && GetTickCount64() - g_last_active > (ULONGLONG)g_idle_min * 60000ULL) {
            logf_("no hub for %lld minutes: exit", g_idle_min);
            ExitProcess(0);
        }
    }
}
