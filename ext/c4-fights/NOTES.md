# C4 Gallery + Test Fights: code part (no game), 2026-10-15

Branch `c4-fights`, worktree ~/ds_mp_c4fights. MASTER_PLAN rows C4, flows H and I. No shared MP file edited
(only `.gitignore`: `!/custom/` so the new folder can be committed). Game wiring waits for M4 (split) + L2 (TAB API).

## What is here

| Piece | Files | What it does |
|---|---|---|
| QuickJS 2026-06-04 | custom/fights/quickjs/ (+ LICENSE, VENDOR.md) | unmodified engine library |
| tinf 1.2.1 | custom/fights/tinf/ (+ LICENSE, VENDOR.md) | raw inflate for v2 team links |
| Engine | custom/fights/sg_js.{c,h}, sg_prelude.js, sg_glue.js | runs the wiki's data.js + builder.js + savegen.js; `sg_panel` (tab choices + wiki defaults), `sg_generate` (opts JSON -> the save text the wiki download writes + file name + problems/notes), `sg_decode_blob` (gallery blob -> builds), `sg_template_file` |
| Cache | custom/fights/sg_cache.{c,h}, cm_http.{c,h} | first-use download of the 3 scripts (kept together, tags must agree, refresh by age, offline = cached copy) and the templates (`<name>_<tag>.json`, checked complete); dungeonsettlers.wiki only; Mac libcurl (dlopen), Windows WinHTTP |
| Gallery | custom/gallery/gallery.{c,h} | `/api/gallery` list URL (kind, sort, race, tree, has, complete, by, page), parser into `gal_item` / `gal_unit`, `?id=&full=1` item + blob, words.json, names by the wiki's teamname.js rule, `gal_usable` (gb vs game version) |

## How "same as the wiki" holds

- The game runs the wiki's own files; nothing of savegen or the stat model is ported.
- The panel helpers the wiki keeps in its UI half (sgBossList, sgDummy, sgEnemies, sgKitDefaults, sgMgmtFor,
  sgDiffTiers, b64dec, SG_MAX_SQUAD, SG_SHOW_BASE) are LIFTED from the builder.js text at load (sg_glue.js), not retyped.
- Browser pieces given to QuickJS (sg_prelude.js): fetch (templates from the cache), atob/btoa,
  Blob -> DecompressionStream('deflate-raw') -> Response (tinf). Node gets its own (atob, zlib) in the reference.

## Proof (all run 2026-10-15, all green)

| Test | Result |
|---|---|
| `custom/fights/test/run_eq.sh` (local wiki source ~/dungeonsettlersdb/site) | 64/64 files byte-identical QuickJS vs node/V8: 25 saves + metas, 1 error case, panel, 12 gallery blob decodes |
| same, `DET_SEED=777 DET_NOW=1790000000000` | 64/64 identical |
| same on the LIVE wiki files (downloaded by sg_cache_test --live, BUILDER/SAVES env) | 64/64 identical |
| `build_c4/sg_cache_test <dir> ~/dungeonsettlersdb/site` (offline rules) | 24/24 |
| `build_c4/sg_cache_test <dir> --live` (real download, one fight, all 4 templates) | 13/13 |
| `build_c4/gal_test custom/gallery/test/data` | 28/28 |
| `build_c4/gal_test --live` | 7/7 |
| `custom/gallery/test/gal_names.sh` (178 live items, 780 names vs teamname.js on node) | identical |
| `custom/fights/build.sh win` (llvm-mingw) | all objects compile; NOT run on Windows |

Cases (custom/fights/test/cases.json): DPS Check; Boss Golem at all 5 difficulties; Sentinel; Abyss with Magic
Saturation default (100), 0, 60; Arena in the Golem, Sentinel and Abyss rooms (+ empty arena); Finished Base;
supplies; kits all / all without the weight inscription / none; management set apart; 1 unit with an accented team
name; no team name; defaults only; a gallery team; gallery solo characters; no units (error, same text both sides).

Determinism for the compare only: test/det_prelude.js (seeded Math.random, fixed clock, no crypto) in both engines,
TZ=UTC. In the game the GUIDs are random and the save time is now (checked: sg_cli without DET_JS).

## Facts found (for the lanes that wire this in)

- Headless, builder.js runs only its pure core (everything after "UI (browser only)" needs `document`).
  validatedSnapshot / defaultBuild are UI-side, so builds enter `sg_generate` AS STORED; the wiki panel also passes
  stored roster builds. Gallery decode = decodeTeam / b64dec without the UI's validatedSnapshot pass.
- The wiki WITHHOLDS Finished Base (builder.js `SG_SHOW_BASE = false`, owner 2026-09-26: the base template is a day-0
  start). The engine generates it (tested); `sg_panel` lists `base` only when the wiki flag turns true.
- Template versions today: base DS_B.0.4.11, golem DS_B.0.4.23 ... site DS_B.0.4.29; the wiki loads them anyway (it
  warns in its panel only for a mismatch between template and site data, `templateWarnings`).
- The engine caps builds at D.consts.partyMax (4), as the wiki panel does.
- Gallery `gb` = the game version a build was made on ("B.0.4.29"). The top popular team today is "B.0.4.23":
  the plan's rule "other game build: refused on use" would refuse most of the gallery. `gal_usable` implements the plan
  rule; the lead decides whether to apply it.
- Gallery names: the wiki never shows a typed team name; titles come from f.name + builds/words.json.
- A test world in the game must stay out of the Load list (flow I) - game side, after M4.

## Not done here (needs the game / other lanes)

Tab UI (L2 TAB API), worker thread + writing the world + LoadCampaign, hiding test worlds, My Units hand-over (C1),
Windows run. Build outputs and live downloads stay in build_c4/ (git-ignored).
