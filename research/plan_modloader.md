# Dungeon Settlers mod LOADER (in the game) - plan (2026-10-15)

Status: plan only. No code changed. Replaces the "external launcher does everything" shape of `plan_launcher.md`.
Marks: **[V]** = checked in our files this session. **[U]** = not verified, test before we build on it.

Owner feedback 2026-10-15 (short): he wants a mod LOADER like tModLoader, not a launcher. The game boots, and the
mod controls are INSIDE the game. Today "Play this world" only starts the game, and he must pick the world again in
the game; the launcher's world list cannot start a world. Keep: mods on/off, groups, Unit Builder, My Units. Wants a
Mods tab. Unclear today: how to pick a unit and then use "Paste Wiki Link". Logo: a Mud Slogel (or a Gloth).

## TLDR

| Step | What the owner sees | Cost (agent-hours, risk) | Failure point |
|---|---|---|---|
| 1. "Dungeon Settlers Modded" = the download itself. NO launcher, NO installer window (owner 2026-10-15: "The only thing I want is a mod loader") | Mac: download the app, drag to Applications, double-click: the first start makes its game copy from Steam's install (no window: an APFS clone takes seconds **[U: time]**), then the title screen with mods. Every later start goes straight to the title. Windows: unzip "DS Modded", double-click `DS Modded.exe`, same first-run copy (minutes for ~4 GB **[U]**; progress as a Windows notification, no window). | 8-12 h, medium. Copy code exists (`copy_mac.rs`, `make_mp_app.sh` stub) and moves into the app's own stub + helper. | The one unavoidable one-time step and why: Steam's install is never touched (design rule of `plan_launcher.md`: a Steam update or file check must not break or undo the mods), so the first start must clone it (Mac APFS clone: seconds **[U: time]**; Windows: full copy, about 4 GB **[U]**). Plus macOS "Open Anyway" once (unsigned). Steam must be running. |
| 2. "Mods" button on the title screen, window with tabs: Mods / Unit Builder / My Units / Gallery / Test Fights | Mods tab: list with on/off switches, "Save these mods as a group", groups as chips, "Check for updates" | 10-16 h, medium. Same method as the MP title buttons and windows (`mpmenu.c` clones `SubUI_TitleMenuButton`, text fields are clones of a stock `TMP_InputField`) **[V]** | Title column already holds the MP clones: layout space. |
| 3. "Soft restart" to apply mods (section 2) | Press Apply: the game's own loading screen (the one shown when entering a dungeon, `LoadingUI` **[V: dump.cs]**) covers the screen. Behind it a new game process with the new mods starts HIDDEN, skips logos / intro, opens the same screen (same title page; same world, same camera, if one was open). Then it swaps in and the old process closes. The player only sees a loading screen. | 10-16 h, HIGH | Hiding the new window until it is ready **[U: not tried]**. Two game processes with the same Steam app id for a few seconds **[U]**. Boot time unmeasured **[U]**. A world must be saved before the swap (the game's own save). Not inside a running multiplayer session. |
| 4. Mods per world in the game's own Load screen and New Campaign screen | Each save shows "Mods: Hell, Multiplayer [Change]". Load a world that needs other mods: "This world uses other mods: <names>. Load it with them?" -> soft restart (row 3) straight into that world. | 8-12 h, medium-high | Load after the soft restart goes through `LoadCampaign(string)` 0xD6C900 (rig and MP loads already use it **[V]**); iron mode autosave path **[U]**. |
| 5. My Units tab + unit library inside the game data | List of saved custom units and teams; "Paste Wiki Link" adds a team to the library in ONE click (reads the clipboard, no paste box); in PvP / expedition hub "Bring Units" has a "My Units" list: tick units, Bring (section 4a) | 8-12 h, medium. Export code is C already (`pvp_build.c`, `pvp_wiki.c`) **[V]** | The guard "never into a regular save" must be tested on the Foreign Expedition apply path **[U]**. |
| 5b. Short wiki links + one-character links work everywhere | `dungeonsettlers.wiki/t/abc123` (and old `/build/<name>`) pasted = the units arrive, same numbers as the builder (folded passives included) | 4-6 h, medium | Needs an internet call from the game (only to dungeonsettlers.wiki). Offline = clear message "needs internet; or copy the long link". |
| 6. Unit Builder tab in the game | The builder's steps (race, background, traits, level, skills, gear) in game widgets, live stat sheet | 25-40 h, HIGH (largest step). Logic reusable (`builder/` Rust: 114 377 numbers equal to the wiki builder **[V]**), UI must be rebuilt in game widgets | Scope: `ui/builder.js` is 1045 lines of UI. Rust compute must be linked into the C module (staticlib) or ported. |
| 7. Updates inside the game | Title line: "Steam has build N+1. Mods ready: 3/4 [Update]". Pinned build until all mods are ready (owner rule) | 6-10 h, medium. `decide.rs`, `fetch.rs`, `crypto.rs` reused as a helper binary **[V]** | A copy cannot rebuild itself while it runs: the helper does it at the next start. |
| 8. Windows | Same app shape: a "Dungeon Settlers Modded" shortcut to a game folder copy with our `baselib.dll` | 20-30 h, HIGH | dsload is not ported to the `baselib.dll` proxy yet (it has its own MinHook engine **[V]**). Own save folder on Windows needs a redirect **[U]**. Real Windows (not Wine) untested **[V: win/NOTES.md]**. |
| 9. Logo = Mud Slogel idle sprite (decided) | App icon + title of the Mods window | 1-2 h, low | Pixel art at 1024 px: needs nearest-neighbour scaling (no blur). |
| 10. "Test Fights" tab = the wiki builder's "Test this team in-game", done INSIDE the game (section 4c) | Pick units (My Units, a team, or Gallery), pick a fight: Boss (Golem / Sentinel / Abyss), Arena (choose monsters), DPS Check (scarecrow), or Finished Base; difficulty, supplies, battle kits; press Start: the game loads straight into that fight. No save download, no file copying. | 14-22 h, medium-high | The wiki's generator is 1674 lines of JS (`savegen.js`) + the builder's stat core. Plan: run the wiki's OWN JS inside the module (QuickJS, a small C JS engine) so results equal the wiki **[U: QuickJS inside the game process not tried]**. Templates = 4 save files, 0.7-3 MB each. |
| 11. "Gallery" tab = the wiki's public build gallery, read-only (section 4d) | Browse teams or single units (popular / recent, filter by race, tree, skill), open one, then "Save to My Units", "Bring" (PvP / hub) or "Test Fight" | 6-10 h, medium. Live API checked: `GET /api/gallery?kind=team\|char&sort=popular` answers 24 items per page **[V 2026-10-15]** | Needs internet. Builds made on another game build: shown with "other game version" and refused on use (`gb` field per item **[U: meaning of gb = game build]**). |

Order (lanes decide; this is the starting proposal): 1 -> 2 -> 3 -> 4 -> 5 + 5b -> 11 -> 10 -> 9 -> 7 -> 6 -> 8. Steps 1-5b are what makes it "a mod loader" for him.
5 and 5b can start NOW in parallel with 1-4 (they live in the MP src: Bring Units window + `pvp_wiki.c`).
10 and 11 depend on 5b (the link decoder) and on the window frame of step 2. The Windows LOADER (8) stays; there is
no Windows launcher.
Steps 6 and 8 are the big ones and can run in parallel lanes after step 2 fixes the window frame.

## 1. Shape (taken from tModLoader, shape only)

tModLoader is a separate Steam app. You start it, the normal title screen comes up, and the mod controls are IN the
game menus: a mod list with enable switches, a "Reload Mods" button that applies the changes, saved mod lists
("mod packs"), and a mod browser. Changes apply on reload, not live. **[U: from general knowledge; one web search
did not confirm the exact menu names or its per-world behaviour. It is used for the shape only.]**

Our version:
```
Dungeon Settlers Modded.app        (Mac; Windows: a shortcut to the copy's exe)
 └─ stub executable (C, ~200 lines, from rig/mp_launcher.c)
      1. reads <data>/Mods/active.json (which mod set to start)          [file, not a launcher]
      2. runs dsml-helper --prepare <set> (only if the copy is missing / stale / an update is pending)
      3. starts the play copy of that set with DYLD_INSERT_LIBRARIES=dsload.dylib + DSL_PROFILE + DSML_* env
 └─ dsml-helper (Rust CLI = the current core crate without Tauri): copies, byte patches, data mods,
      signed index, downloads, pin/update decision
 └─ dsload.dylib + mods/<id>/ (unchanged loader, plus one new module "modmenu")
```
The game itself shows everything. The new module `modmenu` (C, in-game UI) draws the Mods button and window,
the per-world line in the Load screen, and writes the files the stub and the helper read.

Why a play copy per mod set stays: data mods change asset files (Hell's tables + `catalog.bin`, weapon slot's
`data.unity3d` + GameAssembly bytes **[V: NOTES / memory]**). Code mods no longer need their own copy (dsload
`patch_rva` writes code patches at start **[V]**). APFS clones cost almost no disk on Mac **[V method; size not measured]**.
The owner sees ONE app; the copies sit in `~/Library/Application Support/DSModLauncher/games/` as today.

## 2. What needs a restart and what can change live (honest list)

| Mod / kind | Live toggle? | Why |
|---|---|---|
| Data mods (tables, catalog.bin, assets: Hell data half, cheaper recruits, weapon slot assets) | **No. Restart, and another play copy.** | The game reads its tables and bundles at boot. Files of a running copy are never rewritten. |
| Code patches (`patch_rva`: Hell's 20 instruction patches) | **No. Restart.** | Written in the early phase before game code runs. Writing code that may be running is the risk the engine avoids. |
| Hook modules today (multiplayer 131 hooks, gcd-cdr 1 hook) | **No today. Restart.** | Hooks are installed at start; the engine has no "remove hook" call while game threads run. |
| Future opt-in: small hook-only mods marked `"live": true` (gcd-cdr is a candidate) | Possible later, at WORLD LOAD only, never mid-game | The hook stays installed for the whole session, the module checks an on/off flag. Gameplay mods must not change inside a running world (multiplayer mod-set check, determinism). |
| Client UI comfort mods that opt in | Possible later, live | Same flag method. |

So every change of the active mod set is a restart. The owner wants it to feel like loading a dungeon, not
like a restart ("soft restart", owner 2026-10-15). Process A = the running game, B = the new one.

1. **Apply.** Mods tab switches change a draft; Apply starts the swap. A notes where the player is: scene, title page
   or open window, and, in a world: the world, camera position and zoom, game speed / pause. In a world, A first saves
   with the game's own save (the normal save of that world; nothing else is written into it).
2. **Cover.** A shows the game's own loading screen (`LoadingUI`: stage text, tip, progress, region animation;
   `SetStage` 0x5D8650, `SetProgress` 0x5D84B0 **[V: dump.cs]**) and keeps it up until step 5.
3. **Start B hidden.** A writes `<data>/Mods/handoff.json` (new set + where to return) and starts B (Mac: `open -n`
   of the same app; Windows: `CreateProcess`). B must never show a frame before it is ready:
   - Mac: play copies get `LSUIElement` (no Dock icon, not activated) **[V: copy_mac.rs already writes it for hidden
     test launches]**; dsload in B hides the game window before its first frame (window alpha 0 / ordered out through
     the Objective-C runtime from the dylib) **[U: Unity's window creation path not traced]**, and turns the app back
     to a normal Dock app when it shows itself (`setActivationPolicy:Regular`).
   - Windows: the `baselib.dll` proxy hooks `ShowWindow` / `CreateWindowExW` of the game window (hidden, same size)
     **[U]**.
   - Same window size, position and full-screen mode as A (read from A, passed in the handoff).
4. **Skip the fluff.** B's dsload module goes to the title scene at once: the Unity splash (if this build has one; the
   `SplashScreen` API is not in this build's dump **[V]**, so it may be off already) and the game's own logos / intro
   are skipped by moving straight to the title scene **[U: what plays before the title is not recorded; the proof
   test measures it]**. Then B restores the place: title page, or `LoadCampaign(<save>)` 0xD6C900 and waits for
   `LoadCampaign.Stabilized` (the event mp_game.c already watches **[V]**), then camera / zoom / speed.
5. **Swap.** B tells A "ready" (file / socket). B shows its window at A's place and activates; A closes right after,
   so there is never an empty screen. If B fails or is not ready in 90 s, A removes its loading screen, keeps the old
   mods (the draft is not applied) and names the reason.
6. Not offered inside a running multiplayer session (other players' games would not follow); from the title or in
   single player only. The world mod check of section 3 uses the same flow.

**First proof test** (needs a VISIBLE window: the rig's hidden `-batchmode` launches draw nothing, so this run is
planned with the lead, on a test copy, never the owner's install or saves):
- Copy L, profile 1 = multiplayer only, profile 2 = + gcd-cdr. A on the title: Apply. Then A in a loaded save with the
  camera moved: Apply.
- Pass: (a) B's window is never on screen before "ready" (B's window state logged from inside B every frame + a
  screen recording of the run); (b) no logo / splash frame in the recording; (c) B's `dsload.log` lists the new set,
  A has exited; (d) in the world case: same save loaded, camera within one tile, same speed; (e) total time from Apply
  to swap recorded (first number we have); (f) Steam still sees the game (overlay opens) **[U today]**.
- Also measured once in that run: what the game shows between process start and the title, and how long.

## 3. Mods per world (in the game's own screens)

- **Load screen** (`SaveLoadView`; mpmenu already retitles it and its Load button for "Host from a save" **[V: mpmenu.c 1930]**):
  under the selected save, one line "Mods: <names> [Change]". Change opens the Mods window for that world.
- **Load a world whose mods differ from the running set:** today dsload REFUSES it (`dsl_world.c`, notice "Start it
  from the launcher") **[V]**. New: the hook asks instead: "This world uses: Hell difficulty, Multiplayer. Load it with
  them?" [Load] [Cancel]; Load = the soft restart of section 2, straight into that world. Continue (QuickLoadWithNotice) goes through the same function **[V]**.
- **New Campaign screen:** "Mods for this world: <current set> [Change]". The new world's guid is stored with the set
  after its first save (same as `worlds.rs` today, which reads `CampaignSaveHeader.CampaignGuid` **[V]**).
- Unknown world (made before the loader) = allowed with the running set, then remembered.
- Multiplayer join: unchanged (HELLO mod-set check, refusal names the mods **[V: PLAN.md launcher merge]**).
- Files: `<data>/Mods/worlds.json` (guid -> set), `groups.json`, `active.json`, in the modded copy's own data folder
  (the copy has its own save folder via `CFFIXED_USER_HOME`, first run copies the normal saves, as today **[V: launch.rs]**).

## 4. Custom units (owner rule)

- Custom units (Unit Builder, wiki links) are used ONLY in custom modes: PvP, the Foreign Expedition hub, custom
  fights (arena rooms of `trn_game.c`; a stand-alone "custom fight" mode does not exist yet **[U]**).
- Never written into a regular save. Today the FE "apply results" path writes a NEW save of the unit's home world
  (`fe_game.c` `fe_apply`) **[V]**. Wiki units carry `home_campaign_guid` zero and `source "wiki"` **[V: builder/NOTES.md]**,
  so they PROBABLY fail that path, but no test proves it **[U]**. Add an explicit refusal for `source` wiki / library +
  a unit test.
- **Unit library** = `<data>/Mods/units/library.json`, same format as `ds-launcher-units` v1 (units stored by key, teams
  of 1-8, atomic write + `.bak`) **[V: builder/NOTES.md]**. A run COPIES units out of the library; the library entry is
  never deleted or changed by a hub run (today FE results sit under `data/fe/runs/<run>` **[V]**).
- "How do I choose, then paste the wiki link?" goes away: My Units has "Paste Wiki Link" (adds the team to the
  library, uses the clipboard as `pvp_ui.inc` does today **[V]**), and Bring Units shows a "My Units" list next to
  "From a save". Pick, Bring. Clipboard stays as a second path.

### 4a. My Units: the flow in 5 plain steps (owner asked: "how is My Units supposed to work?")

1. **Make a unit.** Two ways: Mods > Unit Builder > build it > "Save to My Units"; or copy a team / unit link on the
   wiki builder, then Mods > My Units > "Paste Wiki Link" (one click, it reads the clipboard itself).
2. **It is kept.** My Units lists every saved unit (name, race, level, portrait). Tick several and "Save as team" to keep
   a group. Stored in the game's data folder (`<data>/Mods/units/library.json`), never in a save.
3. **Pick it for a mode.** In PvP or the expedition hub, press "Bring Units". The window has two tabs: "From a save"
   (today's path) and "My Units". Tick units (or one team), press Bring. The game builds copies of them for this match.
4. **Play.** The copies fight in that mode only. Nothing is written into your normal worlds.
5. **Reuse.** After the match or the hub run, the units are still in My Units, unchanged. Bring them again or edit them in
   the Unit Builder.

Today's gaps this fixes **[V]**:
- The launcher's "Bring" button only COPIES a team link and says "In the game: Bring Units, Paste Wiki Link"
  (`ui/builder.js` `BRING_HELP`); the game has no list of saved units, so a marked unit cannot be selected. Fix: the
  in-game Bring Units window reads `library.json` directly (no link round trip).
- The launcher's "Paste wiki link" opens a dialog with a text box (`pasteLink`, `dialog({... area: true ...})`). Fix:
  one click reads the clipboard; the box appears only when the clipboard holds no wiki link (then: "Copy a link on
  the wiki builder first"). The game's own PvP button already reads the clipboard directly (`pvp_ui.inc`).

### 4b. Short wiki links (owner: "short links do not work")

What happens today **[V]**:
- Launcher (`builder/src/codec.rs`): a short link gives the error "This is a short link. Open it in a browser, then copy
  the long link". Game (`pvp_wiki.c` line 149): it only looks for `team=`, so a short link is read as a broken code;
  a one-character link (`/builder#b=...`) is not read at all.
- The wiki's short links: `https://dungeonsettlers.wiki/t/<6 chars>` and old named `/build/<name>` both go to
  `/api/shorten?id=` (`site/vercel.json`), which answers a **302 redirect** to `/builder?team=<code>` or
  `/builder#b=<payload>`, or, for links with a preview card, a small HTML page with `<meta http-equiv="refresh"
  content="0; url=...">` (`site/api/shorten.js` lines 281, 467) **[V]**. The resolver answers live (unknown id = 404,
  checked 2026-10-15).

Fix (one shared C resolver used by My Units, Bring Units and the PvP button):
1. Accept: a long `?team=` link, a `#team=` link, `#b=` / `#b=~` one-character links, a bare code, and short links
   `/t/<id>`, `/build/<id>` (any language prefix).
2. Short link: one HTTPS GET to `dungeonsettlers.wiki` ONLY (no other host, no redirect to another host), redirects not
   followed automatically: read the `Location` header, else the meta-refresh URL in the HTML. Timeout 5 s. Mac:
   the system libcurl or CFNetwork from the module **[U: which one loads cleanly inside the game]**; Windows: WinHTTP.
3. Decode the long link with the existing decoder; add `#b=` decoding (port of the launcher's Rust `codec.rs`, which
   already reads all of these and matches the wiki builder on 500 builds **[V]**).
4. Build the unit with `pvp_build.c`, which already adds the builder's folded conditional passives as permanent values
   (`pvp_build.c` line 250 **[V: code present]**), so the numbers match the builder (owner rule 2026-10-14 ~18:00).
   Test: the existing builder-vs-game stat check (`ext/pvp/pvp_wiki_units.py`) run on short links too.
5. Messages: offline -> "This short link needs the internet. Or open it on the wiki and copy the long link."; 404 ->
   "This short link does not exist (removed?)"; other game data -> the existing "made with other wiki data" text.

### 4c. Test Fights inside the game (owner: "follow the way it's done inside of the wiki")

How the wiki does it today **[V: `~/dungeonsettlersdb/site/builder/savegen.js`, builder.js "Test this team in-game"]**:
- `SG.generate(opts)`, modes `base | boss | arena | dps | upload`. Boss = `golem | sentinel | abyss`.
- It downloads a save TEMPLATE captured from the game (`/assets/saves/{base,boss_golem,boss_sentinel,boss_abyss}_25846389.json`,
  0.7 / 3.0 / 2.9 / 2.9 MB), removes the template's roster, writes the chosen builds as real units (the builder's
  stat model `computeBuild`, skill split, shortcut bar), adds supplies and battle kits (+ carry-weight inscription),
  sets the difficulty, reveals the maps, puts every unit on Hold Position.
  Arena = the boss is taken out and the chosen monsters stand on and around the boss's tile. DPS Check = one scarecrow
  target in that room. The player then downloads the file and copies it into the game's save folder by hand.
- `upload` mode ("add to my save") stays HIDDEN (owner rule 2026-09-05). It is NOT brought into the game either: the
  game never adds custom units to a regular world (owner rule, section 4).

In the game:
1. Mods > "Test Fights": units from My Units / a team / the Gallery (max = the builder's team size), fight type, boss or
   monsters, difficulty, supplies, kits: the same choices as the wiki panel, same defaults.
2. Start: the module runs the wiki's own `savegen.js` + builder core in QuickJS (C, MIT licence, embeddable) on the
   template, writes the result as a test world, and loads it with `LoadCampaign` (step 4 path). Same JS = same save as
   the wiki. The builder core is already lifted out of the UI closure for node in `~/ds_launcher/builder/tests/node/builder_ref.js` **[V]**.
3. Templates: downloaded from the wiki on first use and cached per game build (same files the wiki serves); a template
   for another game build is refused, as the wiki's `templateWarnings` does.
4. Test worlds are kept OUT of the normal Load list (FE already moves its run saves out of `Saves/` so the list never shows
   them **[V: fe_game.c line 330]**).
5. The units stay in My Units, unchanged.
Alternative to QuickJS: port `savegen.js` to C or Rust (1674 lines) - more work and a second copy that drifts from the wiki.
Gameplay mods: a test fight runs with the mod set active now (no world mod check for test worlds).

### 4d. Gallery tab (read-only)

The wiki exposes it as JSON **[V: `site/api/gallery.js` header + live call 2026-10-15]**:
- `GET /api/gallery?kind=team|char&sort=popular|recent[&race=][&tree=][&has=K1,K2][&page=N]` -> `{items:[{id, k, c, o, cp,
  lk, bm, f, u, au, tags, gb}], more}`; 24 per page, pages up to 20. `f` = facets (name, races, focus), `u` = the units of
  the preview (no free text) + learned skills, `au` = author name or pseudonym.
- `GET /api/gallery?id=<id>&full=1` adds `blob` = the stored build. A listed item opens at `/build/<id>` (short link).
Game side: list cards (name, races, level, author, likes), filters, a detail view with the unit sheet (the builder core
from 4c computes it). Use = resolve the item's build through the 5b decoder, then "Save to My Units", "Bring", "Test
Fight". Read-only: no likes, bookmarks, reports or uploads from the game. Mac / Windows HTTP as in 5b, wiki host only.

## 5. What happens to the current launcher

| Part | Fate |
|---|---|
| `core/` Rust (steam, crypto, fetch, decide, copy_mac, join, worlds, difficulty, launch env) | KEPT, becomes the `dsml-helper` CLI inside the app (first-run copy, updates). `launch.rs` env moves into the stub. |
| `src-tauri/` + `ui/app.js` (Play and mods screen, world list) | REMOVED completely (owner 2026-10-15). No installer window either: the downloaded app makes its own copy on its first start (step 1). Uninstall = delete the app + its folder in Application Support (Windows: the DS Modded folder). |
| `ui/builder.js` / `builder.css` (Unit Builder UI) | Rewritten in game widgets (step 6). Kept in the repo only as reference and for tests; not shipped. |
| `builder/` Rust (compute, validate, codec, store, export) | KEPT. Linked into a module as a static library with a small C interface, or called through `dsml-helper` for heavy calls **[U: which is simpler; link size]**. |
| `loader/` dsload + modules | KEPT unchanged, plus: new `modmenu` module, `dsl_world.c` refusal -> "Load it with them?" offer (soft restart), handoff handling. |
| MP src (`mpmenu.c`, `ui_game.c`, `pvp_ui.inc`) | Reused as the method and as code (clone helpers, windows, tooltips, text fields). Bring Units gains the library list. |

Windows (the LOADER only; no Windows launcher): `DS Modded.exe` (stub) makes on its first start a full copy of the game folder per pinned build (no clone on NTFS; hard links unsafe
**[U]**), puts our `baselib.dll` proxy in it (original renamed `baselib_orig.dll` **[V: win/NOTES.md]**) and a desktop
shortcut "Dungeon Settlers Modded". The proxy loads as a dependency of GameAssembly, so no env and no stub are needed
**[V]**; it reads `Mods/active.json` itself. Work: port dsload's loader + engine API into the proxy (replace the MP-only
MinHook setup), a save-folder redirect for the copy **[U: Windows saves sit in the user's LocalLow folder; no
CFFIXED_USER_HOME equivalent]**, and a restart via `CreateProcess` of the copy's exe.

## 6. Logo (checked in the game data as extracted by the wiki pipeline)

- **Mud Slogel exists.** Unit `UNIT_MudSlogel` (UnitTable: Enemy, Beast, skills `SKILL_MudSpit`, `SKILL_Run`). English
  name in the game text table: `TEXTKEY_UNIT_MudSlogel_NAME = "MudSlogel"` (one word, no space: the game's own text).
  Also `UNIT_AbyssMudSlogel` "Abyss Mud Slogel". Sprites: `MudSlogel_MS_Idle_sheet.png` (160 x 36), Hit, Dead,
  DeadEnter sheets in `~/dungeonsettlersdb/site/anim_scenes/` **[V]**. Not checked directly in the Mac app's Data
  folder (the wiki pipeline extracts these from the game files).
- **Gloth: no unit is called just "Gloth".** Units: Gloth Pup, Male Gloth, Female Gloth, Challenger Gloth, Elder Gloth
  (+ Abyss versions) **[V]**. Sprite sheets: `GlothPup_GP_*`, `MaleGloth_MG_*`, `ChallengerGloth_CG_*` **[V]**.
- **Decided (owner 2026-10-15): the Mud Slogel idle sprite**, scaled with nearest neighbour for the app icon and the Mods window title.

## 7. Owner questions

None open. Every choice left changes nothing he sees or is settled by his words; the lanes decide them (step order,
including when the in-game Unit Builder comes; template download vs shipped; HTTP library).
Custom units rule, exactly as the owner gave it: stored in My Units, never deleted after use, never imported into a
regular game.

## Sources
- Our files: `~/ds_launcher/README.md`, `loader/NOTES.md`, `builder/NOTES.md`, `core/src/{launch,worlds,copy_mac,flow,windows}.rs`,
  `src-tauri/src/main.rs`, `ui/index.html`; `~/ds_multiplayer/src/mpmenu.c`, `ui_game.c`, `pvp_ui.inc`, `pvp_units.c`,
  `fe/fe_game.c`; `PLAN.md`; `research/plan_launcher.md`; `win/NOTES.md`; `~/dungeonsettlersdb/data/{db.json,UnitTable.json}`;
  `win/25846389/dump/dump.cs` (Gloth / Slogel enums only).
- tModLoader: general knowledge, shape only [U]. Search tried: tModLoader changelog
  https://steamcommunity.com/sharedfiles/filedetails/changelog/3299699015?p=2 (mod manager refuses mods with missing references).
