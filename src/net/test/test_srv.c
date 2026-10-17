/* Server frame handling without sockets: dsnet_server.c is compiled in here so on_frame can be fed exact bodies
 * (each in its own malloc of its own size, so AddressSanitizer sees any read past the frame). Code review 2026-10-16. */
#include "../dsnet_server.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; ok = 0; } } while (0)

static long w_any(void *t, const void *b, size_t n) { return (long)n; }
static long r_none(void *t, void *b, size_t n) { return 0; }
static int  p_none(void *t) { return -1; }
static void c_none(void *t) { }

static dsnet_server *srv_new(void) {
    dsnet_server *s = calloc(1, sizeof *s);
    s->cfg = dsnet_cfg_defaults(); s->tpt = 16; s->round_mode = fegetround();
    pthread_mutex_init(&s->mu, NULL);
    return s;
}
static sconn *conn_new(dsnet_server *s, int slot, int id, int status) {
    sconn *c = calloc(1, sizeof *c);
    dsnet_link_init(&c->link, (dsnet_transport){ w_any, r_none, p_none, c_none, NULL });
    c->id = id; c->status = status; c->last_rx = c->born = dsnet_now_ms();
    s->conn[slot] = c;
    return c;
}
static void feed(dsnet_server *s, sconn *c, const uint8_t *b, uint32_t n) {   /* the body in a buffer of exactly n bytes */
    uint8_t *m = malloc(n); memcpy(m, b, n);
    on_frame(s, c, m, n);
    free(m);
}

/* #8: a 1-byte HELLO from the host's own (local) link: refused, nothing read past byte 0 */
static void test_short_hello(void) {
    int ok = 1;
    dsnet_server *s = srv_new();
    sconn *c = conn_new(s, 0, -1, ST_NEW); c->local = 1;
    uint8_t h = P_HELLO;
    feed(s, c, &h, 1);
    CHECK(c->id == -1 && c->closing, "short HELLO: id %d closing %d", c->id, c->closing);
    printf("%s short_hello: 1-byte HELLO refused (protocol), no read past it\n", ok ? "PASS" : "FAIL");
}

/* #9: rules from a host whose status is LOADING are dropped whole: the server's settings do not change */
static void test_rules_loading(void) {
    int ok = 1;
    dsnet_server *s = srv_new();
    sconn *h = conn_new(s, 0, 0, ST_LOADING); h->is_host = 1;
    uint8_t b[12] = { P_CMD, DSNET_RULES, 0, 7, 0, DSNET_PAUSE_NONE, 0, DSNET_GEAR_OWN_STORAGE, 2, 0, 0, 1 };
    int pm = s->cfg.pause_mode, gm = s->cfg.gear_mode, sp = s->cfg.allow_speedup;
    feed(s, h, b, sizeof b);
    CHECK(s->cfg.pause_mode == pm && s->cfg.gear_mode == gm && s->cfg.allow_speedup == sp && s->log_n == 0,
          "loading host: pause %d gear %d speed %d, %u issued", s->cfg.pause_mode, s->cfg.gear_mode, s->cfg.allow_speedup, s->log_n);
    h->status = ST_PLAYING;
    feed(s, h, b, sizeof b);
    CHECK(s->cfg.gear_mode == DSNET_GEAR_OWN_STORAGE && s->cfg.pause_mode == DSNET_PAUSE_NONE && s->log_n == 1,
          "playing host: gear %d pause %d, %u issued", s->cfg.gear_mode, s->cfg.pause_mode, s->log_n);
    printf("%s rules_loading: dropped while loading (settings kept), applied + issued once playing\n", ok ? "PASS" : "FAIL");
}

/* #6: a guest's mail over DSNET_MAIL_MAX is refused at its first fragment (no 9 MB buffer) */
static void test_mail_cap(void) {
    int ok = 1;
    dsnet_server *s = srv_new();
    conn_new(s, 0, 0, ST_PLAYING)->is_host = 1;
    sconn *g = conn_new(s, 1, 1, ST_PLAYING);
    uint8_t f[1 + BLOB_HDR + 64] = { 0 }, *p = f;
    p = put8(p, P_BLOB); p = put32(p, 7); p = put8(p, BLOB_MAIL); p = put32(p, DSNET_MAIL_MAX + 1); p = put32(p, 0);
    feed(s, g, f, sizeof f);
    CHECK(g->rx.buf == NULL, "a %u-byte mail was started", DSNET_MAIL_MAX + 1);
    put32(f + 6, 64);                                  /* a small one still goes: total 64, one fragment */
    feed(s, g, f, sizeof f);
    CHECK(g->rx.buf == NULL && s->conn[0]->link.out_len > 0, "a 64-byte mail was not relayed to the host");
    printf("%s mail_cap: guest mail over %u bytes refused at its first fragment; a small one relayed\n", ok ? "PASS" : "FAIL", DSNET_MAIL_MAX);
}

int main(void) {
    test_short_hello();
    test_rules_loading();
    test_mail_cap();
    printf("%d failure(s)\n", failures);
    return failures != 0;
}
