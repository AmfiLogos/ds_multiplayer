// mp_api v2 unit test: the seeded random for mods (mp_rand.c) and the mod chat line limits (mp_modchat.c), no game.
// Each check stands for a real break: peers that draw different values desync; a mod chat flood fills every screen.
#include "../mp_api.h"
#include "../mp_game.h"
#include <stdio.h>
#include <string.h>

/* stubs: one "peer" at a time (the test swaps the state) */
static int in_sim = 1; static uint32_t seed = 777, gen = 1; static int64_t t_now = 100; static uint32_t step_now = 50;
int rig_det_in_sim(void) { return in_sim; }
uint32_t rig_det_seed(void) { return seed; }
uint32_t rig_det_load_gen(void) { return gen; }
int64_t tick(void) { return t_now; }
static mp_session *fake = (mp_session *)1;
mp_session *mp_game_session(void) { return fake; }
uint32_t mp_cur_step(mp_session *s) { (void)s; return step_now; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* one step of a peer: 3 mods draw in a fixed order; `ui` = extra local (non-sim) draws this peer makes in between */
static void peer_step(uint32_t *out, int ui) {
    int k = 0;
    for (int i = 0; i < 4; i++) {
        out[k++] = mp_rand_u32("delvers", 1);
        if (ui) { in_sim = 0; for (int j = 0; j < ui; j++) (void)mp_rand_u32("delvers", 1); in_sim = 1; }
        out[k++] = mp_rand_u32("delvers", 2);
        out[k++] = (uint32_t)mp_rand_range("other", 1, -5, 5);
    }
}

int main(void) {
    uint32_t a[12], b[12];
    /* 1. two peers, same seed / tick / step: same values, even when one peer also draws in its UI (local draws) */
    t_now = 100; step_now = 50; gen = 1; peer_step(a, 0);
    gen = 2; peer_step(b, 3);                       /* "the other peer": counters restart (new load generation) */
    CHECK(!memcmp(a, b, sizeof a), "a peer's UI draws changed the shared values");
    int same = 0; for (int i = 1; i < 12; i++) same += a[i] == a[0];
    CHECK(same < 2, "shared draws repeat (%d equal to the first)", same);

    /* 2. late joiner: peer A drew 7 times at step 50; peer B starts fresh at step 51. Step 51 must match. */
    gen = 3; t_now = 100; step_now = 50; for (int i = 0; i < 7; i++) (void)mp_rand_u32("delvers", 1);
    t_now = 101; step_now = 51; peer_step(a, 0);
    gen = 4; peer_step(b, 0);
    CHECK(!memcmp(a, b, sizeof a), "draws of an earlier step changed the next step's values (late joiner desyncs)");

    /* 3. other seed (another session) = other values; local draws differ from shared ones */
    seed = 778; gen = 5; peer_step(b, 0); seed = 777;
    CHECK(memcmp(a, b, sizeof a), "the session seed does not change the values");

    /* 4. range / value bounds */
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        int r = mp_rand_range("x", 9, -3, 4); float v = mp_rand_value("x", 9);
        bad += r < -3 || r > 3 || !(v >= 0.0f && v < 1.0f);
    }
    CHECK(!bad, "%d draws out of range", bad);
    CHECK(mp_rand_range("x", 9, 5, 5) == 5 && mp_rand_range("x", 9, 5, 2) == 5, "hi <= lo must return lo");

    /* 5. chat flood: 100 lines in 1 s -> at most burst + 1 pass; text cannot fake the mod-line marker */
    mpc_bucket bk = {0}; int pass = 0;
    for (int i = 0; i < 100; i++) pass += mpc_take(&bk, 1000.0 + i * 0.01, 3, 2.0);
    CHECK(pass <= 4 && pass >= 3, "broadcast limit let %d of 100 lines through in 1 s", pass);
    pass = 0; for (int i = 0; i < 100; i++) pass += mpc_take(&bk, 1100.0 + i * 2.0, 3, 2.0);
    CHECK(pass >= 99, "steady 1 line / 2 s was cut (%d of 100)", pass);
    char w[256], mod[32]; const char *txt;
    CHECK(mpc_encode("Delvers", "hi\x1F" "Fake\x1Fx\nnext", w, sizeof w) > 0 && mpc_decode(w, mod, sizeof mod, &txt)
          && !strcmp(mod, "Delvers") && !strcmp(txt, "hi Fake x next"), "marker / newline inside the text not cleaned: %s", txt);
    char c[8]; mpc_clean("ab\xC3\xA9\xC3\xA9", c, 5);   /* "abéé" cut at 4 bytes: "abé", never half a character */
    CHECK(!strcmp(c, "ab\xC3\xA9"), "UTF-8 cut inside a character (%zu bytes)", strlen(c));
    CHECK(mpc_encode("", "x", w, sizeof w) < 0 && mpc_encode("m", "", w, sizeof w) < 0, "empty mod name or text accepted");

    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
