# fpsfix lane (2026-10-20): "the multiplayer mod lowers FPS, even in single player" (player report, Windows, v0.1.16)

Mac rig only (copy C, batchmode, 240 fps cap, no mp.json = single player, ElvenExpedition save). Windows not run.

## Causes (measured)
1. ui_game.c / ui_teams.c `find_view`: FindObjectOfType twice (active, then inactive) on EVERY call for views the scene
   does not have (PauseUIView, QuickSlotUI, EntityStatusUI/SkillUI, Task/Consume views, SystemMenuView), 10-60x/s.
   One lookup = 2.2-3.5 ms (up to 10 ms). macOS `sample`, main thread, single player:
   title 35.1% of the main thread in rig_frame (33.9% find_view); world 12.6% (9.4% mpmenu observe -> find_view_x).
2. mpmenu assets scan (all loaded sprites, ~16 500): 4.7 ms avg, 8.4 ms max per scan, 30 scans per process
   (every 300 frames; ITEM_CogWheel often not loaded kept it going). Owner rig.log: one every ~2.7 s for ~80 s.
3. mpmenu find_view_x throttle was 15 FRAMES: at 200 fps = 14 lookups/s per absent view.

## Fix
- `mpmenu_find_any` (mpmenu.c): the ui lanes' find_view goes through the absent-view throttle, keyed by class + mode.
- Throttle: retry after 2 s wall clock (was 15 frames), max one throttled lookup per frame. Immediate: 10 frames after a
  rig command or a stock popup (ui_game hk_activate_view), 3 frames after a key/mouse press. New scene / end of load:
  every absent view due again (still one per frame).
- banner_frame: no view lookups without a session (the banner needs one).
- assets scan: ITEM_CogWheel optional (pixel cog fallback; title/lobby builds still look for it); timer scans max 3 per
  scene (60 frames after it settles, then every 600 frames while a needed sprite is missing), never while loading.
- Test command `perf` (rig control channel): per-part frame cost, frame-time histogram, scans, view lookups per class.

## Numbers (module time per frame, rig_frame "all", avg / max ms)
| phase | before | after |
|---|---|---|
| title 60 s | 0.939 / 14.8 | 0.087 / 10.0 |
| world, 15-90 s after load 1 | 0.245-0.255 / 10.4-11.8 | 0.048-0.061 / 10.5-11.9 |
| world after load 2 | 0.332-0.348 / 16-19 | 0.063-0.068 / 10.8-18.7 |
| main-thread share (sample) title / world | 35.1% / 12.6% | 0.9% / 0.5% |
| asset scans per process | 30 (141 ms) | 3 (9.5 ms) |
Title frames in 60 s: 12165 -> 14336 (+18%; 240 cap, load-dependent). World runs at the 240 cap both ways.
Left: SystemMenuView (Esc menu, absent until first opened) ~2 lookups/s x 2.2 ms in a world.

Tools: fps_probe.py, lookups_probe.py, sample_probe.py + sym_tree.py (inclusive samples per function), with_dylib.py.
Check: ux_core_pair.py 6 PASS 0 FAIL on the fixed dylib. owner_flow_test.py fails "both running" on base and fixed alike.
