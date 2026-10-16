# Building ds_multiplayer

This repo builds the multiplayer mod: a Mac dylib and a Windows DLL (or loader modules).
All builds run on a Mac. The Windows files are cross-built on the Mac.

## 1. Requirements

You need a Mac with Apple silicon (arm64). The Mac mod is arm64 only (`src/build.sh`).
The versions below are the ones used for the current builds.

| Tool | Version used | Used by |
|---|---|---|
| macOS | 27.2 | all |
| Xcode (clang, codesign) | Xcode 27.1, Apple clang 21.0.0 | Mac parts |
| llvm-mingw | clang 23.1.3, target x86_64-w64-windows-gnu | `win/math/build_math.sh` (also for the Mac library), `win/build_win.sh` |
| Python 3 | 3.10.4 | Windows generators, range tables |
| .NET SDK | 10.0.402 | only to make dumps and the MelonLoader guard DLL |
| git, rsync | git 2.54.0, openrsync | Custom Modes copy |

Python packages: `pefile` (2024.8.26), `lief` (1.0.0) and `capstone` (5.0.7) for `win/win_map.py`,
`win/port/gen_port.py`, `win/port/gen_def.py` and `win/inline_audit.py`.

Downloads:
- llvm-mingw: the macOS release of https://github.com/mstorsjo/llvm-mingw.
- Il2CppDumper (https://github.com/Perfare/Il2CppDumper): makes the dumps. `win/NOTES.md` section 1 has the run used.

Fixed tool place (no env var): the scripts expect llvm-mingw at `tools/llvm-mingw` in your home folder
(`win/build_win.sh:12`, `win/math/build_math.sh:10`, `:14`, `:17`). Put it there, use a link, or edit those lines.

## 2. External inputs (not in this repo)

Never commit these. `.gitignore` keeps game files, dumps and builds out of git.

| Input | What it is | How it is found |
|---|---|---|
| ds_custommodes clone | https://github.com/AmfiLogos/ds_custommodes. `src/build.sh` and `win/build_win.sh` build it too. | `CM_REPO` (default `ds_custommodes` in your home folder) |
| `win/25846389/` | Copies of the Windows game files of Steam build 25846389: `baselib.dll`, `GameAssembly.dll`, `global-metadata.dat` (plus `UnityPlayer.dll`, `DungeonSettlers.exe`, `appmanifest_2798330.acf`, `SHA256SUMS`). Read only. | fixed place in this checkout (`DSMP_ROOT` = the checkout) |
| `win/25846389/dump/` | Il2CppDumper output of the Windows `GameAssembly.dll` + `global-metadata.dat` (`script.json`, `dump.cs`, `il2cpp.h`, `stringliteral.json`), with `GenerateDummyDll=false`. | fixed place in this checkout |
| Mac dump | Il2CppDumper output (`script.json`, `dump.cs`) of the arm64 slice of the Mac `GameAssembly.dylib`, build 25846389, with its `global-metadata.dat`. | `win/win_map.py --mac-dump`. `build_win.sh` passes no flag, so the default is used: `ds_skill_truth/dump_25846389` in your home folder. |
| Mac GameAssembly arm64 slice | The arm64 slice of the Mac `GameAssembly.dylib`, build 25846389. | `win/win_map.py --mac-bin`, default `ds_skill_truth/GA_arm64_25846389.dylib` in your home folder |
| `build/det_ranges.tsv` | Mac method ranges for the determinism layer (`src/rig_det.c`). Made from the Mac dump (next section). | the rig reads `build/det_ranges.tsv`; `DSRIG_RANGES` names another file |
| MelonLoader 0.7.3 x64 zip | Reference for the guard plugin `ext/melon/plugin/DSMelonGuard` (`MelonLoader.dll`, `0Harmony.dll`). | MSBuild property `MLDir` (default `ext/melon/dl.noindex/ml073/x/MelonLoader`) |
| Mod loader SDK + mod.json | Only for `DSMP_TARGET=module`: `dsl_sdk.h` and `loader/modules/multiplayer/mod.json` of https://github.com/AmfiLogos/ds_modloader. | `DSL_SDK`, `DSL_MODJSON` (defaults under `ds_launcher` in your home folder) |

Paths fixed in scripts, with no override:
- `ext/core_next/tools/gen_ranges.py:21-23` reads the Mac dump from `ds_skill_truth/dump_25846389` in your home folder,
  and writes to `ds_multiplayer/ext/core_next/build` in your home folder. Edit those lines.
- `win/mac_callers.py:7` and `:9` (Mac dump and arm64 slice), and `win/win_callers.py:6-7`
  (`ds_multiplayer/win/25846389` in your home folder; it ignores `DSMP_ROOT`). `win/inline_audit.py` runs both
  during `win/build_win.sh`. Edit those lines when your files are in another place.
- `win/build_win.sh` runs `win/win_map.py` with no flags (Mac dump defaults above). It rewrites `win/win_map.tsv`, which is in git.

The supported game is Dungeon Settlers, Steam build 25846389 (Unity 6000.0.58f2).

## 3. Build order

Run each command from the repo root.

1. Deterministic math library (Mac arm64, Mac x86_64, Windows x64; run once):
   `zsh win/math/build_math.sh`
   Output: `win/math/build/libdsmath_<arch>.a` and `win/math/build/dsmath_test_<arch>`.
2. Mac mod and Custom Modes:
   `CM_REPO=<your ds_custommodes clone> zsh src/build.sh`
   Output: `build/dsrig.dylib`, `build/custom-modes.dylib`, `build/custom-modes-mp.dylib`. `OUT=<dir>` builds into another folder.
3. Mac range table (after the Mac dump exists):
   `python3 ext/core_next/tools/gen_ranges.py`
   Output: `det_ranges.tsv` and `methods.tsv` in the output folder of that script. Copy `det_ranges.tsv` to `build/`.
   `rig/tools/gen_ranges.py`, named in comments, is not in git; use this copy.
4. Windows mod (after `win/25846389/` and both dumps exist):
   `CM_REPO=<your ds_custommodes clone> zsh win/build_win.sh`
   Output: `win/build/baselib.dll` (proxy of the game's `baselib.dll`, the whole mod in one DLL) and
   `win/build/det_ranges_win.tsv` (also copied to `build/`).
   For the mod loader: `DSMP_TARGET=module DSL_SDK=<sdk folder> DSL_MODJSON=<mod.json> CM_REPO=<clone> zsh win/build_win.sh`.
   Output: `win/build/mods/multiplayer/` and `win/build/mods/custom-modes/`.
   `SKIP_GEN=1` skips `win_map.py` and `port/gen_port.py` (reuses `win/build/gen*`).
5. MelonLoader guard DLL (only for the ds_modloader Windows build): in `ext/melon/plugin/DSMelonGuard`, build the
   Release configuration with the .NET SDK and `MLDir` set to the unpacked MelonLoader 0.7.3 folder:
   `dotnet build -c Release -p:MLDir=<folder>`. Output: `bin/Release/net6.0/DSMelonGuard.dll` (ds_modloader `ML_GUARD_DLL`).

The build stops when it finds a new hook that MSVC inlined on Windows (`win/inline_known.tsv` lists the known ones).

## 4. Tests

- Math, no game: after step 1, run `win/math/build/dsmath_test_arm64` (and `dsmath_test_x86_64`;
  `dsmath_test_win64.exe` runs on Windows).
- Game tests use the rig: `rig/make_instance.sh A` makes an isolated game copy from the Steam install, then
  `zsh rig/gate.sh` builds and runs the smoke and mp2 tests on a free pair. `rig/rig.py` expects this checkout at
  `ds_multiplayer` in your home folder (`rig/rig.py:24`).
