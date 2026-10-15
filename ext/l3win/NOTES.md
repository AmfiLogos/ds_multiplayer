# L3 Windows start part (lane l3win, 2026-10-15)

Branches: ds_launcher `l3-win` (loader core/OS split + loader/start/win), ds_multiplayer `l3-win` (win/ module build,
wrig loader mode, tools). Pushed, not merged.

## What was built
- Loader core made OS-neutral (ds_launcher `loader/src/dsl_os.h`, `dsl_soft_os.h`): Mac start part moved unchanged to
  `loader/start/mac/` (host test 31/31); l1-core 7800778 merged on top (API 3 services, dsl_ui/reg/rows).
- `loader/start/win/`: baselib.dll proxy (`dsl_start_win.c`: PE build check, save folder -> DSRIG_DIR/data, Steam check
  off only when no signed-in Steam), MinHook engine behind the same dsl_hook API (`dsl_engine_win.c`), OS calls
  (`dsl_os_win.c`), soft restart OS part (`dsl_soft_win.c`), BepInEx host (`bie_win.c`), starter
  "Dungeon Settlers Modded.exe" (`ds_modded_win.c`, Mud Slogel icon), kit (install.bat / uninstall.bat / README),
  `build_win.sh` -> `loader/build/win/` + "Dungeon Settlers Modded - Windows.zip".
- ds_multiplayer `win/build_win.sh` `DSMP_TARGET=module` -> `win/build/mods/multiplayer/` (multiplayer.dll + ranges +
  mod.json with win-x64 sha256s). Same file `port/rig_main_win.c` (#ifdef DSMP_MODULE). Tools follow the checkout
  (DSMP_ROOT). pvp_install added on Windows (was missing). rename() replaces on Windows (dswin.c).
- `win/wrig.py` with `DSMP_WIN_LOADER=1` = the Windows copy through the loader kit (`win/wloader.py`).

## Proof (DSMP bottle, -batchmode, wine; never "DS Windows Test")
- mp2 xplay, Mac host (build/dsrig.dylib) + Windows guest through the loader + multiplayer.dll: 19/19, fingerprints
  equal every tick (before and after the l1-core merge; after: BepInEx installed but off).
- Soft restart (`soft_win_test.py`): title + world 12/12 (B loads A's autosave, same camera target); BepInEx off->on
  and on->off 5/5 each.
- BepInEx be.788 + Extended Hotbar (Nexus) + our loader + MP in one process: plugin loads, 0 hooks refused. A guest
  with it is refused by a host without it, with the mod named ("Extra: ExtendedHotbar 0"); with it on, the full-state
  check flagged a difference (the plugin changes saved hotbar data) -> it is correctly a gameplay mod.
- Kit from a clean state: uninstall.bat, install.bat from the zip in a path with spaces (671 MB copy in ~11 s on this
  Mac), starter first start copies the saves, game at the title with the "Mods" title button (L1) on Windows.

## Not proven / open
- Window swap of the soft restart (no window under -batchmode). Title case: A's loading screen never reports done in
  -batchmode on Windows, so the swap goes on 3 s after B is ready (dsl_soft.c); the world case uses the normal rule.
- Real Steam start (no Steam in DSMP); real Windows PC.
- custom-modes.dll: waits for the M4 split (no custom/ folder yet).
- Unity's own analytics folder (LocalLow\CanOpener\Dungeon Settlers\Unity) is still written by the copy (no saves).
- DSMP bottle registry: HKCU\Software\Wine\AppDefaults\DungeonSettlers.exe\DllOverrides winhttp=native,builtin
  (Doorstop under wine; a copy without winhttp.dll falls back to builtin).
- Unity "force single instance": A closes its own SingleInstanceMutex handle before it starts B (else B never starts).

## Update 21:40 (after l1-core 5988e29 merge, main merged into ds_multiplayer l3-win)
- Mod set across platforms (ds_launcher 1ead659 + 7e..): modset_lines = id@version:<sorted sha256s of every platform's
  listed files> (BepInEx: platforms.any), multiplayer left out; HELLO modset = sha256 of the lines, "" = none; old world
  keys with "multiplayer" migrated. dsload_modset.json: same rule + "bepinex" list.
- BepInEx off reason -> DSL_BIE_REASON -> DSLModInfo.reason ("Not running: ..." in L2's row).
- wloader: refuses unless the DSMP bottle is Graphics=null / Audio=""; watchdog stops the copy (no title in 3 min, 25 min
  max); install waits for a free bottle (install.bat refuses while any DungeonSettlers.exe runs there).
- Results: mp2 xplay through the loader 19/19 (B 1 desync = the planned resync step); soft restart title + world 12/12;
  BepInEx on: guest refused with "Extra: ExtendedHotbar 0". M2 RVAs (RefreshLoadPanel etc.) map on Windows (gen_port ok).
- Open: Extended Hotbar's mod.json was written before hashes were added, so its line has no hashes (new plugins get them).
