# Lane melon (v13 WP ML): MelonLoader mods through our loader, Mac (Apple chips) and Windows

Branches: ds_launcher `melon` (worktree ~/ds_launcher_melon), ds_multiplayer `melon` (worktree ~/ds_mp_melon, this folder).
Forks, builds and downloads stay in ~/ds_multiplayer/ext/melon/*.noindex (never committed).

## Result (2026-10-16)

| Proof | Result |
|---|---|
| Mac ML core, copy Q: HelloMelon (2 Harmony postfixes + injected MonoBehaviour) next to dsload + multiplayer (`tools/ml_core_test.py`) | 8/8 |
| Mac through the loader, `loader/test/l1_ml_test.py` (install at first use from our zip, Apply soft restart, mixed BepInEx + melon set refused, back off) | 17/17 |
| `l1_bie_test.py` on copy Q with this dsload (`tools/l1_bie_on_q.py`) | 14/14 (one earlier run: guard killed C because it became frontmost, see "Open") |
| Mac pair mp2 (host C stock, guest D with ML + HelloMelon), `tools/ml_mac_pair.py` | 19 PASS 0 FAIL (without patch 8 the guest died at the chat) |
| Extended Hotbar ML build (Nexus) on Mac | its own check refuses ("10-Slot-Erweiterung sicher deaktiviert ... FileHash ... GameAssembly.dll not found"), game stable 45 s, frames move. Accepted (owner v12-16), not bypassed |
| Windows (wine DSMP), official ML 0.7.3, kit + starter, `tools/wml_test.py run` | 11/11, first start 260 s (downloads + interop), next start 55 s |
| Windows ML off (profile without melon mods): Loader.cfg `disable = true`, no ML log, multiplayer runs | 7/7 |
| Windows: Mac host + Windows guest mp2 xplay, guest with ML 0.7.3 + HelloMelon + guard plugin (`tools/ml_xplay.py`) | 19 PASS 0 FAIL (HelloMelon 2 patches, 38 ticks, 0 ML errors; chat "text MISSING" as without ML) |
| Builds: loader/build.sh, build_win.sh, loader/test/run_tests.sh | green (31/31) |

## Mac: our MelonLoader build

Source: MelonLoader alpha-development 7c549cc (research/loadertest.noindex/ml) + `patches/melonloader-7c549cc.patch`;
Il2CppInterop = our arm64 fork (ext/bepinexmac, e7c52f6, global.json rollForward patch) packed as `1.5.3-dsmac.1` into
`feed.noindex` (local NuGet feed; a suffix, because a different `1.5.3` is in the NuGet cache). `tools/build_ml.sh` builds
everything into `inst.noindex/ml`; `tools/package.sh 0.1.0` makes `out.noindex/pkg/melonloader-mac-arm64-0.1.0.zip`
(52 MB, licenses + SOURCE.txt + manifest.json inside; no Cpp2IL / Unity libraries / interop: ML downloads or makes those
at the first start). The app installs it at first use: `app/melon/install_melon.sh` (sha256 pinned).

Local ML patches (all marked "DS LOCAL PATCH"):
1. Cpp2IL `--exe-name`: `ProcessName` is cut to 15 characters on macOS (from 2026-10-14).
2. `Il2CppICallInjector`: follow the 4-byte `b` export thunk of `il2cpp_resolve_icall` (from 2026-10-14).
3. `LoaderConfig`: base folder from env `MELONLOADER_BASEDIR` (a soft restart keeps the game's argv).
4. `MelonFolderHandler`: env `MELONLOADER_MOD_DIRS` (":" list, one folder per enabled melon mod, like BEPINEX_PLUGIN_DIRS).
5. `Il2CppInteropInjectorHelpersSetupFix`: on arm64 keep our fork's own hook choice (ML swapped in the x86 Unity 6
   GenericMethod hook -> "Required target method GenericMethod::GetMethod not found" = the 10-14 failure).
6. `BootstrapInterop.NativeHookAttach/DetachDirect`: arm64 follows `b imm26` thunks before detouring (same as BepInEx row
   8). Without it the 16-byte hook on Class::FromName (0x76b774, a `b 0x75cb1c` thunk) broke the next function -> SIGBUS.
7. Bootstrap `RemoveLibraryPreloadEnv`: no crash when DYLD_INSERT_LIBRARIES is already gone (the rig's dsrig.dylib clears it).
8. `Il2CppICallInjector`: a native caller of il2cpp_resolve_icall for an icall the game stripped keeps il2cpp's null.
   ML's managed stand-in, called from C, kills the game (multiplayer chat: `UnityEngine.Input::get_inputString`;
   fatal error in `JIT_ReversePInvokeEnterRare`). Only C# callers get the stand-in.

## Loader (ds_launcher branch melon)

- mod.json `"kind": "melon"`, plugin DLL sha256s under `platforms.any.files` (same shape as bepinex) -> modset_lines /
  HELLO check; scope gameplay unless `"scope": "client"`. dsload_modset.json gets a `"melon"` list.
- `src/dsl_ml.h`: dsl_ml_* (state / env / install / running / ready marker) per OS + dsl_net_* (one .NET host per set:
  `dsl_net_check` refuses BepInEx + melon together with the reason "BepInEx mods and MelonLoader mods cannot run
  together. Turn off the mods of one of them.").
- Mac `start/mac/dsl_ml_mac.c` (compiled through dsl_bie_mac.c so every dsload build has it): DSML_MELON_DIR / _SCRIPT,
  DYLD = dsload ... MelonLoader.Bootstrap.dylib, MELONLOADER_BASEDIR / MELONLOADER_MOD_DIRS; running = the Bootstrap's
  export `MLRealDlsym` is in the process. First start: same 300 s soft-restart timeout as BepInEx; marker ds_ready_<build>.
- Windows `bie_win.c`: official MelonLoader 0.7.3 x64 zip pinned (sha256 5b2b2f3d...a416 = GitHub's digest), unpacked in
  the game copy at first use; Mods\<id>\ = enabled melon mods (+ manifest.json, ML's rule for sub-folders, .dsmodded
  marker); `UserData\Loader.cfg` `disable = true` for a start without melon mods (0.7.3 has no env switch); a loose
  folder without mod.json whose DLL carries `MelonInfoAttribute` gets kind "melon". `dsl_ml_win.c`: the core's calls.
- Wine needs `version=native,builtin` for DungeonSettlers.exe (set in the DSMP bottle registry by wml_test.py install).
- Files of other lanes I changed on this branch (orchestrator13 asked, see "Open"): `loader/src/dsl_reg.c` (kind melon
  status, Apply through dsl_net_*), `app/stub/ds_modded.c` (melon_launch at the first start), `app/build_app.sh` (1 line).

## Windows guard

Same stripped-icall crash on Windows with the official 0.7.3 (CoreCLR "internal error", 0x80131506, right after
"Registered mono icall UnityEngine.Input::get_inputString"; the Windows guest died when the multiplayer chat was built).
The official build is not changed. The loader writes our MelonLoader plugin `plugin/DSMelonGuard` (embedded into
bie_win.c by build_win.sh with `xxd -i`) to `<game>\Plugins\DSMelonGuard.dll`. At OnPreInitialization (after
MelonLoader installed its il2cpp_resolve_icall hook, Core.cs:155) it detaches MelonLoader's hook, attaches its own on the
same function and gives MelonLoader's hook field the new hook (so MelonLoader's detour still reaches the original). Its
detour: a native caller (no managed frame above the IL stub) gets il2cpp's own answer; a C# caller goes on to MelonLoader's
detour. Log: "[DS loader guard] native code asked for the stripped icall UnityEngine.Input::get_inputString(): left null".
A first version with a Harmony prefix on the injector's methods never ran (on Mac and on Windows): not used.
Mac uses ML patch 8 in our build instead (no plugin in the Mac zip).

## Open
- One l1_bie_test run: copy C became the frontmost app during the soft restart and the guard killed it (next run 14/14).
  Not in this lane's files (soft restart activation).
- crossmods' win_mods_test.py stops the wine copy in `finally` even without the wine lock (relayed to orchestrator13).
- The Mac zip `out.noindex/pkg/melonloader-mac-arm64-0.1.0.zip` (sha256 b7a87649...6074, pinned in
  app/melon/install_melon.sh) is not published yet: it must be a GitHub release asset `melonloader-mac-0.1.0` on
  AmfiLogos/ds_multiplayer before the v13 ship (the BepInEx zip is `bepinex-mac-0.1.0`). A rebuilt zip gets a new sha256.
- Windows ML first start needs the internet (MelonLoader downloads Cpp2IL, the Unity libraries and .NET 6): 260 s here.
