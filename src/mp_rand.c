// mp_rand.c - seeded random for mods (mp_api v2 rand_*, mp_bridge dsmp_bridge_rand_*; docs: src/MP_API.md).
// Shared draw (main thread, inside the lockstep step = rig_det_in_sim): a pure function of (session seed, mod id,
// stream, campaign tick, lockstep step, n) where n counts the draws of that (mod, stream) in this step. Nothing to
// save or send: a late joiner gets the seed with the det state and the counters restart at every step on every peer.
// Every other draw (UI, frame code, other threads, single player) is local: a per-process seed, never shared, and it
// never moves the shared counters (like Guid.NewGuid's OUT counter in rig_det.c).
#include "mp_api.h"
#include "mp_game.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <pthread.h>
#endif

int64_t tick(void);                                    /* rig_cmd.c: campaign tick, -1 = no campaign */

static uint64_t sm64(uint64_t z) {                     /* splitmix64 finalizer */
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static uint64_t mod_hash(const char *id) {             /* FNV-1a 64 of the id (first 64 bytes) */
    uint64_t h = 0xCBF29CE484222325ULL;
    for (int i = 0; id && id[i] && i < 64; i++) { h ^= (uint8_t)id[i]; h *= 0x100000001B3ULL; }
    return h;
}

#define NSTREAM 256                                    /* (mod, stream) pairs with a draw in the current step */
static struct { uint64_t mod; uint32_t stream, n; int used; } cnt[NSTREAM];
static int64_t cnt_tick = -2; static uint32_t cnt_step, cnt_gen, overflow_n;
static uint64_t local_seed, local_n, n_shared, n_local;

int mp_rand_shared(void) { return pthread_main_np() && rig_det_in_sim(); }

static uint32_t next_n(uint64_t mh, uint32_t stream, int64_t t, uint32_t step) {
    uint32_t gen = rig_det_load_gen();
    if (t != cnt_tick || step != cnt_step || gen != cnt_gen) {   /* a new step: every counter restarts */
        memset(cnt, 0, sizeof cnt); overflow_n = 0; cnt_tick = t; cnt_step = step; cnt_gen = gen;
    }
    uint32_t i = (uint32_t)(sm64(mh ^ stream) % NSTREAM);
    for (int k = 0; k < NSTREAM; k++, i = (i + 1) % NSTREAM) {   /* same draw order = same slots on every peer */
        if (!cnt[i].used) { cnt[i].used = 1; cnt[i].mod = mh; cnt[i].stream = stream; }
        if (cnt[i].mod == mh && cnt[i].stream == stream) return cnt[i].n++;
    }
    return 0x80000000u | overflow_n++;                 /* > 256 pairs in one step: one shared counter, still identical */
}

uint32_t mp_rand_u32(const char *mod_id, uint32_t stream) {
    uint64_t mh = mod_hash(mod_id);
    if (mp_rand_shared()) {
        mp_session *s = mp_game_session();
        int64_t t = tick(); uint32_t step = s ? mp_cur_step(s) : 0;
        uint32_t n = next_n(mh, stream, t, step);
        n_shared++;
        uint64_t h = sm64((uint64_t)rig_det_seed() << 32 ^ mh);
        h = sm64(h ^ stream); h = sm64(h ^ (uint64_t)t); h = sm64(h ^ ((uint64_t)step << 32 | n));
        return (uint32_t)(h >> 32);
    }
    if (!local_seed) {                                 /* first local draw: a per-process seed */
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        local_seed = sm64((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec ^ (uint64_t)(uintptr_t)&local_seed) | 1;
    }
    n_local++;
    return (uint32_t)(sm64(sm64(local_seed ^ mh) ^ stream ^ (++local_n << 32)) >> 32);
}
int mp_rand_range(const char *mod_id, uint32_t stream, int lo, int hi) {
    uint32_t u = mp_rand_u32(mod_id, stream);          /* drawn even when hi <= lo: the counters move the same way */
    if (hi <= lo) return lo;
    return (int)((int64_t)lo + (int64_t)(((uint64_t)u * (uint64_t)((int64_t)hi - lo)) >> 32));
}
float mp_rand_value(const char *mod_id, uint32_t stream) {
    return (float)(mp_rand_u32(mod_id, stream) >> 8) * (1.0f / 16777216.0f);
}
void mp_rand_stats(uint64_t *shared, uint64_t *local) { if (shared) *shared = n_shared; if (local) *local = n_local; }
