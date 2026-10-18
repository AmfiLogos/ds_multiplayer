// Control socket: one Unix socket per instance, one JSON object per line in, one per line out.
// A thread per connection reads lines and queues them; the game's main thread runs them in
// rig_drain() (MainFlow.Update hook) and hands the answer back. "ping" and "quit" are answered here.
#include "rig.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static RigReq *qhead, *qtail;

RigReq *rig_take_requests(void) {
    pthread_mutex_lock(&mu);
    RigReq *r = qhead; qhead = qtail = NULL;
    pthread_mutex_unlock(&mu);
    return r;
}
void rig_finish(RigReq *r, char *resp) {
    pthread_mutex_lock(&mu);
    r->resp = resp; r->done = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

/* ---------------------------------------------------------------- JSON helpers */
static const char *jfind(const char *j, const char *key) {
    char k[80]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(j, k); if (!p) return NULL;
    p += strlen(k); while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return NULL;
    p++; while (*p == ' ' || *p == '\t') p++;
    return p;
}
int jstr(const char *j, const char *key, char *out, size_t n) {
    const char *p = jfind(j, key); if (!p || *p != '"') return 0;
    p++; size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) { p++; out[i++] = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p; p++; continue; }
        out[i++] = *p++;
    }
    out[i] = 0; return 1;
}
int jnum(const char *j, const char *key, double *out) {
    const char *p = jfind(j, key); if (!p) return 0;
    char *e; double v = strtod(p, &e); if (e == p) return 0;
    *out = v; return 1;
}

void bput(Buf *b, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        size_t room = b->cap - b->n;
        va_start(ap, fmt); int k = vsnprintf(b->p ? b->p + b->n : NULL, b->p ? room : 0, fmt, ap); va_end(ap);
        if (k < 0) return;
        if (b->p && (size_t)k < room) { b->n += (size_t)k; return; }
        size_t nc = b->cap ? b->cap * 2 : 4096; while (nc < b->n + (size_t)k + 1) nc *= 2;
        b->p = realloc(b->p, nc); b->cap = nc;
    }
}
void bstr(Buf *b, const char *s) {
    bput(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') bput(b, "\\%c", c);
        else if (c < 0x20) bput(b, "\\u%04x", c);
        else bput(b, "%c", c);
    }
    bput(b, "\"");
}

/* ---------------------------------------------------------------- file paths in test commands
   Commands that write (screenshot, mpm_shot, ui_capture, teams shot) or read (load_save file, plan_file) a file at a
   path given in the command: no "..", and the file only under the rig folder (DSRIG_DIR) or a folder listed in
   DSRIG_WRITE_DIRS (writes) / DSRIG_READ_DIRS (reads; the write folders may be read too). The lists are set only by
   the test launchers: ':'-separated (';' on Windows, where paths have drive letters); an entry ending in '*' is a
   folder name prefix ("<home>/ds_mp_*"). Windows compares case-insensitively, either slash. */
#ifdef _WIN32
#define RIG_LIST_SEP ';'
static int is_sep(char c) { return c == '/' || c == '\\'; }
static int path_abs(const char *in, char *out, size_t n) {
    if (!_fullpath(out, in, n)) return 0;
    for (char *p = out; *p; p++) { if (*p == '\\') *p = '/'; else if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a'); }
    size_t l = strlen(out); while (l > 3 && out[l - 1] == '/') out[--l] = 0;
    return 1;
}
#else
#include <limits.h>
#include <sys/stat.h>
#define RIG_LIST_SEP ':'
static int is_sep(char c) { return c == '/'; }
/* symlinks resolved; a file that does not exist yet (a write): its folder resolved + its name */
static int path_abs(const char *in, char *out, size_t n) {
    char r[PATH_MAX], d[PATH_MAX]; struct stat st;
    if (realpath(in, r)) { snprintf(out, n, "%s", r); return 1; }
    if (!lstat(in, &st)) return 0;                                /* exists but does not resolve: a dangling link */
    snprintf(d, sizeof d, "%s", in);
    char *s = strrchr(d, '/'); const char *base;
    if (!s) { base = in; snprintf(d, sizeof d, "."); }
    else if (s == d) { base = in + 1; snprintf(d, sizeof d, "/"); }
    else { *s = 0; base = in + (s - d) + 1; }
    if (!*base || !realpath(d, r)) return 0;
    snprintf(out, n, "%s/%s", strcmp(r, "/") ? r : "", base);
    return 1;
}
#endif
static int under_dir(const char *abs, const char *entry) {
    char e[1100], r[1100]; snprintf(e, sizeof e, "%s", entry);
    size_t l = strlen(e); if (!l) return 0;
    if (e[l - 1] == '*') {                                        /* "<dir>/<prefix>*" */
        e[--l] = 0;
        char *s = e + l; while (s > e && !is_sep(s[-1])) s--;
        if (s == e) return 0;
        char stem[256]; snprintf(stem, sizeof stem, "%s", s); s[-1] = 0;
        if (!path_abs(e[0] ? e : "/", r, sizeof r)) return 0;
#ifdef _WIN32
        for (char *p = stem; *p; p++) { if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a'); }
#endif
        char pre[1400]; snprintf(pre, sizeof pre, "%s/%s", strcmp(r, "/") ? r : "", stem);
        return !strncmp(abs, pre, strlen(pre));
    }
    if (!path_abs(e, r, sizeof r)) return 0;
    size_t k = strlen(r);
    if (!strcmp(r, "/")) return 1;
    return !strncmp(abs, r, k) && (abs[k] == 0 || abs[k] == '/');
}
static int in_list(const char *abs, const char *list) {
    if (!list) return 0;
    while (*list) {
        const char *e = strchr(list, RIG_LIST_SEP); size_t l = e ? (size_t)(e - list) : strlen(list);
        char one[1100]; if (l && l < sizeof one) { memcpy(one, list, l); one[l] = 0; if (under_dir(abs, one)) return 1; }
        if (!e) break;
        list = e + 1;
    }
    return 0;
}
const char *rig_path_refused(const char *path, int wr) {
    if (!path || !path[0]) return "need path";
    const char *why = NULL; char a[1100];
    if (strstr(path, "..")) why = "path refused (\"..\")";
    else if (!path_abs(path, a, sizeof a)) why = "path refused (its folder does not exist)";
    else if (!(rig_dir[0] && under_dir(a, rig_dir)) && !in_list(a, getenv("DSRIG_WRITE_DIRS")) && !(!wr && in_list(a, getenv("DSRIG_READ_DIRS"))))
        why = "path refused (outside the rig folder and the test folders)";
    if (why) rig_log("%s %s: %s", wr ? "write" : "read", path, why);
    return why;
}
char *rig_path_err(const char *why) {
    Buf b = {0}; bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, why); bput(&b, "}"); return b.p;
}

/* ---------------------------------------------------------------- connections */
static void send_all(int fd, const char *s, size_t n) {
    while (n) { ssize_t k = write(fd, s, n); if (k <= 0) { if (errno == EINTR) continue; return; } s += k; n -= (size_t)k; }
}

static char *run_on_main(const char *line) {
    RigReq *r = calloc(1, sizeof *r); r->line = strdup(line);
    pthread_mutex_lock(&mu);
    if (qtail) qtail->next = r; else qhead = r;
    qtail = r;
    struct timespec until; clock_gettime(CLOCK_REALTIME, &until); until.tv_sec += 60;
    while (!r->done) if (pthread_cond_timedwait(&cv, &mu, &until) == ETIMEDOUT) break;
    int done = r->done;
    pthread_mutex_unlock(&mu);
    if (!done) return strdup("{\"ok\":false,\"error\":\"main thread did not answer in 60 s\"}");   /* r leaks on purpose: main may still use it */
    char *resp = r->resp; free(r->line); free(r);
    return resp ? resp : strdup("{\"ok\":false,\"error\":\"no response\"}");
}

static void *conn_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    char *buf = malloc(1 << 16); size_t n = 0;
    for (;;) {
        ssize_t k = read(fd, buf + n, (1 << 16) - 1 - n);
        if (k <= 0) break;
        n += (size_t)k; buf[n] = 0;
        char *nl;
        while ((nl = strchr(buf, '\n'))) {
            *nl = 0;
            char cmd[64] = "";
            jstr(buf, "cmd", cmd, sizeof cmd);
            char *resp;
            if (!strcmp(cmd, "ping")) {
                Buf b = {0}; bput(&b, "{\"ok\":true,\"pong\":true,\"pid\":%d,\"frames\":%lld}", getpid(), (long long)rig_frames); resp = b.p;
            } else if (!strcmp(cmd, "quit")) {
                const char *bye = "{\"ok\":true,\"quit\":true}\n";
                send_all(fd, bye, strlen(bye));
                rig_log("quit requested on the socket: exiting");
                _exit(0);
            } else resp = run_on_main(buf);
            send_all(fd, resp, strlen(resp)); send_all(fd, "\n", 1);
            free(resp);
            size_t used = (size_t)(nl - buf) + 1;
            memmove(buf, nl + 1, n - used + 1); n -= used;
        }
        if (n >= (1 << 16) - 1) n = 0;   /* line too long: drop it */
    }
    close(fd); free(buf);
    return NULL;
}

static char sock_path[1100];   /* the Windows port cuts the file here (win/port/gen_port.py: TCP listener instead) */
static char link_path[1100];   /* the asked path when it is too long (symlink to sock_path) */
/* 1 = another process answers on this socket path (a running game; a second copy of the same folder, e.g. the
   loader's hidden prewarm copy, must not take it over: unlink + bind would leave the running game without a socket) */
static int sock_live(const char *path) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0); if (s < 0) return 0;
    struct sockaddr_un a = {0}; a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    int live = connect(s, (struct sockaddr*)&a, sizeof a) == 0;
    close(s); return live;
}
static void *accept_thread(void *arg) {
    (void)arg;
    if (sock_live(sock_path)) {                                  /* wait until the owner is gone, then take the path */
        rig_log("control socket %s: another running copy owns it; waiting until it quits", sock_path);
        while (sock_live(sock_path)) sleep(2);
    }
    if (link_path[0]) {
        unlink(link_path);
        if (symlink(sock_path, link_path)) rig_log("socket path %s is too long; link to %s failed (%s)", link_path, sock_path, strerror(errno));
        else rig_log("socket path %s is too long (%zu bytes): listening at %s (linked)", link_path, strlen(link_path), sock_path);
    }
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = {0}; a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", sock_path);
    unlink(sock_path);
    if (bind(s, (struct sockaddr*)&a, sizeof a) || listen(s, 8)) { rig_log("socket %s: bind/listen failed (%s)", sock_path, strerror(errno)); return NULL; }
    rig_log("control socket %s listening", sock_path);
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; rig_log("accept failed (%s)", strerror(errno)); sleep(1); continue; }
        int one = 1; setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);   /* client gone: EPIPE, not SIGPIPE (killed copies) */
        pthread_t t; pthread_create(&t, NULL, conn_thread, (void*)(intptr_t)c); pthread_detach(t);
    }
    return NULL;
}

/* A unix socket path must fit sun_path (104 bytes on macOS); a longer one was cut silently (".../ctl.soc") and
   nobody could connect. Long path: bind at /tmp/dsrig-<uid>-<hash of the path>.sock and put a symlink at the asked
   path, so clients that resolve the link (rig.py, launcher tests) still find it. Short paths are unchanged. */
static void short_sock_path(const char *path, char *out, size_t n) {
    unsigned long long h = 1469598103934665603ULL;           /* FNV-1a 64 */
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) { h ^= *p; h *= 1099511628211ULL; }
    snprintf(out, n, "/tmp/dsrig-%u-%016llx.sock", (unsigned)getuid(), h);
}

/* Test tools only: the socket opens only with DSRIG_CTL=1 (set by the test launchers, never by a player's game).
   DSRIG_NO_CTL=1: no control socket at all (a helper copy that must never answer for the game). */
void rig_sock_start(const char *path) {
    const char *no = getenv("DSRIG_NO_CTL"), *on = getenv("DSRIG_CTL");
    if (no && *no == '1') { rig_log("control socket off (DSRIG_NO_CTL=1)"); return; }
    if (!on || strcmp(on, "1")) { rig_log("control socket off (no DSRIG_CTL)"); return; }
    struct sockaddr_un probe;
    if (strlen(path) >= sizeof probe.sun_path) {
        short_sock_path(path, sock_path, sizeof sock_path);
        snprintf(link_path, sizeof link_path, "%s", path);
    } else {
        snprintf(sock_path, sizeof sock_path, "%s", path);
    }
    pthread_t t; pthread_create(&t, NULL, accept_thread, NULL); pthread_detach(t);
}
