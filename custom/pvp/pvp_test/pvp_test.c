// pvp_test.c - unit tests for pvp_core.c (PvP rules state). make -C pvp_test test
#include "../pvp_core.h"
#include "../pvp_modcheck.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void g(uint8_t out[16], int n) { memset(out, 0, 16); out[0] = (uint8_t)(n * 37); out[15] = (uint8_t)n; }

static int setup(pvp_state *s, int issuer, const uint8_t teams[32]) {
    uint8_t b[64]; size_t n = pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, NULL);
    return pvp_apply(s, issuer, b, n, NULL, 0);
}
static int simple(pvp_state *s, int issuer, int op, const pvp_owned *o, int no) {
    uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, op);
    return pvp_apply(s, issuer, b, n, o, no);
}

int main(void) {
    pvp_state s; pvp_init(&s);
    uint8_t teams[32] = { 0 }; teams[0] = PVP_TEAM_BLUE; teams[1] = PVP_TEAM_RED; teams[2] = PVP_TEAM_BLUE;
    pvp_owned own[6];
    for (int i = 0; i < 6; i++) { g(own[i].guid, i + 1); own[i].player = i % 4; }   /* players 0,1,2,3,0,1; 3 has no team */

    /* host only, phases */
    CHECK(setup(&s, 1, teams) == PVP_ERR_NOT_HOST && s.phase == PVP_OFF);
    CHECK(simple(&s, 0, PVP_OP_START, own, 6) == PVP_ERR_PHASE);
    CHECK(simple(&s, 0, PVP_OP_END, NULL, 0) == PVP_ERR_PHASE);
    CHECK(setup(&s, 0, teams) == PVP_OK && s.phase == PVP_SETUP && s.mode == PVP_MODE_FIGHT);
    CHECK(!pvp_active(&s));
    CHECK(simple(&s, 2, PVP_OP_START, own, 6) == PVP_ERR_NOT_HOST && s.phase == PVP_SETUP);

    /* bad payloads leave the state unchanged */
    pvp_state before = s;
    uint8_t bad[64]; size_t bn = pvp_cmd_setup(bad, sizeof bad, PVP_MODE_FIGHT, teams, NULL);
    bad[0] = 9; CHECK(pvp_apply(&s, 0, bad, bn, NULL, 0) == PVP_ERR_BAD);
    bad[0] = PVP_VERSION; bad[2] = 7; CHECK(pvp_apply(&s, 0, bad, bn, NULL, 0) == PVP_ERR_BAD);
    bad[2] = PVP_MODE_FIGHT; bad[5] = 3; CHECK(pvp_apply(&s, 0, bad, bn, NULL, 0) == PVP_ERR_BAD);
    bad[5] = 0; CHECK(pvp_apply(&s, 0, bad, bn - 1, NULL, 0) == PVP_ERR_BAD);
    bad[1] = 77; CHECK(pvp_apply(&s, 0, bad, 2, NULL, 0) == PVP_ERR_BAD);
    CHECK(pvp_apply(&s, 0, NULL, 0, NULL, 0) == PVP_ERR_BAD);
    uint8_t st3[3] = { PVP_VERSION, PVP_OP_START, 0 }; CHECK(pvp_apply(&s, 0, st3, 3, own, 6) == PVP_ERR_BAD);
    CHECK(!memcmp(&before, &s, sizeof s));

    /* empty team refused */
    CHECK(simple(&s, 0, PVP_OP_START, own, 1) == PVP_ERR_EMPTY_TEAM && s.phase == PVP_SETUP);

    /* start: table sorted, team from the owner, units of team-less players left out */
    CHECK(simple(&s, 0, PVP_OP_START, own, 6) == PVP_OK && pvp_active(&s));
    CHECK(s.nunits == 5 && s.matches == 1);
    for (int i = 1; i < s.nunits; i++) CHECK(memcmp(s.units[i - 1].guid, s.units[i].guid, 16) < 0);
    CHECK(pvp_team_of_unit(&s, own[0].guid) == PVP_TEAM_BLUE);
    CHECK(pvp_team_of_unit(&s, own[1].guid) == PVP_TEAM_RED);
    CHECK(pvp_team_of_unit(&s, own[2].guid) == PVP_TEAM_BLUE);
    CHECK(pvp_team_of_unit(&s, own[3].guid) == PVP_TEAM_NONE);   /* player 3: no team */
    CHECK(pvp_hostile(&s, own[0].guid, own[1].guid) == 1);
    CHECK(pvp_hostile(&s, own[1].guid, own[0].guid) == 1);
    CHECK(pvp_hostile(&s, own[0].guid, own[2].guid) == 0);        /* teammates (two players) */
    CHECK(pvp_hostile(&s, own[0].guid, own[4].guid) == 0);        /* same player */
    CHECK(pvp_hostile(&s, own[0].guid, own[3].guid) == -1);       /* not a PvP unit: stock answer */
    uint8_t mon[16]; g(mon, 99); CHECK(pvp_hostile(&s, mon, own[1].guid) == -1);
    CHECK(setup(&s, 0, teams) == PVP_ERR_PHASE);                  /* no team change in a match */
    CHECK(simple(&s, 0, PVP_OP_START, own, 6) == PVP_ERR_PHASE);

    /* input order does not matter */
    pvp_state s2; pvp_init(&s2); setup(&s2, 0, teams);
    pvp_owned rev[6]; for (int i = 0; i < 6; i++) rev[i] = own[5 - i];
    simple(&s2, 0, PVP_OP_START, rev, 6);
    CHECK(pvp_hash(&s) == pvp_hash(&s2) && !memcmp(s.units, s2.units, sizeof s.units));
    /* duplicates ignored */
    pvp_state s3; pvp_init(&s3); setup(&s3, 0, teams);
    pvp_owned dup[8]; memcpy(dup, own, sizeof own); dup[6] = own[0]; dup[7] = own[1];
    CHECK(simple(&s3, 0, PVP_OP_START, dup, 8) == PVP_OK && s3.nunits == 5 && pvp_hash(&s3) == pvp_hash(&s));
    /* too many units refused */
    pvp_state s4; pvp_init(&s4); setup(&s4, 0, teams);
    pvp_owned many[40]; for (int i = 0; i < 40; i++) { g(many[i].guid, i + 1); many[i].guid[1] = (uint8_t)i; many[i].player = i % 2; }
    CHECK(simple(&s4, 0, PVP_OP_START, many, 40) == PVP_ERR_BAD && s4.phase == PVP_SETUP);
    CHECK(simple(&s4, 0, PVP_OP_START, many, 32) == PVP_OK && s4.nunits == 32);

    /* ticks + hash */
    uint64_t h0 = pvp_hash(&s);
    pvp_on_tick(&s); CHECK(s.ticks == 1 && pvp_hash(&s) != h0);
    for (int i = 0; i < 999; i++) pvp_on_tick(&s);
    CHECK(s.ticks == 1000);

    /* out marks: first report keeps the match tick; saved and hashed */
    uint64_t h1 = pvp_hash(&s);
    CHECK(pvp_out_tick(&s, own[1].guid) == -1);
    CHECK(pvp_mark_out(&s, own[1].guid) == 1 && pvp_out_tick(&s, own[1].guid) == 1000 && pvp_hash(&s) != h1);
    pvp_on_tick(&s);
    CHECK(pvp_mark_out(&s, own[1].guid) == 0 && pvp_out_tick(&s, own[1].guid) == 1000);
    CHECK(pvp_mark_out(&s, own[3].guid) == 0 && pvp_mark_out(&s, mon) == 0);
    { pvp_state o; pvp_init(&o); CHECK(pvp_mark_out(&o, own[1].guid) == 0); }

    /* save / load round trip */
    uint8_t buf[1024]; size_t n = pvp_save(&s, buf, sizeof buf);
    CHECK(n == pvp_save_size(&s) && n > 0);
    CHECK(pvp_save(&s, buf, n - 1) == 0);
    pvp_state l; memset(&l, 0xAB, sizeof l);
    CHECK(pvp_load(&l, buf, n) == 0 && pvp_hash(&l) == pvp_hash(&s) && !memcmp(&l, &s, sizeof s));
    CHECK(pvp_hostile(&l, own[0].guid, own[1].guid) == 1);
    /* corrupt blocks rejected and reset */
    CHECK(pvp_load(&l, buf, n - 1) == -1 && l.phase == PVP_OFF);
    uint8_t c2[1024]; memcpy(c2, buf, n); c2[0] = 'X'; CHECK(pvp_load(&l, c2, n) == -1);
    memcpy(c2, buf, n); c2[4] = 9; CHECK(pvp_load(&l, c2, n) == -1);
    memcpy(c2, buf, n); c2[n - 6] = 7; CHECK(pvp_load(&l, c2, n) == -1);         /* bad team */
    { pvp_state z; pvp_init(&z); uint8_t zb[256]; size_t zn = pvp_save(&z, zb, sizeof zb); pvp_state zl; CHECK(pvp_load(&zl, zb, zn) == 0 && pvp_hash(&zl) == pvp_hash(&z)); }

    /* end: hostility off, match count kept, setup allowed again */
    CHECK(simple(&s, 1, PVP_OP_END, NULL, 0) == PVP_ERR_NOT_HOST && pvp_active(&s));
    CHECK(simple(&s, 0, PVP_OP_END, NULL, 0) == PVP_OK && !pvp_active(&s) && s.matches == 1);
    CHECK(pvp_hostile(&s, own[0].guid, own[1].guid) == -1);
    CHECK(setup(&s, 0, teams) == PVP_OK && simple(&s, 0, PVP_OP_START, own, 6) == PVP_OK && s.matches == 2);

    /* determinism: two peers fed the same random command stream stay byte-equal */
    for (int seed = 1; seed <= 200; seed++) {
        pvp_state a, b; pvp_init(&a); pvp_init(&b); srand((unsigned)seed);
        for (int k = 0; k < 60; k++) {
            int op = rand() % 4, who = rand() % 3 == 0 ? 1 : 0;
            uint8_t t[32]; for (int i = 0; i < 32; i++) t[i] = (uint8_t)(rand() % 3);
            pvp_owned o[6]; for (int i = 0; i < 6; i++) { o[i] = own[rand() % 6]; o[i].player = rand() % 5; }
            if (op == 0) { setup(&a, who, t); setup(&b, who, t); }
            else if (op == 3) { pvp_on_tick(&a); pvp_on_tick(&b); }
            else { simple(&a, who, op + 1, o, 6); simple(&b, who, op + 1, o, 6); }
        }
        CHECK(pvp_hash(&a) == pvp_hash(&b));
    }

    /* arena: SETUP only, flags on until END, START with arena = placing, begin_fight once; saved */
    {
        pvp_state a; pvp_init(&a); uint8_t b[32];
        size_t an = pvp_cmd_arena(b, sizeof b, 1, 3, 777);
        CHECK(pvp_apply(&a, 0, b, an, NULL, 0) == PVP_ERR_PHASE && !pvp_arena(&a));
        setup(&a, 0, teams);
        CHECK(pvp_apply(&a, 1, b, an, NULL, 0) == PVP_ERR_NOT_HOST && !pvp_arena(&a));
        CHECK(pvp_apply(&a, 0, b, an - 1, NULL, 0) == PVP_ERR_BAD);
        { uint8_t bf[32]; size_t m = pvp_cmd_arena(bf, sizeof bf, 1, 300, 1); CHECK(pvp_apply(&a, 0, bf, m, NULL, 0) == PVP_ERR_BAD); }
        CHECK(pvp_apply(&a, 0, b, an, NULL, 0) == PVP_OK && pvp_arena(&a) && a.region == 1 && a.floor == 3 && a.seed == 777);
        CHECK(!pvp_active(&a) && !pvp_fighting(&a));
        CHECK(simple(&a, 0, PVP_OP_START, own, 6) == PVP_OK && pvp_active(&a) && a.placing && !pvp_fighting(&a));
        CHECK(pvp_apply(&a, 0, b, an, NULL, 0) == PVP_ERR_PHASE);
        CHECK(pvp_hostile(&a, own[0].guid, own[1].guid) == 1);          /* hostile while placing too */
        int32_t bl[2] = { 10, 20 }, rd[2] = { 90, 80 }; pvp_set_ends(&a, bl, rd);
        for (int i = 0; i < 50; i++) pvp_on_tick(&a);
        uint8_t sv[2048]; size_t sn = pvp_save(&a, sv, sizeof sv); pvp_state la;
        CHECK(pvp_load(&la, sv, sn) == 0 && !memcmp(&la, &a, sizeof a));
        { pvp_state cd = a; CHECK(pvp_place_done(&cd) == 0 && cd.countdown_at == 51 && !pvp_fighting(&cd));
          for (int i = 0; i < PVP_COUNTDOWN_TICKS - 1; i++) { pvp_on_tick(&cd); CHECK(pvp_place_done(&cd) == 0); }
          pvp_on_tick(&cd); CHECK(pvp_place_done(&cd) == 1 && pvp_fighting(&cd) && cd.fight_start == 51 + PVP_COUNTDOWN_TICKS); }
        CHECK(pvp_begin_fight(&a) == 1 && pvp_fighting(&a) && a.fight_start == 51 && pvp_begin_fight(&a) == 0);
        CHECK(pvp_hash(&a) != pvp_hash(&la));
        CHECK(simple(&a, 0, PVP_OP_END, NULL, 0) == PVP_OK && !pvp_arena(&a) && !pvp_active(&a));
        /* no arena: START fights at once */
        pvp_state c; pvp_init(&c); setup(&c, 0, teams); simple(&c, 0, PVP_OP_START, own, 6);
        CHECK(pvp_fighting(&c) && c.fight_start == 1 && pvp_begin_fight(&c) == 0);
    }

    /* M3 rounds: elimination, time out by HP, draws, best of 3; next round / after match states */
    {
        pvp_state m; pvp_init(&m); uint8_t b[64];
        pvp_rules r = pvp_rules_default(); r.best_of = 3; r.round_secs = 30;
        pvp_rules bad_r = r; bad_r.best_of = 2;
        CHECK(pvp_apply(&m, 0, b, pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, &bad_r), NULL, 0) == PVP_ERR_BAD);
        bad_r = r; bad_r.round_secs = 5; CHECK(pvp_apply(&m, 0, b, pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, &bad_r), NULL, 0) == PVP_ERR_BAD);
        CHECK(pvp_apply(&m, 0, b, pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, &r), NULL, 0) == PVP_OK && m.rules.round_secs == 30);
        CHECK(simple(&m, 0, PVP_OP_START, own, 6) == PVP_OK && m.round == 1 && pvp_fighting(&m));
        uint32_t hp[3] = { 0, 500, 400 };
        CHECK(pvp_round_check(&m, hp) == PVP_RESET_NONE);
        /* round 1: Red eliminated (units 1 and 5 are Red: players 1) */
        for (int i = 0; i < 6; i++) if (own[i].player == 1) pvp_mark_out(&m, own[i].guid);
        CHECK(pvp_round_check(&m, hp) == PVP_RESET_ROUND && m.score[1] == 1 && m.last_winner == 1 && !pvp_fighting(&m));
        CHECK(pvp_round_check(&m, hp) == PVP_RESET_NONE);          /* once */
        pvp_state m2; pvp_next_round(&m, &m2);
        CHECK(m2.round == 2 && !m2.resetting && pvp_fighting(&m2) && m2.ticks == 0 && pvp_out_tick(&m2, own[1].guid) == -1 && m2.score[1] == 1);
        /* round 2: time out, equal HP = draw */
        for (uint32_t t = 0; t < 30u * 60 - 1; t++) pvp_on_tick(&m2);
        uint32_t eq[3] = { 0, 300, 300 };
        CHECK(pvp_round_check(&m2, eq) == PVP_RESET_NONE);
        pvp_on_tick(&m2);
        CHECK(pvp_round_check(&m2, eq) == PVP_RESET_ROUND && m2.draws == 1 && m2.last_winner == 0);
        pvp_state m3; pvp_next_round(&m2, &m3);
        /* round 3: time out, Red more HP -> 1-1 with 1 draw after 3 rounds = match over, draw */
        for (uint32_t t = 0; t < 30u * 60; t++) pvp_on_tick(&m3);
        uint32_t rd2[3] = { 0, 200, 600 };
        CHECK(pvp_round_check(&m3, rd2) == PVP_RESET_MATCH && m3.match_winner == 0 && m3.score[1] == 1 && m3.score[2] == 1);
        pvp_state after; pvp_after_match(&m3, &after);
        CHECK(after.phase == PVP_OFF && !pvp_arena(&after) && after.matches == m3.matches && after.score[2] == 1 && after.draws == 1);
        uint8_t sv[2048]; size_t sn = pvp_save(&m3, sv, sizeof sv); pvp_state lm; CHECK(pvp_load(&lm, sv, sn) == 0 && !memcmp(&lm, &m3, sizeof lm));
        /* best of 3 decided early: 2 wins */
        pvp_state w; pvp_init(&w); pvp_apply(&w, 0, b, pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, &r), NULL, 0); simple(&w, 0, PVP_OP_START, own, 6);
        uint32_t bl[3] = { 0, 900, 100 };
        for (int k = 0; k < 2; k++) { for (uint32_t t = 0; t < 30u * 60; t++) pvp_on_tick(&w); int e = pvp_round_check(&w, bl);
            if (k == 0) { CHECK(e == PVP_RESET_ROUND); pvp_state nx; pvp_next_round(&w, &nx); w = nx; } else CHECK(e == PVP_RESET_MATCH && w.match_winner == 1); }
    }
    /* M5 planning pause: every N s of fight; READY from every team player or host RESUME ends it */
    {
        pvp_state q; pvp_init(&q); uint8_t b[64];
        pvp_rules r = pvp_rules_default(); r.pause_rule = PVP_PAUSE_PLANNING; r.plan_every_s = 5; r.plan_len_s = 3;
        pvp_apply(&q, 0, b, pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, &r), NULL, 0);   /* teams: players 0, 2 Blue; 1 Red */
        simple(&q, 0, PVP_OP_START, own, 6);
        for (int t = 0; t < 299; t++) pvp_on_tick(&q);
        CHECK(!pvp_planning(&q));
        pvp_on_tick(&q); CHECK(pvp_planning(&q) && q.plans == 1);
        CHECK(simple(&q, 1, PVP_OP_READY, NULL, 0) == PVP_OK && pvp_planning(&q));
        CHECK(simple(&q, 3, PVP_OP_READY, NULL, 0) == PVP_ERR_PHASE);   /* player 3 has no team */
        CHECK(simple(&q, 0, PVP_OP_READY, NULL, 0) == PVP_OK && pvp_planning(&q));
        CHECK(simple(&q, 2, PVP_OP_READY, NULL, 0) == PVP_OK && !pvp_planning(&q));   /* all three ready */
        CHECK(simple(&q, 0, PVP_OP_READY, NULL, 0) == PVP_ERR_PHASE);
        for (int t = 0; t < 300; t++) pvp_on_tick(&q);
        CHECK(pvp_planning(&q) && q.plans == 2);
        CHECK(simple(&q, 1, PVP_OP_RESUME, NULL, 0) == PVP_ERR_NOT_HOST && pvp_planning(&q));
        CHECK(simple(&q, 0, PVP_OP_RESUME, NULL, 0) == PVP_OK && !pvp_planning(&q));
        pvp_state none; pvp_init(&none); setup(&none, 0, teams); simple(&none, 0, PVP_OP_START, own, 6);
        for (int t = 0; t < 2000; t++) pvp_on_tick(&none);
        CHECK(!pvp_planning(&none) && none.plans == 0);
    }

    /* M6 CTF: take after 1 s near, carry, drop when out, return by a teammate, capture only with own flag home */
    {
        pvp_state c; pvp_init(&c); uint8_t b[64];
        pvp_rules r = pvp_rules_default(); r.best_of = 1; r.round_secs = 300;
        pvp_apply(&c, 0, b, pvp_cmd_setup(b, sizeof b, PVP_MODE_CTF, teams, &r), NULL, 0);
        CHECK(simple(&c, 0, PVP_OP_START, own, 6) == PVP_OK && pvp_fighting(&c));
        /* table (guid order) -> find a Blue and a Red unit index */
        int bl = -1, rd = -1, bl2 = -1;
        for (int i = 0; i < c.nunits; i++) { if (c.units[i].team == 1) { if (bl < 0) bl = i; else if (bl2 < 0) bl2 = i; } else if (rd < 0) rd = i; }
        int32_t hb[2] = { 10, 10 }, hr[2] = { 50, 10 }; pvp_ctf_set_homes(&c, hb, hr);
        CHECK(c.flag[1].state == PVP_FLAG_HOME && c.flag[2].carrier == PVP_NO_UNIT);
        int32_t cell[PVP_MAX_UNITS][2]; uint8_t kn[PVP_MAX_UNITS];
        for (int i = 0; i < c.nunits; i++) { cell[i][0] = 30; cell[i][1] = 30; kn[i] = 1; }
        int ft = 0, ev;
        /* elimination does not end a CTF round */
        uint32_t hp[3] = { 0, 900, 900 };
        cell[bl][0] = 52; cell[bl][1] = 12;                          /* Blue unit next to Red's flag */
        for (int t = 0; t < PVP_FLAG_TAKE_TICKS - 1; t++) { pvp_on_tick(&c); ev = pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft); CHECK(!ev); }
        pvp_on_tick(&c); ev = pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft);
        CHECK(ev == PVP_EV_TAKEN && ft == 2 && c.flag[2].state == PVP_FLAG_CARRIED && c.flag[2].carrier == bl);
        cell[bl][0] = 30; pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft);
        CHECK(c.flag[2].cell[0] == 30);                              /* follows the carrier */
        /* carrier goes out: dropped where it was */
        pvp_mark_out(&c, c.units[bl].guid);
        ev = pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft);
        CHECK(ev == PVP_EV_DROPPED && c.flag[2].state == PVP_FLAG_DROPPED && c.flag[2].cell[0] == 30);
        CHECK(pvp_round_check(&c, hp) == PVP_RESET_NONE);
        /* a Red unit touches it: home at once (Blue's flag untouched) */
        cell[rd][0] = 31; cell[rd][1] = 12;
        ev = pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft);
        CHECK(ev == PVP_EV_RETURNED && ft == 2 && c.flag[2].state == PVP_FLAG_HOME && c.flag[2].cell[0] == 50 && c.flag[1].state == PVP_FLAG_HOME);
        /* second Blue unit takes Red's flag; Red takes Blue's flag; Blue carrier at home cannot capture while its flag is away */
        cell[rd][0] = 30; cell[rd][1] = 30;
        cell[bl2][0] = 49; cell[bl2][1] = 9;
        cell[rd][0] = 11; cell[rd][1] = 9;
        for (int t = 0; t < PVP_FLAG_TAKE_TICKS; t++) { pvp_on_tick(&c); pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft); }
        CHECK(c.flag[2].state == PVP_FLAG_CARRIED && c.flag[2].carrier == bl2 && c.flag[1].state == PVP_FLAG_CARRIED && c.flag[1].carrier == rd);
        cell[rd][0] = 30; cell[bl2][0] = 10; cell[bl2][1] = 10;     /* Blue carrier home, Blue flag away */
        ev = pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft);
        CHECK(!(ev & PVP_EV_CAPTURED) && !c.resetting);
        /* Red carrier goes out: Blue flag dropped at (30,9); Blue carrier (at home, next to it? no: far) */
        pvp_mark_out(&c, c.units[rd].guid);
        ev = pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft);
        CHECK((ev & PVP_EV_DROPPED) && c.flag[1].state == PVP_FLAG_DROPPED);
        /* a Blue unit returns it (the carrier walks there and back) */
        cell[bl2][0] = 30; cell[bl2][1] = 9;
        ev = pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft);
        CHECK((ev & PVP_EV_RETURNED) && c.flag[1].state == PVP_FLAG_HOME && c.flag[2].state == PVP_FLAG_CARRIED);
        cell[bl2][0] = 11; cell[bl2][1] = 11;
        uint8_t sv[2048]; size_t sn = pvp_save(&c, sv, sizeof sv); pvp_state lc; CHECK(pvp_load(&lc, sv, sn) == 0 && !memcmp(&lc, &c, sizeof lc));
        ev = pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft);
        CHECK((ev & PVP_EV_CAPTURED) && c.score[1] == 1 && c.captures == 1 && c.resetting == PVP_RESET_MATCH && c.match_winner == 1);
        CHECK(pvp_ctf_step(&c, (const int32_t (*)[2])cell, kn, &ft) == 0);
        /* next round (best of 3 = 3 captures): flags home, units back in */
        pvp_state n2; c.rules.best_of = 3; c.resetting = PVP_RESET_ROUND; pvp_next_round(&c, &n2);
        CHECK(n2.flag[1].state == PVP_FLAG_HOME && n2.flag[2].state == PVP_FLAG_HOME && n2.flag[2].cell[0] == 50 && pvp_out_tick(&n2, n2.units[rd].guid) == -1);
        /* time out in CTF = draw round */
        for (uint32_t t = 0; t < 300u * 60; t++) pvp_on_tick(&n2);
        CHECK(pvp_round_check(&n2, hp) == PVP_RESET_ROUND && n2.draws == 1);
    }

    /* story events held: setup, match, or brought units (hold); after the match: free again */
    { pvp_state h; pvp_init(&h); CHECK(!pvp_hold_events(&h)); setup(&h, 0, teams); CHECK(pvp_hold_events(&h));
      simple(&h, 0, PVP_OP_END, NULL, 0); CHECK(!pvp_hold_events(&h)); h.hold = 1; CHECK(pvp_hold_events(&h));
      uint8_t sv[2048]; size_t sn = pvp_save(&h, sv, sizeof sv); pvp_state l; CHECK(pvp_load(&l, sv, sn) == 0 && l.hold == 1);
      pvp_state o; pvp_after_match(&h, &o); CHECK(!pvp_hold_events(&o)); }

    /* Vanilla check: carried by SETUP, saved; a bad value is refused */
    { pvp_state v; pvp_init(&v); uint8_t b[64]; pvp_rules r = pvp_rules_default(); CHECK(r.vanilla == 1);
      r.vanilla = 0; CHECK(pvp_apply(&v, 0, b, pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, &r), NULL, 0) == PVP_OK && v.rules.vanilla == 0);
      uint8_t sv[2048]; size_t sn = pvp_save(&v, sv, sizeof sv); pvp_state l; CHECK(pvp_load(&l, sv, sn) == 0 && l.rules.vanilla == 0 && pvp_hash(&l) == pvp_hash(&v));
      r.vanilla = 1; pvp_apply(&v, 0, b, pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, &r), NULL, 0); CHECK(v.rules.vanilla == 1 && pvp_hash(&l) != pvp_hash(&v));
      size_t n = pvp_cmd_setup(b, sizeof b, PVP_MODE_FIGHT, teams, &r); b[n - 1] = 2; CHECK(pvp_apply(&v, 0, b, n, NULL, 0) == PVP_ERR_BAD); }

    /* mod sets: session file / report parse, the host's check per rule */
    { const char *sess_none = "{\"world\":\"w\",\"modset\":\"e3b0\",\"modset_lines\":[],\"rule\":\"vanilla\",\"offer\":null,\"client_mods\":[{\"id\":\"notch-fix\",\"version\":\"1.0.0\"}],\"worlds\":{}}";
      const char *sess_hell = "{\"modset\":\"aa11\",\"modset_lines\":[\"hell-difficulty@2.0.0:abc\",\"hell-difficulty-data@2.0.0:def\"],\"rule\":\"mods\",\"client_mods\":[]}";
      const char *sess_hell2 = "{\"modset\":\"bb22\",\"modset_lines\":[\"hell-difficulty@2.0.0:abc\",\"hell-difficulty-data@2.0.0:XXX\"],\"rule\":\"mods\",\"client_mods\":[]}";
      const char *sess_gcd = "{\"modset\":\"cc33\",\"modset_lines\":[\"gcd-cdr@1.0.0:111\"],\"rule\":\"mods\",\"client_mods\":[]}";
      pvp_modset none, hell, hell2, gcd, unk, rt; char why[700];
      CHECK(pvp_modset_parse(sess_none, strlen(sess_none), &none) == 0 && none.known && none.rule == 1 && none.ngame == 0 && none.nclient == 1 && !strcmp(none.client[0], "notch-fix 1.0.0"));
      CHECK(pvp_modset_parse(sess_hell, strlen(sess_hell), &hell) == 0 && hell.ngame == 2 && hell.rule == 0 && !strcmp(hell.hash, "aa11"));
      pvp_modset_parse(sess_hell2, strlen(sess_hell2), &hell2); pvp_modset_parse(sess_gcd, strlen(sess_gcd), &gcd);
      CHECK(pvp_modset_parse(NULL, 0, &unk) == 0 && !unk.known && unk.rule == -1);
      CHECK(pvp_modset_parse("{bad", 4, &rt) == -1 && !rt.known);
      char js[2048]; CHECK(pvp_modset_json(&hell, js, sizeof js) > 0 && pvp_modset_parse(js, strlen(js), &rt) == 0 && rt.known && rt.ngame == 2 && !strcmp(rt.game[1], hell.game[1]) && !strcmp(rt.hash, "aa11"));
      CHECK(pvp_modset_json(&none, js, sizeof js) > 0 && pvp_modset_parse(js, strlen(js), &rt) == 0 && rt.nclient == 1 && !strcmp(rt.client[0], "notch-fix 1.0.0") && rt.rule == 1);
      CHECK(pvp_modset_json(&unk, js, sizeof js) > 0 && pvp_modset_parse(js, strlen(js), &rt) == 0 && !rt.known);
      CHECK(pvp_modset_json(&hell, js, 20) == 0);
      /* On: a guest with a gameplay mod is refused and named; client mods only = allowed; the host's own mods too */
      CHECK(pvp_modset_check(1, &none, &hell, "Ben", 0, why, sizeof why) == 1 && strstr(why, "Ben") && strstr(why, "hell-difficulty 2.0.0"));
      CHECK(pvp_modset_check(1, &none, &none, "Ben", 0, why, sizeof why) == 0);
      CHECK(pvp_modset_check(1, &none, &unk, "Ben", 0, why, sizeof why) == 0);
      CHECK(pvp_modset_check(1, &gcd, &gcd, "The owner", 1, why, sizeof why) == 1 && strstr(why, "your gameplay mod gcd-cdr 1.0.0"));
      /* Off: the host's set is required */
      CHECK(pvp_modset_check(0, &hell, &hell, "Ben", 0, why, sizeof why) == 0);
      CHECK(pvp_modset_check(0, &hell, &hell, "The owner", 1, why, sizeof why) == 0);
      CHECK(pvp_modset_check(0, &hell, &none, "Ben", 0, why, sizeof why) == 1 && strstr(why, "Missing: hell-difficulty 2.0.0, hell-difficulty-data 2.0.0"));
      CHECK(pvp_modset_check(0, &none, &gcd, "Ben", 0, why, sizeof why) == 1 && strstr(why, "Not allowed: gcd-cdr 1.0.0") && !strstr(why, "Missing"));
      CHECK(pvp_modset_check(0, &hell, &gcd, "Ben", 0, why, sizeof why) == 1 && strstr(why, "Missing") && strstr(why, "Not allowed: gcd-cdr"));
      CHECK(pvp_modset_check(0, &hell, &hell2, "Ben", 0, why, sizeof why) == 1 && strstr(why, "hell-difficulty-data 2.0.0") && strstr(why, "other files"));
      CHECK(pvp_modset_check(0, &hell, &unk, "Ben", 0, why, sizeof why) == 1 && strstr(why, "did not start the game from the launcher"));
      CHECK(pvp_modset_check(0, &none, &unk, "Ben", 0, why, sizeof why) == 0);
      CHECK(pvp_modset_check(0, &unk, &unk, "Ben", 0, why, sizeof why) == 0); }

    /* PvP v1 (fepvp13): BASE = the settlement map, teams walk to its two ends (placing), no transfer */
    {
        pvp_state b; pvp_init(&b);
        CHECK(simple(&b, 0, PVP_OP_BASE, NULL, 0) == PVP_ERR_PHASE);                 /* only in SETUP */
        CHECK(setup(&b, 0, teams) == PVP_OK);
        CHECK(simple(&b, 1, PVP_OP_BASE, NULL, 0) == PVP_ERR_NOT_HOST && b.arena == 0);
        uint8_t big[4] = { PVP_VERSION, PVP_OP_BASE, 0, 0 }; CHECK(pvp_apply(&b, 0, big, 3, NULL, 0) == PVP_ERR_BAD);
        CHECK(simple(&b, 0, PVP_OP_BASE, NULL, 0) == PVP_OK && b.arena == PVP_WHERE_BASE && pvp_arena(&b));
        CHECK(simple(&b, 0, PVP_OP_START, own, 6) == PVP_OK && pvp_active(&b) && b.placing && !b.fight_start && !pvp_fighting(&b));
        int32_t e1[2] = { 120, 146 }, e2[2] = { 170, 146 }; pvp_set_ends(&b, e1, e2);
        uint8_t blk[2048]; size_t bl = pvp_save(&b, blk, sizeof blk); pvp_state b2;
        CHECK(bl && pvp_load(&b2, blk, bl) == 0 && b2.arena == PVP_WHERE_BASE && pvp_hash(&b2) == pvp_hash(&b) && b2.end_cell[2][0] == 170);
        blk[6 + 32 + 8] = 3; CHECK(pvp_load(&b2, blk, bl) == -1);                     /* arena byte 3: refused */
        for (int t = 0; t < PVP_COUNTDOWN_TICKS + 2 && !pvp_fighting(&b); t++) { pvp_on_tick(&b); pvp_place_done(&b); }
        CHECK(pvp_fighting(&b) && b.fight_start);
        for (int i = 0; i < b.nunits; i++) if (b.units[i].team == PVP_TEAM_BLUE) pvp_mark_out(&b, b.units[i].guid);
        uint32_t hp[3] = { 0, 0, 500 };                                                /* Blue has nobody left: round to Red */
        CHECK(pvp_round_check(&b, hp) == PVP_RESET_ROUND && b.score[PVP_TEAM_RED] == 1);
        pvp_state nx; pvp_next_round(&b, &nx);
        CHECK(nx.round == 2 && nx.arena == PVP_WHERE_BASE && nx.placing && !nx.fight_start);   /* round 2 places again */
        pvp_state am; b.resetting = PVP_RESET_MATCH; pvp_after_match(&b, &am);
        CHECK(am.phase == PVP_OFF && am.arena == 0 && !pvp_arena(&am));                /* match over: arena rules off */
    }
    printf(fails ? "pvp_test: %d of %d FAILED\n" : "pvp_test: all %2$d checks passed\n", fails, checks);
    return fails != 0;
}
