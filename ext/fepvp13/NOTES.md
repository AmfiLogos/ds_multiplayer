# Lane fepvp (v13 WP FEP): Foreign Expedition neutral hub + PvP v1 on the starting map

Branch `fepvp13`, worktree `~/ds_mp_fepvp13`. Brief: research/briefs_v13/fepvp.md. Owner words: research/owner_feedback_v12.md
items 5, 8, 13; research/owner_feedback_2026-10-15.md (hub, death Off, PvP).

## What is built

| Part | Files | What it does |
|---|---|---|
| FE choice text | custom/cm_mp.c | "Everyone brings units into a neutral hub with everything unlocked. Nobody's save changes until they bring the results home." (same as mpfix's fallback) |
| Hub / PvP world | custom/fe/fe_hub.c | the wiki base template `base_<build>.json` (Test Fights' cache `<data>/Mods/custom-modes/wiki/saves/`, downloaded by `sgc_template` when lane fights' cache is in the build, found with dlsym; else the cached file). Written as a new save `MPHub_<id>` / `MPPvP_<id>` and loaded (LoadCampaignRequested). Version = the running game's (`Application.version`, "DS_B.0.4.29"), iron off, GameSpeed 1 (the template was saved paused: with the rig host the lockstep never stepped). Hub: every research done (Workload = RequiredWorkload, unlocked; 31/31), every region open (10/10). |
| Hub stock | fe_hub.c `fe_hub_stock`, called in `fe_on_start` (DSNET_FE_START, every peer, same step) | the game's own SpawnItemRequested per stack of 20, on walkable cells (RuntimeMapData.IsMovable) in rings around the clan's units: Hardtack 100, Wild Herb 40, Ointment 20, Aid Kit 20, Wood Plank 60, Stone Brick 60, Textile 40, Leather 40 (keys + MaxStack checked in ItemTable.json of 0.4.29). Settlers carry them to storage. Once: only while the world still has the template prefix fe5eb (the start gives fe5ea00x). |
| FE host page | custom/fe/fe_menu.c `fe_page_*` | until the menu has per-choice Start buttons (request below): on the FE host page the "Host from a save" button reads "Start" and builds + loads the hub; the menu then waits for the campaign like a pick in the load list and opens the lobby. The Expedition Rules panel (src's own, tooltips, Death Off default) is forced open, its Done button hidden. |
| PvP BASE | custom/pvp/pvp_core.c/.h (op BASE = 7, arena value 2), pvp_game.c | the match on the settlement map: no transfer; arena rules on (no game over, no portals, events held); at START every peer computes the two ends (`find_base_ends`: the longest straight run of IsMovable cells in rows / columns within 12 cells of the units' mean cell, cut to 18 cells each side, 2 cells inset; Blue low end, Red high end, logged) and both teams walk there; countdown; fight. Round end = snapshot reload (as the arena); match end = the world snapshot from before the brought units. Old "PVP5" blocks load unchanged (arena byte 0..2). |
| PvP v1 choice | custom/pvp/pvp_ui.c | `pvp_v1_page_start` (PvP world `MPPvP_<id>`, campaign prefix b45e0 = every peer knows it), `pvp_v1_lobby_start` (brought units in -> SETUP + BASE -> START), `pvp_v1_page_draw(panel)` (PvP Rules on the host page: Mode, To win, Round time, Pause, Vanilla check; tooltips; arrows read by mouse rect), lobby Bring Units (today's PvP Bring window over the lobby), lobby names "N units brought", Vanilla check asked when the lobby opens / someone joins, result window "Back to the Title" (leave + quit to title; the host deletes its MPPvP file). |
| PvP from the hub | pvp_ui.c `pv_here()` | in a hub (or a PvP world) the Esc "PvP Match" plays on this settlement (BASE), elsewhere on the tested dungeon floor as before. |
| Bring Units (units lane) | fe_game.c `fe_bring_picks`, fe_menu.c `fe_bring_open` | compiled when custom/units/mu_bring.h + myunits.h are in the tree (units full on main): FE lobby Bring = mu_bring (tabs From a save / My Units), one send for all picks (save exports + mu_entries, source "library"). Until then the FE Bring window. PvP waits for `pvp_units_send_entries` (requested from lane units). ONE TODO in fe_menu.c. |
| cm_mp.c | pvp_world_build wrapper | the brought-units build (fe_build_hub) gives a new campaign guid: a PvP v1 world gets its prefix back (`pvp_v1_keep_prefix`). |

## Requests to other lanes (sent to team-lead for orchestrator13; "orchestrator13" does not resolve as a SendMessage name)
1. mpfix (src): `register_host_choice2(const mp_host_choice *)` with draw_page(panel) / page_start / lobby_start, 2+ choices
   (FE then PvP) under "Host a game", choice host page without Load save / New campaign, `host_choice_now()`. FE keeps
   src's own Expedition Rules panel (draw_page NULL), shown open. Until then: FE through today's API (above); PvP v1 only
   by rig commands (`mpm_pvp v1_page_start / v1_world_file / v1_lobby_start / page_arrow / lobby_bring`).
2. units: `int pvp_units_send_entries(jv *entries, char *err, size_t errn)` in custom/units/pvp_units.c.

## Build (until lane units' skeleton is on main)
`ext/fepvp13/build_lane.sh`: src/build.sh (its custom-modes-mp link fails on the new fe_hub.c: expected) then
custom-modes-mp.dylib again with fe_hub.c. After `git merge main` with units' lists: fe_hub.c goes into custom/fe/sources_mp.txt.

## Tests and results
| Test | Result |
|---|---|
| `make -C custom/pvp/pvp_test test` (+ BASE: phase, host only, save/load, arena byte 3 refused, placing, round 2 places again, match end clears) | 597/597 PASS |
| `ext/foreign_expedition/tests/test_custom_guard.py` (FE custom guard) | 18/18 PASS |
| `ext/fepvp13/tests/hub_smoke.py` (one copy, menu: Multiplayer > Foreign Expedition > Start -> hub -> lobby; the game's own save of the running hub checked) | 5/5 PASS (hub up in 1-2 s; research 31/31, regions 10/10, DS_B.0.4.29, iron off) |
| `ext/fepvp13/tests/pvp_v1_pair.py` (pair, full PvP v1 flow, checks 0-7) | see below |
| `ext/fepvp13/tests/hub_dive_test.py` (pair, FE dive in the neutral hub through the menus; copy of fe_dive_test.py) | see below |

## Hub map candidates (brief 6)
Searched (sub-agent, public web only, nobody contacted): no downloadable Dungeon Settlers player save anywhere (Nexus 16
files = mods, Steam guides / discussions, Reddit newest, wiki "Save Files", Chinese sites). One base screenshot only:
Steam user "clamsand", https://steamcommunity.com/sharedfiles/filedetails/?id=3797585452 (finished symmetric base, no
save offered). Discord not checked (no access). Files: ~/ds_multiplayer/ext/fepvp13/candidates/ (not in git: another
player's image). Default: the wiki base template.
