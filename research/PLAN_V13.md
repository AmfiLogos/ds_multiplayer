# PLAN v13: Dungeon Settlers Modded, next release (2026-10-16)

Source of truth: research/owner_feedback_v12.md items 1-17 (owner's words rule), AFTER_MVP_LIST.md, research/MASTER_PLAN.md
(3 pieces: mod loader / multiplayer mod / custom modes mod), research/owner_feedback_2026-10-15.md. Start state: ds_multiplayer
main 0542d5d (M4 split merged: src/mp_api.h, custom/), ds_launcher main bbe7602 (l1-core, l2-modswin, l3-win, m4-busy merged).
Released: v12-mvp (GitHub, Latest). Rules for every lane: LANE_RULES.md. Lane briefs: research/briefs_v13/.
Marks: **[V]** = checked in files or by a test on 2026-10-16. **[U]** = not proven yet; the lane proves it first.

## 1. For the owner (plain summary)

| What you will see | Done when |
|---|---|
| Title screen: "Multiplayer", then "Mods" right below it (no hover card), then a new button "Custom Modes". | Mac + Windows gate item G8 green |
| All hover cards open to the RIGHT of the panel. They never cover the buttons below. | G8 |
| Multiplayer page: space between Your save / New save / Foreign Expedition / Back. Rules in their own panel, open (no extra click). | G2 |
| World lists: each world shows its mods on the right side of its row. Hover shows ALL its mods. | G8 |
| Mods change only at the title screen. Apply at the title is faster (target: under 2 s with the new set ready; today 7.2-7.6 s). | G8 (time printed) |
| No "App Running in Background" notice when the game starts. | G8 |
| Custom Modes > My Units, Unit Builder, Gallery, Test Fights (not in the Mods window). | G5, G6 |
| Bring Units (expedition and PvP): tabs "From a save" / "My Units", cards with icons, "New unit" opens the Unit Builder. | G4, G5 |
| Foreign Expedition = neutral hub (wiki base map, everything unlocked, stocked). Rules on the host page, Death permanent Off. | G4 |
| PvP v1: Multiplayer > Host a game > PvP: everyone appears on the starting map with their units, PvP is on. | G4 |
| Weapon-skill slot mod: only the host needs it. | G3 |
| Global cooldown mod and Hell difficulty work on Windows too. Every mod we ship lists Mac + Windows. | G9, G11 |
| MelonLoader mods load on Mac (Apple chips) and Windows through our loader (like BepInEx today). | G10, G11 |
| One GitHub release "v13" with the Mac zip and the Windows zip; the new app in your Applications folder. | section 6 |

## 2. Research answers (R1-R5)

**R1 PvP at the base: YES, units fight on the settlement map [V].** Test ext/plan13/pvp_base.py (pair AB, hidden, muted): seed save
MoonlightBrigade (5 units at Base, map 0 / region 0 / floor 0, no monsters). PvP SETUP + START without the arena step: hostile both
ways, units fight by themselves, attack orders go through the lockstep, a unit goes down, 6535 common ticks with equal fingerprints
on host and guest. Evidence ext/plan13/NOTES.md. Why it works: the PvP hooks (GetRelation 0x82BBA0, IsHostile 0xFA4CE8, the hostile
lists, CheckAggroTrigger 0xBCE72C) act on the current map's CombatDataHandler, whatever the map. Not exercised: round end / match end /
world restore at the base (the arena path is tested). So PvP v1 = START without an arena on a base map.

**R2 Native mods are Mac-only because nothing was built or mapped for Windows, not because of the loader [V].**
- The loader core is shared C and the Windows engine has hooks + in-place patches + tables (start/win/dsl_engine_win.c dsl_hook /
  dsl_patch; build_win.sh compiles every src/*.c). The multiplayer mod already runs on Windows through it (v12-mvp xplay 19/19).
- gcd-cdr: mod.json lists only `mac-arm64`, only `rva/25846389_mac.tsv`, built only by clang arm64 (loader/build.sh). Its 3 table
  entries map to Windows in win/win_map.tsv: GetComponent<object> 0x185FEDC -> 0x1221D80, slot GetComponent<StatusReader>
  0x6691B40 -> 0x4F35D98, codegen init 0x6FB6FC -> 0x443AF0; SetGlobalCooldown exists on Windows (0xAE56B0). Work: Windows table,
  llvm-mingw build to gcd-cdr.dll, `win-x64` in mod.json, inline audit of SetGlobalCooldown, one world test under wine. ~3-5 h.
- hell-difficulty: its 20 patches are arm64 instruction bytes (cmp/b.cond/cset), and the staff strip checks an arm64 return address
  (+0xA4). Its data half edits `StandaloneOSX/table_assets_all_*.bundle` + catalog.bin; the Windows copy has
  `StandaloneWindows64` (in the DSMP bottle). The Windows starter has no data-mod step (ds_modded_win.c). Work: re-derive the 20
  sites in the Windows GameAssembly.dll (x86-64), Windows return-address check, Windows data half, data-mod apply in the Windows
  starter. ~12-18 h. Note: Hell is not in the v12-mvp app at all (only multiplayer + gcd-cdr ship) [V].
- Custom Modes mod.json is also Mac-only; on Windows it is compiled into the multiplayer DLL (-DCM_SAME_IMAGE). Needs its own DLL.
- Rule from now: a gate script fails the release when a shipped mod.json lacks `win-x64` or `mac-arm64`.

**R3 MelonLoader: Windows not integrated, Mac blocked by the exact bug our Il2CppInterop fork already fixed [V].**
- Windows: MelonLoader 0.7.3 supports Unity 6 IL2CPP; 3 DS Nexus mods ship an ML build. Our loader hosts only BepInEx
  (dsl_bie*); no ML code. ML uses a `version.dll` proxy, ours `baselib.dll` (BepInEx `winhttp.dll` coexisted, v8_xplay 8/8). Not tested.
- Mac (2026-10-14 log research/loadertest.noindex/mac_E/MelonLoader/Latest.log): ML alpha-development (7c549cc, osx-arm64 build)
  loads mods, then the support module dies: "Required target method GenericMethod::GetMethod not found" (Il2CppInterop 1.5.1-ci.845).
  Our fork (ext/bepinexmac/src.noindex/il2cppinterop, 1.5.3) fixed exactly this on arm64 (NOTES row 11) plus 14 other arm64 faults.
- Route: pack our Il2CppInterop fork into a local NuGet feed, rebuild ML alpha with `Il2CppInteropVersion` = it, keep the 2 local ML
  fixes of 10-14 (4-byte export thunk), prove a test Melon mod (Harmony patch + injected MonoBehaviour) with dsload + multiplayer;
  then loader support for kind "melon" on both OS (pinned package, Mods window mark, per-world, modset lines). Estimate: Mac 1-2 days,
  Windows 0.5-1 day, loader 1 day. BepInEx and MelonLoader both host CoreCLR: one of them per mod set (default, logged).
  Owner #16: no upstream pull requests; Extended Hotbar keeps its own hash check (refuses on Mac: accepted).

**R4 One home: a title-screen button "Custom Modes" (shown when the Custom Modes mod is on), below Mods.** It opens a page in the
title column, the same pattern as Multiplayer > Host a game: My Units / Unit Builder / Gallery / Test Fights / Back. Each opens a
game-style window. The only other door: Bring Units (hub, PvP) has the tabs "From a save" / "My Units" and a "New unit" button that
opens the same Unit Builder; Save returns to Bring Units with the new unit ticked. Why: (1) the owner removed them from the Mods window,
and the title column is where the game puts every top-level choice; (2) the players already know the Multiplayer sub-page pattern, so no
new kind of menu; (3) all four are solo tools that must work without multiplayer, so they cannot live in the Multiplayer page.

**R5 Faster soft restart at the title [V timings, loader/NOTES.md].** Today title case = 7.2-7.6 s: B start -> MainFlow.Awake
0.46 s, -> title ready 6.1 s (the game's boot), then ~1.05 s settle wait before "ready" (camera read). Route: (1) title-only Apply
(owner #10): remove the world case from the Mods window, keep flow K (load a world with its mods from the title Load list); (2) drop the
1 s settle at the title (no world to restore); (3) measure B's boot by step (boot_measure.py + dsload timestamps) and cut what a hidden B
does not need [U which steps]; (4) prewarm: start B hidden with the new set as soon as the Mods window has unapplied changes (1.5 s after
the last change; a newer change restarts B), so Apply swaps in ~1 s. Cost: a second hidden game process while changes wait (memory,
CPU). Default ON; logged in MORNING_QUESTIONS.

## 3. Every owner item mapped (nothing dropped, nothing invented)

| # | Owner item (short) | Work package | How |
|---|---|---|---|
| v12-1 | Title "Mods": no hover card | LUI | dsl_reg.c title_add tip NULL |
| v12-2 | "Mods" below "Multiplayer" | LUI | title button order: right after the Multiplayer button |
| v12-3 | Hover cards outside the panel, right side | MPF (src/ui_tip.c, mpmenu), LUI (src/dsl_ui.c) | anchor = panel's right edge at the element's height |
| v12-4 | Multiplayer page buttons glued | MPF | mpmenu layout spacing |
| v12-5 | Rules outside, own panel, expanded | MPF (host Rules), FEP (Expedition / PvP rules on the host page) | own panel, open by default |
| v12-6 | Host a world "Mods:" hover shows ALL mods | LUI | dsl_rows.c |
| v12-7 | "App Running in Background" notice | LUI | root cause first (process flags / helper), then fix |
| v12-8 | FE card "into a copy of your world" | FEP (custom/cm_mp.c), MPF (fallback text src/mpmenu.c:3008) | text = neutral hub |
| v12-9 | Each world's mods on the RIGHT of its row | LUI | dsl_rows.c; MP tag stays left after the name |
| v12-10 | No mod restart in a world; Apply only at the title; faster | LUI | R5 |
| v12-11 | Our mods on Windows too | XM | R2 |
| v12-12 | MelonLoader in our loader, Mac + Windows | ML | R3 |
| v12-13 | PvP v1 on the starting map | FEP | R1: host choice "PvP", base map, START without arena |
| v12-14 | My Units / Builder / Gallery / Test Fights not in Mods window | UNI (home + My Units), FIG (Gallery, Test Fights), BLD (Builder) | R4 |
| v12-15 | Weapon slot host-only: do it | MPF | quick-slot orders by skill key + QuickSlots skip in the full-state check |
| v12-16 | No upstream PRs; Extended Hotbar keeps its check | rule in ML, XM briefs | no work |
| v12-17 | Serious plan, executable without him | this file + briefs + orchestrator13 | - |
| AM-1 | Tabs My Units / Gallery / Test Fights | replaced by v12-14 (title home) | UNI, FIG |
| AM-2 | Bring Units: My Units tab, Compact Plus cards, no overflow | UNI | one window for hub + PvP |
| AM-3 | FE neutral hub; rules on host page; Death Off; PvP from the hub. Later: a real player's world with OK | FEP | candidates listed only (MORNING_QUESTIONS) |
| AM-4 | Unit Builder in the game's menus | BLD | wiki builder.js core in QuickJS (FIG engine) + game-style UI |
| AM-5 | Weapon slot host-only | MPF | = v12-15 |
| AM-6 | M4 split (merged), busy flag (merged bbe7602), BepInEx bridge (branch, pre-split) | MPF (bridge port + merge) | |
| AM-7 | Not testable: real Windows PC, Steam on Windows, a friend over the internet | none (release note says so) | |
| AM-8 | Owner questions: upstream / Hotbar / extra checks / Teams | answered by v12-16; extra checks in gate G10-G11; Teams questions stay in MORNING_QUESTIONS | |

## 4. Work packages (one lane = one branch + one worktree; only the owner lane edits its files)

Repos: MP = ~/ds_multiplayer, LD = ~/ds_launcher.

| WP / lane | Owns (only these files) | Start point | Does | Est. |
|---|---|---|---|---|
| MPF `mpfix` | MP src/** (mpmenu*.c/h, mpmenu_kit.h, ui_tip.c, ui_game.c, ord_*.c/h, mp_game.c, rig_cmd.c, net/, tests under src/) | MP main | v12-3 (MP), 4, 5 (host Rules), 8 fallback text, 15; AM-6 bridge port onto the split | 20-26 h |
| UNI `units` | MP custom/cm_module.c, custom/build_module.sh, custom/home/** (new), custom/units/** | MP main + branch c1-units (rebase) | step 1 skeleton (build by folder lists, home button + page, weak entry points); My Units window; Bring Units window + Compact Plus cards; solo build (no multiplayer needed) | 30-40 h |
| FEP `fepvp` | MP custom/fe/**, custom/pvp/**, custom/cm_mp.c | MP main | neutral hub; Expedition Rules page; PvP v1 host choice on the base map; Bring Units from UNI; PvP from the hub; FE text | 24-32 h |
| FIG `fights` | MP custom/gallery/**, custom/fights/** | MP main + branch c4-fights (rebase) | step 1 merge the code part (engine, cache, gallery client); Test Fights + Gallery windows in the game; test worlds hidden from Load lists | 28-36 h |
| BLD `builder` (starts when FIG step 1 is merged) | MP custom/builder/** | MP main | Unit Builder window: wiki builder.js core through FIG's QuickJS engine, game-style UI, Save to My Units, "New unit" door | 25-40 h |
| LUI `loaderui` | LD loader/src/dsl_ui.c, dsl_rows.c, dsl_reg.c, dsl_world.c, dsl_soft.c, dsl_soft_os.h, loader/ui/**, loader/start/mac/dsl_soft_mac.c, dsl_start_mac.c, loader/start/win/dsl_soft_win.c, app/stub/**, core/src/bin/dsml_helper.rs, loader/test/soft/** | LD main | v12-1, 2, 3 (loader), 6, 7, 9, 10, R5 | 24-32 h |
| XM `crossmods` | LD loader/modules/** (+ new loader/modules/build_mods.sh), loader/start/win/ds_modded_win.c, loader/start/win/kit/**; MP win/** | LD main + MP main | R2: gcd-cdr + hell on Windows, Windows data-mod apply, Custom Modes as its own Windows DLL, Windows build of every new custom/ source, both-platform mod.json check | 28-36 h |
| ML `melon` | LD loader/build.sh, loader/start/win/build_win.sh, loader/src/dsl_main.c, dsl_internal.h, dsl_bie.h, new dsl_ml.h, loader/start/mac/dsl_bie_mac.c + new dsl_ml_mac.c, loader/start/win/dsl_bie_win.c, bie_win.c + new dsl_ml_win.c; MP ext/melon/** (notes, tools, forks *.noindex) | LD main | R3 | 24-32 h |
| GATE `gate13` (last) | MP rig/release_gate.sh, rig/release_all.sh, rig/gate13/** (new); LD app/build_app.sh, loader/mp_pin.txt, README.md | both mains after all merges | release gate section 5, ship section 6 | 6-10 h |
| `merger` | merge commits only | - | merges in the order below when orchestrator13 says | - |
| `orchestrator13` | ext/orchestrator/ORCHESTRATOR_LOG.md, MORNING_QUESTIONS.md (append), research/briefs_v13/** (new briefs) | - | loop, nudge, merge order, gate start | - |

Fixed interfaces (written in the briefs so lanes work in parallel; the owner lane implements, the others call):
- `custom/<folder>/sources.txt` (solo dylib, no multiplayer link) and `custom/<folder>/sources_mp.txt` (custom-modes-mp.dylib):
  one .c path per line. build_module.sh (UNI) reads them; no other lane edits build_module.sh. XM reads the same lists for Windows.
- Home entry points, called by cm_module.c (UNI) through weak symbols when present:
  `void cm_units_init(const DSLHost*, DSLPlugin*)`, `void cm_fights_init(...)`, `void cm_gallery_init(...)`,
  `void cm_builder_init(...)`; and `int cm_home_add(const char *id, const char *label, const char *tip, int order, void (*open)(void))`
  in custom/home/cm_home.h (orders: My Units 10, Unit Builder 20, Gallery 30, Test Fights 40).
- Builder door: `void cm_builder_open(const char *unit_id_or_NULL, void (*saved)(const char *unit_id, void *ud), void *ud)` (BLD).
  Until BLD merges, UNI shows "New unit" only when the symbol resolves.
- Bring Units: `custom/units/mu_bring.h` (UNI writes it on day 1 and puts the text in ext/units13/API.md):
  open(request: max units, sources allowed, title) -> done(picks: source save + guid, or My Units id). FEP calls it.
- My Units store: custom/units myunits.h (mu_load / mu_add_build / mu_team_*), decoder wk_link.h (the one rule for old links). FIG and
  BLD call these, never their own decoder.
- Shared lock for the one wine copy: `mkdir ~/ds_multiplayer/rig/wine.lock` (write your lane name into wine.lock/owner), `pgrep -fl wine`
  first, `rmdir` when done. Never two wine copies.

## 5. Order, merges, machine limits

Parallel at start (7 build lanes): MPF, UNI, FEP, FIG, LUI, XM, ML. BLD is the 8th: orchestrator13 asks team-lead to spawn it when FIG
step 1 is merged AND one of the 7 lanes is finished (keeps 7 at a time). GATE starts when everything is merged.
Test pairs: max 4 machine-wide (`rig.py lease <lane> --pid`), one wine copy (wine.lock). Loader test copies (LUI copy S, ML copy E)
take a lease for the time they run. Release gate: rig/release.lock, 1 pair + the wine copy.

Merge order, MP main (LANE_RULES rule 3, rig/src_merge.lock, `merger` does it on orchestrator13's word):
1. MPF (src/ base; bridge + weapon slot)  2. UNI step 1 (skeleton: build lists + home)  3. FIG step 1 (engine, cache, gallery client)
4. UNI full (My Units, Bring Units)  5. FEP  6. FIG full  7. BLD  8. XM win/ (after each custom merge XM re-runs the Windows build;
XM's own merge can go any time it is green; it must be green on the final main).
Merge order, LD main: LUI, XM (modules), ML in any order when green (disjoint files); ML after LUI if both touch the Mods window mark
(LUI draws the "MelonLoader" mark for mod kind "melon", ML provides the kind).
Dependencies: FEP needs UNI's Bring Units (works on its own parts first); FIG full + BLD need UNI's My Units store; BLD needs FIG's
engine; GATE needs all. A lane that is green before its turn waits; it never merges out of order unless its files are disjoint AND no
lane ahead depends on it (orchestrator13 decides).

## 6. One release gate (lane gate13; rig/release.lock)

Mac (built app "Dungeon Settlers Modded.app" from the pinned commit, test copy; pair for MP tests):
- G1 Unit suites: net, mp_test, ui_test, mpmenu_test, trn_test, teams_test, pvp_test, FE custom guard (18), custom/units mu_test (148+),
  custom/fights run_eq 64/64, sg_cache_test, gal_test, builder equality test, loader run_tests.sh, l1_test, l1_bie_test, uikit_test.
- G2 Rig: smoke 3/3, mp2 19 PASS 0 FAIL, left_prompt 16 PASS 0 FAIL, menu_pair (MPF updated), prefs identical before/after.
- G3 Weapon slot: host with the mod + stock guest, full-state check ON, 100 weapon casts + `--auto last --flip`: 0 desyncs.
- G4 Custom MP: FE dive in the neutral hub (one save unit + one My Units unit, Death Off: save unit result applies, My Units unit does not,
  library unchanged), PvP v1 on the base map (pvp_base checks 1-7 incl. round end), PvP m4_van / m4_bring / m4_ui, PvP from the hub.
- G5 My Units: long + short link one click, team, Bring Units both tabs, no text overflow (card test), "New unit" -> builder -> back ticked.
- G6 Test Fights: Boss Golem with a My Units team loads; Arena; DPS Check; Gallery save + test fight; test worlds not in Load lists.
- G7 Bridge: Mac host with BepInEx bridge mod + Windows guest: C#-handled action, 0 desync.
- G8 Loader UI + restart: title order (Multiplayer, Mods, Custom Modes), Mods without hover card, hover cards right of the panel (screenshot
  coordinates: card x >= panel right edge), world row mods on the right + hover lists all, Apply disabled in a world, title Apply time
  (prewarm and cold) printed, flow K, no "App Running in Background" (check method from LUI).
- G9 Our mods Mac: gcd_world_test, Hell (difficulty 5 present, 20/20 patches, staff strip on Hell only).
- G10 MelonLoader Mac: test Melon mod fires (Harmony + MonoBehaviour) next to dsload + multiplayer; BepInEx mods still load in a BepInEx set.
- G11 Windows (wine bottle DSMP, graphics/audio off): kit install.bat from a clean copy; Mods window; Custom Modes button; gcd-cdr effect
  log; Hell 20/20 + data half; test Melon mod on ML; BepInEx be.788 + bridge; Mac host + Windows guest mp2 19 PASS with gcd-cdr on both;
  Mac host + Windows guest PvP v1 on the base map (checks 1, 4, 7).
- G12 Public repos: diff grep for keys; zips without saves, game files, templates, /Users or user names; every shipped mod.json has
  mac-arm64 + win-x64.

Ship (as v12-mvp, PLAN.md "v12-mvp released"), only after G1-G12 green and team-lead says "ship":
tag `v13` on both repos (MP main merge commit, LD main), loader/mp_pin.txt = that MP commit, build the Mac app (app/build_app.sh, mods:
multiplayer, custom-modes, gcd-cdr, hell-difficulty) and the Windows kit (loader/start/win/build_win.sh with MODS=...), GitHub release
`v13` (Latest) with Dungeon-Settlers-Modded-Mac.zip + Dungeon-Settlers-Modded-Windows.zip + sha256 lines; owner app: the old
~/Applications/Dungeon Settlers Modded.app -> ~/Applications/old_versions/Dungeon Settlers Modded-<YYYY-MM-DD-HHMM>.app, new app
installed (not launched); PLAN.md section "v13 released"; one line to team-lead. Release note says: not tested on a real Windows PC, Steam
on Windows, or over the internet (AM-7).

## 7. Defaults taken (logged in MORNING_QUESTIONS.md by the lanes, owner can change later)
- Home button label "Custom Modes" (= the mod's name in the Mods window).
- PvP v1 map = the wiki base template (the day-0 starting base, same file the hub uses), teams placed at two sides of the base.
- Prewarm on for title Apply (second hidden process while changes wait).
- BepInEx and MelonLoader not in one mod set (both host .NET): the Mods window refuses the second one with a reason.
- Hell difficulty ships in both kits, off by default.
