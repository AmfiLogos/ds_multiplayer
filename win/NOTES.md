# Windows port notes (build 25846389, 2026-10-12)

## 1. Files and dump
- `25846389/`: copies of `GameAssembly.dll`, `UnityPlayer.dll`, `DungeonSettlers.exe`, `global-metadata.dat`, `appmanifest_2798330.acf`
  (buildid 25846389), and `SHA256SUMS`. Taken from the CrossOver bottle, read only. Unity 6000.0.58f2 on both platforms.
- `25846389/dump/`: Il2CppDumper output (`dump.cs`, `script.json`, `il2cpp.h`, `stringliteral.json`). Metadata v31. The run used
  a copy of the dumper in `tools/il2cppdumper_nokey` with `GenerateDummyDll=false`:
  `~/.dotnet/dotnet tools/il2cppdumper_nokey/Il2CppDumper.dll ../GameAssembly.dll ../global-metadata.dat .` (from `dump/`).
- Windows `global-metadata.dat` is NOT byte-identical to the Mac file. Windows has 25626 types and Mac has 25602. The extra
  types are Windows interop (Interop.Kernel32.*, NtDll, BCrypt). Game types and names are the same. Never use TypeDefIndex
  across platforms. Use names.

## 2. Name map: `win_map.py` -> `win_map.tsv`
Re-run after an update: `python3 win_map.py --mac-dump DIR --mac-bin DYLIB --win-dump DIR --win-bin DLL` (about 5 s).
It reads every hex constant in `src/ research/ rig/ PLAN.md` and classifies each one against the Mac dump.
- method (326/326 ok): match on name plus signature. `MethodInfo_XXXX` is removed from the signature. The overload order
  breaks ties (no ties occurred).
- metadata slot (76/76 ok): TypeInfo and MethodInfo slots (`TI_*`, `md(...)`), matched on the script.json name.
- il2cpp export (2/2): `il2cpp_string_new` 0x6F9738 -> 0x49E320, `il2cpp_resolve_icall` 0x6F8B90 -> 0x49DED0.
- il2cpp internal (2/2, by vote): the Mac callers of each address are matched to their Windows twins, and the most
  common callee wins. `md_init` (codegen initialize_runtime_metadata) 0x6FB6FC -> **0x443AF0** (149/150 agree).
  `obj_new` (codegen object_new) 0x6FB9C4 -> **0x443D40** (150/150).
- 4 `code-inside` + 5 `other` rows are not addresses. They are bit masks in `rig_hook.c` and `rig_cmd.c`, the cave
  bounds in `rig.h`, and one old-build address in a research doc. None of these needs a Windows value. The arm64
  cave/slot scheme (`RIG_CAVE_*`, `RIG_SLOT_*`) does not port. Windows uses VirtualAlloc trampolines.
- Fields: every field of the 720 classes named in the sources: 3887 are the same and 13 differ. None of the 13 is a
  field that the mod reads. The 13 are in Passive_SelfRepair, Passive_SunHeartBloomAttack, the BehaviorDesigner
  Selector/RandomSelector/RandomSequence classes, and Steamworks.LobbyCreated_t. `dsnet_steam.c` already handles
  LobbyCreated_t (pack 4 on Mac, 8 on Windows, chosen by `cub`).
- **Layout trap:** over the whole dump, 1665 field offsets differ (82 of them in `Refactor.*`, for example the Task
  subclasses, DASText, DSDungeonPreDataGenerator). Cause: clang/Itanium puts a subclass's first fields into the base
  class's tail padding. MSVC does not. Examples: base ends with a bool at 0x20. Mac subclass field at 0x24, Windows at
  0x28. Rule: **never share a hard-coded field offset between platforms.** Take each offset from `win_map.tsv`, or
  generate it from each platform's `il2cpp.h`. For Windows, compile `offsets_check.cpp` with
  `--target=x86_64-pc-windows-msvc` against `25846389/dump/il2cpp.h` so that clang uses the MSVC layout.

## 3. Cross-play floats (Mac arm64 vs Windows x64)
Tools: `floatscan_xplat.py` (per-method op counts, `floatscan_xplat.tsv`), `dis2.py` (side-by-side disassembly),
`win_emu.py` (runs single Windows CRT functions in unicorn, CPU emulation only), `libm_compare.py` / `pow_compare.py`
(bit comparison with Apple libm on this Mac), `unity_icalls.py` (Unity native icall bodies on both platforms),
`win_crt_math.py` -> `crt_math.tsv` (Windows CRT math entry points).

**Basic arithmetic is the same on both platforms.**
- Fused multiply-add: over all 22.3 M arm64 instructions of GameAssembly, there are 2 `fmadd` in game/plugin code:
  `Pathfinding.AstarMath.HSVToRGB` (debug colour) and `DG.Tweening...Flash.WeightedEase` (tween). The il2cpp runtime
  has 22 more. One of them is in the `Math::Pow` icall, where it computes `y - 2*trunc(y/2)`, which is exact. No vector
  `fmla`. UnityPlayer arm64: 0 fused in 6 M instructions. x64: 0 `vfmadd*` in game code (40 114 method pairs scanned).
  The likely reason: IL2CPP writes one IL op per C statement, so clang has nothing to contract.
- Counts over the 1896 game/Pathfinding/Math methods with float work: arm64 mul 4754, add/sub 6960, div 1086.
  x64 mul 4612, add/sub 6387, div 912. The differences come from inlining (MSVC calls `sqrt`; clang uses `fsqrt`).
  No x87 (the 7 hits are capstone decoding data). Float stays float on both; double appears only where C# uses double.
- Unity native `Random.Range(float,float)` is the same algorithm on both platforms: xorshift128,
  `t = (r & 0x7FFFFF) * 1.192093e-7f`, `t*min + (1-t)*max`, with separate mul and add and the same constants.
  `PerlinNoise` core: 0 fused on both sides.

**Transcendental functions differ.** The Mac game calls Apple libm (libSystem, so it also depends on the macOS
version). Windows has UCRT statically linked inside GameAssembly.dll. Bit compare on the game's real inputs
(Windows SSE2 path emulated):

| Function (sim user) | Inputs | Differ (1 ulp) | Effect on game outcome |
|---|---|---|---|
| atan2f (CombatUtility.SnapTo8Direction, GridHelper.NormalizeToEightDirection / FindPositionWithDirection) | integer grid deltas, \|d\| <= 256 | 14 732 / 263 168 (5.6 %); Windows is the correctly rounded one | 8-direction result changed: **0** |
| atan2f | random floats | 11 537 / 200 000 | n/a |
| acos double (GridHelper.FindPositionsInCone, ExecutionArea Cone / ConeExceptMe) | cosines of integer offsets + random | 41 986 / 252 480 at double precision | after the code's cast to float: **0 / 117 120** |
| sinf/cosf vs Apple `__sincosf_stret` (VectorExtensions.Rotate, used by SKILL_RapidFireAttack only, a +-0.4 tile offset; BaseAreaPreviewService) | whole degrees, tenths of degrees, random | 304 + 291 / 208 642 (for example sin(210 deg): Windows 0.5, Mac 0.49999997) | not measured; RapidFire use looks like an FX offset |
| pow double (Math.Pow -> Edgar dungeon generator: SimulatedAnnealingEvolver, BasicConstraint energy) | pow(e, -d/T), random | 148 / 100 000 and 121 / 100 000; pow(int,2): 0 | not measured; generator layout could in theory change |
| fmodf, modf (Round), floor/ceil, sqrt | | 0 (exact by definition; fmodf 0 / 66 666) | none |

- **Windows-only trap:** the UCRT picks an FMA3 code path at run time. The flag is a global at RVA **0x520E4B8**
  (`(flag & 3) == 3` for atan2f, `!= 0` for sinf and cosf). This applies to atan2f, acos, asin, atan, sin, cos, tan,
  log, log10, sinh, cosh, tanh (see `crt_math.tsv`). So two Windows PCs with different CPUs can disagree. Unicorn
  cannot run AVX/FMA3, so only the SSE2 path was emulated. Writing 0 to the flag at mod start forces the SSE2 path
  everywhere. That is one 4-byte store. Under CrossOver the flag depends on what Rosetta reports in cpuid (not checked).
- Mac-to-Mac also depends on the macOS version (Apple libm). Not checked.

**Verdict:** Mac and Windows agree bit for bit in all plain float math. They do NOT agree in libm results. In the
combat uses that were measured, the discrete outcome did not change. Not proven: Edgar dungeon generation (pow),
BaseAreaPreviewService (sincos, Perlin), and RapidFire's Rotate. **Smallest fix that makes them identical:**
1. One shared C math file (musl, or CORE-MATH correctly-rounded functions) compiled with
   `-ffp-contract=off -fno-fast-math` into both mods.
2. Mac: rebind the GameAssembly import pointers (`__la_symbol_ptr` / `__got`) of about 28 libm symbols. This is a
   pointer write per symbol, with no code patch: atan2f, atan2, acos(f), asin, atan(f), __sincosf_stret,
   __sincos_stret, sin(f), cos(f), tan(f), exp(f), exp2f, log(f), log10(f), pow(f), sinh, cosh, tanh, hypot. modf,
   fmod(f) and ldexpf are exact and can stay.
3. Windows: jmp-patch the matching UCRT entries in GameAssembly.dll, about 20 RVAs. They are in `crt_math.tsv`, plus
   the float forms found at game call sites: atan2f 0x4DB470, sinf 0x4DA140, cosf 0x4D9850, fmodf 0x4D9A70. Also zero
   the FMA3 flag.

Cheaper fallback, if the owner accepts it: hook only atan2f, acos, sincos and pow (the measured sim users), and keep the
fingerprint check as the safety net. If dungeon layouts are generated on the host and sent to the others, pow drops
out.
Not checked: float->int conversion of NaN or out-of-range values. arm64 `fcvtzs` saturates, while x64 `cvttss2si`
returns INT_MIN. This only matters if game code converts a NaN or a huge value.

## 4. Windows loader design (no build yet)
Proxy candidates:
- UnityPlayer.dll imports `version.dll` and `winhttp.dll`. BepInEx/Doorstop uses winhttp.dll.
- GameAssembly.dll imports the game's own `baselib.dll` (12 imports).

| Option | Windows | CrossOver / Wine | Notes |
|---|---|---|---|
| **baselib.dll proxy** (rename the original to `baselib_orig.dll`, forward every export with a .def file) | yes | yes, no DLL override needed (Wine has no builtin baselib) | Loads as a dependency of GameAssembly, so GameAssembly is already mapped and relocated when our DllMain runs. Install hooks there (same role as `_dyld_register_func_for_add_image` on Mac). A Steam verify or update restores the file, so the installer must re-apply it. **Recommended.** |
| winhttp.dll / version.dll proxy | yes | needs `WINEDLLOVERRIDES=winhttp=n,b` or a bottle registry DllOverride (this changes the bottle, so ask the owner first) | Loads before GameAssembly. We would have to wait for it (LdrRegisterDllNotification). |
| BepInEx 6 IL2CPP / MelonLoader | yes | works with the same winhttp override | Large (.NET runtime, Il2CppInterop). Our mod is C, so this is overkill. |
| Own injector (CreateRemoteThread + LoadLibrary) | yes | possible, fragile | Needs a launcher process. Not recommended. |

Hooks: the arm64 hook (`rig_hook.c`: first-instruction branch to a cave stub) does not port. On x64, use MinHook
(BSD-2, small, builds with mingw). It needs a length disassembler and a trampoline within +-2 GB, or a 14-byte
`jmp [rip]`. Then VirtualProtect plus FlushInstructionCache.

POSIX/Mac-only parts and their Windows equivalents:
| Module | Mac code | Windows |
|---|---|---|
| rig_main.c | `__attribute__((constructor))`, `_dyld_register_func_for_add_image`, mach-o headers | DllMain(PROCESS_ATTACH) in the baselib proxy, `GetModuleHandleA("GameAssembly.dll")` as ds_base |
| rig_hook.c | mach_vm_protect, sys_icache_invalidate, arm64 stubs in a code cave | MinHook (or VirtualAlloc trampoline + VirtualProtect + FlushInstructionCache) |
| rig_sock.c | AF_UNIX socket at a path | loopback TCP (127.0.0.1:port) or a named pipe. AF_UNIX exists on Windows 10 1803+, but Wine support is uncertain. TCP is portable on both platforms. |
| rig_win.c | objc runtime / CoreGraphics window hiding | EnumWindows for our PID + ShowWindow(SW_HIDE) or move off screen |
| dsnet_link/server/client | pthread mutex/cond/thread, poll, fcntl O_NONBLOCK, pipe() wake-up, socketpair(AF_UNIX), SO_NOSIGPIPE, clock_gettime, Apple `pthread_cond_timedwait_relative_np` | winpthreads (ships with llvm-mingw, so most pthread code compiles as is) or SRWLOCK/CONDITION_VARIABLE; WSAPoll; ioctlsocket FIONBIO; replace pipe/socketpair with a loopback TCP pair; drop SO_NOSIGPIPE; QueryPerformanceCounter; absolute-time timedwait; WSAStartup |
| dsnet_steam.c | `_dyld_image_count/_get_image_name`, dlopen/dlsym of libsteam_api.dylib | GetModuleHandleA("steam_api64.dll") + GetProcAddress |
| fegetround (HELLO check) | fenv.h | the same in mingw. The arm64 FPCR.FZ check becomes an MXCSR FTZ/DAZ check on x64 |
| ABI | game function typedefs | the same with the default ms_abi. Careful: an 8-byte struct return (Vector2, Vector2Int) comes back in RAX on Win x64 and in s0/s1 or x0 on arm64. Declare struct types, never two floats. Our C frames need SEH unwind info so that il2cpp exceptions can unwind through hooks. clang-mingw x64 emits it by default. |

## 5. Toolchain (nothing installed)
Installed: Apple clang 21, Homebrew LLVM 20.1.2 (clang knows the x86-64 target, plus llvm-dlltool, llvm-rc,
llvm-windres, llvm-ar, llvm-objdump). Missing: a COFF linker (lld-link) and Windows headers and import libraries.
- **llvm-mingw 20261006 (ucrt, macOS universal)**: 124 MB download. It is one self-contained folder with clang, lld,
  mingw headers, winpthreads and UCRT. No brew changes. **Recommended.** Extracted size not measured; it is a few
  hundred MB.
- brew `mingw-w64` 14.0.0: 357 MB download, 1.45 GB installed (GCC based).
- brew `lld` 23: 6 MB itself, but it depends on `llvm` 23, so brew would upgrade the installed llvm 20 (about 1.7 GB). Headers would still be missing.
- brew `zig` 0.17: 37 MB download, 212 MB installed, plus llvm@22 and lld@22. `zig cc -target x86_64-windows-gnu` works
  without other parts.
- clang `--target=x86_64-pc-windows-msvc` + xwin: needs the Microsoft SDK/CRT download (license accept, about 1 GB). Not needed.

## 6. Windows build and first test (2026-10-12, afternoon)
Toolchain: llvm-mingw 20261006 (ucrt) in `~/tools/llvm-mingw` (769 MB unpacked). No brew changes.

Build: `win/build_win.sh` -> `win/build/baselib.dll` (proxy + rig + det + mp + dsnet + ord + ui + math).
- `port/gen_port.py` turns the shared `src/*.c` into `build/gen/*.c` on every build. Never edit those copies.
  It rewrites each Mac RVA/slot literal through `win_map.tsv` (re-run first), uses `RtlCaptureStackBackTrace` for
  rig_det's CALLERS (in place of the arm64 frame walk), and switches to `det_ranges_win.tsv`. It also rewrites
  socket close/read/write/send/recv to `dsw_*` and renames a module's own static `send()`.
  - It stops the build if a literal that looks like an RVA has no Windows twin.
  - It warns when MSVC has folded identical method bodies into one address. Random.Range(int) and
    RandomRangeInt share 0x3964BB0, so the second hook shares the first one. SetCombatTarget's body is shared
    by 253 methods, so its hook also sees their calls.
  - trn_* is left out, like the Mac build.
- `port/dswin.h` (force-included), `port/dswin.c`: Winsock shims. pthreads come from winpthreads (static).
  `port/posix/*`: empty POSIX headers, plus dlfcn (dlsym looks in our own exports) and spawn/wait (ENOSYS).
- `port/rig_main_win.c`: DllMain in the baselib proxy, MinHook (`third_party/minhook`). Hooks: MainFlow.Update,
  persistentDataPath, and the Steam gate. The Steam gate hooks `StoreRuntimeHandler.Start` 0xA3F240 to return
  Disabled. `SteamRuntime.get_IsEnabled` cannot be patched, because its body is the shared "return true" body
  0x5D3EE0. The rig also zeroes the CRT FMA3 flag twice: at load, and again at the first Update, because the CRT
  sets it after our DllMain. **Under CrossOver/Rosetta the flag is 3** (Rosetta reports FMA3/AVX2).
- `port/rig_sock_tcp.inc`: control channel on TCP 127.0.0.1:`DSRIG_PORT` (the test tools use 47811). Off when
  `DSRIG_PORT` is not set (no default port: test builds only). The port is also written to `<rig_dir>/ctl.port`.
- `port/steam_stub_win.c`: the Steam bridge is not ported. It reports "no Steam", so mp uses direct TCP.

Math: `win/math/` = musl 1.2.5 libm (MIT) with a dsm_ prefix, built with `-ffp-contract=off` and the
non-FMA paths. `build_math.sh` builds it for arm64, x86_64 and win64. `dsmath_test` (about 7 M results, NaNs
hashed as one value) gives the same 32 hashes on Mac arm64, Mac x86_64 (Rosetta) and Windows x64 (run in
DSMP). NaN bit patterns themselves differ by CPU: x86 sets the sign bit.
- `math/dsmath_win.c` redirects 24 UCRT entry points (`math/crt_patch_list.tsv`; each one checked by emulation
  with `verify_crt_list.py`). It is enabled only with `DSMP_MATH=1`, because the Mac side (rebinding the libm
  imports) is not written yet.

Test bottle: CrossOver bottle **DSMP** (win10_64). Settings: Graphics driver = null, Audio = "" (no window or
sound is possible). Game copy in `drive_c/DSW` (APFS clone): baselib.dll = ours, original = baselib_orig.dll,
UnityCrashHandler64.exe disabled. `win/wrig.py start|cmd|stop|procs`. Start flags: `-batchmode -nographics`,
nice 19. `tools/dsmp_guard.py` kills DSMP processes if one of them shows a window. The guard finds them with
lsof, because Wine hides process env from `ps -E`. It found 0 windows in all runs. Stop = SIGKILL by PID of
the processes that have files open under Bottles/DSMP. The owner's Wollay bottle was running the whole time
and was never touched.

Results:
- dsnet test under Wine: 9 tests pass. The multi-process test needs posix_spawn and crashes on Windows; it is
  not ported.
- W reaches the title, loads the ElvenExpedition dungeon save, and answers status, list_units and fingerprint.
- `xplay_test.py`: W (Windows) vs C (Mac). Same save, seed 12345, locked sim.
  - 600 ticks idle: equal at lock (tick 8938508, c1b24978122ed6b3) and at the end. All 600 per-tick fp.log
    lines are equal.
  - 4000 ticks with det_test's 16 move orders (units walk into the enemies): end hash 4d3fa3c5dface840 on
    both, and all 4000 per-tick lines are equal.
  - Both runs use the stock CRT and Apple libm on each side, so the libm 1-ulp differences did not show up in
    this scenario.

## 7. Combat cross-play and the math swap on both sides (2026-10-12, 15:00-15:40)
`xplay_test.py --ticks N --orders [--save FILE] [--math] --tag T` -> `instances/xplay_T.json`. It runs W
(Windows) and C (Mac) with the same save, seed 12345 and det_test's 16 move orders.

| Run | Start (players / enemies) | End, identical on W and C | End hash | Per-tick first diff |
|---|---|---|---|---|
| ElvenExpedition 6000 | 4 units hp 2635.2 / 20 units hp 57625 | 2 players dead, hp 697.1 / enemies hp 53459.2 | 5a9b933e7b0fb362 | none of 6000 |
| Golem (WikiGolem import) 6000 | 4 hp 37083 / Golem hp 18000 | Golem dead (hp 0), players untouched | b4d698a50d8dfa73 | none of 6000 |
| same two runs with `--math` (shared libm on both) | same | same | same hashes | none |
| ElvenExpedition 4000 `--math` | same | players hp 972.4 / enemies 52893.1 | 4d3fa3c5dface840 | none of 4000 |

The `--math` runs end with the same hashes as the runs with each platform's own libm. So these fights never
fed a libm result that differs between the two into the hashed state. The math swap breaks nothing, but these
runs do not show a case where it was needed. A case that would show it: skills or areas that use atan2f/acos
on borderline geometry, or Edgar dungeon generation (pow) running inside the lockstep.

**Mac math swap:** `win/math/math_mac.c` (new file, self-contained). Its own add-image callback points 27
`__la_symbol_ptr` slots of GameAssembly (`mac_libm_slots.h`, generated by `gen_mac_slots.py`) at the dsm_
functions. `__sincos(f)_stret` -> separate dsm_sin/dsm_cos, as Windows does. `exp2f` -> dsm_powf(2,x), because
on Windows the same game code reaches powf. hypot stays native on both sides (no Windows twin found).
It is on when `DSMP_MATH=1`, or when `<DSRIG_DIR>/dsmp_math.on` exists (rig.py does not pass env).
Test build: `math/build_mac_test_dylib.sh` -> `win/build/dsrig_math.dylib`.
Registration for the real build (src/build.sh), the only change needed:
```
  ... ui_game.c ui_model.c \
  ../win/math/math_mac.c -I../win/math ../win/math/build/libdsmath_arm64.a
```
(run `win/math/build_math.sh` once to build `libdsmath_arm64.a`). Windows: `DSMP_MATH=1` in the env, which
wrig.py passes through.

**What CrossOver is NOT (only a real Windows PC can confirm):**
- The C runtime: the game's own math is statically linked UCRT inside GameAssembly.dll, so that part is the real
  code. But Wine supplies kernel32/ntdll/ws2_32 and the D3D layer. Real Windows file paths, Winsock behaviour
  (WSAPoll, loopback, firewall prompt on the listen port), and `RtlCaptureStackBackTrace` unwinding are only
  tested under Wine.
- CPU: x64 runs under Rosetta 2, not on an Intel/AMD CPU. Rosetta reports FMA3 (the CRT flag read 3). The rig
  zeroes that flag, so the CRT FMA3 paths never ran. Plain SSE float maths is IEEE on both, but this is not proven on real silicon.
  Not tested: a real PC with and without FMA3/AVX2, an AMD CPU, and the FTZ/DAZ bits (MXCSR) as Windows sets them.
- Graphics: the Windows copy ran with `-batchmode -nographics` and the null driver. A real PC runs D3D11/12
  with a window. Any sim path that reads render or view state is not exercised (the Mac copy uses -batchmode too).
- Loader: the baselib proxy was only loaded by Wine's loader. Real Windows (and antivirus / SmartScreen on an
  unsigned DLL next to the exe) is untested. So are Steam updates restoring baselib.dll and the Steam overlay
  hooks. The Steam gate is on in all tests, so the real Steam path was never run.
- dsnet over a real LAN/Internet between a Mac and a Windows PC (only loopback inside one Mac so far). The Steam
  bridge on Windows is a stub.

## 8. Full multiplayer stack on Windows + Mac host / Windows client test (2026-10-13 night)
Build: every src module now goes into baselib.dll (rig, det, mp, dsnet, ord, ui, ui_teams, mpmenu, trn); 2026-10-13
src compiles. The Windows install order is the same as src/rig_main.c (DSRIG_LIVE and DSRIG_MINIMAL are handled).
- New shims (`port/posix`):
  - `mach-o/dyld.h`: empty image list, so mpmenu's Steam public-IP lookup reports "not available".
  - `ifaddrs.h`: over GetAdaptersAddresses.
  - `pwd.h`: no passwd database, so the player name falls back to `$USER`.
  - dlfcn `dladdr`, which also lets det_ranges be found next to the DLL.
  - `arc4random_buf` (BCryptGenRandom).
  - New Steam stub functions.
- All RVAs mapped; none failed. Identical-code folding warnings: see the build output. Hooks on shared bodies:
  SetCombatTarget (253 methods share it; trace only) and HashCode.GenerateGlobalSeed (2).
- **ABI audit** (`abi_audit.py` -> `abi_audit.tsv`, 62 call sites pass/return structs by value):
  - arm64 passes Vector2 in s0/s1 and Guid in x0/x1. x64 packs Vector2 into ONE integer register and passes Guid
    by pointer. Calls written with struct types (V2, V3, Col, R4, Guid, V2I) are correct on both.
  - Wrong on Windows and fixed:
    - 8 ui_game calls written `(float, float, Obj, void *)`. They crashed the Windows client in
      `RectTransform.set_anchoredPosition` (hk_clock_init). gen_port now rewrites them to `dsw_v2`.
    - 6 rig_det trace hooks that split a Guid/Vector2 (MoveThing, OnSpawnThing, GetWorldPosition,
      GetCenterOfWorldOccupies, AddHostile, RemoveHostile). They are NOT installed on Windows (trace only).
  - h_keeponly (Guid split) passes its arguments straight through and is correct by register position.
- Tools: `mp2_xplay.py <pair> [--math]` runs rig/mp2_test.py with copy B = the Windows copy.
  `ord_probe.py` checks order capture without mp. `DSW_DEBUG_ORD=1` logs the right-click chain.
  `wrig.py`: `DSMP_GRAPHICS=1` keeps a D3D device (bottle graphics driver must be "mac"; tested once, no
  visible window). The bottle is back on the null driver.

Result: `mp2_xplay.py EF --math` (Mac E hosts on 127.0.0.1:7831, Windows W joins):
| check | result |
|---|---|
| join: W loaded the host save through the join point | PASS |
| fight: fingerprints equal every tick | FAIL: W desynced 4 times (steps 1740, 4020, 5250, 6450); each time it auto-resynced from the host save |
| fight: orders through capture and dsnet | FAIL: captured A 16, **W 0**, applied 16 / 16 |
| fight: units identical on both at the same tick (paused) | PASS |
| fight: combat happened | FAIL (no HP lost on either side within the test window) |
| pause vote from W stops both; run vote resumes both | PASS / PASS |
| speed: lowest vote wins; fast = 3.5x; rate 58.8 / 200.4 ticks/s on both | PASS |
| refused order / floor vote | not reached (the test timed out at "the refused order reached both") |

First divergence: step 1740, about 110 ms after W's own injected right-click (SelectPlayerUnit +
InteractGridRequested). **Root cause found so far: W's order capture.** ord_probe without mp gives
captured 2 on the Mac and 0 on Windows. On Windows the click reaches
PlayerControlEventHandler.OnEvent -> DefaultCommand.RightClick(446,105), but RightClick never calls
InteractionRunner.Run (the Tier B capture hook). So W's order takes another branch, is not sent, and changes
only W's world. The same result with a D3D device, so it is not -nographics. Next step: follow RightClick's
branches on x64 (CommandCheck.CanCommand, the interface calls through 0x440B80) against arm64.
The lockstep sim itself still matches: `xplay_test.py --mac E --math --orders` 4000 ticks with today's src:
equal at lock and at the end (f26494e4bb44de52), all 4000 per-tick lines equal.
Wine caveat as in section 7: none of this is a real Windows PC.

## 9. Windows client capture fixed; full mp2 cross-play passes (2026-10-13, 03:20-03:51)
Two MSVC optimisations broke our hooks on Windows (clang on the Mac does not do them):
1. **Inlining.** `inline_audit.py` -> `inline_audit.tsv` compares direct callers of every hooked method (Mac BL vs
   Windows call). Fully inlined on Windows:
   - MoveOrder.All and WaypointOrder.ClearWayPoint, inlined into CommandFlow.Move. This is the right-click move, so
     ord captured nothing on the Windows client.
   - ExpeditionPlanView.InputOnAccept, inlined into ExpeditionPlanPresenter.Start/TryStartExpedition (Teams
     "Go to dungeon").
   - TaskFinishEventHandler.TryCraft.

   Fix in `port/rig_main_win.c`: CommandFlow.Move and StartExpedition are replaced by their Mac bodies, which call
   the stand-alone, hooked copies. TryStartExpedition in an mp session goes straight to StartExpedition, so the
   critical-risk confirmation popup is skipped on Windows. **Still open:** TryCraft. Crafted items are not
   owner-tagged on Windows, which affects only item ownership, not the world hash.
   `inline_missing.py` shows the remaining partial differences. They are merged call sites in trace-only hooks or
   non-sim code; none of them changes sim routing.
2. **Folded identical bodies + hooks.** `QuitToTitleSceneRequested..ctor` shares its code with TickRequested..ctor
   and 16 others, so every tick "requested a quit" and the client left the session after the load.
   `UnitObject.SetHUD` (158 sharers) crashed ui_game. Fix: class-filtered hooks (FILT in rig_main_win.c). An asm
   thunk saves the argument registers (rcx rdx r8 r9 xmm0-3), checks the class of `this` (Il2CppClass.name),
   then tail-jumps to the detour or the original. Filtered: QuitToTitle ctor, SetHUD, SetCombatTarget,
   CombatTargetApplyData ctor. gen_port now stops the build when a hook sits on a shared body that is in neither
   FILT nor its harmless list.
3. One more arm64 argument order: `Image.fillAmount` written as `(float, Obj, void *)`. gen_port now reorders it
   and fails the build on any remaining float-before-Obj prototype.

`ord_probe.py` on Windows: captured 2 (was 0), the same as the Mac.

**`mp2_xplay.py GH --math` (Mac G hosts, Windows W joins, shared musl libm): 18 / 18 checks PASS.**
- Join point.
- Fight: fingerprints equal on all 6207 common ticks, no desync. Orders captured A 16 / W 16, applied 32 / 32.
  Combat: 4 enemies dead, enemy HP 57625 -> 47932 on both.
- Pause and speed votes; rates 59.4 / 198 ticks/s on both.
- Ownership: W's order on A's unit refused on both.
- Floor vote: (1,0) -> (1,1), generator hash b6b6d65bd7a6ac53 on both, 1017 equal ticks after; autosave on the
  host only.
- Return-to-base vote (equal, host-only autosave).
- Forced desync -> resync, equal again.

Log: `instances/mp2_xplay_math.out`. Wine/Rosetta caveat (section 7) still applies.

## 10. TryCraft, dlsym exports, cross-play test set (2026-10-13, 03:55-04:06)
- **TryCraft** (inlined into TaskFinishEventHandler.CraftingTaskFinished on Windows). gen_port appends a
  Windows-only wrapper to the generated ord_game.c (`h_win_craft_fin`, hooked at 0xAD0970). It does the same
  owner tagging as ord's h_try_craft one level up: CraftingTaskFinished passes its taskData as TryCraft's
  `crafting`, and nothing else in it creates items. The build fails if ord's h_try_craft text changes.
  Not tested by a crafting run.
- **dlsym exports.** On Windows every `dlsym(RTLD_DEFAULT, "...")` in the sources returned NULL, because our
  symbols were not exported. Affected: ui_teams_exec, ui_map_propose, mp_game_left_choice, rig_det_in_sim/
  sim_scope, the ord_item session lookup and more. gen_def.py now exports all 18 names found in build/gen.
- ABI rewrite now parses nested arguments (one more ui_game Vector2 call caught).
- `xplay_run.py <pair> <script> [--math] [args]` runs any rig/ui test with the pair's 2nd copy = Windows W.
  rig.lease returns the given pair.
- Results (pair EF, shared math):

  | Test | Result |
  |---|---|
  | mp2_test | 18/18 PASS |
  | left_prompt_pair_mpglue (Start path; guest quit -> popup, Keep playing, rejoin, app quit, Wait, rejoin) | 16/16 PASS, fingerprints equal 1084 ticks after the rejoin |
  | ui_test/teams_stockgo_pair (Go marks Ready, both parties land, owners, max 3) | 7/7 PASS, 2315 equal ticks |

  teams_stockgo calls the ui_teams core directly, so the stock StartExpedition presenter path fixed in
  section 9 is still untested.
- **Item-owner hash:** dsnet exposes no hash of its owner tables, and changing it belongs to another lane.
  Proposal ready to apply: `win/proposals/dsnet_owner_hash.md` (order-independent hash + mp status fields,
  compared while paused).

## 11. Crafting, stock Go-to-dungeon, critical-risks confirmation (2026-10-13, 04:08-04:30)
- **Critical-risks confirmation restored.** h_exp_try now always runs the stock TryStartExpedition. Its
  confirmation is a `CommonNotice_ShowYesNo` UIInput (34) whose Yes = ParamCallback1 (+0x78), a delegate to
  StartExpedition, which on Windows is hooked and calls the hooked InputOnAccept. The inlined no-risk start
  (UIInput 102, then 95) is swallowed in `UIInputStream.AddInput` (0xA67CF0, hooked) while TryStartExpedition
  runs, and the hooked InputOnAccept is called instead. Single player stays fully stock.
- **Windows-only test commands** (`port/win_test_cmds.c`, dispatched by a gen_port patch at the top of rig_exec):
  `win_exp {"do":"state"|"next"|"confirm"}`. next = the panel's Next/Go handler OnClickNext; confirm = press Yes
  on the risk notice.
- `craft_xplay.py <pair>`: **Windows HOSTS** in own-storage gear mode (its simulation tags crafted items; that is
  the inlined TryCraft path). The Mac guest selects the Carpenter station (`ord_select group 1001` =
  SelectGroup.Building) and queues RECIPE_WoodPlank (MoonlightBrigade save). Result: order applied on both;
  item 00000102-…-53494d tagged player 1 by the Windows host simulation; `ord_item` owner 1 on both;
  2363 per-tick fingerprints equal. All 5 PASS.
- `expo_xplay.py <pair>`: Mac hosts the base save, Windows guest claims 2 units, opens the stock panel
  (ShowPopUpRequested 2) and presses Next twice; the second press runs the stock TryStartExpedition. Result: stock risk
  confirmation shown on Windows -> Yes -> "teams: Go to dungeon pressed ... Ready with 1 unit" (the ui_teams
  path), no map change, no desync. 3/3 PASS. This save always has critical risks, so the no-risk inlined path
  (UIInput 102 swallow) is built but not hit by a test yet.
- Regression with the final DLL: mp2_test cross-play 18/18 PASS.

## 12. No-risk expedition start; Windows real-PC test kit (2026-10-13, 04:30-04:40)
- **No-risk start covered.** Test switch `win_exp {"norisk":1}` hooks ExpeditionPlanRiskData.get_IsCritical
  (0xBF28D0) to false (test only), so the stock TryStartExpedition takes its no-risk branch.
  `expo_xplay.py CD --norisk`: TryStartExpedition sent UIInput 102 + 95 (the MSVC-inlined InputOnAccept) ->
  both swallowed -> hooked InputOnAccept -> "teams: Go to dungeon pressed ... Ready with 1 unit"; 98 (the panel's
  own close) passes. No map change, no desync. PASS.
- **Test kit** `win/kit/DSMP_WindowsTestKit_25846389.zip` (13 MB): our DLL (`dsmp_baselib.dll`),
  `det_ranges_win.tsv`, `dsmp_kit/{record.txt, orders.txt, save.json}`, install.bat, uninstall.bat, README.txt
  (STE). No game files.
  - install.bat renames the player's baselib.dll to baselib_orig.dll and puts ours in its place. uninstall.bat
    reverses that (Steam "Verify integrity" also restores the file).
  - **Recorder mode** (`port/win_kit.c`): the DLL turns it on when `<game>\dsmp_kit\record.txt` exists and
    DSRIG_DIR is not set. Steam stays on; for the CrossOver test `DSMP_KIT_NOSTEAM=1` keeps the gate. Saves go
    to dsmp_kit\data. It loads the save (det, seed), schedules the orders, runs 6000 locked ticks with shared
    math, writes dsmp_kit\fp.log and RESULT.txt, then quits. A different game build makes the DLL inactive, and
    the game then runs normally through the forwarders.
  - Mac side: `kit/kit_record.py make <copy>` writes the payload and `kit/mac_ref_fp.log`.
    `kit/kit_record.py compare <fp.log> [<copy>]` prints IDENTICAL, or the first different tick and part.
  - Dry run in the DSMP bottle (no DSRIG_DIR, kit folder only): DONE, 6000 ticks, **IDENTICAL on every tick**
    with the Mac reference.

## 13. Windows build brought up to the 18:25 src (mpmenu v2, ui_game v2, ui_teams proposal, DSNET_RULES/LEND) (2026-10-13, 18:20-18:55)
- Rebuilt with today's src (`build_win.sh`, win_map re-run). All RVAs mapped; no new hook on a shared body. Old DLL kept:
  `old_versions/baselib-2026-10-13-0434.dll`. Installed in DSMP `drive_c/DSW/baselib.dll`.
- **Build fix (dsnet_client.c, new timed connect):** a non-blocking connect polled with getsockopt(SO_ERROR). New shims in
  `port/dswin.{h,c}`: `dsw_connect` (WSAEWOULDBLOCK -> EINPROGRESS) and `dsw_getsockopt` (SO_ERROR WSA code -> errno:
  ECONNREFUSED / ETIMEDOUT), so "refused" / "timeout" give the right DSNET_CONN_* result on Windows.
- **ABI fix:** ui_teams.c stock_setup calls ExpeditionPlanPresenter.OnUnitSelected / OnUnitUnselected with a Guid written as
  two u64 (arm64 x1/x2). Windows x64 passes a 16-byte struct by pointer, so this would crash. gen_port now rewrites
  `(Obj, uint64_t, uint64_t, void *)` calls to `(Obj, Guid, void *)` and stops the build on any other split-Guid prototype.
  The other 11 new struct-by-value sites in abi_audit.tsv use struct types (V2, V3, R4, RectF4) and are correct; the
  SaveFileButton.SetData hook takes the header by pointer (correct on both).
- Exports: 20 dlsym names now (18 before; gen_def.py finds them by itself).
- inline_audit: 102 hooked methods. Inlined on Windows = the same 4 as before (all handled, sections 9-10). The 3 new hooks
  (ExpeditionPlanPresenter.OnActivateView / OnClickNext / OnDeactivateView) have 0 direct callers on both (virtual).
- Tests, pair AB (lease windows2), Mac host + Windows guest, shared math, all hidden. Logs `instances/x2_*.out`:

  | Test | Result |
  |---|---|
  | mp2_test (xplay) | 19/19 PASS, 6280 equal ticks in the fight, floor + return votes, resync |
  | disconnect with Start (`left_prompt_xplay.py`) | 16/16 PASS |
  | Teams proposal (teams_proposal_pair: cap, decline, cancel, timeout, approve) | all PASS, 3231 equal ticks in the dungeon |
  | Teams expedition (teams_expedition_pair) | 8/8 PASS |
  | Lending (`lend_xplay.py`: lend, borrower order, owner order dropped, take back, give back from W) | 9/9 PASS, 2585 equal ticks |
  | Crafting (craft_xplay, Windows host) | 5/5 PASS |

- Test notes: `rig/left_prompt_pair_mpglue.py` (04:04) has stale button numbers, and `src/ui_test/left_prompt_pair.py`
  waits for "both running" before Start; the v2 lobby holds the world (DSNET_HOLD_LOBBY) until Start, so both time out.
  By the src notes this is the test order, not a Windows fault (Mac-Mac run of the same script not done here). `left_prompt_xplay.py` = the src copy with Start first.
  `xplay_wrap.py` runs a src/ui_test test cross-play with its frame folder moved to `instances/xplay_shots`.
- Wine/Rosetta caveat (section 7) still applies. Kit zip (section 12) still holds the 04:34 DLL; not rebuilt.

## 14. Release build v7 (gate 20261013-1902, 2026-10-13 19:10-19:20)
- Source: frozen `rig/release/v7_src/` (Mac dylib sha256 35d104c04b619e1b...). `build_win.sh` and gen_port now take
  `DSMP_SRC=<folder>` (default src/). Build: `DSMP_SRC=~/ds_multiplayer/rig/release/v7_src zsh build_win.sh`.
- Release baselib.dll sha256 **2a21ca30e8b8407f047e3818ab65284f9391430b87c902638ed5ebc0184dc968**; installed in DSMP.
  Previous DLL: `old_versions/baselib-2026-10-13-1827.dll`.
- mp2 xplay (pair AB, shared math, Mac side = the 35d104c0 dylib, checked before and after): 19/19 PASS.
  Log `instances/x2_release_mp2.out`.
- Kit: `kit_record.py make A` -> Mac reference equal byte for byte to the 04:36 one (sim unchanged). Dry run in DSMP
  (no DSRIG_DIR, DSMP_KIT_NOSTEAM=1): DONE, 6000 ticks, **IDENTICAL on every tick**. New
  `kit/DSMP_WindowsTestKit_25846389.zip` (2.2 MB) with the release DLL. Old kit: `old_versions/kit-2026-10-13-0436/`.

## 15. Release build v8 (gate 20261013-2303, 2026-10-13 23:10-23:50)
- Source: frozen `rig/release/v8_src/` (Mac dylib sha256 228a6c263ece01f5...). Release baselib.dll sha256
  **8103a9aeaf1d0eba6ab4b4d8cdb48c29ebd0a417adaf93467c1dcc53ce5d4704**. v7 DLL: `old_versions/baselib-2026-10-13-v7.dll`.
- Build fixes:
  - `steam_stub_win.c`: `dsnet_steam_lobby_owner` (new in mpmenu.c) -> 0.
  - Population hooks `ClanDataContainer.get_CurrentPopulation / get_MaxPopulation` sit on folded field-getter bodies
    (65 / 29 sharers, some of them value-type methods whose `this` is raw struct data). FILT entries 4-5 with a namespace:
    the class pointer (Refactor.ClanDataContainer) is resolved at the first call, then only `klass == pointer` is compared
    (never dereferences an unknown `this`). Older FILT entries still compare by class name.
- **inline_audit.py missed every hook in a hex-address table** (`{ 0xABC, hk, (void **)&o, "who" }`): its regex took
  only VA_ names. Fixed (134 hooked methods now, was 102/104). Real inlined hooks found, 4 of them already in v7:
  | Inlined on Windows (into) | Effect without a fix | Windows wrapper (appended to the generated module by gen_port WIN_WRAP) |
  |---|---|---|
  | PopUpUI_Base.Set/UnsetTimeLock (sealed OnActivate / OnDeactivate) | level-up, trade, Esc menu, recruitment on Windows would pause | OnActivate / OnDeactivate run with `_timeLock` (+0x39) cleared when no_timelock() |
  | EventNodeUIView.OnClickChoice (choice button lambda, closure view +0x10, idx +0x18) | a Windows click applied the choice locally: story desync | lambda -> hk_ev_click |
  | SubUI_UnitShortCutParts.RefreshUnitName (PlayerUnitShortCutView.RefreshUnitShortCutPartsName) | owner tag lost on portrait name refresh | after it, sc_tag on the tracked parts of that unit |
  | KeyInputDispatcher.IsTextInputFocused (UpdateKeyStream; v8 chat) | hotkeys fire while typing chat on Windows | while chat_typing: UpdateKeyStream's own focused branch (lists reset) |
  The build stops if the Mac hook text these wrappers mirror changes. Partial inlining (some call sites only) unchanged
  from earlier builds: GetClosestSingleEnemy, Convert, IsPointerOverUI, NewGuid, Random.get_value, Input/clipboard.
- Test-only: `win_exp` OnActivateView hook fails (MinHook 3) since ui_teams hooks the same method (v7 too); harmless.
  New test command `win_evlambda {idx}` calls the hooked choice lambda.
- Tests, pair CD, Mac side pinned to `rig/release/dsrig-228a6c263ece01f5.dylib` (build/dsrig.dylib changed mid-session
  from another lane; `xplay_wrap.py --pin-dylib`), shared math:
  - mp2 xplay: 19/19 PASS (`instances/x2_v8_mp2.out`).
  - `v8_xplay.py`: chat both ways, recruitment on W pauses nobody, population Split (W HUD 1/3 -> hires to 3/3 ->
    refused "Population full for you (3/3)"), story event answered by W through the stock lambda, 4246 equal ticks:
    8/8 PASS. Not runtime-tested: the UpdateKeyStream wrapper (no keyboard in batchmode) and the portrait-name wrapper.
- Kit: Mac reference (pinned dylib) byte-identical to v7's; DSMP dry run IDENTICAL on all 6000 ticks; new zip in
  `kit/` (2.4 MB, DLL sha verified inside). v7 kit: `old_versions/kit-2026-10-13-v7/`.

## 16. Wrapper tests, full hook re-audit, hand-test bottle (2026-10-13 23:50 - 10-14 00:15)
- Built from current src/ (not frozen). New for it: mpmenu.c includes objc / CoreGraphics for its Mac-only AppKit test
  input (mpm_key). Stand-in headers `port/posix/objc/{runtime,message}.h`, `port/posix/CoreGraphics/CoreGraphics.h`:
  objc_getClass returns NULL, so those test helpers do nothing on Windows. `tmp_keys` (Unity Event.KeyboardEvent into
  the chat field) is plain Unity and works on Windows.
- UpdateKeyStream wrapper now calls the Mac hook `h_textfocus(NULL)` itself (src widened the condition: lobby settings,
  rules); guard text = `static bool h_textfocus(void *mi) {` ... `o_textfocus(mi); }`.
- **rig_hook table was capped at 128** (hooks[128]); with 134 hooks the dedupe for shared addresses stopped after 128
  (v8: win_exp OnActivateView failed with MinHook 3). Now 512. Every successful hook logs `<who>: hooked 0x...`.
- Wrapper tests `wrap_xplay.py` (pair AB, Mac dylib pinned copy of build/dsrig.dylib 23:54, shared math): 7/7 PASS.
  Name refresh on W runs the wrapper and re-tags (sc_calls 1, sc_tagged 1). Chat keys: not typing -> stock path;
  typing -> key stream blocked every frame (62 frames / 2 s); Unity key events reach the W chat field
  ("hi from windows"); after sending -> not blocked; line reaches the host; 1288 equal ticks. Real OS key presses
  (WM_KEYDOWN) cannot be sent to a hidden copy (no window): the owner's hand test covers them. Counters:
  `win_wrapstat`, `win_scname`.
- **Full re-audit**: hook list = union of the source tables (inline_audit.py) and the hooks the DLL really installed
  (`inline_audit.py --log <rig.log>`): 148 methods. Plus `icall_inline.py`: icall wrappers (rip slot load) whose slot is
  also read elsewhere = wrapper inlined there. Random.get_value / Range / RandomRangeInt: slot read only in their own
  body (not inlined). Results: 9 fully inlined, all FIXED; 11 partial, none on a sim path; 3 icall copies
  (mouse x2 = test hooks; Steam GetNextCallback = Steam invites, needs a wrapper when Steam is ported to Windows).
- **Hand test bottle "DS Windows Test"** (APFS clone of DSMP, graphics + sound back to default, dsmp_kit moved to
  instances/play_bottle_moved). `C:\DSW\Play_Multiplayer.bat` sets DSRIG_DIR=C:\DSMP_Play, DSRIG_LIVE=1, DSRIG_FPS=60
  and starts the game (args passed on). Checked hidden in DSMP with `-batchmode -nographics`: live mode, 134 hooks,
  title reached, data in C:\DSMP_Play\data. Not run with a window (rule: nothing on the owner's screen).
  Shared math stays OFF there: the owner's Mac LAN app logs no dsmath line (math off), both sides must match.
  Guide: `win/WINDOWS_CROSSOVER_TEST.txt`.

Full hook table (Mac / Windows = direct call sites in each binary; s = section):

| Hooked method | Windows RVA | Mac / Windows direct callers | Status |
|---|---|---|---|
| AstarPath$$StartPath | 0xcf24a0 | 9 / 9 | OK |
| Edgar.GraphBasedGenerator.Grid2D.GraphBasedGeneratorGrid2D.<>c__DisplayClass16_0<object>$$<GetEarlyStoppingHandler>b__0 | 0x14daed0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Edgar.GraphBasedGenerator.Grid2D.GraphBasedGeneratorGrid2D<object>$$.ctor | 0x15b5e50 | 2 / 2 | OK |
| GridTargetSelector$$SpecificGrid | 0x53e450 | 2 / 2 | OK |
| GridTargetSelector$$ToGrid | 0x53e570 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.CampaignSaveHelper$$RequestAutoSave | 0xcc87d0 | 3 / 3 | OK |
| Refactor.CampaignSaveHelper.<ProcessPendingAutoSave>d__50$$MoveNext | 0xce4560 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.CampaignSaveHelper.<StartAutoSave>d__53$$MoveNext | 0xce4e30 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.CampaignSaveLoader$$Delete | 0x589740 | 3 / 3 | OK |
| Refactor.CampaignSaveLoader$$KeepOnlyRetainedCampaignSave | 0x589d20 | 1 / 1 | OK |
| Refactor.CampaignSaveLoader$$Save | 0x58b9f0 | 2 / 2 | OK |
| Refactor.ClanDataContainer$$get_CurrentPopulation | 0x5773f0 | 0 / 2 | OK |
| Refactor.ClanDataContainer$$get_MaxPopulation | 0x50e3a0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Combat.CombatUtility$$GetClosestSingleEnemy | 0x6120d0 | 10 / 8 | partial, trace-only det hook: OK |
| Refactor.Combat.ExecutionContext$$Convert | 0x619f90 | 56 / 55 | partial, trace-only det hook: OK |
| Refactor.Combat.ExecutionContext$$SetTarget | 0x61a320 | 2 / 2 | OK |
| Refactor.Component.CombatInfoComponent$$SetCombatTarget | 0x5c3ee0 | 0 / 4 | OK |
| Refactor.Component.SkillComponent$$ChangeAutoSkillData | 0xae4260 | 2 / 2 | OK |
| Refactor.Control.ActionOrder$$Execute | 0x9190b0 | 1 / 1 | OK |
| Refactor.Control.CancelMode$$DragSelection | 0x91b910 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.CancelMode$$LeftClickInteraction | 0x91c0d0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.ConstructionMode$$DragSelection | 0x9202e0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.ConstructionMode$$LeftClickInteraction | 0x923400 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.DemolitionMode$$DragSelection | 0x92a8b0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.DemolitionMode$$LeftClickInteraction | 0x92b110 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.GatheringMode$$DragSelection | 0x9309a0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.GatheringMode$$LeftClickInteraction | 0x930d70 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.InteractionRunner$$Run | 0x934f50 | 2 / 2 | OK |
| Refactor.Control.MoveOrder$$All | 0x935980 | 2 / 0 | FIXED: CommandFlow.Move replaced (s9) |
| Refactor.Control.MoveOrder$$Near | 0x935b60 | 1 / 1 | OK |
| Refactor.Control.MoveOrder$$TryMove | 0x935e40 | 2 / 4 | OK |
| Refactor.Control.PickUpMode$$DragSelection | 0x936680 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.PickUpMode$$LeftClickInteraction | 0x9369a0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Control.PortalOrder$$Open | 0x936c10 | 1 / 1 | OK |
| Refactor.Control.UseSkillMode$$UseSkill | 0x939230 | 4 / 4 | OK |
| Refactor.Control.WaypointOrder$$ClearWayPoint | 0x939b30 | 2 / 0 | FIXED: CommandFlow.Move replaced (s9) |
| Refactor.Control.WaypointOrder$$Reserve | 0x939d90 | 2 / 4 | OK |
| Refactor.EntityTargetSelector$$ClosestHostile | 0x5b7a00 | 47 / 49 | OK |
| Refactor.EntityTargetSelector$$SelectFromSpecificTargets | 0x5b85c0 | 1 / 1 | OK |
| Refactor.EntityTargetSelector$$Self | 0x5b8850 | 20 / 21 | OK |
| Refactor.EntityTargetSelector$$SpecificTarget | 0x5b8c60 | 14 / 14 | OK |
| Refactor.EntityTargetSelector$$SpecificUsableTarget | 0x5b8d80 | 7 / 7 | OK |
| Refactor.EntityUtility$$GetRayCastPlayerEntities | 0xcb94e0 | 2 / 2 | OK |
| Refactor.InMapDataInterface$$GetCenterOfWorldOccupies | 0xccd860 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.InMapDataInterface$$GetWorldPosition | 0xcd0160 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.EntityDataContainer$$MoveThing | 0x9a23d0 | 1 / 1 | OK |
| Refactor.Main.EntityDataContainer$$OnSpawnThing | 0x9a38c0 | 2 / 2 | OK |
| Refactor.Main.Event.CampaignEventHandler.<OnEvent>d__30$$MoveNext | 0xa836e0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.CampaignEventHandler.<OnEvent>d__31$$MoveNext | 0xa83c30 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.CampaignEventHandler.<OnEvent>d__51$$MoveNext | 0xa84f30 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.CampaignEventHandler.<OnEvent>d__52$$MoveNext | 0xa84fe0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.CampaignEventHandler.<OnEvent>d__53$$MoveNext | 0xa85090 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.CampaignLoadingEventHandler$$OnEvent | 0xa71830 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.CampaignLoadingEventHandler.<OnEvent>d__100$$MoveNext | 0xa9c590 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.DirectOrderRequested$$.ctor | 0x56af40 | 10 / 169 | OK |
| Refactor.Main.Event.EntityEventHandler$$OnEvent | 0xa8d340 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.InputEventHandler$$OnEvent | 0xaade00 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.InteractionEventHandler$$OnEvent | 0xab3cb0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.ItemEventHandler$$OnEvent | 0xab8fd0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.MapTransitionCoordinator.<Run>d__1$$MoveNext | 0xad7af0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.MapTransitionCoordinator.<RunGuarded>d__3$$MoveNext | 0xad6880 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.PlayerControlEventHandler$$OnEvent | 0xac89f0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Event.QuitToTitleSceneRequested$$.ctor | 0x8e1990 | 2 / 17 | OK |
| Refactor.Main.Event.TaskFinishEventHandler$$TryCraft | 0xad2d50 | 1 / 0 | FIXED: CraftingTaskFinished wrapper (s10) |
| Refactor.Main.EventProcessor$$Update | 0xa03820 | 1 / 1 | OK |
| Refactor.Main.EventQueue$$Update | 0x9e03b0 | 1 / 1 | OK |
| Refactor.Main.EventQueue.<>c__DisplayClass8_0$$<.ctor>b__2 | 0x9f05b0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.EventQueue.<>c__DisplayClass8_1$$<.ctor>b__3 | 0x9f0670 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.EventQueue.<ProcessAsyncEvent>d__14$$MoveNext | 0x9ef8e0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.GameSpeedSetting$$AddPauseReason | 0xa35ac0 | 3 / 3 | OK |
| Refactor.Main.GameSpeedSetting$$RemovePauseReason | 0xa35e20 | 3 / 3 | OK |
| Refactor.Main.GameSpeedSetting$$SetSpeed | 0xa35fb0 | 11 / 13 | OK |
| Refactor.Main.InputModule.InputDispatcher$$<Update>b__8_0 | 0xa4c8b0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.InputModule.KeyInputDispatcher$$IsTextInputFocused | 0xa62590 | 1 / 0 | FIXED: UpdateKeyStream wrapper (s16, tested) |
| Refactor.Main.InputModule.UIInputStream$$AddInput | 0xa67cf0 | 285 / 277 | partial (UI callers); Windows-only expedition redirect, only needed inside TryStartExpedition (tested): OK |
| Refactor.Main.LevelUpHelper$$SetLevelUpTargetUnit | 0x9cbb50 | 2 / 2 | OK |
| Refactor.Main.MainFlow$$Update | 0xa3b1d0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Main.Processor.SelectProcessor$$Select | 0xa82740 | 7 / 9 | OK |
| Refactor.Main.Processor.SelectProcessor$$Toggle | 0xa82840 | 10 / 11 | OK |
| Refactor.Main.TimeHandler$$Update | 0xa454a0 | 1 / 1 | OK |
| Refactor.Main.TradeHelper$$TryCommit | 0x9cfa40 | 1 / 1 | OK |
| Refactor.Map.DungeonGenerator.DSDungeonPreDataGenerator$$GetRandomNumbersGenerator | 0x97ae10 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.Map.DungeonGenerator.DSDungeonPreDataGenerator$$SetGenerationResult | 0x97bb20 | 5 / 5 | OK |
| Refactor.ObserverDriven.ObserverAggregator$$Record | 0x8eda90 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.ObserverDriven.ObserverDriven$$Update | 0x8eea50 | 1 / 1 | OK |
| Refactor.Runtime.Combat.HostileEntityData$$AddHostile | 0x9311a0 | 6 / 6 | OK |
| Refactor.Runtime.Combat.HostileEntityData$$GetClosestCombatableHostiles | 0x931e40 | 2 / 2 | OK |
| Refactor.Runtime.Combat.HostileEntityData$$RemoveHostile | 0x9325d0 | 3 / 3 | OK |
| Refactor.Task.DecidePlayerCombatBehaviour$$DecideSkill | 0x851600 | 1 / 1 | OK |
| Refactor.Task.DecidePlayerCombatBehaviour$$DecideTarget | 0x851a60 | 1 / 1 | OK |
| Refactor.Task.DecidePlayerCombatBehaviour$$FindNewTarget | 0x852040 | 1 / 1 | OK |
| Refactor.Task.DecidePlayerCombatBehaviour$$GetSpecificTarget | 0x852160 | 2 / 2 | OK |
| Refactor.Task.TaskAssignableHelper$$IsAvailable | 0x826470 | 1 / 2 | OK |
| Refactor.Tick.TickFlow$$Update | 0x80b3a0 | 2 / 2 | OK |
| Refactor.UI.AutoSaveHandlerView$$CallAutoSave | 0xb7ad30 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.AutoSavePresenter$$CallAutoSave | 0xb7ae30 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.CampaignInfoPresenter$$SetPlayerCount | 0xb7c070 | 2 / 2 | OK |
| Refactor.UI.CommonNoticeView$$OnClick_Cancle | 0xc70a00 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.CommonNoticeView$$OnClick_Confirm | 0xc70a50 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.DungeonResultView$$OnClick_GoDown | 0xba24e0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.DungeonResultView$$OnClick_Return | 0xba25c0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.EventNodeUIView$$OnClickChoice | 0xbdfea0 | 1 / 0 | FIXED: choice lambda wrapper (s15, tested) |
| Refactor.UI.EventNodeUIView$$SetChoices | 0xbe01b0 | 2 / 2 | OK |
| Refactor.UI.EventNodeUIView$$SetEventUI | 0xbe04e0 | 2 / 2 | OK |
| Refactor.UI.ExpeditionPlanPresenter$$OnActivateView | 0xbe6010 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.ExpeditionPlanPresenter$$OnClickBack | 0xbe6720 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.ExpeditionPlanPresenter$$OnClickNext | 0xbe69c0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.ExpeditionPlanPresenter$$OnDeactivateView | 0xbe6a60 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.ExpeditionPlanView$$InputOnAccept | 0xbebc90 | 2 / 0 | FIXED: StartExpedition / AddInput redirect (s9, s11) |
| Refactor.UI.ForcedBaseReturnView$$OnClick_ReturnBase | 0xbf5e70 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.PauseUIPresenter$$OnSpeedChanged | 0xc19a10 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.PopUpUI_Base$$SetTimeLock | 0xc535b0 | 1 / 0 | FIXED: OnActivate wrapper (s15) |
| Refactor.UI.PopUpUI_Base$$UnsetTimeLock | 0xc537a0 | 1 / 0 | FIXED: OnDeactivate wrapper (s15) |
| Refactor.UI.PopUpViewContainer$$ActivateView | 0xc80f50 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.RecruitView$$OnRecruitCandidate | 0xc2f9c0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.SubUI_CampaignClockUI$$Initialize | 0xb8d640 | 2 / 2 | OK |
| Refactor.UI.SubUI_SaveFileButton$$SetData | 0xc84420 | 2 / 2 | OK |
| Refactor.UI.SubUI_SelectRegionUI$$InitOnActivate | 0xc06fc0 | 2 / 2 | OK |
| Refactor.UI.SubUI_SelectRegionUI$$SetUnit | 0xc082a0 | 5 / 5 | OK |
| Refactor.UI.SubUI_SystemMenuButton$$OnPressButton | 0xc36a40 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.SubUI_TitleMenuButton$$OnPointerClick | 0xb6f690 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.SubUI_UnitShortCutParts$$Initialize | 0xc29c10 | 1 / 2 | OK |
| Refactor.UI.SubUI_UnitShortCutParts$$OnPointerClick | 0xc2a4a0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.UI.SubUI_UnitShortCutParts$$RefreshUnitName | 0xc2a500 | 1 / 0 | FIXED: RefreshUnitShortCutPartsName wrapper (s16, tested) |
| Refactor.UI.SystemMenuView$$OnLoadCampaignPressed | 0xc37bb0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.View.UnitEntityHUD$$Init | 0xb23e90 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| Refactor.View.UnitObject$$SetHUD | 0x5c3fe0 | 1 / 2 | OK |
| Steamworks.NativeMethods$$SteamAPI_ManualDispatch_GetNextCallback | 0xe5f7a0 | 0 / 0 | icall inlined into CallbackDispatcher.RunFrame: Steam invites only; Windows has no Steam bridge (stub). Needs a wrapper when Steam is ported |
| System.Guid$$NewGuid | 0x2ad1340 | 115 / 110 | partial, missing callers are Json schema / Unity Services / UI Toolkit / FX: no sim: OK |
| System.HashCode$$GenerateGlobalSeed | 0x2ad43a0 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| System.Random$$.ctor | 0x2af4cc0 | 32 / 36 | OK |
| System.Random$$Next | 0x2af4a00 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| System.Random$$Next | 0x2af4b40 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| System.Random$$Next | 0x2af4c00 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| System.Random$$Sample | 0x2af4c10 | 0 / 0 | OK (no direct caller on either: virtual / delegate) |
| UnityEngine.Application$$Quit | 0x38e91a0 | 0 / 0 | call sites merged, every caller still calls it: OK |
| UnityEngine.Application$$Quit | 0x38e91e0 | 6 / 5 | call sites merged, every caller still calls it: OK |
| UnityEngine.Application$$get_persistentDataPath | 0x38e9dd0 | 21 / 25 | OK |
| UnityEngine.GUIUtility$$get_systemCopyBuffer | 0x39f54b0 | 10 / 6 | partial (TMP paste), test-only hook: OK |
| UnityEngine.GUIUtility$$set_systemCopyBuffer | 0x39f58b0 | 11 / 4 | partial (TMP copy); hook = copy-address test: OK |
| UnityEngine.Input$$GetMouseButton | 0x3a0e530 | 23 / 25 | icall inlined in SendMouseEvents.UpdateMouse (Unity OnMouse events); test hook: OK |
| UnityEngine.Input$$GetMouseButtonDown | 0x3a0e4b0 | 12 / 11 | partial + icall inlined in SendMouseEvents.UpdateMouse; teams TEST hook only: OK |
| UnityEngine.Input$$GetMouseButtonUp | 0x3a0e4f0 | 12 / 11 | partial; teams TEST hook only: OK |
| UnityEngine.Input$$get_mousePosition | 0x3a0e930 | 62 / 62 | icall inlined in SendMouseEvents.UpdateMouse; our use is a direct call, hook test-only: OK |
| UnityEngine.Random$$RandomRangeInt | 0x3964bb0 | 0 / 130 | OK |
| UnityEngine.Random$$Range | 0x3964bf0 | 69 / 76 | OK |
| UnityEngine.Random$$get_value | 0x3964dc0 | 31 / 30 | call sites merged (GetPointsOnNodes calls via virtual node methods); icall slot read only in its own body: OK |
| Util.UnityUtil$$IsPointerOverUI | 0xb58b40 | 17 / 10 | call sites merged, every caller still calls it: OK |

## 17. Shared math ON by default (lead decision for v9, 2026-10-14 00:00-00:20)
- `src/build.sh`: `-DDSMATH_DEFAULT_ON=1` on math_mac.c (libdsmath_arm64.a was already linked). Windows: dsmath_win_install
  runs unless `DSMP_MATH=0` or `<rig dir>/dsmp_math.off`. Mac: same off switches (they win over the default).
  xplay_run.py without `--math` now writes dsmp_math.off / DSMP_MATH=0 (native-math runs stay possible).
- Cost (`math_cost.py E`, ElvenExpedition fight save, det_test's 16 orders, 2000 locked ticks at 50 ticks/frame,
  3 runs each, alternating): math off median 1.316 ms/tick (1.300 / 1.333 / 1.316), on median 1.316 ms/tick
  (1.316 / 1.367 / 1.283): no measurable cost. End fingerprint equal on / off (dc130f2fa7c7d682).
- mp2 Mac-Mac (EF, default build, no flag on F): 19/19 PASS; F logs "libm pointers of GameAssembly redirected".
- Windows W started without DSMP_MATH: "dsmath ...: 24 CRT math entries redirected". DLL installed in DSMP and in
  "DS Windows Test"; the .bat comment updated.

## 18. "Downed unit: purple life bar in solo, not in MP" (owner bug, PLAN.md 2026-10-14 00:55 item 3) - NOT reproduced
- Game path (dump 25846389, Mac RVAs): sim AffecterApplier.CalculateAdditionalEffect (0xAB4504) builds
  ViewEntityBoolData(UpdateType 0x405, value 0/1) -> view queue -> EntityHUDViewPresenter.SetDeadly(viewData) 0x826E0C ->
  EntityHUDView.SetDeadly(Guid, bool) 0x821FC0 -> UnitEntityHUD.SetDeadly(bool) 0xE174BC: head bar (+0x88)
  ColorPreset 7 Health_Deadly, body bar (+0x98) preset 8 Health_DeadlyBody (false: 5 / 6). UnitEntityHUD.Init also calls it.
  The portrait card (SubUI_UnitShortCutParts.RefreshUnitStatus) has no deadly colour (fill amounts only).
- Our code: ui_game touches the world HUD only for the name text (hud_apply, +0x68) and the see-through alpha
  (world_frame: reads SpriteRenderer.color, writes the same rgb with a new alpha). No HP-bar colour code anywhere.
- Probe (test only, never in a release): `win/probe/deadly_probe.c` linked into a scratch dylib built from
  rig/release/v8_src (`build_probe.sh`). Hooks the 3 SetDeadly steps, SpriteRenderer.set_color (any recolour of a
  deadly bar outside SetDeadly is logged with the caller RVA), UnitEntityHUD.RefreshStatus (bar colour changes), and
  frames the downed unit (Camera.main moved + ortho 2.5, rig `screenshot`). Saves: elven_low*.json (players at 12 %).
- Results (pair EF, hidden windows):
  - solo det: 4 downs, every step called, bars (0.27 0.17 1.00) / (0.35 0.02 0.67) = purple; frame solo_det_deadly_cam.png.
  - MP host + guest (det, claims 3 + 1, attack moves): the same 4 downs on BOTH peers at the same moment, every step
    called, the same purple colours, no recolour afterwards (0 set_color on those bars); frames mp_host_deadly_cam.png,
    mp_guest_deadly_cam.png show the purple bar (Leonis) like solo.
- So with the v8 build the downed bar is purple in MP on host and guest. Open: which bar the owner means (world bar
  above the unit or the portrait card), which build he saw it on, own unit or another player's, his see-through
  setting. Shots in `win/probe/shots/`.

## 19. ui_tip.c on Windows (uxdesign, 2026-10-14 01:17)
- gen_port picks every src/*.c, so ui_tip.c is in the Windows build without a list change. RVAs mapped:
  TooltipUI.ShowTooltip 0xF4A244 -> 0xC5FF10, ShowTooltipAtScreenPosition 0xF4A648 -> 0xC5FCF0, HideToolTip 0xF4AA04 ->
  0xC5EFC0, Tooltip_ETC.Show 0xF3D8B8 -> 0xC45B00 (hook), Tooltip_Base.HideLastDivider 0xF379A8 -> 0xC40150. None is a
  shared body. Tooltip_ETC.Show has 0 direct callers on Mac and Windows (virtual), so not inlined.
- ABI: Guid / V2 / TipCtx (0xC0 bytes, by pointer on both) written as struct types: correct on x64.
- ui_tip's build guard compares the first arm64 instruction of each function: gen_port now writes the first 4 bytes of
  the Windows function into any `want[]` `{ rva, word }` table (else tooltips would be off on Windows).
- gen_def.py exported every dlsym name; ui_tip.c dlsyms `ui_tooltips_enabled`, which no module defines yet, and the
  link failed. Now only names with a definition in build/gen are exported (others return NULL, as on the Mac).
- Runtime tooltip check on Windows: in the v9 cross-play run.
- 2026-10-14 01:40-01:56, wider test (`win/probe/deadly_matrix.py`, probe built from current src and from v8_src by
  `build_probe2.sh`; probe also hooks portrait SetDead / SetGrey / SetBattle and UnitEntityHUD.SetDead):
  | Case | build | World bar above the downed unit | Portrait card |
  |---|---|---|---|
  | solo | current, v8 | purple, 4/4 downs | no colour change at a down (stock has none; SetDead only at death) |
  | MP host / guest, own unit, other player's unit, lent unit (Leo lent to the guest), guest see-through 75 | current, v8 | purple on both peers, alpha 1.00, never recoloured | same as solo (other player's cards dimmed by our overlay, as designed) |
  | MP, guest quit + host Keep playing (guest unit "away") | current | purple on the host for all 4 incl. "Silvarion, Guest, away" | same |
  Side-by-side frames: `win/probe/shots/SIDE_cur_solo_host_guest.png`, `SIDE_v8_solo_host_guest.png`, per case
  `shots/{cur,v8}_{solo,mp}/`, `shots/cur_left/grid_world.png`.

## 20. Release build v9 (gate 20261014-1201, 2026-10-14 11:00-13:25)
- gen_port fixes (found on the dry build from pre-freeze src): (1) RVAs in comments are no longer translated or flagged
  (ui_game.c comment "+0xD85838 here" is mid-function: no twin). Code output checked identical to the old translate
  except ui_game.c. (2) ui_game.c hk_pop_enqueue walks the arm64 frame-pointer chain; MSVC GameAssembly has no rbp
  chain on x64 (garbage reads, possible crash when popup 3 opens): rewritten to RtlCaptureStackBackTrace like rig_det
  CALLERS. (3) its CheckGameOver return-address range ends at the Windows function end from .pdata (chained
  UNW_FLAG_CHAININFO parts followed: 0xA8A9A0-0xA8AC56), not at the Mac next method (RecordDeadUnit, not adjacent on Windows).
- Source: frozen `rig/release/v9_src/`, Mac dylib sha256 2548c67a8d80f46d... Release baselib.dll sha256
  **529991056ee703c8531394de3ff4996550a464ed171f0e39785c35a0e20c161c**; copy `old_versions/baselib-2026-10-14-v9.dll`.
  Installed in DSMP and "DS Windows Test" (+ det_ranges_win.tsv 20879c39...).
- Tests (pair EF, Mac pinned to rig/release/dsrig-2548c67a8d80f46d.dylib, shared math; `instances/v9_suite.sh`):
  mp2 xplay 19/19, v8_xplay 8/8, wrap_xplay 7/7, prefs identical. Logs `instances/x9_rel_*.out`.
- Kit: Mac reference on the frozen dylib byte-identical to v8's; DSMP dry run (no DSRIG_DIR, DSMP_KIT_NOSTEAM=1)
  IDENTICAL on all 6000 ticks; new zip `kit/DSMP_WindowsTestKit_25846389.zip`. v8 kit: `old_versions/kit-2026-10-14-v8/`.

## 21. Release tooling after lan_v10 (gatefix, 2026-10-15 05:00-06:00)
- lan_v10 released from gate round 2 (ALL GREEN 38/38): Mac dylib 3d83e997e24c184c..., baselib.dll 17ac07796b479e33...
  Installed in DSMP (by the gate) and in "DS Windows Test" (old DLL + det_ranges_win.tsv in old_versions/*DSWindowsTest*).
- Round 1 Windows failures (win_mp2, win_partial reload loop, win_floorgen10): the full-state check hashed the save lines
  with the CR of Windows CRLF, so every section differed on a Windows guest. mp_game.c now strips the CR.
- Inline-audit stop: build_win.sh runs inline_audit.py and stops on an inlined hook not in inline_known.tsv (9 known).
- Kit dry run: kit/kit_dryrun.py <mac_copy> --dylib D --ranges R (Mac reference, then the kit alone in DSMP, compare).
  Trap: the Mac copies read build/det_ranges.tsv, which other lanes' builds rewrite; a run with a newer file differed
  from tick 26 (the Windows run equalled the v8 reference byte for byte). The gate now freezes
  rig/release/det_ranges-<sha16>.tsv and exports DSRIG_RANGES (wrig.py drops it for the Windows copy).
- Base-save cross-play: rig/soak_test.py under xplay_run (Mac host + Windows guest, base save, 15 min, full check every
  600 steps): 3/3 PASS, 91 equal checks, 0 desyncs (rig/det_out/wintools_0459_base_soak.out).
- release_gate.sh lane3: + win_base_soak, + win_kit (EXP 40).
- Kit dry run on the released 3d83 dylib (06:51): that dylib predates DSRIG_RANGES, so kit_dryrun.py runs the Mac reference
  on an APFS clone under kit/refroot/instances (rig.py INST from DSRIG_INSTANCES) whose ../../build/det_ranges.tsv is
  the frozen release file (116128 ranges): RESULT IDENTICAL on every tick, 6000 ticks (rig/det_out/wintools_kit3.out).
