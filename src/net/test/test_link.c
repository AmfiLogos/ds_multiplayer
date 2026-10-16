/* Link-layer limits on a fake transport (no sockets, deterministic): frame headers checked as soon as they are in,
 * fragmented and packed frames, bounded input per read call, output cap and send stall, a max-size blob end to end. */
#include "../dsnet_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FRAME (DSNET_MAX_PAYLOAD + 64)

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; ok = 0; } } while (0)

/* fake byte stream: reads come from buf (then from gen() forever if set), at most `chunk` bytes a call;
 * writes go to sink (if set) or block (return 0) */
typedef struct fake {
    const uint8_t *buf; size_t len, pos;
    size_t (*gen)(struct fake *f, uint8_t *out, size_t n); uint64_t gen_pos;
    size_t chunk;
    size_t total_read, max_read_call;
    uint8_t *sink; size_t sink_len, sink_cap; int blocked; long budget;   /* budget: bytes it takes, -1 = any */
} fake;
static long f_read(void *t, void *out, size_t n) {
    fake *f = t;
    if (f->chunk && n > f->chunk) n = f->chunk;
    size_t r = 0;
    if (f->pos < f->len) { r = f->len - f->pos < n ? f->len - f->pos : n; memcpy(out, f->buf + f->pos, r); f->pos += r; }
    else if (f->gen) r = f->gen(f, out, n);
    f->total_read += r;
    if (r > f->max_read_call) f->max_read_call = r;
    return (long)r;
}
static long f_write(void *t, const void *b, size_t n) {
    fake *f = t;
    if (f->blocked) return 0;
    if (f->budget >= 0) { if ((long)n > f->budget) n = (size_t)f->budget; f->budget -= (long)n; }
    if (!f->sink) { f->sink_len += n; return (long)n; }
    if (f->sink_len + n > f->sink_cap) {
        f->sink_cap = (f->sink_len + n) * 2; f->sink = realloc(f->sink, f->sink_cap);
    }
    memcpy(f->sink + f->sink_len, b, n); f->sink_len += n;
    return (long)n;
}
static int  f_poll(void *t) { return -1; }
static void f_close(void *t) { }
static dsnet_transport fake_tr(fake *f) { return (dsnet_transport){ f_write, f_read, f_poll, f_close, f }; }

static size_t frame(uint8_t *out, uint32_t n, uint8_t seed) {
    put32(out, n);
    for (uint32_t i = 0; i < n; i++) out[4 + i] = (uint8_t)(seed + i * 31);
    return 4 + n;
}
static int frame_ok(const uint8_t *b, uint32_t n, uint32_t want, uint8_t seed) {
    if (n != want) return 0;
    for (uint32_t i = 0; i < n; i++) if (b[i] != (uint8_t)(seed + i * 31)) return 0;
    return 1;
}
/* endless valid 100-byte frames */
static size_t gen_small(fake *f, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++, f->gen_pos++) {
        uint64_t k = f->gen_pos % 104;
        out[i] = k < 4 ? (uint8_t)(100 >> (8 * k)) : (uint8_t)(k - 4);
    }
    return n;
}
static size_t gen_junk(fake *f, uint8_t *out, size_t n) { memset(out, 0x5a, n); return n; }

static void test_header(void) {
    int ok = 1;
    uint8_t b[4 + 40]; size_t n = frame(b, 40, 7);
    fake f = { .buf = b, .len = 3 };                  /* 3 of the 4 header bytes */
    dsnet_link l; dsnet_link_init(&l, fake_tr(&f));
    const uint8_t *body; uint32_t len;
    CHECK(dsnet_link_fill(&l) == 0 && !dsnet_link_next(&l, &body, &len) && !l.dead, "incomplete header: not waiting");
    f.len = 4 + 20;                                   /* header + half the body */
    CHECK(dsnet_link_fill(&l) == 0 && !dsnet_link_next(&l, &body, &len) && !l.dead, "half body: not waiting");
    f.len = n;
    CHECK(dsnet_link_fill(&l) == 0 && dsnet_link_next(&l, &body, &len) == 1 && frame_ok(body, len, 40, 7), "frame not complete");
    CHECK(!dsnet_link_next(&l, &body, &len), "extra frame");
    dsnet_link_free(&l);

    uint32_t bad[3] = { 0, MAX_FRAME + 1, 0xffffffffu };
    int stopped = 1; size_t worst = 0;
    for (int i = 0; i < 3; i++) for (int chunk = 0; chunk < 2; chunk++) {   /* one big read, then 1 byte a read */
        uint8_t h[4]; put32(h, bad[i]);
        fake g = { .buf = h, .len = 4, .gen = gen_junk, .chunk = chunk ? 1 : 0 };
        dsnet_link_init(&l, fake_tr(&g));
        int r = dsnet_link_fill(&l);
        if (r != -1 || !l.dead || dsnet_link_next(&l, &body, &len)) stopped = 0;
        if (chunk && g.total_read != 4) stopped = 0;   /* byte by byte: stops right after the header */
        if (g.total_read > worst) worst = g.total_read;
        dsnet_link_free(&l);
    }
    CHECK(stopped, "a bad length did not kill the link at its header");
    CHECK(worst <= DSNET_IN_MAX, "read %zu bytes past a bad header", worst);
    printf("%s header: incomplete header / half body wait, lengths 0, max+1, 4 GB refused at the header (<= %zu bytes read, 4 when byte by byte)\n",
           ok ? "PASS" : "FAIL", worst);
}

static void test_fragments(void) {
    int ok = 1;
    uint32_t sizes[] = { 1, 1000, MAX_FRAME, 5, MAX_FRAME, 60000, 2 };
    enum { NF = sizeof sizes / sizeof *sizes };
    uint8_t *b = malloc(NF * (4 + MAX_FRAME)); size_t n = 0;
    for (int i = 0; i < NF; i++) n += frame(b + n, sizes[i], (uint8_t)i);
    size_t chunks[] = { 1, 3, 7, 1500, 65536, 0 };   /* 0 = as much as fits */
    int got_all = 1, frames = 0;
    for (size_t c = 0; c < sizeof chunks / sizeof *chunks; c++) {
        fake f = { .buf = b, .len = n, .chunk = chunks[c] };
        dsnet_link l; dsnet_link_init(&l, fake_tr(&f));
        int k = 0;
        for (int guard = 0; guard < 1000000 && k < NF && !l.dead; guard++) {
            dsnet_link_fill(&l);
            const uint8_t *body; uint32_t len;
            while (k < NF && dsnet_link_next(&l, &body, &len)) {
                if (!frame_ok(body, len, sizes[k], (uint8_t)k)) got_all = 0;
                k++; frames++;
            }
        }
        if (k != NF || l.dead) { got_all = 0; printf("  chunk %zu: %d of %d frames, dead %d\n", chunks[c], k, NF, l.dead); }
        dsnet_link_free(&l);
    }
    CHECK(got_all, "fragmented frames wrong");
    /* 50 frames in one read: all come out of one fill */
    size_t m = 0; for (int i = 0; i < 50; i++) m += frame(b + m, 10 + i, (uint8_t)i);
    fake f = { .buf = b, .len = m };
    dsnet_link l; dsnet_link_init(&l, fake_tr(&f));
    dsnet_link_fill(&l);
    int k = 0, good = 1; const uint8_t *body; uint32_t len;
    while (dsnet_link_next(&l, &body, &len)) { if (!frame_ok(body, len, 10 + k, (uint8_t)k)) good = 0; k++; }
    CHECK(k == 50 && good && f.max_read_call == m, "packed: %d frames, good %d", k, good);
    dsnet_link_free(&l);
    free(b);
    printf("%s fragments: 7 frames (1 B .. max) in reads of 1, 3, 7, 1500, 64K and any size, %d frames ok; 50 frames in one read ok\n",
           ok ? "PASS" : "FAIL", frames);
}

static void test_flood_in(void) {
    int ok = 1;
    fake f = { .gen = gen_small };
    dsnet_link l; dsnet_link_init(&l, fake_tr(&f));
    size_t worst_call = 0, worst_cap = 0; long frames = 0; int bad = 0;
    for (int it = 0; it < 2000; it++) {
        size_t before = f.total_read;
        dsnet_link_fill(&l);
        if (f.total_read - before > worst_call) worst_call = f.total_read - before;
        if (l.in_cap > worst_cap) worst_cap = l.in_cap;
        const uint8_t *body; uint32_t len;
        while (dsnet_link_next(&l, &body, &len)) { if (len != 100 || body[0] != 0 || body[99] != 99) bad++; frames++; }
    }
    CHECK(!l.dead && !bad, "flood: dead %d, bad frames %d", l.dead, bad);
    CHECK(worst_call <= DSNET_IN_MAX && worst_cap == DSNET_IN_MAX, "flood: %zu bytes in one fill, buffer %zu", worst_call, worst_cap);
    dsnet_link_free(&l);
    /* one max frame announced, body dribbling in forever: the buffer stays at the cap */
    uint8_t h[4]; put32(h, MAX_FRAME);
    fake g = { .buf = h, .len = 4, .gen = gen_junk, .chunk = 999 };
    dsnet_link_init(&l, fake_tr(&g));
    long got = 0;
    for (int it = 0; it < 500; it++) {
        dsnet_link_fill(&l);
        const uint8_t *body; uint32_t len;
        while (dsnet_link_next(&l, &body, &len)) got++;
        if (l.in_cap > worst_cap) worst_cap = l.in_cap;
    }
    CHECK(worst_cap == DSNET_IN_MAX && got == 1 && l.dead, "dribble: buffer %zu, %ld frames, dead %d (junk header after the max frame)", worst_cap, got, l.dead);
    dsnet_link_free(&l);
    printf("%s flood_in: endless sender -> %ld frames, <= %zu bytes read per fill, buffer fixed at %u; endless junk after a max header -> buffer %zu\n",
           ok ? "PASS" : "FAIL", frames, worst_call, DSNET_IN_MAX, worst_cap);
}

static void test_out_cap(void) {
    int ok = 1;
    fake f = { .blocked = 1, .budget = -1 };
    dsnet_link l; dsnet_link_init(&l, fake_tr(&f));
    static uint8_t body[1000];
    long sent = 0; int r = 0;
    while ((r = dsnet_link_send(&l, body, sizeof body)) == 0) sent++;
    size_t q = l.out_len - l.out_off;
    CHECK(l.dead && q <= DSNET_OUT_MAX && q + 1004 > DSNET_OUT_MAX, "cap: dead %d, %zu queued", l.dead, q);
    CHECK(dsnet_link_send(&l, body, 10) == -1 && dsnet_link_flush(&l) == -1, "dead link still takes data");
    dsnet_link_free(&l);

    dsnet_set_tx_stall_ms(100);
    fake g = { .blocked = 1 };
    dsnet_link_init(&l, fake_tr(&g));
    dsnet_link_send(&l, body, sizeof body);
    int a1 = dsnet_link_flush(&l) == 0;
    usleep(60 * 1000);
    int a2 = dsnet_link_flush(&l) == 0;
    usleep(60 * 1000);
    int d3 = dsnet_link_flush(&l) == -1 && l.dead;
    CHECK(a1 && a2 && d3, "stall: alive at 0 / 60 ms %d/%d, dead at 120 ms %d", a1, a2, d3);
    dsnet_link_free(&l);
    /* a slow reader that takes 1 byte every 60 ms (480 ms in all) is never cut */
    fake h = { .budget = 0 };
    dsnet_link_init(&l, fake_tr(&h));
    for (int i = 0; i < 20; i++) dsnet_link_send(&l, body, sizeof body);
    int slow_ok = 1;
    for (int i = 0; i < 8; i++) {
        if (dsnet_link_flush(&l)) slow_ok = 0;
        usleep(60 * 1000);
        h.budget = 1;
    }
    CHECK(slow_ok && !l.dead && h.sink_len >= 7, "a slow reader was cut (took %zu bytes)", h.sink_len);
    dsnet_link_free(&l);
    dsnet_set_tx_stall_ms(10000);
    printf("%s out_cap: blocked peer -> cut at %zu queued bytes (cap %u) after %ld frames; send stall cut at 100 ms, kept while bytes move\n",
           ok ? "PASS" : "FAIL", q, DSNET_OUT_MAX, sent);
}
/* the biggest save the protocol allows goes through the capped output and the bounded input */
static void test_max_blob(void) {
    int ok = 1;
    uint32_t total = DSNET_MAX_BLOB;
    uint8_t *data = malloc(total);
    for (uint32_t i = 0; i < total; i++) data[i] = (uint8_t)(i * 7 + (i >> 10));
    fake w = { .budget = -1 }; w.sink_cap = 1; w.sink = malloc(1);
    dsnet_link a; dsnet_link_init(&a, fake_tr(&w));
    int rc = dsnet_link_send_blob(&a, 1, BLOB_SAVE, 9, 9, data, total);
    rc |= dsnet_link_flush(&a);
    fake r = { .buf = w.sink, .len = w.sink_len, .chunk = 70000 };
    dsnet_link b; dsnet_link_init(&b, fake_tr(&r));
    dsnet_blob_rx rx = { 0 };
    int done = 0, frames = 0;
    for (int guard = 0; guard < 1000000 && !done && !b.dead; guard++) {
        dsnet_link_fill(&b);
        const uint8_t *body; uint32_t len;
        while (!done && dsnet_link_next(&b, &body, &len)) {
            frames++;
            int f = body[0] == P_BLOB ? dsnet_blob_feed(&rx, body + 1, len - 1) : -1;
            if (f < 0) { b.dead = 1; break; }
            done = f == 1;
        }
    }
    CHECK(rc == 0 && done && rx.total == total && !memcmp(rx.buf, data, total),
          "64 MB blob: rc %d done %d total %u", rc, done, rx.total);
    printf("%s max_blob: %u-byte save (protocol max) queued (%zu bytes framed, cap %u) and rebuilt from %d frames\n",
           ok ? "PASS" : "FAIL", total, w.sink_len, DSNET_OUT_MAX, frames);
    free(rx.buf); free(w.sink); free(data);
    dsnet_link_free(&a); dsnet_link_free(&b);
}

/* code review #6: a blob's continuation must keep the first fragment's kind and size (MAIL and REPORT share one rx) */
static void test_blob_kind(void) {
    int ok = 1;
    uint8_t f[BLOB_HDR + 1024] = { 0 }, *p = f;
    p = put32(p, 5); p = put8(p, BLOB_REPORT); p = put32(p, 2048); p = put32(p, 0);
    dsnet_blob_rx rx = { 0 };
    int r0 = dsnet_blob_feed(&rx, f, sizeof f);
    f[4] = BLOB_MAIL; put32(f + 9, 1024);
    int r1 = dsnet_blob_feed(&rx, f, sizeof f);
    f[4] = BLOB_REPORT; put32(f + 5, 4096);
    int r2 = dsnet_blob_feed(&rx, f, sizeof f);
    put32(f + 5, 2048);
    int r3 = dsnet_blob_feed(&rx, f, sizeof f);
    CHECK(r0 == 0 && r1 == -1 && r2 == -1 && r3 == 1 && rx.kind == BLOB_REPORT, "feeds %d %d %d %d", r0, r1, r2, r3);
    free(rx.buf);
    printf("%s blob_kind: a continuation with another kind or size is refused; the right one completes the blob\n", ok ? "PASS" : "FAIL");
}

int main(void) {
    test_header();
    test_fragments();
    test_flood_in();
    test_out_cap();
    test_max_blob();
    test_blob_kind();
    printf("%d failure(s)\n", failures);
    return failures != 0;
}
