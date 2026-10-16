# crossmods13 (v13 WP XM, ds_multiplayer side): win/** and Windows tests

## Changes in win/
- Custom Modes is its own Windows module (like the Mac): `custom-modes.dll` (cm_module.c + every custom/<folder>/sources.txt)
  and `custom-modes-mp.dll` (cm_mp.c + every sources_mp.txt, linked to multiplayer.dll's and custom-modes.dll's import
  libraries). `build_win.sh` (DSMP_TARGET=module) writes build/mods/custom-modes/ with mod.json (+ win-x64 entry and file
  sha256s). The rig's baselib.dll keeps the mp part inside (CM_SAME_IMAGE), as before.
- `port/gen_port.py`: custom sources from the folder lists into build/gen_cm (vendored QuickJS / tinf copied unchanged);
  every src/ and custom/ HEADER that holds game addresses is translated into build/gen_inc (first on the include path).
- Bugs found and fixed on the way (all made the Windows copy die right after the title):
  1. src/mpmenu_kit.h, src/mp_game.h: static inline helpers with Mac RVAs (u_alive = 0x50FAF88 ...) were compiled
     untranslated (gen_port only translated .c). Superset of mpfix's gen_port_mpmenu_kit.patch (not applied: same fix).
  2. lld never resolves a WEAK undefined symbol from an import library: `weak_import` calls from custom-modes-mp into
     multiplayer.dll (mp_game_session, ui_mp.h) stayed 0. The mp part compiles with -Dweak_import=unused.
  3. Solo part: POSIX shims (dswin.h + its own dswin.c), -fwrapv, --embed-dir for #embed, WinHTTP, st_mtimespec forms.
- `port/rig_main_win.c`: crash note: the first access violation goes to rig.log with the address and every stack value
  inside a module (module + offset). This found bug 2 in one run.
- `wloader.py` watchdog and the tests stop only the wine processes they started (never another lane's copy). Wine busy
  check = process names (ps comm) younger than a day; `pgrep -fl wine` also matches shells that only mention wine.

## Tests (ext/crossmods13/)
- `win_mods_test.py <kit>`: 12/12 PASS (2026-10-16 04:28).  `mac_mods_test.py <loader>`: 8/8 PASS.
- `xplay_loader.py <loader>`: Mac host + Windows guest, both loader + multiplayer + custom-modes + gcd-cdr: mp2 19 PASS 0 FAIL.
- `hell_probe/` (test only): calls RecruitHelper.DetermineInitialEquipments("AFFECTER_Mage") on a helper whose campaign
  has a given CombatDifficulty, to prove the staff strip.
Details: ~/ds_launcher (branch crossmods) loader/modules/NOTES_crossmods.md.
