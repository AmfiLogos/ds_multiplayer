# Lane pvprules (owner 2026-10-20): PvP Rules box settings, presets, map options

## For other lanes
- `int pvp_rule_get(const char *id)` (src/pvp_rules.h, strong symbol in src/pvp_rules.c): the host's value after the
  host's rules arrived in this session, else the rule's default; -1 = unknown id. Ids: enemy_vision, enemy_windows,
  enemy_portraits, enemy_hpbars, enemy_status, enemy_viewrange (0/1, default 0), mode, win, round_time, pause,
  vanilla, map (0 here, 1 random, 2 dungeon 1, 3 dungeon 2), floor (0 random, 1-4, 5 boss room).
- `const pvp_map_pick *pvp_map_now(void)`: region, floor index, forced seed, boss, random_starts (map "Random map":
  pvpfix picks random start points from `seed`).
- Sync: the host sends its values + the map pick in mp order type 56 at Start (before SETUP), every peer applies it at
  the same step; join-point block "PVPR" for late joiners and reloads; mixed into the world fingerprint.

## Files
- src/pvp_rules_core.c (pure): rule table, `id=value` text, pvp_rules.txt = `[last]` + `[preset]` sections.
- src/pvp_maps.c (pure): choice -> (region, floor, seed). Dungeon 1 = DungeonRegion VeilSilence (0), dungeon 2 =
  FesteringBurrow (1) (TEXTKEY_SaveLoad_BlockedFesteringBurrowSaveLoad: Festering Burrow = Area 2). 5 floors each
  (GAMESYSTEM_Dungeon_MaxFloor); floor index 4 = phase 5 = Portal, Entry, Boss (data/extra/floor_graphs.json).
  The floor is built by the game (ChangeMapRequested with a forced seed, pvp_game.c arena_go).
- src/pvp_rules.c (glue): file in dsl_mod_data_dir (fallback persistentDataPath/ModData/multiplayer), order, block,
  texts of the new rows, rig commands `pvpr_status|set|preset_save|preset_pick|preset_delete|reload|resolve`.
- pvp_ui.c: host page box = 5 old rows + Map, Floor, 6 visibility rows + Preset row (arrows apply a preset),
  Save as preset... (name field cloned from a stock TMP_InputField on first use: mpmenu_field_new), Delete preset.
  The Esc window's Arena row = the map choice. `mpm_pvp` actions: page_arrow row 0-12, preset_arrow, preset_save_open,
  preset_save {name}, preset_delete.

## Tests
- `make -C src/pvpr_test test`: file round trip (presets, UTF-8 / long names, replace, delete, other versions) and the
  map pick (boss room = floor index 4, random map covers both dungeons and floors 1-4).
- `ext/pvprules/pvprules_pair.py --phase rules` (host only, restart) and `--phase match --map M --floor F` (pair,
  real host-choice flow: PvP world, lobby, Bring, Start).
