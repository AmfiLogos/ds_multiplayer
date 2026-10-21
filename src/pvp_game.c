/* pvp_game.c - PvP glue in the dylib (plan: research/plan_pvp.md 4.1 + 4.2). Separate source copy (ext/pvp), NOT part
 * of the multiplayer release.
 *  - State: one pvp_state (pvp_core.c). Changed only by the host's PvP command (mp order PVP_ORDER_TYPE, zero guid,
 *    applied on every peer at its stamped step; pvp_core drops it unless the issuer is slot 0) and by pvp_sim_tick.
 *  - Save: mp_game.c g_save / g_load carry pvp_block_get / pvp_block_set between the det state and the game JSON.
 *  - Fingerprint: mp_game.c g_fp mixes pvp_fp_mix (a peer with another PvP state is a desync).
 *  - Hostility (only while a match runs): every unit stays Faction Player. Two PvP units of different teams are made
 *    hostile at the three places the game decides it:
 *      FactionRelationInfo.GetRelation(IEntity, IEntity) 0x82BBA0  -> Hostile (skills: IsRelation / IsAffectTarget)
 *      EntityExtensions.IsHostile 0xFA4CE8                         -> true
 *      CombatDataHandler hostile lists (per unit HostileEntityData, the AI's target lists, filled by faction bucket in
 *      stock, so Player-Player is never there): after the stock OnSpawnUnit / OnMoveUnit / OnDeadUnit / OnDespawnUnit,
 *      the same AddHostile / RefreshHostile / RemoveHostile calls for the other team's units; START pairs everyone
 *      already on the map, END removes the pairs. Order = the team table's guid order: same on every peer.
 *  - Aggro: CheckAggroTrigger 0xBCE72C only looks in hostile buckets. A PvP unit that moves now also looks in its own
 *    bucket (Player, Player) and keeps the units of the other team (stock: a mover alerts the Unawared units whose
 *    aggro area it enters). */
#include "rig.h"
#include "mp_modes_text.h"
#include "mp.h"
#include "pvp_core.h"
#include "mu_bring.h"
int pvp_bring_picks(const mu_pick *picks, int n, char *err, size_t errn);   /* fe_game.c */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <dlfcn.h>

#define PVP_ORDER_TYPE 48            /* mp order type (free: 1-7, 20-22, 30-37 are used) */
#define PVP_FREEZE_TYPE 49           /* test only: host order "no world tick from world tick T on" (payload i64 T, -1 = off) */

mp_session *mp_game_session(void) __attribute__((weak_import));
static mp_session *sess(void) { return mp_game_session ? mp_game_session() : NULL; }
static pvp_state g;
/* Units Laboratory is a launcher feature (owner 2026-10-20): never in the multiplayer mod set, so a peer of a normal world
   may run without it. Nothing of it may then reach that world's shared state: with PvP off and its state as at start
   (pvp_init), the fingerprint mix is 0 (= no mix) and no join-point block is written (= the save of a peer without it). */
static int pvp_untouched(void) {
    static pvp_state z; static uint8_t zb[2048], b[2048]; static size_t zn;
    if (!zn) { pvp_init(&z); zn = pvp_save(&z, zb, sizeof zb); }
    size_t n = pvp_save(&g, b, sizeof b);
    return n == zn && !memcmp(b, zb, n);
}
int pvp_v1_world(void);                                  /* fe/fe_hub.c */
int fe_world_is_hub(void);                               /* fe/fe_game.c */

static uint32_t n_rel_hostile, n_ishostile, n_add, n_refresh, n_remove, n_aggro, n_rejected;
static int last_rc = 1;

/* ---------------------------------------------------------------- game access */
#define VA_GETRELATION   0x82BBA0    /* static RelationType FactionRelationInfo.GetRelation(IEntity, IEntity) */
#define VA_ISHOSTILE     0xFA4CE8    /* static bool EntityExtensions.IsHostile(IEntity, IEntity) */
#define VA_ISRELATION    0x82C5DC    /* static bool FactionRelationInfo.IsRelation(IEntity, IEntity, RelationType) = GetRelation(a, b) == r */
#define VA_CDH_INIT      0xBCDE24    /* CombatDataHandler.Init(RuntimeMapData) */
#define VA_CDH_SPAWN     0xBCF690    /* .OnSpawnUnit(UnitEntity, Vector2Int) */
#define VA_CDH_MOVE      0xBD1930    /* .OnMoveUnit(UnitEntity, Vector2Int last, Vector2Int current) */
#define VA_CDH_DEAD      0xBD00E8    /* .OnDeadUnit(UnitEntity) */
#define VA_CDH_DESPAWN   0xBD1EB8    /* .OnDespawnUnit(UnitEntity) */
#define VA_CDH_AGGRO     0xBCE72C    /* .CheckAggroTrigger(UnitEntity, Vector2Int, out List<IEntity>) */
#define VA_CDH_ISHOSTILE 0xBCEBA8    /* .IsHostileTo(Guid, Guid) */
#define VA_HED_ADD       0xBF3B88    /* HostileEntityData.AddHostile(Guid, Vector2Int) */
#define VA_HED_REMOVE    0xBF3C10    /* .RemoveHostile(Guid) */
#define VA_HED_REFRESH   0xBF3C88    /* .RefreshHostile(Guid, Vector2Int) */
#define VA_FAD_TRYGET    0xBF38F4    /* FactionAggroData.TryGetAggro(Vector2Int, out List<IEntity>) */
#define VA_LIST_ADD      0x240B904   /* List<object>.Add (shared generic body) */

typedef struct { uint64_t lo, hi; } G16;
static int owned_now(dsnet_client *c, pvp_owned *out, int max);
static G16 gget(Obj e) { Guid x = ent_guid(e); G16 r = { x.lo, x.hi }; return r; }

/* Dictionary<Guid, ref> entry walk: entries array +0x18, count +0x20; entry 0x20 bytes {hash, next, key16, value} */
static Obj dict_guid_get(Obj d, const uint8_t key[16]) {
    if (!d) return NULL;
    Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20;
        if (*(int32_t *)e >= 0 && !memcmp(e + 8, key, 16)) return *(Obj *)(e + 0x18);
    }
    return NULL;
}
/* CombatDataHandler per map (captured in Init / spawn hooks): RuntimeMapData +0x10 = MapType */
#define MAXMAPS 32
static Obj handler_of[MAXMAPS];
static void note_handler(Obj h) {
    Obj rd = h ? AT(h, 0x10, Obj) : NULL; if (!rd) return;
    int m = AT(rd, 0x10, int32_t);
    if (m >= 0 && m < MAXMAPS) handler_of[m] = h;
}
/* EntityDataContainer (campaign data +0x20) _runtimeMapDatas (+0x58) -> RuntimeMapData._combat (+0x60) */
static Obj combat_of(int map) {
    Obj c = cdc(); Obj edc = c ? AT(c, 0x20, Obj) : NULL;
    Obj rd = edc ? dict_int_get(AT(edc, 0x58, Obj), map) : NULL;
    Obj h = rd ? AT(rd, 0x60, Obj) : NULL;
    return h ? h : (map >= 0 && map < MAXMAPS ? handler_of[map] : NULL);
}
Obj pvp_runtime_map(int map) { Obj h = combat_of(map); return h ? AT(h, 0x10, Obj) : NULL; }   /* CombatDataHandler._runtimeData (fe/fe_hub.c) */
static Obj hed_of(Obj h, const uint8_t g16[16]) { return h ? dict_guid_get(AT(h, 0x30, Obj), g16) : NULL; }

static void add_hostile(Obj hed, const uint8_t who[16], uint64_t pos) {
    G16 x; memcpy(&x, who, 16);
    F(VA_HED_ADD, void (*)(Obj, uint64_t, uint64_t, uint64_t, void *))(hed, x.lo, x.hi, pos, NULL); n_add++;
}
static void refresh_hostile(Obj hed, const uint8_t who[16], uint64_t pos) {
    G16 x; memcpy(&x, who, 16);
    F(VA_HED_REFRESH, void (*)(Obj, uint64_t, uint64_t, uint64_t, void *))(hed, x.lo, x.hi, pos, NULL); n_refresh++;
}
static void remove_hostile(Obj hed, const uint8_t who[16]) {
    G16 x; memcpy(&x, who, 16);
    F(VA_HED_REMOVE, void (*)(Obj, uint64_t, uint64_t, void *))(hed, x.lo, x.hi, NULL); n_remove++;
}
/* both ways, positions = each HostileEntityData's own _position (+0x20, kept by RefreshOwner) */
static void pair(Obj h, const uint8_t a[16], const uint8_t b[16]) {
    Obj da = hed_of(h, a), db = hed_of(h, b);
    if (!da || !db) return;
    add_hostile(da, b, AT(db, 0x20, uint64_t));
    add_hostile(db, a, AT(da, 0x20, uint64_t));
}
static void pair_all(Obj h, int on) {
    for (int i = 0; i < g.nunits; i++)
        for (int j = i + 1; j < g.nunits; j++) {
            if (g.units[i].team == g.units[j].team) continue;
            if (on) pair(h, g.units[i].guid, g.units[j].guid);
            else {
                Obj di = hed_of(h, g.units[i].guid), dj = hed_of(h, g.units[j].guid);
                if (di) remove_hostile(di, g.units[j].guid);
                if (dj) remove_hostile(dj, g.units[i].guid);
            }
        }
}

/* ---------------------------------------------------------------- hooks */
/* the teams are enemies now. On the settlement (BASE) not while they walk to their ends: they start mixed together, and
   hostile from START they fought on the spot (pair test v1_b: placing timed out, units in combat at match tick 45) */
static int hostile_now(void) { return pvp_active(&g) && !(g.arena == PVP_WHERE_BASE && g.placing); }
static int (*o_getrel)(Obj, Obj, void *);
static int h_getrel(Obj a, Obj b, void *mi) {
    if (hostile_now() && a && b) {
        G16 ga = gget(a), gb = gget(b);
        if (pvp_hostile(&g, (const uint8_t *)&ga, (const uint8_t *)&gb) == 1) { n_rel_hostile++; return 1; }   /* RelationType.Hostile */
    }
    return o_getrel(a, b, mi);
}
/* Windows: MSVC inlined GetRelation into FactionRelationInfo.IsRelation (the Mac build calls it), and IsAffectTarget
   (DecidePlayerCombatBehaviour.DecideTarget / FindNewTarget) goes through IsRelation there. So on a Windows peer the
   GetRelation hook never ran for target choice: a unit picked another target than on the Mac host -> units section
   desync in Mac + Windows PvP (gate13 known issue, 2026-10-16). Same answer as GetRelation's hook, on both platforms. */
static uint32_t n_isrel;
static bool (*o_isrel)(Obj, Obj, int, void *);
static bool h_isrel(Obj a, Obj b, int rel, void *mi) {
    if (hostile_now() && a && b) {
        G16 ga = gget(a), gb = gget(b);
        if (pvp_hostile(&g, (const uint8_t *)&ga, (const uint8_t *)&gb) == 1) { n_isrel++; return rel == 1; }   /* RelationType.Hostile */
    }
    return o_isrel(a, b, rel, mi);
}
static bool (*o_ishostile)(Obj, Obj, void *);
static bool h_ishostile(Obj a, Obj b, void *mi) {
    if (hostile_now() && a && b) {
        G16 ga = gget(a), gb = gget(b);
        if (pvp_hostile(&g, (const uint8_t *)&ga, (const uint8_t *)&gb) == 1) { n_ishostile++; return true; }
    }
    return o_ishostile(a, b, mi);
}
static void (*o_init)(Obj, Obj, void *);
static void h_init(Obj self, Obj rd, void *mi) { o_init(self, rd, mi); note_handler(self); }

static int team_of(Obj unit, G16 *gu) {
    if (!hostile_now() || !unit) return PVP_TEAM_NONE;
    *gu = gget(unit);
    return pvp_team_of_unit(&g, (const uint8_t *)gu);
}
static void (*o_spawn)(Obj, Obj, uint64_t, void *);
static void h_spawn(Obj self, Obj unit, uint64_t pos, void *mi) {
    o_spawn(self, unit, pos, mi);
    note_handler(self);
    G16 gu; int t = team_of(unit, &gu); if (!t) return;
    if (pvp_out_tick(&g, (const uint8_t *)&gu) >= 0) return;
    for (int i = 0; i < g.nunits; i++)
        if (g.units[i].team != t && !g.units[i].out) pair(self, (const uint8_t *)&gu, g.units[i].guid);
}
static void (*o_move)(Obj, Obj, uint64_t, uint64_t, void *);
static void h_move(Obj self, Obj unit, uint64_t last, uint64_t cur, void *mi) {
    o_move(self, unit, last, cur, mi);
    G16 gu; int t = team_of(unit, &gu); if (!t || unit_dead(unit) || pvp_out_tick(&g, (const uint8_t *)&gu) >= 0) return;
    for (int i = 0; i < g.nunits; i++) {
        if (g.units[i].team == t || g.units[i].out) continue;
        Obj d = hed_of(self, g.units[i].guid);
        if (d) refresh_hostile(d, (const uint8_t *)&gu, cur);
    }
}
static void gone(Obj self, Obj unit) {
    G16 gu; int t = team_of(unit, &gu); if (!t) return;
    for (int i = 0; i < g.nunits; i++) {
        if (g.units[i].team == t) continue;
        Obj d = hed_of(self, g.units[i].guid);
        if (d) remove_hostile(d, (const uint8_t *)&gu);
    }
}
static void (*o_dead)(Obj, Obj, void *);
static void h_dead(Obj self, Obj unit, void *mi) { o_dead(self, unit, mi); gone(self, unit); }
static void (*o_despawn)(Obj, Obj, void *);
static void h_despawn(Obj self, Obj unit, void *mi) { o_despawn(self, unit, mi); gone(self, unit); }

/* CheckAggroTrigger: stock looks only in the hostile buckets' FactionAggroData. For a PvP mover also look in its own
   bucket's FactionAggroData and add the other team's units found there (no teammates). */
static bool (*o_aggro)(Obj, Obj, uint64_t, Obj *, void *);
static bool h_aggro(Obj self, Obj unit, uint64_t pos, Obj *out, void *mi) {
    bool r = o_aggro(self, unit, pos, out, mi);
    G16 gu; int t = team_of(unit, &gu); if (!t || !out || !*out) return r;
    /* own bucket: _factionAggros (+0x18) is keyed by (Faction, SubFaction); every PvP unit is (Player 1, Player 1).
       Dictionary<ValueTuple<int,int>, ref> entries: {hash, next, key i32 i32, value} = 0x18 bytes */
    Obj d = AT(self, 0x18, Obj); if (!d) return r;
    Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t); Obj fad = NULL;
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x18;
        if (*(int32_t *)e >= 0 && *(int32_t *)(e + 8) == 1 && *(int32_t *)(e + 12) == 1) { fad = *(Obj *)(e + 0x10); break; }
    }
    if (!fad) return r;
    Obj found = NULL;
    if (!F(VA_FAD_TRYGET, bool (*)(Obj, uint64_t, Obj *, void *))(fad, pos, &found, NULL) || !found) return r;
    Obj arr = AT(found, 0x10, Obj); int cnt = AT(found, 0x18, int32_t);
    Obj list = *out; int added = 0;
    if (!*(void **)list) return r;
    for (int i = 0; arr && i < cnt; i++) {
        Obj e = AT(arr, 0x20 + i * 8, Obj);
        if (!e) continue;
        G16 ge = gget(e);
        int te = pvp_team_of_unit(&g, (const uint8_t *)&ge);
        if (!te || te == t) continue;
        /* List<IEntity>.Add = shared List<object>.Add 0x240B904: its MethodInfo is only read for klass (+0x20) ->
           rgctx (+0xC0) -> AddWithResize; a stand-in whose +0x20 is the list's own class gives the same path */
        void *mi_add[5] = { 0, 0, 0, 0, *(void **)list };
        F(VA_LIST_ADD, void (*)(Obj, Obj, void *))(list, e, mi_add);
        added++;
    }
    if (added) { n_aggro += (uint32_t)added; return true; }
    return r;
}


/* ---------------------------------------------------------------- arena (M2, plan 4.3) */
#define VA_SPAWN_CATEGORY 0xCBA210   /* DungeonGeneratorStrategy.HandleSpawnCategory(roomID, seed, SpawnCategory, positions, table, portal) */
#define VA_SPAWN_NPC      0xDA8B30   /* CampaignEntityLoadHelper.SpawnDungeonNPC() -> List<UnitEntity> */
#define VA_DUNGEON_TICK   0xBC4D80   /* DungeonBehaviorSimulator.Tick(): respawn, patrol, core, penalty */
#define VA_PORTAL_PROCESS 0xF9E298   /* DungeonPortalHandler.Process(InteractBuildingRequested) */
#define VA_GAMEOVER       0xD854EC   /* EntityEventHandler.CheckGameOver(MapType) */
#define VA_LIST_CTOR      0x240AEDC  /* List<object>..ctor() (shared body; MethodInfo only read for klass) */
#define TI_LIST_IENTITY   0x66458C0  /* List<IEntity> TypeInfo slot (CheckAggroTrigger) */
#define TI_LIST_UNIT      0x66464D8  /* List<UnitEntity> TypeInfo slot (SpawnDungeonNPC) */
static uint32_t n_spawn_cut, n_npc_cut, n_dungeon_tick_cut, n_portal_refused, n_gameover_cut;
static Obj empty_list(uint64_t ti) {
    Obj l = obj_new(ti); if (!l) return NULL;
    void *mi[5] = { 0, 0, 0, 0, *(void **)l };
    F(VA_LIST_CTOR, void (*)(Obj, void *))(l, mi);
    return l;
}
/* SpawnCategory: MonsterSpawn 2, ItemSpawn1-2 9-10, NPCSpawn1-3 11-13 are cut; portals, player spawn and buildings stay */
static Obj (*o_spawncat)(Obj, Obj, int32_t, int32_t, Obj, Obj, Obj, void *);
static Obj h_spawncat(Obj self, Obj room, int32_t seed, int32_t cat, Obj pos, Obj table, Obj portal, void *mi) {
    if (pvp_arena(&g) && (cat == 2 || cat >= 9)) { n_spawn_cut++; return empty_list(TI_LIST_IENTITY); }
    return o_spawncat(self, room, seed, cat, pos, table, portal, mi);
}
static Obj (*o_spawnnpc)(Obj, void *);
static Obj h_spawnnpc(Obj self, void *mi) {
    if (pvp_arena(&g)) { n_npc_cut++; return empty_list(TI_LIST_UNIT); }
    return o_spawnnpc(self, mi);
}
static void (*o_dtick)(Obj, void *);
static void h_dtick(Obj self, void *mi) { if (pvp_arena(&g)) { n_dungeon_tick_cut++; return; } o_dtick(self, mi); }
void ui_notice(const char *text) __attribute__((weak_import));
static void (*o_portal)(Obj, Obj, void *);
static void h_portal(Obj self, Obj msg, void *mi) {
    if (pvp_arena(&g)) {
        n_portal_refused++;
        if (ui_notice && n_portal_refused % 64 == 1) ui_notice(TR("No portals in a PvP match"));
        return;
    }
    o_portal(self, msg, mi);
}
/* PortalOrder.Open (the portal right-click: opens the return / next-floor screen) is hooked by ui_teams.c; it asks here */
int pvp_portal_blocked(void) {
    if (!pvp_arena(&g)) return 0;
    n_portal_refused++;
    if (ui_notice) ui_notice(TR("No portals in a PvP match"));
    return 1;
}
/* (4) owner 2026-10-14: in a match a PvP unit goes DOWN and stays down; it never dies (no item drop, no corpse). The stock
   death = MainEventApplier.Apply(DeadApplyData) 0xAB7628 (sets EntityInfo._isDead, drops the items, DeadUnitRequested):
   skipped for a unit of the PvP table while the match runs. Same on every peer (sim data + PvP state).
   Stock StatusApplier.CalculateSideEffect: a unit at 0 HP without AFFECTER_Deadly is downed (that affecter + DeadlyApplyData);
   one at 0 HP that has it (already down) gets DeadApplyData with key AFFECTER_Deadly. So a down unit stays down here.
   A death of a unit still up (other paths) becomes MainEventApplier.Apply(DeadlyApplyData) 0xAB838C instead.
   "Down" = the stock party-wipe test <CheckGameOver>b__69_0 0xD8DA20 says not conscious (Unconscious affecter): ctfdrop5
   showed the task is not a safe sign (an order puts a down unit back on task 15). */
static bool unit_conscious(Obj u) { return F(0xD8DA20, bool (*)(Obj, Obj, void *))(NULL, u, NULL); }
#define VA_DEAD_APPLY 0xAB7628
#define VA_DEADLY_APPLY 0xAB838C
#define TI_DEADLY_DATA  0x664C368
#define VA_DEADLY_CTOR  0x845DE4   /* DeadlyApplyData..ctor(IEntity target, string key) */
static uint32_t n_death_skipped, n_downed_made;
static void (*o_deadapply)(Obj, Obj, void *);
static void key_str(Obj s, char *out, int n) {
    int len = s ? AT(s, 0x10, int32_t) : 0, i = 0;
    for (; s && i < len && i < n - 1; i++) out[i] = (char)AT(s, 0x14 + 2 * i, uint16_t);
    out[i] = 0;
}
static void h_deadapply(Obj self, Obj data, void *mi) {
    Obj tgt = data ? AT(data, 0x18, Obj) : NULL;
    if (tgt && pvp_active(&g)) {
        G16 gt = gget(tgt);
        if (pvp_team_of_unit(&g, (const uint8_t *)&gt)) {
            n_death_skipped++;
            if (unit_conscious(tgt)) {
                char k[64]; key_str(AT(data, 0x10, Obj), k, sizeof k);
                Obj d = obj_new(TI_DEADLY_DATA);
                if (d) {
                    F(VA_DEADLY_CTOR, void (*)(Obj, Obj, Obj, void *))(d, tgt, AT(data, 0x10, Obj), NULL);
                    F(VA_DEADLY_APPLY, void (*)(Obj, Obj, void *))(self, d, NULL);
                    n_downed_made++;
                }
                rig_log("pvp: death of a PvP unit (key %s) -> downed instead (task now %d)", k, unit_task(tgt));
            }
            return;
        }
    }
    o_deadapply(self, data, mi);
}
/* E3: builder units (Units Laboratory's My Units / Unit Builder, brought with their full entry) carry the builder's folded
   conditional passives as PermanentStats grants (owner rule: the builder's numbers). Their live grant must not add the same
   bonus again: PassiveActionContainer.Register(entity, key, isInit) 0xA782C8 is skipped for such a skill on a unit whose
   permanent record of that skill's stat is set (StatusReader +0x10 -> StatusComponent, permanent records +0x40,
   Dictionary<StatType,float>), only in a PvP / expedition world (a normal world stays stock). In multiplayer itself (not
   Units Laboratory): every peer runs the same rule on the same save, with or without Units Laboratory. */
int mu_folded_skill(const char *key, char *stat, size_t n);   /* mu_save.c */
int rig_stat_id(const char *name);
static uint32_t n_passive_skipped;
static void (*o_preg)(Obj, Obj, Obj, bool, void *);
static int perm_has(Obj ent, int stat) {
    Obj sr = F(0x185FEDC, Obj (*)(Obj, void *))(ent, md(0x6691B40));          /* GetComponent<StatusReader> */
    Obj sc = sr ? AT(sr, 0x10, Obj) : NULL, d = sc ? AT(sc, 0x40, Obj) : NULL;
    Obj ent_arr = d ? AT(d, 0x18, Obj) : NULL; int cnt = d ? AT(d, 0x20, int32_t) : 0;
    for (int i = 0; ent_arr && i < cnt; i++) {
        char *e = (char *)ent_arr + 0x20 + i * 16;                            /* {hash, next, key, value} */
        if (*(int32_t *)e >= 0 && *(int32_t *)(e + 8) == stat && *(float *)(e + 12) != 0) return 1;
    }
    return 0;
}
static void h_preg(Obj self, Obj ent, Obj key, bool init, void *mi) {
    char k[96] = "", st[64];
    int len = key ? AT(key, 0x10, int32_t) : 0;
    for (int i = 0; i < len && i < 95; i++) k[i] = (char)AT(key, 0x14 + 2 * i, uint16_t);
    if (ent && k[0] && (pvp_v1_world() || fe_world_is_hub() || pvp_hold_events(&g)) && mu_folded_skill(k, st, sizeof st)) {
        int id = rig_stat_id(st);
        if (id > 0 && perm_has(ent, id)) { n_passive_skipped++; rig_log("pvp: %s not registered on a builder unit (its bonus is folded in its permanent stats)", k); return; }
    }
    o_preg(self, ent, key, init, mi);
}
/* stock story events are held from PvP setup until the old world is back (pvp_hold_events): EventContainer.IsEventStartable
   0xC8D6FC (timed events) answers no, EventContainer.StartEvent(key) 0xC8DFBC and EventFlowProcessor.StartEvent(key) 0xCEA620
   (keyed events) do nothing */
static uint32_t n_events_held;
static bool (*o_ev_startable)(Obj, int64_t, void *);
static bool h_ev_startable(Obj self, int64_t t, void *mi) { if (pvp_hold_events(&g)) return false; return o_ev_startable(self, t, mi); }
static bool (*o_ev_start1)(Obj, Obj, void *), (*o_ev_start2)(Obj, Obj, void *);
static bool h_ev_start1(Obj self, Obj key, void *mi) { if (pvp_hold_events(&g)) { n_events_held++; return false; } return o_ev_start1(self, key, mi); }
static bool h_ev_start2(Obj self, Obj key, void *mi) { if (pvp_hold_events(&g)) { n_events_held++; return false; } return o_ev_start2(self, key, mi); }
static void (*o_gameover)(Obj, int32_t, void *);
static void h_gameover(Obj self, int32_t map, void *mi) { if (pvp_arena(&g)) { n_gameover_cut++; return; } o_gameover(self, map, mi); }

/* arena transfer: a stock ChangeMapRequested (Dungeon -> Dungeon, forced seed) put in the EventQueue by the PvP order on
   every peer at the same step (trn_game.c builds it). Units = every owned unit of a player with a team, guid order. */
int trn_apply_map_request(const uint8_t *p, uint16_t len);
int trn_last_rooms(int32_t *type, int32_t (*piv)[2], int max, int32_t off[2], uint64_t *hash);
static int arena_rc = 1;
static void arena_go(mp_session *s) {
    static pvp_owned own[256];
    int no = owned_now(mp_client(s), own, 256), n = 0, map = cur_map();
    uint8_t p[1 + 19 + 1 + 32 * 16]; int o = 0;
    int32_t vals[2] = { g.region, g.floor }, sd = g.seed, z = 0;
    p[o++] = 2; p[o++] = (uint8_t)map; p[o++] = 1; memcpy(p + o, vals, 8); o += 8;
    p[o++] = 2;                                   /* flags: bit1 hasForcedDungeonSeed */
    memcpy(p + o, &sd, 4); o += 4; memcpy(p + o, &z, 4); o += 4;
    int ni = o++;
    /* guid order: owned_now is per player; sort the picked guids */
    uint8_t gl[32][16];
    for (int i = 0; i < no && n < 32; i++) {
        if (own[i].player < 0 || own[i].player >= PVP_MAX_PLAYERS || !g.team_of[own[i].player]) continue;
        Guid gg; memcpy(&gg, own[i].guid, 16); if (!find_unit(map, gg)) continue;
        memcpy(gl[n++], own[i].guid, 16);
    }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && memcmp(gl[j - 1], gl[j], 16) > 0; j--) { uint8_t t[16]; memcpy(t, gl[j], 16); memcpy(gl[j], gl[j - 1], 16); memcpy(gl[j - 1], t, 16); }
    for (int i = 0; i < n; i++) { memcpy(p + o, gl[i], 16); o += 16; }
    p[ni] = (uint8_t)n;
    arena_rc = trn_apply_map_request(p, (uint16_t)o);
    rig_log("pvp: arena transfer of %d units to region %d floor %d seed %d (map %d): rc %d", n, g.region, g.floor, g.seed, map, arena_rc);
}

/* team ends at START (arena): Blue holds the arrival point (mean cell of the Blue units); Red goes to the exit portal
   building farthest from it (BuildingDataContainer per map: EntityDataContainer +0x20 -> +0x18 Dictionary<Guid, BuildingEntity>,
   key +0x38 contains "Portal"). Cells from pos_dict (Guid -> Vector2). Same data on every peer. */
static int unit_cell(int map, const uint8_t g16[16], int32_t c[2]) {
    Obj pd = pos_dict(map); if (!pd) return 0;
    Obj ent = AT(pd, 0x18, Obj); int n = AT(pd, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {                /* Dictionary<Guid, Vector2>: {hash, next, key16, x, y} = 0x20 */
        char *e = (char *)ent + 0x20 + i * 0x20;
        if (*(int32_t *)e >= 0 && !memcmp(e + 8, g16, 16)) { c[0] = (int32_t)floorf(*(float *)(e + 0x18)); c[1] = (int32_t)floorf(*(float *)(e + 0x1C)); return 1; }
    }
    return 0;
}
static int key_has(Obj s, const char *needle) {
    if (!s) return 0;
    int n = AT(s, 0x10, int32_t), k = (int)strlen(needle); const uint16_t *w = (const uint16_t *)((char *)s + 0x14);
    for (int i = 0; i + k <= n; i++) { int j = 0; while (j < k && w[i + j] == (uint16_t)needle[j]) j++; if (j == k) return 1; }
    return 0;
}
static int32_t portal_cells[16][2]; static uint8_t portal_guid[16][16]; static int nportal;
static void find_ends(int map) {
    int32_t blue[2] = { 0, 0 }, red[2] = { 0, 0 }; int nb = 0; int64_t sx = 0, sy = 0;
    for (int i = 0; i < g.nunits; i++) {
        int32_t c[2]; if (g.units[i].team != PVP_TEAM_BLUE || !unit_cell(map, g.units[i].guid, c)) continue;
        sx += c[0]; sy += c[1]; nb++;
    }
    if (nb) { blue[0] = (int32_t)(sx / nb); blue[1] = (int32_t)(sy / nb); }
    nportal = 0;
    Obj c0 = cdc(); Obj edc = c0 ? AT(c0, 0x20, Obj) : NULL;
    Obj bc = edc ? dict_int_get(AT(edc, 0x20, Obj), map) : NULL;
    Obj bd = bc ? AT(bc, 0x18, Obj) : NULL;
    int64_t best = -1;
    if (bd) {
        Obj ent = AT(bd, 0x18, Obj); int n = AT(bd, 0x20, int32_t);
        for (int i = 0; ent && i < n; i++) {
            char *e = (char *)ent + 0x20 + i * 0x20;
            if (*(int32_t *)e < 0) continue;
            Obj b = *(Obj *)(e + 0x18);
            if (!b || !key_has(AT(b, 0x38, Obj), "Portal")) continue;
            int32_t c[2]; if (!unit_cell(map, (const uint8_t *)(e + 8), c)) continue;
            if (nportal < 16) { portal_cells[nportal][0] = c[0]; portal_cells[nportal][1] = c[1]; memcpy(portal_guid[nportal], e + 8, 16); nportal++; }
            int64_t d = (int64_t)(c[0] - blue[0]) * (c[0] - blue[0]) + (int64_t)(c[1] - blue[1]) * (c[1] - blue[1]);
            if (d > best || (d == best && memcmp(c, red, 8) < 0)) { best = d; red[0] = c[0]; red[1] = c[1]; }
        }
    }
    pvp_set_ends(&g, blue, red);
    rig_log("pvp: ends blue (%d,%d) red (%d,%d), %d portal buildings", blue[0], blue[1], red[0], red[1], nportal);
}
static int send_to_ends(int map) {
    int sent = 0;
    for (int i = 0; i < g.nunits; i++) {
        if (g.units[i].team != PVP_TEAM_RED) continue;
        Guid gg; memcpy(&gg, g.units[i].guid, 16);
        if (!rig_order_move(gg, g.end_cell[PVP_TEAM_RED][0], g.end_cell[PVP_TEAM_RED][1])) sent++;
    }
    (void)map;
    return sent;
}
/* PvP v1 on the settlement map (BASE): each team walks to its end (owner v12 13: "players appear there, PvP is on"); the
   ends: find_base_ends below (E6: random reachable cells). */
#define VA_MAP_MOVABLE 0xBEF2F0      /* RuntimeMapData.IsMovable(Vector2Int) */
#define VA_MAP_VISITED 0xBEF354      /* RuntimeMapData.IsVisited(Vector2Int) */
typedef struct { int32_t x, y; } V2I;
static int movable(Obj rd, int x, int y, int mx, int my) {
    if (x < 0 || y < 0 || x >= mx || y >= my) return 0;
    V2I v = { x, y };                                                     /* walkable AND explored: the lit settlement (grid probe */
    return F(VA_MAP_MOVABLE, bool (*)(Obj, V2I, void *))(rd, v, NULL)      /* 2026-10-16: movable-only cells run far into the dark, */
        && F(VA_MAP_VISITED, bool (*)(Obj, V2I, void *))(rd, v, NULL);     /* where the units never got) */
}
/* E6 (owner v0.1.20 PvP session: "on a random PvP map, teams spawn at random valid locations"): the two ends are drawn
   at random from the settlement cells the units can reach (walkable AND explored, one connected area from the units, at most
   BASE_REACH cells away), each with at least 6 of its 8 neighbours walkable (two units fit), Red between BASE_MIN_D and BASE_MAX_D
   cells from Blue. The draw is a hash of the match number and the unit table (inside the stamped START: same cells on every
   peer, a new pair each match), never a game or local random number. No such pair: the farthest reachable pair. */
#define BASE_REACH 40
#define BASE_MIN_D 12
#define BASE_MAX_D 30
static uint64_t mix64(uint64_t x) { x += 0x9E3779B97F4A7C15ull; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull; x = (x ^ (x >> 27)) * 0x94D049BB133111EBull; return x ^ (x >> 31); }
static int roomy(Obj rd, int x, int y, int mx, int my) {                /* at least 6 of the 8 neighbours walkable: two units fit */
    int n = 0;
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if ((dx || dy) && movable(rd, x + dx, y + dy, mx, my)) n++;
    return n >= 6;
}
static void find_base_ends(int map) {
    Obj h = combat_of(map), rd = h ? AT(h, 0x10, Obj) : NULL;             /* CombatDataHandler._runtimeData */
    int64_t sx = 0, sy = 0; int nu = 0; int32_t first[2] = { 0, 0 };
    for (int i = 0; i < g.nunits; i++) { int32_t c[2]; if (unit_cell(map, g.units[i].guid, c)) { if (!nu) { first[0] = c[0]; first[1] = c[1]; } sx += c[0]; sy += c[1]; nu++; } }
    if (!rd || !nu) { rig_log("pvp: base ends: no map data (%p) or no unit cell (%d): teams stay where they are", rd, nu); return; }
    int mx = AT(rd, 0x28, int32_t), my = AT(rd, 0x2C, int32_t);           /* _maxSize */
    int cx = (int)(sx / nu), cy = (int)(sy / nu);
    if (!movable(rd, cx, cy, mx, my)) {                                    /* the mean cell may be a wall or a unit's cell: the nearest walkable one */
        int found = 0;
        for (int r = 1; r <= 6 && !found; r++) for (int dy = -r; dy <= r && !found; dy++) for (int dx = -r; dx <= r && !found; dx++)
            if ((abs(dx) == r || abs(dy) == r) && movable(rd, cx + dx, cy + dy, mx, my)) { cx += dx; cy += dy; found = 1; }
        if (!found) { cx = first[0]; cy = first[1]; }
    }
    enum { S = 2 * BASE_REACH + 1 };
    static uint8_t seen[S * S]; static int16_t qx[S * S], qy[S * S]; static int32_t room[S * S][2];
    memset(seen, 0, sizeof seen);
    int qh = 0, qt = 0, nroom = 0;
    if (movable(rd, cx, cy, mx, my)) { seen[BASE_REACH * S + BASE_REACH] = 1; qx[qt] = (int16_t)cx; qy[qt] = (int16_t)cy; qt++; }
    while (qh < qt) {                                                       /* flood fill, 4 directions, fixed order */
        int x = qx[qh], y = qy[qh]; qh++;
        if (roomy(rd, x, y, mx, my)) { room[nroom][0] = x; room[nroom][1] = y; nroom++; }
        static const int d4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int k = 0; k < 4; k++) {
            int nx = x + d4[k][0], ny = y + d4[k][1], ix = nx - cx + BASE_REACH, iy = ny - cy + BASE_REACH;
            if (ix < 0 || iy < 0 || ix >= S || iy >= S || seen[iy * S + ix] || !movable(rd, nx, ny, mx, my)) continue;
            seen[iy * S + ix] = 1; qx[qt] = (int16_t)nx; qy[qt] = (int16_t)ny; qt++;
        }
    }
    if (nroom < 2) { rig_log("pvp: base ends: %d roomy reachable cells near (%d,%d): teams stay where they are", nroom, cx, cy); return; }
    uint64_t seed = 1469598103934665603ULL; seed = fnv(seed, &g.matches, 4); for (int i = 0; i < g.nunits; i++) seed = fnv(seed, g.units[i].guid, 16);
    uint64_t r1 = mix64(seed), r2 = mix64(seed ^ 0xA5A5A5A5A5A5A5A5ull);
    int b0 = (int)(r1 % (uint64_t)nroom), bi = b0, ri = -1, nok = 0;
    for (int k = 0; k < nroom; k++) {                                     /* Blue: the drawn cell, else the next one that has a Red cell in range */
        int c2 = (b0 + k) % nroom, cnt = 0;
        for (int i = 0; i < nroom; i++) {
            int64_t dx = room[i][0] - room[c2][0], dy = room[i][1] - room[c2][1], d2 = dx * dx + dy * dy;
            if (d2 >= BASE_MIN_D * BASE_MIN_D && d2 <= BASE_MAX_D * BASE_MAX_D) cnt++;
        }
        if (cnt) { bi = c2; nok = cnt; break; }
    }
    if (nok) {
        int want = (int)(r2 % (uint64_t)nok);
        for (int i = 0; i < nroom && ri < 0; i++) {
            int64_t dx = room[i][0] - room[bi][0], dy = room[i][1] - room[bi][1], d2 = dx * dx + dy * dy;
            if (d2 >= BASE_MIN_D * BASE_MIN_D && d2 <= BASE_MAX_D * BASE_MAX_D && want-- == 0) ri = i;
        }
    } else {                                                                /* small area: the farthest cell from Blue */
        int64_t best = -1;
        for (int i = 0; i < nroom; i++) { int64_t dx = room[i][0] - room[bi][0], dy = room[i][1] - room[bi][1], d2 = dx * dx + dy * dy; if (d2 > best) { best = d2; ri = i; } }
    }
    int32_t blue[2] = { room[bi][0], room[bi][1] }, red[2] = { room[ri][0], room[ri][1] };
    pvp_set_ends(&g, blue, red);
    rig_log("pvp: base ends (random, match %u): %d roomy reachable cells from (%d,%d), %d at 12..30 cells: blue (%d,%d) red (%d,%d)", g.matches, nroom, cx, cy, nok,
            blue[0], blue[1], red[0], red[1]);
}
static int send_both_to_ends(void) {
    int sent = 0;
    if (!g.end_cell[PVP_TEAM_RED][0] && !g.end_cell[PVP_TEAM_RED][1]) return 0;
    for (int i = 0; i < g.nunits; i++) {
        int t = g.units[i].team; if (t != PVP_TEAM_BLUE && t != PVP_TEAM_RED) continue;
        Guid gg; memcpy(&gg, g.units[i].guid, 16);
        if (!rig_order_move(gg, g.end_cell[t][0], g.end_cell[t][1])) sent++;
    }
    return sent;
}
/* fight start (inside the sim tick): every PvP unit still in gets an attack order on the closest enemy (cells, ties by
   guid order), the RUN payload of a right-click attack applied on every peer (no network: every peer runs the same
   rule at the same tick) */
int ord_attack_payload(const uint8_t unit[16], const uint8_t target[16], int x, int y, uint8_t **out, size_t *n);
int ord_apply(int player, const uint8_t *payload, size_t len);
void rig_det_sim_scope(int enter);
static int n_kick;
static int fix_out = 1;
/* E7 (owner v0.1.20 PvP session: "G (hold position) is ignored and units start chasing / attacking opponents"): the
   automatic attack orders (every unit on its closest enemy at the fight start, idle units every 2 s) were given in the
   player's name and overrode hold position and the player's own orders. Off: units do what their player tells them (the
   game's own combat AI still answers enemies that come close). pvp_cfg kicks 1 = the old behaviour (tests only). */
static int kicks = 0;
/* every 2 s of fight while a unit is out: what each unit in is doing (combat state, target unit index, task) */
static void diag(int map) {
    char line[400]; int o = 0, any_out = 0;
    for (int i = 0; i < g.nunits; i++) any_out |= g.units[i].out != 0;
    if (!any_out) return;
    for (int i = 0; i < g.nunits; i++) {
        Guid gi; memcpy(&gi, g.units[i].guid, 16); Obj u = find_unit(map, gi);
        int cs = -9, ti = -1, tt = -1; Guid a = {0, 0}, b2, c2;
        if (u) unit_combat(u, &cs, &a, &b2, &c2);
        for (int j = 0; j < g.nunits; j++) { if (!memcmp(&a, g.units[j].guid, 16)) ti = j; if (!memcmp(&c2, g.units[j].guid, 16)) tt = j; }
        o += snprintf(line + o, sizeof line - (size_t)o, " u%d:%s cs%d tgt%d task%d->%d hp%.0f", i, g.units[i].out ? "OUT" : "in", cs, ti,
                      u ? unit_task(u) : -2, tt, u ? unit_stat(u, 9) : -1.0f);
    }
    rig_log("pvp: diag t%u%s", g.ticks, line);
}
static void fight_kick_idle(int map, int only_idle);
static void fight_kick(int map) { fight_kick_idle(map, 0); }
static void fight_kick_idle(int map, int only_idle) {
    const char *why = "start";
    if (!kicks) return;
    int32_t cell[PVP_MAX_UNITS][2]; int ok[PVP_MAX_UNITS];
    for (int i = 0; i < g.nunits; i++) ok[i] = !g.units[i].out && unit_cell(map, g.units[i].guid, cell[i]);
    for (int i = 0; i < g.nunits; i++) {
        if (!ok[i]) continue;
        if (only_idle) {                          /* re-kick: only a unit that is Unawared (combat state 0) */
            Guid gi; memcpy(&gi, g.units[i].guid, 16); Obj u = find_unit(map, gi);
            int cs = -1; Guid a, b2, c2; if (u) unit_combat(u, &cs, &a, &b2, &c2);
            /* idle, or still busy with an enemy that is out (current / forced / task target) */
            int on_out = 0;
            for (int j = 0; j < g.nunits; j++) if (g.units[j].out) {
                if (!memcmp(&a, g.units[j].guid, 16) || !memcmp(&b2, g.units[j].guid, 16) || !memcmp(&c2, g.units[j].guid, 16)) on_out = 1;
            }
            if (!fix_out) on_out = 0;
            if (cs != 0 && !on_out) continue;
            why = on_out ? "target out" : "idle";
        }
        int best = -1; int64_t bd = 0;
        for (int j = 0; j < g.nunits; j++) {
            if (!ok[j] || g.units[j].team == g.units[i].team) continue;
            int64_t d = (int64_t)(cell[i][0] - cell[j][0]) * (cell[i][0] - cell[j][0]) + (int64_t)(cell[i][1] - cell[j][1]) * (cell[i][1] - cell[j][1]);
            if (best < 0 || d < bd) { best = j; bd = d; }
        }
        if (best < 0) continue;
        uint8_t *pl = NULL; size_t pn = 0;
        if (ord_attack_payload(g.units[i].guid, g.units[best].guid, cell[best][0], cell[best][1], &pl, &pn)) continue;
        int rc = ord_apply(g.units[i].player, pl, pn); free(pl);
        if (!rc) n_kick++;
        rig_log("pvp: fight %s unit %d -> %d (%s): rc %d", only_idle ? "re-kick" : "kick", i, best, why, rc);
    }
}
static uint32_t n_walk_resend;
#define PLACE_MAX_TICKS 3600         /* placing ends after 60 s of game time even if Red is not there */
static void placing_step(int map) {
    int arrived = 1, any = 0;
    for (int i = 0; i < g.nunits; i++) {                  /* dungeon: Red walks, Blue holds; base: both walk */
        int t = g.units[i].team;
        if ((t != PVP_TEAM_RED && !(g.arena == PVP_WHERE_BASE && t == PVP_TEAM_BLUE)) || g.units[i].out) continue;
        int32_t c[2]; if (!unit_cell(map, g.units[i].guid, c)) continue;
        any = 1;
        int dx = c[0] - g.end_cell[t][0], dy = c[1] - g.end_cell[t][1];
        if (dx * dx + dy * dy > (g.arena == PVP_WHERE_BASE ? 36 : 16)) arrived = 0;   /* base: two walkers share one end cell */
    }
    /* E4 / E8 (owner 2026-10-20: "nobody starts a fight alone"): round 1 is the teams' preparation, with no time limit: the
       fight begins only after a proposal every team player agreed to, at the end of its countdown (pvp_core.c: PROPOSE /
       AGREE set countdown_at). Later rounds start again from the arena snapshot (teams at their ends): the 3 s countdown
       follows at once, as before. A unit not at its end is sent there again every 2 s (a settler job took it off its walk),
       except in the preparation once its player gave it an order (E7: the player's orders win; ord_game.c ord_apply). */
    int prep = g.round <= 1;
    /* every 2 s: a unit not at its end and not ordered by its player walks on. Dungeon too (Red only), v0.1.21 F5: a far Red
       end (the farthest portal of a big floor, 56-62 cells) was not reached before round 1's fight; the arena snapshot then
       kept Red mid-walk with no order, so every later round waited the 60 s placing time out */
    if (!arrived && g.ticks % 120 == 0) {
        for (int i = 0; i < g.nunits; i++) {
            int t = g.units[i].team; int32_t c[2];
            if ((t != PVP_TEAM_RED && !(g.arena == PVP_WHERE_BASE && t == PVP_TEAM_BLUE)) || g.units[i].out || (prep && g.units[i].ordered) || !unit_cell(map, g.units[i].guid, c)) continue;
            int dx = c[0] - g.end_cell[t][0], dy = c[1] - g.end_cell[t][1]; if (dx * dx + dy * dy <= (g.arena == PVP_WHERE_BASE ? 36 : 16)) continue;
            Guid gg; memcpy(&gg, g.units[i].guid, 16); rig_order_move(gg, g.end_cell[t][0], g.end_cell[t][1]); n_walk_resend++;
        }
    }
    if (prep ? g.countdown_at != 0 : ((any && arrived && g.ticks >= 60) || g.ticks >= PLACE_MAX_TICKS || g.countdown_at)) {
        if (!pvp_place_done(&g)) return;            /* M4: 3 s countdown first (units hold where they are) */
        if (g.arena == PVP_WHERE_BASE) { Obj h = combat_of(map); if (h) pair_all(h, 1); }   /* enemies from now on */
        rig_log("pvp: fight begins at match tick %u (%s)", g.ticks, arrived ? (g.arena == PVP_WHERE_BASE ? "teams at their ends" : "Red at its end") : "placing time out");
        fight_kick(map);
    }
}

/* ---------------------------------------------------------------- M6 Capture the Flag (rules in pvp_core.c) */
#define VA_SPAWN_BUILDING 0xC93308   /* EntityLifecycleHelper.SpawnBuilding(map, key, Vector2Int, astarApply, rotation, faction, Nullable<Guid> summoner) */
typedef struct { uint8_t has, pad[3]; uint8_t g[16]; } NGuid;   /* Nullable<Guid>, 20 bytes: passed by reference like the game does */
static int banners = 1, n_banner;    /* test switch (pvp_cfg banners): spawn the stock banner stands at the homes */
/* the stock SpawnBuildingRequested event (key, grid, rotation) through the EventQueue: its handler spawns the building AND
   asks the view for it (EntityLifecycleHelper.SpawnBuilding alone made the data only: nothing on screen, 2026-10-14) */
#define TI_SPAWN_BUILDING_REQ 0x6654980
#define VA_SPAWN_BUILDING_REQ 0xD62C38   /* SpawnBuildingRequested..ctor(string key, Vector2Int grid, BuildingRotation rotate) */
#define VA_EQ_ADD_EVENT       0xCCFAD4   /* EventQueue.AddEvent (trn_game.c) */
static void spawn_banner(int map, const char *key, const int32_t c[2]) {
    (void)map;
    Obj eq = mf ? AT(mf, 0x48, Obj) : NULL;
    Obj ev = obj_new(TI_SPAWN_BUILDING_REQ);
    if (!eq || !ev) { rig_log("pvp: banner %s: no event queue", key); return; }
    uint64_t pos = (uint32_t)c[0] | ((uint64_t)(uint32_t)c[1] << 32);
    F(VA_SPAWN_BUILDING_REQ, void (*)(Obj, Obj, uint64_t, int32_t, void *))(ev, F(0x6F9738, Obj (*)(const char *))(key), pos, 0, NULL);
    F(VA_EQ_ADD_EVENT, void (*)(Obj, Obj, void *))(eq, ev, NULL);
    n_banner++;
    rig_log("pvp: banner %s at (%d,%d): SpawnBuildingRequested queued", key, c[0], c[1]);
}
static int n_markers;
/* (3) dropped flag: a banner stand of the flag's colour stands on the ground cell while it lies there (stock building through
   the stock events, inside the sim tick: same entity on every peer); it goes when the flag is taken or returned. The cell
   is read from the PvP state at the start of the tick, so a peer that joined mid-match removes the same one. */
#define TI_DESPAWN_BUILDING_REQ 0x664C8D0
#define VA_DESPAWN_BUILDING_REQ 0xD62CB4  /* DespawnBuildingRequested..ctor(MapType, BuildingEntity) */
static void marker_remove(int map, const int32_t cell[2]) {
    Obj c0 = cdc(); Obj edc = c0 ? AT(c0, 0x20, Obj) : NULL; Obj bc = edc ? dict_int_get(AT(edc, 0x20, Obj), map) : NULL; Obj bd = bc ? AT(bc, 0x18, Obj) : NULL;
    Obj ent = bd ? AT(bd, 0x18, Obj) : NULL; int n = bd ? AT(bd, 0x20, int32_t) : 0;
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20; if (*(int32_t *)e < 0) continue;
        Obj b = *(Obj *)(e + 0x18); if (!b || !key_has(AT(b, 0x38, Obj), "BannerStand")) continue;
        int32_t cc[2]; if (!unit_cell(map, (const uint8_t *)(e + 8), cc) || cc[0] != cell[0] || cc[1] != cell[1]) continue;
        Obj eq = mf ? AT(mf, 0x48, Obj) : NULL, ev = obj_new(TI_DESPAWN_BUILDING_REQ);
        if (!eq || !ev) return;
        F(VA_DESPAWN_BUILDING_REQ, void (*)(Obj, int32_t, Obj, void *))(ev, map, b, NULL);
        F(VA_EQ_ADD_EVENT, void (*)(Obj, Obj, void *))(eq, ev, NULL);
        n_markers--; rig_log("pvp: dropped-flag marker at (%d,%d) removed", cell[0], cell[1]);
        return;
    }
}
static uint32_t n_ctf_ev[4], ctf_seq; static int ctf_last_kind, ctf_last_team, ctf_last_unit;
static void ctf_tick(int map) {
    int32_t cell[PVP_MAX_UNITS][2]; uint8_t kn[PVP_MAX_UNITS];
    for (int i = 0; i < g.nunits; i++) kn[i] = (uint8_t)unit_cell(map, g.units[i].guid, cell[i]);
    if (!g.home[1][0] && !g.home[1][1] && !g.home[2][0] && !g.home[2][1]) {    /* first fight: home = the cell of the team's first unit (guid order): a floor cell */
        int32_t h[3][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 } }; int got[3] = { 0, 0, 0 };
        for (int i = 0; i < g.nunits; i++) { int t = g.units[i].team; if (kn[i] && !got[t]) { h[t][0] = cell[i][0]; h[t][1] = cell[i][1]; got[t] = 1; } }
        pvp_ctf_set_homes(&g, h[1], h[2]);
        rig_log("pvp: ctf homes blue (%d,%d) red (%d,%d)", h[1][0], h[1][1], h[2][0], h[2][1]);
        if (banners) {
            rig_det_sim_scope(1);
            spawn_banner(map, "BUILDING_BlueBannerStand", h[1]); spawn_banner(map, "BUILDING_BannerStand", h[2]);
            rig_det_sim_scope(0);
        }
    }
    int was_dropped[3] = { 0, 0, 0 }; int32_t was_cell[3][2];
    for (int t = 1; t <= 2; t++) { was_dropped[t] = g.flag[t].state == PVP_FLAG_DROPPED; was_cell[t][0] = g.flag[t].cell[0]; was_cell[t][1] = g.flag[t].cell[1]; }
    int ft = 0, ev = pvp_ctf_step(&g, (const int32_t (*)[2])cell, kn, &ft);
    for (int t = 1; t <= 2; t++) {
        int now_dropped = g.flag[t].state == PVP_FLAG_DROPPED;
        if (now_dropped && !was_dropped[t]) {
            rig_det_sim_scope(1); spawn_banner(map, t == PVP_TEAM_BLUE ? "BUILDING_BlueBannerStand" : "BUILDING_BannerStand", g.flag[t].cell); rig_det_sim_scope(0);
            n_markers++;
        } else if (was_dropped[t] && !now_dropped) { rig_det_sim_scope(1); marker_remove(map, was_cell[t]); rig_det_sim_scope(0); }
    }
    static const char *nm[4] = { "taken", "dropped", "returned", "CAPTURED" };
    for (int k = 0; k < 4; k++) if (ev & (1 << k)) {
        n_ctf_ev[k]++; ctf_last_kind = k; ctf_last_team = ft; ctf_last_unit = g.flag[ft].carrier; ctf_seq++;
        rig_log("pvp: ctf %s flag of team %d at match tick %u (carrier %d, cell %d,%d; score %d-%d)", nm[k], ft, g.ticks,
                g.flag[ft].carrier == PVP_NO_UNIT ? -1 : g.flag[ft].carrier, g.flag[ft].cell[0], g.flag[ft].cell[1], g.score[1], g.score[2]);
    }
}

/* ---------------------------------------------------------------- command */
static int owned_now(dsnet_client *c, pvp_owned *out, int max) {
    int n = 0;
    for (int p = 0; p < PVP_MAX_PLAYERS && n < max; p++) {
        uint8_t gl[64][16];
        int k = dsnet_owned_list(c, p, gl, 64); if (k > 64) k = 64;
        for (int i = 0; i < k && n < max; i++) { memcpy(out[n].guid, gl[i], 16); out[n].player = p; n++; }
    }
    return n;
}
static int pvp_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)guid;
    static pvp_owned own[256];
    int no = owned_now(mp_client(s), own, 256);
    int was = pvp_active(&g);
    Obj h = combat_of(cur_map());
    if (was && n >= 2 && a[1] == PVP_OP_END && player == 0 && h) pair_all(h, 0);    /* pairs out before the table goes */
    int rc = pvp_apply(&g, player, a, n, own, no);
    last_rc = rc;
    if (rc) n_rejected++;
    if (!was && hostile_now() && h) pair_all(h, 1);                  /* BASE: when the fight begins (placing_step) */
    if (!rc && n >= 2 && a[1] == PVP_OP_ARENA) arena_go(s);
    if (!rc && n >= 2 && a[1] == PVP_OP_START && g.arena == PVP_WHERE_DUNGEON) { find_ends(cur_map()); rig_log("pvp: %d Red units sent to their end", send_to_ends(cur_map())); }
    if (!rc && n >= 2 && a[1] == PVP_OP_START && g.arena == PVP_WHERE_BASE) { find_base_ends(cur_map()); rig_log("pvp: %d units sent to their team's end of the base", send_both_to_ends()); }
    rig_log("pvp: command op %d from player %d at step %u: %s (phase %d, %d units, map %d handler %p)", n >= 2 ? a[1] : -1, player,
            mp_cur_step(s), pvp_err_text(rc), g.phase, g.nunits, cur_map(), h);
    return 0;                                     /* consumed on every peer (an error is a rule result, not a bad order) */
}
/* Test tool (repeat runs): the world stops at the same world tick on every peer, whatever the wall clock did. Set by a
   stamped host order, read by mp_game.c g_tick (weak pvp_freeze_now). Not saved: only used before a resync. */
static int64_t freeze_at = -1;
static int freeze_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    if (player != 0 || n != 8) return 0;
    memcpy(&freeze_at, a, 8);
    rig_log("pvp: freeze at world tick %lld (now %lld)", (long long)freeze_at, (long long)tick());
    return 0;
}
int pvp_freeze_now(void) { return (freeze_at >= 0 && tick() >= freeze_at) || pvp_planning(&g) || pvp_ending(&g) || pvp_vote_open(&g); }   /* + M5 planning pause, E9 round result screen, E4 fight vote */
static mp_session *reg_sess;
static void session_check(void) {
    mp_session *s = sess();
    if (s == reg_sess) return;
    reg_sess = s; pvp_init(&g); freeze_at = -1;
    if (s) { mp_set_order_handler(s, PVP_ORDER_TYPE, pvp_order); mp_set_order_handler(s, PVP_FREEZE_TYPE, freeze_order); }
}
static int send_cmd(const uint8_t *p, size_t n) {
    session_check();
    mp_session *s = sess(); static const uint8_t zero[16];
    return s ? mp_order(s, PVP_ORDER_TYPE, zero, p, (uint16_t)n) : -1;
}

/* ---------------------------------------------------------------- exported to mp_game.c (weak there) */
/* once per world tick, inside the sim: count the tick, then mark PvP units that are downed (task 1000) or dead */
void pvp_sim_tick(void) {
    pvp_on_tick(&g);
    if (!pvp_active(&g)) return;
    int map = cur_map();
    if (g.placing) { rig_det_sim_scope(1); placing_step(map); rig_det_sim_scope(0); }
    for (int i = 0; i < g.nunits; i++) {
        if (g.units[i].out) continue;
        Guid gg; memcpy(&gg, g.units[i].guid, 16);
        Obj u = map >= 0 ? find_unit(map, gg) : NULL;
        if (u && (unit_dead(u) || unit_task(u) == 1000 || !unit_conscious(u))) {
            pvp_mark_out(&g, g.units[i].guid);
            rig_log("pvp: unit %d (team %d) out at match tick %u", i, g.units[i].team, g.ticks);
            /* an out (downed) unit is no target any more: off every enemy's hostile list and theirs off its own,
               same calls as a death (stock keeps a downed player unit in the lists: the enemies kept hitting the body) */
            Obj h = fix_out ? combat_of(map) : NULL;
            if (h) {
                Obj mine = hed_of(h, g.units[i].guid);
                for (int j = 0; j < g.nunits; j++) {
                    if (g.units[j].team == g.units[i].team) continue;
                    Obj d = hed_of(h, g.units[j].guid);
                    if (d) remove_hostile(d, g.units[i].guid);
                    if (mine) remove_hostile(mine, g.units[j].guid);
                }
            }
        }
    }
    /* a unit left idle (its target went out, nobody near) goes for the closest enemy again: every 2 s of fight */
    if (pvp_fighting(&g) && g.fight_start && (g.ticks - (g.fight_start - 1)) % 120 == 0) { diag(map); rig_det_sim_scope(1); fight_kick_idle(map, 1); rig_det_sim_scope(0); }
    if (pvp_fighting(&g) && g.mode == PVP_MODE_CTF) ctf_tick(map);
    if (pvp_fighting(&g)) {                       /* M3 round rule: HP left per team (units not out), per mille of full HP */
        double cur[3] = { 0, 0, 0 }, full[3] = { 0, 0, 0 };
        for (int i = 0; i < g.nunits; i++) {
            Guid gg; memcpy(&gg, g.units[i].guid, 16);
            Obj u = map >= 0 ? find_unit(map, gg) : NULL; if (!u) continue;
            float hp = unit_stat(u, 9), mx = unit_stat(u, 22);
            full[g.units[i].team] += mx > 0 ? mx : 0;
            if (!g.units[i].out && hp > 0) cur[g.units[i].team] += hp;
        }
        uint32_t pm[3] = { 0, 0, 0 };
        for (int t = 1; t <= 2; t++) pm[t] = full[t] > 0 ? (uint32_t)(cur[t] * 1000.0 / full[t]) : 0;
        int e = pvp_round_check(&g, pm);
        if (e) rig_log("pvp: round %d over at match tick %u: winner team %d (score %d-%d, draws %d, hp %u/%u)%s", g.round, g.ticks,
                       g.last_winner, g.score[1], g.score[2], g.draws, pm[1], pm[2], e == PVP_RESET_MATCH ? ": MATCH OVER" : "");
    }
}
uint64_t pvp_fp_mix(void) { return pvp_untouched() ? 0 : pvp_hash(&g); }            /* 0 = the world fingerprint unchanged */
size_t pvp_block_size(void) { return pvp_untouched() ? 0 : pvp_save_size(&g); }    /* 0 = no PVPB block (pvp_block_set(NULL) = pvp_init) */
size_t pvp_block_get(uint8_t *out, size_t cap) { return pvp_save(&g, out, cap); }
void pvp_block_set(const uint8_t *p, size_t n) {
    session_check();
    if (!p) { pvp_init(&g); rig_log("pvp: save has no PvP block: state reset"); return; }
    int rc = pvp_load(&g, p, n);
    rig_log("pvp: state from the save: rc %d, phase %d, %d units, tick %u", rc, g.phase, g.nunits, g.ticks);
}

/* ---------------------------------------------------------------- M3 snapshots and reloads (host keeps them in memory)
   WORLD: taken by the host right before it sends ARENA (or START without an arena): the match end reloads it, so the world
   is the same as before the match. ARENA: taken by the host once the first fight began (teams at their ends): every
   round reset reloads it. The reload is the resync path: each guest asks for a join point when it sees `resetting`; the
   host's join-point save is the snapshot (mp_game.c g_save_override) and the PvP state moves on there (next round / off).
   Every peer then loads the same bytes at the same step. */
int mp_game_snapshot(uint8_t **det, size_t *dl, uint8_t **json, size_t *jn) __attribute__((weak_import));
int dsnet_request_resync(dsnet_client *c);
int trn_flow_busy(void) __attribute__((weak_import));
int rig_det_mp_ready(void);
typedef struct { uint8_t *det, *json; size_t dl, jn; int tick_at; } snap_t;
static snap_t snap_world, snap_arena;
static int arena_snap_match = -1, resync_key = -1, n_resync, n_override;
static void snap_free(snap_t *x) { free(x->det); free(x->json); memset(x, 0, sizeof *x); }
static int world_locked;             /* own units (pvp_units.c): the world snapshot was taken before the units came in */
static int snap_take(snap_t *x, const char *what) {
    if (x == &snap_world && world_locked) { rig_log("pvp: world snapshot kept from before the brought units"); return 0; }
    snap_free(x);
    if (!mp_game_snapshot || mp_game_snapshot(&x->det, &x->dl, &x->json, &x->jn)) { rig_log("pvp: %s snapshot FAILED", what); return -1; }
    x->tick_at = (int)tick();
    rig_log("pvp: %s snapshot at world tick %d (%zu bytes)", what, x->tick_at, x->jn);
    return 0;
}
static int is_host(void) { mp_session *s = sess(); return s && dsnet_is_host(mp_client(s)); }
void pvp_set_hold(int on) { g.hold = (uint8_t)(on != 0); }   /* pvp_units.c, inside DSNET_FE_START (every peer, same step) */
int pvp_snap_world_lock(void) {                         /* E2: a second units start in the same match setup keeps the first world */
    if (world_locked && snap_world.json) { rig_log("pvp: world snapshot kept (units brought again)"); return 0; }
    world_locked = 0; int rc = snap_take(&snap_world, "world"); if (!rc) world_locked = 1; return rc;
}
void pvp_units_after_match(void);
void pvp_units_frame(void);
char *pvp_units_exec(const char *cmd, const char *line);
char *pvp_mods_exec(const char *cmd, const char *line);
int  pvp_mods_gate(int vanilla, char *why, size_t n);
int pvp_save_override(const uint8_t **det, size_t *dl, const uint8_t **json, size_t *jn) {
    if (!is_host() || !pvp_active(&g) || !g.resetting) return 0;
    snap_t *x = g.resetting == PVP_RESET_MATCH ? &snap_world : &snap_arena;
    if (!x->json) { rig_log("pvp: no %s snapshot for the reset", x == &snap_world ? "world" : "arena"); return 0; }
    pvp_state nx;
    if (g.resetting == PVP_RESET_MATCH) { pvp_after_match(&g, &nx); world_locked = 0; pvp_units_after_match(); } else pvp_next_round(&g, &nx);
    rig_log("pvp: reset (%s): sending the %s snapshot; next state round %d phase %d", g.resetting == PVP_RESET_MATCH ? "match over" : "round over",
            x == &snap_world ? "world" : "arena", nx.round, nx.phase);
    g = nx; n_override++;
    *det = x->det; *dl = x->dl; *json = x->json; *jn = x->jn;
    return 1;
}
/* no player pause in a PvP match (owner: host setting none / planning): dsnet pause mode NONE from START to the end */
static int rules_saved = -1;
int pvp_pause_mode_saved(void) { return rules_saved; }    /* mpmenu.c rules_sync: the host's own mode while a match overrides it */
static void pause_rule_frame(void) {
    mp_session *s = sess(); if (!s || !is_host()) return;
    dsnet_client *c = mp_client(s);
    if (pvp_active(&g) && rules_saved < 0) {
        dsnet_rules r; dsnet_rules_get(c, &r); rules_saved = r.pause_mode;
        r.pause_mode = DSNET_PAUSE_NONE; rig_log("pvp: player pauses off for the match (rc %d)", dsnet_set_rules(c, &r));
    } else if (!pvp_active(&g) && g.phase == PVP_OFF && rules_saved >= 0) {
        dsnet_rules r; dsnet_rules_get(c, &r); r.pause_mode = rules_saved; rules_saved = -1;
        rig_log("pvp: player pauses back to mode %d (rc %d)", r.pause_mode, dsnet_set_rules(c, &r));
    }
}
static int send_cmd(const uint8_t *p, size_t n);
static uint32_t plan_seen; static double plan_t0;
#define ROUND_SCREEN_S 5
static uint32_t end_seen; static double end_t0;

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
/* CTF: the carrier's world name shows the flag it carries (ui_game.c hud_apply, weak); pvp_tag_sig changes when a flag moves
   from home / carrier / ground, so the names re-tag then */
int pvp_unit_tag(const uint8_t gd[16], char *out, int cap) {
    out[0] = 0;
    if (!pvp_active(&g) || g.mode != PVP_MODE_CTF) return 0;
    for (int t = 1; t <= 2; t++)
        if (g.flag[t].state == PVP_FLAG_CARRIED && g.flag[t].carrier < g.nunits && !memcmp(g.units[g.flag[t].carrier].guid, gd, 16))
            return snprintf(out, (size_t)cap, t == PVP_TEAM_BLUE ? "  <color=#7FA8FF>[Blue flag]</color>" : "  <color=#FF7F7F>[Red flag]</color>");
    return 0;
}
uint64_t pvp_tag_sig(void) {
    uint64_t h = 1469598103934665603ULL;
    for (int t = 1; t <= 2; t++) { uint8_t b[2] = { g.flag[t].state, g.flag[t].carrier }; h = fnv(h, b, 2); }
    return h ^ g.phase;
}
/* test camera: centre the view on a cell (CampaignSettingContainer.CameraSetting position, its z kept) */
typedef struct { float x, y, z; } V3f;
static V3f cam_last; static float cam_size_last;
static void cam_to(float x, float y, float size) {
    Obj cs0 = mf ? AT(mf, 0xD8, Obj) : NULL, cs = cs0 ? AT(cs0, 0x18, Obj) : NULL; if (!cs) return;
    V3f cur = F(0xD23584, V3f (*)(Obj, void *))(cs, NULL), np = { x, y, cur.z };
    cam_last = cur; cam_size_last = F(0xD23590, float (*)(Obj, void *))(cs, NULL);
    if (x > -1e8f) F(0xD22C54, void (*)(Obj, V3f, void *))(cs, np, NULL);
    if (size > 0) F(0xD22DB0, void (*)(Obj, float, void *))(cs, size, NULL);
    /* the data alone does not move the view: also Camera.main itself (position + orthographic size) */
    Obj cam = F(0x5083208, Obj (*)(void *))(NULL);
    Obj ct = cam ? F(0x50ED864, Obj (*)(Obj, void *))(cam, NULL) : NULL;                       /* Component.transform */
    if (ct && x > -1e8f) { V3f p = F(0x5109A08, V3f (*)(Obj, void *))(ct, NULL); p.x = x; p.y = y; F(0x5109B04, void (*)(Obj, V3f, void *))(ct, p, NULL); }
    if (cam && size > 0) F(0x50805E8, void (*)(Obj, float, void *))(cam, size, NULL);          /* Camera.orthographicSize */
}

/* ---------------------------------------------------------------- E5: PvP vision (mp_fog.c, ui_game.c; view only) */
static int my_team_now(void);
int pvp_rule_on(const char *id);
int pvp_vision_my_team(void) {                           /* my team for the fog, 0 = none (no match, I play no team, or rule enemy_vision on) */
    return pvp_rule_on("enemy_vision") ? 0 : my_team_now();
}
static int my_team_now(void) {                           /* my team while a match runs, 0 = none */
    if (!pvp_active(&g)) return 0;
    mp_session *s = sess(); dsnet_client *c = s ? mp_client(s) : NULL; int me = c ? dsnet_player_id(c) : -1;
    return me >= 0 && me < PVP_MAX_PLAYERS ? g.team_of[me] : 0;
}
int pvp_vision_team_of(const uint8_t gd[16]) { return pvp_active(&g) ? pvp_team_of_unit(&g, gd) : 0; }
/* PvP rule settings by id (lane pvprules, owner 2026-10-20): 1 on, 0 off, -1 unknown. Weak default here (all unknown: the
   defaults below, every enemy_* hidden); the rules lane's strong definition in the same image replaces it at link. Ids:
   enemy_vision enemy_windows enemy_portraits enemy_hpbars enemy_status enemy_viewrange (1 = shown). Display only. */
__attribute__((weak)) int pvp_rule_get(const char *id) { (void)id; return -1; }
int pvp_rule_on(const char *id) { return pvp_rule_get(id) == 1; }
int pvp_vision_enemy_range(void) { return pvp_active(&g) && pvp_rule_on("enemy_viewrange"); }
int pvp_vision_units(uint8_t (*gl)[16], uint8_t *team, int max) {
    int n = 0; if (!pvp_active(&g)) return 0;
    for (int i = 0; i < g.nunits && n < max; i++) { memcpy(gl[n], g.units[i].guid, 16); team[n] = g.units[i].team; n++; }
    return n;
}
/* a player slot on the other team than mine in a match (ui_game.c: no camera view, ping or follow of an opponent) */
int pvp_peer_hidden(int slot) {
    int mt = my_team_now();
    return mt && slot >= 0 && slot < PVP_MAX_PLAYERS && g.team_of[slot] && g.team_of[slot] != mt;
}

/* E5b / E7b (owner 2026-10-20): an enemy PvP unit is a Player unit to the game, so a click selected it and opened the full
   unit window (stats, inventory, skills), and its portrait, health bars and statuses showed. View only, decided here:
   - after SelectProcessor.Select / Toggle (pvp_select_after): an enemy unit is the EnemyUnit group (3001), like a monster:
     a click opens only the monster inspect view, no selection of it for orders (rule enemy_windows on = stock);
   - ui_game.c pvp_view_pass asks pvp_view_hide each frame: portrait (enemy_portraits), health bars (enemy_hpbars),
     statuses (enemy_status). */
static int enemy_unit(const uint8_t gd[16]) {
    int mt = my_team_now(), ut = mt ? pvp_team_of_unit(&g, gd) : 0;
    return ut && ut != mt;
}
int pvp_view_hide(const uint8_t gd[16], int what) {
    if (!enemy_unit(gd)) return 0;
    return what == 1 ? !pvp_rule_on("enemy_portraits") : what == 2 ? !pvp_rule_on("enemy_hpbars") : what == 3 ? !pvp_rule_on("enemy_status") : 0;
}
/* ord_game.c ord_apply (E7): units that got their player's order (inside the step, every peer) */
void pvp_units_ordered(const uint8_t (*u)[16], int n) {
    if (!pvp_active(&g) || !g.placing) return;
    for (int i = 0; i < n; i++) pvp_mark_ordered(&g, u[i]);
}
/* ord_game.c (E7b): an interaction other than an attack (InteractionType 2) by a PvP unit on a unit of the other team
   (rescue, first aid, carry, give ...) is refused. Sim state only (the PvP table): the same answer on every peer. */
static uint32_t n_act_refused;
int pvp_act_refused(int type, const uint8_t actor[16], const uint8_t target[16]) {
    if (!pvp_active(&g) || type == 2) return 0;
    int ta = pvp_team_of_unit(&g, actor), tt = pvp_team_of_unit(&g, target);
    if (!ta || !tt || ta == tt) return 0;
    n_act_refused++;
    return 1;
}
static uint32_t n_selgroup_enemy;
/* SelectProcessor.GetSelectGroup is inlined on Windows (inline audit), so the rule runs after SelectProcessor.Select /
   Toggle (ui_game.c, hooked there): SelectDataContainer (SelectProcessor +0x10): group +0x10, List<IEntity> +0x18. A
   selection of my player units (3003) that holds enemy PvP units: only enemies = the enemy group (3001: the game shows
   the monster inspect view, no command), else the enemies leave the list (rule enemy_windows on = stock). View only. */
void pvp_select_after(Obj sd) {
    if (!sd || !my_team_now() || pvp_rule_on("enemy_windows") || AT(sd, 0x10, int32_t) != 3003) return;
    Obj l = AT(sd, 0x18, Obj); Obj items = l ? AT(l, 0x10, Obj) : NULL; int n = l ? AT(l, 0x18, int32_t) : 0;
    if (!items || n <= 0 || n > 256) return;
    int en = 0;
    for (int i = 0; i < n; i++) { Obj e = AT(items, 0x20 + 8 * i, Obj); if (!e) continue; G16 ge = gget(e); if (enemy_unit((const uint8_t *)&ge)) en++; }
    if (!en) return;
    n_selgroup_enemy++;
    if (en == n) { AT(sd, 0x10, int32_t) = 3001; rig_log("pvp: a click selected %d enemy unit(s): enemy view only (group 3001)", n); return; }
    int j = 0;
    for (int i = 0; i < n; i++) { Obj e = AT(items, 0x20 + 8 * i, Obj); G16 ge = gget(e); if (e && !enemy_unit((const uint8_t *)&ge)) AT(items, 0x20 + 8 * j++, Obj) = e; }
    for (int i = j; i < n; i++) AT(items, 0x20 + 8 * i, Obj) = NULL;
    AT(l, 0x18, int32_t) = j; AT(l, 0x1C, int32_t)++;
    rig_log("pvp: %d enemy unit(s) left out of a selection of %d", en, n);
}

/* ---------------------------------------------------------------- M4: API for the PvP windows (pvp_ui.inc in mpmenu.c) */
const pvp_state *pvp_ui_state(void) { return &g; }
int pvp_ui_is_host(void) { return is_host(); }
/* HP left per team (units not out), per mille of the team's full HP, from the live units (view only) */
void pvp_ui_team_hp(uint32_t pm[3]) {
    double cur[3] = { 0, 0, 0 }, full[3] = { 0, 0, 0 }; int map = cur_map();
    for (int i = 0; i < g.nunits; i++) {
        Guid gg; memcpy(&gg, g.units[i].guid, 16); Obj u = map >= 0 ? find_unit(map, gg) : NULL; if (!u) continue;
        float hp = unit_stat(u, 9), mx = unit_stat(u, 22);
        full[g.units[i].team] += mx > 0 ? mx : 0;
        if (!g.units[i].out && hp > 0) cur[g.units[i].team] += hp;
    }
    pm[0] = 0; for (int t = 1; t <= 2; t++) pm[t] = full[t] > 0 ? (uint32_t)(cur[t] * 1000.0 / full[t]) : 0;
}
/* host: SETUP (teams + rules) then ARENA; START follows by itself once every peer is on the arena floor */
static int auto_start_match = -1;
int pvp_units_received(void);
int pvp_units_built(void);
/* own units: the match is set up once the world with the brought units is in (pvp_units.c) */
static struct { int on, mode, region, floor, seed, built0; uint8_t teams[PVP_MAX_PLAYERS]; pvp_rules r; int64_t ok_since; } after_units;
int pvp_ui_start_match(int mode, const uint8_t teams[PVP_MAX_PLAYERS], const pvp_rules *r, int region, int floor, int seed);
/* E2 (owner v0.1.20: "bring units from inside a PvP world"): in the preparation (round 1, no vote, no countdown) a player
   may Bring Units again (HUD button). The host then sets the match up again with the new roster: END, the units start
   (the world snapshot and owner tables of before the first start are kept), then SETUP / BASE / START as at first. */
int pvp_units_gen(void);
static int match_gen = -1, rebring_step;
static struct { int mode, region, floor, seed; uint8_t teams[PVP_MAX_PLAYERS]; pvp_rules r; } last_setup;
static void rebring_frame(void) {
    if (!is_host()) return;
    if (rebring_step == 1 && g.phase == PVP_OFF) {
        rebring_step = 0;
        char *res = pvp_units_exec("pvp_units_start", "{}"); int ok = res && strstr(res, "\"ok\":true");
        rig_log("pvp: units brought again in the preparation: units start %s", res ? res : "?"); free(res);
        if (ok) { after_units.on = 1; after_units.mode = last_setup.mode; after_units.region = last_setup.region; after_units.floor = last_setup.floor; after_units.seed = last_setup.seed;
                  memcpy(after_units.teams, last_setup.teams, PVP_MAX_PLAYERS); after_units.r = last_setup.r; after_units.ok_since = -1; after_units.built0 = pvp_units_built(); }
        return;
    }
    if (rebring_step || after_units.on || !world_locked || match_gen < 0 || pvp_units_gen() == match_gen) return;
    if (!pvp_active(&g) || !g.placing || g.round > 1 || g.countdown_at || g.vote_open || g.resetting || g.ending) return;
    uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, PVP_OP_END);
    int rc = send_cmd(b, n); match_gen = -1; rebring_step = rc ? 0 : 1;
    rig_log("pvp: new units brought in the preparation: the match is set up again (END rc %d)", rc);
}
static void after_units_frame(void) {
    if (!after_units.on || !is_host()) return;
    mp_session *s = sess(); dsnet_client *c = s ? mp_client(s) : NULL;
    int ready = pvp_units_built() > after_units.built0 && cur_map() == 0 && !busy() && !(trn_flow_busy && trn_flow_busy()) && rig_det_mp_ready() && c && !dsnet_busy_mask(c);
    if (!ready) { after_units.ok_since = -1; return; }
    if (after_units.ok_since < 0) { after_units.ok_since = tick(); return; }
    if (tick() - after_units.ok_since < 60) return;
    after_units.on = 0;
    rig_log("pvp: the brought units are in on every copy: setting up the match");
    pvp_ui_start_match(after_units.mode, after_units.teams, &after_units.r, after_units.region, after_units.floor, after_units.seed);
}
int pvp_ui_start_match(int mode, const uint8_t teams[PVP_MAX_PLAYERS], const pvp_rules *r, int region, int floor, int seed) {
    if (!is_host()) return -1;
    if (!world_locked) {                                                /* Vanilla check (pvp_mods.c): before anything moves */
        char why[700]; int g_rc = pvp_mods_gate(r->vanilla, why, sizeof why);
        if (g_rc) { rig_log("pvp: Start Match refused (%s): %s", g_rc == 2 ? "waiting" : "mods", why); return -4; }
    }
    if (pvp_units_received() > 0 && !after_units.on && !world_locked) {    /* first bring the units in, then this again */
        char *res = pvp_units_exec("pvp_units_start", "{}"); int ok = res && strstr(res, "\"ok\":true");
        rig_log("pvp: Start Match with brought units: %s", res ? res : "?"); free(res);
        if (!ok) return -3;
        after_units.on = 1; after_units.mode = mode; after_units.region = region; after_units.floor = floor; after_units.seed = seed;
        memcpy(after_units.teams, teams, PVP_MAX_PLAYERS); after_units.r = *r; after_units.ok_since = -1; after_units.built0 = pvp_units_built();
        return 0;
    }
    uint8_t b[64]; size_t n = pvp_cmd_setup(b, sizeof b, mode, teams, r);
    int rc = send_cmd(b, n);
    if (rc) return rc;
    last_setup.mode = mode; last_setup.region = region; last_setup.floor = floor; last_setup.seed = seed; memcpy(last_setup.teams, teams, PVP_MAX_PLAYERS); last_setup.r = *r;
    match_gen = world_locked ? pvp_units_gen() : -1;
    if (snap_take(&snap_world, "world")) return -2;
    n = region < 0 ? pvp_cmd_simple(b, sizeof b, PVP_OP_BASE) : pvp_cmd_arena(b, sizeof b, region, floor, seed);   /* region < 0: here, on the settlement */
    rc = send_cmd(b, n);
    if (!rc) auto_start_match = (int)g.matches;
    rig_log("pvp: match set up from the window: mode %d, best of %d, %d s, pause %d, vanilla check %d, floor %d seed %d: rc %d", mode, r->best_of, r->round_secs, r->pause_rule, r->vanilla, floor, seed, rc);
    return rc;
}
int pvp_ui_ready(void) { uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, PVP_OP_READY); return send_cmd(b, n); }
/* E4: the fight vote. Any team player: PROPOSE (the world stands still while it is open), AGREE, NOT YET; CANCEL stops an
   agreed countdown. Stamped commands: the same answer at the same step on every peer (pvp_core.c). */
int pvp_ui_vote(int op) {
    if (op != PVP_OP_PROPOSE && op != PVP_OP_AGREE && op != PVP_OP_NOTYET && op != PVP_OP_CANCEL) return -1;
    uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, op);
    int rc = send_cmd(b, n); rig_log("pvp: fight vote op %d sent: rc %d", op, rc); return rc;
}
int pvp_ui_my_slot(void) { mp_session *s = sess(); dsnet_client *c = s ? mp_client(s) : NULL; return c ? dsnet_player_id(c) : -1; }
int pvp_ui_player_name(int slot, char *out, int cap) {
    mp_session *s = sess(); dsnet_client *c = s ? mp_client(s) : NULL; const char *nm = c ? dsnet_player_name(c, slot) : NULL;
    snprintf(out, (size_t)cap, "%s", nm && nm[0] ? nm : slot == 0 ? TR("Host") : TR("Player")); dsi_utf8_fix_end(out); return 0;
}
int pvp_ui_unit_name(int i, char *out, int cap) {        /* the unit's name (DASText of its info) for messages */
    out[0] = 0; if (i < 0 || i >= g.nunits) return -1;
    Guid gg; memcpy(&gg, g.units[i].guid, 16); Obj u = find_unit(cur_map(), gg); if (!u) return -1;
    Obj nm = F(0xFA47A0, Obj (*)(Obj, void *))(u, NULL);      /* UnitEntity.get_Name (list_units) */
    if (!nm) return -1;
    void utf16_to_utf8(Obj s, char *out, size_t cap);                   /* multiplayer (mpmenu_kit.h): whole characters */
    utf16_to_utf8(nm, out, cap > 0 ? (size_t)cap : 0); return 0;
}
static void center(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void center(const char *fmt, ...) {
    static void (*cm)(const char *); static int looked;
    if (!looked) { looked = 1; cm = (void (*)(const char *))dlsym(RTLD_DEFAULT, "ui_center_message"); }
    char b[400]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); dsi_utf8_fix_end(b);
    if (cm) cm(b); rig_log("pvp: message \"%s\"", b);
}
/* local messages from the shared state (every peer shows the same ones at the same step) */
static void messages_frame(void) {
    static uint32_t seen_match, seen_round, seen_fight, seen_plans, seen_ctf, seen_prep; static int seen_reset, seen_cd = -1;
    if (pvp_active(&g)) {
        if (g.matches != seen_match) { seen_match = g.matches; seen_round = 0; seen_fight = 0; seen_reset = 0; seen_plans = g.plans; seen_ctf = ctf_seq; }
        if (g.round != seen_round) { seen_round = g.round; seen_fight = 0; seen_cd = -1; seen_reset = 0; }
        if (g.placing && g.countdown_at) {
            int left = 3 - (int)((g.ticks - (g.countdown_at - 1)) / 60);
            if (left != seen_cd && left >= 1 && left <= 3) { seen_cd = left; center("%d", left); }
        }
        if (g.fight_start && !g.placing && seen_fight != g.fight_start) {
            seen_fight = g.fight_start;
            if (g.arena) center(g.mode == PVP_MODE_CTF ? TR("Round %d - Capture the flag!") : TR("Round %d - Fight!"), g.round);
        }
        if (g.plans != seen_plans && g.planning) { seen_plans = g.plans; center("%s", TR("Planning pause: give your orders, then press Ready")); }
        if (ctf_seq != seen_ctf) {
            seen_ctf = ctf_seq; char nm[96]; pvp_ui_unit_name(ctf_last_unit, nm, sizeof nm);
            int red = ctf_last_team == PVP_TEAM_RED;                       /* whole sentences per team (word order, gender) */
            const char *who = nm[0] ? nm : TR("A unit");
            if (ctf_last_kind == 0) center(red ? TR("%s took the Red flag") : TR("%s took the Blue flag"), who);
            else if (ctf_last_kind == 1) center("%s", red ? TR("The Red flag was dropped") : TR("The Blue flag was dropped"));
            else if (ctf_last_kind == 2) center("%s", red ? TR("The Red flag is back home") : TR("The Blue flag is back home"));
            else center("%s", red ? TR("Blue scores!") : TR("Red scores!"));
        }
        { static int was_open; static uint32_t was_match;                     /* E4: a proposal closed without agreement */
          if (was_match != g.matches) { was_match = g.matches; was_open = 0; }
          if (was_open && !g.vote_open && !g.countdown_at && g.placing) center("%s", TR("Not yet: the fight waits until everyone agrees."));
          was_open = g.vote_open; }
        if (g.placing && !g.countdown_at && g.round <= 1 && seen_prep != g.matches) {   /* E8: the preparation starts */
            seen_prep = g.matches; center("%s", TR("Preparation: place your units, then propose the fight. It starts when every player agreed."));
        }
        /* E9: the round / match result is the round result screen (pvp_ui.c), shown while the world stands still */
        (void)seen_reset;
    }
}
/* host: START once every peer is on the arena floor and nobody is loading */
static void auto_start_frame(void) {
    if (!is_host() || auto_start_match < 0 || g.phase != PVP_SETUP || !g.arena) return;
    Obj c0 = cdc(); Obj dd = c0 ? AT(c0, 0x40, Obj) : NULL;
    mp_session *s = sess(); dsnet_client *c = s ? mp_client(s) : NULL;
    int floor_now = dd ? AT(dd, 0x14, int32_t) : -1;
    static int64_t ok_since = -1;
    int there = g.arena == PVP_WHERE_BASE ? cur_map() == 0 : floor_now == g.floor;   /* base: nobody travels */
    int ready = there && !busy() && !(trn_flow_busy && trn_flow_busy()) && rig_det_mp_ready() && c && !dsnet_busy_mask(c);
    if (!ready) { ok_since = -1; return; }
    if (ok_since < 0) { ok_since = tick(); return; }
    if (tick() - ok_since < 60) return;                  /* one second on the floor for everyone */
    uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, PVP_OP_START);
    rig_log("pvp: everyone %s: START (rc %d)", g.arena == PVP_WHERE_BASE ? "at the settlement" : "on the arena floor", send_cmd(b, n));
    auto_start_match = -1; ok_since = -1;
}
void pvp_frame(void) {
    session_check();
    mp_session *s = sess(); if (!s) return;
    pause_rule_frame();
    messages_frame();
    auto_start_frame();
    pvp_units_frame();
    after_units_frame();
    rebring_frame();
    if (is_host()) {
        /* ARENA snapshot once the first fight began, between steps, never in a map change */
        if (pvp_fighting(&g) && g.round == 1 && arena_snap_match != (int)g.matches && rig_det_mp_ready() && !busy() && !(trn_flow_busy && trn_flow_busy()))
            if (!snap_take(&snap_arena, "arena")) arena_snap_match = (int)g.matches;
        /* E9: the round result screen shows for ROUND_SCREEN_S on the host's clock, then NEXT runs the reset */
        if (pvp_ending(&g)) {
            if (end_seen != g.matches * 64 + g.round) { end_seen = g.matches * 64 + g.round; end_t0 = now_s(); }
            else if (end_t0 > 0 && now_s() - end_t0 >= ROUND_SCREEN_S) {
                uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, PVP_OP_NEXT);
                rig_log("pvp: round %d result screen over: next (rc %d)", g.round, send_cmd(b, n)); end_t0 = -1;
            }
        }
        /* planning pause: the host's clock ends it after plan_len_s (Ready from everyone may end it sooner) */
        if (pvp_planning(&g)) {
            if (plan_seen != g.plans) { plan_seen = g.plans; plan_t0 = now_s(); }
            else if (plan_t0 > 0 && now_s() - plan_t0 >= g.rules.plan_len_s) {
                uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, PVP_OP_RESUME);
                rig_log("pvp: planning pause %u: %d s over, resume (rc %d)", g.plans, g.rules.plan_len_s, send_cmd(b, n)); plan_t0 = -1;
            }
        }
    } else if (pvp_active(&g) && g.resetting) {   /* guest: ask for the snapshot once per reset */
        int key = (int)(g.matches * 64 + g.round * 4 + g.resetting);
        if (key != resync_key) { resync_key = key; n_resync++; rig_log("pvp: reset seen (round %d): asking the host for the snapshot (rc %d)", g.round, dsnet_request_resync(mp_client(s))); }
    }
}

void pvp_install(void) {
    int bad = 0;
    bad |= rig_hook(VA_GETRELATION, (void *)h_getrel, (void **)&o_getrel, "pvp FactionRelationInfo.GetRelation");
    bad |= rig_hook(VA_ISHOSTILE, (void *)h_ishostile, (void **)&o_ishostile, "pvp EntityExtensions.IsHostile");
    bad |= rig_hook(VA_ISRELATION, (void *)h_isrel, (void **)&o_isrel, "pvp FactionRelationInfo.IsRelation");
    bad |= rig_hook(VA_CDH_INIT, (void *)h_init, (void **)&o_init, "pvp CombatDataHandler.Init");
    bad |= rig_hook(VA_CDH_SPAWN, (void *)h_spawn, (void **)&o_spawn, "pvp CombatDataHandler.OnSpawnUnit");
    bad |= rig_hook(VA_CDH_MOVE, (void *)h_move, (void **)&o_move, "pvp CombatDataHandler.OnMoveUnit");
    bad |= rig_hook(VA_CDH_DEAD, (void *)h_dead, (void **)&o_dead, "pvp CombatDataHandler.OnDeadUnit");
    bad |= rig_hook(VA_CDH_DESPAWN, (void *)h_despawn, (void **)&o_despawn, "pvp CombatDataHandler.OnDespawnUnit");
    bad |= rig_hook(VA_CDH_AGGRO, (void *)h_aggro, (void **)&o_aggro, "pvp CombatDataHandler.CheckAggroTrigger");
    bad |= rig_hook(VA_SPAWN_CATEGORY, (void *)h_spawncat, (void **)&o_spawncat, "pvp DungeonGeneratorStrategy.HandleSpawnCategory");
    bad |= rig_hook(VA_SPAWN_NPC, (void *)h_spawnnpc, (void **)&o_spawnnpc, "pvp CampaignEntityLoadHelper.SpawnDungeonNPC");
    bad |= rig_hook(VA_DUNGEON_TICK, (void *)h_dtick, (void **)&o_dtick, "pvp DungeonBehaviorSimulator.Tick");
    bad |= rig_hook(VA_PORTAL_PROCESS, (void *)h_portal, (void **)&o_portal, "pvp DungeonPortalHandler.Process");
    bad |= rig_hook(VA_GAMEOVER, (void *)h_gameover, (void **)&o_gameover, "pvp EntityEventHandler.CheckGameOver");
    bad |= rig_hook(VA_DEAD_APPLY, (void *)h_deadapply, (void **)&o_deadapply, "pvp MainEventApplier.Apply(DeadApplyData)");
    bad |= rig_hook(0xC8D6FC, (void *)h_ev_startable, (void **)&o_ev_startable, "pvp EventContainer.IsEventStartable");
    bad |= rig_hook(0xC8DFBC, (void *)h_ev_start1, (void **)&o_ev_start1, "pvp EventContainer.StartEvent(key)");
    bad |= rig_hook(0xCEA620, (void *)h_ev_start2, (void **)&o_ev_start2, "pvp EventFlowProcessor.StartEvent(key)");
    bad |= rig_hook(0xA782C8, (void *)h_preg, (void **)&o_preg, "pvp PassiveActionContainer.Register");
    rig_log("pvp: hooks %s", bad ? "INCOMPLETE" : "installed (18)");
}

/* ---------------------------------------------------------------- rig commands */
static char *okj(Buf *b) { return b->p; }
static char *errj(const char *m) { Buf b = {0}; bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, m); bput(&b, "}"); return b.p; }
static int parse_teams(const char *line, uint8_t t[PVP_MAX_PLAYERS]) {
    char s[200]; memset(t, 0, PVP_MAX_PLAYERS);
    if (!jstr(line, "teams", s, sizeof s)) return -1;
    int i = 0; for (char *p = strtok(s, ","); p && i < PVP_MAX_PLAYERS; p = strtok(NULL, ",")) t[i++] = (uint8_t)atoi(p);
    return 0;
}
char *pvp_exec(const char *cmd, const char *line) {
    session_check();
    if (!strncmp(cmd, "pvp_units_", 10)) return pvp_units_exec(cmd, line);
    if (!strcmp(cmd, "pvp_mods")) return pvp_mods_exec(cmd, line);
    if (!strcmp(cmd, "pvp_setup")) {
        uint8_t t[PVP_MAX_PLAYERS], b[64]; double mode = PVP_MODE_FIGHT; jnum(line, "mode", &mode);
        if (parse_teams(line, t)) return errj("need teams, e.g. \"1,2\" (team per player slot: 1 Blue, 2 Red)");
        pvp_rules ru = pvp_rules_default(); double v;
        if (jnum(line, "best_of", &v)) ru.best_of = (uint8_t)v;
        if (jnum(line, "round_secs", &v)) ru.round_secs = (uint16_t)v;
        if (jnum(line, "pause", &v)) ru.pause_rule = (uint8_t)v;
        if (jnum(line, "plan_every", &v)) ru.plan_every_s = (uint8_t)v;
        if (jnum(line, "plan_len", &v)) ru.plan_len_s = (uint8_t)v;
        if (jnum(line, "vanilla", &v)) ru.vanilla = (uint8_t)v;
        size_t n = pvp_cmd_setup(b, sizeof b, (int)mode, t, &ru);
        Buf r = {0}; bput(&r, "{\"ok\":true,\"rc\":%d}", send_cmd(b, n)); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_bring_lib")) {               /* E3 test: {"ids":"id1,id2"} = the card window's My Units picks -> Bring (pvp_bring_picks) */
        char ids[600] = ""; jstr(line, "ids", ids, sizeof ids);
        static mu_pick pk[8]; int n = 0; memset(pk, 0, sizeof pk);
        for (char *t2 = strtok(ids, ","); t2 && n < 8; t2 = strtok(NULL, ",")) { snprintf(pk[n].lib_id, sizeof pk[n].lib_id, "%s", t2); snprintf(pk[n].unit_guid, sizeof pk[n].unit_guid, "%s", t2); snprintf(pk[n].name, sizeof pk[n].name, "%s", t2); n++; }
        char err[300] = ""; int rc = pvp_bring_picks(pk, n, err, sizeof err);
        Buf r = {0}; bput(&r, "{\"ok\":%s,\"rc\":%d,\"ul\":%d,\"error\":", rc ? "false" : "true", rc, mu_ul_present()); bstr(&r, err); bput(&r, "}"); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_hudcheck")) {                /* E5b test: per PvP unit [team, enemy, head health bar shown (-1 no HUD)] on this copy */
        int ui_hud_bars_on(const uint8_t g[16]);
        Buf r = {0}; bput(&r, "{\"ok\":true,\"units\":[");
        for (int i = 0; i < g.nunits; i++) bput(&r, "%s[%d,%d,%d]", i ? "," : "", g.units[i].team, enemy_unit(g.units[i].guid), ui_hud_bars_on(g.units[i].guid));
        bput(&r, "]}"); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_selgroup")) {                /* E7b test: a click's selection of this unit (SelectProcessor.Select, hooked like a
                                                          click); the group after it: 3003 = my units, 3001 = enemy (monster view) */
        char gs[64]; Guid gg; if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &gg)) return errj("need guid");
        int ord_debug_click_select(int map, const uint8_t g[16]);
        int grp = ord_debug_click_select(cur_map(), (const uint8_t *)&gg);
        Buf r = {0}; bput(&r, "{\"ok\":true,\"group\":%d}", grp); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_vote")) {                    /* E4: {"op":"propose|agree|notyet|cancel"} from this player */
        char o[16] = ""; jstr(line, "op", o, sizeof o);
        int op = !strcmp(o, "propose") ? PVP_OP_PROPOSE : !strcmp(o, "agree") ? PVP_OP_AGREE : !strcmp(o, "notyet") ? PVP_OP_NOTYET : !strcmp(o, "cancel") ? PVP_OP_CANCEL : -1;
        Buf r = {0}; bput(&r, "{\"ok\":true,\"rc\":%d}", pvp_ui_vote(op)); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_ready") || !strcmp(cmd, "pvp_resume")) {
        uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, cmd[4] == 'r' && cmd[5] == 'e' && cmd[6] == 'a' ? PVP_OP_READY : PVP_OP_RESUME);
        Buf r = {0}; bput(&r, "{\"ok\":true,\"rc\":%d}", send_cmd(b, n)); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_start") && is_host() && !g.arena && snap_take(&snap_world, "world")) return errj("world snapshot failed");
    if (!strcmp(cmd, "pvp_start") || !strcmp(cmd, "pvp_end")) {
        uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, cmd[4] == 's' ? PVP_OP_START : PVP_OP_END);
        Buf r = {0}; bput(&r, "{\"ok\":true,\"rc\":%d}", send_cmd(b, n)); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_arena")) {                 /* {"region":r,"floor":f,"seed":s} (host, SETUP phase) */
        double r = 1, f = 1, sd = 1; jnum(line, "region", &r); jnum(line, "floor", &f); jnum(line, "seed", &sd);
        uint8_t b[32]; size_t n = pvp_cmd_arena(b, sizeof b, (int32_t)r, (int32_t)f, (int32_t)sd);
        if (is_host() && snap_take(&snap_world, "world")) return errj("world snapshot failed");
        Buf q = {0}; bput(&q, "{\"ok\":true,\"rc\":%d}", send_cmd(b, n)); return okj(&q);
    }
    if (!strcmp(cmd, "pvp_cellmap")) {              /* test: the settlement grid around (x, y): M movable, B blocked, V visited bits */
        double x0 = 120, y0 = 120, w = 60, h = 60; jnum(line, "x", &x0); jnum(line, "y", &y0); jnum(line, "w", &w); jnum(line, "h", &h);
        Obj rd = pvp_runtime_map(cur_map()); if (!rd) return errj("no map data");
        Buf q = {0}; bput(&q, "{\"ok\":true,\"rows\":[");
        for (int y = (int)(y0 + h) - 1; y >= (int)y0; y--) {
            char row[260]; int k = 0;
            for (int x = (int)x0; x < (int)(x0 + w) && k < 250; x++) {
                V2I v = { x, y };
                int m = F(VA_MAP_MOVABLE, bool (*)(Obj, V2I, void *))(rd, v, NULL), b = F(0xBEF2BC, bool (*)(Obj, V2I, void *))(rd, v, NULL), vi = F(0xBEF354, bool (*)(Obj, V2I, void *))(rd, v, NULL);
                row[k++] = (char)('0' + (m | b << 1 | vi << 2));
            }
            row[k] = 0; bput(&q, "%s\"%s\"", y == (int)(y0 + h) - 1 ? "" : ",", row);
        }
        bput(&q, "]}"); return okj(&q);
    }
    if (!strcmp(cmd, "pvp_base")) {                  /* host, SETUP phase: play on the settlement map (PvP v1) */
        uint8_t b[4]; size_t n = pvp_cmd_simple(b, sizeof b, PVP_OP_BASE);
        if (is_host() && snap_take(&snap_world, "world")) return errj("world snapshot failed");
        Buf q = {0}; bput(&q, "{\"ok\":true,\"rc\":%d}", send_cmd(b, n)); return okj(&q);
    }
    if (!strcmp(cmd, "pvp_arena_info")) {            /* monsters / NPCs / items left on the current map, portals, rooms */
        int map = cur_map(); Obj ud = units_dict(map); int nu = 0, nmon = 0, nother = 0;
        if (ud) {
            Obj ent = AT(ud, 0x18, Obj); int n = AT(ud, 0x20, int32_t);
            for (int i = 0; ent && i < n; i++) {
                char *e = (char *)ent + 0x20 + i * 0x20; if (*(int32_t *)e < 0) continue;
                Obj u = *(Obj *)(e + 0x18); if (!u) continue; nu++;
                Obj r = AT(u, 0x40, Obj); Obj ci = r ? AT(r, 0x10, Obj) : NULL; int fac = ci ? AT(ci, 0x18, int32_t) : -1;
                if (fac == 3) { if (!unit_dead(u)) nmon++; } else if (fac != 1) nother++;
            }
        }
        int32_t ty[64], pv[64][2], off[2]; uint64_t rh = 0; int nr = trn_last_rooms(ty, pv, 64, off, &rh);
        Buf q = {0};
        bput(&q, "{\"ok\":true,\"map\":%d,\"units\":%d,\"monsters_alive\":%d,\"non_player_non_monster\":%d,\"arena_rc\":%d,\"rooms\":%d,\"room_hash\":\"%016llx\",\"offset\":[%d,%d],\"room_list\":[",
             map, nu, nmon, nother, arena_rc, nr, (unsigned long long)rh, off[0], off[1]);
        for (int i = 0; i < nr; i++) bput(&q, "%s[%d,%d,%d]", i ? "," : "", ty[i], pv[i][0], pv[i][1]);
        bput(&q, "],\"portals\":[");
        for (int i = 0; i < nportal; i++) { Guid pg; memcpy(&pg, portal_guid[i], 16); char ps[64]; guid_str(pg, ps); bput(&q, "%s[%d,%d,\"%s\"]", i ? "," : "", portal_cells[i][0], portal_cells[i][1], ps); }
        bput(&q, "],\"banner_cells\":[");
        { Obj c0 = cdc(); Obj edc = c0 ? AT(c0, 0x20, Obj) : NULL; Obj bc = edc ? dict_int_get(AT(edc, 0x20, Obj), map) : NULL; Obj bd = bc ? AT(bc, 0x18, Obj) : NULL; int k = 0;
          Obj ent = bd ? AT(bd, 0x18, Obj) : NULL; int n = bd ? AT(bd, 0x20, int32_t) : 0;
          for (int i = 0; ent && i < n; i++) { char *e = (char *)ent + 0x20 + i * 0x20; if (*(int32_t *)e < 0) continue; Obj b = *(Obj *)(e + 0x18);
            if (!b || !key_has(AT(b, 0x38, Obj), "BannerStand")) continue; int32_t cc[2] = { -1, -1 }; unit_cell(map, (const uint8_t *)(e + 8), cc);
            bput(&q, "%s[%d,%d,%d]", k++ ? "," : "", cc[0], cc[1], key_has(AT(b, 0x38, Obj), "Blue")); } }
        bput(&q, "],\"cuts\":{\"spawn\":%u,\"npc\":%u,\"dungeon_tick\":%u,\"portal\":%u,\"gameover\":%u,\"kick\":%d,\"death_skipped\":%u,\"downed_made\":%u,\"passive_skipped\":%u,\"events_held\":%u,\"markers\":%d},\"hold\":%d}",
             n_spawn_cut, n_npc_cut, n_dungeon_tick_cut, n_portal_refused, n_gameover_cut, n_kick, n_death_skipped, n_downed_made, n_passive_skipped, n_events_held, n_markers, pvp_hold_events(&g));
        return okj(&q);
    }
    if (!strcmp(cmd, "pvp_cfg")) {                   /* test: {"fix_out":0|1} on THIS copy (set the same on every copy before START) */
        double v; if (jnum(line, "fix_out", &v)) fix_out = v != 0;
        if (jnum(line, "banners", &v)) banners = v != 0;
        if (jnum(line, "kicks", &v)) kicks = v != 0;
        { double cx = -1e9, cy = -1e9, cz = 0; int has = jnum(line, "cam_x", &cx) && jnum(line, "cam_y", &cy); jnum(line, "cam_size", &cz);
          if (has || cz > 0 || strstr(line, "cam_read")) cam_to(has ? (float)cx : -1e9f, has ? (float)cy : -1e9f, (float)cz); }
        Buf q = {0}; bput(&q, "{\"ok\":true,\"fix_out\":%d,\"banners\":%d,\"cam_before\":[%.2f,%.2f,%.2f],\"cam_size_before\":%.2f}", fix_out, banners,
                          cam_last.x, cam_last.y, cam_last.z, cam_size_last); return okj(&q);
    }
    if (!strcmp(cmd, "pvp_banner_test")) {           /* test: one banner stand at (x, y) now, outside the sim; scope=1 inside it */
        double x = 0, y = 0, sc = 0; jnum(line, "x", &x); jnum(line, "y", &y); jnum(line, "scope", &sc);
        int32_t c2[2] = { (int32_t)x, (int32_t)y };
        if (sc) rig_det_sim_scope(1);
        spawn_banner(cur_map(), "BUILDING_BlueBannerStand", c2);
        if (sc) rig_det_sim_scope(0);
        Buf q = {0}; bput(&q, "{\"ok\":true,\"banners\":%d}", n_banner); return okj(&q);
    }
    if (!strcmp(cmd, "pvp_freeze")) {                /* test: {"at": world tick or -1} (host only) */
        double at = -1; jnum(line, "at", &at); int64_t t = (int64_t)at; static const uint8_t zero[16];
        mp_session *s = sess();
        Buf r = {0}; bput(&r, "{\"ok\":true,\"rc\":%d}", s ? mp_order(s, PVP_FREEZE_TYPE, zero, &t, 8) : -1); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_raw")) {                   /* test: any payload (hex) from this peer, e.g. a guest's START */
        char hx[200]; uint8_t b[100]; size_t n = 0;
        if (!jstr(line, "hex", hx, sizeof hx)) return errj("need hex");
        for (size_t i = 0; hx[i] && hx[i + 1] && n < sizeof b; i += 2) { unsigned v; sscanf(hx + i, "%2x", &v); b[n++] = (uint8_t)v; }
        Buf r = {0}; bput(&r, "{\"ok\":true,\"rc\":%d}", send_cmd(b, n)); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_rel")) {                   /* {"a":guid,"b":guid}: what the game answers now */
        char as[64], bs[64]; Guid ga, gb;
        if (!jstr(line, "a", as, sizeof as) || !jstr(line, "b", bs, sizeof bs) || !guid_parse(as, &ga) || !guid_parse(bs, &gb)) return errj("need a, b");
        int map = cur_map(); Obj ua = find_unit(map, ga), ub = find_unit(map, gb);
        Obj h = combat_of(map);
        if (!ua || !ub) return errj("unit not on this map");
        int rel = F(VA_GETRELATION, int (*)(Obj, Obj, void *))(ua, ub, NULL);
        bool ih = F(VA_ISHOSTILE, bool (*)(Obj, Obj, void *))(ua, ub, NULL);
        int l_ab = -1, l_ba = -1;
        if (h && hed_of(h, (uint8_t *)&ga) && hed_of(h, (uint8_t *)&gb)) {
            l_ab = F(VA_CDH_ISHOSTILE, bool (*)(Obj, uint64_t, uint64_t, uint64_t, uint64_t, void *))(h, ga.lo, ga.hi, gb.lo, gb.hi, NULL);
            l_ba = F(VA_CDH_ISHOSTILE, bool (*)(Obj, uint64_t, uint64_t, uint64_t, uint64_t, void *))(h, gb.lo, gb.hi, ga.lo, ga.hi, NULL);
        }
        Buf r = {0}; bput(&r, "{\"ok\":true,\"relation\":%d,\"is_hostile\":%d,\"list_ab\":%d,\"list_ba\":%d}", rel, ih, l_ab, l_ba); return okj(&r);
    }
    if (!strcmp(cmd, "pvp_status")) {
        int map = cur_map(); Obj h = combat_of(map);
        Buf r = {0};
        bput(&r, "{\"ok\":true,\"ending\":%d,\"vote_open\":%d,\"vote_by\":%d,\"agree_mask\":%u,\"cd_ticks\":%u,\"countdown_at\":%u,\"kicks\":%d,\"walk_resend\":%u,\"selgroup_enemy\":%u,\"act_refused\":%u,\"round\":%d,\"score\":[%d,%d],\"draws\":%d,\"resetting\":%d,\"last_winner\":%d,\"match_winner\":%d,\"planning\":%d,\"plans\":%u,\"rules\":[%d,%d,%d,%d,%d],\"vanilla\":%d,\"snaps\":[%d,%d],\"resyncs\":%d,\"overrides\":%d,\"arena\":%d,\"placing\":%d,\"fight_start\":%u,\"ends\":[[%d,%d],[%d,%d]],\"frozen\":%d,\"freeze_at\":%lld,\"world_tick\":%lld,\"phase\":%d,\"mode\":%d,\"active\":%d,\"ticks\":%u,\"matches\":%u,\"hash\":\"%016llx\",\"last_rc\":%d,"
                 "\"counts\":{\"rel\":%u,\"ishostile\":%u,\"add\":%u,\"refresh\":%u,\"remove\":%u,\"aggro\":%u,\"rejected\":%u,\"isrel\":%u},\"handler\":%d,\"teams\":[",
             g.ending, g.vote_open, g.vote_by, g.agree_mask, g.cd_ticks, g.countdown_at, kicks, n_walk_resend, n_selgroup_enemy, n_act_refused, g.round, g.score[1], g.score[2], g.draws, g.resetting, g.last_winner, g.match_winner, g.planning, g.plans,
             g.rules.best_of, g.rules.round_secs, g.rules.pause_rule, g.rules.plan_every_s, g.rules.plan_len_s, g.rules.vanilla, snap_world.tick_at, snap_arena.tick_at, n_resync, n_override,
             g.arena, g.placing, g.fight_start, g.end_cell[1][0], g.end_cell[1][1], g.end_cell[2][0], g.end_cell[2][1],
             pvp_freeze_now(), (long long)freeze_at, (long long)tick(), g.phase, g.mode, pvp_active(&g), g.ticks, g.matches, (unsigned long long)pvp_hash(&g), last_rc,
             n_rel_hostile, n_ishostile, n_add, n_refresh, n_remove, n_aggro, n_rejected, n_isrel, h != NULL);
        for (int i = 0; i < 8; i++) bput(&r, "%s%d", i ? "," : "", g.team_of[i]);
        bput(&r, "],\"ctf\":{\"homes\":[[%d,%d],[%d,%d]],\"captures\":%u,\"banners\":%d,\"events\":[%u,%u,%u,%u],\"flags\":[", g.home[1][0], g.home[1][1], g.home[2][0], g.home[2][1],
             g.captures, n_banner, n_ctf_ev[0], n_ctf_ev[1], n_ctf_ev[2], n_ctf_ev[3]);
        for (int t = 1; t <= 2; t++) bput(&r, "%s[%d,%d,%d,%d,%d]", t > 1 ? "," : "", g.flag[t].state, g.flag[t].carrier == PVP_NO_UNIT ? -1 : g.flag[t].carrier, g.flag[t].cell[0], g.flag[t].cell[1], g.flag[t].hold);
        bput(&r, "]},\"units\":[");
        for (int i = 0; i < g.nunits; i++) {
            Guid gg; memcpy(&gg, g.units[i].guid, 16); char gs[64]; guid_str(gg, gs);
            Obj u = map >= 0 ? find_unit(map, gg) : NULL; Obj d = hed_of(h, g.units[i].guid);
            int enemies = 0, nh = 0;
            if (d && AT(d, 0x28, Obj)) {
                nh = AT(AT(d, 0x28, Obj), 0x20, int32_t) - AT(AT(d, 0x28, Obj), 0x28, int32_t);   /* Count = _count (+0x20) - _freeCount (+0x28) */
                for (int j = 0; j < g.nunits; j++)
                    if (g.units[j].team != g.units[i].team && dict_guid_get(AT(d, 0x28, Obj), g.units[j].guid)) enemies++;
            }
            bput(&r, "%s{\"guid\":\"%s\",\"team\":%d,\"player\":%d,\"on_map\":%d", i ? "," : "", gs, g.units[i].team, g.units[i].player, u != NULL);
            if (u) {
                int cs; Guid cur, force, tt; unit_combat(u, &cs, &cur, &force, &tt);
                char cg[64], tg[64]; guid_str(cur, cg); guid_str(tt, tg);
                Obj ei = AT(u, 0x40, Obj), ec = ei ? AT(ei, 0x10, Obj) : NULL;   /* EntityInfoReader -> EntityInfoComponent._behaviourOption (+0x20, Mac = Windows) */
                bput(&r, ",\"options\":%d", ec ? AT(ec, 0x20, int32_t) : -1);
                bput(&r, ",\"dead\":%d,\"hp\":%.2f,\"task\":%d,\"combat\":%d,\"target\":\"%s\",\"task_target\":\"%s\"", unit_dead(u), unit_stat(u, 9), unit_task(u), cs, cg, tg);
            }
            bput(&r, ",\"hed\":%d,\"hostile_entries\":%d,\"enemies_listed\":%d,\"out_tick\":%d,\"ordered\":%d}", d != NULL, nh, enemies, pvp_out_tick(&g, g.units[i].guid), g.units[i].ordered);
        }
        bput(&r, "]}");
        return okj(&r);
    }
    return NULL;
}
