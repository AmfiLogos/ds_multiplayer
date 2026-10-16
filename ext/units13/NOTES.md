# Lane units13 (v13 WP UNI) notes

Worktree ~/ds_mp_units13, branch units13 (from main a138cb2). Brief: research/briefs_v13/units.md. Interfaces: API.md.

## Step 1 skeleton (2026-10-16)
- custom/build_module.sh builds from folder lists (sources.txt -> custom-modes.dylib solo; sources_mp.txt ->
  custom-modes-mp.dylib, which now also links custom-modes.dylib, rpath @loader_path).
- custom/home/cm_home.c: title button "Custom Modes" (order 110 = right below the loader's "Mods" 100, no tip). Click =
  page in the title column: the stock column (stock buttons + MP clones + loader buttons) is switched off as a whole and a
  copy of it (Object.Instantiate of the column Transform: same RectTransform + layout group = same place and spacing),
  emptied, holds dsl_ui kit buttons (title-button clones) for the entries + Back. No hook; il2cpp by name through the
  host (ports to Windows). Two traps found: Instantiate a GameObject returns a GameObject (Component.get_transform on it
  crashes): instantiate the Transform; gchandles are pointer-sized on this Unity (uint32 truncation crashed).
- cm_module.c: dsl_plugin_init -> cm_home_init, then cm_units_init / cm_builder_init / cm_gallery_init / cm_fights_init
  (weak default definitions log "not in this build").
- Test copy U (loader layout, ~/ds_launcher/loader/test_env/instances/U, made by make_instance.sh U) with the test-only
  module units-driver (ext/units13/driver: uikit-demo's fake mouse + capture, targets from cm_test_target).
  ext/units13/test/cmrig.py (lease, guard, start/stop), home_test.py.

### Proofs
- home_test.py run 1 (solo: custom-modes + driver, no multiplayer): 11 PASS 0 FAIL. Run 2 (with multiplayer): 11 PASS
  0 FAIL. Checks: Custom Modes in the column right below Mods (siblings 8/9 solo, 34/35 with MP), no hover card, a real
  (fake-mouse) click opens the page, page = [Back], stock column off (Mods not on tree), Back closes, stock back, opens
  again, no guard violation. Captures: ext/units13/out/home_*/run*/drv/cap (r*_title, r*_home, r*_back).
  One earlier run 2 attempt ended with the copy killed by SIGKILL at the 8th request (not reproduced in the next run).
- rig smoke with this build (DSRIG_CUSTOM=~/ds_mp_units13/build/custom-modes.dylib, main's dsrig.dylib, pair AB):
  3/3 fp_tick_advanced, Foreign Expedition + PvP on (rc 0).

## Step 2: c1-units in custom/units (2026-10-16)
- From branch c1-units: src/myunits.*, wk_link.*, wk_http.c, pvp_wiki.* (its decoder edits) and custom/units/* moved into
  custom/units (main's pvp_wiki files had not changed since c1's base, so c1's versions were taken as they are).
  gen_wiki_tables.js (c1 edit) copied to custom/units/tools (ext/pvp untouched). Test paths fixed (Makefile, http/*.sh).
- c1's src/fe edits (fe_game.c mu_load, fe_menu.inc My Units tab) NOT carried: ext/units13/fe_from_c1.diff (398 lines)
  + ext/units13/NOTES_fe_from_c1.md, for lane fepvp.
- Solo dylib (custom-modes.dylib) now holds fe/fe_json.c + fe/fe_save.c (fe/sources.txt; pure C) and the units code
  (units/sources.txt); custom-modes-mp keeps fe_game/fe_menu, pvp/*, units/pvp_units.c and links them from the solo
  dylib. Nothing in the solo dylib needs multiplayer (nm -u: system symbols only).
- custom/units/.gitignore: test tmp/old/fixtures, binaries.
- mu_test: 148 PASS; NET=1: 160 PASS. FE custom guard (ext/foreign_expedition/tests/test_custom_guard.py): 18 PASS.

## Save reader (custom/units/mu_save.c, helper agent)
- Which units = FE's rule (fe_game.c fe_list_units: PlayerGuidList of every side; fe_save.c fe_export_unit: the guid has
  an entity). Save -> wiki build = C port of the builder's own "import a save" + the save's own portrait and acquired
  traits. Proofs (custom/units/test/save/run.sh): 6 real saves, 22 units = the builder's import (but the 6 portraits on
  purpose); round trip build -> pvp_build -> reader: 317/317 identical. Notes: custom/units/test/save/NOTES.md.

## Icons and texts (custom/home/cm_il.c)
- Items / skills / skill trees / affecters (race, background, inscriptions, soul stones, traits): Refactor.ResourceLoader
  LoadItemIcon / LoadSkillIcon / LoadSkillTreeIcon / LoadAffecterIcon (synchronous, work on the title screen; a missing
  key gives the game's default icon, treated as none). Portraits: Util.UnitVisualDatabase (namespace Util, not Refactor:
  first try missed it) GetPortraitSprite(key, false), found through Resources.FindObjectsOfTypeAll on the title screen
  (UnitVisualDataManager exists only in a world). Names: Refactor.TextKeyExtensions.GetText("TEXTKEY_<key>_NAME").
- Known: CJK names (a Chinese wiki team) draw as empty boxes: the kit's text font has no CJK glyphs.

## My Units window (custom/units/mu_window.c, 2026-10-16)
- Home page entry "My Units" (order 10). One window: "Add from wiki link" (one click: the clipboard is read; a link ->
  read on a worker, short links one GET; no link -> a paste box with Add / Cancel), New unit (only with the Unit
  Builder: cm_builder_open weak import), Close, a message line; left: All units + teams; right: a page of Compact Plus
  cards (mu_card_draw) with a tick each; bottom: "n selected", Save as team, Rename (name field + Save / Cancel), Edit
  (Builder only), Delete (notice box), Delete team (team shown), page < n / m >. Only the library file is written.
- Every window text and button label ends in "..." when it does not fit (TMP Ellipsis + no wrapping). The window follows
  the game's UI scale (UIScaleManager.Scale on the window's scale; room = layer / scale) and is built again when the
  kit's layer size or the UI scale changes (first-open trap above).
- Frame tick: mu_tick (mu_clip.c: CFRunLoopTimer on the main run loop / Windows SetTimer), 50 ms, between frames.

### Proofs (ext/units13/test/myunits_test.py, copy U, solo: no multiplayer; test library + private pasteboard)
- default scale: 26 PASS 0 FAIL (out/myunits_20261016-014608); UI scale 0.75: 26 PASS 0 FAIL; UI scale 1.4 (no net):
  21 PASS 0 FAIL. Checks: home lists My Units; one click long team link (4 units + team, no box, 0.3 s), one-unit link,
  short /fr/build/ and /t/ links (live), no link -> box, link put in the box + Add, Save as team (file has it), Rename
  to a 63-char name (ellipsis), Delete (notice OK), mu_test_overflow = 0 outside on every page (texts: rendered width
  <= box; icons inside their card), Close, no guard violation. Captures: out/myunits_*/drv/cap.

## Bring Units (lane units13 step 4, 2026-10-16)
- custom/units/mu_bring.c: the window of mu_bring.h (contract unchanged). Same look and code as My Units (mu_window.c):
  title, subtitle line, tabs "From a save" / "My Units" (gold text = the open tab; one source = a gold header, no
  tabs), "New unit" only when My Units is allowed AND the Unit Builder is in the build (cm_builder_open weak import; its
  Save reloads the library, opens My Units, ticks the new unit if the cap allows), the message line, a left column
  (saves, or "All units" + teams), a page of Compact Plus cards with a tick each (paging, never a long scroll), and a
  bottom bar "Picked n / max", < page >, Cancel, Bring (off with 0 picks).
- From a save: req->list_saves, else mu_save_files(<persistentDataPath>/Saves) newest first; the name comes from the
  file name ("MoonlightBrigade__AutoSave_..." -> "Moonlight Brigade"), the date is in the row's tooltip. Units:
  mu_save_units; card: mu_save_build_ex + mu_card_of, the save's own skill bar (x.bar) and portrait; a TEXTKEY_ name
  goes through cm_gettext. My Units: mu_build_of + mu_card_of. A team in the left column ticks its units, up to the cap
  (message when some did not fit).
- Picks are kept as mu_pick (save file + guid, or library id) and survive tab and page changes. A tick past the cap is
  undone with the message "You can bring N units at most. Untick one first."
- Bring / Cancel / mu_bring_close close the window first; req->done runs once, on the next tick (mu_tick), with a copy
  of the picks (Cancel: NULL, -1). Opening again while an answer waits sends that answer first.
- Escape = Cancel: Input.GetKey(Escape) polled on the 50 ms tick (a real key press is caught; the test driver's key
  sends down + up at once, so the test does not cover it).
- Trap found: on the first kit window of a run the kit's screen layer still reports its default size (960x540, layout
  comes a frame later), so mu_window made a small window in the top left corner. Bring Units checks the layer size on
  every tick and builds the window again when it changed (also covers a UI scale or window size change). The My Units
  window (mu_window.c / mu_window in mu_ui.c) most likely has the same first-open problem: not changed here.
- Test: ext/units13/test/bring_test.py (WITH multiplayer; saves copied from ~/DungeonSettlers_SaveBackups/2026-10-11-1827
  into <instance>/data/Saves; fixture library with 30 units and 3 teams in the run's out/lib). `--scale=0.75` sets
  GraphicSettingData.UIScale itself: cmrig's ui_scale changes the GeneralSettingData keys with "scale" in their name,
  which is only CameraMovementSpeedScale, not the UI scale.
- Copy: instance W (CMRIG_INST=W). V was in use by the loader-ui lane when this ran.

## Trap: "with multiplayer" in the test copy (fixed 02:15)
- cmrig first installed the rig's dsrig.dylib as mods/multiplayer/multiplayer.dylib: the loader refuses it (no
  DSL_PLUGIN), so the first home run 2 and bring_test runs were in fact WITHOUT multiplayer. cmrig.build_mp_module() now
  builds the real module from this worktree's src (loader build.sh recipe, mp_module.c read only). Re-run with it:
  home_test 22/22 (solo + multiplayer), bring_test 32/32, myunits_test 26/26, bring_rig_test 5/5; dsload.log shows
  "multiplayer: early start done" and "Foreign Expedition + PvP on".

## Bring Units from a rig command (multiplayer's test socket)
- cm_module.c registers "mu_" rig commands (units/mu_init.c mu_rig_exec) when multiplayer runs: mu_bring_open
  {"max","sources"}, mu_bring_state, mu_bring_result, mu_bring_close, mu_open, mu_state, mu_overflow.
- ext/units13/test/bring_rig_test.py: mu_bring_open (max 2) -> both tabs -> a save unit + a My Units unit ticked by
  clicks -> Bring -> mu_bring_result "2:1|<guid>|Sophia|ElvenExpedition...json|5;2|3e0b151998896644|Unit 0||10". 5/5 PASS.

## Card overflow coordinates (myunits_test default scale, 960x540 copy, game pixels x0 y0 x1 y1, y up)
- window [178,72,782,468]; card area [305,107,772,380]; cards [334,303,767,376] [334,226,767,299] [334,149,767,222].
  mu_test_overflow: 0 texts / icons outside their card or their box on every page, at UI scale 0.75, 1.0 and 1.4
  (My Units and Bring Units). Longest name tried: 63 chars (ellipsis); most items: the 8-unit fixture team.

## Windows
- Solo sources pass an llvm-mingw syntax check with the win/port flags, and link as custom-modes.dll except dsw_rename
  (the port layer, lane crossmods adds win/port). Portable now: cm_module.c (LoadLibrary / GetProcAddress, names
  multiplayer.dll + custom-modes-mp.dll), mu_save.c lock (SRWLOCK), mu_window poll name (dswin.h macro), mu_clip.c test
  stubs. Weak import of cm_builder_open: weak_import + -U on the Mac, __attribute__((weak)) on Windows (links to NULL).

## Not covered yet
- "New unit" -> Unit Builder -> back ticked: code path is there in both windows; no builder in the build yet (lane builder).
- CJK unit names draw as empty boxes (kit font).
