# dsuxmp (UX review 2026-10-25 fixes in ds_multiplayer)

## uxmp-menu

Files: `src/mpmenu.c` (all changes), test driver `ext/dsuxmp/menu_test.py` (+ `run_menu.sh` = the lane's copy slot,
`make_instance_menu.sh` = the hidden copy UMN, `btn.json` = `enum mpm_button` of `src/mpmenu.h`).
Run: `zsh ext/dsuxmp/run_menu.sh "en 960x540 1.4" "de 1728x1000 1.0"` (default: this worktree's `build/dsrig.dylib`;
`--mp <dylib> --tag base` = another build, e.g. origin/main's). Shots and logs go to `~/ToDelete_2026-10-25/dsuxmp/`
(never committed). The copy runs the shared loader build (`~/ds_launcher_wt_dsuxmp/loader/build`, 43b8c06) with the
multiplayer binary under test (its sha256 rewritten in the copied mod.json).

Decisions (question, pick, why):
- M3 box above the fade, or after it? After it: the box waits until the world's opening fade is over
  (ScreenFadeEffectView's material `_FadeAmount` reaches 1; 10 s at most). The game's own boxes after a load come once
  the world shows; a box over a black or clouded screen has nothing to read it against.
- M6 the hub lobby cog: hidden in the hub. The hub rules are set on the hub host page (the lobby model has no hub-rule
  arrows); the world rules the cog edits do not apply in the hub. The note under the rows: host "Visitors see these rules
  before they choose units." (the host page's own line), guest "Set by the host." (existing strings, no new key).
- M9 widen one button or both? Both cells of the box's GridLayoutGroup grow to the longest label (the game's boxes keep
  one button size), only for our boxes, only when a label does not fit at its normal size; stock size back with the
  stock text.
- M5 UI scale getter: `mpm_ui_scale()` in mpmenu.c reads the same `UIScaleManager.Instance.Scale` as `mu_ui_scale()`
  (mu_ui.c), by name through il2cpp, so it also works in the rig without the loader kit (mu_ui_scale needs cm_il, which
  only the loader module sets up). Exported in mpmenu_kit.h for the other windows.
- M5 the chat: CanvasScaler reference = 1920 x 1080 / scale (the brief's rule). The chat's places said "540" (half of a
  1080-high canvas) in 6 spots (clamp, default place, drag): now `ch_hh` = 540 / scale. Nothing else in the chat code.
