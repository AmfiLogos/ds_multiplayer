# Lane builder13: Unit Builder in the game (PLAN_V13 WP BLD)

Branch `builder13` (worktree ~/ds_mp_builder13) = main + 2 commits. Files: custom/builder/** only (+ ext/builder13).
It needs only main (units13 merged: My Units store, mu_ui kit, home page; fights step 1: QuickJS, sg_cache, sg_prelude).
It does not need fights13 full (cm_win / cm_work are not used), so it can merge before or after it (disjoint files).

## What it is
- Home entry "Unit Builder" (order 20) on the title's Custom Modes page (`cm_builder_init` -> `cm_home_add`).
- `cm_builder_open(unit_id_or_NULL, saved, ud)` (bld.h): the door My Units ("New unit", "Edit") and Bring Units ("New
  unit") call. NULL = new unit; an id = Edit that unit. saved(id) after Save, never on Cancel.
- Window (bld_ui.c, My Units look via mu_ui.h): Name, Level, then Race / Background / Traits / Skills / Gear /
  Inscriptions, live sheet on the right (HP, P.Atk, M.Atk, Atk Spd, Crit %, Crit Dmg, Phys Res, Mag Res, Move, the 6
  majors, Main/Sub SP left, inscriptions free). Chosen entries: gold edge + gold wash; locked skills greyed with the reason
  in the hover card. Every list comes from the model's state (no hard-coded lists in C).
- Model (bld_glue.js in QuickJS, bld_js.c): the wiki's data.js + builder.js from the Test Fights cache
  (<persistentDataPath>/Mods/custom-modes/wiki, sg_cache.h, refreshed after 6 h, same as Test Fights). The wiki's UI-part
  rules are LIFTED from the builder.js text by name (validate, setSlotItem, jewelRetarget, bgAvail, indAvail,
  bgCandidates, defaultBuild, soloShareBuild, soloCompactUrl, statLabel, fmtN, resistDisp, ...), never retyped. A click
  = the wiki's click handler body on the build, then the wiki's validate(). Only the node lock reason and the
  unlearn cascade sit inside a wiki closure (treeGraph); those few lines are copied as written.
- Game build check: the wiki files' build tag must equal the game's build (same rule as Test Fights), else the window says so.
- Thread: bld_work.c (own thread, 8 MB stack; results back on the main thread via mu_tick). A click costs ~1.1 ms in QuickJS.
- Save: the build as the wiki stores and shares it (soloShareBuild) -> My Units `mu_add_build` (checked by
  wk_from_json). Edit: same unit id (teams keep it), build + name replaced, `updated` set. The unit's wiki link =
  soloCompactUrl: `https://dungeonsettlers.wiki/builder#b=~<code>`; without a deflate in the game it is the raw (v1)
  code, which every wiki client reads (the wiki itself mints v1 when CompressionStream is missing).
- Guard: the builder writes only the My Units library file. A library unit goes into a world only as a copy (Bring
  Units) and the FE guard refuses library units in a world's results (units / fepvp lanes). Test: no file in Saves.

## Proofs (2026-10-16, tree = main 3e03d9b + builder13)
- Equality: `custom/builder/build.sh && custom/builder/test/run_eq.sh 300 13` -> 300 random builds made by clicks the
  state offers (19642 clicks): QuickJS model == node/V8 + the wiki's own builder.js for every build: the wiki's
  validate (its UI-part declarations, loaded WITHOUT our lifter) keeps the build unchanged, computeBuild JSON equal,
  link equal + decodable by the wiki, sheet equal; My Units takes all 300 (mu_add_build) and the link decodes in C
  (wk_link) to the same unit as the stored build (wk_hash). 300/300. Positive control: a changed number / link is caught.
- In game, hidden copy R (make_instance.sh R), solo (custom-modes + units-driver), `ext/builder13/test/builder_test.py`:
  32 PASS 0 FAIL: home order (My Units, Unit Builder), wiki model starts, every section drawn, race / background /
  trait / 3 skills / locked skill refused / weapon / inscription clicks, Save (name, choices in the library file),
  Edit (same id, build replaced), My Units > New unit -> listed + ticked, Bring Units > New unit -> saved + ticked ->
  Bring hands it back as a My Units pick, no file in Saves, no guard violation.
- UI scales: `--scale=0.75 --short` and `--scale=1.4 --short`: 11/11 each, window inside the 960x540 screen.
- Test Fights: `ext/builder13/test/tf_check.sh` -> the units saved in the game test + 96 equality-test builds as 4-unit
  squads (boss golem / sentinel, DPS check): 25 saves, 0 errors, 0 problems; QuickJS == node files 51/51.
- rig smoke with build/merge_builder13/custom-modes.dylib (DSRIG_CUSTOM, main's dsrig): 3/3, prefs identical.

## Defaults (MORNING_QUESTIONS.md, [builder])
- Gear rolls: pick the stat per roll; value = middle of its range (the wiki's default on pick). No slider.
- Not in the window (kept as they are on Edit): talents, portrait, soul stones, traits gained in play, elixirs, needs.

## Windows
- `x86_64-w64-mingw32-clang -c` of every custom/builder file: 0 errors, 0 warnings. bld_work.c has a Win32 thread path.
  The Windows module build (lane crossmods) reads custom/builder/sources.txt.

## Branch history
- builder13-dev (local only) = the first try with fights13 full merged in for development; builder13 was rebuilt on main
  without it (cherry-picks), so it carries only its own files.
