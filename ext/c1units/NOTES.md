# C1 units, pure-code part (branch c1-units, worktree ../ds_mp_c1units, 2026-10-15)

Started from wp-units (2e148c4 = myunits 38ef920 + merge main) plus its 3 uncommitted edits (snprintf in myunits.c,
wk_fp()). No game, no test pair. New code is in custom/units (the plan's C1 home); the files M4 moves
(src/myunits.*, src/wk_link.*, src/wk_http.c, src/pvp_wiki.*) were edited in place. No shared MP file touched
except .gitignore (whitelist `/custom/`) and ext/pvp/tools/gen_wiki_tables.js (generator of src/pvp_wiki_tables.h).

## What is here

| File | What |
|---|---|
| src/myunits.c/.h | + mu_add_build, mu_rename, mu_delete (teams follow; an emptied team goes), mu_team_new ("Save as team"), mu_team_delete, mu_team_id_at. Library file, path and format unchanged (Mods/units/library.json in the game's data folder, never in a save). |
| src/pvp_wiki.c/.h, pvp_wiki_tables.h | the builder's current decode: a retired race (Dwarf, Tal, White Demon) comes back as Human, retired traits (TestTestTest, TestSuperman) are dropped (builder.js dsbDecBuild / D.retired). New field wk_build.has_saved_inds. |
| src/wk_link.c | buffers per thread (the click reads on a worker); a hidden gallery build (302 to /builds/removed) says "This build was removed on the wiki." |
| custom/units/mu_card.c/.h + mu_card_tables.h | Compact Plus card data: name, level, race, background, weapon label, portrait + avatar (portrait, else the race's first), branches ACTIVE / PASSIVE with invested flags, banked milestone stacks, inscriptions / soul stones / acquired traits, worn items per slot, the skill bar (quickFor), passive skills with `folded` and stack level. Keys only; the UI takes icons and names from the game. Tables from tools/gen_card_tables.js (static assert: same wiki data as pvp_wiki_tables.h). |
| custom/units/mu_link.c/.h | one click: mul_click reads the clipboard; a link -> worker thread (short link: one GET, 5 s) -> mul_poll adds one team to My Units and saves, on the caller's thread. No link -> MUL_NEED_BOX; mul_submit(text) for the box. |
| custom/units/mu_clip.c | clipboard text: Mac NSPasteboard via the objc runtime (env DSMP_CLIP_PB = a private test pasteboard), Windows CF_UNICODETEXT. Never writes the player's clipboard. |
| custom/units/test/ | mu_test.c (moved from ext/myunits/tests), card_tool.c + builder_vs_c.js, node/ (builder_ref.js from ds_launcher, card_ref.js, make_team.js), http/ |

Not changed on purpose: a link made with OTHER wiki data (old fingerprint) is still refused. The builder opens it
with "a few picks may have shifted", but the shift is real: the owner's RzTjHe team decodes to a ring in the Head
slot and a bow in Body, and validate() strips 10 learned skills to 2. Building that unit would be wrong.

## The one rule for old links (lead decision 2026-10-15)

Every entry point (Paste link / Add from wiki link, Bring Units, Gallery, Test Fights) reads a link through this
decoder (src/wk_link.h wkl_resolve / wkl_decode_offline). It refuses ONLY when the link's wiki DATA version (the
fingerprint in header bytes 1-2) differs from WK_FP, with "This link was made with other wiki data (another game
version): make it again on the wiki." A build made on an older GAME build but with the current data loads normally
(the game build is never checked). C4 (Gallery, Test Fights) calls this decoder and never writes its own.

## Tests (all green 2026-10-15)

- `make -C custom/units/test test`: 148 PASS. `NET=1 make ... test`: 160 PASS, including:
  - short links live: /t/jEkTAd, /fr/build/NNUGfJ, /t/rey8Rw, /fr/build/txg3sz (preview-card pages, HTTP 200 + meta
    refresh) resolve to 4 / 1 / 4 / 1 units; RzTjHe (302) resolves but is refused as other wiki data; unknown /t/ and
    /build/ ids = "does not exist".
  - 5 s limit: a dead address (192.0.2.1) gives up after 5.00 s.
  - one click with a short link on the clipboard: the click returns at once (< 50 ms), the team lands in My Units.
  - folding: pvp_build.c pb_folded_skill folds exactly the builder's conditional passives (14 grants).
  - launcher fixture: 30 of 30 library units equal their team links (needs custom/units/test/fixtures, git-ignored;
    copied from ../ds_mp_wpunits; made by `cargo run -p ds-unit-builder --example myunits_fixture`).
- `make -C custom/units/test builder N=1500`: two seeds, 3000 random builds (legal and illegal picks), 18 300 links
  (?team= v1, #team= v2 deflate, &team= %-encoded, #b=~, bare code, #b=<JSON>, teams of 2-8): decode 16 499 units
  and card 18 762 units compared with the builder's own functions: 0 differences. #b=<JSON> links with keys this data
  does not have are refused (737; the builder drops those keys).

## HTTP inside the game (custom/units/test/http/run.sh)

Game facts: DungeonSettlers.app is hardened runtime with disable-library-validation, allow-dyld-environment-variables,
disable-executable-page-protection; the game binary and UnityPlayer.dylib link CFNetwork; UnityPlayer exports no
curl symbols. run.sh builds a host signed with the game's own entitlements (hardened runtime), AppKit + CFNetwork
linked, NSApplication up and the main run loop running, and injects probe.dylib with DYLD_INSERT_LIBRARIES (the mod's
way in). Result: CFNetwork and libcurl (dlopen of /usr/lib/libcurl.4.dylib) both load and answer (HTTP 200, 140-190 ms)
from a worker thread and from the main thread; wkl_resolve gives 4 units with either (DSMP_HTTP=curl forces libcurl).
Order kept: CFNetwork first (already linked by the game), libcurl second. Not yet proven inside the real game: needs a
rig command (rig_cmd.c is shared; wk_http_probe exists for it) and a pair.

Windows: custom/units/test/http/win_run.sh builds win_probe.exe (llvm-mingw; units code + WinHTTP, links with
-lwinhttp -lbcrypt). Built clean; run under CrossOver wine (bottle DSMP, no other wine running): WinHTTP HTTP 200,
wkl_resolve 4 units in 216 ms, card JSON filled, one click on the box path added the 4-unit team, dead address gave
up after 5003 ms. Wine's WinHTTP is not Windows' own: a real Windows PC is still untested.

## For M4 / L3

- The Mac dylib does not build myunits.c / wk_*.c yet (src/build.sh); fe_game.c on this branch calls mu_load.
- Windows: gen_port.py takes every src/*.c, so wk_http.c joins the Windows build: add -lwinhttp to build_win.sh.
  custom/units/*.c need adding to both builds; mu_clip.c needs AppKit at run time (the game has it).
- Side effect: I ran win/port/gen_port.py once from this worktree; it writes ~/ds_multiplayer/win/build/gen from
  ~/ds_multiplayer/src (paths fixed in the script) = what build_win.sh does before every build. Nothing else written.
