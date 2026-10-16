# Lane mpfix (v13 WP MPF), 2026-10-16

Branch `mpfix`, worktree ~/ds_mp_mpfix. Brief: research/briefs_v13/mpfix.md. Work split in three parts:
- UI (sub-branch mpfix-ui, notes NOTES_ui.md): hover cards outside the panel (src/ui_tip.c), stock gap on the
  Multiplayer page, Rules panel always open on the host page, neutral-hub fallback text of Foreign Expedition.
- Weapon slot (sub-branch mpfix-ws, notes NOTES_ws.md): quick-slot orders (ToggleAutoSkill / Set / Unset / Move) by
  skill key, resolved against each copy's own slots; PlayerUnitsSaveData/QuickSlots skipped in the full-state check.
- BepInEx bridge: branch bepinex-bridge ported onto main (src/mp_bridge.c/.h, 3 hooks in mp_game.c, 1 line in
  rig_cmd.c, build.sh). ext/bepinex_bridge scripts build from this checkout (build_mac.sh wraps src/build.sh,
  output build/mac/). Windows: gen_port takes every src/*.c, multiplayer.dll exports all 11 dsmp_bridge_*. No .def line.

## Proofs (dylib ~/ds_multiplayer/build/merge_mpfix/dsrig.dylib; fake-mouse build build/merge_mpfix_mouse/)
- Unit: net ALL PASS, mp_test ALL PASS, mpmenu_test, trn_test, ui_test 530, teams_test 437 097, ord_test 65 607: 0 fail.
- Rig: smoke 3/3 (prefs identical), mp2 --ticks 3000 19 PASS 0 FAIL, left_prompt --start 16 PASS 0 FAIL,
  menu_pair 17/17, tests/tip_pos.py 8/8 (title column x2, host page column + Rules row, lobby, Esc, HUD cog + box).
- Bridge: tests/run_unit.sh all PASS; bridge_xplay (Mac host + Windows guest W_BRG, pair GH) RESULT PASS:
  plugin sees the session, AddGold(250) applied once on both, full-state checks equal, 0 desyncs, 1345 equal ticks.
- Weapon slot: NOTES_ws.md (3 flip runs + 1 cast run, 0 desyncs, on the mpfix-ws build).
- Runner: tests/run_on.py <dylib> [--lease LANE] <script> runs a rig test on a given dylib.

## Windows crash found on main (fix for lane crossmods, not committed here)
src/mpmenu_kit.h (M4 split) holds static inline game calls with Mac addresses. win/port/gen_port.py translates only
.c files, so every user of the kit (mpmenu.c, mp_api.c, cm_mp.c, fe_menu.c, pvp_ui.c) calls Mac addresses on Windows.
The copy crashed at the title: page fault on execute at GameAssembly + 0x50FAF88 (Mac RVA of Object.op_Implicit).
Fix: gen_port_mpmenu_kit.patch (writes a translated build/gen/mpmenu_kit.h). With it the copy reaches the title and
bridge_xplay passes. Sent to orchestrator13 for crossmods.

## Test copy changes
- DSW_BRG (bottle DSMP): baselib.dll + det_ranges_win.tsv replaced by the mpfix build (with the gen_port fix); the old
  ones are in C:\DSW_BRG\old_mpfix\.
