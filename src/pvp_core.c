/* pvp_core - PvP rules state (pure C). See pvp_core.h. */
#include "pvp_core.h"
#include <string.h>
#include <stdlib.h>

#define HEAD 2
#define SETUP_LEN (HEAD + 1 + PVP_MAX_PLAYERS + 7)

pvp_rules pvp_rules_default(void) { pvp_rules r = { 3, 180, PVP_PAUSE_NONE, 7, 5, 1 }; return r; }
size_t pvp_cmd_setup(uint8_t *out, size_t cap, int mode, const uint8_t team_of[PVP_MAX_PLAYERS], const pvp_rules *r) {
    if (cap < SETUP_LEN) return 0;
    pvp_rules d = pvp_rules_default(); if (!r) r = &d;
    out[0] = PVP_VERSION; out[1] = PVP_OP_SETUP; out[2] = (uint8_t)mode;
    memcpy(out + 3, team_of, PVP_MAX_PLAYERS);
    uint8_t *q = out + 3 + PVP_MAX_PLAYERS;
    q[0] = r->best_of; q[1] = (uint8_t)r->round_secs; q[2] = (uint8_t)(r->round_secs >> 8); q[3] = r->pause_rule; q[4] = r->plan_every_s; q[5] = r->plan_len_s; q[6] = r->vanilla;
    return SETUP_LEN;
}
static int rules_ok(const pvp_rules *r) {
    return (r->best_of == 1 || r->best_of == 3 || r->best_of == 5) && r->round_secs >= 30 && r->round_secs <= 1800 &&
           r->pause_rule <= PVP_PAUSE_PLANNING && r->plan_every_s >= 3 && r->plan_every_s <= 60 && r->plan_len_s >= 1 && r->plan_len_s <= 60 && r->vanilla <= 1;
}
size_t pvp_cmd_simple(uint8_t *out, size_t cap, int op) {
    if (cap < HEAD) return 0;
    out[0] = PVP_VERSION; out[1] = (uint8_t)op;
    return HEAD;
}

size_t pvp_cmd_arena(uint8_t *out, size_t cap, int32_t region, int32_t floor, int32_t seed) {
    if (cap < HEAD + 12) return 0;
    out[0] = PVP_VERSION; out[1] = PVP_OP_ARENA;
    memcpy(out + 2, &region, 4); memcpy(out + 6, &floor, 4); memcpy(out + 10, &seed, 4);
    return HEAD + 12;
}

void pvp_init(pvp_state *s) { memset(s, 0, sizeof *s); }

static int cmp_unit(const void *a, const void *b) { return memcmp(((const pvp_unit *)a)->guid, ((const pvp_unit *)b)->guid, 16); }

static void flags_home(pvp_state *s);
static int start(pvp_state *s, const pvp_owned *owned, int nowned) {
    pvp_unit tmp[PVP_MAX_UNITS]; int n = 0, per_team[3] = { 0, 0, 0 };
    memset(tmp, 0, sizeof tmp);
    for (int i = 0; i < nowned; i++) {
        int p = owned[i].player;
        if (p < 0 || p >= PVP_MAX_PLAYERS || s->team_of[p] == PVP_TEAM_NONE) continue;
        int dup = 0;
        for (int j = 0; j < n; j++) if (!memcmp(tmp[j].guid, owned[i].guid, 16)) dup = 1;
        if (dup) continue;
        if (n == PVP_MAX_UNITS) return PVP_ERR_BAD;
        memcpy(tmp[n].guid, owned[i].guid, 16); tmp[n].team = s->team_of[p]; tmp[n].player = (uint8_t)p;
        per_team[tmp[n].team]++; n++;
    }
    if (!per_team[PVP_TEAM_BLUE] || !per_team[PVP_TEAM_RED]) return PVP_ERR_EMPTY_TEAM;
    qsort(tmp, (size_t)n, sizeof tmp[0], cmp_unit);
    memset(s->units, 0, sizeof s->units);
    memcpy(s->units, tmp, sizeof tmp[0] * (size_t)n);
    s->nunits = (uint16_t)n; s->ticks = 0; s->matches++; s->phase = PVP_MATCH;
    s->placing = s->arena != 0; s->fight_start = s->arena ? 0 : 1;
    s->round = 1; memset(s->score, 0, sizeof s->score); s->draws = 0; s->resetting = 0; s->last_winner = 0; s->match_winner = 0;
    s->planning = 0; s->ready_mask = 0; s->plans = 0;
    memset(s->home, 0, sizeof s->home); flags_home(s); s->captures = 0; s->countdown_at = 0;
    memset(s->end_cell, 0, sizeof s->end_cell);
    return PVP_OK;
}

int pvp_apply(pvp_state *s, int issuer, const uint8_t *p, size_t n, const pvp_owned *owned, int nowned) {
    if (!p || n < HEAD || p[0] != PVP_VERSION) return PVP_ERR_BAD;
    if (issuer != 0 && p[1] != PVP_OP_READY) return PVP_ERR_NOT_HOST;
    switch (p[1]) {
    case PVP_OP_SETUP:
        if (n != SETUP_LEN || (p[2] != PVP_MODE_FIGHT && p[2] != PVP_MODE_CTF)) return PVP_ERR_BAD;
        for (int i = 0; i < PVP_MAX_PLAYERS; i++) if (p[3 + i] > PVP_TEAM_RED) return PVP_ERR_BAD;
        { const uint8_t *q = p + 3 + PVP_MAX_PLAYERS;
          pvp_rules r = { q[0], (uint16_t)(q[1] | q[2] << 8), q[3], q[4], q[5], q[6] };
          if (!rules_ok(&r)) return PVP_ERR_BAD;
          if (s->phase == PVP_MATCH) return PVP_ERR_PHASE;
          s->mode = p[2]; memcpy(s->team_of, p + 3, PVP_MAX_PLAYERS); s->rules = r; s->phase = PVP_SETUP; }
        return PVP_OK;
    case PVP_OP_READY:
        if (n != HEAD) return PVP_ERR_BAD;
        if (!pvp_active(s) || !s->planning || issuer < 0 || issuer >= PVP_MAX_PLAYERS || !s->team_of[issuer]) return PVP_ERR_PHASE;
        s->ready_mask |= 1u << issuer;
        { uint32_t need = 0; for (int i = 0; i < PVP_MAX_PLAYERS; i++) if (s->team_of[i]) need |= 1u << i;
          if ((s->ready_mask & need) == need) { s->planning = 0; s->ready_mask = 0; } }
        return PVP_OK;
    case PVP_OP_START:
        if (n != HEAD) return PVP_ERR_BAD;
        if (s->phase != PVP_SETUP) return PVP_ERR_PHASE;
        return start(s, owned, nowned);
    case PVP_OP_ARENA:
        if (n != HEAD + 12) return PVP_ERR_BAD;
        if (s->phase != PVP_SETUP) return PVP_ERR_PHASE;
        { int32_t r, f, sd; memcpy(&r, p + 2, 4); memcpy(&f, p + 6, 4); memcpy(&sd, p + 10, 4);
          if (r < 0 || f < 0 || f > 99) return PVP_ERR_BAD;
          s->arena = 1; s->region = r; s->floor = f; s->seed = sd; }
        return PVP_OK;
    case PVP_OP_BASE:
        if (n != HEAD) return PVP_ERR_BAD;
        if (s->phase != PVP_SETUP) return PVP_ERR_PHASE;
        s->arena = PVP_WHERE_BASE; s->region = s->floor = s->seed = 0;
        return PVP_OK;
    case PVP_OP_RESUME:
        if (n != HEAD) return PVP_ERR_BAD;
        if (!pvp_active(s) || !s->planning) return PVP_ERR_PHASE;
        s->planning = 0; s->ready_mask = 0;
        return PVP_OK;
    case PVP_OP_END:
        if (n != HEAD) return PVP_ERR_BAD;
        if (s->phase == PVP_OFF) return PVP_ERR_PHASE;
        { uint32_t m = s->matches; pvp_init(s); s->matches = m; }
        return PVP_OK;
    }
    return PVP_ERR_BAD;
}

void pvp_on_tick(pvp_state *s) {
    if (s->phase != PVP_MATCH) return;
    s->ticks++;
    if (s->rules.pause_rule == PVP_PAUSE_PLANNING && pvp_fighting(s) && !s->planning) {
        uint32_t ft = s->ticks - (s->fight_start - 1), every = (uint32_t)s->rules.plan_every_s * PVP_TICKS_PER_S;
        if (ft && every && ft % every == 0) { s->planning = 1; s->ready_mask = 0; s->plans++; }
    }
}
int pvp_planning(const pvp_state *s) { return pvp_active(s) && s->planning; }
int pvp_hold_events(const pvp_state *s) { return s->phase != PVP_OFF || s->hold; }

static int end_round(pvp_state *s, int winner) {
    s->last_winner = (uint8_t)winner;
    if (winner) s->score[winner]++; else s->draws++;
    int played = s->score[1] + s->score[2] + s->draws, decided;
    if (s->mode == PVP_MODE_CTF) {               /* best_of = captures to win; at most 2 * best_of - 1 rounds */
        decided = s->score[1] >= s->rules.best_of || s->score[2] >= s->rules.best_of || played >= 2 * s->rules.best_of - 1;
    } else {
        int need = s->rules.best_of / 2 + 1;
        decided = s->score[1] >= need || s->score[2] >= need || played >= s->rules.best_of;
    }
    if (decided) {
        s->resetting = PVP_RESET_MATCH;
        s->match_winner = s->score[1] > s->score[2] ? PVP_TEAM_BLUE : s->score[2] > s->score[1] ? PVP_TEAM_RED : 0;
    } else s->resetting = PVP_RESET_ROUND;
    s->planning = 0; s->ready_mask = 0;
    return s->resetting;
}
int pvp_round_check(pvp_state *s, const uint32_t hp_permille[3]) {
    if (!pvp_fighting(s)) return PVP_RESET_NONE;
    int timeout = s->ticks - (s->fight_start - 1) >= (uint32_t)s->rules.round_secs * PVP_TICKS_PER_S;
    if (s->mode == PVP_MODE_CTF) return timeout ? end_round(s, 0) : PVP_RESET_NONE;   /* only a capture scores */
    int in[3] = { 0, 0, 0 };
    for (int i = 0; i < s->nunits; i++) if (!s->units[i].out) in[s->units[i].team]++;
    int winner = -1;
    if (!in[PVP_TEAM_BLUE] || !in[PVP_TEAM_RED]) winner = in[PVP_TEAM_BLUE] ? PVP_TEAM_BLUE : in[PVP_TEAM_RED] ? PVP_TEAM_RED : 0;
    else if (timeout)
        winner = hp_permille[PVP_TEAM_BLUE] > hp_permille[PVP_TEAM_RED] ? PVP_TEAM_BLUE : hp_permille[PVP_TEAM_RED] > hp_permille[PVP_TEAM_BLUE] ? PVP_TEAM_RED : 0;
    if (winner < 0) return PVP_RESET_NONE;
    return end_round(s, winner);
}
static void flag_home(pvp_state *s, int t) {
    s->flag[t].state = PVP_FLAG_HOME; s->flag[t].carrier = s->flag[t].taker = PVP_NO_UNIT; s->flag[t].hold = 0;
    s->flag[t].cell[0] = s->home[t][0]; s->flag[t].cell[1] = s->home[t][1];
}
static void flags_home(pvp_state *s) { flag_home(s, PVP_TEAM_BLUE); flag_home(s, PVP_TEAM_RED); }
void pvp_ctf_set_homes(pvp_state *s, const int32_t blue[2], const int32_t red[2]) {
    memcpy(s->home[PVP_TEAM_BLUE], blue, 8); memcpy(s->home[PVP_TEAM_RED], red, 8);
    flags_home(s);
}
/* "at the flag": within PVP_FLAG_REACH cells (the banner stand itself takes the home cell) */
static int near1(const int32_t a[2], const int32_t b[2]) { int dx = a[0] - b[0], dy = a[1] - b[1]; return dx >= -PVP_FLAG_REACH && dx <= PVP_FLAG_REACH && dy >= -PVP_FLAG_REACH && dy <= PVP_FLAG_REACH; }
int pvp_ctf_step(pvp_state *s, const int32_t (*cell)[2], const uint8_t *known, int *flag_team) {
    if (s->mode != PVP_MODE_CTF || !pvp_fighting(s)) return 0;
    int ev = 0; if (flag_team) *flag_team = 0;
    for (int t = 1; t <= 2 && !s->resetting; t++) {
        __typeof__(s->flag[0]) *f = &s->flag[t];
        if (f->state == PVP_FLAG_CARRIED) {
            int c = f->carrier;
            if (c >= s->nunits || s->units[c].out) {            /* carrier out: the flag lies where it was */
                f->state = PVP_FLAG_DROPPED; f->carrier = PVP_NO_UNIT; f->taker = PVP_NO_UNIT; f->hold = 0;
                ev |= PVP_EV_DROPPED; if (flag_team) *flag_team = t;
                continue;
            }
            if (known[c]) { f->cell[0] = cell[c][0]; f->cell[1] = cell[c][1]; }
            int own = s->units[c].team;
            if (known[c] && s->flag[own].state == PVP_FLAG_HOME && near1(cell[c], s->home[own])) {
                s->captures++; ev |= PVP_EV_CAPTURED; if (flag_team) *flag_team = t;
                end_round(s, own);
            }
            continue;
        }
        if (f->state == PVP_FLAG_DROPPED) {                     /* a teammate touches it: home at once */
            for (int i = 0; i < s->nunits; i++)
                if (!s->units[i].out && known[i] && s->units[i].team == t && near1(cell[i], f->cell)) {
                    flag_home(s, t);
                    ev |= PVP_EV_RETURNED; if (flag_team) *flag_team = t;
                    break;
                }
            if (f->state == PVP_FLAG_HOME) continue;
        }
        /* HOME or DROPPED: an enemy within 1 cell for PVP_FLAG_TAKE_TICKS takes it (lowest unit index first) */
        int cand = -1;
        if (f->taker != PVP_NO_UNIT && f->taker < s->nunits && !s->units[f->taker].out && known[f->taker] &&
            s->units[f->taker].team != t && near1(cell[f->taker], f->cell)) cand = f->taker;
        for (int i = 0; cand < 0 && i < s->nunits; i++)
            if (!s->units[i].out && known[i] && s->units[i].team != t && near1(cell[i], f->cell)) cand = i;
        if (cand < 0) { f->taker = PVP_NO_UNIT; f->hold = 0; continue; }
        if (f->taker != cand) { f->taker = (uint8_t)cand; f->hold = 0; }
        if (++f->hold >= PVP_FLAG_TAKE_TICKS) {
            f->state = PVP_FLAG_CARRIED; f->carrier = (uint8_t)cand; f->taker = PVP_NO_UNIT; f->hold = 0;
            f->cell[0] = cell[cand][0]; f->cell[1] = cell[cand][1];
            ev |= PVP_EV_TAKEN; if (flag_team) *flag_team = t;
        }
    }
    return ev;
}
void pvp_next_round(const pvp_state *s, pvp_state *o) {
    *o = *s;
    o->round++; o->resetting = 0; o->ticks = 0; o->placing = o->arena != 0; o->fight_start = o->arena ? 0 : 1; o->countdown_at = 0;
    o->planning = 0; o->ready_mask = 0;
    for (int i = 0; i < o->nunits; i++) o->units[i].out = 0;
    flags_home(o);
}
void pvp_after_match(const pvp_state *s, pvp_state *o) {
    pvp_init(o);
    o->matches = s->matches; memcpy(o->score, s->score, sizeof o->score); o->draws = s->draws;
    o->match_winner = s->match_winner; o->last_winner = s->last_winner; o->round = s->round; o->rules = s->rules;
}

int pvp_active(const pvp_state *s) { return s->phase == PVP_MATCH && s->nunits > 0; }
int pvp_arena(const pvp_state *s) { return s->phase != PVP_OFF && s->arena; }
int pvp_fighting(const pvp_state *s) { return pvp_active(s) && !s->placing && !s->resetting; }
void pvp_set_ends(pvp_state *s, const int32_t blue[2], const int32_t red[2]) {
    memcpy(s->end_cell[PVP_TEAM_BLUE], blue, 8); memcpy(s->end_cell[PVP_TEAM_RED], red, 8);
}
int pvp_place_done(pvp_state *s) {
    if (!pvp_active(s) || !s->placing) return 0;
    if (!s->countdown_at) s->countdown_at = s->ticks + 1;
    if (s->ticks - (s->countdown_at - 1) < PVP_COUNTDOWN_TICKS) return 0;
    return pvp_begin_fight(s);
}
int pvp_begin_fight(pvp_state *s) {
    if (!pvp_active(s) || !s->placing) return 0;
    s->placing = 0; s->fight_start = s->ticks + 1;
    return 1;
}

static pvp_unit *find_mut(pvp_state *s, const uint8_t g[16]);
static const pvp_unit *find(const pvp_state *s, const uint8_t g[16]) { return find_mut((pvp_state *)s, g); }
static pvp_unit *find_mut(pvp_state *s, const uint8_t g[16]) {
    int lo = 0, hi = s->nunits - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = memcmp(s->units[mid].guid, g, 16);
        if (!c) return &s->units[mid];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}
int pvp_mark_out(pvp_state *s, const uint8_t guid[16]) {
    if (s->phase != PVP_MATCH) return 0;
    pvp_unit *u = find_mut(s, guid);
    if (!u || u->out) return 0;
    u->out = s->ticks + 1;
    return 1;
}
int pvp_out_tick(const pvp_state *s, const uint8_t guid[16]) {
    const pvp_unit *u = s->phase == PVP_MATCH ? find(s, guid) : NULL;
    return u && u->out ? (int)(u->out - 1) : -1;
}
int pvp_team_of_unit(const pvp_state *s, const uint8_t guid[16]) {
    if (s->phase != PVP_MATCH) return PVP_TEAM_NONE;
    const pvp_unit *u = find(s, guid);
    return u ? u->team : PVP_TEAM_NONE;
}
int pvp_hostile(const pvp_state *s, const uint8_t a[16], const uint8_t b[16]) {
    if (!pvp_active(s)) return -1;
    const pvp_unit *ua = find(s, a), *ub = find(s, b);
    if (!ua || !ub) return -1;
    return ua->team != ub->team;
}

/* ---- save block: "PVP5", u8 phase, u8 mode, team_of[32], u32 ticks, u32 matches, u16 n, n x (guid16, team, player, u32 out) */
#define FIXED (4 + 2 + PVP_MAX_PLAYERS + 4 + 4 + 2 + ARENA_BYTES + ROUND_BYTES + CTF_BYTES)
#define ARENA_BYTES (2 + 12 + 24 + 4)
#define ROUND_BYTES (7 + 1 + 3 + 1 + 1 + 1 + 1 + 1 + 4 + 4)
#define CTF_BYTES (24 + 3 * 13 + 4 + 4 + 1)
#define UNIT_BYTES 22
size_t pvp_save_size(const pvp_state *s) { return FIXED + (size_t)s->nunits * UNIT_BYTES; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
size_t pvp_save(const pvp_state *s, uint8_t *out, size_t cap) {
    size_t need = pvp_save_size(s);
    if (cap < need) return 0;
    uint8_t *p = out;
    memcpy(p, "PVP5", 4); p += 4;
    *p++ = s->phase; *p++ = s->mode;
    memcpy(p, s->team_of, PVP_MAX_PLAYERS); p += PVP_MAX_PLAYERS;
    put32(p, s->ticks); p += 4; put32(p, s->matches); p += 4;
    *p++ = s->arena; *p++ = s->placing;
    put32(p, (uint32_t)s->region); put32(p + 4, (uint32_t)s->floor); put32(p + 8, (uint32_t)s->seed); p += 12;
    for (int t = 0; t < 3; t++) { put32(p, (uint32_t)s->end_cell[t][0]); put32(p + 4, (uint32_t)s->end_cell[t][1]); p += 8; }
    put32(p, s->fight_start); p += 4;
    *p++ = s->rules.best_of; *p++ = (uint8_t)s->rules.round_secs; *p++ = (uint8_t)(s->rules.round_secs >> 8);
    *p++ = s->rules.pause_rule; *p++ = s->rules.plan_every_s; *p++ = s->rules.plan_len_s; *p++ = s->rules.vanilla;
    *p++ = s->round; *p++ = s->score[0]; *p++ = s->score[1]; *p++ = s->score[2]; *p++ = s->draws; *p++ = s->resetting;
    *p++ = s->last_winner; *p++ = s->match_winner; *p++ = s->planning; put32(p, s->ready_mask); put32(p + 4, s->plans); p += 8;
    for (int t = 0; t < 3; t++) { put32(p, (uint32_t)s->home[t][0]); put32(p + 4, (uint32_t)s->home[t][1]); p += 8; }
    for (int t = 0; t < 3; t++) { p[0] = s->flag[t].state; p[1] = s->flag[t].carrier; p[2] = s->flag[t].taker; p[3] = (uint8_t)s->flag[t].hold; p[4] = (uint8_t)(s->flag[t].hold >> 8);
                                  put32(p + 5, (uint32_t)s->flag[t].cell[0]); put32(p + 9, (uint32_t)s->flag[t].cell[1]); p += 13; }
    put32(p, s->captures); put32(p + 4, s->countdown_at); p += 8;
    *p++ = s->hold;
    *p++ = (uint8_t)s->nunits; *p++ = (uint8_t)(s->nunits >> 8);
    for (int i = 0; i < s->nunits; i++) { memcpy(p, s->units[i].guid, 16); p[16] = s->units[i].team; p[17] = s->units[i].player; put32(p + 18, s->units[i].out); p += UNIT_BYTES; }
    return need;
}
int pvp_load(pvp_state *s, const uint8_t *p, size_t n) {
    pvp_state t; pvp_init(&t);
    if (n < FIXED || memcmp(p, "PVP5", 4)) { pvp_init(s); return -1; }
    t.phase = p[4]; t.mode = p[5];
    memcpy(t.team_of, p + 6, PVP_MAX_PLAYERS);
    const uint8_t *q = p + 6 + PVP_MAX_PLAYERS;
    t.ticks = get32(q); t.matches = get32(q + 4); q += 8;
    t.arena = q[0]; t.placing = q[1]; q += 2;
    t.region = (int32_t)get32(q); t.floor = (int32_t)get32(q + 4); t.seed = (int32_t)get32(q + 8); q += 12;
    for (int k = 0; k < 3; k++) { t.end_cell[k][0] = (int32_t)get32(q); t.end_cell[k][1] = (int32_t)get32(q + 4); q += 8; }
    t.fight_start = get32(q); q += 4;
    t.rules.best_of = q[0]; t.rules.round_secs = (uint16_t)(q[1] | q[2] << 8); t.rules.pause_rule = q[3]; t.rules.plan_every_s = q[4]; t.rules.plan_len_s = q[5]; t.rules.vanilla = q[6]; q += 7;
    t.round = q[0]; t.score[0] = q[1]; t.score[1] = q[2]; t.score[2] = q[3]; t.draws = q[4]; t.resetting = q[5];
    t.last_winner = q[6]; t.match_winner = q[7]; t.planning = q[8]; t.ready_mask = get32(q + 9); t.plans = get32(q + 13); q += 17;
    for (int k = 0; k < 3; k++) { t.home[k][0] = (int32_t)get32(q); t.home[k][1] = (int32_t)get32(q + 4); q += 8; }
    for (int k = 0; k < 3; k++) { t.flag[k].state = q[0]; t.flag[k].carrier = q[1]; t.flag[k].taker = q[2]; t.flag[k].hold = (uint16_t)(q[3] | q[4] << 8);
                                  t.flag[k].cell[0] = (int32_t)get32(q + 5); t.flag[k].cell[1] = (int32_t)get32(q + 9); q += 13; }
    t.captures = get32(q); t.countdown_at = get32(q + 4); q += 8;
    t.hold = *q++;
    t.nunits = (uint16_t)(q[0] | q[1] << 8); q += 2;
    if (t.phase > PVP_MATCH || t.arena > PVP_WHERE_BASE || t.placing > 1 || t.nunits > PVP_MAX_UNITS || n != FIXED + (size_t)t.nunits * UNIT_BYTES) { pvp_init(s); return -1; }
    for (int i = 0; i < t.nunits; i++) {
        memcpy(t.units[i].guid, q, 16); t.units[i].team = q[16]; t.units[i].player = q[17]; t.units[i].out = get32(q + 18); q += UNIT_BYTES;
        if (t.units[i].team < PVP_TEAM_BLUE || t.units[i].team > PVP_TEAM_RED) { pvp_init(s); return -1; }
        if (i && memcmp(t.units[i - 1].guid, t.units[i].guid, 16) >= 0) { pvp_init(s); return -1; }
    }
    *s = t;
    return 0;
}

uint64_t pvp_hash(const pvp_state *s) {
    uint8_t b[FIXED + PVP_MAX_UNITS * UNIT_BYTES];
    size_t n = pvp_save(s, b, sizeof b);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

const char *pvp_err_text(int rc) {
    switch (rc) {
    case PVP_OK: return "ok";
    case PVP_ERR_NOT_HOST: return "only the host may change PvP";
    case PVP_ERR_BAD: return "bad PvP command";
    case PVP_ERR_PHASE: return "not allowed now";
    case PVP_ERR_EMPTY_TEAM: return "a team has no unit";
    }
    return "?";
}
