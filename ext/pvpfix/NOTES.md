# pvpfix lane (v0.1.21, PLAN_v021.md section E + the lead's later owner rules)

Branches: ds_multiplayer `pvpfix` (from int020 1e65178), ds_custommodes `pvpfix` (from origin/main 3f7b43e, one commit:
units/ul_bring_api.c). Not merged.

## What changed, by item
| Item | Change | Files |
|---|---|---|
| E1 | Bring lists DSML_GAME_SAVES (the normal game's worlds) + the modded Saves; FE Bring reads DSML_GAME_SAVES too | pvp_ui.c, fe_game.c |
| E2 | Lobby Bring works (E1); HUD "Bring Units..." in the preparation; a new Bring there re-sets the match (END, units start, SETUP/BASE/START), first world snapshot and owner tables kept | pvp_ui.c, pvp_game.c, pvp_units.c |
| E3 | With Units Laboratory: Saves / My Units switch, My Units cards, "Build a unit" (Unit Builder), full entry JSON travels (others need nothing); folded-passive skip moved into multiplayer | mu_bring.c/h, fe_game.c, mu_save.c, pvp_game.c; ds_custommodes units/ul_bring_api.c |
| E4 | Fight vote: any team player proposes, all agree, world paused while open, 10 s countdown, Cancel; no start without agreement | pvp_core.c/h, pvp_game.c, pvp_ui.c |
| E5/E5b/E5c | Per-team fog in a match, enemy units only on my visible tiles; no opponent camera view / follow / ping; enemy portrait, HP bars, status hidden; enemy_viewrange shows enemy vision tiles. Rule ids via pvp_rule_get (lane pvprules; weak default = hidden) | mp_fog.c, ui_game.c, pvp_game.c |
| E6 | Random reachable team ends (hash of match number + units), 12..30 cells apart | pvp_game.c |
| E7 | No automatic attack orders (they overrode hold position and player orders); in the preparation a unit its player ordered is no longer walked to its end | pvp_game.c, ord_game.c |
| E7b | Click on an enemy = monster group 3001 (inspect view, no command; after SelectProcessor.Select, GetSelectGroup is inlined on Windows); non-attack interactions on an enemy refused at apply | pvp_game.c, ui_game.c, ord_game.c |
| E8 | HUD clock: Preparation / Fight proposed / Fight in N | pvp_ui.c |
| E9 | Round result screen ("Blue wins round N", score, "Next round in N") while the world stands still; host NEXT after 5 s | pvp_core.c, pvp_game.c, pvp_ui.c |

New hook sites: PassiveActionContainer.Register 0xA782C8 (pvp_game.c). ds_modloader docs/hooks.json must be regenerated
with the final pins (the loader build stops otherwise). Windows module build: compiles, no new inlined hook.

## Proof (hidden Mac pairs; out/<tag>/ has logs and frames)
- pvpfix_pair.py on the final code (e348aa7): full9 (Fight, best of 3, 3 rounds) 17/17, ctf9 (Capture the Flag) 17/17, 0 desyncs.
- Gate test ext/fepvp13/tests/pvp_v1_pair.py (updated): v1new3 11/11.
- e3_lib_pair.py: e3b 5/5 (rig, data path). e3_cards_pair.py (loader copies, real card window + Unit Builder): 9/9.
- e2_rebring.py: e2a 5/5. e7_orders.py: v0.1.20 FAIL (hold unit walked 5 cells), new PASS.
- Fails on v0.1.20: E1 + E5 (base7), E7 (e7_base2).

## For the release lane
- Gate tests that assumed the old behaviour: ext/fepvp13/tests/pvp_v1_pair.py updated here (vote + orders); loader
  repo test/bringcards_pair.py (g020_bringcards) still waits for an automatic fight start and automatic combat: it
  needs the same two steps (pvp_vote propose/agree, ord_run_attack mp:1 per unit).
- i18n: 22 new keys in all 9 languages (lang/), stale ones pruned.
