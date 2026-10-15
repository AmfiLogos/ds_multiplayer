# Dungeon Settlers mod launcher - plan (2026-10-14)

Status: plan only. No code yet. Owner decisions are in PLAN.md, section "Launcher".
Marks: **[V]** = checked in our files or a cited source in this session. **[U]** = not verified, test before we build on it.

Short answer to the owner's question "can it be a generic mod launcher?": **yes**. Most of the parts exist already.
- `~/DungeonSettlersMods` already has a manifest (`mod.json`), three mod types (builtin / data / code), a code-mod loader with a plugin API (`loader/sdk/dsl_sdk.h`, hooks, a crash guard, a per-build table) and a manager (`dsmm` + a SwiftUI app). **[V]**
- `~/ds_multiplayer` already has the "separate copy of the game" method (`rig/make_mp_app.sh`) and a Windows loader (the `baselib.dll` proxy). **[V]**
- The launcher joins these two parts. The multiplayer mod becomes one code mod in the list.

## TLDR

| Change | Impact | Cost | Failure point |
|---|---|---|---|
| 1. One launcher app (Mac + Windows) that keeps a modded copy of the game per Steam build, next to Steam's own install | Steam's install is never touched. A Steam update never breaks the mods. | M. Mac: APFS clone, almost no disk. Windows: a full copy, about 4 GB per build **[U: Windows size]** | Windows has no clone, so disk use doubles. Steam must be installed and the game owned. |
| 2. One small loader we own (Mac dylib, Windows `baselib.dll` proxy) that loads N code mods from a `mods/` folder | Multiplayer, multi-unit bars, floor logger and later mods all load the same way | M-L. Merge `dsmods.dylib` (loader C) and `dsrig.dylib` (MP) into one hook engine | Two mods that hook the same function. The loader refuses the second one (rule exists in dsl_sdk.h). |
| 3. Manifest per mod: id, version, platforms, supported game builds, kind (data / patch / code), sim-affecting yes/no, requires, conflicts, sha256 per file | The launcher can check everything before the game starts | S | Authors who forget a field. A schema check in the publish script stops that. |
| 4. Pin the game build. Auto-switch to the new build when every enabled mod has a version for it | The owner's rule: play on, update without breaking | S-M | A new player arrives after Steam updated and before the mods are ready. Then there is no old build on his disk (see 7). |
| 5. Files on GitHub Releases + one signed `index.json`. Download page on dungeonsettlers.wiki links to GitHub. | No Vercel bandwidth. GitHub has no bandwidth limit on releases. **[V]** | Free (public repo) | A private repo needs a login to download **[U]**, so the release repo must be public. |
| 6. Only signed mods: the index and every file are checked (ed25519 signature + sha256) | Answers the Discord concern "one bad actor". No peer pushes files to another peer. | S | The owner's private signing key. If it leaks, every launcher trusts the attacker. Keep it offline + a backup. |
| 7. Optional: download the old pinned build from Steam with the player's own login (DepotDownloader) | A late player can still join a group that is on the old build | M | Steam login inside our tool = a trust question. Off by default. Reports say chunk downloads time out often. **[U]** |
| 8. Multiplayer join check: same game build, same platform rounding mode, same sim-affecting mod set (id + version + hash) | A mismatch is refused before the world loads, with a clear list of the differences | S. HELLO already sends build + mod version + round mode **[V]** | Cosmetic-only mods must be marked correctly, or a desync follows |
| 9. Signing: Apple Developer ID 99 USD/yr; Windows signing optional | Signed = opens with a normal double-click on Mac | 99 USD/yr (Mac). Windows: see Costs. | Unsigned Mac launcher: the player must use System Settings > Privacy & Security > Open Anyway (macOS 15+) **[V]** |
| 10. UI: Tauri 2 (one web UI + Rust core) recommended; native Swift + C# is the other option | One UI for two platforms | M | WebView2 must be on the Windows PC (it is on Windows 11 **[U: Windows 10]**) |

## 1. What existing launchers do (research)

| Tool | How mods load | What we take from it |
|---|---|---|
| r2modman / Thunderstore | Profiles: each profile has its own mods and configs. Packages have a `manifest.json` with `name`, `version_number`, `description`, `website_url`, `dependencies` as strings `Team-Name-1.2.3`. A version can never be changed after upload. Launch goes through Doorstop with arguments that point at the profile's BepInEx folder. [1][2] | Profiles; dependency strings with an exact version; "a version is immutable"; launch arguments that point at a profile, so the game folder holds nothing of ours |
| BepInEx 6 (IL2CPP) | Unity Doorstop puts a `winhttp.dll` proxy next to the game. Unity loads it early. It starts the .NET runtime and Il2CppInterop. IL2CPP builds are "Bleeding Edge" only, Windows x86/x64. [3][4] | The proxy-DLL idea (we use `baselib.dll`, see win/NOTES.md section 4). We do NOT use BepInEx: our mods are C, BepInEx is large, and there is no macOS IL2CPP build in the docs **[U]**. |
| MelonLoader | `version.dll` proxy. The docs say the proxy method is Windows only. v0.7.3 notes "fixed compilation issues for MacOS". [5][6] | Same conclusion as BepInEx: no Mac IL2CPP path we can use |
| Vortex (Nexus) | Mods sit in a "staging" folder. "Deployment" puts them into the game folder as hard links or symlinks. Hard links need the same drive. Users report that symlinks do not work under Wine. [7] | Staging folder + deploy into a game copy. We copy into OUR copy only, never Steam's folder. Use real files, not symlinks (CrossOver test bottle). |
| Steam beta branches | Only the game developer can make them | Not available to us (we are not the developer) |
| Steam console `download_depot` / DepotDownloader | Downloads an old manifest of a game you own. Console method: some guides say it was removed. DepotDownloader: `-app -depot -manifest`, login with `-qr` (Steam mobile app) and `-remember-password`. Needs .NET. Users report chunk timeouts and retries. [8][9][10] | Optional rescue path for a late player (TLDR item 7). Never ship game files ourselves. |

## 2. User flow

### First run
1. The player downloads the launcher from the wiki page (button links to `github.com/<org>/<repo>/releases/latest/download/...`).
2. The launcher finds Steam's install of Dungeon Settlers:
   - Mac: `~/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app` (path used today by make_mp_app.sh **[V]**). Other Steam libraries: read `steamapps/libraryfolders.vdf` **[U: exact format]**.
   - Windows: the Steam folder from the registry, then `libraryfolders.vdf`.
   - Build id: `steamapps/appmanifest_2798330.acf`, key `buildid` **[U: read it, but also check the GameAssembly UUID / hash as make_mp_app.sh does]**. App id 2798330 is in mp_launcher.c **[V]**.
3. The launcher checks that Steam's files are stock (Mac: Apple's seal in `_CodeSignature/CodeResources` + the developer team id on GameAssembly, as make_mp_app.sh and dsmm do today **[V]**; Windows: a sha256 list per build that we publish in the index).
4. It downloads the signed `index.json` and shows the mod list: name, version, "works on your game build: yes/no", platforms.
5. The player turns mods on (profile = a list of enabled mods, like r2modman). Multiplayer is one row in that list.
6. Play: the launcher builds the copy for this build (section 3), installs the mods, starts the game.

### Play
- Press Play. The launcher checks the copy (files + mods match the profile), then starts the game from the copy:
  - Mac: like mp_launcher.c today: own save folder, `CFFIXED_USER_HOME`, `steam_appid.txt`, `DYLD_INSERT_LIBRARIES=<loader>` **[V]**.
  - Windows: the copy has our `baselib.dll` (original renamed `baselib_orig.dll`) **[V]**.
- Steam stays open and signed in (Steam invites need it).
- Each profile has its own save folder. The first run COPIES the normal saves (as today). The normal game's saves are only read.

### Update (the owner's rule: pin, then auto-update)
1. Steam updates the real install to build N+1. Our copy stays on build N. The player keeps playing on N.
2. The launcher checks the index at start and every few hours while open.
3. When every enabled mod has a version that lists N+1 for this platform, the launcher:
   - builds a new copy N+1 from Steam's install (only if Steam's install is now exactly N+1 and stock),
   - installs the new mod versions,
   - makes N+1 the active build,
   - keeps copy N for one more build (so the player can still join a friend who has not updated). It removes N only when N+2 is active, and it asks first.
4. A mod with no N+1 version: the launcher says which mod holds the update back. The player can choose "update now without <mod>" (that mod is then off).
5. The launcher updates itself with the same signed channel (section 6).

### Multiplayer join check
Today HELLO sends: protocol, mod version, round mode, user id, game build, name **[V: src/net/dsnet_internal.h line 10]**.
Add one field: **the mod-set hash** = sha256 of the sorted list of `id@version:filehash` for every enabled mod with `sim: true`.
- Host compares. Different build, round mode or mod-set hash -> refuse with a reason code.
- Then the host sends its full list (id + version), so the guest's launcher can show: "Host has Hell difficulty 1.2.0. You do not have it." with one button "Get the same mods" (only from the signed index, never from the host's disk).
- Mods with `sim: false` (for example the notch fix: window only) can differ between players. The author sets this flag, and the owner checks it before signing.
- Mac + Windows in one game: allowed when both have the same build and the same mod versions for both platforms (cross-play passes today on build 25846389 **[V: win/NOTES.md sections 9 and 14]**).

## 3. Architecture

```
Launcher app (UI + core)
 ├─ reads Steam install (read only)
 ├─ games/<build>/<platform copy>         one modded copy per pinned build
 │     ├─ stock files (clone / copy), checked
 │     ├─ static patches applied (patch mods), one signature at the end (Mac)
 │     ├─ loader (Mac dsml.dylib / Windows baselib.dll proxy)
 │     └─ mods/<id>/ (code plugins + data)   installed from the cache
 ├─ cache/<id>/<version>/                  downloaded + verified mod files
 ├─ profiles/<name>.json                   enabled mods, order
 └─ saves/<profile>/                       own save folder per profile
```

### Copy of the game per build
- Mac: `cp -c -R` (APFS clone) as make_mp_app.sh does. Clone costs almost no disk. When Steam later rewrites a file, only the changed blocks use new space **[V: method in use; disk figure not measured]**. Copy steps reused from make_mp_app.sh: stock GameAssembly check, seal check, `xattr -c`, minos 12 fix for the loader, ad-hoc signatures inside-out, own bundle id, executable = our small launcher stub.
- Windows: full copy (NTFS has no clone). Hard links are NOT safe: if Steam patches a file in place, the copy changes too **[U: how Steam writes updates]**. Copy size about the Mac size (3.7 GB) **[U]**.
- Never more than 2 builds on disk unless the player asks.

### The loader (one per platform, we own it)
Start point: `~/DungeonSettlersMods/loader` (dsmods.dylib, plugin API v1: `dsl_plugin_init`, `host->hook`, name lookups `find_method_address`, per-build table `dsl_builds.h`, crash guard) **[V]**.
Changes:
1. Load from the launcher's `mods/` folder, read each `mod.json`, check build + sha256, load in dependency order.
2. Inject with `DYLD_INSERT_LIBRARIES` from the launcher stub (as MP does), not with an LC_LOAD patch in GameAssembly. Then GameAssembly stays byte-stock for code mods.
3. One hook engine. Today MP has its own (`src/rig_hook.c`) and dsmods has its own (`dsl_hook.c`). They both use the zero tail of `__TEXT` for stubs. Two engines in one process will collide on that space. Merge them into one before the first combined release. **[V: both exist; collision is a conclusion, not a test]**
4. Windows: the `baselib.dll` proxy (`win/port/rig_main_win.c`, MinHook) gets the same plugin API: `LoadLibrary` for each `mods/<id>/<id>.dll`. Same `DSLHost` struct, x64 code.
5. API rule: `DSLHost` only grows (fields appended, `size` field) **[V: rule in dsl_sdk.h]**.

### Manifest (`mod.json`, extends CONTRACT.md; only new fields added)
```json
{ "id": "multiplayer", "name": "Multiplayer", "version": "0.10.0", "author": "AmfiLogos",
  "kind": "code",                       // data | patch | code
  "api": 1,
  "sim": true,                          // changes the simulation -> must match in multiplayer
  "platforms": {
    "mac-arm64": { "files": { "multiplayer.dylib": "sha256...", "det_ranges.tsv": "sha256..." } },
    "win-x64":   { "files": { "multiplayer.dll":   "sha256...", "det_ranges_win.tsv": "sha256..." } } },
  "game_builds": ["25846389"],          // exact Steam build ids; "*" only for mods with no RVA and no table use
  "hooks": { "25846389": { "mac-arm64": "rva/25846389_mac.tsv", "win-x64": "rva/25846389_win.tsv" } },
  "requires": ["loader>=2.0.0"],
  "conflicts": ["hell-difficulty"],
  "description": "one line" }
```
- `requires` / `conflicts` use an id plus an optional version range. The launcher refuses a profile with a conflict and says which two mods.
- Load order = dependency order, then the profile order.

### How the owner's existing mods become modules
| Mod today | Kind in the launcher | Work |
|---|---|---|
| Multiplayer (`dsrig.dylib` + `det_ranges.tsv`; Windows `baselib.dll`) | code, sim: true | Turn it into a plugin of the merged loader (its own `dsl_plugin_init`). Its menus stay as they are. |
| Multi-unit skill bars (inside dsmods.dylib today) | code, sim: false **[U: check it only changes UI]** | Split from the loader into its own plugin |
| Floor logger (example) | code, sim: false | None |
| Cheaper recruits, richer treasure rooms, tougher monsters | data, sim: true | `dsdatamod apply` on the copy. Tables bundle + 8 bytes of catalog.bin **[V]**. Not tested in the game yet **[V: datamod README]**. Windows tables path differs (`StandaloneWindows64`?) **[U]** |
| Notch fix | patch (Mac only), sim: false | Static change in the copy at build time. Not needed on Windows. |
| Weapon-skill slot | patch, sim: true | Changes GameAssembly bytes + `data.unity3d` assets per build **[V: memory DungeonSettlers_ModSet]**. Ship as a patch file with "expected old bytes" checks, applied to the copy. |
| Hell difficulty, GCD uses CDR | patch, sim: true, off by default | Same as weapon slot. GCD-CDR has no build-25846389 version yet **[V: manager README]**. Better later: rewrite as code plugins (runtime hooks), then they survive updates more easily. |

Patch mods need their own author tools (the owner's scripts in `~/ds_weaponslot`, `~/ds_skill_truth`). The launcher only applies the published patch file. It never runs a script from a mod.

## 4. How RVAs survive a game update
Three layers, best first:
1. **Name lookup at run time.** `find_method_address("Namespace", "Class", "Method", argc)` through il2cpp **[V: in dsl_sdk.h]**. Works across builds while the name and the argument count stay the same. This is the default for new code mods.
2. **Signature scan** for things with no name: inline patch sites, compiler-inlined code (win/NOTES.md lists MSVC-inlined code on Windows, for example `InputOnAccept` **[V]**). A short byte pattern with wildcards, and the result must be unique, else the mod refuses to load.
3. **Per-build tables** (what MP uses today: RVAs + `det_ranges.tsv` per platform). Made by the dump pipeline on each new build.

Dump pipeline (on the owner's Mac, script, one command per new build):
1. Steam build N+1 arrives (the `ds-update` skill already runs the wiki update).
2. Il2CppDumper on the arm64 slice + metadata (recipe in memory `macos_il2cpp_patching`) and on the Windows GameAssembly.dll (win_map.py **[V]**).
3. For each mod: resolve its names to RVAs, write `rva/<build>_<platform>.tsv`, diff against build N, flag any method whose body changed.
4. Gates: loader host tests; for MP: the determinism + cross-play test set (xplay, kit dry run).
5. Publish new mod versions that list N+1. The launchers switch by themselves (section 2).
MP is the slow one (many hooks + determinism tests). Expect days, not hours, per game update **[U: estimate]**.

## 5. Security ("one bad actor")
- **No peer sends files to another peer. Ever.** Mod mismatch = refuse to join + a button to get the same versions from the signed index.
- **Signed index.** `index.json` lists every mod version with its file hashes and URLs. The owner signs it with an ed25519 key (minisign format). The launcher has the public key built in. A file whose sha256 is not in a signed index is not loaded.
- **Pin the index version too:** the launcher refuses an index older than the last one it saw (stops a "replay the old signed index" attack; Tauri's updater had this exact bug class, CVE-2026-95625 [11]).
- **Curated.** Third-party authors send a mod; the owner (or his review) checks the source, builds it, signs it. Like Thunderstore, a published version is immutable [1].
- **Developer mode** (local unsigned mods, for authors): off by default, a red banner, and the multiplayer join check marks such a player as "unsigned mods". The host setting "only signed mods" (default on) then refuses him.
- Code mods are native code with full user rights. Signing proves who published it, not that it is safe. Say this plainly on the download page.
- Signing key: offline, on the owner's Mac (Keychain or a file outside any repo), plus one encrypted backup. A second key for rotation, listed in the launcher from day one.

## 6. Update channel
- Repo: one PUBLIC GitHub repo, for example `dungeon-settlers-mods` (name: owner's choice). Releases:
  - `launcher-vX.Y.Z`: `DSModLauncher-mac.zip`, `DSModLauncher-win.zip` (or .msi), `.sig` files.
  - `<mod-id>-vX.Y.Z`: mod files per platform.
  - `index`: a rolling release that holds `index.json` + `index.json.sig`. The launcher reads `https://github.com/<org>/<repo>/releases/download/index/index.json`.
- GitHub limits: each release file under 2 GiB, no total or bandwidth limit [12]. Our files are small (MP dylib, data patches: a few MB). Plain download URLs are not REST API calls, so the 60 calls/hour unauthenticated API limit does not apply **[U]**. Do not use the REST API from the launcher.
- Launcher self-update: Tauri updater (signature required, cannot be turned off [11]) with plugin >= 2.12.0, or Sparkle (Mac) + a small Windows updater if we go native **[U: pick with the UI choice]**.
- Wiki page: one static page in `~/dungeonsettlersdb` (built by the wiki pipeline), two buttons that link to the GitHub "latest" URLs, a short install guide for each platform, plus the Gatekeeper / SmartScreen steps. Vercel serves only the HTML page.

## 7. UI technology
| Option | Size | Work | Notes |
|---|---|---|---|
| **Tauri 2** (Rust core + web UI in WKWebView / WebView2) | small (a few MB) **[U: measure]** | one UI, one core | Signed updater included. The game's look (fonts, frames) is easy in HTML/CSS. Our team writes web UI every day (the wiki). Recommended. |
| Swift/SwiftUI (Mac) + C# WinForms/WPF (Windows) | small | two UIs, shared core needs a C library | The Mac manager app already exists in SwiftUI **[V]**, but it calls Python (`dsmm.py`), which a player does not have. |
| Electron | 100+ MB | one UI | Too big for this job |
Core logic (copy, verify, patch, index, signature) goes into one Rust crate in all cases. The Python tools (`dsmm.py`, `dsdatamod.py`) are ported or called only on the owner's Mac for publishing. **Data mods must apply on the player's machine without Python**: port the `dsdatamod apply` part (bundle rewrite + catalog CRC) to Rust, or ship pre-built table bundles per build in the mod (simpler; size about 2.9 MB per bundle **[V: stock_size 2889096]**). Recommendation: pre-built bundles, made and signed on the owner's Mac.

## 8. Costs and signing
| Item | Cost | If we do not pay |
|---|---|---|
| Apple Developer Program (Developer ID + notarization) | 99 USD/yr [13] (notarization included [13]) | macOS 15+: the first open of a downloaded unsigned app fails; the player opens System Settings > Privacy & Security > Open Anyway and types his password. Control-click no longer bypasses Gatekeeper [14]. One-time per app version **[U: per update or once]**. |
| Mac game copy + mods | 0 | Built on the player's Mac and ad-hoc signed there, like today. Files our launcher downloads itself get no quarantine flag unless the app turns on `LSFileQuarantineEnabled` **[U: test]**. We still run `xattr -c` (as make_mp_app.sh does). |
| Windows: Azure Artifact Signing (was "Trusted Signing") | about 9.99 USD/month [15] | Public trust only for organizations in USA/Canada/EU/UK and individuals in USA/Canada [15]. The owner as an individual in Portugal does NOT qualify. His US LLC might qualify **[U: Microsoft's identity checks, years of history]**. |
| Windows: OV certificate from a CA | a few hundred USD/yr **[U: current price, hardware token rule]** | SmartScreen reputation still builds slowly per file, same as Artifact Signing [15] |
| Windows unsigned | 0 | SmartScreen: "More info" > "Run anyway". Smart App Control (if on): blocks, no per-app override [16]. Antivirus may flag a proxy DLL **[U]**. |
| GitHub Releases | 0 (public repo) | - |
Recommendation: pay Apple (most players of the Mac MP are on Mac today). Start Windows unsigned with clear steps on the wiki page. Revisit Windows signing when the Windows player count grows.

## 9. Milestones (sizes: S = under 1 agent-day, M = 1-3, L = 3+)
| # | Step | Size | Depends on | Done when |
|---|---|---|---|---|
| 0 | Owner choices: UI tech, Apple membership, repo name, Windows signing now or later, DepotDownloader rescue yes/no | - | - | answered |
| 1 | Manifest schema v2 + index format + `sign_index` / `publish_mod` scripts (owner's Mac) | S | 0 | a signed test index validates |
| 2 | Merge the two hook engines; loader v2 loads plugins from `mods/` with checks (Mac) | L | - | MP + multi-bars + floor logger load together on a rig copy; MP gates green |
| 3 | MP as a plugin (Mac), HELLO mod-set hash + mismatch list | M | 2 | mp2 + owner-flow tests green |
| 4 | Core: Steam detect, build id, copy per build, stock checks, patch apply, ad-hoc sign (Mac) - port of make_mp_app.sh | M | 1 | a copy built from the real install (read only) equals today's MP app byte for byte, except planned changes |
| 5 | Windows: baselib proxy loads plugins; Windows core (copy, verify) | M | 2, 4 | CrossOver bottle test + kit dry run identical |
| 6 | UI (first run, profiles, mod list, Play, update banner) | M | 4 | headless UI tests; owner looks at it live |
| 7 | Update channel + launcher self-update + pin/auto-switch logic | M | 1, 4 | test with a fake build N+1 index |
| 8 | Wiki download page | S | 7 | page live, links resolve |
| 9 | New-build pipeline (dump -> RVA tables -> gates -> publish) | M | 2 | dry run on build 25846389 reproduces today's tables |
| 10 | Data mods in the launcher (pre-built bundles) + first in-game test of a data mod | S-M | 4 | a data mod runs in the game in MP on both peers, no desync |
| 11 | Optional DepotDownloader rescue path | M | 4 | owner says yes first; test with his account only |
| 12 | Friend test Mac + Windows | S | 3, 5, 6, 7 | owner + friend play from launcher installs |

Parallel lanes: 1, 2 and 4 have no shared files and can run at once. 5 and 6 can run at once after that.

## 10. Open points (not verified)
- Windows install size and how Steam writes updates (decides copy vs hard link).
- `appmanifest_2798330.acf` `buildid` matches the build ids we use (25846389).
- Whether the multi-unit bars mod changes the simulation (sets `sim`).
- Gatekeeper "Open Anyway": once per app, or again after each update of an unsigned app.
- Quarantine on files downloaded by our own launcher (Mac) and Mark of the Web on Windows.
- DepotDownloader: does it still get the old Dungeon Settlers manifests; Mac depot id.
- GitHub download URLs and rate limits for many players at once.

## Sources
1. Thunderstore package basics (manifest keys, dependency strings, immutable versions): https://hytalemodding.dev/en/docs/publishing/thunderstore/package-basics ; example package https://thunderstore.io/c/valheim/p/PoundTown/pt_valheim/v/1.1.1/
2. r2modman profiles + Doorstop launch arguments: https://thunderstore.io/c/inscryption/p/The_Unwanted_but_Useful_Libraries/Unused_Sigils_Disabler_by_Void_Slime/wiki/446-installation-guide/ ; https://developer.cloud.tencent.cn/article/2417775
3. BepInEx IL2CPP install (Bleeding Edge, win-x86/x64): https://docs.bepinex.dev/master/articles/user_guide/installation/unity_il2cpp.html
4. Unity Doorstop (Windows, Linux, macOS; Mono + IL2CPP): https://github.com/Apprentice9999/UnityDoorstop (fork; upstream is NeighTools/UnityDoorstop)
5. MelonLoader releases (v0.7.3 MacOS compile fix): https://github.com/LavaGang/MelonLoader/releases
6. MelonLoader on Thunderstore: https://thunderstore.io/c/cairn/p/LavaGang/MelonLoader/v/0.7.3/
7. Vortex deployment (hard links, same drive, Wine): https://forums.nexusmods.com/topic/10595318-vortex-mode-of-deployment ; https://github.com/360900/tinkergame/wiki/Vortex
8. DepotDownloader: https://github.com/SteamRE/DepotDownloader
9. Old builds with DepotDownloader (community guides): https://forums.crateentertainment.com/t/how-to-download-and-install-older-versions-of-gd-steam-only/111814 ; https://www.speedrun.com/wttg2/guides/yh27d
10. `download_depot` console method and report of its removal: https://www.speedrun.com/Solarpunk/guides/5icrh ; https://www.speedrun.com/posts/enin5
11. Tauri updater (signature required) and CVE-2026-95625: https://v2.tauri.app/plugin/updater/ ; https://www.sentinelone.com/vulnerability-database/cve-2026-95625/
12. GitHub releases limits: https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases
13. Apple Developer Program: https://developer.apple.com/programs/ ; notarization included: https://developer.apple.com/forums/thread/746992
14. Sequoia Gatekeeper change: https://9to5mac.com/2024/08/13/security-bite-apple-finally-making-it-harder-to-override-gatekeeper-is-a-telling-move/ ; https://appleinsider.com/inside/macos-sequoia/tips/whats-changed-in-runtime-protection-for-macos-sequoia
15. Windows code signing options (Artifact Signing price, eligibility, SmartScreen): https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
16. Smart App Control (no per-app override): https://textslashplain.com/2026/04/28/smart-app-control/
Local files read: PLAN.md, rig/make_mp_app.sh, rig/release_all.sh, rig/mp_launcher.c, rig/kit_README.txt, win/NOTES.md (sections 4, 12), win/WINDOWS_CROSSOVER_TEST.txt, ~/DungeonSettlersMods (CONTRACT.md, manager/README.md, datamod/README.md, loader/sdk/dsl_sdk.h, loader/src/dsl_builds.h, mod.json files), src/net/dsnet_internal.h, research/discord (the "one bad actor" messages), memory notes macos_il2cpp_patching, DungeonSettlers_GcdCdrPatch, DungeonSettlers_ModTools, DungeonSettlers_ModSet, DungeonSettlers_VercelPlan.
