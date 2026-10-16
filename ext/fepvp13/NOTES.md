# Lane fepvp (v13 WP FEP): Foreign Expedition neutral hub + PvP v1 on the starting map

Branch `fepvp13`, worktree `~/ds_mp_fepvp13`. Brief: research/briefs_v13/fepvp.md. Owner words: research/owner_feedback_v12.md
items 5, 8, 13; research/owner_feedback_2026-10-15.md (hub, death Off, PvP).

## What is built

| Part | Files | What it does |
|---|---|---|
| FE choice text | custom/cm_mp.c | "Everyone brings units into a neutral hub with everything unlocked. Nobody's save changes until they bring the results home." (same as mpfix's fallback) |
| Hub / PvP world | custom/fe/fe_hub.c | the wiki base template `base_<build>.json` (Test Fights' cache `<data>/Mods/custom-modes/wiki/saves/`, downloaded by `sgc_template` when lane fights' cache is in the build, found with dlsym; else the cached file). Written as a new save `MPHub_<id>` / `MPPvP_<id>` and loaded (LoadCampaignRequested). Version = the running game's (`Application.version`, "DS_B.0.4.29"), iron off, GameSpeed 1 (the template was saved paused: with the rig host the lockstep never stepped). Hub: every research done (Workload = RequiredWorkload, unlocked; 31/31), every region open (10/10). |
| Hub stock | fe_hub.c `fe_hub_stock_json`, called by the host in `fe_join_point_save` on the template world BEFORE fe_build_hub (so the start totals include it) | copies of the template's own ground items of the same key (record shape), fresh durability, new guids, on free cells of the discovered field area nearest to the units: Hardtack 100, Blue Berry 40, Ointment 40, Aid Kit 20, Log 100 (stacks of 20). Every peer loads that file. Only while the world has the template prefix fe5eb. First try (SpawnItemRequested in the FE_START sim command) failed: the join point was written before the events ran (0 of 380 items in the hub) and the guest kept 2 orphan entities (full-state check difference). |
| Host choices | custom/cm_mp.c | `register_host_choice2` (lane mpfix, on main 231d3e5): "Foreign Expedition" (draw_page NULL = the menu's Expedition Rules panel, open, Death Off default; page Start = fe_world_start hub; lobby Start = fe_menu_start with M.set.fe_*) and "PvP" (draw_page = PvP Rules panel; page Start = PvP world; lobby Start = pvp_v1_lobby_start). The earlier fe_menu.c page adapter is removed. |
| PvP BASE | custom/pvp/pvp_core.c/.h (op BASE = 7, arena value 2), pvp_game.c | the match on the settlement map: no transfer; arena rules on (no game over, no portals, events held); at START every peer computes the two ends (`find_base_ends`: the longest straight run of IsMovable cells in rows / columns within 12 cells of the units' mean cell, cut to 18 cells each side, 2 cells inset; Blue low end, Red high end, logged) and both teams walk there; countdown; fight. Round end = snapshot reload (as the arena); match end = the world snapshot from before the brought units. Old "PVP5" blocks load unchanged (arena byte 0..2). |
| PvP v1 choice | custom/pvp/pvp_ui.c | `pvp_v1_page_start` (PvP world `MPPvP_<id>`, campaign prefix b45e0 = every peer knows it), `pvp_v1_lobby_start` (brought units in -> SETUP + BASE -> START), `pvp_v1_page_draw(panel)` (PvP Rules on the host page: Mode, To win, Round time, Pause, Vanilla check; tooltips; arrows read by mouse rect), lobby Bring Units (today's PvP Bring window over the lobby), lobby names "N units brought", Vanilla check asked when the lobby opens / someone joins, result window "Back to the Title" (leave + quit to title; the host deletes its MPPvP file). |
| PvP from the hub | pvp_ui.c `pv_here()` | in a hub (or a PvP world) the Esc "PvP Match" plays on this settlement (BASE), elsewhere on the tested dungeon floor as before. |
| Bring Units (units lane) | fe_game.c `picks_entries` / `fe_bring_picks` / `pvp_bring_picks`, fe_menu.c `fe_bring_open`, pvp_ui.c `pv_bring_open` | FE lobby, PvP lobby and the PvP window open mu_bring (tabs From a save / My Units) with the per-player cap; one OK = one send (save exports + mu_entries with source "library"; PvP through units' pvp_units_send_entries). When mu_bring_open refuses (no widget kit: the rig copies) the old windows open. NOT tested in the game: the rig has no widget kit, and a My Units library in a rig copy would be the owner's real one (mu_lib_path uses the real home; rig forwards only DSRIG_* env), so the "one My Units unit" dive was not run; the guard (custom units never applied home) is the FE custom guard test 18/18. |
| cm_mp.c | pvp_world_build wrapper | the brought-units build (fe_build_hub) gives a new campaign guid: a PvP v1 world gets its prefix back (`pvp_v1_keep_prefix`). |

## Requests to other lanes: both delivered (mpfix register_host_choice2, units pvp_units_send_entries) and used.

## Build (until lane units' skeleton is on main)
`ext/fepvp13/build_lane.sh`: src/build.sh (its custom-modes-mp link fails on the new fe_hub.c: expected) then
custom-modes-mp.dylib again with fe_hub.c. After `git merge main` with units' lists: fe_hub.c goes into custom/fe/sources_mp.txt.

## Tests and results
| Test | Result |
|---|---|
| `make -C custom/pvp/pvp_test test` (+ BASE: phase, host only, save/load, arena byte 3 refused, placing, round 2 places again, match end clears) | 597/597 PASS |
| `ext/foreign_expedition/tests/test_custom_guard.py` (FE custom guard) | 18/18 PASS |
| `ext/fepvp13/tests/hub_smoke.py` (one copy, menu: Multiplayer > Foreign Expedition > Start -> hub -> lobby; the game's own save of the running hub checked) | 5/5 PASS (hub up in 1-2 s; research 31/31, regions 10/10, DS_B.0.4.29, iron off) |
| `ext/fepvp13/tests/pvp_v1_pair.py` (pair, full PvP v1 flow, checks 0-7) | 3 runs 11/11 PASS (out/v1_r1..r3); round end + match end + world back + fingerprints equal (7000+ common ticks in longer runs) |
| `ext/fepvp13/tests/hub_dive_test.py --death 0 --feed` (pair, FE dive in the neutral hub through the menus; copy of fe_dive_test.py + stock check) | 25/25 PASS (out/hubdive_20261016-025811): save units only (My Units part waits for lane units) |
| PvP m4_van / m4_bring / m4_ui (ext/pvp/tests, PVP_DYLIB = this build) | PASS / PASS / PASS (02:52, 02:55, 02:57) |

## Fixes found by the pair tests
- Day-0 template: the game asked "Lock Consecration Results ... Enable it?" at load (pause): LockConsecrationEnabled false +
  NoticeAcknowledged true written into the built world.
- PvP on the settlement: teams start mixed, so no hostility while they walk to their ends (pairs made when the fight
  begins); ends only on explored walkable cells (grid probe: movable-only cells run into the dark); fighters fed / rested
  and settler jobs off in the PvP world (they hauled ground items and ate instead of walking); arrival radius 6 on the
  settlement (two walkers share one end cell).
- fe_save.c next_free_pos: when every preferred cell is taken (a ground item there) the unit went to the map's first entity
  position (a unit landed outside the settlement); now the nearest free cell around the first preferred one.

## Hub map candidates (brief 6)
Searched (sub-agent, public web only, nobody contacted): no downloadable Dungeon Settlers player save anywhere (Nexus 16
files = mods, Steam guides / discussions, Reddit newest, wiki "Save Files", Chinese sites). One base screenshot only:
Steam user "clamsand", https://steamcommunity.com/sharedfiles/filedetails/?id=3797585452 (finished symmetric base, no
save offered). Discord not checked (no access). Files: ~/ds_multiplayer/ext/fepvp13/candidates/ (not in git: another
player's image). Default: the wiki base template.

## Final proofs (merge build = branch fepvp13 after `git merge main` e9f2829; build/ of this worktree)
- pvp_test 597/597; FE custom guard 18/18.
- hub_smoke 5/5 (menu: Multiplayer > Foreign Expedition > Start; also --nocache: the template downloaded from the wiki, 717 495 bytes).
- PvP v1 pair test through the menu (Multiplayer > PvP > rules > Start > lobby > guest joins > bring > Start): 11/11 in
  v1_n1, v1_n2, v1_n3, v1_f1 (out/).
- Hub dive (neutral hub, Death Off, save units): 25/25 (out/hubdive_20261016-041551 and earlier).
- PvP from the hub (Esc > PvP Match, the hub's units, match end, hub back, same run): 13/13.
- PvP m4_van / m4_bring / m4_ui: PASS (before the units merge; PvP window code unchanged since except the Bring button).
- Rig gate on this build (`ext/fepvp13/gate_lane.sh`): smoke 3/3, mp2 19 PASS 0 FAIL, prefs identical.
