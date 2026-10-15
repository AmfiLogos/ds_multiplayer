# PvP v1 (ext/pvp) - notes

MERGED into src 2026-10-15 12:10 (PLAN.md "PvP merge"); this copy stays the lane's work tree (its net/ is older than src's:
merge with tools/merge_prep.py + merge_full.sh + merge_audit.py, never copy it over src). Before the merge:
PvP was NOT part of the multiplayer release. Everything lives in this separate source copy:
`ext/pvp/src/` builds `ext/pvp/build/dsrig.dylib` with `src/build.sh` (never `build/dsrig.dylib` or `rig/`).
Game build: Steam 25846389 (DS_B.0.4.29), arm64 macOS. Tests lease rig lane `pvp`, one pair, and must run
`check-prefs` after (each test does it in `finally`).

## What is built

| Part | Files | What it does |
|---|---|---|
| Rules (pure C) | `pvp_core.c/.h`, unit tests `src/pvp_test/` | match state, teams, rounds, best-of, timer, pause rule, out units, CTF flags (take, carry, drop, return, capture), hold of story events; saved in the PvP save block |
| Game hooks | `pvp_game.c` | teams hostile to each other, aggro, arena floor (no monsters / NPCs / respawn / portals / game over), down-not-die, idle re-kick, CTF banners and the dropped-flag marker, round reset and match end by world snapshot reload, test camera (`pvp_cfg`) |
| Own units (M3c) | `pvp_units.c`, `fe/` (copied from Foreign Expedition) | each player brings units from a save on his computer; mail to the host; FE_START + join point builds the PvP world; world and owner tables come back after the match; roster mail so every PvP window shows the brought units |
| Wiki units (M3b) | `pvp_wiki.c/.h` + `pvp_wiki_tables.h` (link decoder), `pvp_build.c/.h` + `pvp_unit_tables.h` (unit writer) | a DS wiki builder team link -> save closures built from the game's own tables (never the wiki's savegen.js), then the brought-units path |
| UI | `pvp_ui.inc` (included by `mpmenu.c`), `ui_game.c` (carrier tag) | Esc "PvP Match" window (teams, rules, brought units), Bring Units window (saves, units, Paste Wiki Link), Ready, countdown, HUD (boss bar copy), result window, messages, tooltips |
| Rig commands | `pvp_game.c` exec, `rig_cmd.c` | `pvp_setup/arena/start/status/cfg/arena_info`, `pvp_units_bring/wiki/start/status`, `mpm_pvp action=...`, `unit_stats`, `list_units` (downed = stock unconscious test) |

## Vanilla check (owner 2026-10-14 ~20:00; design board PvPVanillaCheck; 2026-10-15)

Host rule in the PvP window, row "Vanilla check": **On: vanilla only** = no GAMEPLAY mod for anyone (host included),
CLIENT mods fine and never compared; **Off: mods allowed** = every player must run exactly the host's gameplay mods.
- Mods of a copy: the launcher's session file (`DSML_SESSION` -> `dsml_session.json`, ~/ds_launcher core/src/flow.rs
  `Session`: `rule`, `modset`, `modset_lines`, `client_mods`). Tests: `DSRIG_DSML_SESSION` or rig `pvp_mods {"session": path}`.
  No file = not started by the launcher = no mod known (counts as no gameplay mod; MORNING_QUESTIONS).
- First value of the row = the world's launcher rule (`rule`), none = On.
- Transport (outside the sim): PvP mails 14 ask (host -> player, carries the host's choice: a guest's window shows it),
  15 report (player -> host, the session keys), 16 refused (host -> players, the message). Asked when the window opens,
  when the row changes and when a player joins. Not in HELLO yet (corenext had no modset fields when this was built).
- Enforced in `pvp_ui_start_match` (Start Match; before own units are brought in): `pvp_mods_gate` checks the host and
  every player present (`pvp_modcheck.c`, pure C, `make -C src/pvp_test test`). Refused = the window stays open and its
  note names the player and the mod ("Vanilla check is on: Red has the gameplay mods hell-difficulty 2.0.0, ..."); the
  players get the same message. A missing report = "Waiting for the mod list of X" (asked again). Off: "Missing: ...",
  "Not allowed: ...", same names but other files, or "did not start the game from the launcher".
- The rule rides in SETUP (`pvp_rules.vanilla`, payload 40 bytes) and the save block (now "PVP5"); `pvp_status` "vanilla".
  The rig commands `pvp_setup` / `pvp_start` are NOT gated (raw test commands); `pvp_mods {"gate": 0|1}` runs the gate.
- Files: `pvp_modcheck.c/.h` (rules + messages), `pvp_mods.c` (session, mails, gate, window text), `pvp_ui.inc` (row,
  note, tooltip), `pvp_game.c` (gate call). Test: `tests/pvp_vanilla.py` (V0-V5, D).
- Mailbox: one reader. In the merged build fe/fe_game.c fe_poll reads it and hands unknown kinds to pvp_mail_take
  (pvp_units.c); the lane build (no FE glue, found with dlsym "fe_poll") polls itself.
- Results 2026-10-15: lane build out/van2 7/7, v1_ui2 9/9, v1_bring 8/8, v1_wiki 7/7, v1_rounds 5/5, v1_ctfdrop 7/8 (F5 checked
  the first 3 events; a round-1 capture came first; events identical on both copies; check fixed to "in order"). Merged
  build: m_van2 7/7, m_bring 8/8, m_ui 9/9. Tests take PVP_DYLIB; tools/run_with_dylib.py, gate_merge.sh, bg_test.sh,
  wait_test.sh. Tests now wait for the host's load before the guest starts (a loaded Mac took 3 min: guest gave up).
- Mod names are `<id> <version>` (the session file has no display names).
- HELLO modset (core next, merged 2026-10-15 07:35, src/net/dsnet.h DSNET_PROTO 2+): the server checks the WORLD's rule
  at join (rule from the session file, refusal DSNET_REJ_MODSET). PvP's check is per match and the host can change it in
  the window, so it is still needed when the PvP rule differs from the world's rule (world "mods" + PvP On; a vanilla
  world + PvP Off then passes: nobody has gameplay mods). Reuse: dsnet gives a copy only its OWN lines
  (dsnet_my_mod_lines) and the host's lines after a refusal; the host cannot read each player's HELLO lines. So the
  report mail (15) stays until dsnet exposes per-player lines (e.g. dsnet_player_mod_lines(c, p)); then pvp_mods_gate
  can read them and drop mails 14/15 except the window's rule display. Mine could come from mp_config.c
  mp_session_mods instead of reading the session file again. Not rewritten tonight (lead).

## Generated tables (rerun after a game or wiki update)

```
node tools/gen_wiki_tables.js > src/pvp_wiki_tables.h            # share-link index order (builder.js dsbIdx)
node tools/gen_unit_tables.js > src/pvp_unit_tables.h 2> out/unit_tables_diff.txt
```
`gen_unit_tables.js` reads `~/dungeonsettlersdb/data` (game tables) and `~/ds_skill_truth/dump_<build>/dump.cs`
(tree enums); it prints every fact where the builder and the game differ (0 on 25846389). A wiki unit is stamped with
`WU_GAME_VERSION`; the hub refuses it when the game version moved.

## Tests

Pure C (no game):
```
make -C src/pvp_test test        # rules
make -C src/wiki_test test       # link decoder vs the wiki's own decode (1509 checks)
make -C src/build_test test      # unit writer vs the builder's numbers (python3 run.py N for more cases)
```
Pair tests (`/opt/homebrew/bin/python3 ext/pvp/tests/<file> --tag NAME`; output `ext/pvp/out/NAME/`):

| Test | Checks |
|---|---|
| `pvp_pair.py` | M1: two copies, PvP state same on both. OBSOLETE since M3: a team wipe ends the match before its 2500 ticks, so it waits 900 s (2026-10-15) |
| `pvp_arena.py` | M2: arena floor, no monsters, fight, out units, no desync |
| `pvp_rounds.py` | M3/M5: rounds, reset by snapshot, match end, pause rule |
| `pvp_ui.py` | M4: windows, HUD, Ready, result (frames) |
| `pvp_ctf.py`, `pvp_ctf_drop.py` | M6: banners, take, carry, drop marker, return, down-not-die (F1-F7) |
| `pvp_own_units.py` | M3c by command |
| `pvp_bring_ui.py` | M3c through the windows: B1-B6 (columns show the brought units), held events, down-not-die, world and owners back |
| `pvp_wiki_units.py [--edge] [--seed N]` | M3b: wiki links, every stat in game vs builder (`stat_diff.txt`), both copies equal, match runs |
| `cam_probe.py` | one copy: test camera |
| `pvp_vanilla.py` | Vanilla check: env session, On refused / allowed, guest sees the rule, host's own mod, Off set rules, a match runs |

## RVAs that need a Windows mapping

Hooks (`rig_hook`): FactionRelationInfo.GetRelation 0x82BBA0, EntityExtensions.IsHostile 0xFA4CE8,
CombatDataHandler Init 0xBCDE24 / OnSpawnUnit 0xBCF690 / OnMoveUnit 0xBD1930 / OnDeadUnit 0xBD00E8 /
OnDespawnUnit 0xBD1EB8 / CheckAggroTrigger 0xBCE72C, DungeonGeneratorStrategy.HandleSpawnCategory 0xCBA210,
CampaignEntityLoadHelper.SpawnDungeonNPC 0xDA8B30, DungeonBehaviorSimulator.Tick 0xBC4D80,
DungeonPortalHandler.Process 0xF9E298, EntityEventHandler.CheckGameOver 0xD854EC,
MainEventApplier.Apply(DeadApplyData) 0xAB7628, EventContainer.IsEventStartable 0xC8D6FC / StartEvent 0xC8DFBC,
EventFlowProcessor.StartEvent 0xCEA620 (+ the ui_game.c / ui_teams.c / mpmenu.c hooks of the base mod).

Calls: CombatDataHandler.IsHostileTo 0xBCEBA8, HostileEntityData Add 0xBF3B88 / Remove 0xBF3C10 / Refresh 0xBF3C88,
FactionAggroData.TryGetAggro 0xBF38F4, List<object> ctor 0x240AEDC / Add 0x240B904,
MainEventApplier.Apply(DeadlyApplyData) 0xAB838C, DeadlyApplyData ctor 0x845DE4, CheckGameOver lambda
(conscious test) 0xD8DA20, SpawnBuildingRequested ctor 0xD62C38, DespawnBuildingRequested ctor 0xD62CB4,
EventQueue.AddEvent 0xCCFAD4, EntityLifecycleHelper.SpawnBuilding 0xC93308, TextKeyExtensions.GetText 0x80EBAC,
UnitEntity GetDisplayName 0xFA467C / get_Name 0xFA47A0, UnitVisualDatabase.GetPortraitSprite 0xE51A58,
GUIUtility.systemCopyBuffer get 0x518AEA0, Application.persistentDataPath 0x507B74C, Camera / Transform
0x5109B04 / 0x50805E8, the Unity UI calls in `pvp_ui.inc` (0x50FCCC0, 0x510EAE4, 0x510FC60, 0x5266F04 ...).

TypeInfo slots: List<IEntity> 0x66458C0, List<UnitEntity> 0x66464D8, DeadlyApplyData 0x664C368,
SpawnBuildingRequested 0x6654980, DespawnBuildingRequested 0x664C8D0.
Field offsets used: DeadApplyData Key +0x10 / Target +0x18, UnitVisualDataManager._db +0x20, EntityInfo dead +0x1C,
task type +0x28 (TaskReader), HostileEntityData lists (see pvp_game.c comments).

## Known limits

- Only one tested arena: region 1 floor 2 seed 424242 (both team ends and the portal checked).
- Out = down for the round; no revive, no respawn. A down unit is taken off the hostile lists.
- The arena region adds its environment (region 1: Agility -30%) and the host's difficulty affecter applies to all
  units; Lycans get -10% majors by day. The PvP window says nothing about these yet.
- Brought units: at most 4 in total (2 per player with two players). A player who joins after a bring sees the
  roster only at the next bring. A save from another game version is refused.
- Wiki links: Dwarf / Tal / White Demon have no look in the game and are refused; manual non-major stats and
  hunger / exhaustion settings cannot be stored (the Bring status lists them as notes). Builder-vs-game differences
  are listed in the M3b report (generation bake, typed majors, conditional passives, test trait).
- Placing is by walking (Red walks to its end); no teleport.
- macOS only; Windows needs the RVA mapping above.

## v2 ideas

- Fog of war per team (vision ranges, hide enemy units outside it).
- Respawn mode (timer at the team's end) besides down-for-the-round.
- Protect Base (each team guards a building) and Tower Defense (waves against one team's base).
- More tested arena seeds and floors (checked team ends, portal distance, room count), a seed picker in the window.
- Teleport placement at round start instead of walking.
- Show arena environment and difficulty effects in the PvP window; optional "neutral" mode that strips them.
