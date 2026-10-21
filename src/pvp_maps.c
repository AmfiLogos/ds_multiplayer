/* pvp_maps - which map a PvP match is played on (lane pvprules, owner 2026-10-20). Pure C, no game calls. See pvp_rules.h.
 *
 * The arena floor itself is the GAME's own floor generator: the match sends the teams through a stock ChangeMapRequested
 * (Region, Floor, HasForcedDungeonSeed + ForcedDungeonSeed; pvp_game.c arena_go), and the game builds that floor from the
 * seed (DSDungeonPreDataGenerator: new System.Random(seed) -> DSLevelInput.GetLevelDescription -> Edgar layout), the same
 * floor on every peer (rig run arena2, room hash equal). This file only turns the host's choice + the host's seed draw
 * into (region, floor, seed): integer math, so every computer gets the same answer from the same input.
 * Floors (game tables, build 25846389): GAMESYSTEM_Dungeon_MaxFloor = 5 for both regions; floor index n runs phase
 * PhaseUnit[n + 1]; phase 5 of VeilSilence and of FesteringBurrow has one graph: Portal, Entry, Boss (the boss room). */
#include "pvp_rules.h"
#include <string.h>

static uint32_t mix(uint32_t seed, uint32_t salt) {    /* splitmix32-style finalizer */
    uint32_t z = seed + 0x9E3779B9u * (salt + 1);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}
int pvp_map_resolve(int map, int floor_choice, uint32_t seed, pvp_map_pick *out) {
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (map < 0 || map >= PVP_MAP_N || floor_choice < 0 || floor_choice >= PVP_FLOOR_N) return -1;
    if (map == PVP_MAP_HERE) { out->here = 1; out->region = -1; return 0; }
    out->seed = (int32_t)(mix(seed, 3) & 0x7FFFFFFFu);
    if (map == PVP_MAP_RANDOM) {                        /* a random dungeon and a random normal floor (not the boss room) */
        out->region = (int32_t)(mix(seed, 1) % 2u);
        out->floor = (int32_t)(mix(seed, 2) % (PVP_DUNGEON_FLOORS - 1));
        out->random_starts = 1;
        return 0;
    }
    out->region = map == PVP_MAP_DUNGEON1 ? 0 : 1;     /* DungeonRegion VeilSilence = 0, FesteringBurrow = 1 */
    if (floor_choice == PVP_FLOOR_BOSS) { out->floor = PVP_DUNGEON_FLOORS - 1; out->boss = 1; }
    else if (floor_choice == PVP_FLOOR_RANDOM) out->floor = (int32_t)(mix(seed, 2) % (PVP_DUNGEON_FLOORS - 1));
    else out->floor = floor_choice - 1;
    return 0;
}
