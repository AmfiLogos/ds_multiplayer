# Master plan: Dungeon Settlers Modded (mod loader + multiplayer + units) - 2026-10-15

Source of truth: `research/owner_feedback_2026-10-15.md` (owner's words, every line mapped in section 4).
Also read for this plan: PLAN.md, research/plan_modloader.md, plan_foreign_expedition_spectator.md, plan_pvp.md,
LANE_RULES.md, HANDOFF_2026-10-15.md, the two owner screenshots, every branch (`git log main..<b> --stat`) and the
stopped worktrees. Plan only: no code was changed.

Marks: **[V]** checked in our files today. **[U]** not verified, the lane proves it first.

Rules this plan keeps (owner 2026-10-15): build only what he asked; no invented extras; no owner questions about internal
choices; reviews flag only what breaks in production or loses data ("found nothing" is fine).

## 1. TLDR

| Area | What the player sees | Owner quote it answers | Cost | Risk |
|---|---|---|---|---|
| One app, mod loader in the game | ONE app "Dungeon Settlers Modded" (Mud Slogel icon). Double-click: Steam starts by itself, the game title screen comes up with "Multiplayer" and "Mods". No launcher window. The "Dungeon Settlers MP" app and the Tauri launcher retire. | "a mod loader, not a mod launcher ... like tModLoader that boots the game and then inside of the booter you can do things" | Stub + helper exist (modsmenu branch); multiplayer module build change: S | Medium: Gatekeeper on a downloaded app (Terminal step stays) |
| Mods window | Title "Mods" opens one window, tabs: Mods / My Units / Gallery / Test Fights / Unit Builder. Mods tab: switches, "Save these mods as a group", group chips, Apply. | "the mods is fine and save these mods as group. I would want a tab just to control the mods. Unit builder is fine, my units" | 10-16 h | Medium: title column already holds our Multiplayer clones |
| Apply = soft restart | The game's dungeon loading screen, then you are back on the same screen (same world, camera, speed). No visible relaunch. | "soft restart in the back ... put people back exactly where they were ... cut the fluff" | 10-16 h (title case proven: 15.5 s, hidden **[V]**) | HIGH: in-world case not proven yet; 15 s is long |
| Multiplayer page | "Host a game" with three choices written right below: Your save / New save / Foreign Expedition. Join by Steam / address, Recent Worlds as today. No hover card on the title "Multiplayer". | "should have written right below it Your save, new save or foreign expedition"; "shouldn't have a hover card"; "The title is glitched" | 2-4 h (half done in uifix11) | Low |
| Save lists | Single player Load: only single-player worlds, toggle "Show multiplayer worlds". Multiplayer "Your save": only multiplayer worlds, toggle "Show single-player worlds". Tag "Multiplayer with Ben, Djigui" next to the name; "Remove tag". | "by default only show the worlds that are multiplayer, and a toggle..."; "add a tag multiplayer ... you could deactivate it" | 4-6 h (tag done in mptag) | Low |
| Foreign Expedition = neutral hub | Host picks Foreign Expedition, sets the rules on the same page (Death permanent OFF by default, XP, loot, units each), Start. Everyone loads a fully kitted base map with everything unlocked. No "host from a save / new campaign" question. | "some kind of a neutral hub ... a basic map. We have everything unlocked ... right now it still asks me host from a save or host a new campaign"; "Death is permanent should be off by default" | 8-12 h | Medium: hub source = template world |
| PvP from the hub | "PvP Match" in the Esc menu of any multiplayer world, the hub included. | "and then what if I wanted to do PvP" | 2-4 h (test + fixes) | Low-medium |
| My Units + Bring Units | Units kept in My Units (game data folder, never in a save). One Bring Units window (hub AND PvP): tabs "From a save" / "My Units". Unit cards in the wiki's Compact Plus style: avatar, worn items with icons, skill bar, branches, passive, inscriptions. No text overflow. | "I have no way to actually select them"; "I just don't understand how the my units thing is supposed to work"; "no icons for the units"; "Compact Plus view" | 14-20 h (library, guard, tab done in myunits worktree) | Medium: avatar of a unit that is not in the world **[U]** |
| Wiki links | "Add from wiki link" reads the clipboard in one click. Long links, short links (/t/abc123, /build/name) and one-character links all work; numbers equal the wiki builder (folded passives). | "I should just click and it should open the thing with the clipboard"; "it doesn't work with the short links ... check the folding" | 4-6 h (resolver written in myunits worktree) | Low: needs internet for short links |
| Gallery + Test Fights | Gallery tab: browse the wiki's public builds (teams or units), "Save to My Units", "Test Fight". Test Fights tab: pick your units, a team or a gallery build, pick Boss / Arena / DPS Check / Finished Base like the wiki, Start: the game loads straight into the fight. No save download. | "custom fights and the boss fights should just follow the way it's done inside of the wiki"; "integrate the build gallery inside of the mod loader" | 20-30 h | Medium-high: running the wiki's own savegen.js in the game (QuickJS) **[U]** |
| Unit Builder | In the Mods window (tab) and from Bring Units > My Units > "New unit" (same window). | "the builder was supposed to be something inside of the launcher ... allow people to create custom units inside of the multiplayer ... think about it" | 25-40 h | HIGH: biggest UI job (see Q1) |
| In-game HUD | Following a friend hides their marks; "Ben sees this too" when you look at the same place; cog "Teams button: Show/Hide"; Players panel stays when Teams opens; both fold; see-through has an Off step; Players panel no longer breaks when you have no units at the base. | the six HUD lines + "see them without any transparency" | Done in viewui + seethrough; empty-base bug 2-4 h | Low |
| Fast join | Cog option "Fast join", default Off. | "put it as a toggle" | Done (latejoin) | Low |
| Windows | Windows players get the same mod loader (Mods window, soft restart) through our baselib.dll in a game copy; install.bat kit. No Windows launcher. | "No, I don't want a Windows version of the launcher ... The only thing I want is a mod loader" | 20-30 h | HIGH: dsload not ported to the proxy yet; one wine copy only (see Q2) |
| Downloads | GitHub releases (public, MIT) + a download page on dungeonsettlers.wiki linking the kits. | "sure" | 1-2 h | Low |

## 2. Menu map (every owner item has one home)

```
TITLE SCREEN (stock buttons kept)
├─ Continue / New Campaign / Load ............ stock. Load list = single-player worlds only,
│                                                toggle "Show multiplayer worlds"; MP tag + "Remove tag" on tagged rows.
│                                                A world that needs other mods: "Load it with them?" -> soft restart.
├─ Multiplayer  (no hover card)
│   ├─ Host a game
│   │   ├─ Your save ........ host page (name, cog Rules) -> load list = multiplayer worlds only,
│   │   │                     toggle "Show single-player worlds" -> lobby -> Steam invite / address
│   │   ├─ New save ......... host page -> stock New Campaign -> lobby
│   │   └─ Foreign Expedition  host page: Expedition Rules on the page (Death permanent Off, XP counts,
│   │                         Dungeon loot comes back, Units each, Recruiting) -> Start -> neutral hub
│   ├─ Join a friend on Steam / Join by address / Recent Worlds (Reconnect, Watch) ... as today
│   └─ Back
├─ Mods  (window, Mud Slogel in the title)
│   ├─ Mods ........ list with switches, groups ("Save these mods as a group", chips), Apply (soft restart)
│   ├─ My Units .... saved units and teams; "Add from wiki link" (clipboard, one click); New unit; Edit; Delete
│   ├─ Gallery ..... wiki public builds (teams / units, popular / recent, filters) -> Save to My Units / Test Fight
│   ├─ Test Fights . units (My Units / team / Gallery) + fight (Boss, Arena, DPS Check, Finished Base) + difficulty,
│   │                supplies, kits -> Start
│   └─ Unit Builder  race, background, traits, level, skills, gear, live sheet -> Save to My Units
└─ Settings / Quit ... stock

IN A MULTIPLAYER WORLD (your save, new save or the hub)
├─ Players panel (top; fold to header; right-click drag) + cog settings (see-through incl. Off, Teams button
│  Show/Hide, Fast join, tooltips, mini overview)
├─ Teams button + Teams window (Hide = fold to a bar)
├─ Camera marks of friends, "<name> sees this too", pings, chat ............ as v11 + viewui
├─ Esc menu: PvP Match (every MP world, hub included), Leave multiplayer
└─ HUB ONLY: Bring Units window at the start (tabs From a save / My Units, "New unit" -> Unit Builder),
             End expedition -> Results ("Apply to a new save" only for units from a save)
```

**Where custom units and the builder live (decision, owner asked us to think):**
1. My Units, the unit card, the link reader, Gallery, Test Fights and the Unit Builder are ONE code set
   (`src/units/` in ds_multiplayer, built into the multiplayer module). The loader's Mods window shows them as tabs
   through a small tab hook. Reason: Bring Units (hub, PvP) and Test Fights use the same library, card and decoder.
2. One builder, two doors: the Mods window tab, and Bring Units > My Units > "New unit" inside a hub or PvP (he asked
   for creation inside multiplayer). Same window, same library; no second builder.
3. The library is a file in the game's data folder (`Mods/units/library.json`), never in a save: a unit can be brought
   to many hubs, a run uses a copy, nothing is deleted after use and nothing goes into a regular world.

Removed homes (no duplicates): the Tauri launcher (Play, world list, its builder and Paste dialog); the "World ‹ ›" row on
the host page (replaced by the three choices); the separate PvP "Bring Units" window (merged into the one Bring Units).
Kept as is, not in this plan's work: spectator, chat, pings, proposal, lend, Rules panel.

## 3. Flows

**A. First start (Mac)**
1. Download "Dungeon Settlers Modded" zip from GitHub (or the wiki download page). Drag to Applications.
2. One Terminal line from the README (removes the download quarantine; without an Apple Developer ID macOS 27 moves the
   app to the Trash **[V: modsmenu NOTES]**).
3. Double-click. If Steam is not running, the app starts it in the background and waits (steamstart).
4. First start only: the app clones the Steam install into its own folder (7 s measured **[V]**) and copies the saves once.
   Steam's install is never changed.
5. Title screen with Multiplayer and Mods. Later starts go straight to the title.

**B. Host your save**
1. Multiplayer > Your save. 2. Name, cog Rules. 3. Load list shows multiplayer worlds; toggle shows single-player ones.
4. Pick, Load. 5. Lobby, Steam invite or address. 6. When a friend joins, the world gets the Multiplayer tag with their name.

**C. Host a new save**
1. Multiplayer > New save. 2. Name, cog Rules. 3. Stock New Campaign. 4. Lobby, invite.

**D. Foreign Expedition (neutral hub)**
1. Host: Multiplayer > Foreign Expedition. The page shows Expedition Rules directly: Death permanent (Off), XP counts,
   Dungeon loot comes back, Units each (1/2/3/4/6), Recruiting in the hub. Each with a game tooltip.
2. Start: the host builds the hub from the neutral template world and kits it: all research done, all regions open,
   storage stocked (new code in WP-FE; no kit code exists today **[V: grep fe/]**; what the template already has **[U]**). Lobby opens. Friends join by Steam or address.
3. Every player gets Bring Units: tab "From a save" (pick a save, tick units) or "My Units" (tick units or a team),
   or "New unit" (Unit Builder, then it is in My Units). Cards show the Compact Plus view. Units are copies.
4. Play: dive together; PvP Match from the Esc menu any time between dives.
5. End expedition -> Results per player. Units from a save: "Apply to a new save" per the rules (the original save is
   never changed). Units from My Units: nothing comes home, nothing is deleted; they stay in My Units unchanged.

**E. PvP**
1. In any multiplayer world (hub included): Esc > PvP Match. 2. Mode, teams, Vanilla check. 3. Each player: Bring Units
   (same window as the hub) or the world's own units. 4. Start Match; the world comes back as it was after the match.

**F. My Units: make, keep, bring, reuse**
1. Make: Unit Builder > Save to My Units; or copy a link on the wiki builder (long or short), Mods > My Units >
   "Add from wiki link" (one click, reads the clipboard; a box appears only if the clipboard holds no wiki link);
   or Gallery > Save to My Units.
2. Keep: My Units lists units and teams with the Compact Plus card. Tick units > "Save as team".
3. Bring: hub or PvP > Bring Units > My Units tab > tick > Bring. Test Fights: pick from My Units.
4. Reuse: after any run the entry is unchanged. Bring it again, edit it, or delete it yourself.
5. Never: a custom unit is never written into a regular world (guard in every save writer + test).

**G. Wiki links**: long `?team=` / `#team=`, one-character `#b=`, bare code, short `/t/<id>` and `/build/<id>` (any
language prefix). Short link = one HTTPS GET to dungeonsettlers.wiki only, read the redirect or meta refresh, 5 s timeout.
Units built with the builder's folded values. Messages: offline, 404, other game data.

**H. Gallery**: Mods > Gallery > Teams or Units, Popular / Recent, filters (race, tree, skill) > open a card >
Save to My Units / Test Fight. Read-only (no likes or uploads). Builds from another game build: shown, refused on use.

**I. Test Fights**: Mods > Test Fights > pick units (My Units, a team, a Gallery build) > fight type (Boss: Golem /
Sentinel / Abyss; Arena: pick monsters; DPS Check; Finished Base) > difficulty, supplies, kits (wiki defaults) > Start >
the game loads into it. Test worlds never show in the Load list. Units stay in My Units.

**J. Apply mods (soft restart)**
1. Mods tab: change switches or pick a group, Apply. 2. In a world: the game's own save first. 3. The dungeon loading screen.
4. Behind it a hidden new game process starts with the new mods, skips logos, opens the same place (title page, or the
same world, camera, zoom, speed). 5. It swaps in; the old one closes. Fails or 90 s: loading screen goes, old mods stay,
the reason is shown. Not offered inside a running multiplayer session.

**K. Load a world made with other mods**: "This world uses: <names>. Load it with them?" [Load] [Cancel] -> flow J
straight into that world. (Today the loader refuses with "Start it from the launcher", which no longer exists.)

**L. Windows**: unzip the kit, run install.bat (copies the game folder once, puts our baselib.dll in the copy, makes a
"Dungeon Settlers Modded" shortcut). Same title, Multiplayer and Mods as Mac once the loader port lands (see Q2).

## 4. Checklist: every line of owner_feedback_2026-10-15.md

| # | Owner line (short) | Answer in this plan | Status |
|---|---|---|---|
| 1 | Full plan, then build, reorganize | This file; section 5 | plan |
| 2 | Only what he asked; no invented extras; no internal questions | Section 6; parked list in 5.4 | rule |
| 3 | Reviews: only production breaks / data loss | 5.3 rule 4 | rule |
| 4 | Mod loader like tModLoader, things inside the game | §1 row 1, §2, WP-LOADER | build |
| 5 | Launcher "play this world" opens a normal game; world list useless; UI bad | Launcher retired; worlds load in the game's own Load list; flow K | build |
| 6 | Keep mods + groups tab, Unit Builder, My Units | Mods window tabs | build |
| 7 | No Windows launcher; Windows needs the mod loader | Flow L, WP-WIN | build (Q2) |
| 8 | Soft restart behind a loading screen, back where you were, cut the fluff | Flow J, WP-LOADER | build |
| 9 | Logo Mud Slogel idle | App icon done **[V: modsmenu]**; Mods window title | done/build |
| 10 | Key: don't care; mod list open | No work; shared key stays | no work |
| 11 | Saved units cannot be selected in Bring | One Bring Units, My Units tab, WP-UNITS | build |
| 12 | Wiki link: click, clipboard | Flow F step 1 | build |
| 13 | Short links + folding | Flow G; stat check vs builder on short links | build |
| 14 | Does not understand My Units | Flow F; one home (Mods > My Units) + Bring tab | build |
| 15 | Custom units never in regular games; saved in the game; reuse in hubs; not deleted, not saved | §2 decision 3, flow F step 5, guard (myunits worktree, 18 checks **[V]**) | build |
| 16 | Custom / boss fights like the wiki, in the game | Flow I, WP-FIGHTS | build |
| 17 | Gallery in the mod loader; choose team / units / gallery | Flow H + I, WP-FIGHTS | build |
| 18 | Builder placement: think | §2 decision 1-2, WP-BUILDER | build (Q1) |
| 19 | Host page title glitched; Multiplayer hover card | World row removed (three choices); hover card off **[V: uifix11 commit]** | done/build |
| 20 | Your save / New save / Foreign Expedition under Host | §2, WP-MENU (half done in uifix11 worktree) | build |
| 21 | Save lists split with toggles | §1 save lists, WP-MENU | build |
| 22 | Multiplayer tag when a friend connects, removable, names | mptag branch **[V]** -> WP-MENU | done, merge |
| 23 | MP app starts Steam | steamstart branch + Modded stub **[V]** | done, merge |
| 24 | Install needs Terminal (Mac); Windows install.bat | Mac: one Terminal line stays. Cannot remove it without an Apple Developer ID (owner: no membership now, 2026-10-14). Windows: winkit install.bat | cannot (Mac) / done (Win) |
| 25 | FE = neutral hub, everything unlocked; no save/new question | Flow D, WP-FE + WP-MENU | build (Q3) |
| 26 | Bring Units overflow, no icons | Compact Plus card, WP-UNITS | build |
| 27 | Hub rules not choosable; death off by default; PvP from hub | Flow D step 1 + E, WP-MENU / WP-FE | build |
| 28 | Unit display like the wiki Compact Plus | Card: avatar, worn items, skill bar, branches, passive, inscriptions | build |
| 29 | Could not test PvP / hub (v10) | In v11 **[V: PLAN.md lan_v11]** | done |
| 30 | FE = fully kitted hub, units copied | Flow D (template hub, copies) | build |
| 31 | Follow: friend's frame annoying | viewui **[V]** | done, merge |
| 32 | Inside a friend's view: show we look at the same thing | viewui "sees this too" **[V]** | done, merge |
| 33 | Setting to hide Teams | viewui cog "Teams button" **[V]** | done, merge |
| 34 | Teams hides the Players bar | viewui **[V]** | done, merge |
| 35 | Collapse Players and Teams | Teams fold (viewui); Players fold exists since v9 - WP-HUD checks it by eye-free probe | done, check |
| 36 | No units at base: Players window bugs out | WP-HUD: reproduce + fix | build |
| 37 | See-through Off step | seethrough branch **[V]** | done, merge |
| 38 | Weapon slot host-only is fine | No work; weaponslot lane stays stopped | no work |
| 39 | Fast join toggle | latejoin branch, default Off **[V]** | done, merge |
| 40 | Players never on different floors | No work | no work |
| 41 | GitHub public MIT; download page | Repos public + MIT **[V]**; WP-DL page on the wiki | build |
| 42 | Branch list (keep finished work) | §5.1 start points | plan |

## 5. Build plan

### 5.1 Existing work and its fate

| Branch / worktree | Content | Fate |
|---|---|---|
| sockpath | control socket path fix | merge in phase 0 |
| steamstart (+ steamstart-win = winkit, same commit 09cf1bc) | Steam starts by itself; Windows install.bat kit | merge in phase 0 |
| seethrough, latejoin, viewui | HUD items 31-35, 37, 39 | merge in phase 0 (in this order, all touch ui_game.c / ui_model) |
| uifix11 (committed: World row + hover card) + worktree changes (three host choices, uncommitted) | items 19-20 | WP-MENU starts in ../ds_mp_uifix11, commits the worktree first |
| fehub | branch = the uifix11 commit, worktree clean: nothing extra | drop (branch kept, not used) |
| mptag | item 22 | merged by WP-MENU (same files) |
| myunits (../ds_mp_myunits, uncommitted: myunits.c, wk_link.c, wk_http.c, fe guard, My Units tab) | items 11-15 | WP-UNITS commits it first; its fe/ guard merges in phase 0 so WP-FE starts on it |
| ds_launcher myunits (library path, short links in Rust) | logic reference | Rust shortlink = reference for the C one; Tauri UI not shipped |
| ds_launcher modsmenu (app stub, helper, icon) + worktree `loader/modules/modmenu/` | item 4, 9 | WP-LOADER starts here |
| ds_launcher softrestart worktree (dsl_soft.c 606 lines, uncommitted) | title case PASS: hidden, 15.5 s **[V: test_env/soft_out/run2]** | WP-LOADER commits it, proves the world case |
| ds_launcher ownerapp, winlauncher | Tauri app / Windows launcher | not shipped. winlauncher's game-copy + Steam detection code = reference for WP-WIN |
| weaponslot (both repos) | no new commits | stopped (item 38) |
| bepinex-bridge | C# bridge | parked: not in the owner's list; not merged for v12 |
| loader `vendor/mp` snapshot | hand-synced copy of src | replaced: the multiplayer module builds from a pinned ds_multiplayer commit (the release tag), so the gate tests the same code |

### 5.2 Work packages (split by file; one lane = one branch + one worktree)

| WP | Owns (only this lane edits) | Starts from | Items | Pair use |
|---|---|---|---|---|
| P0 lead merges | main of ds_multiplayer | main | sockpath, steamstart+winkit, seethrough, latejoin, viewui, myunits fe/ guard only | gate unit + smoke per merge |
| WP-HUD | src/ui_game.c, ui_model.c/.h, ui_teams.c, ui_test/ | main after P0 | #35 check, #36 fix | 1 pair, short |
| WP-MENU | src/mpmenu.c, mpmenu_model.c, mpmenu.h, mpmenu_test/ | ../ds_mp_uifix11 (+ merge mptag) | #19-22, #25 host page, #27 rules on the FE page (death Off default) | 1 pair |
| WP-UNITS | src/units/ (new: myunits, wk_link, wk_http, unitcard), pvp_wiki.c/.h, fe/fe_menu.inc, pvp_ui.inc | ../ds_mp_myunits | #11-15, #26, #28; one Bring Units for hub + PvP; tab-content functions for the Mods window | 1 pair |
| WP-FE | src/fe/fe_game.c, fe_save.c, fe_json.c, mp_game.c (hub hooks), pvp_game.c / pvp_core.c | main after P0 | #25, #27 (defaults), #30, PvP in the hub | 1 pair |
| WP-LOADER | ~/ds_launcher: loader/src (dsl_soft.c, dsl_world.c, dsl_main.c), modules/modmenu, app/, loader/build.sh | modsmenu branch + softrestart worktree | #4-9; Mods window + Mods tab + tab hook; flow K; module built from a pinned src commit | test copy S; counts as 1 of the 4 pairs while it runs |
| WP-FIGHTS | src/units/gallery.c, testfight.c, vendor QuickJS | main after WP-UNITS merges | #16-17 | 1 pair (after WP-HUD frees one) |
| WP-BUILDER | src/units/builder_ui.c + ~/ds_launcher/builder (Rust staticlib) | main after WP-UNITS + WP-LOADER | #18 | 1 pair (Q1) |
| WP-WIN | win/ (baselib proxy: dsload port, save-folder redirect, soft restart via CreateProcess), winkit | winkit + winlauncher reference | #7, #24 | the one wine copy (Q2) |
| WP-DL | ~/dungeonsettlersdb download page | its main | #41 | none; gates green = push live |

Lanes at the same time: phase 1 = WP-HUD, WP-MENU, WP-UNITS, WP-FE, WP-LOADER (5 lanes, 4 pairs: WP-HUD is short and
gives its pair to WP-FIGHTS). WP-WIN runs beside them on the one wine copy. Phase 2 = WP-FIGHTS, WP-BUILDER.
`mp_game.c` belongs to WP-FE only (latejoin's 20 lines merge in P0 first). `pvp_ui.inc` belongs to WP-UNITS; WP-FE
reports any PvP-button change to it. The title column: WP-MENU owns our Multiplayer clones; WP-LOADER places "Mods"
below them and reads the column from the game objects (no edit of mpmenu.c).

### 5.3 Merge order and rules

1. P0 (lead, sequential, LANE_RULES rule 3 each): sockpath -> steamstart + winkit -> seethrough -> latejoin -> viewui ->
   myunits fe/ guard. Then tag nothing; lanes branch from this main.
2. Phase 1 merges as each lane goes green: WP-HUD -> WP-MENU -> WP-UNITS -> WP-FE (needs WP-UNITS' Bring window) ->
   WP-LOADER (in ds_launcher; it pins the ds_multiplayer commit it builds). Phase 2: WP-FIGHTS -> WP-BUILDER.
3. Interfaces fixed up front so lanes do not wait: library.json v1 format (ds-launcher-units v1 **[V]**);
   `units_tab_draw(tab, root)` / `units_tab_close()` exported by the multiplayer module, found by modmenu with dlsym;
   Bring Units API `fe_bring_lib` / `fe_lib_list` (fe_ui_api.h **[V]**).
4. Reviews (owner rule): only production breaks or data loss. No visual review round: visual work goes live to him
   after the gates.

### 5.4 Parked (not asked in this feedback; nothing built for v12)
BepInEx bridge; spectator open questions (MORNING_QUESTIONS); per-world mods LINE in the Load list (flow K covers the
need); custom difficulty downloads; mod auto-updates inside the game.

### 5.5 Release gate lan_v12 (one gate, takes rig/release.lock, 1 pair)

Ships as ONE app "Dungeon Settlers Modded" (Mac zip + Windows kit) on GitHub releases, tag `lan_v12`.
Includes WP-HUD, MENU, UNITS, FE, LOADER, FIGHTS (and BUILDER if Q1 says hold). All green, then ship (owner rule: no
partial versions; exact "what is in it" list checked against the build).

Tests:
1. `release_gate.sh v12` on the frozen src (Mac 31 + Windows 8, fail-fast).
2. Unit: net, mp_test, ui_test, mpmenu_test, pvp_test, trn_test, fe_cli custom guard (18), mu_test, link decoder
   (500 builder links + short / one-character links), gallery parser.
3. Pairs: host your save; host new save; FE dive with one save unit + one My Units unit, Death Off, then apply (save unit
   comes home, custom unit does not, library unchanged); PvP Match from the hub; load-list filter + tag + Remove tag;
   viewui_pair; seethrough_pair; fastjoin_pair; Players panel with 0 units at the base.
4. Stats: builder vs game on long AND short links (`ext/pvp/pvp_wiki_units.py`): equal numbers.
5. Test Fight: Boss Golem with 3 My Units units loads; Arena and DPS Check load; units equal the builder sheet.
6. Loader: `app/test/run.sh`; soft restart title + world (never visible, same save, camera within one tile, speed, time
   recorded); dsload pair soak with every module; flow K offer; first start from a clean folder; Steam not running at start.
7. Windows: kit dry run on the one wine copy; Mac host + Windows guest xplay.
8. Public repo: grep the diff for keys; no saves, templates, game files or binaries committed.

## 6. Owner questions (only where his words leave something open that he would see)

- **Q1. Unit Builder timing.** The in-game builder is the largest job (25-40 h). Default: v12 ships everything else; the
  Unit Builder tab in v12 says "Coming next version" and "Open the wiki builder" (browser), and "Add from wiki link"
  brings the result in with one click; the in-game builder ships in v13. Option: hold v12 until the builder is in.
- **Q2. Windows mod loader timing.** Porting the loader into the Windows baselib.dll is 20-30 h and has never run on a real
  Windows PC. Default: v12 Windows kit = multiplayer as today (install.bat, no Mods window); the Windows Mods window and
  soft restart follow in v13. Option: hold v12 for Windows.
- **Q3. Neutral hub map.** Default: the wiki's "Finished Base" template world (a built base; the same template Test Fights uses),
  kitted by new hub steps in WP-FE (all research, all regions, stocked storage), downloaded from the wiki on first use. Option: an empty
  base map with everything unlocked but nothing built.

## One release (owner 2026-10-15)
- "I want everything in one. Why would you separate it?" -> no v12/v13 split. The next release contains ALL of MASTER_PLAN.md: menus, units, FE hub, loader, Gallery + Test Fights, in-game Unit Builder, Windows mod loader. Lanes: merger, wpmenu, wpunits, wpfe, wploader, wpfights, wpbuilder, wpwin (+ viewui, weaponslot, softrestart finishing).
