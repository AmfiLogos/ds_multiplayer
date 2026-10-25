# dsuxmp notes

## uxmp-hud

Scope: M7 (Bring Units window under our Teams button) and M5 (our HUD windows follow the game's UI Scale) of
`UX_REVIEW_2026-10-25.md`, files `src/ui_game.c`, `ui_teams.c`, `ui_friends.c`, `mu_ui.c`, `mu_bring.c`.

Decisions (question / pick / why):
- M7: hide our HUD under the card window, or draw the window above it? Pick: the window above. The game's own windows
  sit above its HUD (the portrait bar, clock and build menu stay visible under a popup and take no click through it).
  So the Teams button goes under the loader kit's layer while a card window is open (canvas order 30800 < 30900; else 31000
  as before), and none of our HUD takes a click, drag or hover on the window's rect (`mu_bring_hit`, asked by
  `ui_game.c ui_hit` for every Players panel / menu / viewer box / mini overview / off-screen mark hit test, and by
  `ui_teams.c hud_hit` for the Teams button). The button stays where it is; nothing comes and goes.
- M5: how to scale the Players panel and the other boxes of `ui_game.c`? Pick: the whole top layer (Players panel + cog
  rows, lend / player menu, viewer box, "sees this too" line, spectator mini overview, off-screen marks, our tooltip box)
  gets localScale = the game's UI scale (`mu_ui_scale`, read about 3 times a second in `plist_frame`). Why: every box on
  that layer already places itself in layer units through `scr_to_layer` / `pl_scale` (the layer's lossy scale), so places,
  drags, keep-inside and hit tests follow with no per-box change. The vote strip (root canvas, layer units) is drawn at the
  same scale. The panel's top under the portrait bar is converted from canvas to layer units (`pl_y0`).
- M5 fit: the panel (open cog list at 960x540, UI 1.4) is drawn smaller only as much as needed to fit from its top down to
  the screen's bottom with 16 px free; its top may go up to the portrait bar's bottom, never over the game's HUD (clock,
  resources), unless the player dragged it higher. Before, a too-tall panel was pushed up over the top-left HUD.
- M5 Teams button (`ui_teams.c`): drawn at the UI scale too (it sits right of the panel, which grows); its place right of
  the panel converts the panel's layer units to canvas units (`ui_layer_scale`).
- M5 Teams window: verified, it already follows the UI scale (`Refactor.UIScaleRoot`) and fits the screen (`win_fit`): review
  shots en_1728x1000 26_teams_host: window 1215 px wide at UI 1.0, 1430 px at 1.4 (fit-clamped), texts 1.17x. Not changed.
  The proposal side panel has UIScaleRoot too. The game's tooltip and notices are the game's own.
- M5 Steam friends picker (`ui_friends.c`): drawn at the UI scale, then scaled down to fit with 16 px free (the lobby
  window's rule in `mpmenu.c`).

Test: `ext/dsuxmp/hud_test.py` (one hidden loader copy UXH, `hud_make_instance.sh UXH` once). The multiplayer build under
test needs `-DTEAMS_MOUSE_TEST` (fake mouse through the game's own `UnityEngine.Input`). "Old" = origin/main plus only the
test probes (`hud_probes_on_main.patch`: the `ui_scale_probe` command, the drawn rects in `mpm_bring_state`,
`mpm_friends`, the Teams button's canvas order in `ui_teams`); no behaviour change.
