# Foreign Expedition, F1 + F2 (2026-10-14)

Plan: `../../research/plan_foreign_expedition_spectator.md`. Not part of the multiplayer release. Nothing in `src/`,
`rig/` or `build/` was edited. Real save folders were only READ (copied once into `fixtures/`).

## Files
- `fe_save.py`: the save library (F1). Python first version.
- `tests/test_fe.py`: offline tests on copies (`python3 tests/test_fe.py`). Output in `out/test_<time>/`.
- `rig_ext/fe_rig.c` + `rig_ext/build.sh`: builds `build/dsrig_fe.dylib` = the rig dylib from `src/` (read only) plus
  one test command `fe_stats {guid}` (every StatType 1..122 of a unit, through StatusReader). Only `rig_cmd.c` is
  compiled with `-Drig_exec=rig_exec_orig`; `fe_rig.c` is the new `rig_exec` and falls through to the original.
- `tests/f2_game.py`: F2 on one game copy (leases a pair, uses its first copy, stops it, releases).
- `fixtures/`: `home_Elven.json` (EA 0.4.29, 8 units), `hub_Moonlight_mp.json` (MP app copy, 0.4.29, 1 unit),
  `home_Moonlight_old.json` (0.4.23, for the version refusal).

## Library API (fe_save.py)
| Function | Does |
|---|---|
| `export_units(save, guids, player_uid)` | closure per unit + manifest (export_id, home campaign, version, hashes, level) |
| `build_hub(host, entries, run_id)` | deep copy, new campaign guid, `SaveName MPFE_<run8>`, iron off (header + start settings), removes the host clan and every reference, injects each closure with new guids, retargets difficulty holders, population |
| `extract_results(hub_end, run, rules, uid, item_guids)` | per unit alive/dead + end closure; items tagged to the player except the copies that travelled |
| `apply_results(home, result, manifests, ledger, out_dir, choices)` | checks (hash, ledger, ownership, campaign, version, marker), merge per rules, item placement, marker, NEW file, ledger |
| `check_refs(save, gone)` | savegen validate + duplicate guid/holder check + whole-file scan for guids that must be gone |

Closure = component list (Task reset like savegen `resetTask`, Carry guids zeroed), unit registry row, position,
animation state for each side, every `PlayerUnitsSaveData` row, `LevelUpPrayerAttemptIndexByUnitGuid`,
`UnitExpMaxStamp`, and each item (equipment + backpack + items inside items) with its components, registry row and
AutoFixable flag. Removal also scrubs `BedOwnerMap`, `Recreations[].PerformerGuid` (entry dropped),
`EventInstanceSaveData.*.TargetUnits`, `UnitSummonerDictionary` (+ the summons), `AllyGuidList`.

Findings while building it:
- Units in 0.4.29 carry NO baked `Stats` block (StatusComponent SchemaVersion 2: State, Progression, Generated,
  Permanent only). Derived stats are rebuilt by the game on load. So the risk is the affecter list, not baked numbers.
- Every unit carries the campaign difficulty as two holders (`AFFECTER_PlayerDifficulty<X>`,
  `AFFECTER_PlayerManagementDifficulty<X>`). A unit moved into a world of another difficulty would keep its home
  difficulty. The library swaps them for the target world's keys on inject and back to the home keys on apply.
  Both fixtures are VeryHard, so F2 could not show a difficulty change in the game (open test, needs a hub of
  another difficulty).
- Real saves already hold dangling guids (bed owners and recreation performers of dead units). The game tolerates them;
  our scrub removes ours anyway.
- "Changed at home" = a progress signature (progression, attributes, skills, skill trees, equipment keys), not the
  whole closure: needs and moods change every minute while the home game runs.

## Owner decisions (1-3 ANSWERED by the owner 2026-10-14, the rest = plan defaults)
1. Death permanent On: the home unit AND the gear it wears at home are removed. (owner)
2. Death permanent Off and the unit died: it goes home in its state from the LATEST multiplayer save where it was
   alive (`extract_results(..., snapshots=[newest first])`); no such save = home unit unchanged. (owner)
3. Items come back On: DUNGEON LOOT ONLY. `extract_results(..., loot={guid: {'found_in_dungeon': bool}})`; the tag
   must come from dsnet (F5/F6: tag at pickup in a dungeon); gear copies that travelled never come back. (owner)
4. Returned items: a free slot of a storage building whose storage policy accepts the key, else a free backpack slot
   of a home unit, else left in the result (`claims.json` lists them as `left_items`).
5. Apply writes a NEW save `<Clan>_BackFromExpedition_<run8>__AutoSave_<campaign>_0` with DisplayName
   "<Clan> - back from expedition"; the original is never modified.
6. Spectators: not part of F1/F2.
Extra default (new): XP counts = progression, attributes, affecter holders, skills, skill trees, learn notice,
level-up roll index, exp stamp come from the hub; needs (StateStats), equipment, backpack, position,
task, quick slot bar (a skill learned in the hub only fills an empty slot), consume policy, task priorities, name and portrait stay as at home.

## Not done in F1 (belongs to later milestones)
- Kit toggles (research / regions / storage): F5.
- Save listing across both folders, Windows paths: F4.
- "Is the normal game running" check before apply: F6 UI (needs the process name per platform).
- Steam Cloud test (plan F2.6): needs the owner's OK on a throwaway campaign.

## C port plan
`fe_save.c/.h` with vendored yyjson (MIT, one .c/.h), same function list. yyjson mutable docs give the deep copy
(`yyjson_mut_val_mut_copy`), key/value guid remap is one recursive walk, the whole-file reference scan is the same walk.
Port when F5 needs it from the dylib; until then the Python file is the reference and `tests/test_fe.py` becomes the
fixture set for the C tests (same inputs, compare outputs with `canon()`). Risk to watch in C: number formatting
(write floats with `%.9g` so 0.659269333-style values survive), and keeping `$type` the first key of each object
(Newtonsoft TypeNameHandling reads it first).

## Iron mode note
The new home save keeps the home campaign guid and its iron flag. When the player loads it and the game autosaves,
iron mode writes the campaign's own file name and `KeepOnlyRetainedCampaignSave` removes the other saves of that
campaign, which may include the original. The plan's backup copy (`<MP data>/fe/backups/`) is the protection; it
belongs to the apply UI (F6).

## F2 result (2026-10-14 09:14, copy C, out/f2_20261014-090907/)
- Hub (Moonlight base + Ivan, Leonis from Elven Expedition) loads; `2_hub_ui.png` shows both portraits (level 4),
  population 2/6. player.log: 0 exceptions. Game re-save of the hub: reference check clean, 9 / 8 items kept.
- Stats after the move (fe_stats, all 122 StatTypes, `stats_home.json` vs `stats_hub.json`): every combat and
  attribute stat equal. Only differences: needs ticking (Energy, Hunger -0.05), VisionRange 4 -> 7 and Leonis
  EnergyRegeneration 1.15 -> -0.05. Cause: the game re-evaluates world holders (it dropped NightTime and BedSleep:
  different time of day / not in a bed in the hub). Not a transplant fault.
- Apply (+2 levels, +3 Strength simulated on the game-saved hub) -> new home save loads: Level 4 -> 6, Strength 26 -> 29,
  PhysicalAttackPower 140.25 -> 143.55, MaxWeight 254 -> 266, MaxExp 1017 -> 2315: the game rebuilds derived stats
  coherently. Leonis (no change) identical. Second apply refused. Marker `mp.fe.<run>` survives a game load + save.
- Quick slots: the stock game copy cuts the 5-slot bar to 4 on every load (the owner's real install has the weapon-slot
  mod with 5 slots). Not caused by the move; the apply keeps the home bar anyway.
- Not proven in game: a hub of ANOTHER difficulty (both fixtures are VeryHard); real XP gain (simulated offline).

## F3-F6 (2026-10-14, source copy `src/`, output `build/dsrig_fe.dylib` only)
Source: rsync of `~/ds_multiplayer/src` (no old_versions) into `src/`; `src/build.sh` writes `../build/dsrig_fe.dylib`.
C library: `src/fe/fe_json.[ch]` (JSON DOM that keeps raw number / string text) + `src/fe/fe_save.[ch]` (port of
fe_save.py). Game glue: `src/fe/fe_game.c`. Hooks in the copy: rig_cmd.c (fe_* commands), mp_game.c (join-point save,
poll, delete guard), mp_core.c (FE_START), ord_mp.c (loot tag, recruit refusal), net/* (mail, FE_START).

Tests (all offline ones pass):
- `python3 tests/test_fe.py` (Python reference, 39 checks)
- `src/fe/build_cli.sh && python3 tests/test_c.py` (C library under ASan/UBSan, 32 checks, incl. a NORMAL hub
  `fixtures/hub_Moonlight_normal.json` made by `tests/make_normal_fixture.py`: units go in with Normal holders, come
  home with VeryHard ones)
- `cd src/net && make build/test_dsnet && ./build/test_dsnet` (whole suite + fe_mail + fe_start)
- `python3 tests/fe_pair_test.py` (one pair; leases only while the menuteams desync lane holds one)

Design:
- F3 mail: `P_BLOB` kind `BLOB_MAIL` (to_uid, from_uid stamped by the server, app kind, payload), any player ->
  host or host -> one uid, 1 KB fragments, max 8 MB. Kinds: 1 units, 2 units ack, 3 result, 4 result ack.
- F5 start: host `fe_start` sets gear Own storage and sends `DSNET_FE_START` (run id + new owners of every hub unit
  and travelling item). Every peer replaces its owner tables at that step; the server sets everyone else to loading and
  asks for a join point. The host's join-point save is turned into the hub (`fe_join_point_save`: live world + units,
  campaign guid `fe5ea000-...`, iron off) and every peer loads it through the normal path.
- During the run: dungeon loot = items tagged on a dungeon map (host, ord_mp it_tag); last-alive closures kept at each
  host join-point save (death not permanent); recruits refused in any `fe5ea000` world (same on every peer).
- F6 end: host `fe_end` (settlement only): hold, game save moved to `data/fe/runs/<run>/hub_end.json`, one result per
  player, mailed; unacknowledged results are mailed again every 10 s while that player is connected.
- F6 apply (`fe_apply {run}`): FIRST a copy of the whole Saves folder (and the source save's folder) into
  `$DSRIG_FE_BACKUP/<time>/` (tests: `ext/foreign_expedition/backups/`), then a NEW save in the player's own Saves,
  ledger + marker. The original's name goes into `data/fe/protected.json`; `CampaignSaveLoader.Delete` refuses those
  names, so an iron-mode autosave can never delete an original.
- Steam Cloud test: owner 2026-10-14: later, close to release. Until then copies only, never the real save folders.

## Recruiting in the hub (owner 2026-10-14): host option, default Off
`fe_start {recruit: 0|1}`. The flag travels inside the hub's campaign guid (`fe5ea00<0|1>-...`), so every peer and every
late joiner reads the same value with no new network state. Off: the recruitment window (popup 8) is never opened
(no message) and the replayed recruit order has a silent guard; the HUD opener still has to be hidden (its object name
comes from `ui_find {sub: recruit}` in the next dive). On: recruiting works as usual, recruits belong to the
recruiter and stay in the hub (they are not in the run, so results never send them home).

## F4 host page (board FESettingsA, owner choice (a), 2026-10-14)
- Host page: new row "World  ‹ Your save | Foreign Expedition ›" above Rules (click = the other value, game tooltip).
- In Foreign Expedition mode the Rules button opens "Expedition Rules" (fe_build in mpmenu.c, same frame and row style
  as the Rules panel): Death (Permanent / Not permanent), Experience (Counts / Does not count), Dungeon loot
  (Comes back / Stays in the hub), Units per player (1-6), Gear (Own storage, fixed, no arrows), Recruiting (Off / On).
  A line under each row says what the value does; each row has a game-format tooltip (texts in mpmenu_model.c).
- The Rules summary under the button shows the expedition rules. Remembered in mp_menu.json (fe, fe_death, ...).
- Lobby Start in this mode = MPA_FE_START: fe_menu_start (same as fe_start) with these rules, then the lobby closes.
  Nobody brought units: the host gets "Nobody brought units yet: Bring units first." and the lobby stays.
- Model tests: src/mpmenu_test (make test) cover the toggle, arrows, descriptions, summary and Start.
- Not built yet: the "Bring units" window (boards FEBringA/B), lobby lines "N units brought", the results window
  (FEResultsA/B). Today units are brought with the rig command fe_bring.

## Merge notes (2026-10-15 night, from the PvP lane)
- src now has BLOB_PATCH = 2, BLOB_REPORT = 3 (net/dsnet_internal.h): our BLOB_MAIL must become 4.
- src has DSNET_PATCH_POINT = 20 (net/dsnet.h): DSNET_FE_START must become 21 (only caught as a duplicate case in mp_core.c).
- dsnet_server.c P_BLOB: guest path accepts only BLOB_REPORT -> route mail before that check; host path: BLOB_MAIL branch before BLOB_PATCH.
  Resolved versions (mail_relay helper): ~/ds_multiplayer/ext/pvp/merge/tree/net/. PvP merges after FE; message pvp when src_merge.lock is free.
- Dive 1 (out/dive_20261014-153242) re-read: the "died" results were NOT combat deaths. Units idled at the dungeon entrance
  ("Dying by Hunger", "No Edible food" alerts), XP never moved, and hub_end.json has ZERO units on both sides. Needs a re-run
  with the new logging (hunger, task, base after return) before it counts as a pass.

## Night 2026-10-15: dives in game + merge (tests/fe_dive_test.py, all through the menus / windows)
Results (out/dive_*):
- Dive 2 (death permanent, XP + loot): 23/23 on the lane build (dive_20261015-103650) and 23/23 on the MERGED build
  (dive_20261015-112055): XP gained in the hub = level/exp at home, the game shows it after loading, dungeon loot comes home, only loot.
- Dive 3 (death NOT permanent, --weak): 21/21 (dive_20261015-110440): party wiped (stock game over), both guest units restored from
  their last multiplayer save state (from_snapshot), home save loads.
- Recruiting On: 23/23 (dive_20261015-105658): recruit allowed on both peers, window opens, Guild Contact shown.
  Recruiting Off (every dive): order refused on both peers, popup 8 never opens, Guild Contact button hidden on both peers.
- Windows tested in game with frames saved in each out folder: host page rules + tooltip (host_page_fe_*.png), Bring units
  (bring_window_*.png, bring_tip_*.png), lobby counts (lobby_units_brought_*.png), Results window + Apply (results_window_guest.png,
  results_applied_guest.png), Expedition results list "Already Brought Home" (results_list_title.png, results_already_applied.png).
Bugs found and fixed:
1. mpmenu: the world's remembered rules (mp_worlds.txt) replaced the Host page's World = Foreign Expedition -> guests never got the FE mode,
   Bring did nothing. World + expedition rules now always come from the Host page.
2. Host mode mail is re-sent every 5 s (a guest that joined during the host's join-point reload missed it once).
3. Stackable loot merged into a stack on pick-up (its id is gone) -> lost. Now: key + amount recorded BEFORE the pick-up
   (ord_game ord_last_pickup), results bring it back by key, capped by what the hub gained of that key since the start
   (run.json key_totals_start). Offline tests in test_c.py.
4. Recruiting Off: the HUD Guild Contact button (FactionManagementView._clanMenuButtons[0]) is hidden in the hub.
Test fixes (not product bugs): old dive 1 "pass" was false (units idle, deaths = units left behind at the portal); left click in
Teams RELEASES an owned unit (now right click = mark); units walk to the portal first; one unit uses the portal; game over handled.
Merge: see PLAN.md "Foreign Expedition merge". Tools: tools/merge_prep.py (3-way, base = closest old src), tools/resolve.py,
tools/transplant_ui_game.py (lane copy older than any snapshot: FE hunks applied by hand), tools/gate_merge.sh, tools/run_with_dylib.py.
Lane tests now run against src ids with --dylib ~/ds_multiplayer/build/dsrig.dylib --ids mpm_ids_merged.json.
Open (MORNING_QUESTIONS): stacked loot rule, left-behind / wiped units count as dead, no food in the hub kit.
