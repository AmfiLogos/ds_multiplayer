# Master plan: three pieces, one release (2026-10-15, rewrite)

Structure approved by the owner 2026-10-15 ("that makes more sense"): MOD LOADER (the base) + MULTIPLAYER MOD (co-op only)
+ CUSTOM MODES MOD (units, builder, gallery, test fights, Foreign Expedition, PvP). ONE release with everything
("I want everything in one"). Previous version: research/old_versions/MASTER_PLAN-2026-10-15-1823.md.
Source of truth for features: research/owner_feedback_2026-10-15.md (every line mapped in section 6).
Plan only: no code changed. **[V]** = checked in our files today. **[U]** = not verified; the lane proves it first.
Rules kept: build only what he asked; no invented extras; no owner questions about internal choices; reviews flag only
production breaks or data loss.

## 1. TLDR

| Piece | What the player sees | Owner quote it answers | Cost | Risk |
|---|---|---|---|---|
| MOD LOADER (base) | One app / one Windows kit "Dungeon Settlers Modded" (Mud Slogel icon). Steam starts by itself; first start copies the game once; title screen gets "Mods". Mods window: mods on/off, groups, Apply = soft restart (loading screen, back where you were). Load / New Campaign screens show and set the mods per world. No game features. | "a mod loader, not a mod launcher ... like tModLoader"; "save these mods as group. I would want a tab just to control the mods"; "soft restart in the back ... put people back exactly where they were"; "No, I don't want a Windows version of the launcher" | Mac start: mostly done (modsmenu, softrestart title case **[V]**); Mods window 10-16 h; world case of soft restart 6-10 h; Windows start part 20-30 h | HIGH: soft restart in a world not proven; Windows start part not ported |
| MULTIPLAYER MOD (co-op only) | Multiplayer page: Host a game > Your save / New save / Foreign Expedition (the third only when Custom Modes is on). Lobby, sync, HUD panels, multiplayer tag, save lists split with toggles. Mac + Windows. | "Your save, new save or foreign expedition as options"; save-list and tag lines; the six HUD lines; "see them without any transparency"; "put it as a toggle" | Mostly done on branches (wp-menu, viewui, seethrough, latejoin, mptag); save-list filter 4-6 h; empty-base bug 2-4 h; weaponslot (running) | Low-medium |
| CUSTOM MODES MOD | Mods window tabs: My Units, Gallery, Test Fights, Unit Builder. Foreign Expedition neutral hub (rules on the host page, death Off), PvP (Esc menu, also in the hub), one Bring Units with Compact Plus cards, one-click wiki links incl. short links. Test Fights / Builder / Gallery also solo. Mac + Windows. | "I have no way to actually select them"; "check the folding"; "follow the way it's done inside of the wiki"; "integrate the build gallery"; "neutral hub ... everything unlocked"; "death is permanent should be off by default"; "Compact Plus view" | Split out of the MP src 1-1.5 days; units + cards 14-20 h; FE hub 8-12 h; Gallery + Test Fights 20-30 h; in-game builder 25-40 h | HIGH: builder size; split must keep the sim identical |
| ONE release | One GitHub release (Mac zip + Windows kit), one gate, everything green before it ships. | "I want everything in one" | gate ~3 h | Windows real-PC never tested **[V: win/NOTES.md]** |

## 2. The three pieces

```
                ┌──────────────────────────────────────────────────────────────────┐
                │ MOD LOADER  (repo ~/ds_launcher, public, MIT)                    │
  Mac start ─►  │  start/mac: app stub + dsml-helper (first-start copy, Steam      │  ◄─ Windows start
  (app stub)    │   start, active.json)    start/win: baselib.dll proxy (MinHook   │     (DLL proxy)
                │   engine host, copy via install.bat, CreateProcess restart)      │
                │  core (shared C, both OS): module loading, profiles, groups,     │
                │   per-world mods, soft restart, Mods window + Mods tab           │
                │  SDK dsl_sdk.h API 3: hooks, TAB API, LOAD-ROW API, TITLE API,   │
                │   dsl_ui widget kit (game-style buttons, toggles, text fields,   │
                │   game tooltips, notice box)                                     │
                └───────────────▲───────────────────────────────▲──────────────────┘
                                │ dsl_sdk.h                     │ dsl_sdk.h (tabs, ui kit)
                ┌───────────────┴──────────────┐   MP API   ┌───┴──────────────────────────────┐
                │ MULTIPLAYER MOD              │ ◄───────── │ CUSTOM MODES MOD                 │
                │ ds_multiplayer/src           │ (mp_api.h, │ ds_multiplayer/custom            │
                │ lockstep, net, lobby, host   │  optional) │ units/ (library, links, cards,   │
                │ page, HUD, tag, save filter  │            │  Bring Units), builder/,         │
                │ id "multiplayer", gameplay   │            │ gallery/, fights/, fe/, pvp/     │
                └──────────────────────────────┘            │ id "custom-modes", gameplay      │
                                                            └──────────────────────────────────┘
```

Why these lines:
1. The loader knows nothing about the game's features. It starts the game, loads mods, shows the Mods window and restarts.
   Only the start part differs per OS; the Mods window is shared C built for both, through the same win/ port tools the
   multiplayer mod already uses **[V: win/build_win.sh, gen_port]**.
2. Multiplayer never calls Custom Modes code. Custom Modes registers callbacks in the multiplayer mod (MP API). So
   multiplayer works alone, and Custom Modes works solo when multiplayer is off (Test Fights, Builder, Gallery, My Units).
3. Custom units live in Custom Modes only. Library file `Mods/units/library.json` in the game's data folder, never in a
   save: reusable in many hubs, never deleted after a run, never written into a regular world. One builder, two doors:
   the Unit Builder tab, and Bring Units > My Units > "New unit" (owner asked for unit creation inside multiplayer).

### 2.1 Where the code lives

| Piece | Repo / folder | Builds to | Today |
|---|---|---|---|
| Loader core + Mods window | ~/ds_launcher/loader/src, loader/ui (new) | Mac: dsload.dylib. Windows: inside the baselib.dll proxy | dsload exists **[V]**; Mods window only an icon (`modules/modmenu/slogel.png`) |
| Loader Mac start | ~/ds_launcher/app (stub, build), core/src/bin/dsml_helper.rs | "Dungeon Settlers Modded.app" | modsmenu branch **[V]** |
| Loader Windows start | ~/ds_launcher/loader/start/win (new) | baselib.dll proxy + install.bat kit | MP-only proxy in ds_multiplayer/win/port (rig_main_win.c, MinHook) **[V]**; winkit bat files **[V]** |
| SDK | ~/ds_launcher/loader/sdk (dsl_sdk.h API 3, dsl_ui.c) | header + static lib per OS | API 2 **[V]** |
| Multiplayer mod | ~/ds_multiplayer/src (minus fe/, pvp_*, units) | mods/multiplayer/multiplayer.dylib; Windows multiplayer.dll | today one dylib dsrig.dylib with FE + PvP inside **[V]**; loader builds it from a hand-synced `vendor/mp` snapshot **[V: loader/build.sh]** |
| Custom Modes mod | ~/ds_multiplayer/custom (new): units/, builder/ (+ the Rust builder crate moved from ~/ds_launcher/builder), gallery/, fights/ (+ QuickJS), fe/, pvp/ | mods/custom-modes/custom-modes.dylib; Windows .dll | code today in src/fe, src/pvp_*, src/myunits.c / wk_* (wp-units), ~/ds_launcher/builder |
| Port tools | ~/ds_multiplayer/win (gen_port, inline audit, ranges) | used by all three Windows builds | **[V]** |
| Retired (moved to old/, not deleted) | ~/ds_launcher src-tauri, ui/, ownerapp, winlauncher (game-copy + Steam code kept as reference); ~/ds_multiplayer rig/make_mp_app.sh "Dungeon Settlers MP.app" | - | - |

Release builds: the loader's build pins one ds_multiplayer commit (the release tag) and builds both mods from it (no more
vendor snapshot), so the gate tests the code that ships.

### 2.2 APIs between the pieces

**A. Loader SDK (dsl_sdk.h API 3; API 1-2 plugins keep loading)**
- Existing: `hook_rva`, `table`, `module_dir`, `module_loaded(id)` **[V]**.
- TAB API: `dsl_menu_add_tab(self, id, title, order, &cb)`; `cb.open(root, w, h)`, `cb.update()`, `cb.close()`,
  `cb.can_close()`. The loader draws the window, title (Mud Slogel) and tab strip; the mod fills `root` with its widgets.
  Mods tab = the loader's own tab, always first.
- LOAD-ROW API: the loader owns the hooks on the game's load-list rows (`SubUI_SettlerNameCase.Init`,
  `SubUI_SaveFileButton.SetData` **[V: used by mptag]**) and its New Campaign screen. Mods add text through
  `dsl_loadrow_add(self, cb(guid, out_text))` and a row filter `dsl_loadlist_filter(self, cb(guid) -> show)` plus one
  toggle label. Reason: two modules cannot both own one hook site **[U: engine registry is "one owner per site"; chaining
  not tested]**. The loader uses it itself for "Mods: <names> [Change]"; multiplayer for the tag and the save-list filter.
- TITLE API: `dsl_title_add_button(self, label, order, click_cb, tip)`. Loader adds "Mods". Multiplayer keeps its own
  title clones this release (working code, not rewritten); the loader places "Mods" below the existing column.
- `dsl_ui` kit: game-style button, toggle row, value row ‹ ›, text field, game tooltip, notice box, scroll list, copied once
  from mpmenu.c helpers into the SDK (multiplayer keeps its internal copy; Custom Modes and the loader use the kit).
- Soft restart: `dsl_soft_restart(profile, place)` for the Mods tab and the world mod check; not callable inside a
  running multiplayer session (`dsl_session_busy` set by multiplayer).

**B. Multiplayer API (src/mp_api.h; `mp_api_get(version)` returns a table of function pointers; Custom Modes finds it with
`module_loaded("multiplayer")` + dlsym / GetProcAddress; absent = FE and PvP entries hidden, solo tabs still work)**

| Need (where it sits today) | API call |
|---|---|
| Session facts | `role()`, `is_host()`, `my_uid()`, `players(out)`, `present_mask()`, `session_busy()` |
| Host page choice "Foreign Expedition" (mpmenu MPB_HOST_FE, uifix11 **[V]**) | `register_host_choice(id, label, tip, draw_page(root), on_start())`. mpmenu shows the third choice ONLY when one is registered; the Expedition Rules rows are drawn by Custom Modes on that page. |
| Esc menu "PvP Match" (pvp_ui.inc **[V]**) | `register_esc_button(label, tip, visible_cb, click_cb)` |
| Mail outside the sim (BLOB_MAIL kinds: FE 1-5, PvP 11-16 **[V: PLAN.md PvP merge]**) | `mail_send(kind, to_uid, blob, len)`, `register_mail(kind_lo, kind_hi, cb)`; one reader in multiplayer dispatches |
| Sim command through lockstep (DSNET_FE_START 21, pvp orders **[V]**) | `register_sim_cmd(kind, apply_cb)`, `send_sim_cmd(kind, payload)` |
| Every sim step + fingerprint (PvP tick, fingerprint mix in mp_game **[V]**) | `register_tick(cb)`, `register_fingerprint(cb)` |
| Join-point save extras (PVPB block, FE hub build, round snapshot override **[V]**) | `register_joinpoint(tag, write_cb, read_cb)`, `register_world_build(cb)` |
| Order filter (recruit refused in a hub, ord_mp **[V]**) | `register_order_filter(cb)` |
| UI flags (popup 8 guard, Guild Contact hidden, CTF flag tag in ui_game; no portal in an arena in ui_teams **[V]**) | `ui_set_flag(MPUI_HIDE_GUILD_CONTACT, on)`, `register_unit_tag(cb)`, `register_teams_portal(cb)` |
| Mod-set lines for PvP "Vanilla check" (pvp_modcheck **[V]**) | `modset_lines(out)` |
| Rig test commands fe_* / pvp_* (rig_cmd **[V]**) | `register_rig_cmd(prefix, cb)` (tests keep their names) |

Mod scopes: both mods are GAMEPLAY (HELLO mod-set check requires the same set on every player **[V: core next merge]**).
A guest without Custom Modes gets the existing refusal that names the missing mod.

## 3. Menu map (every owner item has one home)

```
TITLE SCREEN (stock buttons kept)
├─ Continue / New Campaign / Load ... stock. Load list: single-player worlds only, toggle "Show multiplayer worlds" [MP];
│                                      "Multiplayer with Ben, Djigui" tag + Remove tag [MP]; "Mods: <names> [Change]" [LOADER].
│                                      World with other mods: "Load it with them?" -> soft restart into it [LOADER].
│                                      New Campaign: "Mods for this world: <set> [Change]" [LOADER].
├─ Multiplayer (no hover card) [MP]
│   ├─ Host a game
│   │   ├─ Your save ........ host page -> multiplayer worlds only, toggle "Show single-player worlds" -> lobby
│   │   ├─ New save ......... host page -> stock New Campaign -> lobby
│   │   └─ Foreign Expedition [shown when CUSTOM MODES is on] host page with Expedition Rules (Death permanent Off,
│   │                         XP counts, Dungeon loot comes back, Units each, Recruiting) -> Start -> neutral hub
│   ├─ Join a friend on Steam / Join by address / Recent Worlds (Reconnect, Watch) ... as today
│   └─ Back
├─ Mods [LOADER window]
│   ├─ Mods ........ switches, "Save these mods as a group", group chips, Apply (soft restart) [LOADER]
│   ├─ My Units .... units + teams, "Add from wiki link" (one click), New unit, Edit, Delete [CUSTOM]
│   ├─ Gallery ..... wiki public builds -> Save to My Units / Test Fight [CUSTOM]
│   ├─ Test Fights . units (My Units / team / Gallery) + Boss / Arena / DPS Check / Finished Base -> Start [CUSTOM]
│   └─ Unit Builder  race, background, traits, level, skills, gear, live sheet -> Save to My Units [CUSTOM]
└─ Settings / Quit ... stock

IN A MULTIPLAYER WORLD [MP unless marked]
├─ Players panel (fold to header, drag) + cog: see-through incl. Off, Teams button Show/Hide, Fast join, tooltips, mini overview
├─ Teams button + window (Hide = fold to a bar); friends' camera marks, "<name> sees this too", pings, chat
├─ Esc menu: PvP Match [CUSTOM via MP API; every MP world incl. the hub], Leave multiplayer
└─ Hub only [CUSTOM]: Bring Units (tabs From a save / My Units; "New unit" -> Unit Builder), End expedition -> Results
```

Removed homes: Tauri launcher (Play, world list, its builder, Paste dialog); "Dungeon Settlers MP.app"; the host page
"World ‹ ›" row; the separate PvP Bring Units window (one Bring Units for hub and PvP).

## 4. Flows

**A. First start.** Mac: download the zip (GitHub or the wiki download page), drag to Applications, one Terminal line from
the README (quarantine; without an Apple Developer ID macOS 27 moves the app to the Trash **[V: modsmenu NOTES]**),
double-click. Windows: unzip, run install.bat (copies the game folder once, puts our baselib.dll in the copy, makes a
"Dungeon Settlers Modded" shortcut), double-click the shortcut. Both: Steam not running -> started in the background and
waited for; first start copies the game (Mac APFS clone 7 s **[V]**; Windows full copy **[U: time]**) and the saves once.
Steam's install is never changed. Title screen with Multiplayer and Mods.

**B. Host your save.** Multiplayer > Your save > name, cog Rules > load list (multiplayer worlds; toggle) > Load > lobby >
Steam invite or address. A friend joins -> the world gets the tag with their name.

**C. Host a new save.** Multiplayer > New save > name, cog Rules > stock New Campaign > lobby.

**D. Foreign Expedition.** 1. Multiplayer > Foreign Expedition: Expedition Rules on the page, each with a game tooltip;
Death permanent Off by default. 2. Start: the host builds the neutral hub (template world, then kitted: all research
done, all regions open, storage stocked; no kit code exists today **[V: grep fe/]**, new in WP-C2) and opens the lobby.
3. Each player: Bring Units: "From a save" or "My Units", or "New unit". Compact Plus cards. Units are copies.
4. Dive together; PvP Match from the Esc menu between dives. 5. End expedition -> Results: units from a save can go to
"Apply to a new save" per the rules (original save never changed); My Units units bring nothing home and stay unchanged.

**E. PvP.** Any multiplayer world (hub too): Esc > PvP Match > mode, teams, Vanilla check > Bring Units (same window) or the
world's units > Start Match > the world comes back as it was.

**F. My Units.** Make: Unit Builder > Save to My Units; or "Add from wiki link" (reads the clipboard; a box only when the
clipboard has no wiki link); or Gallery > Save to My Units. Keep: list of units and teams with cards; tick > "Save as team".
Bring: hub / PvP Bring Units > My Units; Test Fights. Reuse: unchanged after every run. Never written into a regular world
(guard in every save writer: myunits work, 18 checks **[V]**).

**G. Wiki links.** Long `?team=` / `#team=`, one-character `#b=`, bare code, short `/t/<id>` and `/build/<id>` (any language
prefix). Short = one HTTPS GET to dungeonsettlers.wiki only, redirect or meta refresh read, 5 s. Built with the builder's
folded values; checked against the builder on long AND short links.

**H. Gallery.** Teams or Units, Popular / Recent, filters > card > Save to My Units / Test Fight. Read-only. Other game
build: shown, refused on use.

**I. Test Fights.** Units (My Units, team, Gallery) > Boss (Golem / Sentinel / Abyss), Arena (pick monsters), DPS Check,
Finished Base > difficulty, supplies, kits (wiki defaults) > Start > loads into it (wiki's own savegen.js in QuickJS
**[U]**; templates downloaded from the wiki and cached, never committed). Test worlds never show in the Load list.

**J. Apply mods.** Mods tab > change > Apply > in a world the game's own save first > the dungeon loading screen > hidden new
process with the new mods skips logos, opens the same place (title page, or same world, camera, zoom, speed) > swap.
Fail or 90 s: old mods stay, reason shown. Not inside a running multiplayer session.

**K. World with other mods.** "This world uses: <names>. Load it with them?" > flow J into that world.

## 5. Work packages

### 5.1 Existing work: where it goes

| Branch / worktree | State | Goes to |
|---|---|---|
| sockpath, steamstart, winkit | merged into main (54e5a2a) **[V]** | MP (steamstart's stub code also feeds the loader's Mac stub, already copied in modsmenu **[V]**) |
| seethrough, latejoin, viewui, myunits FE guard | merger lane is merging them now (P0) | MP; the guard moves to custom/fe in the split |
| weaponslot (../ds_mp_weaponslot) | running: skip QuickSlots in the full-state check + free-slot-4 auto-use test (owner approved host-only) | MP (mp_game.c); merges before the split |
| wp-menu (../ds_mp_wpmenu) = uifix11 + mptag | host choices + World row removed + tag **[V]**; save-list filter not built yet | MP; the filter moves to the LOAD-ROW API once the loader has it (until then its own hook, as mptag does) |
| wp-units (../ds_mp_wpunits) = myunits | library, decoder + short links, guard, My Units tab; uncommitted edits in myunits.c / pvp_wiki.* | CUSTOM (custom/units). Until the split lands it edits only myunits.c, wk_*.c, pvp_wiki.* (pure code, no menu files) |
| fehub | holds nothing beyond the uifix11 commit **[V]** | dropped |
| ds_launcher modsmenu + softrestart worktree | Mac app stub, helper, icon; soft restart title case PASS **[V]** | LOADER |
| ds_launcher myunits (Rust short links, shared library path) | reference | CUSTOM (logic ported in C in wp-units) |
| ds_launcher builder/ (Rust, 114 377 numbers equal to the wiki **[V: plan_modloader]**) | - | moved to ds_multiplayer/custom/builder (git history kept by a subtree move) |
| ds_launcher ownerapp, winlauncher, src-tauri, ui/ | not shipped | old/ (winlauncher's Windows copy + Steam detection = reference for L3) |
| bepinex-bridge | not in the owner's list | parked, not in the release |
| loader vendor/mp snapshot | hand-synced copy | replaced by the pinned-commit build |

### 5.2 Packages by file ownership (one lane = one branch + one worktree; only the owner lane edits its files)

| WP | Piece | Owns | Starts from | Does |
|---|---|---|---|---|
| P0 merger (running) | MP | main | main | seethrough, latejoin, viewui, myunits guard |
| M1 weaponslot (running) | MP | mp_game.c full_sections | its worktree | QuickSlots skip + test |
| M2 menu | MP | mpmenu.c, mpmenu_model.c, mpmenu.h, mpmenu_test | wp-menu | save-list filter + toggles; tag; three host choices (FE choice behind the registration added in M4) |
| M3 HUD | MP | ui_game.c, ui_model.*, ui_teams.c, ui_test | main after P0 | empty-base Players panel bug; Players fold check |
| M4 SPLIT | MP + CUSTOM | mp_api.h (new), mp_core.c, mp_game.c, net/ mail, ord_mp.c, rig_cmd.c, ui_game.c / ui_teams.c hook points, mpmenu.c includes, build.sh; `git mv` src/fe, src/pvp_*, units -> custom/ | main after M1, M2, M3 merge | MP API (2.2 B); Custom Modes builds as its own dylib; behaviour unchanged (same gate numbers as v11: mp2 19, left_prompt 16, FE dive 23, PvP m_van2 / m_bring / m_ui) |
| L1 loader core + Mac start | LOADER | loader/src, loader/sdk, app/, core/src/bin | modsmenu + softrestart worktree | SDK API 3 (tab, load-row, title, dsl_ui kit), soft restart world case, per-world mods + flow K, pinned-commit build of both mods |
| L2 Mods window | LOADER | loader/ui (new) | after L1's SDK header is fixed (first day) | window, Mods tab, groups, Apply, Mud Slogel title |
| L3 Windows | LOADER + all Windows builds | loader/start/win, ~/ds_multiplayer/win | winkit + win/port/rig_main_win.c | proxy hosts the loader core (MinHook engine behind dsl_sdk), loads multiplayer.dll and custom-modes.dll, install.bat first-start copy, Steam start, save-folder redirect **[U]**, soft restart via CreateProcess **[U]** |
| C1 units | CUSTOM | custom/units | wp-units (rebased after M4) | library, links, Compact Plus card (avatar **[U]**, worn items with icons, skill bar, branches, passive, inscriptions), one Bring Units for hub + PvP, My Units tab |
| C2 FE hub | CUSTOM | custom/fe | main after M4 | neutral hub from template + kit, rules page through register_host_choice, death Off default |
| C3 PvP | CUSTOM | custom/pvp | main after M4 | Esc button through the API, Bring from C1, PvP in the hub |
| C4 Gallery + Test Fights | CUSTOM | custom/gallery, custom/fights, vendor QuickJS | main after M4 (code part can start before, no game) | flows H, I |
| C5 Unit Builder | CUSTOM | custom/builder (+ Rust crate) | main after M4 + L2 tab API | in-game builder, Save to My Units, "New unit" door |
| D1 download page | - | ~/dungeonsettlersdb page | its main | links to the GitHub release (gates green = push live) |

### 5.3 Order and parallel lanes (max 4 test pairs, one wine copy)

1. Now: P0 merger, M1 weaponslot, M2 menu, M3 HUD (each 1 pair, short), L1 + L2 loader (loader test copy S: counts as a pair
   while it runs, so it plays when one is free), L3 Windows (the wine copy), C1 pure code (no pair), C4 code part (no pair).
2. Merge into ds_multiplayer main: P0 -> M1 -> M3 -> M2.
3. M4 SPLIT runs alone on the MP shared files (no other lane edits them meanwhile). C1 rebases on it (renames follow with git).
4. Then in parallel by folder: C1, C2, C3, C4, C5 (5 lanes, 4 pairs: C4 code and C5 UI work share timing), L1-L3 continue.
   Merge order: C1 -> C2 -> C3 -> C4 -> C5. Loader lanes merge in ds_launcher whenever green; L3 integrates last
   (it builds all three for Windows).
5. Release gate (5.4).

### 5.4 One release gate (takes rig/release.lock, 1 pair + the wine copy)

Ships everything as ONE GitHub release: "Dungeon Settlers Modded" Mac zip + Windows kit, tag `lan_v12`, exact "what is in
it" list checked against the build. Nothing ships until all is green.
1. release_gate.sh on the frozen src through the LOADER (both mods loaded by dsload / the proxy), fail-fast; Mac 31 + Windows 8.
2. Unit: net, mp_test, ui_test, mpmenu_test, pvp_test, trn_test, fe_cli custom guard (18), mu_test, link decoder (500 builder
   links + short + one-character), gallery parser, loader tests (SDK API 1-3 modules load), app/test/run.sh.
3. Pairs: host your save; host new save; FE dive (one save unit + one My Units unit, death Off, apply: save unit comes home,
   custom unit does not, library unchanged); PvP from the hub; load-list filter + tag + Remove tag; viewui, seethrough,
   fastjoin pairs; Players panel with 0 units at base; weaponslot host-only (QuickSlots skip, slot-4 auto-use); guest without
   Custom Modes refused with the mod named; multiplayer OFF: solo tabs work, FE choice and PvP button absent.
4. Stats: builder vs game on long and short links (ext/pvp/pvp_wiki_units.py).
5. Test Fights: Boss Golem with 3 My Units units; Arena; DPS Check; units equal the builder sheet. Unit Builder: a built unit
   equals the wiki builder for the same choices.
6. Loader: soft restart title + world (never visible, same save, camera within one tile, speed, time recorded); flow K;
   per-world mods; groups; first start from a clean folder; Steam not running at start.
7. Windows: install.bat kit on the wine copy from a clean copy; Mods window; soft restart; Mac host + Windows guest xplay.
8. Public repos: diff grep for keys; no saves, templates, game files or binaries committed.

## 6. Checklist: every line of owner_feedback_2026-10-15.md

| # | Owner line (short) | Home | Status |
|---|---|---|---|
| 1 | Full plan, then build, reorganize | this file | plan |
| 2 | Only what he asked; no invented extras; no internal questions | rules at top; 5.1 parked | rule |
| 3 | Reviews: only production breaks / data loss | rules at top | rule |
| 4 | Mod loader like tModLoader | LOADER, L1-L3 | build |
| 5 | Launcher "play this world" useless, world list useless, UI bad | launcher retired; worlds load in the game's own Load list (flow K) | build |
| 6 | Keep mods + groups tab, Unit Builder, My Units | Mods window: Mods tab (LOADER), My Units + Unit Builder (CUSTOM) | build |
| 7 | No Windows launcher; Windows needs the mod loader | L3 Windows start part only | build |
| 8 | Soft restart, back where you were, cut the fluff | flow J, L1 | build |
| 9 | Logo Mud Slogel idle | icon done **[V]**; Mods window title (L2) | done/build |
| 10 | Key: don't care; mod list open | no work | no work |
| 11 | Saved units cannot be selected for PvP / expedition | C1 Bring Units My Units tab | build |
| 12 | Wiki link: click, clipboard | flow F, C1 | build |
| 13 | Short links + folding | flow G, C1 | build |
| 14 | Does not understand My Units | flow F; one home (Mods > My Units) + Bring tab | build |
| 15 | Custom units never in regular games; saved in game; reuse in hubs; not deleted, not saved | section 2 line 3; guard | build |
| 16 | Custom / boss fights like the wiki, in the game | flow I, C4 | build |
| 17 | Gallery in the mod loader; team / units / gallery choice | Mods window Gallery tab (CUSTOM through the TAB API), C4 | build |
| 18 | Builder placement: think | section 2 line 3; C5 | build |
| 19 | Host page title glitched; Multiplayer hover card | World row removed; hover card off **[V: wp-menu]** | done |
| 20 | Your save / New save / Foreign Expedition | M2 (+ register_host_choice from M4) | done/build |
| 21 | Save lists split with toggles | M2 | build |
| 22 | Multiplayer tag, removable, names | mptag in wp-menu **[V]** | done |
| 23 | App starts Steam | steamstart merged **[V]**; loader stub L1 | done |
| 24 | Install needs Terminal (Mac); Windows install.bat | Mac: the Terminal line stays; it cannot go without an Apple Developer ID (owner: no membership now, 2026-10-14). Windows: install.bat (winkit merged **[V]**, extended in L3) | cannot (Mac) / build (Win) |
| 25 | FE = neutral hub, everything unlocked; no save/new question | flow D, C2 + M2 | build |
| 26 | Bring Units overflow, no icons | C1 card | build |
| 27 | Hub rules not choosable; death Off default; PvP from hub | flow D step 1 (C2 page), flow E (C3) | build |
| 28 | Compact Plus view | C1 card | build |
| 29 | Could not test PvP / hub (v10) | in v11 **[V]** | done |
| 30 | FE fully kitted hub, units copied | flow D | build |
| 31 | Follow: friend's frame annoying | viewui (P0) | done, merging |
| 32 | Same view shown | viewui "sees this too" (P0) | done, merging |
| 33 | Setting to hide Teams | viewui cog "Teams button" (P0) | done, merging |
| 34 | Teams hides Players bar | viewui (P0) | done, merging |
| 35 | Collapse Players and Teams | Teams fold (viewui); Players fold since v9, M3 checks | done, check |
| 36 | No units at base: Players window bugs out | M3 | build |
| 37 | See-through Off | seethrough (P0) | done, merging |
| 38 | Weapon slot host-only is fine | M1: QuickSlots skip + slot-4 auto-use test (running) | build |
| 39 | Fast join toggle | latejoin (P0), default Off | done, merging |
| 40 | Players never on different floors | no work | no work |
| 41 | GitHub public MIT; download page | repos public + MIT **[V]**; D1 | build |
| 42 | Keep finished branches | 5.1 | plan |

## 7. Owner question (only one: his words leave it open and he would see the difference)

- **Q1. Neutral hub map.** He said "a basic map ... everything unlocked" and, before, "a fully kitted hub world".
  Default: the wiki's "Finished Base" template world (a built base; the same template Test Fights uses), then all research
  done, all regions open, storage stocked. Option: an empty base map with everything unlocked but nothing built.

## Hub map (owner 2026-10-15)
- Owner: "A: the wiki's Finished Base" + "ideally I'd want a world download of someone else to make sure we have something looking nice". -> C2: the hub is a nice-looking, fully built and stocked world made by a real player, downloaded at first use and cached (never shipped in the public repo). Default until such a world is chosen: the wiki Finished Base template. C2 finds candidates (whose world, where it comes from, screenshot) and the lead shows them to the owner; a real player's world needs that player's OK before it is used.

## BepInEx (owner 2026-10-15: "the mod loader will be compatible with the other format of mods ... right?")
- Windows: the loader hosts BepInEx (be.788, pinned download on first use); BepInEx plugins show in the Mods window (on/off, groups, per world). Lanes l3win + l2modswin. bepinex-bridge is UNPARKED and in the release (sync of bridged actions).
- Mac: not possible today (no arm64 BepInEx/MelonLoader for IL2CPP, research/bepinex_compat.md). Told the owner.
