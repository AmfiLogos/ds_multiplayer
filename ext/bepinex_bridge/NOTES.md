# BepInEx bridge (lane ext/bepinex_bridge, 2026-10-15)

Goal: C# mods (BepInEx 6 IL2CPP be.788, Windows) can (a) see the multiplayer session (on/off, role, player, mod set)
and (b) send gameplay actions through our lockstep order path instead of applying them locally.

## Design (narrowest that works)
- Native side (`src/mp_bridge.c`, `src/mp_bridge.h`): 11 plain C exports (`dsmp_bridge_*`) from our mod library
  (Windows `baselib.dll`: `__declspec(dllexport)` is enough, no .def change, checked; Mac `dsrig.dylib`).
  - info: `api_version`, `active`, `role` (0 none, 1 host, 2 guest, 3 spectator), `player`, `step`, `applying`,
    `modset` ("rule\nhash\nlines", from the launcher session file, mp_session_mods).
  - `send(channel, data, len)` -> ONE mp order, type 50 (`MP_BRIDGE_TYPE`), zero owner Guid (open to everyone),
    args = u8 ver 1, u8 channel length, channel, data. Runs on every peer at the stamped step, inside the sim scope.
    - `dsmp.*` channels: native actions (same C code Mac + Windows, no plugin needed on any peer). Now: `dsmp.gold.add` (i32).
    - other channels: the managed dispatcher (`set_dispatch`), i.e. the plugin's C# handlers. A peer without it counts
      `unhandled` + logs (that peer can differ).
  - `order(type, owner, args)`: pass-through to mp_order for MOVE (1) and ORD (2) only, normal owner check.
  - spectators: send/order refused (-2). No session: returns 1 (caller applies locally).
  - rig commands `bridge_status`, `bridge_send {channel, hex}`.
- Hooked in with 3 lines in mp_game.c (`mp_bridge_attach` next to `ord_mp_attach`) + one dispatch line in rig_cmd.c +
  mp_bridge.c in build.sh. Ported onto main (after the M4 split) by lane mpfix 2026-10-16: src/build.sh, Windows gen_port takes every src/*.c (multiplayer.dll exports all 11 dsmp_bridge_*, checked).
  Not merged to main. The lane folder ~/ds_multiplayer/ext/bepinex_bridge keeps the build copies (src/, lib/, inst/).
- C# side (`plugin/`, net6.0): `DsmpBridge.dll` (plugin `dsmp.bridge`): static `DsmpBridge.Multiplayer`:
  `Installed, Active, Role, Player, Step, Applying, Modset, Register(channel, handler), Send, Do(channel, data, local)`
  (Do = send in a session, run the handler now in single player), `AddGold`, `Order`. Finds the exports with
  GetModuleHandle("multiplayer.dll") (mod loader module, L3) then "baselib.dll" (old friend kit) + GetExport (Windows) or dlsym(RTLD_DEFAULT) (Mac, untested: no loader on Mac).
  Dispatcher = `[UnmanagedCallersOnly]` function pointer; handler exceptions are caught and logged.
  `DsmpBridgeDemo.dll`: test plugin (trigger file `<DSRIG_DIR>/bridge_demo.trigger`: `gold N` / `note text`;
  writes `bridge_demo.status`).
- Mods already get some sync for free: game UI calls that ord hooks (Tier A/B) made by a Harmony patch outside a
  world tick are captured and replayed like the player's own clicks.

## Limits
- Mac has no BepInEx (research/bepinex_compat.md): on a Mac + Windows pair only native `dsmp.*` actions and
  MOVE / ORD orders apply on both. Managed channels need the same plugin on every peer (Windows-only pairs).
- Bridge plugins are not in the HELLO mod set yet (MORNING_QUESTIONS: launcher should list BepInEx DLLs).

## Build / test
- `./build_mac.sh` -> build/dsrig_bridge.dylib; `./build_win.sh` -> build/win/baselib.dll. Both build from the
  checkout src (BRIDGE_SRC, default ../../src); all outputs stay in the lane folder (lane copies of
  win/port/gen_port.py and gen_def.py in tools/, output paths changed only). Since main has zlib + fe in the
  Windows port (2026-10-15 evening) no lane workaround is needed any more.
- C#: `cd plugin/DsmpBridgeDemo && ~/.dotnet/dotnet build -c Release -o ../../build/plugins` (refs in lib/ copied
  from the be.788 install).
- Unit: `tests/run_unit.sh` (stubs, ASan/UBSan): all PASS.
- Windows copy: C:\DSW_BRG (APFS clone of DSW_BIE; ExtendedHotbar and the old baselib.dll moved to disabled_plugins/),
  instance ext/bepinex_bridge/inst/W_BRG, runner tools/brg_wrig.py (winhttp=n,b).
- Pair: `python3 rig/rig.py lease bepinex` then `python3 tools/brg_wrig.py xplay <pair> tests/bridge_xplay.py`.

## Pair test result (2026-10-15 21:20, pair GH: Mac host G + Windows guest W_BRG in bottle DSMP)
- PASS all: plugin loads next to our baselib.dll and sees the session (Guest, player 1); Mac host role Host, no
  dispatcher; the W plugin's Multiplayer.AddGold(250) applied once on both (gold 51 -> 301 on both, same step);
  guest full-state checks after it all "equal"; 0 desyncs on both; fingerprints equal (1225 common ticks).
- INFO (expected limit): managed channel demo.note ran on W (applied_managed 1), Mac counted it unhandled 1.
- Run 1 (21:08) had the same PASS for the action and 15202 equal fingerprints; its full-check wait read the host's
  full_ok (the host only sends; the guest compares), fixed in run 2.

## Shipping with the L3 Windows loader (l3win answer, 2026-10-15 evening)
- Multiplayer code moves to <ROOT>\mods\multiplayer\multiplayer.dll (loader module, all symbols exported): the plugin
  looks up multiplayer.dll first, then baselib.dll. Rebuilt; the 21:20 pair test ran the baselib.dll path (not re-run).
- Mod folder: `tools/make_mod.sh` -> mods/dsmp-bridge/ (DsmpBridge.dll + mod.json: kind bepinex, scope gameplay,
  hidden). The loader copies enabled bepinex mods to BepInEx\plugins\<id>\ and lists them in modset_lines (HELLO).

## Mac host with BepInEx (2026-10-15 21:30, pair GH, run with `--mac-bie`)
- Mac host started the way the l1-core launcher does it (`tools/mac_bie_host.py`): l1-core loader (l1-core 2946347,
  built in l1/loader with MP_PIN = this branch) + multiplayer.dylib module (RTLD_LOCAL) + Doorstop + bepinex-mac 0.1.1
  + DsmpBridge + demo via BEPINEX_PLUGIN_DIRS. First start (interop generation) took 130 s to the title.
- DsmpBridge on Mac: RTLD_LOCAL module is not in RTLD_DEFAULT, so the plugin scans the loaded images for
  multiplayer.dylib (then dsrig.dylib) and takes a RTLD_NOLOAD handle. Found ("native multiplayer bridge found").
- PASS all: Mac role Host with the managed dispatcher; native AddGold(250) on both; C#-HANDLED demo.gold (the plugin's
  own handler sets ClanDataContainer.ClanGold through the interop types) 100 from W + 40 from the Mac: 301 -> 441 on
  both (Mac log: step 2340 player 1, step 2467 player 0); full checks after it equal; 0 desyncs; fingerprints equal
  (1951 ticks); managed demo.note handled on both (unhandled 0).
