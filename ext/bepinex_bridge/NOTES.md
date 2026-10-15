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
  mp_bridge.c in build.sh. On git branch `bepinex-bridge` (worktree ~/ds_mp_bepinex; base = v11 candidate src).
  Not merged to main. The lane folder ~/ds_multiplayer/ext/bepinex_bridge keeps the build copies (src/, lib/, inst/).
- C# side (`plugin/`, net6.0): `DsmpBridge.dll` (plugin `dsmp.bridge`): static `DsmpBridge.Multiplayer`:
  `Installed, Active, Role, Player, Step, Applying, Modset, Register(channel, handler), Send, Do(channel, data, local)`
  (Do = send in a session, run the handler now in single player), `AddGold`, `Order`. Finds the exports with
  GetModuleHandle("baselib.dll") + GetExport (Windows) or dlsym(RTLD_DEFAULT) (Mac, untested: no loader on Mac).
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
  worktree src (BRIDGE_SRC, default ~/ds_mp_bepinex/src); all outputs stay in the lane folder (lane copies of
  win/port/gen_port.py and gen_def.py in tools/, output paths changed only). Since main has zlib + fe in the
  Windows port (2026-10-15 evening) no lane workaround is needed any more.
- C#: `cd plugin/DsmpBridgeDemo && ~/.dotnet/dotnet build -c Release -o ../../build/plugins` (refs in lib/ copied
  from the be.788 install).
- Unit: `tests/run_unit.sh` (stubs, ASan/UBSan): all PASS.
- Windows copy: C:\DSW_BRG (APFS clone of DSW_BIE; ExtendedHotbar and the old baselib.dll moved to disabled_plugins/),
  instance ext/bepinex_bridge/inst/W_BRG, runner tools/brg_wrig.py (winhttp=n,b).
- Pair: `python3 rig/rig.py lease bepinex` then `python3 tools/brg_wrig.py xplay <pair> tests/bridge_xplay.py`.
