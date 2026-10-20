/* pvp_core - PvP rules state for the lockstep mod (pure C, no game code; plan: research/plan_pvp.md 4.1).
 * One pvp_state per session. It changes only through pvp_apply (a stamped host command, same step on every peer)
 * and pvp_on_tick (once per world tick), so every peer holds the same bytes. It goes into the join-point save
 * (pvp_save / pvp_load) and into the world fingerprint (pvp_hash).
 * Tests: pvp_test/ (make -C pvp_test test). */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define PVP_MAX_PLAYERS 32
#define PVP_MAX_UNITS   32
#define PVP_VERSION     1

enum pvp_team  { PVP_TEAM_NONE = 0, PVP_TEAM_BLUE = 1, PVP_TEAM_RED = 2 };
enum pvp_phase { PVP_OFF = 0, PVP_SETUP = 1, PVP_MATCH = 2 };
enum pvp_mode  { PVP_MODE_FIGHT = 1, PVP_MODE_CTF = 2 };
enum pvp_op    { PVP_OP_SETUP = 1, PVP_OP_START = 2, PVP_OP_END = 3, PVP_OP_ARENA = 4, PVP_OP_READY = 5, PVP_OP_RESUME = 6, PVP_OP_BASE = 7 };
/* arena: where the match is played. 0 = where the units stand (no arena rules), 1 = a dungeon floor (ARENA), 2 = the
   settlement map the units are on (BASE, PvP v1: teams walk to the two ends of the map, arena rules on). */
enum pvp_where { PVP_WHERE_HERE = 0, PVP_WHERE_DUNGEON = 1, PVP_WHERE_BASE = 2 };
enum pvp_pause { PVP_PAUSE_NONE = 0, PVP_PAUSE_PLANNING = 1 };
/* round results / reset kinds */
enum { PVP_RESET_NONE = 0, PVP_RESET_ROUND = 1, PVP_RESET_MATCH = 2 };
#define PVP_TICKS_PER_S 60
/* host rules (SETUP). best_of 1/3/5; round_secs 30..1800; pause rule NONE or PLANNING every plan_every_s s of fight,
   plan_len_s s (the host's clock ends it with RESUME) or until every team player sent READY.
   vanilla: the host rule "Vanilla check" (1 = no gameplay mod for anyone, 0 = the host's gameplay mods for everyone);
   the host checks it before it sends SETUP (pvp_modcheck.h); here it is only carried, saved and hashed. */
typedef struct pvp_rules { uint8_t best_of; uint16_t round_secs; uint8_t pause_rule, plan_every_s, plan_len_s, vanilla; } pvp_rules;
pvp_rules pvp_rules_default(void);   /* best of 3, 180 s, no pause, planning 7 s / 5 s, Vanilla check on */
/* pvp_apply results */
enum { PVP_OK = 0, PVP_ERR_NOT_HOST = -1, PVP_ERR_BAD = -2, PVP_ERR_PHASE = -3, PVP_ERR_EMPTY_TEAM = -4 };

typedef struct pvp_unit { uint8_t guid[16]; uint8_t team, player; uint32_t out; } pvp_unit;   /* out: match tick + 1 when it went out, 0 = in */

typedef struct pvp_state {
    uint8_t phase, mode;
    uint8_t team_of[PVP_MAX_PLAYERS];   /* team per player slot (set by SETUP) */
    uint32_t ticks;                     /* world ticks since START */
    uint32_t matches;                   /* START count (a new match = a new number) */
    uint16_t nunits;
    pvp_unit units[PVP_MAX_UNITS];      /* sorted by guid (memcmp order), filled at START */
    /* arena (M2): ARENA sets it in SETUP; the glue then sends the teams to (region, floor) with the forced seed. While
       arena is on, the floor has no monsters / NPCs / items / respawns, no exit portal and no stock game over. */
    uint8_t arena, placing;             /* placing: START with an arena = teams walk to their ends, no fight yet */
    int32_t region, floor, seed;
    int32_t end_cell[3][2];             /* [team] cell each team goes to at START (set by the glue, same on every peer) */
    uint32_t fight_start;               /* match tick + 1 when the fight began, 0 = not yet */
    /* M3 rounds + M5 pause rule */
    pvp_rules rules;
    uint8_t round;                      /* 1-based round being played */
    uint8_t score[3];                   /* round wins per team */
    uint8_t draws;
    uint8_t resetting;                  /* PVP_RESET_*: the round / match ended; every peer reloads a snapshot (glue) */
    uint8_t last_winner;                /* last round: team, 0 = draw */
    uint8_t match_winner;               /* set when resetting == MATCH: team, 0 = draw */
    uint8_t planning;                   /* planning pause on: 0 world ticks (glue), orders still run */
    uint32_t ready_mask;                /* players that pressed Ready in this planning pause */
    uint32_t plans;                     /* planning pauses so far in the match */
    /* M6 Capture the Flag (mode CTF): one flag per team, our own sim state (no game item). Homes are set by the glue when
       the first fight begins (the team's mean cell); the stock banner stands there are visuals only. */
    int32_t home[3][2];
    struct { uint8_t state, carrier, taker; uint16_t hold; int32_t cell[2]; } flag[3];   /* [team]; carrier/taker = unit index */
    uint32_t captures;                  /* all captures in the match */
    uint32_t countdown_at;              /* M4: match tick + 1 when the 3 s countdown before the fight began, 0 = none */
    uint8_t hold;                       /* own units came in (pvp_units.c): PvP time until the old world is back */
} pvp_state;
enum { PVP_FLAG_HOME = 0, PVP_FLAG_CARRIED = 1, PVP_FLAG_DROPPED = 2 };
#define PVP_FLAG_TAKE_TICKS 60          /* an enemy stays within PVP_FLAG_REACH cells of a flag for 1 s to take it */
#define PVP_FLAG_REACH 2                /* cells (Chebyshev); the banner stand blocks the home cell itself */
#define PVP_NO_UNIT 0xFF
/* CTF events (bits) from pvp_ctf_step */
enum { PVP_EV_TAKEN = 1, PVP_EV_DROPPED = 2, PVP_EV_RETURNED = 4, PVP_EV_CAPTURED = 8 };

/* Command payload (mp order, zero guid; only the host's is applied):
 *   u8 version (1), u8 op
 *   SETUP: u8 mode, u8 team_of[32], u8 best_of, u16 round_secs, u8 pause_rule, u8 plan_every_s, u8 plan_len_s, u8 vanilla (40 after the head)
 *   READY: nothing (any team player, planning pause only); RESUME: nothing (host)
 *   ARENA: i32 region, i32 floor, i32 seed   (12 bytes; SETUP phase only)
 *   BASE:  nothing                           (SETUP phase only): play on the settlement map, teams at its two ends
 *   START: nothing                           the unit table is taken from the owners given to pvp_apply
 *   END:   nothing */
size_t pvp_cmd_setup(uint8_t *out, size_t cap, int mode, const uint8_t team_of[PVP_MAX_PLAYERS], const pvp_rules *r);
size_t pvp_cmd_simple(uint8_t *out, size_t cap, int op);
size_t pvp_cmd_arena(uint8_t *out, size_t cap, int32_t region, int32_t floor, int32_t seed);

/* Owner list for START: every owned unit (guid + owner slot), any order. Units of players with no team are left out.
 * Ties and order never depend on the input order (the table is sorted by guid). */
typedef struct pvp_owned { uint8_t guid[16]; int player; } pvp_owned;

void pvp_init(pvp_state *s);
/* issuer = the command's player slot (0 = host). Returns PVP_OK or an error; the state is unchanged on error. */
int  pvp_apply(pvp_state *s, int issuer, const uint8_t *p, size_t n, const pvp_owned *owned, int nowned);
void pvp_on_tick(pvp_state *s);         /* once per world tick */
/* The glue reports, inside the sim tick, a PvP unit that is downed or dead: the first report keeps the match tick.
 * Returns 1 if this report marked it, 0 if already out / not a PvP unit / no match. */
int  pvp_mark_out(pvp_state *s, const uint8_t guid[16]);
int  pvp_out_tick(const pvp_state *s, const uint8_t guid[16]);   /* match tick it went out, -1 = still in */

int  pvp_active(const pvp_state *s);    /* a match runs: the hostility hooks are on */
int  pvp_arena(const pvp_state *s);     /* the arena rules are on (from ARENA until END) */
int  pvp_fighting(const pvp_state *s);  /* match on and not placing */
void pvp_set_ends(pvp_state *s, const int32_t blue[2], const int32_t red[2]);   /* glue, inside START, every peer */
int  pvp_begin_fight(pvp_state *s);     /* placing -> fight (glue decides when, inside the sim tick); 1 if it began now */
#define PVP_COUNTDOWN_TICKS 180         /* 3 s at 1x between "teams in place" and the fight */
/* placing: the teams are in place (or the place time ran out): start the countdown once; returns 1 when the countdown is
   over and the fight begins now (then pvp_fighting). Glue, inside the sim tick. */
int  pvp_place_done(pvp_state *s);
/* Round rule, once per world tick while fighting (glue, inside the sim tick). hp_permille[team] = HP left of the team's
   units that are not out, in per mille of the team's full HP. A team with no unit left loses the round; at the round time
   the higher HP wins, equal = draw. Returns PVP_RESET_* (ROUND: next round after the reload; MATCH: the match is decided). */
int  pvp_round_check(pvp_state *s, const uint32_t hp_permille[3]);
/* The state every peer gets with the reload (the glue writes it into the snapshot it sends): */
void pvp_next_round(const pvp_state *s, pvp_state *out);   /* same match, round + 1, units back in, placing again */
void pvp_after_match(const pvp_state *s, pvp_state *out);  /* OFF; keeps matches, the last score and the winner */
int  pvp_planning(const pvp_state *s);
/* PvP time (setup, match, or brought units in the world): stock story events are held */
int  pvp_hold_events(const pvp_state *s);
void pvp_ctf_set_homes(pvp_state *s, const int32_t blue[2], const int32_t red[2]);   /* flags home there */
/* once per world tick while fighting in CTF mode (glue, inside the sim tick). cell[i] / known[i] = unit i's cell (units[]
   order). Take, drop (carrier out), return (a teammate touches its dropped flag), capture (carrier within 1 cell of its own
   home while its own flag is home: +1 point, the round ends). Returns PVP_EV_* bits; *flag_team = the flag concerned. */
int  pvp_ctf_step(pvp_state *s, const int32_t (*cell)[2], const uint8_t *known, int *flag_team);
int  pvp_team_of_unit(const pvp_state *s, const uint8_t guid[16]);   /* PVP_TEAM_*, NONE if not a PvP unit */
/* 1 = hostile (PvP units of two teams), 0 = same team, -1 = not decided here (no match, or a unit is not in the
 * table): the caller keeps the game's own answer. */
int  pvp_hostile(const pvp_state *s, const uint8_t a[16], const uint8_t b[16]);

uint64_t pvp_hash(const pvp_state *s);
/* Save block: "PVP1" + fields; size known in advance (pvp_save_size). pvp_load: 0 ok, -1 bad block (state reset). */
size_t pvp_save_size(const pvp_state *s);
size_t pvp_save(const pvp_state *s, uint8_t *out, size_t cap);
int    pvp_load(pvp_state *s, const uint8_t *p, size_t n);
const char *pvp_err_text(int rc);
