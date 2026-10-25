# dsuxmp2-hud (sub-lane of dsuxmp2)

Scope: owner rules CLOSE / ESCAPE / MOVE (2026-10-25) on the Teams window (`src/ui_teams.c`) and the Steam friends picker
(`src/ui_friends.c`), the one-press guard in `src/ui_game.c` escape_ours, and the proof of M7 (UX review 2026-10-25) that the
last lane (uxmp-hud, ext/dsuxmp/NOTES.md) coded but could not run. Base: dsuxmp2 c4c4b26 + 7bc1904 (mpm_esc opens the Esc
menu again; `{list:1}` = the window list only).

## What changed (what the player sees)
- Teams window (open, not folded): on the loader kit's Escape stack (mpesc_sync): one Escape closes it and the game does
  not get that press (no Esc menu). Its close is the kit's copy of the game's red X (mpesc_close_x, 34 units) where the
  Task Planner prefab's own CloseButton was (centred on it); the prefab button hides (one X). The kit X has the game's
  hover / press look; a click = the same close as Escape. The lend menu inside it is its own entry over the window
  (Escape closes the menu first, then the window). Older kit / the rig's own dylib: the prefab X and the old Escape check.
- Folded Teams bar ("Teams  Show  Close"): "Close" is now the game's red X (a copy of the prefab's CloseButton, 32 x 32),
  right in the bar, "Show" left of it. A click closes Teams as before (hit-tested, our click code).
- Teams window MOVE: a right-drag on it moves it (dsl_movable, key `multiplayer.teams`); the kit keeps it on the screen and
  puts it back at the kept place when it opens again. win_fit still scales it to fit; it no longer re-centres it after the
  dsl_movable call (that would undo the move). No kit: re-centred as before.
- Steam friends picker: on the kit's Escape stack; the game's red X at the top right of its 62-unit title band (34 units,
  14 from the right edge); its "Close" button hides when the X exists (same action). The title is 112 units narrower so a
  long translation never runs under the X. Older kit: Close button + old Escape (uf_escape through mpmenu_escape_ours).
- ui_game.c escape_ours: returns "taken" at once when the kit takes (or just took) the press (mpesc_took): with a window of
  ours on the kit's stack none of the HUD boxes (lend menu, panel settings, viewer box, mini overview) closes on the same
  press. (The kit already keeps the game's EscapeActionRequested from being made then; this is the guard if it is made.)

## Decisions (question / pick / why)
- The open Teams window already had the game's red X (the Task Planner prefab's CloseButton, hit-tested). Kit X or keep the
  prefab's? Pick: the kit's X at the same place, the prefab's hidden. Why: the owner's template (mpesc_close_x); same look,
  plus the game's hover / press colours; its click is the registered close (one place, same as Escape).
- Folded bar: a window for Escape or not? Pick: not a window (off the kit's stack): the owner's viewui rule (2026-10-15)
  says that while folded the world, the game's keys and Escape work. Its "Close" still becomes the game's red X (CLOSE
  RULE: one close control); the kit X cannot serve there (it closes only a window on the Escape stack, and a HUD-flagged
  entry gets no close), so it is a copy of the prefab's CloseButton, hit-tested like the text was.
- Teams MOVE: its layout set the position again (win_fit re-centred the window every layout). What was needed: centre only
  until dsl_movable took it. Done that way (scale changes stay). The full-screen click eater (the root) is not movable.
- X size: 34 (the template's 34-38), centred on the prefab button's centre (32) so it sits where the game draws it.
- The Bring Units overlap of M7 needs the card window over the world with the HUD showing. The PvP window lives inside the
  Esc menu, which hides the Teams button (lan_v9), so its own "Bring Units..." never overlaps; the overlap is the
  preparation HUD's "Bring Units" (pvp_ui.c PVB_HUD_BRING, needs a match) and the review's state 41. The test opens the
  card window through the PvP Bring action with the Esc menu shut (one copy, no match).

## Test (ext/dsuxmp/hud_test.py)
One hidden loader copy UXH (hud_make_instance.sh UXH), loader build ~/ds_launcher_wt_dsuxmp2/loader (83636d0, kit 0.1.28),
the loader's test module uikit-demo built from its test source (REAL Escape key events; its fake mouse hooks are refused,
the multiplayer test build's load first), multiplayer module with -DTEAMS_MOUSE_TEST (fake mouse through the game's own
UnityEngine.Input; now also a right-drag: ui_teams {action:"mouse", button:1, dx, dy}). New checks: C1a-d, C2a-c, C3 (see
the file's header). BEFORE = `git archive origin/main` + `hud_probes_on_main.patch` (the last lane's probes + the same fake
drag, test build only), AFTER = this branch; both built with hud_build_module.sh. The kit's kept places
(data/Mods/ui_places.tsv) are set aside before each run.

## Results (2026-10-25, one hidden copy each, logs ~/ToDelete_2026-10-25/dsuxmp2/hud/logs/, shots (local only)
~/ToDelete_2026-10-25/dsuxmp2/shots/hud/{before,after}/<lang>_<W>x<H>_ui<scale>/)
- AFTER (this branch) en 960x540 --ui 1.4: 16 PASS, 0 FAIL. de 1728x1000 --ui 1.0: 16 PASS, 0 FAIL.
- BEFORE (origin/main + probes) en 960x540 --ui 1.4: 4 PASS, 12 FAIL; de 1728x1000 --ui 1.0: 4 PASS, 12 FAIL. Every new
  check fails (M5a/b/c, M7a at 1.0 and 1.4, M7b, C1a, C1b, C1d, C2a, C2b, C3); the guards pass (M5d, M5e, C1c, C2c).
  BEFORE M7a: Teams button canvas order 31000 over the card window's 30900; M7b: the click opened the Teams window over
  the card window. BEFORE C1d shot: the folded bar shows "Show  Close" (text); AFTER: "Show" + the red X.
- M5e (last lane's guard) first failed on de 1728 AFTER: at UI 1.4 the open cog list makes the panel fit smaller (k 0.893);
  closing the list puts it back to 1.0 and the cog moves about 32 px, so a second click on the old spot missed. The test now
  clicks the cog where it is drawn. Left as it is (M5's fit rule); a player who clicks twice fast sees the cog jump.
- Host: ui_test 557/0, teams_test 0 failed, mp_test all pass, mpmenu_test all passed, mp_api_test 11/0; rv_gate ok;
  close_x_guard: no window of ui_teams.c / ui_friends.c listed (fe_menu, mu_bring, pvp_ui windows are other lanes');
  i18n check 0 missing / 0 stale / 0 bad (no new strings); rig build (dsrig.dylib + custom-modes) ok.
