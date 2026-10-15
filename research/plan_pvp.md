# PvP modes: design and technical plan (2026-10-14, build 25846389)

Scope: plan only. Build later in a separate source copy (not lan_v9 / lan_v10). Modes in order: Fight (team vs team),
Capture the Flag (CTF), later Protect Base and Tower Defense. Arenas = the game's own dungeon floors first.
Sources read: PLAN.md (owner decisions, "Next features"), research/discord/*.txt, HANDOFF_v9.md, src/UI_NOTES.md,
src/net/dsnet.h, src/mp_game.c, src/ord_game.c, src/mp_core.c, research/transitions_design.md, the Mac IL2CPP dump
(~/ds_skill_truth/dump_25846389/dump.cs) and the Ghidra pseudo-code of the same binary (~/ds_skill_truth/decomp_25846389).
Addresses are RVAs from dump.cs. **[UNVERIFIED]** = not proven in code or in a run. **[INFERRED]** = read from
pseudo-code, not run.

## TLDR

| Change | Impact | Cost | Failure point |
|---|---|---|---|
| 1. Team table + PvP rules as sim state (new dsnet command `DSNET_PVP`, pure-C `pvp_core.c` with unit tests) | Every peer knows the same teams, mode and rules at the same step | S (1 lane, no game) | Rules state missing from the save / fingerprint -> late join or resync desyncs |
| 2. Hostility between two player teams (hooks on `FactionRelationInfo.GetRelation`, `EntityExtensions.IsHostile`, and the `CombatDataHandler` hostile lists) | Blue and Red units attack each other; teammates stay allies; skills, auto attack and right-click attack work | M, hardest part, do first as a spike | The game groups hostility by (Faction, SubFaction) buckets: both teams are in the same `(Player, Player)` bucket, so per-unit hooks alone do not feed the AI target lists. A missed path = units ignore each other, or one peer sees a hostile and the other not = desync |
| 3. Arena floor (forced seed, no monsters / NPCs / respawn / patrol / penalty, exit portal blocked, each team placed at one end) | A clean, same-on-all-peers arena from a stock dungeon floor | M | Edgar time limit on floor generation (known, transitions_design 2.2); spawn-cell mapping in `InsertUnitTo` not proven |
| 4. Fight mode rules (elimination, round clock in game ticks, best-of rounds, stock game-over off) + world restore by save reload | Rounds that end and reset the same way on all peers; the host's world is unchanged after a match | M | Reload time per round; depends on the existing join-point / resync save path |
| 5. PvP lobby, HUD, messages in game style | Players can set up and read a match | L (UI heavy) | Owner taste (he judges by eye) |
| 6. Pause rule: no pause, or planning pause every N s | Owner's choice per match | S | Orders during a planning pause must run at a stamped step (they do today for holds; check) |
| 7. Capture the Flag (logic in our C sim state, banner stands as static visuals, carried / dropped flag drawn by us) | Second mode with no new game objects in the sim | M | Pickup / capture checks must run inside the sim tick and read only sim positions |
| 8. Soak, Windows kit, release gate | Ships like lan_v7-v9 | M | Long-run desync only seen after 20+ min |
| 9. Unit source A: copy of your own units from YOUR save, with the Foreign Expedition save library (ext/foreign_expedition, FE plan F1, 1.2-1.5) | Bring your real units; your save and the host's world never change | S on top of FE | Depends on the FE library landing; injecting on the Dungeon side (to skip the 4-unit cap) is not proven, FE injects on Base |
| 10. Unit source B: import a build from a wiki builder share URL (`?team=`, `#b=~`, `#b=` JSON, short `/build/<id>` / `/t/<id>`) | Theory-craft a build on the wiki and fight with it | M-L | Wiki index tables must match the game build (fingerprint); spawn path (`SpawnUnitFromCandidateData` + learn skills + equip with tags) not proven to give the same unit as the builder shows |
| Party size | Units are spawned / injected straight onto the arena floor, so the expedition party cap (4, `SystemVar.Campaign.PlayerPartyMax`, getter 0x83EF5C) is not in the path | - | The party bar and other UI may still assume 4 units **[UNVERIFIED]** |

Owner decisions held here: pause = host setting (no pause, or a planning pause every 5-10 s); arenas = the game's own
dungeon floors first, base-map modes later; units = own units copied from your save, OR a wiki builder URL import;
the builder's save-upload path stays hidden (this plan reads only share links and never names or links that path).

## 1. What the game does today (dump truth)

### 1.1 Factions and who attacks whom
- `Refactor.FactionType` (dump.cs 31136): Neutral 0, Player 1, Ally 2, Enemy 3.
- `Refactor.SubFactionType` (31453): None 0, Player 1, NPC 2, Building 3, Beast 4, Insect 5, Anomaly 6, Artifact 7, Abyss 8.
- Per unit: `Faction` = `EntityInfoComponent._faction` (+0x18, saved in `EntityInfoComponentSaveData.Faction`),
  read by `UnitEntity.get_Faction` 0xFA4950 (UnitEntity +0x40 -> reader +0x10 -> +0x18).
  `SubFaction` = `UnitTableData` +0x34, a STATIC value per unit key (`UnitEntity.get_SubFaction` 0xFA8584). It cannot
  be set per unit.
- `Refactor.FactionRelationInfo` (43086), static tables built in `Init` 0x82AEEC with `SetRelation`:
  faction table: Player-Enemy Hostile, Ally-Enemy Hostile, Enemy-Enemy Hostile, Player-Ally Ally.
  sub-faction table: Player vs Beast/Insect/Anomaly/Artifact/Abyss/NPC/Building Hostile, NPC vs most Hostile, etc.
  **[INFERRED]** split between the two tables: both overloads have the same name in the pseudo-code; the split
  follows the call order (first 4 calls = faction table).
- `GetRelation(IEntity a, IEntity b)` 0x82BBA0: Hostile (1) only if the faction relation AND the sub-faction
  relation are both Hostile; else 2 (Ally). Player-Player has no entry -> never hostile.
  `IsRelation` 0x82C5DC and `IsAffectTarget` 0x82C604 (skill target rules: SingleHostile/MultipleHostile -> relation
  == 1; SingleAlly/MultipleAlly -> == 2) both go through `GetRelation(IEntity, IEntity)`.
  Callers (pseudo-code grep): EntityTargetSelector, ValidTargetCondition, DecidePlayerCombatBehaviour,
  ValidSkillTargetExposer, MoveTaskHelper, CheckInputPath, EntityEventHandler.
- `EntityExtensions.IsHostile(IEntity, IEntity)` 0xFA4CE8: a separate faction-only shortcut (Player/Ally vs Enemy).
  Callers: BuildingAttackValidator, StrangeSeedHeal, WaterDive.
- `Refactor.Runtime.CombatDataHandler` (165182), the AI's hostile bookkeeping, per map:
  `_factionAggros` (+0x18) and `_factionUnits` (+0x20) are keyed by `(FactionType, SubFactionType)`;
  `_entityHostiles` (+0x30) = one `HostileEntityData` per unit (list of hostile Guids + positions).
  `OnSpawnUnit` 0xBCF690, `OnMoveUnit` 0xBD1930, `OnDeadUnit` 0xBD00E8, `OnDespawnUnit` 0xBD1EB8 and `ChangeToEnemy`
  0xBCF104 fill those lists by bucket: `GetRelationsOf(faction, subFaction, Hostile)` 0x82BE08 -> every unit in a
  hostile bucket. Readers: `IsHostileTo` 0xBCEBA8, `GetClosestHostile` 0xBCEFA4, `GetAllCombatableHostiles`
  0xBCED2C, `HasHostileInCombatableArea` 0xBCEAB4, `IsRoomHostile` 0xBCEE70.
  `HostileEntityData` (167665): `AddHostile` 0xBF3B88, `RemoveHostile` 0xBF3C10, `RefreshHostile` 0xBF3C88,
  `AddInRoomHostile` 0xBF3D20, `RemoveInRoomHostile` 0xBF3E24, `IsHostileTo` 0xBF4E44.
- The right-click attack interaction: `InteractionFactory` builds `IsHostileUnitExposer` (ctor 0xFA96E0);
  its `CanVerify<object,object>` 0x1990548 makes one interface call with (Guid, Guid) arguments.
  **[INFERRED]** that call is `ICombatDataReader.IsHostileTo(Guid, Guid)` -> `CombatDataHandler.IsHostileTo` ->
  `HostileEntityData`. So right-click attack on an enemy player depends on the hostile lists, not on GetRelation.
- Also by faction bucket, not covered by this plan's first build: `FactionAggroData` (aggro areas, 167611),
  `AstarFactionHandler` (path traversal per faction, 137975), vision (`VisionDataHandler`).
- The game's only faction change: `ChangeUnitFactionRequested` (194794) -> `EntityEventHandler.OnEvent` 0xD7EAC4 ->
  `EntityLifecycleHelper.ChangeUnitFaction` 0xC9C7B4 -> `EntityDataContainer.ChangeUnitFactionToEnemy` 0xC8122C,
  `CombatDataHandler.ChangeToEnemy`, `PlayerDataHandler.ChangeToEnemy` 0xBE2AE4. The handler only accepts
  Neutral units (faction 0) and forces `CombatStateApplyData(unit, InCombat)` (ctor 0x8457A0).

### 1.2 Can player units be hostile to each other?
Not in stock. Three ways were checked:
- (A) Give team Red another faction (Enemy or Ally). Rejected: 78 classes read `get_Faction` directly (control,
  input, quick slots, unit summary, HUD, map transition, recruit, AI `DecideNonPlayerCombatBehaviour`,
  `PlayerDataHandler` party lists). Red units would stop being player units (no orders, monster AI, wrong UI).
  Also Enemy-Enemy is Hostile at faction level, so a sub-faction trick cannot work: sub-faction is static per key.
- (B) Patch the static relation tables. Rejected: tables are per (faction, sub-faction), so Player-Player becomes
  hostile for teammates too.
- (C) **Chosen.** Keep every unit Faction Player. Add a team table in our sim state. Hook the per-unit checks and
  feed the per-unit hostile lists ourselves (section 4.2). Teammates keep stock behaviour.

### 1.3 Dungeon floors: generate, load, spawn, monsters
- Floor change = `ChangeMapRequested` (ctor 0xD621E8; fields include `HasForcedDungeonSeed`, `ForcedDungeonSeed`,
  `IsSkipSafeFloor`, region, floor). Our `trn_game.c` already builds it as a command and fixes the seed path
  (`DSDungeonPreDataGenerator.GetRandomNumbersGenerator` 0xC5ABDC). Edgar generation risk: transitions_design 2.2 / 3.5.
- Generator: `Refactor.Main.DungeonGeneratorStrategy` (182553): `Generate` 0xCB96BC, `ProcessRoomSpawn` 0xCB9A9C,
  `HandleSpawnCategory` 0xCBA210, `HandleRandomMonsterSpawn` 0xCBA6BC, `HandleStaticMonsterSpawn` 0xCBAC94,
  `HandleStaticBuildingSpawn` 0xCBB234, `HandleStaticItemSpawn` 0xCBB6D4. `SpawnCategory` (174514):
  FloorPortalSpawn 0, PlayerUnitSpawn 1, MonsterSpawn 2, BuildingSpawn1-6, ItemSpawn1-2, NPCSpawn1-2.
  `RoomType`: Entry 0, Portal 1, Combat 2, HardCombat 3, Boss 4, ... SafeFloor 70, Corridor 99.
- Live floor behaviour: `Refactor.Runtime.DungeonBehaviorSimulator` (164626): `Tick` 0xBC4D80 (called from TickFlow)
  drives `MonsterRespawnHandler`, `MonsterPatrolHandler`, core activation, `CheckDungeonPenalty` 0xBC652C,
  `CheckRoomRespawn` 0xBC6C60. `OnEnterDungeon` 0xBC4F6C.
- Dungeon NPCs: `CampaignEntityLoadHelper.SpawnDungeonNPC` 0xDA8B30 (from `MapTransitionCommitService`
  PostTransferEnterHandlers). Player units placed by `CampaignEntityLoadHelper.InsertUnitTo(MapType to,
  List<UnitTransferInfo>, List<Vector2Int> spawnPositions)` 0xDA7E44 (from `MapTransitionCommitService.Commit`).
- Spawn API (sim): `EntityLifecycleHelper.SpawnUnit` 0xC96020, `SpawnBuilding(map, key, pos, astarApply, rotation,
  faction, summoner)` 0xC93308, `DespawnBuilding` 0xC9A84C, `DeadUnit` 0xC990CC, `DespawnUnit` 0xC9998C,
  `RespawnUnit` 0xC96EEC. Position change candidate: `MoveApplyData.Set(target, position, forceMove)` 0x84743C
  applied by `MoveApplier.Apply` (139491). **[UNVERIFIED]** that ForceMove is an instant teleport.
- Exit portal: `DungeonPortalHandler.Process` 0xF9E298 (PLAN.md).

### 1.4 Death and defeat
- Downed = Unconscious affecter (AffecterCategory 900). Dead = `EntityInfoComponent._isDead` (+0x1C,
  `rig_cmd.c unit_dead`). The stock party wipe: `EntityEventHandler.CheckGameOver` 0xD854EC opens the return screen
  when no player unit on the dungeon map passes `<CheckGameOver>b__69_0` 0xD8DA20 (conscious test) - found in lan_v9
  work (ui_game.c:1217).
- Death events: `PlayerDataHandler.OnDeadPlayerUnit` 0xBE2688, `CombatDataHandler.OnDeadUnit` 0xBD00E8,
  `PlayerDataHandler.get_AlivePlayerCount` 0xBE05AC.

### 1.5 UI that exists for score and time
- No PvP score or countdown UI in the game. Closest stock pieces:
  `BossHUDView` (36231): top-centre name text `_bossNameText` (+0x30) and two bars `_headHP` (+0x38), `_bodyHP`
  (+0x40); presenter `BossHUDPresenter` (`Activate` 0xFD5638, `Deactivate` 0xFD524C, `Init` 0xFD54C8).
  `SubUI_CampaignClockUI` (223273): `_timeText` (+0x40), `_dateText` (+0x30), pause/speed buttons (we already hook
  `Initialize` 0xE865E8).
  Centred big message = `HelpNoticeView` (goddess statue text), exported by us as `ui_center_message`.
  Stock "Pause" panel (we already show "Paused by <name>").
  Banner stands for CTF (BuildingTable.json): `BUILDING_BlueBannerStand`, `BUILDING_BannerStand` (red),
  `BUILDING_BlackBannerStand`, `BUILDING_WhiteBannerStand`, `BUILDING_YellowBannerStand` (Furniture / Decorative).

## 2. Mode rules

### 2.1 Fight (team vs team)
- Two teams, Blue and Red. Each player is on one team. Each player brings copied units (own save or wiki build, 2.3), up to the
  "units per player" rule.
- A round: both teams start at opposite ends of the arena floor. 3 s countdown. Fight.
- A unit is OUT when it is downed (Unconscious) or dead. No respawn in a round.
- Round win: the other team has no unit left that is not out.
- Round time (host setting 3 / 5 / 8 min, game ticks at 1x). Time out: the team with more total HP % left wins the
  round; equal = draw (no point).
- Match: best of 1 / 3 / 5 rounds (host setting). Each round starts from the same arena snapshot (section 4.4).
- At match end: result screen, then everyone returns to the world as it was before the match.

### 2.2 Capture the Flag
- Each team has a banner stand at its end of the arena (its "flag").
- Take: a conscious enemy unit stays within 1 cell of the flag for 1 s (60 game ticks at 1x). The unit carries it.
- Drop: the carrier goes out -> the flag lies on that cell.
- Return: a conscious unit of the flag's team touches a dropped flag -> it goes home at once.
- Capture: the carrier reaches its own banner while its own flag is at home -> 1 point.
- After each capture: the round resets (snapshot reload, same as Fight). No respawn in v1: out units stay out until
  the next capture. This removes the need for a revive + teleport API in the first build.
- Win: first to 1 / 3 / 5 captures, or most captures at time out (equal = draw). If one team is fully out, the other
  team still has to capture (it can walk in).
- Later (after M6): timed respawn at home (needs revive + in-map teleport; `MoveApplyData` ForceMove and Unconscious
  removal **[UNVERIFIED]**).
- Flag object: NOT a game item. Reason: items go into inventories and storage rules (gear modes, owner tags,
  pick-up orders) and a downed unit does not drop its inventory **[UNVERIFIED]**. The flag is a few bytes of our sim
  state (home / carried by Guid / dropped at cell). The home banner is a stock banner stand spawned into the arena
  snapshot with `SpawnBuilding` (team colour: Blue / red `BannerStand`). The carried or dropped flag is drawn by us
  (view only, our overlay like the name labels), so it never touches the sim.

### 2.3 Unit sources (owner decision 2026-10-14)
Each player picks his PvP units in the lobby from one of two sources (host setting: "Own units", "Wiki builds",
or "Both"). Units used in PvP are COPIES. The player's own save is only read, never written. The host's world is
restored after the match (4.4), so the copies vanish with the arena.
- A. Own units: a list of the units in the player's own saves (world name, unit portrait, name, level, class /
  skill trees). Pick up to the per-player limit. The player's copy of the game reads his own save file locally.
- B. Wiki build: a text field (stock unit-rename field style) "Paste a builder link". A link can hold a whole team
  (up to 8 builds); the player then ticks which builds to bring. The lobby shows each build as a unit card
  (race portrait, name, level, trees, gear icons) before Ready.
- Both sources end in the same thing: a validated unit SPEC that is sent to every peer as a stamped command and
  built the same way on every copy (4.8). Host fairness settings: level cap (none / 10 / 20 / 30 ...), "Wiki builds
  allowed", "Own units allowed".

### 2.4 Later modes (base maps, after Fight + CTF)
- Protect Base: one team defends a building (for example the Goddess statue / a core) for N minutes.
- Tower Defense: waves of stock monsters (`SpawnUnit` with Enemy faction) on a base map; players build.
  Both need base-map arenas and are out of this plan's build order.

## 3. Lobby settings (host)
Shown in the PvP lobby window, game font and frame, each row with a game-format tooltip (ui_tip.c).
- Mode: Fight / Capture the Flag (Protect Base, Tower Defense shown greyed "Later").
- Arena: list of tested arenas (region + floor + seed, section 4.3), or "Random tested arena".
- Teams: players join Blue or Red; host can move players; "Balance" button.
- Units per player: the existing rule; the lobby shows "Blue 2 / Red 2 units" and refuses Start if a team has 0.
- Pause: "No pause" or "Planning pause every 5 / 7 / 10 s", pause length "3 / 5 s or until all ready".
- Round time: 3 / 5 / 8 min. Fight: best of 1 / 3 / 5. CTF: captures to win 1 / 3 / 5.
- Speed: fixed 1x in PvP (speed votes dropped, existing `allow_speedup` = 0).
- Spectators: allowed / not (when the Spectator lane exists).
- Disabled during a match: lend control, admin mode, Teams changes, expedition proposals, story-event votes
  (no story events on the arena floor).

## 4. Code path per piece

### 4.1 Sim state and command (dsnet + new pvp_core.c)
- New `DSNET_PVP = 20`, host only, stamped like `DSNET_RULES`: payload = op (SETUP / START / PLAN_RESUME / END),
  mode, rules, arena id, team per player slot (u8 x 32). Server checks host; every peer applies at the same step.
- `pvp_core.c` (pure C, no game, like ord_core / trn_logic): team table by unit Guid (from owners at START),
  round clock, rounds, scores, flag states, planning-pause schedule. Inputs per sim tick: list of (Guid, team, cell,
  out). Outputs: events (round end, capture, flag taken). Unit tests in `pvp_test`.
- State goes into the mp save header block (mp_core `MP_HDR` area or a new tagged block) and into `g_fp`
  (fingerprint), so late join and resync carry it.

### 4.2 Hostility hooks (rig_hook, only active while the PvP state says "match")
1. `FactionRelationInfo.GetRelation(IEntity, IEntity)` 0x82BBA0: both are PvP units on different teams -> return 1
   (Hostile); same team -> original. Covers `IsRelation`, `IsAffectTarget` (skills, ally heals, auto skill choice in
   `DecidePlayerCombatBehaviour`).
2. `EntityExtensions.IsHostile` 0xFA4CE8: same rule.
3. `CombatDataHandler` hostile lists, after the original runs:
   - `OnSpawnUnit` 0xBCF690: for each unit of the other team already on the map (sorted by Guid): `AddHostile` both
     ways (same pattern as `ChangeToEnemy` 0xBCF104 lines: `AddHostile(new, other, pos)` and `AddHostile(other, new,
     pos)`).
   - `OnMoveUnit` 0xBD1930: `RefreshHostile(other, unit, newPos)` in every other-team unit's data (stock only
     refreshes hostile buckets, not the same bucket).
   - `OnDeadUnit` 0xBD00E8 / `OnDespawnUnit` 0xBD1EB8: `RemoveHostile` both ways.
   - At START: one "pair all" pass for units that spawned before the flag was set.
   - Room combat: `OnUnitEnterDungeonRoom` 0xBD1098 / `OnUnitExitDungeonRoom` 0xBD1410 add room hostiles by bucket
     too. Add `AddInRoomHostile` / `RemoveInRoomHostile` for other-team units in the same room. **[UNVERIFIED]**
     whether player AI needs room hostiles; test T1 decides.
4. At round start: `CombatStateApplyData(unit, InCombat)` (ctor 0x8457A0) for every PvP unit, through the same
   apply path the faction handler uses (`DataApplyRequested`). **[UNVERIFIED]** needed.
- Not in the first build: `FactionAggroData` (auto aggro areas), `AstarFactionHandler` (enemies may walk through
  each other **[UNVERIFIED]**), vision (both teams share Player vision, so no fog between teams). Listed as known
  limits in the lobby tooltip.

### 4.3 Arena floor
- Arena list = (region, floor, forced seed). A tool run (rig, one pair) generates candidate floors with a fixed seed,
  keeps seeds that generate fast (well under the Edgar timeout) and have two rooms far apart; the list ships in the
  source. Uses the existing `ChangeMapRequested` command path with `HasForcedDungeonSeed = 1`.
- While `pvp.arena_loading` (set by START before the transition command):
  - `HandleRandomMonsterSpawn` 0xCBA6BC and `HandleStaticMonsterSpawn` 0xCBAC94 -> return an empty list.
  - `CampaignEntityLoadHelper.SpawnDungeonNPC` 0xDA8B30 -> empty list.
  - `HandleStaticItemSpawn` 0xCBB6D4 -> empty list (no loot on the floor). Keep `HandleStaticBuildingSpawn`
    (walls, decoration, portals) **[UNVERIFIED]** that chests are buildings and harmless.
  - `DungeonBehaviorSimulator.Tick` 0xBC4D80 -> no-op for the whole match (no respawn, patrol, core, penalty).
  - `DungeonPortalHandler.Process` 0xF9E298 -> refused with a centred message "No portals in a PvP match".
  - `EntityEventHandler.CheckGameOver` 0xD854EC -> skipped (our rules end rounds).
- Placement: hook `InsertUnitTo` 0xDA7E44: replace `spawnPositions` with Blue cells (PlayerUnitSpawn cells of the
  Entry room) and Red cells (cells of the room with the longest path from Entry, usually the Portal room). Order
  = transfer order. **[UNVERIFIED]** that position i goes to unit i; fallback: after arrival, issue stock move orders
  (existing `g_move`) to each side before the countdown, and start the clock only when all arrived.
- With imported units (2.3, 4.8) the host's own world units do not fight. The stock transition still needs at least
  one unit in the party: the host sends one world unit as an "anchor", then inside the sim the PvP units are built
  on the arena floor (4.8.4) and the anchor is removed (`EntityLifecycleHelper.DespawnUnit` 0xC9998C). The world
  snapshot reload at match end brings the anchor back. **[UNVERIFIED]** that an arena floor with only spawned units
  behaves (party bar, camera, `PlayerDataHandler` lists). `InsertUnitTo` placement above is then only needed if
  owners also allow "fight with the units in this world" (not planned).
- After placement and before the countdown, the host takes the ARENA SNAPSHOT (4.4). CTF banners are spawned with
  `SpawnBuilding` 0xC93308 before the snapshot (inside a sim tick, so Guids come from the sim stream).

### 4.4 Snapshots and restore
- Before the match: the host saves the world as today's join-point save (`g_save`), kept in memory = WORLD SNAPSHOT.
- On the arena, after placement: ARENA SNAPSHOT (same path).
- Round reset = every peer loads the ARENA SNAPSHOT (`dsnet_upload_save` / `g_load`, the late-join / resync path).
- Match end = every peer loads the WORLD SNAPSHOT. Nothing that happened in the arena stays in the world
  (deaths, used potions, XP). Scores live outside the sim (local history list, like Recent worlds).
- Autosave is off during the match (host never saves the arena to disk). **Owner question:** keep "nothing changes"
  (recommended for v1) or let XP / loot come back (that is the Foreign Expedition settings set; reuse them later).

### 4.5 Rules step
- After each sim tick (`g_tick` -> `rig_det_sim_tick`), `pvp_step()` reads, for each PvP unit, its cell
  (`EntityDataContainer.GetWorldPosition` 0xC83C6C -> logical cell) and out state (`b__69_0` 0xD8DA20 conscious test,
  `_isDead`), feeds `pvp_core`, and acts on its events (round end -> host issues `DSNET_PVP` END-ROUND; capture ->
  same). The decision is computed on every peer; only the host sends the command; every peer checks the command
  against its own result (mismatch = desync report).

### 4.6 Pause rule
- No pause: PAUSE_VOTE dropped (existing pause_mode NONE). Disconnect and desync holds still pause (owner rule).
- Planning pause: `pvp_core` says "plan now" every N s of game ticks (deterministic). Each peer stops ticking at that
  tick (same mechanism as a hold: 0 game ticks, timer steps continue, orders still execute at their step - mp_core
  `step_ticks = 0` path). Resume = host sends `DSNET_PVP PLAN_RESUME` after the length or when all players pressed
  Ready. Orders given during the pause run at their stamped step, so both peers see them before the resume.
  **[UNVERIFIED]** that every stock order applies with 0 ticks (move orders set paths; skills may wait a tick).

### 4.7 UI (ui_game.c / mpmenu.c style, game font, game frames, our tooltips)
- Entry: base screen, Players panel cog -> "PvP match..." (host) and the Esc menu. Guests see "Host is setting up a
  PvP match" and the lobby read-only, except their own team and units.
- PvP lobby window (Teams window frame): left Blue column, right Red column, each with player rows (player colour
  dot, name, unit portraits); centre settings (section 3); bottom: Ready (players), Start (host, enabled when all
  ready). Start sends a proposal like the expedition proposal (Approve / Decline, 60 s).
- Loading: stock map-change wait, "Waiting for <name>".
- Countdown: centred message "3", "2", "1", "Fight!" (HelpNoticeView).
- HUD top centre: reuse `BossHUDView` look: name text = "Blue 1 - 0 Red   2:31"; the two bars = Blue and Red total HP.
  If BossHUD cannot hold two teams cleanly, a clone of `SubUI_CampaignClockUI`'s text box in our canvas. The stock
  clock shows the round time in place of the date.
- Planning pause: the stock big Pause panel, text "Planning - 4", Ready button under it.
- CTF: our flag icon above the carrier (team colour), a flag icon on a dropped cell, the home banner stand stays.
  Centred messages: "<name> took the Red flag", "Blue flag returned", "Blue scores".
- Round end: centred message "Blue wins the round" (2 s), then the reload wait screen.
- Match end: result window (DungeonResultView frame if reusable, else our Teams frame): winner, rounds or captures,
  per unit downs and damage (damage only if a stock counter exists **[UNVERIFIED]**), "Back to the world" button
  (host), guests wait.
- Units: enemy team names above units in their player colour (existing), own team normal. No grey-out of enemy
  units in PvP (they are not "others' units to protect", they are targets): the existing "not your unit" grey stays
  only for teammates' units.

### 4.8 Unit import: own save copy and wiki URL

#### 4.8.1 Game-side facts (dump truth)
- Save layout: `CampaignSaveData` (dump.cs 40205): `EntityContainerSaveData` (+0x0: per MapType
  `UnitContainerSaveData` {`UnitSaveDataDictionary` Guid -> `EntitySaveData` {Guid, Key}, `UnitSummonerDictionary`,
  `PlayerGuidList`, `AllyGuidList`}, item / building / thing containers, `EntityPositions`),
  `EntityComponentSaveData` (+0x38: Guid -> list of `ComponentSaveData` {Type, Data}, for example
  `EntityInfoComponentSaveData` faction / dead, `UnitProfileComponentSaveData`, `SkillComponentSaveData`).
  Skill trees are saved by `UnitSkillContainer` (183407 area) as `Dictionary<Guid, SkillTreeSaveData>`.
  Saves are JSON on disk (the mod already reads / writes them: `mp_sync.json`, `g_save`).
- Recruit-style spawn: `RecruitCandidateData` (32892): Level +0x10, RaceType +0x14, UnitProfileKey +0x18,
  NameTextKey +0x20, RaceTrait +0x30, BackgroundTrait +0x38, IndividualTraits +0x40, InscriptionsTraits +0x48,
  MajorStats +0x50 (StatType -> int), AptitudeStats +0x58, MajorStatTalents +0x60, MainSkillTrees +0x68,
  SubSkillTrees +0x70, InitialEquipments +0x78 (key, count), InitialInventoryItems +0x80. Spawned by
  `EntityLifecycleHelper.SpawnUnitFromCandidateData(map, data, pos, faction)` 0xC95C38 (also
  `UnitSpawner.SpawnPlayableUnit`).
- Learn a skill: `UnitSkillContainer.LearnSkill(Guid, string skill, int spCost)` 0xCC8B10.
- Equip an item with rolled tags: `ItemControlProcessor.EquipItem(map, unit, key, Dictionary<StatType,float> tags,
  amount, durability, autoFix, allowZero)` 0xD4D574. Or `EntityLifecycleHelper.CreateItem(map, key, tags,
  durability)` 0xC98D80 then equip.
- **[UNVERIFIED]** for all of these: that they run without UI side effects inside a sim tick, that Level in the
  candidate data gives the right stats and skill points, and whether they draw RNG (they would draw from the sim
  stream, so still deterministic, but the result could differ from the builder's preview).

#### 4.8.2 Wiki builder share links (~/dungeonsettlersdb/site/builder/builder.js)
Link forms the builder makes (builder.js 5846-5860, 6355-6420, api/shorten.js):
- Team link: `https://dungeonsettlers.wiki/builder?team=<code>` (also `#team=`). Short form
  `/build/<id>` or `/t/<id>` = an HTTP 302 to the long `?team=` URL.
- One-character link: `/builder#b=~<code>` (same binary codec, one build) or legacy `/builder#b=<base64url JSON>`
  (the whole builder state object); short links for these redirect to the `#b=` form.
Team codec (`encodeTeam` / `decodeTeam` / `dsbEncBuild` / `dsbDecBuild`, builder.js 445-690):
- `<code>` = base64url. v1 = raw blob; v2 = DEFLATE-raw of the blob. Blob = `[ver 1|2, fpHi, fpLo, unitCount
  (LEB128), units...]`. `fp` = 16-bit fingerprint of the index table sizes (race, background, trait, inscription,
  tree, skill, item, slot); a mismatch means "made on another data version".
- Values are LEB128 indices into KEY-SORTED tables of the wiki data (`dsbIdx`: RACE, BG, IND (traits), INSC, TREE,
  SKILL, ITEM, STAT, SLOT; SOUL and ACQ outside the fingerprint, ACQ order frozen by `D.acqOrder`).
- Per build: race, background+1, level (1-255), 6 talent tiers base-5 packed, flags (offer, roll, advanced,
  addShield, hasName, hasPortrait, hasManual, hasMajorBase, passive stacks, souls, saved traits, saved inscriptions,
  acquired traits, hideHead, elixirs, conditions), traits, inscription trees, learned skills, inscriptions,
  equipment slot mask + item index per slot, tag mask + per slot (tag index, value as a 0-255 fraction of the tag's
  [lo, hi] range), then the optional blocks in flag order.

#### 4.8.3 Mod import path (all LOCAL, outside the sim, until the spec is sent)
1. Paste: accept the four link forms. A short link is resolved with one HTTPS HEAD request to
   `dungeonsettlers.wiki`, reading only the `Location` header (Mac: system libcurl or NSURLSession; Windows kit:
   WinHTTP). No other request, never a POST, nothing uploaded. If offline: message "Open the link in your browser and
   paste the long address".
2. Decode in C (`pvp_build.c`, pure, unit tested): base64url, try raw blob, else raw inflate (bundle a small public
   domain inflate for both Mac and Windows; no zlib today in src), LEB128 reader, the same field order as
   `dsbDecBuild`, the same truncation check as `_decodeBlob` (reader past the end = reject). `#b=` JSON: small JSON
   reader for the fields the codec carries.
3. Index tables: generated at mod build time from `site/builder/data.js` by a node script that runs the builder's
   own `dsbIdx()` and writes `pvp_buildidx.h` (sorted key arrays + fp). Never hand-copied. If the link's fp differs
   from the header's fp: refuse "This build was made for another game version".
4. Validate against the GAME's own tables at runtime (DataSheetManager, keys must exist) AND game rules:
   race exists and is recruitable; background and traits valid for the race; level 1..game max, and <= host level
   cap; talent tiers valid; every learned skill is in one of the unit's trees and the total skill point cost fits
   the level; inscription count fits the slots; each item exists, fits its slot, and is allowed for the race / tree;
   tag indices exist on the item and values stay in [lo, hi]. REJECT, never clamp silently: manual stat overrides
   (flag bit 8: free stat values = a cheat in PvP), major-base overrides outside the race / talent roll range,
   elixir uses above the game cap. IGNORE (display only): name (kept, cut to 48 bytes), portrait, hideHead,
   conditions (hunger / exhaustion), passive stacks above what the skill allows. Each refusal names the field
   ("Skill X is not in this unit's trees").
5. Result = a canonical PvP unit SPEC (fixed C struct, game KEYS not indices, so peers never re-run the wiki
   tables): race key, background key, trait keys, level, talents, major stats, trees, skill keys, inscription keys,
   per slot item key + tag values (float bits), name.

Own save copy (source A) = the Foreign Expedition save library, reused as is (research/
plan_foreign_expedition_spectator.md, F1 "save surgery" and sections 1.2-1.5; code in ~/ds_multiplayer/ext/
foreign_expedition/). Nothing new is invented for PvP:
- Export (each player, local, title screen or lobby): FE 1.3. List `<game>/Saves/*.json` and the MP app saves
  (skip `mp_sync`, `MPFE_*`); refuse a save whose `CampaignSaveHeader.Version` differs from the running build; take the
  unit "closure" (FE table 1.2: `EntityComponents[G]`, `UnitSaveDataDictionary[G]`, `PlayerGuidList`,
  `EntityPositions`, `PlayerUnitsSaveData.*[G]`, level-up roll index, equipment / backpack item records); summons
  not exported; Task / Carry reset. The player's save file is only read.
- Send to the host: FE 1.4, the out-of-sim blob `BLOB_FE_UNITS` (closure JSON, zlib, max 1 MB) with an ack per
  unit. PvP uses the same packets with a PvP run id. Only the host parses the closures.
- PvP-only additions: the host level cap is checked on the closure; no results are ever written back (FE 1.7 / 1.8
  apply step is NOT used: PvP changes nothing at home).

#### 4.8.4 Build on every copy (deterministic)
- Source B: each player sends his specs with a new command `DSNET_PVP_UNIT` (payload = spec, max 60000 bytes per
  command; a spec is under 1 KB). Source A closures do not go through commands: they use the FE blob (4.8.3). The server stamps it; every peer stores it in the PvP state
  in arrival order. The host can see them; Start is refused until every ready player's specs are in.
- Source B (wiki build) is built INSIDE a sim tick at arena setup, on every peer, in a fixed order (team, player
  slot, spec index): fill a `RecruitCandidateData` from the spec -> `SpawnUnitFromCandidateData` on the arena map at
  the team's cells -> `LearnSkill` per skill (fixed order) -> equip each slot with its tags. All Guids and RNG come
  from the sim streams (rig_det), so every copy gets the same unit.
- Source A (save copy) is injected by the HOST into the arena save JSON with the FE library (FE 1.5 steps 1-4:
  in-memory copy of the host world, new campaign guid + save name, iron mode off, clan roster removed, closures
  injected with new Guids for unit and items). Two-stage so the 4-unit expedition cap is not in the path:
  (1) the host world copy goes to the arena floor with one anchor unit (4.3); (2) the host takes the arena snapshot
  JSON (it now holds the dungeon map), injects the closures on the `Dungeon` side at the team cells
  (`EntityPositions[Dungeon]`), removes the anchor, and every peer loads it through the existing join / resync path
  (`DSNET_JOIN_POINT` -> `dsnet_upload_save`). A save load is the same on every copy by design. **[UNVERIFIED]**:
  that injected units on the Dungeon side load clean (FE injects on Base only); fallback: inject on Base in the
  world copy, then send everyone through the arena transition (cap 4 units in total applies).
- Source B can ALSO be built this way (option B2): turn the validated SPEC into a closure JSON on the host and inject
  it like source A, so both sources share one path and one save load. The wiki repo already has code that turns a
  builder build into save records; reusing that LOGIC inside the mod would give the builder's exact numbers. It is the
  same code as the builder's save-upload feature, so: logic only, compiled into the mod, nothing in the mod names,
  links, opens or uploads to that page. **Owner to confirm B2** before anyone touches it; until then B1 (sim spawn,
  above) is the plan.
- Order of the arena build: source-B sim spawns run AFTER the source-A save load, inside the first sim ticks, then the
  arena snapshot is retaken for round resets.
- Ownership: every imported unit is owned by the importing player (`DSNET_OWNERSHIP` at START). Lend is off in PvP.
- After the build: one fingerprint check before the countdown (all peers equal, else abort the match with
  "Units did not match on all computers").
- The builder's save-upload page is not used, linked or named anywhere in the mod (owner rule).

## 5. Determinism risks
1. Hostility hooks must give the same answer on every peer: they read only the team table (sim state set by a
   stamped command) and Guids. Never the local player id.
2. Hostile-list injection order: add pairs sorted by Guid. Dictionary / list order inside `HostileEntityData`
   affects `_sortedHostiles` ties and closest-target choice.
3. View code also calls `GetRelation` (HUD, outline). Same result, no sim effect, but keep the hook free of
   allocation and logging in hot paths.
4. Arena generation: forced seed + pre-tested seeds; Edgar's `DateTime.Now` early stop (transitions_design 2.2) can
   still differ on a slow peer. Pre-test seeds on the slowest machine (CrossOver).
5. Rules step reads only sim data (cells, affecters, dead flag) inside the sim tick. No frame time, no real time.
   Round clock = game ticks. The planning-pause length is the only real-time part, and it ends by a stamped command.
6. Snapshot reload: same path as late join; must restore the PvP state block too, else peers disagree on scores.
7. CTF banner spawn uses `SpawnBuilding` inside the sim tick (Guid from the sim stream, rig_det). Never from UI code.
8. A missed hostility path (aggro, A*, room combat) is a gameplay bug, not a desync, as long as it is the same on all
   peers. A path that reads something local (camera / in-view flag, see PLAN.md 2026-10-13 research) IS a desync:
   all PvP players are on the arena map, so the in-view flag is the same for all.
9. Unit import: only the canonical SPEC crosses the network and the sim; the wiki tables, the URL text, the HTTPS
   lookup and the local save read stay local. Source B units are built inside a sim tick in a fixed order; source A
   units arrive through a save load. A spec that one peer cannot build (missing key) aborts the match on every peer
   (checked during validation by the host too), never a silent partial unit.
10. Float tag values travel as exact float bits in the spec (not as the 0-255 fraction), so every peer equips the
   same value.
11. Autosave off in the match; story events cannot start (no NPCs, no event rooms **[UNVERIFIED]** that Event rooms
   spawn nothing without NPCs).

## 6. Test plan
Unit tests (no game): `pvp_test` for pvp_core: team table, elimination, time-out HP rule, draws, best-of, CTF take /
drop / return / capture, capture with own flag away (refused), planning-pause schedule, command validation (non-host
dropped, bad payload dropped), save / load of the state block.
Pair tests (rig, lease one pair, max 2 pairs per HANDOFF_v9; scripted, hidden windows, nice 19):
- T1 Hostility spike on a normal dungeon floor, monsters killed first: 1 unit each, auto attack on; both units fight;
  fingerprints equal for 10 min; manual skill on enemy accepted, heal on enemy refused, heal on teammate accepted;
  right-click on enemy gives "attack".
- T2 2v2 (1 unit each): no friendly fire on teammates (AoE skills), log every GetRelation answer for 1 min and
  compare both peers.
- T3 Arena load: forced seed, no monster, no NPC, no item, simulator idle for 10 min, portal refused.
- T4 Placement: both teams at their ends on both peers (cells logged).
- T5 Fight round end + arena reload + next round; match end -> world snapshot reload -> world save hash equal to
  the pre-match hash.
- T6 CTF scripted: take, drop by downing the carrier, return, capture, reset.
- T7 Planning pause: cadence, orders during the pause, Ready resume, length resume.
- T8 Soak: 30 min 2v2 with scripted random orders, zero desync.
- T9 Rejoin mid-round: joiner lands in the same round state and score.
- T10 Mac host + CrossOver guest (Windows kit from the same source).
- T11 Unit import, no game: `pvp_build_test` decodes a corpus of real share links (v1 raw, v2 deflate, `#b=~`,
  `#b=` JSON) and compares field by field with the builder's own `decodeTeam` run in node on the same links; bad /
  truncated / manual-stat links are refused with the right message; fp mismatch refused.
- T12 Unit import, pair: the same wiki team built on both peers -> unit fingerprints equal; per-unit stats, skills
  and gear tags logged on both peers and compared with the builder page numbers for 3 builds (stat parity; the wiki
  already has a parity checker, pipeline/check_builder_parity.py, to reuse as the reference).
- T13 Own save copy, pair: a guest brings 2 units from his own save; host builds the arena save; both peers load;
  units equal; guest's save file hash unchanged; host world hash unchanged after the match.
Owner's eyes (no checker round): lobby, HUD, messages, flag icons, result window. Ship to him when gates are green.

## 7. Milestones (build order, sizes: S = under a day of one lane, M = 1-2 days, L = 3+ days)
| # | Milestone | Size | Depends on |
|---|---|---|---|
| M0 | Source copy (pvp branch of src), `DSNET_PVP`, `pvp_core.c` + `pvp_test`, state in save + fingerprint | S | - |
| M1 | Hostility spike: hooks 4.2 (1-3), T1 + T2 green | M | M0 |
| M2 | Arena floor: seed tool + tested list, spawn suppression, simulator off, portal block, game-over off, placement; T3 + T4 | M | M1 |
| M3 | Fight rules + world / arena snapshots + round reset + match end; T5 | M | M2 |
| M4 | PvP lobby, proposal, countdown, HUD, round / match messages, result window | L | M3 (can start UI shell in parallel after M0, separate files) |
| M5 | Pause rule (none / planning) + T7 | S | M3 |
| M6 | CTF: banners in arena snapshot, flag state, overlay icons, messages; T6 | M | M3, M4 |
| M3b | Unit import B (wiki URL): `pvp_buildidx.h` generator, `pvp_build.c` decode + validate + tests (T11), short-link HEAD lookup, sim build path (T12) | M-L | M0 (decode / validate need no game; sim build after M2) |
| M3c | Unit import A (own save copy): wire the FE save library (export, `BLOB_FE_UNITS`, inject) into the PvP lobby and the two-stage arena build (T13) | S-M | M3 + FE F1 |
| M7 | Soak T8, rejoin T9, Windows T10, release gate + WHATS_IN list | M | all |
| Later | Respawn in CTF (revive + teleport), team fog of war, aggro / A* per team, party above 4, Protect Base, Tower Defense, spectators in PvP | - | owner |

Parallel lanes (split by file): M0 (net + pvp_core) | M4 UI shell (new ui_pvp.c, mpmenu PvP page) | seed tool (rig
script). M1-M3 stay one lane (same hook file, order matters).

## 8. Open owner questions
1. After a match: world unchanged (recommended v1) or XP / items come back (Foreign Expedition settings)?
2. Team size: units are now built on the arena floor, so the 4-unit expedition cap is not in the path. Default
   per player = the "units per player" rule (1-6)? The party bar may still show only 4 (unverified).
5. Wiki builds vs own units in one match: allow mixing (host setting "Both", recommended) or force one source?
6. Short links need one HTTPS request from the game to dungeonsettlers.wiki (read the redirect only). OK, or long
   links only?
3. Fog of war between teams: none in v1 (both teams see everything). OK?
4. CTF without respawn in v1 (reset after each capture). OK?
