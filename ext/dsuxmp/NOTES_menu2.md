# dsuxmp2-menu (lane dsuxmp2, sub-lane menu)

Files: `src/mpmenu.c`, `src/fe_menu.c`, test driver `ext/dsuxmp/menu_test.py` + `run_menu.sh`.
Run: `zsh ext/dsuxmp/run_menu.sh "de 1728x1000 1.0 title,game,hub --mp <module> --tag after"` (takes one dsuxmp2 copy slot
with `~/ToDelete_2026-10-25/dsuxmp2/slot.sh`). The module = the multiplayer module as the loader loads it, with the fake mouse:
`LOADER=~/ds_launcher_wt_dsuxmp2/loader zsh ext/dsuxmp/build_mouse.sh <out>` (origin/main: `SRC=<git archive>/src`, and
`--lang <archive>/lang`). The copy UMN runs the loader build of `~/ds_launcher_wt_dsuxmp2/loader` (83636d0). The driver now
copies the code's own `lang/*.json` next to the module (the loader build's lang files are older: "Remove Tag", "Apply to a
New Save", "Host a Game" showed in English in de).

## What changed (player view)
- M10: "Remove Tag" (de "Markierung entfernen") is always whole, inside its row and the list, left of [Change], never over
  the name. Since loader 0.1.27 the loader moves the link right after the tag on the title line and cuts the "with <names>"
  part; a long world name still pushed it past the list's edge. Now (`tag_fit_frame`, every 10 frames while the Load list
  shows, after the loader's rebuild): no room = the "with" part ends in "...", then goes, then the world NAME ends in "..."
  ("Moonlight Bri..."), and the link sits right after the text. The whole name comes back when the tags hide (Show tags
  off) or the row is built again. A short name is never cut.
- CLOSE RULE: the title's Foreign Expedition window (Expedition Results list, a result opened from it) has the game's red X
  (36 units, title band top-right; a click = its Escape close). With it the list's Back (= close) goes, and the bottom bar
  hides when it has no button; Later, Apply to a New Save, OK and the "Already Brought Home" page's Back stay. (Bring Units
  opens in the hub lobby, a session: no X there, see below; its plain Cancel would go too on the title.)
- Expedition Results list: the line under the subtitle showed the text template's own word ("Abspann" / "Credits"); it is
  empty now.
- Test command `mpm_esc` opens the Esc menu again (c4c4b26 had shadowed it); `mpm_esc {list:1}` = our windows + red X only.
- Lobby cog (found by the fake-mouse click, de / ru at 960x540 UI 1.4): its tooltip had no room beside the scaled lobby and
  `ui_tip.c` place_card put it "inside", over the cog; a click then hit the tooltip and the Rules never opened. The card now
  goes below the element (else above) when it would cover it. (`src/ui_tip.c`: not in this lane's file list; 3 lines.)

## Decisions (question, pick, why)
- M10 how to make room: cut the NAME with "..." (after the "with" part), not a smaller label or a second line. The link keeps
  the game's size (readable at 960x540); the loader already cuts "with <names>" the same way; one line keeps the row's height.
- M10 where: a pass after whoever placed the link (us at Init, the loader after each rebuild), not in tag_case_show only:
  the loader moves the link after us, so only a check of the result holds. It acts only when the link is out of room or
  over the name; the loader keeps our cut name on its next rebuild (no tug of war).
- Lobby red X? No. Escape does nothing in the lobby (ux_flow F14: it would close the stock Esc view that carries the lobby,
  which comes back 0.5 s later); the lobby closes only with Start or Quit (Quit asks first). An X would need a close action
  the lobby does not have.
- FE window in a session (Esc menu root, "Back from the Expedition") red X? No: it stays on the game's Escape path
  (mpmenu_escape_ours) because the Steam friends picker, not on the kit's stack, opens over it; a kit registration (which
  the X needs) would let Escape close the window under the picker.
- "Later" in Back from the Expedition: kept with the X (it answers the window's question: apply now or later).
- CARD RULE: in these files the picker rows are whole buttons already (FE Bring Units save rows FE_SAVE*, unit cards
  FE_UNIT*: the button is the whole row / card, the tick is a text on it; Recent Worlds / friends rows have no tick). The
  load list's "Show single-player worlds" box and its text both toggle. Nothing to fix, so no check.

## Tests (menu_test.py)
- M10 (title Load list + Your save): a tagged, Modded world named "Moonlight Brigade of the Silver Lake" + a tagged
  "Ashfall": the link's TMP preferred width fits its box, not truncated, size 14, inside row and viewport, not over
  [Change] or the name's drawn end; Ashfall not cut. origin/main read-back has rects only (the link past the viewport).
- X_fe_results_red_x: the title Expedition Results window lists a red X (`mpm_esc {list:1}`); a fake-mouse click on it
  closes the window and the Multiplayer page stays.
- M5 clicks (regression guards, pass on both): the test build's fake mouse (Unity's own input path) on the drawn Pause
  ">" (host page), the lobby cog (Rules open) and Start (playing). The old EventSystem raycast probe `mpm_hit` found no
  hit in hidden copies at any scale (a probe fault); dropped. M5_chat dropped (lane dsdps owns the chat).
- Seeds: one Expedition result, already brought home (so the title list shows it and a session never opens it by itself).

## Runs (2026-10-25, copy UMN, loader 83636d0; logs ~/ToDelete_2026-10-25/dsuxmp2/dsuxmp2-menu/runs, shots .../shots/dsuxmp2-menu)
BEFORE = origin/main e7ba260 (module + its lang); AFTER = this branch (final build).
| check | BEFORE en 960 1.4 / de 1728 1.0 / ru 960 1.4 (de 960 1.4) | AFTER (all four) |
|---|---|---|
| M10_remove_tag_inside (+ _title_load) | FAIL / FAIL / FAIL (FAIL): past the list edge or under [Change] | PASS |
| X_fe_results_red_x | FAIL (no red X) | PASS |
| M3_solo_box_visible | FAIL (Yes 0.000) | PASS |
| M5_rules_panel, M5_recent_worlds, M5_hub_rules_panel | FAIL (no read-back / not scaled) | PASS |
| M5_lobby | FAIL at 1.4 (1.000 x), PASS at 1.0 | PASS |
| M6_hub_lobby_rules / _no_cog / _gear_matches_start | FAIL | PASS |
| M9_hostmods_button | FAIL (label outside its button) | PASS |
| K12_esc_column_on_screen | FAIL at 960 1.4 | PASS |
| M5_lobby_cog_click | PASS (lobby not scaled) | PASS; FAIL de/ru 960 1.4 before the ui_tip fix |
| M5_lobby_start_click | PASS | PASS (regression guard) |
| M5_rules_click | no read-back on origin/main | PASS (regression guard) |
