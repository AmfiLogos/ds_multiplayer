/* Framed byte-stream link, fd transport, blob fragments, logging. */
#include "dsnet_internal.h"
#include "../ds_i18n.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#define MAX_FRAME (DSNET_MAX_PAYLOAD + 64)

static void (*log_fn)(const char *);
static int log_env = -1;
void dsnet_set_log(void (*fn)(const char *line)) { log_fn = fn; }
void dsnet_logf(const char *fmt, ...) {
    if (!log_fn) {
        if (log_env < 0) { const char *e = getenv("DSNET_LOG"); log_env = e && *e == '1'; }
        if (!log_env) return;
    }
    char line[512];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    if (log_fn) log_fn(line); else fprintf(stderr, "[dsnet] %s\n", line);
}

char     dsnet_build_id[32];
uint32_t dsnet_mod_version;
uint8_t  dsnet_uid[16];
int      dsnet_uid_set;
int      dsnet_match_rule;
char     dsnet_modset[65], dsnet_mod_lines[DSNET_MODLINES_MAX];
char     dsnet_reject_detail[400];        /* the server's words with the last DSNET_REJ_MODSET */
int      dsnet_reject_host_rule, dsnet_reject_has_host;
char     dsnet_reject_host_lines[DSNET_MODLINES_MAX];
void dsnet_set_modset(int rule, const char *modset, const char *lines) {
    dsnet_match_rule = rule == DSNET_MATCH_VANILLA;
    snprintf(dsnet_modset, sizeof dsnet_modset, "%s", modset && *modset ? modset : "");
    snprintf(dsnet_mod_lines, sizeof dsnet_mod_lines, "%s", lines ? lines : "");
}
const char *dsnet_my_mod_lines(void) { return dsnet_mod_lines; }
char dsnet_reject_host_rel[16];
static char my_rel[16]; static int my_rel_set;
static void rel_clean(char *out, size_t n, const char *s) {        /* keep [0-9A-Za-z._-], drop a leading 'v' */
    size_t k = 0; if (n) out[0] = 0;
    if (!s || !n) return;
    if (*s == 'v' || *s == 'V') s++;
    for (; *s && k + 1 < n; s++) {
        char ch = *s;
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '-' || ch == '_') out[k++] = ch;
        else break;
    }
    out[k] = 0;
}
void dsnet_set_release(const char *release) { rel_clean(my_rel, sizeof my_rel, release); my_rel_set = 1; }
const char *dsnet_my_release(void) {
    if (!my_rel_set) { rel_clean(my_rel, sizeof my_rel, getenv("DSML_RELEASE")); my_rel_set = 1; }
    return my_rel;
}
const char *dsnet_reject_host_release(void) { return dsnet_reject_host_rel; }
/* i18n: dsnet_release_text is for display (main thread: the player's language); the server's refusal words on the wire
   stay English (dsnet_release_text_en, server thread) and the receiver shows its own text (dsnet_reject_text) */
static int release_text(const char *fmt, const char *host, const char *mine, char *out, size_t n) {
    if (n) out[0] = 0;
    if (!host || !mine || !*host || !*mine || !strcmp(host, mine)) return 0;
    snprintf(out, n, fmt, host, mine);
    return 1;
}
int dsnet_release_text_en(const char *host, const char *mine, char *out, size_t n) {
    return release_text("Host has v%s, you have v%s: update and try again.", host, mine, out, n);
}
int dsnet_release_text(const char *host, const char *mine, char *out, size_t n) {
    return release_text(TR("Host has v%s, you have v%s: update and try again."), host, mine, out, n);
}
int dsnet_reject_host_mods(int *host_rule, char *lines, size_t n) {
    if (!dsnet_reject_has_host) return -1;
    if (host_rule) *host_rule = dsnet_reject_host_rule;
    if (lines && n) snprintf(lines, n, "%s", dsnet_reject_host_lines);
    return 0;
}
void dsnet_set_player_uid(const uint8_t uid[16]) {
    dsnet_uid_set = uid != NULL;
    if (uid) memcpy(dsnet_uid, uid, 16);
}
void dsnet_set_identity(const char *build_id, uint32_t mod_version) {
    snprintf(dsnet_build_id, sizeof dsnet_build_id, "%s", build_id ? build_id : "");
    dsnet_mod_version = mod_version;
}

static int rx_timeout = -1;
int dsnet_rx_timeout_ms(void) {
    if (rx_timeout < 0) { const char *e = getenv("DSNET_RX_TIMEOUT_MS"); rx_timeout = e && *e ? atoi(e) : 5000; if (rx_timeout < 0) rx_timeout = 0; }
    return rx_timeout;
}
void dsnet_set_rx_timeout_ms(int ms) { rx_timeout = ms < 0 ? 0 : ms; }

static int env_ms(int *v, const char *name, int def) {
    if (*v < 0) { const char *e = getenv(name); *v = e && *e ? atoi(e) : def; if (*v < 0) *v = 0; }
    return *v;
}
static int hello_timeout = -1, tx_stall = -1;
int  dsnet_hello_timeout_ms(void) { return env_ms(&hello_timeout, "DSNET_HELLO_TIMEOUT_MS", 10000); }
void dsnet_set_hello_timeout_ms(int ms) { hello_timeout = ms < 0 ? 0 : ms; }
int  dsnet_tx_stall_ms(void) { return env_ms(&tx_stall, "DSNET_TX_STALL_MS", 10000); }
void dsnet_set_tx_stall_ms(int ms) { tx_stall = ms < 0 ? 0 : ms; }
static int save_wait = -1;
int  dsnet_save_wait_ms(void) { return env_ms(&save_wait, "DSNET_SAVE_WAIT_MS", 90000); }
void dsnet_set_save_wait_ms(int ms) { save_wait = ms < 0 ? 0 : ms; }
static int save_held = -1;
int  dsnet_save_held_ms(void) { return env_ms(&save_held, "DSNET_SAVE_HELD_MS", 600000); }
void dsnet_set_save_held_ms(int ms) { save_held = ms < 0 ? 0 : ms; }

double dsnet_now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ---- fd transport ---- */
static long fd_write(void *t, const void *buf, size_t n) {
    ssize_t r = send((int)(intptr_t)t, buf, n, 0);
    if (r >= 0) return r;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
}
static long fd_read(void *t, void *buf, size_t n) {
    ssize_t r = recv((int)(intptr_t)t, buf, n, 0);
    if (r > 0) return r;
    if (r == 0) return -1;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
}
static int  fd_poll(void *t) { return (int)(intptr_t)t; }
static void fd_close(void *t) { close((int)(intptr_t)t); }
dsnet_transport dsnet_fd_transport(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    return (dsnet_transport){ fd_write, fd_read, fd_poll, fd_close, (void *)(intptr_t)fd };
}

/* ---- link ---- */
void dsnet_link_init(dsnet_link *l, dsnet_transport tr) {
    memset(l, 0, sizeof *l);
    l->tr = tr;
    pthread_mutex_init(&l->mu, NULL);
}
void dsnet_link_free(dsnet_link *l) {
    if (l->tr.close) l->tr.close(l->tr.t);
    free(l->out); free(l->in);
    pthread_mutex_destroy(&l->mu);
}

static int reserve(uint8_t **buf, size_t *off, size_t *len, size_t *cap, size_t need) {
    if (*off && *off == *len) *off = *len = 0;
    if (*len + need <= *cap) return 0;
    if (*off) { memmove(*buf, *buf + *off, *len - *off); *len -= *off; *off = 0; }
    if (*len + need <= *cap) return 0;
    size_t nc = *cap ? *cap : 65536;
    while (nc < *len + need) nc *= 2;
    uint8_t *nb = realloc(*buf, nc);
    if (!nb) return -1;
    *buf = nb; *cap = nc;
    return 0;
}

static int append_frame_locked(dsnet_link *l, const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen) {
    if (l->out_len - l->out_off + 4 + alen + blen > DSNET_OUT_MAX) {   /* the peer stopped reading: cut it, never grow on */
        dsnet_logf("link: send queue over %u bytes (%zu queued), peer not reading, dropped", DSNET_OUT_MAX, l->out_len - l->out_off);
        l->dead = 1;
        return -1;
    }
    if (reserve(&l->out, &l->out_off, &l->out_len, &l->out_cap, 4 + alen + blen)) return -1;
    uint8_t *p = put32(l->out + l->out_len, alen + blen);
    memcpy(p, a, alen);
    if (blen) memcpy(p + alen, b, blen);
    l->out_len += 4 + alen + blen;
    return 0;
}

int dsnet_link_send(dsnet_link *l, const uint8_t *body, uint32_t len) {
    pthread_mutex_lock(&l->mu);
    int r = l->dead ? -1 : append_frame_locked(l, body, len, NULL, 0);
    pthread_mutex_unlock(&l->mu);
    return r;
}

int dsnet_link_send_blob(dsnet_link *l, uint32_t id, uint8_t kind, uint32_t tick, uint32_t seq_after,
                         const uint8_t *data, uint32_t len) {
    if (len > DSNET_MAX_BLOB) return -1;
    pthread_mutex_lock(&l->mu);
    int r = l->dead ? -1 : 0;
    uint32_t off = 0;
    do {   /* an empty blob still sends one fragment */
        uint32_t n = len - off < DSNET_FRAG ? len - off : DSNET_FRAG;
        uint8_t h[1 + BLOB_HDR], *p = h;
        p = put8(p, P_BLOB); p = put32(p, id); p = put8(p, kind); p = put32(p, len);
        p = put32(p, off); p = put32(p, tick); p = put32(p, seq_after);
        if (!r) r = append_frame_locked(l, h, sizeof h, data + off, n);
        off += n;
    } while (off < len && !r);
    pthread_mutex_unlock(&l->mu);
    return r;
}

int dsnet_link_has_out(dsnet_link *l) {
    pthread_mutex_lock(&l->mu);
    int r = l->out_len > l->out_off;
    pthread_mutex_unlock(&l->mu);
    return r;
}

int dsnet_link_flush(dsnet_link *l) {
    pthread_mutex_lock(&l->mu);
    while (!l->dead && l->out_len > l->out_off) {
        long w = l->tr.write(l->tr.t, l->out + l->out_off, l->out_len - l->out_off);
        if (w < 0) l->dead = 1;
        else if (w == 0) break;
        else { l->out_off += w; l->out_stall = 0; }
    }
    if (l->dead || l->out_len == l->out_off) l->out_stall = 0;
    else {                             /* output waits: cut a peer that has not taken a byte for the stall limit */
        double now = dsnet_now_ms(); int lim = dsnet_tx_stall_ms();
        if (!l->out_stall) l->out_stall = now;
        else if (lim && now - l->out_stall > lim) {
            dsnet_logf("link: %zu bytes queued, peer took nothing for %d ms, dropped", l->out_len - l->out_off, (int)(now - l->out_stall));
            l->dead = 1;
        }
    }
    int r = l->dead ? -1 : 0;
    pthread_mutex_unlock(&l->mu);
    return r;
}

/* the next frame's length, checked as soon as its 4 header bytes are in: -1 bad (link dead), 0 header incomplete */
static int head_bad(dsnet_link *l) {
    if (l->in_len - l->in_off < 4) return 0;
    uint32_t n = get32(l->in + l->in_off);
    if (n && n <= MAX_FRAME) return 0;
    dsnet_logf("link: bad frame length %u (max %u), dropped", n, (unsigned)MAX_FRAME);
    l->dead = 1;
    return -1;
}

/* Reads into a fixed DSNET_IN_MAX buffer: the rest waits in the socket (TCP flow control), so one call does bounded
 * work and a peer that never stops sending cannot grow memory. Two max frames fit, so a whole frame always fits. */
int dsnet_link_fill(dsnet_link *l) {
    if (l->dead) return -1;
    if (!l->in) {
        if (!(l->in = malloc(DSNET_IN_MAX))) { l->dead = 1; return -1; }
        l->in_cap = DSNET_IN_MAX;
    }
    if (l->in_off) { memmove(l->in, l->in + l->in_off, l->in_len - l->in_off); l->in_len -= l->in_off; l->in_off = 0; }
    while (l->in_len < l->in_cap) {
        long r = l->tr.read(l->tr.t, l->in + l->in_len, l->in_cap - l->in_len);
        if (r < 0) { l->dead = 1; return -1; }
        if (r == 0) break;
        l->in_len += r;
        if (head_bad(l)) return -1;
    }
    return 0;
}

int dsnet_link_next(dsnet_link *l, const uint8_t **body, uint32_t *len) {
    size_t avail = l->in_len - l->in_off;
    if (avail < 4 || head_bad(l)) return 0;
    uint32_t n = get32(l->in + l->in_off);
    if (avail < 4 + (size_t)n) return 0;
    *body = l->in + l->in_off + 4; *len = n;
    l->in_off += 4 + n;      /* body stays valid until the next fill */
    return 1;
}

int dsnet_blob_feed(dsnet_blob_rx *rx, const uint8_t *p, uint32_t n) {
    if (n < BLOB_HDR) return -1;
    uint32_t id = get32(p), total = get32(p + 5), off = get32(p + 9);
    uint32_t dlen = n - BLOB_HDR;
    if (total > DSNET_MAX_BLOB || dlen > DSNET_FRAG) return -1;
    if (off == 0) {
        free(rx->buf);
        memset(rx, 0, sizeof *rx);
        rx->id = id; rx->kind = p[4]; rx->total = total;
        rx->tick = get32(p + 13); rx->seq_after = get32(p + 17);
        rx->buf = malloc(total ? total : 1);
        if (!rx->buf) return -1;
    }
    if (!rx->buf || id != rx->id || p[4] != rx->kind || total != rx->total || off != rx->got || off + dlen > rx->total) return -1;   /* one blob: kind and size fixed */
    memcpy(rx->buf + off, p + BLOB_HDR, dlen);
    rx->got += dlen;
    return rx->got == rx->total;
}
