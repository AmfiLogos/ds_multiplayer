# Lane mpfix (v13 WP MPF), 2026-10-16

Branch `mpfix`, worktree ~/ds_mp_mpfix. Brief: research/briefs_v13/mpfix.md. Work split in three parts:
- UI (sub-branch mpfix-ui, notes NOTES_ui.md): hover cards outside the panel (src/ui_tip.c), stock gap on the
  Multiplayer page, Rules panel always open on the host page, neutral-hub fallback text of Foreign Expedition.
- Weapon slot (sub-branch mpfix-ws, notes NOTES_ws.md): quick-slot orders (ToggleAutoSkill / Set / Unset / Move) by
  skill key, resolved against each copy's own slots. Verdict: the mod STAYS GAMEPLAY. tests/ws_equip_pair.py: a stock
  guest with a full bar never learns the new weapon skill on a weapon swap (stock ProcessEquip needs a free slot) ->
  desync. So the QuickSlots full-state skip was reverted again and no mod.json change (MORNING_QUESTIONS).
- Host-choice API for fepvp (mpfix-ui, NOTES_ui.md section 5): mp_host_choice + register_host_choice2 + host_choice_now
  appended to mp_api_table; up to 4 choices, Start-only host page, draw_page panel, lobby_start. tests/choice2_pair.py.
- BepInEx bridge: branch bepinex-bridge ported onto main (src/mp_bridge.c/.h, 3 hooks in mp_game.c, 1 line in
  rig_cmd.c, build.sh). ext/bepinex_bridge scripts build from this checkout (build_mac.sh wraps src/build.sh,
  output build/mac/). Windows: gen_port takes every src/*.c, multiplayer.dll exports all 11 dsmp_bridge_*. No .def line.

## Proofs (dylib ~/ds_multiplayer/build/merge_mpfix/dsrig.dylib; fake-mouse build build/merge_mpfix_mouse/)
- Unit: net ALL PASS, mp_test ALL PASS, mpmenu_test, trn_test, ui_test 530, teams_test 437 097, ord_test 65 607: 0 fail.
- Rig: smoke 3/3 (prefs identical), mp2 --ticks 3000 19 PASS 0 FAIL, left_prompt --start 16 PASS 0 FAIL,
  menu_pair 17/17, tests/tip_pos.py 8/8 (title column x2, host page column + Rules row, lobby, Esc, HUD cog + box).
- Bridge: tests/run_unit.sh all PASS; bridge_xplay (Mac host + Windows guest W_BRG, pair GH) RESULT PASS:
  plugin sees the session, AddGold(250) applied once on both, full-state checks equal, 0 desyncs, 1345 equal ticks.
- Weapon slot: flip run on build/merge_mpfix 0 desyncs; equip test desyncs (see above, expected for a gameplay mod).
- Final merge (main 02a258b merged in): all units pass again, smoke 3/3, mp2 19/0, left_prompt 16/0, menu_pair 17/17,
  tip_pos 8/8, choice2_pair 8/8, Windows module build clean (with the gen_port patch applied locally, not committed).
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
