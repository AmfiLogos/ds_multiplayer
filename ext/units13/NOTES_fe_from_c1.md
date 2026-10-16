# My Units in the Foreign Expedition hub (lane myunits, FE part, 2026-10-15)

## Guard (owner rule: custom units never enter a regular save)
- `fe_manifest_custom(manifest)` (src/fe/fe_save.c, declared in fe_save.h): custom = `source` "wiki" or "library",
  a `library_id`, or a `home_campaign_guid` that is missing / empty / all zeros (with or without dashes).
- `fe_apply_results`: custom units are skipped (report line `<guid8>: custom unit (My Units / wiki link): stays out of
  your saves`) and are left out of the home-campaign check. Every unit custom: refused with
  `These units came from My Units or a wiki link: nothing to bring home (your saves are not changed).`, nothing written
  (no save, no ledger).
- `cmd_apply` (fe_game.c): same refusal BEFORE the backup and the home lookup when all of the player's units are custom;
  the home campaign / source file now come from the first NON-custom manifest. `fe_result_targets` likewise.
- Other save writers in fe_game.c checked: `fe_join_point_save` (writes the hub = mp_sync, a custom mode) and
  `cmd_end` (hub end state, moved out of Saves/) never write a regular save. `fe_apply_run` goes through `cmd_apply`.
- Results window: `fe_result_all_custom(run)` -> the window says nothing comes home and hides "Apply to a new save".

## Test
`python3 ext/foreign_expedition/tests/test_custom_guard.py` (builds build/fe_cli from this branch and
build/fe_cli_base from ~/ds_multiplayer/src/fe = main, both with ASan + UBSan; env FE_BASE_SRC overrides the baseline).
Fixtures: ext/foreign_expedition/fixtures or ~/ds_multiplayer/ext/foreign_expedition/fixtures. 18 checks, all PASS
2026-10-15. Build line: `clang -fsanitize=address,undefined -g -O1 -Wall -D_DARWIN_C_SOURCE src/fe/test/fe_cli.c
src/fe/fe_save.c src/fe/fe_json.c -o ext/foreign_expedition/build/fe_cli`. New fe_cli command: `custom manifests.json`.

## Bring Units: "My Units" tab
- Tabs "From a save" / "My Units" in the left column's header row. mpmenu.h is not in this lane, so the tabs reuse
  MPB_FE_VER_EXP / MPB_FE_VER_HOME (unused in the Bring window); few_sync moves them (place) per window and back.
- My Units: left = "All units" + teams (mu_team_at), right = unit cards (name, Level N, race, its teams), same tick and
  per-player cap. Picking a team ticks its units (first `cap` if the team is bigger, with a note). No portrait: the
  cards are text only today. Empty library: "No units in My Units yet. ..." Load error: the error text.
- Rig command `fe_bring_lib {"ids":"id1,id2"}`: mu_load + mu_entries(lib, ids, n, my uid hex) then the fe_bring path
  (`send_entries`: exports kept in data/fe/exports without "source_file", mail to the host / host_take_entries).
  The library is never saved by this path. Menu entry: `fe_bring_lib()` / `fe_lib_list()` in fe_ui_api.h.
- `mpm_fewin` read-back adds: tab, lib_err, sel_team, teams, lib_picked, lib_units [{id, level}], all_custom.
- Links only once src/myunits.c exists (lead). Syntax-only clang of mpmenu.c, fe_game.c, fe_save.c, fe_cli.c: clean.
