# Lane fights (v13 WP FIG): Gallery + Test Fights in the game

Branch `fights13`, worktree ~/ds_mp_fights13. History of the code part: ext/c4-fights/NOTES.md (branch c4-fights, 2 commits
replayed onto main a138cb2 with no conflict).

## Step 1 (engine, cache, gallery client on main; no game wiring)

What changed against c4-fights:
- `custom/fights/sources.txt`, `custom/gallery/sources.txt`: the solo module lists (one path per line, relative to custom/).
  fights lists `fe/fe_json.c` (the JSON reader in lane fepvp's folder; the solo dylib has no other copy of it).
- QuickJS is built through 5 wrapper files `custom/fights/qjs_*.c` (they set `_GNU_SOURCE`, `CONFIG_VERSION`, no warnings), so
  the module build needs no per-file flags. QuickJS's Makefile also sets `-fwrapv`: asked orchestrator13 to add it to the module CF.
- `sg_embed.c` embeds sg_prelude.js / sg_glue.js with `#embed` (Apple clang 21, llvm-mingw clang 23): no generated header any more.
- `fe_json.h` include paths follow the M4 split (custom/fe/).
- `custom/fights/build.sh` compiles exactly the two sources.txt lists with the module's flags, links the tests, and links a solo
  test dylib (no multiplayer, no game symbols) as the link check.

Proof 2026-10-16 (all green):

| Test | Result |
|---|---|
| run_eq.sh, local wiki ~/dungeonsettlersdb/site | 64/64 identical (25 saves, 1 error case) |
| run_eq.sh, LIVE wiki files (sg_cache_test --live cache) | 64/64 identical |
| sg_cache_test offline | 24/24 |
| sg_cache_test --live (3 scripts + 4 templates) | 13/13 |
| gal_test data | 28/28 |
| gal_test --live | 7/7 |
| gal_names.sh (178 live items, 780 names) | identical to teamname.js |
| build.sh (Mac arm64, solo dylib link) | ok |
| build.sh win (llvm-mingw, every listed file) | ok |

Merge point for slot 3 (step 1 alone): branch `fights13-step1` (80355ae). Branch `fights13` goes on with the game part,
which needs lane units' custom/home (cm_home.h) and custom/units (myunits.h, wk_link.h): it merges after units full.

## Steps 2-4: Gallery and Test Fights in the game (2026-10-16)

Files (all in custom/fights, custom/gallery):

| File | What |
|---|---|
| fights/cm_win.{c,h} | game-style window on the loader kit (dim backdrop, centred panel like the Mods window, Cinzel title, X / Esc); frame tick = hook on InputDispatcher.Update (by name; multiplayer does not hook it); the game's folders (Application.persistentDataPath, SaveLoadHelper.GetSaveDirectory); LoadCampaignRequested through MainFlow's EventQueue (the InputDispatcher's _eventQueue); after we change Saves: CampaignSaveLoader.TryRefreshSave + TitleMenuPresenter.RefreshSaveState; test channel (cmf_test via lane units' driver "call", targets fights:<name>, gallery:<name>) |
| fights/cm_work.{c,h} | one worker thread (8 MB stack): wiki downloads, the QuickJS engine, sg_generate; results back on the main thread |
| fights/tf_ui.c, tf.h | Test Fights window: Squad (My Units: teams + units, max = panel maxSquad; or a Gallery build), What do you want (DPS Check / Boss fight / Custom fight), Which fight / room, Magic Saturation (Abyss), monsters (search + How many + Remove), Difficulty (Combat, Management follows unless ticked), Battle kits + carry inscription, Start. Texts = the wiki panel's (builder.js renderSaveGen). `cm_fights_init` registers "Test Fights" (order 40) |
| fights/tf_worlds.{c,h} | test worlds: <persistentDataPath>/Mods/custom-modes/test_worlds.json (campaign GUIDs), load-list filter (title Load list, New Campaign, multiplayer host list), removal on the title |
| gallery/gal_ui.c | Gallery window: Teams / Units, Most popular / Newest, Complete only, Races, Weapon trees, cards (title, author, opens / copies / likes, members with level + trees), Save to My Units, Test Fight, Show more. Texts = the gallery page's (site/builds C.UI). `cm_gallery_init` registers "Gallery" (order 30) |

Decisions (safe defaults, no owner question needed):
- Links go through lane units' decoder (wk_link, the one rule for old links). Save to My Units: a team = mu_add_team from
  the wk_link decode; a character = mu_add_build with its stored build JSON ("b:" blob). Test Fight from the Gallery
  uses the wiki's own decoder (sg_decode_blob) for the squad, after wk_link said yes: for the 6 team blobs both give the
  same builds; for the 6 single characters wk_json drops fields the stored JSON has (setWeapon, shelf, stackLv, tag values
  rounded, names cut at 64), so the wiki decoder is the one that matches the wiki's "Test this team"
  (ext/fights13/test/wk_vs_wiki.c). Told nobody else: only matters inside this lane.
- The wiki's scripts are asked again after 6 h (offline: the cached copy). A wiki build tag that differs from the game
  build shows a message and Start stays off.
- Test worlds are removed when the title shows again (the game's own delete is off in this build:
  CampaignSaveDeletionService.IsAvailable is `mov w0,#0; ret`). Without that the title's Continue pointed at the test
  world (found in run8). To fight again: Start again (2.6 s).
- No hover cards on the page entries (tip NULL), as the owner's v12-1 rule for title buttons.

Game facts found:
- A save written by a mod is not loadable until the game's save list knows it ("Save header was not found during load
  preflight"): CampaignSaveLoader.TryRefreshSave first.
- loader data_dir() is the folder of the profile file (test copies: the instance folder), not always <game data>/Mods:
  use the game's own Application.persistentDataPath / SaveLoadHelper.GetSaveDirectory.
- rig `set_speed 3` freezes the sim (tick stops); tests use 2.
- The kit refuses widgets over 1024 live: a gallery card is 5 widgets.
- The main build's multiplayer host list shows only multiplayer worlds (no toggle), so the seed world is hidden there too.

Test: copy O (~/ds_launcher/loader/test_env/instances/O, make_instance.sh O), ext/fights13/test/cmrig.py (lane units'
cmrig, copy O), tf_test.py, build ext/fights13/tools/build_game.sh (tree = units13 + this worktree). Every run leased a
pair, hidden window off-screen, muted; no guard violation in any run after run2.

| Run (out/) | Result |
|---|---|
| run14 gallery + boss | 22 PASS 0 FAIL: page Gallery then Test Fights; live builds; team saved to My Units; Units tab; Show more 48; Races filter; Test Fight from Gallery; engine ready; Boss Golem with the My Units team loads in 2.6 s, 4 units + golem, golem HP 18000 -> 17508 after a move order; test world saves removed on the title; Continue = the player's own world; absent from Load list and MP host list |
| run15 arena + dps | 17 PASS 0 FAIL: Arena (Abyss Acid Slogel x3) HP 2000 -> 1586; DPS Check loads, world runs |
| run16 solo (no multiplayer) | 5 PASS 0 FAIL: page, Gallery, Save to My Units, Boss fight loads (player.log) |
| run6 | filter proof while a test world still existed: title Load list shows only the seed ("Autosave 0"), dsload "1 hidden by filters" |
| Windows | every listed file compiles with llvm-mingw, no warning (not run on Windows) |

Open: guard kill in run2 (01:08:51, a 305x190 window of the copy at x 40, y 742) came while the game sat in a cancelled
load (before the TryRefreshSave fix); not seen again in 14 later runs.

## Merge-ready (main 3e03d9b merged into fights13, 2026-10-16 03:55)
Build build/merge_fights (src/build.sh: dsrig.dylib + custom-modes.dylib 33 sources + custom-modes-mp.dylib).
- run_eq 64/64 local + 64/64 live, sg_cache_test 24/24 + live 13/13, gal_test 28/28 + live 7/7, gal_names identical (178 / 780).
- Game, copy O with this build: m1 gallery + boss 22 PASS 0 FAIL; m2 arena + dps 17 PASS 0 FAIL; m3 solo 5 PASS 0 FAIL.
- rig smoke on this dsrig.dylib (pair AB): 3/3 fp_tick_advanced, prefs identical.
- custom/fights/build.sh now builds only the engine / cache / gallery client for the test tools (the windows need
  custom/home + custom/units; the module build covers them).
