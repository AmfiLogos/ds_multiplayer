# Lane i18n_mpgame: in-game multiplayer text through TR / TRN (2026-10-17)

Branch `i18n-game` (from main bea54f9). Files: `src/ui_game.c`, `src/ui_teams.c`, `src/ui_model.c` (+ `.h`),
`src/ui_tip.c`, new `src/ui_utf8.h`, `src/ds_i18n.h` (shared core, copied unchanged), the ui tests.
The menu lane (`i18n-menu`) owns mpmenu*, mp_*, ord_*, net/** and the one `DSI_IMPLEMENTATION` of the dylib.

## How it was done (re-runnable)
- `tools/i18n_wrap.py` + `tools/i18n_rules/<file>.py`. Every change in the four .c files comes from the rules:
  `git checkout main -- src/ui_game.c src/ui_teams.c src/ui_model.c src/ui_tip.c && python3 tools/i18n_wrap.py`
  gives the same files (a second run says "no change"; `--check` exits 1 when something would change).
  On a newer main: run it, read the `MISS` lines (an anchor moved: fix that rule), build, read the diff.
- Generic part: the string literals in the arguments of the text sinks get `TR()` (`notice`, `snprintf` format and
  its literal arguments, `tip_at`, `tip_on`, `tmp_set`, `line_text`, `plist_set`, `set_text`, `center_msg`).
  A format with no other argument becomes `"%s", TR("...")`. `TR("<color=#rrggbb>Word</color>")` becomes
  `ui_col("#rrggbb", TR("Word"))`, so the colour stays out of the translated text (45 in ui_game, more in ui_teams).
  `·` and friends inside TR texts are written as the UTF-8 character (see CORE CHANGE).
- Hand rules (EDITS): whole sentences instead of glued pieces, plurals, tables, by-value fixes, helpers.
- `.gitignore`: `!/tools/` added (tools/ was ignored by `/*`).

## What is wrapped
About 490 keys from these files (`tools/i18n_tool.py extract` on them: 492). ui_game 235 TR sites, ui_teams 129,
ui_model 73. Status lines, HUD tags, notices, tooltips (titles + bodies, the 18-row cog table), player list and
cog rows, spectator / viewer banner, lend menu, minimap, pings, population line, Teams screen and Going panel.
- Glued pieces made into one format each: "%s belongs to %s (Admin mode lets the host take it)" / "%s belongs to
  %s"; "Watching %s's world" / "Watching %s's expedition"; "Population full (%d/%d): host rule %s" / "Population
  full for you (...)"; "Waiting for %s (loading)", "Waiting for %s (%d of %d agree)", "%s said no: staying here" /
  "... staying on floor %d" / "The vote said no: ..."; " · %d s, then you decide" / " · %d s, then %s decides";
  "%s, away" (owner tags); ui_teams: "Region", "Waiting for: %s", "No answer from %s: expedition cancelled" ...
- Plurals (TRN): "%s wants %s · press >> to agree" / "%s want ..."; "sees this too" / "see this too"; "looks at the
  same part of the map as you." / "look at ..."; ui_teams "%d unit · max %d" / "%d units · max %d".
- Tables keep English with TR_NOOP and translate where drawn: cog tooltip rows, on/off rows, colour names
  (ui_model `color_names[]`, `ui_palette_name` returns TR), region names (fallback).

## Not wrapped, on purpose
- rig_log lines, rig/test commands and their JSON, file paths, class / sprite / TEXTKEY names, strcmp'd test words.
- `ui_default_player_name()` "Player": it is the default player NAME sent to the others (content, not UI).
- The red "X" disconnected marks, the dot and arrow glyphs, "✓" (a glyph; its fallback "yes" is TR).
- ui_teams: `region_line` (log/test text), the 22x22 "RUN" fallback tag, `side_s`, `ui_teams_model.c tm_header_text`
  (only the tests use it).
- Translator notes: keys with spaces at the edges are on purpose (", ", " and ", " (you)", " · %d s, then ..."):
  keep or drop the spaces as the language needs. Fragments shown alone: "resyncing", "loading", "catching up",
  "in sync", "Per player", "Split equally", "Multiplied", "Shared" (host Population rule names; the menu lane has
  the same words in its rule tables), "go down", "enter the dungeon" ... (verb phrases after "%s wants to %s.").

## Game words
Standalone labels now come from the game (GetText, Mac 0x80EBAC, Windows 0x5A3490 by win_map; our English when the
key is missing; cached per `dsi_generation()`):
| English | TEXTKEY | where |
|---|---|---|
| Next | TEXTKEY_Shared_Next | ui_teams stock expedition button (was "Next") |
| region names (7) | TEXTKEY_ExpeditionPlanUI_{VeilSilence, FesteringBurrow, AbandonedMine, ForgottenAbyss, DeadSwamp, TowerApocalypse, Abyss}_NAME | ui_teams `region_name()`, DungeonRegion order 0..6. English now shows the game's own names ("The Veil of Silence", "The Swamp of Death") |
| Cancel | TEXTKEY_CommonPopUp_Cancel | ui_game map vote panel button |
| Settlement | TEXTKEY_MAPTYPE_Base | ui_game minimap floor label |
| Portal | TEXTKEY_DungeonResultUI_Portal | ui_game tooltip title (result pill) |
| Save Campaign | TEXTKEY_SystemMenu_SaveCampaign | ui_game tooltip title (guest Esc menu) |

Game words INSIDE our sentences (stay in TR text; translators should use the game's term):
| English | TEXTKEY (French as a check) |
|---|---|
| settlement ("return to the settlement") | TEXTKEY_MAPTYPE_Base (Colonie) |
| dungeon ("enter the dungeon") | TEXTKEY_Shared_Dungeon_NAME |
| go down | TEXTKEY_DungeonResultUI_GoDown (Descendre) |
| Portal ("closed the Portal screen") | TEXTKEY_DungeonResultUI_Portal (Portail) |
| Population ("host's Population rule") | TEXTKEY_Shared_Population_NAME (Population) |
| Recruit / Recruiting | TEXTKEY_RecruitUI_Recruit (Recrutement) |
| Expedition (Plan) | TEXTKEY_ExpeditionPlanUI_Expedition |
| Event ("Event Choices" rule) | TEXTKEY_EventNodeUI_NAME (Événement) |
| Save Campaign | TEXTKEY_SystemMenu_SaveCampaign |
| floor ("Floor %d", "go down to floor %d") | no key in this sense (TEXTKEY_BuildingSubType_Floor is a floor tile: do not use) |
"Go to dungeon" has no key in the English table. Our own names (Teams, Players, Spectator, Units per Player,
Admin Mode, Fast join, Keep playing ...) are ours: one glossary with the menu lane.

## By-value fixes (i18n_sites 2d)
- `ui_game.c` save button `strstr(cur, "Only the host")`: now a state flag `save_label_ours` (1 = this build wrote
  its own text into the stock Save label). Nothing writes that label now, so the old English check of a label from
  an older build is gone (older builds' labels do not survive a game restart).
- `ui_game.c` tooltip `strstr(status_text, "press >>")`: now by state. `ui_model.c ui_status_line_agree()` = the
  last `ui_status_line` wrote the "press >> to agree" line; `status_frame` clears it when a hold line replaces it.
  Unit test: `ui_test/test_ui.c test_i18n` runs the line in French (`ui_test/lang_test/fr.json`, 3 test keys).
- ui_teams "Send to friends" / "Send the proposal to your friends anyway?" / "Next": the stock text is compared with
  the same translated (or game) text we write, so the rewrite check works in every language.

## UTF-8 (blank labels)
- `src/ui_utf8.h ui_str_new`: valid text goes to il2cpp_string_new as is; broken text is copied, invalid bytes are
  dropped and a cut character at the end too (`dsi_utf8_fix_end`). The `u_str` of ui_game, ui_teams and ui_tip use it.
- `dsi_utf8_fix_end` after the byte-capped composes: `notice` b[256] (both files), `reserved_name` (nm[32]), owner
  tags `tag[96]`, `ui_col` buffers, ui_tip `cur_title[160]` / `cur_body[700]`, ui_teams name lines.
- ui_teams `str_utf8`: surrogate pairs now (a lone surrogate -> U+FFFD), no longer BMP only.

## Fonts and language change
- ui_teams: the templates come from prefab assets (serialized Lora / Cinzel, never changed by the language), so
  taking them again would change nothing. Every Teams text now asks `LanguageSetting.GetFont` (Mac 0x898A28, Windows
  0x61DF50 by win_map; EasyRead 0, VeryDecorative 2) with that font's own material (tmp_plain), template font as the
  fallback. NOT run in the game yet (no game runs in this lane): check the Teams screen in English (should look the
  same) and in Japanese / Chinese at the game test.
- ui_game / ui_tip: no captured fonts (clones keep the stock DASText font; tooltips are the game's own view).
- Live language change: the language can change only on the title screen; all text of these files is built in a
  campaign, every frame or on open, so nothing needs a redraw hook. The game-text caches key on `dsi_generation()`.

## Tests and builds (English, no language file)
- `ext/merger13/tools/unit.sh`: net, mp_test, mpmenu, trn, ui (542 checks), teams (437097), pvp, guard: all rc=0.
- `ui_test/dsi_impl_test.c` gives the test builds their DSI_IMPLEMENTATION (ui_test and teams_test Makefiles).
  It is not in `src/` so neither src/build.sh nor gen_port (src/*.c) picks it up: no second definition in the dylib.
- The dylib needs the menu lane's DSI_IMPLEMENTATION file. Link test here with a temporary untracked
  `src/zz_dsi_local.c` (never committed, moved out after): Mac `src/build.sh` list + that file: built, 0 warnings;
  Windows `win/build_win.sh` (baselib) and `DSMP_TARGET=module`: built, rc=0.
- `win/win_map.tsv` is rewritten by build_win.sh (new rows for GetFont, caller lists): not committed (generated; a
  merge rebuild makes it again).

## Left / for later
- The language poll and `dsi_set_dir` are the menu lane's. Until that branch is merged this branch does not link.
- Game test (later lane): Teams screen fonts in ja / zh, the region names from GetText, one long CJK Steam name in
  owner tags and the player list (should never blank now).
