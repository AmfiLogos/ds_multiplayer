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
- Mac host started the way the l1-core launcher does it (`tools/mac_bie_host.py`): l1-core loader (l1-core 35a26b4,
  built in l1/loader with MP_PIN = this branch) + multiplayer.dylib module (RTLD_LOCAL) + Doorstop + bepinex-mac 0.1.1
  + DsmpBridge + demo via BEPINEX_PLUGIN_DIRS. First start (interop generation) took 130 s to the title.
- DsmpBridge on Mac: RTLD_LOCAL module is not in RTLD_DEFAULT, so the plugin scans the loaded images for
  multiplayer.dylib (then dsrig.dylib) and takes a RTLD_NOLOAD handle. Found ("native multiplayer bridge found").
- PASS all: Mac role Host with the managed dispatcher; native AddGold(250) on both; C#-HANDLED demo.gold (the plugin's
  own handler sets ClanDataContainer.ClanGold through the interop types) 100 from W + 40 from the Mac: 301 -> 441 on
  both (Mac log: step 2340 player 1, step 2467 player 0); full checks after it equal; 0 desyncs; fingerprints equal
  (1951 ticks); managed demo.note handled on both (unhandled 0).

## Mod-maker API v2 in C# (lane mpapi_bridge, 2026-10-18)
- DsmpBridge 1.1.0: `Installed` = native api >= 1 (1.0.0 wanted == 1 and would switch off on api 2). `NativeApi` = the version
  found. api 2 exports are looked up one by one (a missing one = that feature off, the rest works):
  `Rand(modId, stream)` u32, `RandRange(modId, stream, lo, hi)` (lo..hi-1, hi <= lo -> lo), `RandValue` [0,1) 24 bits,
  `RandShared` (a draw now is the shared one), `HasRand`, `Chat(modName, text, broadcast)` -> `ChatResult`
  (Shown 0, NoSession 1, BadArgs -1, Spectator -2, RateLimited -5, NotSent -6, NoNative -100).
  No multiplayer / api 1: Rand* = a local System.Random per (mod, stream) (seeded by time), Chat = NoNative. Nothing throws.
- `plugin/DsmpLite/DsmpLite.cs`: one file a mod pastes into its own source (internal class, no unsafe, no reference):
  Installed, NativeApi, Active, Role, Rand*, RandShared, Chat. It does not set the action dispatcher (that stays DsmpBridge's).
- Soft dependency (mod referencing DsmpBridge.dll that must still start without it):
  `[BepInDependency("dsmp.bridge", BepInDependency.DependencyFlags.SoftDependency)]`, every DsmpBridge call in a separate
  class with `[MethodImpl(MethodImplOptions.NoInlining)]` methods, called only when
  `IL2CPPChainloader.Instance.Plugins.ContainsKey("dsmp.bridge")`.
- Build: Directory.Build.props finds the reference DLLs (DSMP_BIE_LIB, ../lib, else ~/ds_multiplayer/ext/bepinex_bridge/lib);
  deterministic, no SourceLink, no embedded sources: a checkout and the loader's pinned-tree build give the same bytes
  (1.1.0: sha256 c5312b50...; SourceLink had put the checkout path into the DLL's pdb data).
- Shipping: ds_modloader `loader/modules/build_bridge.sh` (called by build_mods.sh after the strip) builds the DLL from the
  pinned ds_multiplayer tree into mods_all/{mac,win}/dsmp-bridge (mod.json: kind bepinex, scope gameplay, hidden,
  platforms.any); package_mods.sh -> dsmp-bridge-1.1.0.zip (DLL, mod.json, ds_multiplayer license). Its own zip, never
  inside the multiplayer zip: an enabled bepinex mod makes the Mac start install and start BepInEx.
  Scope gameplay: the bridge adds no state, but a peer without it (or with 1.0.0) does not run managed actions and draws
  a local random, so the join check must compare it.
- Test `tests/absent/run.sh` (dotnet console + a C stand-in multiplayer.dylib, no game): none / api 1 / api 2 native,
  and a mod running WITHOUT DsmpBridge.dll (soft-dependency pattern + DsmpLite): 27 checks PASS. It fails when
  Installed goes back to == 1 (checked) or a marshalled argument breaks.
- Probe `plugin/DsmpRandProbe` (test only) + `tests/rand_pair.py` (Mac pair, both peers dsload + multiplayer module +
  BepInEx clone per copy + DsmpBridge + probe). One-copy start smoke (17:44, api-1 module): 47 s to
  the title, bridge found api 1, Harmony postfix on Refactor.Tick.TickFlow.Tick installed.

## Rand pair result (2026-10-18 20:38-20:41, pair EF, multiplayer module from mpapi 47280ad, both peers BepInEx)
- PASS all: both peers api 2 with random, session on, Tick patch in; RandShared 0 in MonoBehaviour.Update on both.
- 12 draw sets compared (6 in bridge handlers sent from both peers, 6 in the Harmony postfix on TickFlow.Tick), each
  6 x Random.Range(int) + 6 x Random.value + 6 x Random.Range(float) + 6 x Multiplayer.Rand: same steps, Multiplayer.Rand
  equal in 12/12 with RandShared 1, UnityEngine.Random equal in 12/12 (0/6 handler, 0/6 Tick sets differ).
- Host det counters: draws_sim 381, draws_view_in_sim 0: the C# UnityEngine.Random calls (through Il2CppInterop) landed on the
  SIM stream, from a handler and from the Tick postfix. Chat broadcast from both peers: Shown.
- Full-state checks at steps 3600 and 4200 equal, 0 desyncs on both, fingerprints equal on 2283 common ticks.
