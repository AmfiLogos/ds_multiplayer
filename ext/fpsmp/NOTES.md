# fpsmp lane (2026-10-26): the multiplayer mod's frame costs in single player

Source of the rows: ext/fpsaudit/mp.md (rows 1, 3, 5, 8, 9, 10, 11), fpsmac's k_C vs k_noMP run (row 1 PROVEN:
+30.4 ms MainFlow.Update per key press, MP off 0.5 ms; +112 ms per stock popup).

## Changes
| row | where | change |
|---|---|---|
| 1 | src/mpmenu.c fv_* | Kicked frames (3 after a key press, 10 after a stock popup or a rig command) no longer skip the one-search-per-frame budget. One real FindObjectOfType per frame for all polls together; a class that missed in this frame answers NULL to every later poll in the same frame (miss memo); in a kicked frame the asked-for class searched longest ago goes first (no starving). Actions (find_view_now, rig commands through rig_exec) still search at once. perf gets `poll_frames: [most poll searches in one frame, frames with 2+]`. esc_view (SystemMenuView, the only view single player still polled) searches only while the popup container says popup 1 (SystemMenu) is open: its search takes active views only, so it could only hit then (a container it cannot read: searched as before). |
| 8 | src/ui_game.c hud_track, hk_uo_sethud | NOT gated on a session: a host's world is loaded before the session starts and not again until a guest joins, and reserved units' "away" tags / see-through need these HUDs. The slot search is cheap instead: by address first (`at`, a key never read), then a never-used slot, then a dead one (was: handle get + alive check of up to 256 slots per call). |
| 9 | src/ui_game.c quick_frame | `if (!on() && !qs_foreign) return;` greying is session-only; the restore path (qs_foreign) still runs once after a session. |
| 10 | src/ui_tip.c tooltip_view | TooltipUI goes through mpmenu's find_view_x (2 s retry when absent, budget, miss memo); weak import, old path if absent. |
| 11 | src/mpmenu.c lbox_view | already on find_view_x: gets the miss memo and the budget with row 1. No own change. |
| 5 | src/rig_det.c rig_disarm | `det_on = 0` before the early return. After one host / join, every Random / Guid / new Random() of the process walked the stack (Windows: RtlCaptureStackBackTrace) and swapped the Unity random state, single player too. rig_det_arm / a det load switch it on again. |
| 3 | win/port/rig_main_win.c FILT | Entries 0-3 (QuitToTitleSceneRequested, CombatInfoComponent, CombatTargetApplyData, UnitObject: one class of each name in win/25846389/dump, no subclasses) get their namespace and `late`: filt_resolve() takes the class pointers at the first MainFlow.Update, then filt_pick compares pointers (was: a class-NAME compare on every call of the folded bodies). Entries 4-6 unchanged (6 = nested iterator, name compare). |

Not done (row 3): the thunk itself (4 GPR + 4 XMM saved around filt_pick) still runs on every call of a folded body,
about 20 cycles. A pointer test inside the thunk before the save would remove it; possible follow-up.

fpsscan's kit view-lookup helper and SCAN-RULE guard were not merged when this lane merged (uncommitted in the
fpsscan worktree), so mpmenu_find_any stays the one path and the SCAN-RULE run is still to do once fpsscan lands.

## Numbers (ext/fpsmp/sp_scan_test.py, Mac, base = main 03ec427, new = this lane, MP only, 120 fps, batchmode)
Round 1 = one search per frame + miss memo; round 2 = + esc_view only while popup 1 is open (the merged code).
| phase | base | round 1 | round 2 |
|---|---|---|---|
| title 30 s: scene searches | 15-16 (QuickSlotUI) | 0 | 0 |
| title: MainFlow.Update > 2 ms | 30-32/min, max 7-10 ms | 0/min, max 1.1 ms | 0/min, max 1.6 ms |
| Load list 20 s: searches | 11 | 0 | 0 |
| world idle: searches | 128/min | 48/min | 0 |
| world, 1 key press/s: searches | 885/min | 184/min | 0 |
| searches per key press / per stock popup | 13.9 / 42-43 | 2.5 / 8.2 | 0 / 0 |
| extra MainFlow.Update per key press (3 frames) | 29-31 ms (max 37-40) | 7.6 ms (max 10.1) | 0.03 ms (max 1.0) |
| most poll searches in one frame / frames with 2+ | (no counter) | 1 / 0 | 1 / 0 |
| Esc-menu clones after the Esc menu opens | 4-5 ms | 3 ms | 2 ms |
Sprite scans (Resources.FindObjectsOfTypeAll) were 0 in every phase. MainFlow.Update > 2 ms per minute in a world swings
from run to run on both copies (game ticks: no-tick frames are the same on base and new); not judged.
Why the Esc gate is safe: the owner's own session logs show the game opening the Esc menu as "popup 1 requested, caller
Refactor.UI.ViewObjectLoader.Update" (PopUpViewContainer.ActivateView); SystemMenuView is a PopUpUI_Base the container
makes and keeps (ActiveUIs / InactiveUIs). A hidden copy gets no real key press, so the test opens it with that call.

## Tests
- `python3 ext/fpsmp/sp_scan_test.py <base.dylib> <new.dylib> --probe <abs path>/fpsprobe_plain.dylib` (needs
  ext/fpsaudit/mac/fpsprobe_plain.dylib: fake key presses through Input.anyKeyDown, stock popups). Fails on the old
  code: searches per press 13.9 (max 3.5), per popup 42 (max 10.5), title searches, and the SCAN-RULE limit (more than
  6 searches in a steady single-player phase: base 16 / 11 / 64 / 486).
- Pair tests on the lane dylib: rig smoke, mp2_test --ticks 3000 (host/join, fingerprints equal every tick, desync ->
  resync), left_prompt_pair --start (a guest leaves: session end path), autorejoin_pair (disarm / arm on rejoin, 0
  desyncs after), seethrough_pair (UnitObject.SetHUD slots: FAILS the same way on main 03ec427, its max alpha counts the
  disabled EmotionBubble icons; fingerprints equal). No test reads the world name tags (hud_track slots); that
  change keeps one slot per live HUD as before, only the search order differs. Results in PLAN.md.
- Windows: module build ok; win/inline_audit.py: the same 10 "inlined" methods as main (no hook site changed).
  Windows runtime not run (no VM in this lane).

## Task 2b (lead 13:15): the lookups on the loader kit's view_find (template rule)
- sdk/dsl_sdk.h = ds_modloader 6fc71e6 (merge 1c369a0). find_view_x and mpmenu_find_any ask dsl_view_find first when
  the kit has view_find: active-only callers stay active-only (the kit's records also hold hidden views:
  GameObject.activeInHierarchy check), '*' callers get a shown one first, else a hidden one. The own throttled search
  runs only without that kit (an older loader, the rig's own dylib). The quit action's SystemMenuView lookup
  (mp_game.c stock_quit_title) goes through mpmenu_view_now (kit NOW). rig.log says which path runs:
  "mpmenu: view lookups: the loader kit's view_find ..." or "own search ...".
- Not moved (no module call for them in the kit; dsl_view_assets is loader-only): the Resources.FindObjectsOfTypeAll
  sprite scans (mpmenu assets, max 3 per scene; portraits on the Multiplayer page; ui_game sprite_get every 3 s while a
  sprite is missing, session panels only). Test commands (ui_shot, mpm_slots, ui_texts, ui_tip view) stay direct.
- ds_modloader scan_rate_test.py --mods default (loader 78e6a3a + this pin, own copy FMP): multiplayer 0 searches at
  the title, the Load list and in the world (8 while loading, not judged); no SCAN-RULE multiplayer line. The run still
  FAILS on custom-modes (about 120 searches per 30 s, TitleMenuView: lane fpsul).
- ext/fpsmp/kit_esc_test.py <ds_launcher tree> <out>: the same driver under the loader, the Esc menu opened the game's
  way: kit path on, Esc-menu clones added. PASS.
