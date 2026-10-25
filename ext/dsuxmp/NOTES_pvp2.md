# dsuxmp2-pvp: CLOSE, MOVE, CARD on the PvP windows (owner rules 2026-10-25)

Files: `src/pvp_ui.c`; test only: `src/ui_teams.c` (inside `#ifdef TEAMS_MOUSE_TEST`), `ext/dsuxmp/pvp_pair.py`,
`ext/dsuxmp/build_mouse.sh`. No new strings. `src/mpmenu.c` not touched (the menu helper owns the `mpm_esc` fix, 7bc1904).

## What the player sees
- CLOSE: the PvP Match window, the text Bring Units list and the result window have the game's red X (the loader kit's,
  `mpesc_close_x`) at the top-right of their title band: 36 units, 14 from the right edge, centred in the 60-high band.
  A click = the window's Escape close (PvP Match: Close; Bring list: Back; result: Back to the World). Made when the
  window opens; without the kit (the rig's own dylib, an older loader) there is no X and nothing else changes.
- The PvP Match window's bottom Close is hidden when the X is there (same action). Without the X it stays.
- MOVE: the three windows move with a right-drag (`dsl_movable`, keys `multiplayer.pvp_match`, `multiplayer.pvp_bring`,
  `multiplayer.pvp_result`) and keep their place (also after a rebuild: the kit puts it back). The M5 fit (`win_fit`)
  only sets the scale, never the position, so a move is never undone.
- CARD: in the text Bring list a left click anywhere on a unit's tile (portrait, level, the space between) picks or
  unpicks it, the same as its name button.

## Decisions (question, pick, why)
- Bring list "Back": kept. It goes one level back to the PvP window (a real Back, not a plain Close); the X does the same.
- Result "Back to the World" and PvP "Start Match", "Bring Units...": kept (main actions).
- PvP window bottom row without Close: Bring Units... stays centred, Start Match right. Least change.
- Result title box 520 -> 440 wide, so a long title never runs under the X (it auto-sizes when too long).
- CARD on the text list: the tile click is read like the HUD's Ready (Input.GetMouseButtonDown(0) on the tile's screen
  rect, an invisible measuring image), not an invisible button: a clone of the menu button could show its hover frame
  over the whole tile. A click on the name button stays that button's own click (never counted twice).
- The text Bring list only opens when the card window (mu_bring.c) cannot (no kit). So its red X and tile click are seen
  rarely in play; the test opens it with the test action `mpm_pvp {action:"bring_text"}`.

## Tests
`ext/dsuxmp/pvp_pair.py` (pair UPA/UPB, slot.sh 2 slots + rig lease, loader ~/ds_launcher_wt_dsuxmp2 83636d0). New checks:
ESC (plain `mpm_esc` opens the Esc menu), CLOSE x 7 (red X on each window via `mpm_esc {list:1}`, PvP Close gone, a
fake-mouse click on each X closes it / Bring list back to the PvP window), MOVE x 2 (right-drag moves the window by the
drag; it opens again there), CARD x 2 (click on the portrait spot, then on the empty part of the tile).
Fake mouse: `ui_teams {action:"mouse", x, y, button, x1, y1, frames}` = a drag (button down, move over `frames`, up);
old calls unchanged. The kit's movable reads the same UnityEngine.Input calls.

| build | de 960x540 UI 1.0 | en 1728x1000 UI 1.4 |
|---|---|---|
| BEFORE origin/main e7ba260 (`git archive`) | RESULT FAIL 4/27 | RESULT FAIL 4/27 |
| AFTER 2a344ff + 7bc1904 (menu helper's mpm_esc fix) | RESULT PASS 31/31 | RESULT PASS 31/31 |

BEFORE passes only setup, ESC (origin/main has no mpm_esc bug; it came with base c4c4b26) and the hidden-window guard.
Every CLOSE / MOVE / CARD check and M2 / P1 / P2 / M5 fails there. Runs: `~/ToDelete_2026-10-25/dsuxmp2/pvp/runs/`,
shots (local only): `~/ToDelete_2026-10-25/dsuxmp2/shots/dsuxmp2-pvp/`.

## Traps met
- Base c4c4b26: the new `mpm_esc` window list came before the old `mpm_esc` (open the Esc menu) in mpmenu_exec, so a plain
  `mpm_esc` stopped opening the menu. Fixed by dsuxmp2-menu 7bc1904 (plain = open + list; `{list:1}` = list only).
  Without it the ESC check fails; this test opens the menu with its own `mpm_pvp {action:"esc_menu"}` either way.
- `tools/close_x_guard.py` looks for `mpesc_close_x(&<window>, ...)` written out: a wrapper function hides the call.
- `data/Mods/ui_places.tsv` (the kit's kept places) is moved aside at seed, so every run starts centred.
