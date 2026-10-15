# BepInEx 6 IL2CPP on Apple chips (lane bepinexmac, 2026-10-15)

Goal: run the Nexus BepInEx 6 IL2CPP mods for Dungeon Settlers natively on arm64 macOS.

## Result

On a rig copy (copy C, build DS_B.0.4.29, Unity 6000.0.58f2, metadata v31.1), running native arm64:
- BepInEx 6 (be.788 base + fixes) starts through Doorstop + CoreCLR (.NET 6.0.36 osx-arm64).
- It generates the interop assemblies on the Mac (Cpp2IL reads the universal Mach-O; no LibCpp2IL error here).
- Il2CppInterop class injection works (`Registered mono type ... in il2cpp domain`).
- Test plugin `plugin/HelloArm64`: logs, two HarmonyX patches on Assembly-CSharp methods fire
  (`Reporter._OnLevelWasLoaded`, `TitleScenePreviewController.GetPercentOfDay`), and an injected
  MonoBehaviour's `Update` runs.
- Real Nexus mod **Extended Hotbar 1.0.2 (BepInEx build)**: all 48 Harmony patches install. With our dsrig.dylib
  in the same process, a campaign save loads, units move, and the mod logs
  `Native hotbar expanded: 5 -> 11`, `Custom hotbar layout active: 10 skills + basic attack`.
  It needed a TEST-ONLY shim (not in git, `test.noindex/HotbarMacShim`): the mod refuses to start unless
  `GameAssembly.dll` and `global-metadata.dat` hash to the **Windows** files of DS_B.0.4.29. On the Mac the files
  differ (and `Application.dataPath` points to `Contents`, not `Contents/Resources/Data`). The shim answers the
  mod's `FileHash()` with the Windows values only when the Mac metadata hash is the known DS_B.0.4.29 Mac one.
  For release: ask the mod author to accept the Mac hashes (or drop the check), do not ship a bypass.

## What failed and what fixed it (in start order)

| # | Failure | Fix | Where |
|---|---|---|---|
| 1 | No arm64 Doorstop with `doorstop_jit_memcpy` in be.788 (Doorstop 4.5.0) | Build UnityDoorstop master (has #114 jit_memcpy, #117 chained fixups) for arm64 with clang | `tools/build_install.sh` |
| 2 | be.788 ships x86_64 `libdobby.dylib` | Build cdobbyn/Dobby `macos-arm64-dobby-8b41` (BepInEx/Dobby#16, closed unmerged) | same |
| 3 | be.788 ships an x86_64 .NET runtime | Copy .NET 6.0.36 osx-arm64 runtime + 7 Microsoft.Extensions DLLs from the zip | same |
| 4 | Game data / GameAssembly not found in `.app` (#1407) | Merged cdobbyn `macos-il2cpp-paths-6e2d` | BepInEx fork |
| 5 | MonoMod detour writes refused (RWX, #1408) | Merged cdobbyn `macos-arm64-detours-3f7b` (#1405 + #1409) | BepInEx fork |
| 6 | x86 export-thunk decoding on arm64 (#1403) | Merged baisiqi6 #1412 | BepInEx fork |
| 7 | "Unity 0.0.0": the copy has a `data` folder next to the `.app`; case-insensitive FS made it the data folder | macOS: use `Contents/Resources/Data` first | ours, `BepInEx.Core/Paths.cs` |
| 8 | SIGILL: 16-byte Dobby hook on `il2cpp_runtime_invoke`, a 4-byte `b` export, overwrote 3 neighbouring exports | arm64 `FollowExportThunks`: follow `b imm26` to the body before detouring | ours, `Hook/INativeDetour.cs` |
| 9 | `InjectorHelpers` cctor: no GameAssembly in `Process.Modules` on macOS | Merged sptndc #273 (dladdr base + Mach-O size), conflict with #267 resolved (VirtualQuery path kept for Windows) | Il2CppInterop fork |
| 10 | Injector hook lookups decode arm64 code as x86 | Merged #288 (arch guards), #289, #290; added `Arm64Code`: AArch64 `JumpTargets` using LC_FUNCTION_STARTS (b = tail call only when it lands on a function start), tail-call list, "BL with x1 = stack address" | Il2CppInterop fork |
| 11 | Unity 6 GenericMethod hook: clang arm64 inlines the 3-argument shim | arm64 uses the 1-argument hook on `GenericMethod::GetMethod(const Il2CppGenericMethod*)`, found as the last call in the tail-call target of `Object::GetVirtualMethod` | Il2CppInterop fork |
| 12 | `GetTypeInfoFromTypeDefinitionIndex`: x86 16-byte alignment check rejects arm64 functions | arm64 checks 4-byte alignment | Il2CppInterop fork |
| 13 | Plugin skipped: `[BepInProcess("DungeonSettlers.exe")]` vs Mac process "Dungeon Settlers" | macOS: compare letters and digits only (`dungeonsettlers`) | ours, `BaseChainloader.cs` |
| 14 | Crash in a patched `DeserializeCampaignSaveDataSafely` (struct return): AAPCS64 passes the return buffer in **x8**, Il2CppInterop assumes first argument (x64) | `Arm64ReturnBufferThunk`: 48-byte thunk shifts x0..x6 -> x1..x7, x8 -> x0, jumps to the trampoline | Il2CppInterop fork |
| 15 | Crash in patched `QuickSlotData.Deserialize(QuickSlotSaveData)`: AAPCS64 passes a struct of <= 16 bytes **by value in registers**, Il2CppInterop assumes a pointer (Win64) | `Arm64Abi`: such params become `long` / 16-byte `Pair` and are boxed from their address; same for <= 16-byte struct returns | Il2CppInterop fork |

Verified hook addresses for DS_B.0.4.29 arm64 offline with `tools/a64scan.py` (capstone; output
`out.noindex/a64scan_DS_B.0.4.29.txt`) and by disassembly: GenericMethod::GetMethod 0x71cd68,
GetTypeInfoFromTypeDefinitionIndex 0x752874, Class::GetDefaultFieldValue 0x76c210 (thunk -> 0x751eac),
Class::FromIl2CppType 0x76801c, Class::FromName 0x76b774 (-> 0x75cb1c). Class::Init uses the upstream substitute
`mono_class_instance_size`.

## Known gaps (not done)

- `XrefScanner.XrefScan` / `UsedBy` (uncached, managed method xrefs used by some plugins) still x86-only: throws
  PlatformNotSupported on arm64. The generator's cached xref DB is built from the x86-64 slice, so cached results
  point at x86 code. Plugins that use XrefScanner will misbehave on arm64.
- Struct-return thunk refuses methods with more than 8 general-register arguments or other by-value struct
  kinds (logs `arm64: ... not patched for arm64`); one seen in Extended Hotbar:
  `InteractionFactory.CreateUnsetItemQuickSlot` (returns struct, takes Guid). Fix: count Guid as a `Pair` there too
  (the check runs before the small-struct substitution sees it; needs one more pass).
- Injected classes (ClassInjector) with methods that take/return non-blittable structs: same ABI assumptions,
  not tested.
- Each struct-return patch maps one 16 KB page for its 48-byte thunk.
- `AccessTools.TypeByName` warnings: some generated interop types fail to load (`<>c` in UnityEngine.CoreModule,
  `TextureStackBase`1`). Harmless here; not checked whether Windows shows the same.
- Locally built `libdoorstop.dylib` / `libdobby.dylib` are linker-signed only. Shipping to other Macs: sign with the
  owner's identity (memory Trap_keychain_prompt_adhoc_sign) and never set quarantine.
- The launcher must keep `DYLD_INSERT_LIBRARIES` for Doorstop next to dsrig.dylib (dsrig unsets it; the test used
  `research/loadertest.noindex/envrestore.dylib`) and set the DOORSTOP_* variables (see `tools/bie_start.py`).

## Files

- `tools/build_install.sh`: rebuild `inst.noindex/bie` from `src.noindex` forks (nice 19, 4 threads).
- `tools/bie_start.py C [--rig]`: start a rig copy hidden, batchmode, with Doorstop (+ dsrig.dylib).
- `tools/a64scan.py GameAssembly.dylib`: offline check of the injector hook lookups on the arm64 slice.
- `patches/bepinex/`, `patches/il2cppinterop/`: our commits (format-patch) + full diff vs upstream (incl. merged PRs).
- `plugin/HelloArm64`: test plugin source.
- Not in git (`*.noindex`): forks (`src.noindex`, branch `bepinex-mac` in each), builds, install, downloads, logs,
  decompiled mod (`decomp.noindex`), the hotbar test shim.

Forks: BepInEx master 5b766a3 + cdobbyn/macos-il2cpp-paths-6e2d + cdobbyn/macos-arm64-detours-3f7b +
baisiqi6/codex/il2cpp-export-thunk-architecture + ours. Il2CppInterop master 81a6f78 + sptndc/patch-1 (#273) +
jweivy/fix/native-xref-architecture-guards (#288) + Ketlark/macho-universal-xref (#289) +
Ketlark/macos-field-default-value (#290) + ours. UnityDoorstop master 3035af7. Dobby cdobbyn/macos-arm64-dobby-8b41.
Licenses: BepInEx LGPL-2.1, Il2CppInterop LGPL-3.0, Doorstop LGPL-2.1, Dobby Apache-2.0; patches keep them.
