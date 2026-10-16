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
