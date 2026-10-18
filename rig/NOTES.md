# Test rig notes (2026-10-12)

## Status
`rig.py smoke` passes: A alone, then A and B at once. Each copy reaches the title screen, loads a copy
of a dungeon save (ElvenExpedition autosave, from `~/DungeonSettlers_SaveBackups/2026-10-11-1827`, map 1, region 1),
lists units, moves Ivan 3 cells (443,105 -> 446,105), reads two fingerprints (the tick advances) and saves a camera
screenshot showing the dungeon room. The owner's prefs plist and UserSetting.json were byte-identical after every run.
His saves folder tree was unchanged. Results: `smoke_out/results.json`, `smoke_out/pair/{A,B}_dungeon.png`.

## Commands
```
cd ~/ds_multiplayer/rig
python3 rig.py start A|B|both [--window]   # default -batchmode (no window at all)
python3 rig.py cmd A '{"cmd":"status"}'
python3 rig.py stop A|B|both
python3 rig.py check-prefs                 # compares + restores (only if his game is closed)
python3 rig.py smoke
./make_instance.sh A|B                     # rebuild a copy (start does it if missing)
../src/build.sh                            # rebuild build/dsrig.dylib (stop copies first)
```
Socket commands (one JSON per line, `instances/X/ctl.sock`): `ping`, `status`, `load_save {file|name}`,
`set_speed {speed 0..3}`, `list_units {map?}`, `order_move {guid,x,y}` (cell), `fingerprint`,
`screenshot {path,w,h}`, `quit`. Everything except ping/quit runs on the main thread from the
`MainFlow.Update` hook (queue drained before the original Update).
The socket opens only with `DSRIG_CTL=1` (rig.py / lrig.py `ctl_env()`; a player's game never sets it; Windows: only with
`DSRIG_PORT`). Commands that write or read a file at a given path accept only the rig folder and the folders in
`DSRIG_WRITE_DIRS` / `DSRIG_READ_DIRS` (set by `ctl_env()`), never a path with "..". Gate check: `rig/gate13/ctl_off_check.py`.

## How a copy is built (make_instance.sh)
- APFS clone of the installed app into `instances/X/DungeonSettlers.app`.
- GameAssembly = the Steam-signed `GameAssembly.dylib.stock_25846389` from the real install (read only),
  Steam gate patched at 0xD25730 (`SteamRuntime.get_IsEnabled` -> `mov w0,#0`), ad-hoc signed.
- **Trap found:** the live install carries the weapon-slot mod's asset changes (`aa/catalog.bin` and the
  `uiprefab_assets_all_*.bundle`). With a stock GameAssembly they make `QuickSlotUI.Init` throw
  `KeyNotFoundException '4'`, the `CompleteCampaignSetup` coroutine dies, and map loading hangs at stage 2 forever
  (ticks still run, units never move, the screenshot is empty). The script restores both files from
  `~/ds_weaponslot/b25846389/assets/work/stock_aa`, only after checking them against Steam's seal (real app's CodeResources).
  Every sealed resource of the copy now matches Steam.
- Frameworks: only GameAssembly + UnityPlayer stay; the owner's backups/mod dylibs go to `instances/X/fw_unused/`
  (unsigned files block codesign). `steam_appid.txt` moved out of MacOS for the same reason.
- Info.plist: `CFBundleIdentifier com.CanOpener.Dungeon-Settlers.mpX`, `LSBackgroundOnly`, `LSUIElement`;
  main executable re-signed ad-hoc (no hardened runtime).

## How it runs (rig.py)
- `rig.py _guard X` is the game's parent: lowers priority in the child (nice 19 + `PRIO_DARWIN_BG`, the same
  calls as `taskpolicy -b`) then execs the game binary directly (never `open`, never LaunchServices activation).
  **Trap:** `nice`/`taskpolicy` are SIP-protected, so exec'ing through them strips `DYLD_INSERT_LIBRARIES`.
- Injection: `DYLD_INSERT_LIBRARIES=build/dsrig.dylib` (stock main exe had `allow-dyld-environment-variables`;
  the copy is ad-hoc anyway). The dylib waits for GameAssembly with `_dyld_register_func_for_add_image`, checks the
  arm64 LC_UUID, then writes two entry hooks in memory before any game code runs (stub design copied from the
  dsmods loader; cave 0x604B000, slots 0x6B9B800).
- Saves: `Application.get_persistentDataPath` (0x507B74C) is hooked to return `instances/X/data`.
  HOME and CFFIXED_USER_HOME point to `instances/X/home` (crash reporter cache lands there).
  `-logFile instances/X/player.log`. Game ErrorLog: `instances/X/data/ErrorLog.txt`.
- Prefs: Unity writes `~/Library/Preferences/com.CanOpener.Dungeon-Settlers.mpA.plist` / `.mpB.plist`
  (cfprefsd ignores HOME). These are the copies' own domains; the owner's plist is untouched.
- UserSetting.json per copy is regenerated each start from the backup: all volumes 0 + mute, VSync off,
  960x540 windowed. Each frame the dylib also sets `AudioListener.volume = 0` and (every 60 frames)
  `AudioListener.pause = true`, `targetFrameRate = 30`, `vSyncCount = 0`, `runInBackground = true`.
- Guard: polls the copy's windows (`tools/winlist`, CGWindowList, all spaces) and the frontmost app
  (`lsappinfo front`) every 0.25 s for the first minute, then every 1 s; SIGKILLs the copy (by PID) if a window
  with alpha > 0 is on screen or the copy is frontmost. Logs the exit code/signal to `instances/X/guard.log`.

## Hidden-window method
1. Default: `-batchmode`. Gameplay, campaign load, A* paths and camera rendering all work; there is no window.
2. Fallback `--window`: `DSRIG_HIDEWIN=1` swizzles NSWindow init (alpha 0, ignores mouse, sharing none,
   transient collection behaviour, very low level), `setAlphaValue:` forced to 0, `makeKey/MainWindow` and
   NSApplication/NSRunningApplication `activate*` made no-ops; windows re-hidden every 30 frames.
   Measured: the only window had alpha 0.000 from the first sample, frontmost app never changed.

## Speed / load numbers
Under background priority a copy runs about 8-11 frames/s, but the sim stays at 60 ticks/s (the game runs
several ticks per frame). Boot to title about 40-70 s; save load about 10-25 s in batchmode.

## Observations for the determinism lane
- In the pair run, B's `Random.state` did not change between two fingerprints 1 s apart, while A's did.
  Same save, same order, different RNG consumption: frame-rate-dependent draws, as the audit predicts.
- Enemy units report task -2: they have no TaskReader component (behaviour tree units).

## Open issue
- 13:04 one window-mode run (pid 16627) died silently during boot, before the guard became the game's parent
  (so no exit code recorded). Not reproduced in 6 later runs. If it happens again, `guard.log` now shows the
  return code / signal.
- `screenshot` renders `Camera.main` only (no screen-space UI). `ScreenCapture` is stripped from the build.

## Determinism layer (2026-10-12, determinism lane)
Lockstep test: `python3 det_test.py --ticks 6000` (two copies, same save, scripted orders, A 10 fps / B 30 fps,
different ticks per frame) then `python3 desync_diff.py`. New socket commands: `det_seed`, `load_save {det:1}`
(auto-lock), `lock`, `unlock`, `run`, `step`, `schedule`, `detail`. Full notes: `../src/DETERMINISM_NOTES.md`.
`build/det_ranges.tsv` + `build/methods.tsv` come from `tools/gen_ranges.py`. The socket now sets SO_NOSIGPIPE
(a timed-out client used to kill the copy with SIGPIPE).
