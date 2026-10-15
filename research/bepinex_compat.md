# BepInEx / MelonLoader compatibility for the Dungeon Settlers loader (2026-10-14)

Status: research only. Nothing installed. Game files only read.
Marks: **[V]** = checked in game files or a cited source in this session. **[U]** = not verified.

## Short answer

**Partly.** Players can use the existing Nexus mods with our multiplayer on **Windows** now. On **Mac** there is no working BepInEx or MelonLoader for IL2CPP today, so the Nexus mods do not run on Mac yet.

| Platform | BepInEx 6 IL2CPP | MelonLoader 0.7 IL2CPP | Existing DS mods run? |
|---|---|---|---|
| Windows x64 | **Yes.** Bleeding Edge build be.785 / be.788. The DS mods on Nexus are made for it. [V] | **Yes.** v0.7.3. Some DS mods ship a MelonLoader build. [V] | **Yes** |
| Linux (Proton, Windows build) | Windows build + `winhttp` DLL override. Usual method, not tested for DS [U] | Same [U] | Probably [U] |
| macOS Intel / Rosetta (x86_64 slice) | An official `macos-x64` IL2CPP zip exists, but open bugs say it does not start: no log, no config (#1096, #1373), the `.app` path lookup is broken (#1407, closed, not merged), and the fat Mach-O xref scan is broken (Il2CppInterop PR #289, open). [V] | Official `macOS.x64` zip in v0.7.3. Rosetta fixes for universal Unity 6 games were merged into `alpha-development` (PR #1201, 2026-09-28), not released. [V] | **Not today** |
| macOS Apple Silicon (arm64 native) | **No.** No arm64 build. The preloader dies in a MonoMod hook: arm64 `mprotect` RWX is refused (#1408, open, 2026-09-21). An unofficial arm64 build exists for Football Manager 26 (sortitoutsi, could not open the page: 403). [V] | **Not released, but close.** arm64 support PR #1174 merged 2026-09-29, CoreCLR arm64 detour fix PR #1216 merged 2026-10-05, both in `alpha-development`. A fork reports Bloons TD 6 working native arm64, and Bloons TD 6 uses **the same Unity version as DS (6000.0.58f2)**. No official release yet. [V] | **Not today** |

Game facts [V]: Unity **6000.0.58f2** (Info.plist), IL2CPP metadata **v31** (`global-metadata.dat` header `af1bb1fa 1f000000`), `GameAssembly.dylib` and `UnityPlayer.dylib` are **universal (x86_64 + arm64)**, game version `DS_B.0.4.29`. Because the Mac app is universal, a Rosetta route exists in principle.

**Recommendation:** option (d) + (a) now, and (b) only as "help upstream" later. Keep our native loader as the base on both platforms. On Windows, the launcher can also install a pinned BepInEx 6 BE build next to it. Our multiplayer stays native. On Mac, follow MelonLoader `alpha-development` (nearest to arm64) and BepInEx #1408. Test DS on them when a build exists. Do not write our own BepInEx host.

## 1. BepInEx 6 IL2CPP status per platform

- IL2CPP is only in Bleeding Edge (BE) builds. The docs list only win-x86 / win-x64 for IL2CPP. [3]
- builds.bepinex.dev, latest BE = **be.788** (`+5b766a3`). IL2CPP zips for be.788: `win-x86`, `win-x64`, `linux-x64`, `macos-x64`. **No arm64 zip.** [V, 1]
- Stack: Doorstop proxy (`winhttp.dll` on Windows, `libdoorstop.dylib` with `DYLD_INSERT_LIBRARIES` on Mac) -> hosts **CoreCLR (.NET 6)** -> **Il2CppInterop** makes proxy assemblies from the game -> **HarmonyX** patches the native method pointers (native detours: Dobby / MonoMod).
- Metadata v31 / Unity 6: works on Windows. The DS mod "Food Shelf Life Freeze" says it is for Unity 6000.0.58f2, BepInEx be.785, .NET 6. Some other games with older BE builds report "metadata 23-29 supported, got 31". Use be.785+ only. [V, 9]
- macOS, the open bugs:
  - #1096 (2025-04, Intel): BepInEx does not start, no `BepInEx.cfg`, no log. Open. [4]
  - #1373 (2026-07, Apple Silicon under Rosetta): doorstop + CoreCLR load, then stop silently. No log, no config, no interop. Open. [4]
  - #1407 (2026-09): the game data folder and `GameAssembly.dylib` are not found inside a `.app` bundle. The commit text says "every IL2CPP macOS game threw DirectoryNotFoundException". Closed by its author on 2026-09-25, **not merged**. With the fix, interop generation still failed in LibCpp2IL. [5]
  - #1408 (2026-09-21, Apple Silicon native, Unity 6000.0.61f1, .NET 6 osx-arm64, a self-built arm64 Dobby): the preloader dies in `ConsoleSetOutFix` (MonoMod `ILHook`). Cause: on arm64 macOS, `mprotect` with RWX returns EACCES. The fix (PR #1409) needs a `doorstop_jit_memcpy` that Doorstop 4.5.0 does not have. The same build **works under Rosetta** up to "Runtime invoke patched". Open, no maintainer reply. [6]
  - #1403: on ARM64, `FollowExportThunks` reads ARM64 code as x86 jumps. Open. [4]
  - #899 (2024): "MacOS arm64 support". Still open. [4]
- Il2CppInterop: PR #288 (do not run the x86 decoder on ARM64 code) and PR #289 (scan the x86-64 slice of a **universal** Mach-O; today the offsets are wrong and xref scanning fails) are both open. #289 hits DS directly, because DS's `GameAssembly.dylib` is universal. [7]
- CoreCLR itself runs on osx-arm64 (.NET 6+ has osx-arm64; #1408 used it). The blockers are the hook layer (W^X / `MAP_JIT` on Apple Silicon) and the x86-only scanners, not the runtime. [6, 7]

## 2. MelonLoader IL2CPP status

- Windows x64: v0.7.3 (2026-05-14) works with Unity 6000+ (`Il2CppAssetBundleManager`, `Il2CppImageConversionManager` fixes), Il2CppInterop 1.5.1. [8]
- macOS: v0.7.3 ships `MelonLoader.macOS.x64.zip` + an installer `.dmg` + a launch script (`melonloader-launch.sh`, `DYLD_INSERT_LIBRARIES`). x64 only, so Rosetta on Apple Silicon. [8]
- In `alpha-development` (not released) [V, 10]:
  - #1174 (merged 2026-09-29): osx-arm64 build, arm64 Dobby + plthook, `dlsym` interpose (plthook failed on modern chained fixups), `MLMacOSJitCopy` for arm64 JIT writes, separate `macOS.arm64` / `macOS.x64` zips.
  - #1201 (merged 2026-09-28): universal Unity games under Rosetta (tested on a Unity 6000.3.10f1 game).
  - #1216 (merged 2026-10-05): arm64 CoreCLR (IL2CPP) detour selection + protected writes. This was the missing part for IL2CPP on arm64.
  - #1215, #1219 (merged 2026-10-05): stop the injected bootstrap from leaking into child processes (Cpp2IL helper).
  - #1222 (open): crash fix in the Unix `Player.log` mirror hook (data race).
- A fork commit on top of #1174 reports **Bloons TD 6 (Unity 6000.0.58f2) loading with native arm64 class injection**. Not an official build. [10]
- Conclusion: MelonLoader is the nearest to running DS mods on Apple Silicon. Expect a 0.7.4 / 0.8 release with arm64 [U: no date announced].

## 3. Existing Dungeon Settlers mods (Nexus public API, 2026-10-14) [V, 9]

16 mods on Nexus, **all Windows**, none mention Mac. None on Thunderstore found.

| Mod | Author | Loader | Downloads |
|---|---|---|---|
| Tic's Settler Toolkit | Tic0311 | BepInEx 6 IL2CPP be.788 | 845 |
| Extended Hotbar | Mozzez | **BepInEx 6 be.788 and MelonLoader 0.7.3** (two downloads), build 25846389 | 231 |
| Increase Max Stack, Disable Item Decay, Modify Item Properties, Reactive Status Bars | Cedel1 | BepInEx plugin | 226 / 217 / 123 / 42 |
| Food Shelf Life Freeze | YaLiQAQ | BepInEx 6 IL2CPP be.785, Unity 6000.0.58f2 | 216 |
| Delvers Core, Delvers Frieren | Mozzez | **BepInEx 6 and MelonLoader 0.7.3**, build 25846389 | 52 / 49 |
| 256 Units, MaouXGuiltycrownZ, DuelForExp | Boboxx103 | BepInEx (page links the be.788 *Mono* zip; probably an author error, the game is IL2CPP) | 36 / 9 / 7 |
| Wearefamily, WASDcontrol | boatify7 | BepInEx (same Mono link) | 12 / 4 |
| Dungeon Settlers Tweaks | frostwz120 | BepInEx (bundled, Windows installer on GitHub) | 0 |
| Friendly Fire Off | ASHHIKARI | not stated | 0 |

So: **BepInEx 6 IL2CPP is the de facto loader for DS.** 13 of 16 are BepInEx-only; 3 also have a MelonLoader build. The Discord export in `research/discord` has no mention of BepInEx, MelonLoader or Nexus.

## 4. Prior art: multiplayer as a loader plugin for an IL2CPP game

- **Open Nest Co-op** (Iron Nest, IL2CPP): one mod, two builds (BepInEx 6 IL2CPP BE + MelonLoader 0.7), Steamworks P2P with relay. "Host and client don't even need the same loader; only the mod version must match." Also a "standalone" zip with the loader bundled. Windows only. [11]
- **Iron Nest Co-Op (2 iron nests)**: same pattern, MelonLoader or BepInEx 6 IL2CPP x64. Windows. [11]
- **Get To ReWorked** (Get To Work): MelonLoader multiplayer, Steam P2P, "remove BepInEx first". [12]
- **SteamNetworkLib_Il2Cpp** (Schedule I): a MelonLoader library for Steam lobbies + P2P that other mods use. [13]
- **Deep Synergy** (Subnautica 2): BepInEx IL2CPP co-op. [12]
- **BepInEx.MelonLoader.Loader**: runs MelonLoader mods inside BepInEx 6 (IL2CPP). Supports only MelonLoader 0.5.7; mods often fail on missing types when the generated assemblies differ. Not usable for 0.7 mods. [14]
- **Mac:** none of these mods supports Mac. I found **no** example of an IL2CPP multiplayer plugin with Mac players, and no launcher that runs BepInEx plugins and its own native hooks on Mac. [U: absence of evidence, not proof]

## 5. Options for us

Facts that decide it:
- BepInEx / MelonLoader plugins are .NET IL (AnyCPU). The interop assemblies have the same names on Mac and Windows. So **one plugin DLL can run on both platforms** once a loader runs on Mac. [U: true for the usual Harmony-only plugins; wrong for plugins with Windows P/Invoke]
- Our MP is native C with RVA tables + `det_ranges` per platform, and cross-play Mac arm64 <-> Windows x64 passes today (win/NOTES.md). Moving MP into C# does not remove the native determinism work.
- Our loader (`baselib.dll` proxy) and BepInEx's (`winhttp.dll` Doorstop) are different proxy DLLs, so both can load in one process [U: test]. Both put a jump at the start of native functions (our MinHook / dsl_hook vs Harmony's native detour on IL2CPP method pointers). Two hooks on **one** function can chain or break [U: test per shared function].

| Option | What it is | Effort | Risk | Mods on Win | Mods on Mac | MP Mac <-> Win with mods |
|---|---|---|---|---|---|---|
| **(a)** BepInEx 6 on Windows, our loader on Mac | Launcher installs pinned be.788 on Windows. Mac runs only our native mods. | S-M | Low. BE builds change; pin one build. | Yes | **No** (only our native mods) | Only when no peer runs a sim-changing BepInEx mod. Mac players cannot match a Windows player who has one. |
| **(b)** Port BepInEx + Il2CppInterop to macOS arm64 | Fix Doorstop JIT writes, MonoMod arm64 W^X, Il2CppInterop arm64 scanners + fat Mach-O, `.app` paths | **L-XL** (weeks) [U] | High. Same work is already in progress upstream (#1408/#1409, Il2CppInterop #288/#289). We would keep a fork per BE build. | Yes | Yes when done | Yes, if the same plugin set + versions run on both |
| **(c)** Our loader hosts BepInEx plugins | Embed CoreCLR + Il2CppInterop + HarmonyX + the BepInEx plugin API in our loader | **XL** | Highest. This is (b) plus a BepInEx clone. All Mac blockers of (b) stay. | Yes | Yes when done | Yes |
| **(d)** MP as a BepInEx plugin + a native part | C# plugin for UI/menus/net; native part for determinism hooks | M-L to move code | Medium. Mac still needs a loader (same blocker as (b)). Determinism hooks stay native anyway. | Yes | No until (b) or upstream | Same as (a) on Mac |
| **(a)+(d-light), recommended** | MP stays native on both platforms. Windows launcher can add pinned BepInEx 6 BE (and/or MelonLoader 0.7.3). On Mac, track MelonLoader arm64 (alpha) and BepInEx #1408; add it to the Mac launcher when a release runs DS. | S-M now | Low now | Yes | Later, from upstream | Yes for cosmetic mods now; for sim mods once Mac has a loader |

Join check change for all options: the HELLO mod-set hash must also list **BepInEx / MelonLoader plugin DLLs** (file name + sha256) with a `sim` flag. Unknown plugins = treated as `sim: true`, so a mismatch is refused, not desynced. A plugin we cannot check (a Nexus DLL) cannot be in our signed index unless the owner re-hosts it [U: Nexus terms; authors' permission].

**Which keeps MP between Mac and Windows with mods:** only a set where every sim-changing mod runs on both platforms. Today that means **our native mods only**. Cosmetic BepInEx mods on Windows are fine. Full Nexus-mod parity on Mac needs a Mac IL2CPP loader. Upstream MelonLoader is the nearest (arm64 IL2CPP merged in alpha, same Unity version reported working). Note: 13 of the 16 DS mods are BepInEx-only, so even MelonLoader on Mac covers only 3 of them.

Cheap next steps (no game install touched):
1. On an APFS clone (`cp -c -R`), build MelonLoader `alpha-development` osx-arm64 and test DS headless. Bloons TD 6 with the same Unity version is the closest evidence. [S]
2. On the Windows test PC / CrossOver bottle: our `baselib.dll` + BepInEx be.788 + Extended Hotbar together. Check: both load, MP cross-play gates still green. [S]
3. List the functions our MP hooks and the functions popular DS plugins patch (decompile their DLLs). Overlap = the hook-chain risk. [S]

## Sources
1. BepInEx BE builds (be.788 artifacts: IL2CPP win-x86, win-x64, linux-x64, macos-x64): https://builds.bepinex.dev/projects/bepinex_be
2. Unofficial BepInEx IL2CPP for macOS Intel + Apple Silicon (FM26): https://sortitoutsi.net/content/77878/bepinex-6-be-unity-il2cpp-macos-intel-apple-silicon (search snippet only; page 403)
3. BepInEx IL2CPP install docs: https://docs.bepinex.dev/master/articles/user_guide/installation/unity_il2cpp.html
4. BepInEx issues: https://github.com/BepInEx/BepInEx/issues/1096 , /1373 , /1403 , /899 , /1303
5. BepInEx PR #1407 (.app bundle paths, closed unmerged): https://github.com/BepInEx/BepInEx/pull/1407
6. BepInEx issue #1408 (arm64 preloader, RWX mprotect): https://github.com/BepInEx/BepInEx/issues/1408
7. Il2CppInterop PRs #288, #289: https://github.com/BepInEx/Il2CppInterop/pull/288 , https://github.com/BepInEx/Il2CppInterop/pull/289
8. MelonLoader releases (v0.7.3 notes, macOS x64 assets): https://github.com/LavaGang/MelonLoader/releases
9. Nexus Mods public GraphQL API, game `dungeonsettlers` (16 mods, descriptions): https://api.nexusmods.com/v2/graphql ; e.g. https://www.nexusmods.com/dungeonsettlers/mods/5 , /mods/2 , /mods/3
10. MelonLoader PRs: https://github.com/LavaGang/MelonLoader/pull/1174 , /1201 , /1215 , /1216 , /1219 , /1222
11. Open Nest Co-op: https://www.nexusmods.com/ironnest/mods/36 ; Iron Nest Co-Op: https://www.nexusmods.com/ironnest/mods/81
12. Get To ReWorked: https://www.nexusmods.com/gettowork/mods/16 ; Deep Synergy (Subnautica 2): https://www.skills.sh/aradotso/devtools-skills/subnautica-ii-coop-mod
13. SteamNetworkLib_Il2Cpp: https://thunderstore.io/c/schedule-i/p/ifBars/SteamNetworkLib_Il2Cpp/
14. BepInEx.MelonLoader.Loader: https://github.com/BepInEx/BepInEx.MelonLoader.Loader/
Local files read: game Info.plist, `global-metadata.dat` header, `lipo -info` on GameAssembly/UnityPlayer, `~/DungeonSettlersMods/CONTRACT.md`, `profile.json`, loader folder list, `research/plan_launcher.md`.
