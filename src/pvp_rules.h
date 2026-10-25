/* pvp_rules - the PvP rules by id, the last used rules, named presets, and the host's rules for the match (lane pvprules,
 * owner 2026-10-20).
 *
 * Every PvP rule has a short id (table in pvp_rules_core.c). The host page's "PvP Rules" box (pvp_ui.c) shows them;
 * the values the player sets are saved at once in the multiplayer mod's data folder (dsl_mod_data_dir, else
 * <persistentDataPath>/ModData/multiplayer), file pvp_rules.txt, and come back at the next start. Presets live in the same
 * file; they are local to each player (never synced). The host's values define the match: at Start the host sends them to
 * every player in one stamped order (same step everywhere, also in the join-point save for late joiners and reloads).
 *
 * Other code reads a rule with pvp_rule_get(id). Visibility rules (all display-only, default 0 = hidden):
 *   enemy_vision    1 = enemy PvP units visible everywhere (fog does not hide them)
 *   enemy_windows   1 = clicking an enemy unit opens its full unit window (0 = the monster inspect view only)
 *   enemy_portraits 1 = enemy units in my portrait bar
 *   enemy_hpbars    1 = enemy health bars in the world
 *   enemy_status    1 = enemy status / emotion / work gauges
 *   enemy_viewrange 1 = the enemy team's view range shown (lit tiles; the units stay hidden)
 * Match rules: mode (0 Fight, 1 Capture the Flag), win (index into 1/3/5), round_time (index into 60/120/180/300/480 s),
 *   pause (0 none, 1 planning), vanilla (0 mods allowed, 1 vanilla only, -1 = the mod's default), map (PVP_MAP_*),
 *   floor (PVP_FLOOR_*).
 * Pure part (pvp_rules_core.c + pvp_maps.c): no game calls; tests in src/pvpr_test (make -C src/pvpr_test test). */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* ---- the getter (multiplayer image; pvpfix's weak default in pvp_game.c returns -1).
   In a session after the host's rules arrived: the host's value. Before that (or with no session): the rule's default.
   -1 = unknown id. */
int pvp_rule_get(const char *id);

/* ---- maps (pvp_maps.c) */
enum { PVP_MAP_HERE = 0,          /* this settlement (PvP world / hub); in another world the old fixed arena floor */
       PVP_MAP_RANDOM = 1,        /* a random dungeon floor (dungeon and floor from the seed), random start points per team */
       PVP_MAP_DUNGEON1 = 2,      /* dungeon 1 = DungeonRegion VeilSilence (0), "The Veil of Silence" */
       PVP_MAP_DUNGEON2 = 3,      /* dungeon 2 = DungeonRegion FesteringBurrow (1), "Festering Burrow" */
       PVP_MAP_N = 4 };
enum { PVP_FLOOR_RANDOM = 0,      /* 1..4 = floor 1..4 (game floor index 0..3); 5 = the boss room (game floor index 4) */
       PVP_FLOOR_BOSS = 5, PVP_FLOOR_N = 6 };
#define PVP_DUNGEON_FLOORS 5      /* GAMESYSTEM_Dungeon_MaxFloor of both regions; the 5th floor = Portal, Entry, Boss */
typedef struct pvp_map_pick {
    int here;                     /* 1 = play where the units stand (no transfer); region/floor/seed unused */
    int32_t region, floor, seed;  /* DungeonRegion, 0-based floor index, the forced dungeon seed (ChangeMapRequested) */
    int boss;                     /* floor is the boss room */
    int random_starts;            /* the map was "Random map": pvpfix picks random start points from the seed */
} pvp_map_pick;
/* Same inputs = same pick on every computer (integer math only). seed = the host's draw at Start. Returns 0, -1 bad input. */
int pvp_map_resolve(int map, int floor_choice, uint32_t seed, pvp_map_pick *out);
/* the match's map (host's, after the sync; else the defaults): what was picked at Start (-1 region = here) */
const pvp_map_pick *pvp_map_now(void);

/* ---- rule table + values (pvp_rules_core.c) */
#define PVPR_MAX 24
#define PVPR_NAME_MAX 40          /* bytes of a preset name (UTF-8, cut at a character) */
#define PVPR_PRESETS_MAX 32
typedef struct pvpr_def { const char *id; int dflt, lo, hi; } pvpr_def;
typedef struct pvpr_vals { int v[PVPR_MAX]; } pvpr_vals;
int  pvpr_count(void);
const pvpr_def *pvpr_def_at(int i);
int  pvpr_index(const char *id);                      /* -1 unknown */
void pvpr_defaults(pvpr_vals *v);
int  pvpr_vals_get(const pvpr_vals *v, const char *id);   /* -1 unknown id */
int  pvpr_vals_set(pvpr_vals *v, const char *id, int x);  /* 0 ok, -1 unknown id / out of range (unchanged) */
int  pvpr_vals_equal(const pvpr_vals *a, const pvpr_vals *b);
/* "id=value\n" lines, table order. Returns bytes written (without the 0), 0 if it does not fit. */
size_t pvpr_format(const pvpr_vals *v, char *out, size_t cap);
/* reads "id=value" lines into v (start from what v holds): unknown ids and bad values are skipped. Returns rules set. */
int  pvpr_parse(pvpr_vals *v, const char *text, size_t n);
uint64_t pvpr_hash(const pvpr_vals *v);

/* ---- the store: last used rules + presets (one file) */
typedef struct pvpr_preset { char name[PVPR_NAME_MAX + 1]; pvpr_vals v; } pvpr_preset;
typedef struct pvpr_store { pvpr_vals last; int npresets; pvpr_preset presets[PVPR_PRESETS_MAX]; } pvpr_store;
void pvpr_store_init(pvpr_store *s);                   /* defaults, no preset */
/* file text <-> store. pvpr_store_text returns the length, 0 if it does not fit. pvpr_store_read: 0 ok (missing parts = defaults) */
size_t pvpr_store_text(const pvpr_store *s, char *out, size_t cap);
int  pvpr_store_read(pvpr_store *s, const char *text, size_t n);
int  pvpr_store_load(pvpr_store *s, const char *path); /* 0 read, 1 no file (defaults), -1 error (defaults) */
int  pvpr_store_save(const pvpr_store *s, const char *path);   /* write path.tmp, then rename: 0 ok */
/* presets: name cleaned (control bytes out, spaces trimmed, cut at PVPR_NAME_MAX at a character). Same name = replaced.
   Returns the preset index, -1 empty name, -2 store full. */
int  pvpr_preset_save(pvpr_store *s, const char *name, const pvpr_vals *v);
int  pvpr_preset_find(const pvpr_store *s, const char *name);
int  pvpr_preset_delete(pvpr_store *s, int i);         /* 0 ok, -1 no such preset */
int  pvpr_preset_match(const pvpr_store *s, const pvpr_vals *v);   /* first preset with exactly these values, -1 none */
void pvpr_clean_name(const char *in, char *out, size_t cap);

/* ---- multiplayer glue (pvp_rules.c) */
void pvp_rules_start(void);                            /* mp_modes_start: order handler, join-point block, fingerprint, rig cmd */
pvpr_vals *pvp_rules_local(void);                      /* this player's current choices (the box shows and edits these) */
pvpr_store *pvp_rules_store(void);
void pvp_rules_changed(void);                          /* the box changed a value: save the file now */
const char *pvp_rules_path(void);                      /* the file (for logs and tests) */
/* host at Start: send the current choices (+ the map pick) to every player. 0 = sent. */
int  pvp_rules_send(const pvp_map_pick *map);
int  pvp_rules_synced(void);                           /* 1 = the host's rules arrived in this session */
/* ---- the host's PvP window as shown (UX review 2026-10-25, P1): display only, never the match's rules (those still go
   in the stamped order at Start). Mail outside the sim: the host sends its rules + teams to every other player whenever
   they change, a player joins or a guest opens its window (it asks); a guest's window shows them. teams[p] per player
   slot (PVP_MAX_PLAYERS): 0 unknown, 1 Blue, 2 Red. */
#define PVPR_VIEW_TEAMS 32
int  pvp_rules_view_send(const pvpr_vals *v, const uint8_t teams[PVPR_VIEW_TEAMS]);   /* host: 0 = sent to everyone present */
int  pvp_rules_view_ask(void);                         /* guest: ask the host for its window (0 = mailed) */
int  pvp_rules_view_asked(void);                       /* host: 1 once after a guest asked */
const pvpr_vals *pvp_rules_view(uint8_t teams[PVPR_VIEW_TEAMS]);   /* guest: the host's last view, NULL = none yet */
/* the view as text ("id=value" lines, then "teams=" + one digit per slot); pure (pvp_rules_core.c). Parse returns 0 ok, -1 bad. */
size_t pvpr_view_format(const pvpr_vals *v, const uint8_t teams[PVPR_VIEW_TEAMS], char *out, size_t cap);
int  pvpr_view_parse(const char *text, size_t n, pvpr_vals *v, uint8_t teams[PVPR_VIEW_TEAMS]);
