# mu_save: units of a save as wiki builds

Files: `custom/units/mu_save.h`, `custom/units/mu_save.c`. Tests: this folder (`run.sh`, `save_test.c`, `builder_import.js`).
It is not in the dylib yet: whoever owns `custom/units/sources.txt` must add `units/mu_save.c`. It needs `fe_json.c` and `fe_save.c`, and those are already in the dylib.

## Which units
This is the Foreign Expedition rule:
- `custom/fe/fe_game.c:579` `fe_list_units`: every guid in `EntityContainerSaveData.UnitContainerSaveData.<side>.PlayerGuidList`, for all sides, in container order.
- `custom/fe/fe_save.c:109-112` `fe_export_unit`: a unit can be brought only when `fe_side_of` finds it (`fe_save.c:49`) and the guid has an entity in `EntityComponentSaveData.EntityComponents`.

`mu_save` adds one thing: a guid listed twice is listed once.
FE does not filter dead units or units on the Dungeon side, so `mu_save` does not filter them either.

File reading is the same as `fe_game.c` `read_json`, which is plain `jload`. The game saves on this Mac (October 2026) are plain JSON, not compressed and not wrapped.
`mu_save_files(dir)` copies the file-name filter of `fe_list_saves` (`fe_game.c:558`): `*.json`, not `mp_sync*`, not `MPFE_*`, not `*SaveHeaderCache*`. It lists newest first and does not parse the files.

## Mapping (save -> wk_build)
This is a C port of the wiki builder's own save import (`builder.js` `dsbExtractUnit`, `dsbSaveUnitToBuild`, `dsbItemRolls`, `dsbResolvePortrait`). The port builds the same builder JSON and then calls `wk_from_json`. So tag steps (1/255), the 48-byte name, retired races and traits, and talent tiers come out exactly as they do for a My Units library build.

| Field | Source in the save | Notes |
|---|---|---|
| race | first race affecter holder | Fallback: the race in the `TEXTKEY_PLAYERNAME_<Race>_` name key. If neither is found, Human is used and it counts as unknown. |
| background | first background holder | |
| level | `ProgressionStats.Level` | Rounded and clamped to 1..12. The list row shows the save's raw level. |
| talents | `Talent<Major>` -1..3 | Becomes Poor..Genius. A missing talent is left unset (Moderate). |
| worn items | Type 6 `ItemSlots[].Key` | Each item goes in its table slot. |
| item bonuses | each item entity's `Stats` | A stat value inside a tag's range = that tag. |
| traits | holders in `D.ind` | inds = the race pool, capped at 4. savedInds = all of them. |
| acquired traits | holders in `WK_ACQ` that are not traits | Addition: the builder's import drops these. |
| inscriptions | `insc` = non-tree inscriptions; `savedInsc` = all | |
| soul stones | holders | |
| inscription trees | `SkillTrees` Main/Sub/Other that are not background trees | Trees with a learned skill come first. |
| learned skills | `LearnedSkills` (all trees), then `ExtraSkills` | Wiki skills only. Item skills and Emergency Escape are left out, the same as the builder. |
| milestone counters | Stack of the milestone affecter | |
| growth (majorBase) | Progression + Generated + Permanent | Elixir uses are taken out as elix. A Mandrake also writes PermanentStats, so the two cannot be told apart. The builder has the same limit. |
| portrait | `UnitPortraitID` when it is in the race's pool | Addition: the builder only guesses from ProfileKey. On the real saves it picked a different face for 6 of 22 units (Elf Unisex bodies -> `_000`). If the ID is not in the pool, the builder's guess is used. |
| name | `UnitNameTextKey` | Either a custom name or the default `TEXTKEY_PLAYERNAME_...` key. The UI must localize the key. |
| hide head | `HideHeadEquipment` | |

Not carried, because wk_build has no field for it:
- The real skill bar (`QuickSlots`). `mu_card_of` computes a bar from the learned skills. The real bar is in `mu_save_build_ex` -> `mu_save_extra.bar`. Wiki skills plus `SKILL_EmergencyEscape` and `SKILL_BasicPunchAttack` are kept. Empty slots are left out.
- Hunger, exhaustion and other state holders.
- Durability.
- Experience.

Unknown keys are skipped and counted (`mu_save_extra.unknown`, `unknown_keys`). These are:
- items, trees, learned skills and bar skills that are not in the tables
- `*Inscription` and `*Soul` holders that are not in the tables
- a missing race

## Cache and threads
One cache entry is kept per path. It is checked by size and by mtime in nanoseconds, and holds up to 3 parsed saves (least recently used is dropped). One mutex guards it. `mu_save_cache_clear()` frees everything.
Parse time is 25-75 ms per 2-5 MB save. After that, building and carding all units of a save takes about 1 ms.

## Tests (`run.sh`, ASan + UBSan, about 3 minutes, mostly node)
1. **list**: 6 real saves (2 backup copies, 4 from instance A, including one MPFE hub). 22 units are listed, built and carded. One line per unit is in `tmp/run/list.txt`.
2. **ref**: `builder_import.js` runs the builder's own import on the same saves. Its build goes through `wk_from_json` and is compared field by field (`wk_json`) with the reader's build. All units are identical, except for the by-design portrait differences, which are printed.
3. **rt** (round trip): random builds that the builder itself made legal (`builder_ref.js gen 120` + `validate`, seeds 5, 11 and 424242), plus the launcher library fixture (30 units). The flow is:
   - `wk_from_json` -> `pb_entry` (the save writer)
   - write one save file with all units
   - `mu_save_units` + `mu_save_build_ex`
   - compare race, background, level, talents, items, learned skills (as a set), inscription trees, traits + acquired traits, inscriptions, souls, portrait, name, hide head, bonuses, the bar written vs the bar read, and the card (`mu_card_json`, order-insensitive). Also `pb_stored_majors` (within 0.005, because the link keeps 1/100).

   Results: 317 of 317 written units are identical, with the same card and the same majors.
   - Skipped builds: the generator's deliberate bogus races, which `wk_from_json` refuses.
   - Not compared: an item with the same bonus picked twice (14 cases). A save keeps one value per stat, so this cannot round-trip.
   - Card order differs: the save keeps skills grouped by tree and holders in the writer's order. So the card's bar, passives and acquired traits can come back in a different order, with the same content.
