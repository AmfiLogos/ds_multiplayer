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
