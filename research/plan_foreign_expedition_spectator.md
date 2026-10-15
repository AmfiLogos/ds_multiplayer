# Foreign Expedition + Spectator mode: design and technical plan (2026-10-14)

Scope: plan only. Build later in a separate source copy, NOT in lan_v9. Nothing here was run in the game.
Sources read: PLAN.md (all, "Next features" = owner decisions), HANDOFF_v9.md, src/UI_NOTES.md, research/discord/*.txt,
src/net/dsnet.h + dsnet_internal.h + dsnet_server.c (join / P_CMD), src/mp_core.c (JOIN_POINT), src/mp_game.c (save path),
src/ui_game.c (viewer 8h), research/ping_camera_v10.md, the Mac dump `~/ds_skill_truth/dump_25846389/dump.cs`,
two real EA saves (read only), and the wiki save generator `~/dungeonsettlersdb/site/builder/savegen.js` (it already
writes clan units into template saves; that is the closest prior art).

Marks: **[V]** = verified in this session (dump line, save file, or source). **[U]** = unverified assumption, with the
test that proves it. **[OWNER]** = experience tradeoff: needs the owner's choice (a default is given so work can start).

Save folder note: the folder named in the brief (`com.CanOpener.Dungeon-Settlers--Alpha-/Saves`) holds the old
Demo-era saves. The live EA saves are in `~/Library/Application Support/CanOpener/Dungeon Settlers/Saves/` **[V]**
(it also holds `steam_autocloud.vdf`: Steam Cloud syncs that folder). The MP app saves in
`~/Library/Application Support/CanOpener/Dungeon Settlers MP/data/Saves/` (rig/make_mp_app.sh) **[V]**.

## TLDR

| Change | Impact | Cost | Failure point |
|---|---|---|---|
| F1. Save surgery library (C + vendored yyjson): export a unit "closure" from a save, scrub, inject with new guids, apply results as a NEW save file | Units can travel between worlds with no game change; same file format the game writes | L (one lane, offline, unit tests on copied saves) | a field the game derives on load and does not re-derive (stats, holders) -> wrong stats; savegen already covers most of it |
| F2. In-game proof on one game copy | proves export -> hub -> load -> save -> apply -> load keeps the unit equal | M (1 pair) | load refuses an injected unit from an older save version |
| F3. dsnet: client-to-host blob (units), host-to-client blob (results), pending results kept per player uid | units reach the host before the join point; results reach each player once | M (pure C, net tests) | big blobs on Steam P2P (only TCP tested) |
| F4. Menus: host page "Foreign Expedition", hub picker + kit, 3 rules; guest "Bring units" window | the owner's flow in the game's style | L | reading saves from two folders (normal game + MP app), Windows paths |
| F5. Hub build + start: copy host's save, new campaign guid, remove its units, inject everyone's, kit it, load, owners load | one world all peers load through the existing join point | M | a copied iron-mode world deleting the host's real saves (handled: new campaign guid) |
| F6. End expedition, extract results, deliver, results window, apply once (ledger + in-save marker) | units go home with XP / items per rules; never applied twice | L | Steam Cloud conflict when we add a file in the normal game's Saves folder |
| S1. dsnet role "spectator": not in present mask, cannot send sim commands, never holds the timer, host rule on/off | spectators see all, change nothing | M | `present_mask` is used by every vote/ready/population rule: regressions |
| S2. Spectator join without a host reload (last join-point save + command log replay) | a spectator joining does not pause or reload the players | M | replay from the last join point may not match the host's memory -> spectator desync loop |
| S3. Spectator UI: "Watch" buttons, watching banner (viewer box), panel group, read-only popups, chat + map ping | the owner's flow | M | camera follow needs the lan_v10 out-of-sim camera packet |

Build order: F1 and S1 can run in parallel (different files). Then F2, F3 (dsnet: after S1 or in the same lane, same
files), S2, F4 + S3 (menus: mpmenu.c / ui_game.c, split by file), F5, F6, pair tests.

## Owner decisions needed (defaults used by this plan)

1. **[OWNER] Death permanent = On: what happens to the home unit's own gear?** Default: the unit AND the gear it wears
   at home are removed (like a death in a dungeon). Option: the gear goes to the home storage.
2. **[OWNER] Death permanent = Off and the unit died: does XP gained before the death count?** Default: no, the home unit
   stays as it was (the dead copy's record is gone from the world; we have nothing to copy back).
3. **[OWNER] Items come back = On: which items?** Default: every item tagged to you in the hub (dungeon loot AND items you
   crafted there from the hub's stock). Option: dungeon loot only (needs a "found in dungeon" tag, +S work). Note: with a
   fully kitted hub, the default lets a player craft top gear from hub stock and take it home.
4. **[OWNER] Where do returned items go?** Default: into free slots of a matching storage building at home; what does not
   fit goes into the unit's backpack; what still does not fit stays in the result and can be claimed later.
5. **[OWNER] Apply writes a NEW save** next to the original ("<Clan> - back from expedition"), the original is never
   modified. Default yes (it is also the backup). Option: modify the newest save in place after a backup.
6. **[OWNER] Spectators: maximum number?** Default 8. And may a spectator switch to player mid-game? Default no (later).

## Part 1. Foreign Expedition

### 1.1 User flow (game style: notice-box frames, Cinzel titles, Lora body, cog = ITEM_CogWheel, game tooltips)

Host:
1. Title > Multiplayer > Host page. New first row "World": `‹ Your save | Foreign Expedition ›`.
   Tooltip: "Foreign Expedition: everyone brings units from their own saves into one hub world. Your saves are not changed
   until you choose to bring the results home."
2. Foreign Expedition section (replaces the save list):
   - "Hub world: <save name> ‹ ›" (the host's saves, only those at the settlement: header `CurrentMap` = 0).
   - "Kit the hub" toggles (each with a tooltip): "All research done", "All regions open", "Stock the storage".
   - Rules (in the Rules box, with the existing ones): "Death: Permanent / Not permanent", "Experience: Counts /
     Does not count", "Items: Come back / Stay in the hub", "Units per player" (existing rule 1/2/3/4/6).
   - Gear mode is fixed to "Own storage" and shown greyed: "Fixed in a Foreign Expedition: items must know their owner."
   - "Population" shown greyed "Off: recruits cannot leave the hub".
3. Lobby (existing). Each player row gets a second line: "2 units brought" (green) / "Choosing units..." (muted) /
   "Refused: <reason>" (red). "Start" stays greyed until every present player brought at least one unit
   (tooltip "Waiting for Ben to bring units").
4. Start: centred message "Building the hub world..." (HelpNoticeView), then the normal load. All peers load through
   the existing join-point path.

Every player (host too):
1. Lobby button "Bring units" (opens a standalone window, game font, like Teams):
   - Left: "Your saves" list: both folders (normal game, MP app), newest first: clan name, day, version.
     A save from another game build shows greyed: "Made with <version>. Load and save it once in this build first."
   - Right: the clan units of the chosen save: portrait, name, level, main trees, a check box. Counter "2 / 3".
   - Bottom: "Bring these units" / "Cancel". Text: "Copies travel. Your units stay at home and keep working."
2. In the hub: normal multiplayer (Teams, expedition proposal, max 4 units per dive, lend, chat).
   Own units are the injected copies; the Teams window shows "From <clan name>" under each name.
3. Recruitment window in the hub: hire disabled with the line "Recruits cannot leave the expedition world."

End (host):
1. Player panel cog > "End the Foreign Expedition" (host only; greyed in a dungeon: "Return to the settlement first").
   Confirm box (CommonNoticeView Yes/No): "End the expedition for everyone? Each player gets their results."
2. Every peer: game pauses (hold), centred message "The expedition is over".
3. Results window per player (standalone, game font): title "Back from the expedition".
   Per unit row: portrait, name, "Level 4 -> 6", "+1 240 XP" (or "Experience does not count"), "Died" (red, with
   "Removed from your settlement" or "Comes home as before", per rule), "7 items" (expand: item icons).
   Target line: "Bring home to: <clan> (<save name>) ‹ ›" (default = the save the units came from).
   Warning row when the home unit changed since it left: "Josephine changed at home since she left (level 5 now).
   Bring home: ‹ Expedition version | Home version ›".
   Buttons: "Bring home now" / "Later" (tooltip "Results stay in Multiplayer > Expedition results").
   After apply: "Saved as '<Clan> - back from expedition'. Load it from Load campaign. Your old save is unchanged."
4. Title > Multiplayer > "Expedition results" lists unclaimed results (run, date, hub host, units). Claimed ones show
   "Brought home <date>" and cannot be applied again.

### 1.2 What a unit is in a save (closure) **[V] dump + 2 EA saves**

Root `CampaignSaveData` (dump.cs:40207). A clan unit with guid G on side S (`MapType`, "Base" / "Dungeon") is:

| Path | Dump type | Notes |
|---|---|---|
| `EntityComponentSaveData.EntityComponents[G]` | `Dictionary<Guid, List<ComponentSaveData>>` (40477) | component list, Type: 0 Status, 1 EntityInfo, 3 Affecter, 4 Combat, 5 Inventory (backpack), 6 Inventory (equipment), 7 Task, 8 Skill, 9 Carry, 10 UnitProfile, 15 TradeData (seen in saves) |
| `EntityContainerSaveData.UnitContainerSaveData[S].UnitSaveDataDictionary[G]` | `EntitySaveData {Guid, Key}` (40739) | Key `UNIT_PlayerUnit` |
| `...UnitContainerSaveData[S].PlayerGuidList[]` | `List<Guid>` (40752) | clan membership |
| `EntityContainerSaveData.EntityPositions[S][G]` | `SerializableVector2` | |
| `PlayerUnitsSaveData.{SkillTrees, SkillLearnNotices, QuickSlots, ConsumePolicies, TaskPriorities, WayPointQueues, RecruitCosts}[G]` | (189255) | all 7 seen per unit |
| `CampaignRandomSaveData.LevelUpPrayerAttemptIndexByUnitGuid[G]` | `Dictionary<Guid,int>` (40349) | iron-mode level-up roll index |
| `ReportSaveData.LevelUpTimeData.UnitExpMaxStamp[G]` | (40330) | |
| `AnimationStateSaveData.States[S][G]` | (32398) | savegen writes none and its files load **[U, savegen evidence]** |
| Items: every non-zero `ItemSlots[].Guid` / `TempSlot.Guid` of components 5 and 6 = I | `ItemHolder {Guid, Key, Amount, AutoFixed}` (29661) | |
| `EntityComponents[I]`, `ItemContainerSaveData[S].ItemSaveData[I]`, `.ItemAutoFixable[I]` | (40778) | |

References TO the unit from world records (scrub on removal, never exported): `BedOwnerSaveData.BedOwnerMap` (value),
`RecreationSaveData.Recreations[].PerformerGuid`, `EventSaveData.EventSourceData.EventInstanceSaveData.*.TargetUnits[]`,
`UnitSummonerDictionary` (summons, e.g. `UNIT_Bubbleling`, owned by the unit), `GraveOwnerSave.GraveOwnerMap` (dead
units: grave guid -> `{OwnerGuid, NameTextKey}`, 180338). All seen in real saves **[V]**.

Dead units: the game removes them from the containers; only a grave entry and a `UnitExpMaxStamp` stay **[V: 3 stamps
for guids with no unit in ElvenExpedition]**. So "died in the hub" = the hub guid is no longer in any `PlayerGuidList`
at the end.

Game code that reads/writes it (names from dump, RVAs from dump_25846389; never call by guessed address):
`SaveLoadHelper.SaveFile` 0x87573C (Newtonsoft, TypeNameHandling, `$type` tags kept in the file),
`SaveLoadHelper.ExtractSaveData(string)` 0x8753CC, `SaveLoadHelper.GetAllSavePaths` 0x87262C,
`SaveLoadHelper.RefreshSaveCache` 0x8711D4, `CampaignSaveLoader.TryLoad` 0x8143CC, `IsValidLoadData` 0x814718
(file stem must equal `SaveName`, MP_NOTES), `CampaignSaveLoader.Save` 0x814890 (already hooked as VA_SAVE),
`CampaignDataContainer.Serialize/Deserialize` 0xC724EC / 0xC74C40, `UnitDataContainer.Deserialize` 0xC85AD0,
`PlayerUnitDataContainer.Deserialize` 0xCC26DC, `InGameRecordContainer.Deserialize` 0xB67C54.

Decision: **all unit moves happen on the JSON file, outside the game, before a load.** The game's own loader then
builds the entities. No runtime unit spawning, no new hooks for injection. This is exactly how savegen works.

### 1.3 Export (each player, on the title screen, local)

1. List saves: `<normal game>/Saves/*.json` and `<MP app>/data/Saves/*.json` (skip `mp_sync`, `MPFE_*`). Read headers
   only (stream the first object; `CampaignSaveHeader`). Windows paths **[U]**: `%USERPROFILE%/AppData/LocalLow/CanOpener/
   Dungeon Settlers/Saves` (guess from the old CrossOver install; check on the Windows kit).
2. Refuse when `CampaignSaveHeader.Version` != running build version (the loader migrates per save, a single injected
   unit from an older save may skip it; `StatusComponentSaveData.SchemaVersion` = 2 today) **[U]**.
3. Closure = table 1.2 rows for the chosen guids, plus a manifest:
   `{export_id (random 16 bytes), player_uid, home_campaign_guid, home_save_name, home_save_version,
     unit_guid, closure_hash (FNV-1a 64 of the canonical closure), unit name, level}`.
4. Summons are not exported. Task (7) and Carry (9) reset like savegen `resetTask` / zero guids. Backpack kept.
5. Keep the manifest locally: `<MP data>/fe/exports/<export_id>.json` (needed to check results later).

Size: 14-20 KB JSON per unit, 2.4-2.8 KB zlib **[V: 4 units measured]**. 6 units < 130 KB raw.

### 1.4 Network: units to the host (outside the sim, before the join point)

The lobby already connects guests on the title screen with `need_save` (mp_game.c:540) **[V]**. New out-of-sim traffic:

| Packet / blob | Direction | Payload | Rules |
|---|---|---|---|
| `P_FE_MODE` | s->c (in WELCOME tail and on change) | u8 on, run_id[16], u8 death, u8 xp, u8 items, u8 units_per_player, hub display name | host sets it in the lobby |
| `P_BLOB` kind `BLOB_FE_UNITS` = 2 | c->s | run_id[16], u8 n, per unit: export_id[16], u32 len, closure JSON (zlib) | today `P_BLOB` is host->client only (BLOB_SAVE); reuse `dsnet_link_send_blob` the other way, 1 KB fragments. Max 1 MB. A new send replaces the old one |
| `P_FE_UNITS_ACK` | s->c | run_id, u8 n, per unit: export_id, u8 status (0 ok, 1 wrong run, 2 over the unit rule, 3 bad data, 4 wrong version) | the host's game thread validates (parse, version, count) and answers through dsnet |
| `P_FE_STATE` | s->c (all) | per player: u8 slot, u8 units_ok, u8 choosing | drives the lobby lines |

Host API sketch (dsnet.h): `dsnet_fe_units_poll(c, &from_uid, &data, &len)`, `dsnet_fe_units_ack(c, uid, ...)`,
`dsnet_fe_send_units(c, run_id, data, len)`. The server relays to the host's own client; it never parses JSON.

### 1.5 Hub build (host, at Start, before the load)

1. Copy the chosen host save in memory. Never touch the original file.
2. New identity **[V reason]**: new `CampaignSaveHeader.CampaignGuid` and `ClanSaveData.CampaignGuid`; `SaveName` =
   file stem `MPFE_<run8>`; `DisplayName` "Foreign Expedition <date>"; `IronModeEnabled` false in both places
   (header + `CampaignSettingSaveData.CampaignStartSettingSaveData`, memory DungeonSettlers.md). Why: in iron mode
   `KeepOnlyRetainedCampaignSave` (0x814E88) deletes the campaign's OTHER saves on an autosave; with the host's campaign
   guid, the hub's first autosave would delete the host's real saves.
3. Remove every clan unit of the copy (savegen `scrubRoster` + the extra references in 1.2) and their summons.
4. Inject every accepted closure on side Base: new guid for the unit and each item (remap inside the closure), positions =
   the removed units' old positions first, then savegen `nextFreePos` **[U: walkable cell]**; `EntityInfo.Faction` 1,
   `IsDead` false; `ClanSaveData.CurrentPopulation` = clan unit count. Keep a map
   `hub_guid -> (player_uid, export_id, home_guid)` and the item map in `<MP data>/fe/runs/<run_id>/manifest.json`.
5. Kit (each a toggle): research = every `ResearchSaveData.ResearchDatas[*].Workload = RequiredWorkload`,
   `IsLocked = false` (keys from the game's ResearchTable, not only those present) **[U: missing keys get created by the
   loader?]**; regions = `ClanSaveData.ExpeditionMapData[r].IsCleared = true` for every region before the last
   released one (unlock is derived from IsCleared on load, memory RegionBlock) **[V memory, U this build]**; storage =
   add item stacks to existing storage buildings' Type-5 slots (shape measured like savegen `measureItems`).
6. Write `<MP data>/Saves/MPFE_<run8>.json` (tmp + rename), `rig_load_save_file`, then the existing host start
   (`mp_steam_host(save)` path). Owners: build the dsnet owner export (units + items -> player uid) and send it with
   `dsnet_import_owners` -> `DSNET_OWNERS_LOAD` (existing, host only) **[V API]**. Rules: `DSNET_RULES` with gear mode
   OWN_STORAGE.
7. Guests get the hub through the normal join point (`DSNET_JOIN_POINT` -> `dsnet_upload_save`, 64 MB max) **[V]**.
   A base world is 1-8 MB **[V: save sizes]**.

### 1.6 During the run

- Loot attribution: OWN_STORAGE mode already tags crafted / looted items to the issuer inside the replay
  (`dsnet_item_set_owner` in a callback) and a lent unit's pickups to the OWNER (lan_v9 decision) **[V dsnet.h, PLAN]**.
- Recruit: refuse in the replayed recruit order when FE is on (same place as the population check in ord_mp.c), so every
  peer decides the same.
- Hub autosaves stay host only (existing guest save block). Rejoin mid-run works as today (stable uid).
- A player who leaves for good: his units become reserved (existing). His results are still computed at the end.

### 1.7 End and results

1. Host presses End: new sim command `DSNET_FE_END` = 20 (host only, payload run_id). At that step every peer: hold
   (`DSNET_HOLD` reason OTHER), freeze orders, show the message. Nothing after this step matters to the sim, so the
   host's extraction save cannot hurt determinism.
2. Host: `CampaignSaveLoader.Save("MPFE_<run8>_result")` (same call as the join-point save, through the
   KeepOnlyRetained skip), read the JSON, delete the file. For each manifest row: alive (hub guid in a PlayerGuidList)
   or dead; closure at the end; items tagged to that player (dsnet item owner table) that are NOT in the export set.
3. Result per player: `{run_id, player_uid, rules, host name, per unit: export_id, home_guid, status, closure_end,
   xp_before/after, level_before/after}, items: [closure of each item], result_hash}`. Stored on the host:
   `<MP data>/fe/runs/<run_id>/results/<uid>.json` until the player confirms receipt.
4. Delivery: `P_BLOB` kind `BLOB_FE_RESULT` = 3, s->c to that player only; `P_FE_RESULT_ACK` c->s (stored). Absent
   player: the host keeps it and sends it in the WELCOME tail the next time that uid connects to this host (any world)
   **[U: friend must reconnect to the same host]**.
5. Guest checks: run_id known (a local export manifest with this run exists), every export_id is one of its own.
   Otherwise refuse ("These results are not for your units").

### 1.8 Apply to the home save (each player, local, any time)

Order of checks (refuse with a clear line, never partial):
1. Ledger `<MP data>/fe/claims.json`: run_id already claimed -> "Already brought home on <date>."
2. Target save: default = the newest save whose header `CampaignGuid` = the manifest's `home_campaign_guid`. Its
   version must be the running build.
3. In-save marker: `ReportSaveData.InGameRecordData.Counts["mp.fe.<run_id>"]` present -> refuse (protects against a lost
   ledger). `Counts` is a free `Dictionary<string,long>` with game keys like `unit.killed.UNIT_RedStoneGolem` **[V]**;
   that the game keeps an unknown key through load + save is **[U]** (test F2.5).
4. The normal game must not be running with that save open: check the process list for the game app; refuse "Close
   Dungeon Settlers first" **[U: process name per platform]**.

Merge per unit (home guid H, export closure E, expedition end closure X, current home closure C):
- Home unit missing (died at home since) -> skip, line "Not in this save anymore".
- `hash(C) != hash(E)` (changed at home) -> the player picks per unit: Expedition version / Home version.
- Dead + Death permanent -> remove H closure + references (1.2) (gear per owner decision 1).
- Dead + not permanent -> keep C.
- Alive + XP counts -> replace in C: component 0 (ProgressionStats, GeneratedStats, PermanentStats, Stats,
  SchemaVersion), component 3 (AffecterHolders: level-up inscriptions and the level holder), component 8 (skill lists),
  `PlayerUnitsSaveData.SkillTrees / SkillLearnNotices / QuickSlots`, `LevelUpPrayerAttemptIndexByUnitGuid`,
  `UnitExpMaxStamp`. Keep C's equipment, backpack, position, task, consume policy, task priority, name, portrait.
  StateStats (health, hunger, stress) keep the home values **[U: Stats are baked; replacing the whole status block keeps
  them coherent, savegen + memory "game does not recalculate Stats from affecters on load"]**.
  Items worn in the hub that are not in E stay out of the equipment (they come back as items if the rule says so).
- Items come back -> new item entities (new guids) into storage / backpack (owner decision 4).
- Write: marker into Counts, header `SaveName` = new stem, `DisplayName` "<Clan> - back from expedition",
  `IsAutoSave` = the iron flag (as rig_save_campaign does). New file next to the original (tmp + rename). Then ledger.
  Backup: the original stays untouched; also copy it to `<MP data>/fe/backups/<stem>-YYYY-MM-DD-HHMM.json` (never inside
  Saves/, memory SaveBackupLocation).
- Steam Cloud: a new file in the normal game's Saves folder while the game is closed **[U: no "cloud conflict" box]**
  (test F2.6). Fallback: write into the MP app's Saves and tell the player to load it there.
- The save list cache (`SaveHeaderCache.json`, keyed by file length + write time, `CampaignSaveFileState`, dump.cs struct after `CampaignSaveCache`) should
  pick the new file up by itself (`RebuildSaveHeaderCache`) **[U]**.

Idempotent: same run twice -> refused (ledger + marker). Same export used in two runs -> second apply sees
`hash(C) != hash(E)` and asks. Cloning by copying files to another Mac: accepted by the owner.

### 1.9 Edge cases

- Two players bring units with the same guid (same ancestor save, e.g. a wiki template): always remapped -> no clash.
- A player brings 0 units: may still join? Default: no, "Bring at least one unit" (spectators cover watching).
- Guest sends units, then leaves the lobby: host drops them; on rejoin he sends again.
- Host's chosen hub save is in a dungeon (`CurrentMap` != 0): not listed.
- Hub save from an iron campaign: handled by the new campaign guid + iron off.
- A unit at home levelled during the run: conflict choice (1.8).
- The host crashes mid-run: the hub autosave + manifest let the host reopen "Foreign Expedition <date>" from Recent
  worlds and continue; End works later. Results are never computed from anything but the live hub.
- Version update of the game between export and apply: apply refuses saves of another version; results stay pending.
- Units per player lowered by the host mid-run: existing rule (overflow kept).
- Results for a dead unit with "Items come back": its items are gone with it (dropped in the dungeon), only items in
  storage tagged to the player come back.
- Hub world with no beds / food for 12 injected units: kit "Stock the storage" helps; beds are the host's pick of save.

### 1.10 Test plan

Offline (no game, ASan): `src/fe_test/` with fixtures COPIED from saves into the scratch folder (never Saves/):
export -> inject -> extract round trip equals (guids remapped), scrub leaves no dangling guid (walk the whole JSON for
the old guids, like the reference scan in this research), apply twice refused, marker written, conflict detection.

Game (rig, never the real install / saves; lease pairs, max 2):
- F2.1 one copy: inject 2 units from save A into hub from save B, load, `ui_items` / a new `fe_unit {guid}` rig command
  dumps level, majors, skills, holders, equipment: equal to the source.
- F2.2 save the hub, extract, closure equal (except guids, task, position).
- F2.3 apply a modified closure (level +2) as a new save, load it: unit shows level +2, stats coherent.
- F2.4 death apply: unit gone, bed / recreation / event references gone, load clean (rig.log has no NRE).
- F2.5 marker survives a load + save of the applied file.
- F2.6 Steam Cloud: only with the owner's OK, on a throwaway campaign; else mark as owner test.
- Pair FE-1: host + guest, guest brings 2 units, host 1, Start, both load, fingerprints equal for 3 min.
- Pair FE-2: one dive (existing dungeon driver), loot, End, both get results, guest applies, second apply refused.
- Pair FE-3: guest disconnects before End, reconnects later to the host, gets pending results.
- Pair FE-4: refusals: wrong version save, too many units, tampered run id.

### 1.11 Milestones (build order, sizes for one lane: S < half a day, M about a day, L 2-3 days)

| # | Milestone | Size | Files |
|---|---|---|---|
| F1 | fe_save.c/.h + vendored yyjson: list saves, export closure, scrub, inject + remap, kit, apply, marker, ledger; fe_test | L | new files only |
| F2 | in-game proof F2.1-F2.5 with a rig command `fe_*` | M | rig_cmd.c (small), fe_save.c |
| F3 | dsnet: P_BLOB c->s, BLOB_FE_UNITS / BLOB_FE_RESULT, P_FE_* packets, pending results per uid, DSNET_FE_END; net tests | M | net/* |
| F4 | menus: host page mode, hub picker, kit toggles, FE rules, "Bring units" window, lobby lines, Expedition results list | L | mpmenu.c, mpmenu_model.c |
| F5 | hub build at Start, owners load, gear mode fixed, recruit refusal in replay | M | mp_game.c, ord_mp.c, fe_save.c |
| F6 | End, extraction, delivery, results window, apply UI | L | ui_game.c (or new fe_ui.c), mp_core.c |
| F7 | pair tests FE-1..4, Windows kit paths, tooltips (game format) | M | tests |

## Part 2. Spectator mode

### 2.1 User flow

- Join page (title > Multiplayer > Join by address) and Recent worlds: two buttons per entry: "Join" and "Watch".
  Tooltip on Watch: "Watch the game: you see everything and can chat and ping, but you own no units and cannot pause
  or vote."
- Steam invite accepted: the stock accept starts our join page with the friend's world and both buttons (no auto join).
  **[U: the current Steam join flow can show a choice before it connects; check mpmenu.c Steam join path]**.
- Host page Rules: "Spectators: Allowed / Not allowed" (default Allowed). Turning it off in play: confirm box
  "Remove the 2 spectators now?"; they get "<Host> turned spectators off" and drop to the title.
- In game (spectator): the viewer box variant at top centre: "Watching the owner's world" + "You are a spectator. You can chat
  and ping." Buttons: "Follow <player> ‹ ›" / "Free camera". No "Ask to lend".
  - Clock: speed and pause buttons hidden (tooltip on the clock line: "Spectators cannot pause or change speed").
  - Portraits: all units shown normal (no greying: nothing is "yours" to contrast). Character window read-only with the
    banner "Spectator: view only". Skill bar greyed (existing foreign-unit state, text "You are watching").
  - Teams window: read only, no Take / Free / Lend buttons, title "Teams (watching)".
  - Popups: story popups open read-only ("The owner is choosing"), no vote; level-up / trade never; map-change Yes/No and
    expedition proposal never open, status line shows "The owner wants to go down to floor 3"; Return screen read only.
  - Esc menu: Save hidden, "Leave" only; Esc does not pause anyone (as today).
- Player panel (everyone): players as today; below a divider "Watching (2)" with an eye-style icon row per spectator
  (name in a neutral grey, connection bars). Spectators get no player colour from the 8-colour palette (so the palette
  stays for players) **[OWNER-light: lead default]**.
- Chat: spectator lines "Ana (watching): ..." in grey; "is typing" works. Map ping: allowed, in grey, same spam limits.

### 2.2 Data and code path

dsnet (net/*):
- `P_HELLO` gains u8 role (0 player, 1 spectator) at the end (old clients: absent = player). `DSNET_PROTO` 2.
- Server `sconn.spectator`. Refuse role 1 when the rule is off or the cap is reached: new `DSNET_REJ_NO_SPECTATORS`,
  text "This world does not allow spectators."
- Slots: spectators get a slot id (so chat / players list work) but the PLAYER_JOIN payload gets a role byte; the sim
  state keeps `spectator_mask`. `dsnet_present_mask` returns players only; new `dsnet_spectator_mask`. Every rule that
  counts present players (pause vote, ALL_VOTE, ready / EXPEDITION_START, population SPLIT/MULTIPLIED, story votes,
  map votes, proposal approvals) keeps using present_mask, so it ignores spectators by construction **[V: those rules
  use present_mask per dsnet.h comments]**. Audit every `present` / `DSNET_MAX_PLAYERS` loop in dsnet_client.c,
  ui_model.c, ui_teams_model.c, mpmenu_model.c for direct slot loops.
- `on_frame` P_CMD (dsnet_server.c:180-): if `c->spectator` drop every type. Spectators still send P_KEEPALIVE,
  P_FRAMETIME, P_FP? (no: they compare only), P_DESYNC, P_RESYNC_REQ, P_READY, P_PONG, P_CHAT, P_TYPING, and the
  lan_v10 camera / map-ping packets.
- Timer: exclude spectators from `max_behind` / `ok_behind`, from `busy_mask` holding, from `auto_pause` on their join,
  desync and leave, from `DSNET_HOLD PLAYER_LEFT`. A spectator more than N steps behind (e.g. 600) asks for a resync.
- Rules: `DSNET_RULES` byte 7 `allow_spectators` (shorter payload keeps it). Off -> server sends P_BYE reason 2
  ("spectators off") to each spectator and closes.
- WELCOME tail: role echoed, so the client knows.

Join without disturbing players (S2):
- Today a late join issues PAUSE_ALL (if the pause mode allows) and a new join point; only the host reloads its own
  save; the joiner gets the save + the log after it (`send_save_to`, `send_log_from`) **[V dsnet_server.c:107-173,
  mp_core.c:168]**.
- For a spectator: if the server holds a join-point save (s->save) and the log since it is under a limit (e.g. 20 min of
  steps), send that save + the log, NO new join point, NO pause, NO host reload. The host's memory = that loaded save +
  the same commands, so the replay should match **[U: requires the logged commands since that save to be complete and
  the det state saved with it; test S2]**. Else fall back to a normal join point without PAUSE_ALL (the host reload
  hitch is then visible to the players: one short load) **[OWNER-light: accept the hitch?]**.
- Catch-up: the spectator runs several steps per frame (`dsnet_steps_this_frame`) until it is live.

Game side:
- `ui_check_order` / `may_order`: spectator -> refuse every order locally with "You are watching".
- Viewer box (ui_game.c 8h, `viewer_frame`): `v = 1` always for a spectator, buttons Follow / Free only.
  Follow cannot use UI_ORDER_FOLLOW / UI_ORDER_CAMERA (sim orders, a spectator may not send them): use the lan_v10
  out-of-sim camera broadcast (research/ping_camera_v10.md: camera and ping travel outside the sim) and move the camera
  locally. **Dependency: lan_v10 camera packet.**
- Hooks already present: `PopUpViewContainer.ActivateView` (LevelUp / Trade not opened), story read-only path,
  `CommonNoticeView` map vote, DungeonResultView read-only, Esc menu Save greyed: add "spectator" as one more reason.
- Story pause: a spectator's popup must not send `DSNET_STORY_PAUSE` (server drops it anyway).
- Saves: a spectator never writes the world (existing guest save block).

### 2.3 Edge cases

- Host turns spectators off while one is loading: BYE at once; his load is aborted (existing leave path).
- A spectator desyncs: resync only him; players see nothing (no toast) except his row "resyncing" in "Watching".
- A spectator with a slow Mac: falls behind, never holds the game; past the limit, resync.
- 32 slots total (DSNET_MAX_PLAYERS): spectators count toward it; the cap rule keeps room for players.
- Lobby before Start: spectators wait on the title with "Waiting for the host to start" (need_save path) and load with
  everyone.
- Foreign Expedition + spectators: allowed; a spectator brings no units and gets no results.
- Event choice "First to answer" / "Majority": spectators excluded (present_mask).
- A spectator who was a player before in this world (same uid): his units stay reserved for him as a player; as a
  spectator he cannot order them. Host Admin mode rules are unchanged.

### 2.4 Test plan (3 game copies = 2 leased pairs, max 2 pairs)

- net tests: spectator commands dropped; masks equal on every client over 200 random command streams (like the ui
  determinism test); timer not held by a spectator 300 steps behind; allow_spectators off kicks; reject code text.
- Pair SP-1: host + spectator: spectator joins mid-game, host does NOT reload (rig.log), no pause, fingerprints equal
  after catch-up, spectator orders refused locally and on the server.
- Pair SP-2 (3 copies): host + guest + spectator: pause vote and map vote work with 2 players; story popup read-only on
  the spectator; population SPLIT counts 2; spectator leaves: no disconnect popup, no hold.
- Pair SP-3: host turns spectators off in play: spectator back on the title with the text.
- Pair SP-4: spectator Follow camera (after lan_v10 camera packet), chat, map ping visible to players.

### 2.5 Milestones

| # | Milestone | Size | Files |
|---|---|---|---|
| S1 | dsnet role, masks, command drop, timer / pause / hold exclusion, rule + reject + BYE, net tests | M | net/* |
| S2 | spectator join from the last join-point save + log (fallback join point without pause), SP-1 | M | net/dsnet_server.c, mp_core.c |
| S3 | UI: Watch buttons, Rules toggle, spectator viewer box, panel "Watching" group, read-only states, chat label | M-L | mpmenu.c (menus), ui_game.c (in game), ui_teams.c (read only) |
| S4 | pair tests SP-1..4, tooltips | M | tests |

## Three biggest risks

1. **Unit transplant fidelity.** The game bakes values into `Stats` and re-adds some holders only on events (memory:
   inscription edits need both the key and the Stats). A unit moved between saves or merged back may show wrong stats
   or lose a passive. Mitigation: move whole components, never rebuild them; F2 compares the character sheet before and
   after; savegen's audit (`auditUnitMajors`) is the model for a gate.
2. **Spectator join replay.** No reload for the players needs the spectator's replay from the last join-point save to
   equal the host's memory. If it does not, each spectator join costs a host reload (a short load for the players) or
   loops in resync.
3. **Writing into the player's real save folder.** Steam Cloud syncs the normal game's Saves folder; a file added while
   the game is closed may raise a cloud conflict, and an unknown key in `Counts` (the once-only marker) may be dropped by
   the game. Mitigation: never modify the original (new file + backup), ledger as the first guard, F2.5 / F2.6 tests;
   fallback = write into the MP app's own Saves.
