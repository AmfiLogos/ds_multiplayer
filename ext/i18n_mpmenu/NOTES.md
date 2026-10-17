# i18n lane: Multiplayer menu side (branch i18n-menu)

Shared core: `src/ds_i18n.h` and `tools/i18n_tool.py`, copied unchanged. `src/ds_i18n.c` is the one `DSI_IMPLEMENTATION`
of multiplayer.dylib / .dll (and of dsrig.dylib). It is in `src/build.sh` (the loader reads its source list from there),
and Windows picks it up by itself (gen_port.py takes every `src/*.c`). The tests that use translated text link it too:
`mpmenu_test`, `mp_test`, `net/` (all tests, asan too) and `net/steam`. `ui_test` and `teams_test` link none of my files,
so the other lane's own `DSI_IMPLEMENTATION` in its harness gives no duplicate.
`.gitignore` (root whitelist): `!/tools/` and `!/lang/` added. `lang/en.json` is NOT committed (the lead extracts after
the merge). A local extract of this branch gives 388 keys.

## Runtime (mpmenu.c)
- Language files: `<folder of multiplayer.dylib/.dll>/lang/<code>.json` (dladdr, as rig_det.c does). `DSMP_LANG_DIR`
  overrides it (tests). No folder = English. Windows module build: `win/build_win.sh` copies repo `lang/*.json` into
  `build/mods/multiplayer/lang/`. They are not in mod.json's files list, so they are not hashed. The Mac loader package
  (ds_launcher `loader/build.sh`) must copy `lang/` the same way: that is the loader's repo, not done here.
- Poll: `lang_poll()` once per `mpmenu_frame`. It resolves `Refactor.Setting.LanguageSetting.GetCurrentLanguage` by name
  and calls it only when the class's static fields exist. Then `dsi_set_lang`. Before that: English.
- On a change on the title screen: all title clones are destroyed (column rows, Recent worlds, Rules / Expedition Rules /
  host-choice panels, address + name fields, the load-list toggle, the "Remove tag" texts). `title_sync` rebuilds them on
  the same frame in the new language and fonts. Campaign UI (lobby, Esc buttons, chat) is built per scene, and the
  language cannot change in a campaign. The other lane only compares `dsi_generation()`.
- Fonts: `LanguageSetting.GetFont(style)` by name (0 = body, 2 = titles). It is cached per `dsi_generation()` and called
  only after the game has loaded `_fontSO`. Body material = the font asset's own material (as before: no outline).
  Fallbacks: the old sources (Txt_Notice font, Cinzel found by name). `txt_style(cinzel=1)` (exported, also used by
  Custom Modes) now gets the language's title font. The lobby title uses it too.
- Game words (`mpm_word` in the model, `game_word` on the game side: `TextKeyExtensions.GetText` by name, cached per
  language, only on the title / in a world; "TextKey not found" = our English). Labels that are game terms:

| English | TEXTKEY | where |
|---|---|---|
| Population | TEXTKEY_Shared_Population_NAME | Rules row label + tooltip title |
| Experience | TEXTKEY_Shared_EXP_NAME | Expedition Rules row label |
| Start | TEXTKEY_Common_Start | host page Start, lobby Start, its tooltip title |
| Back | TEXTKEY_Common_Back | title Back, its tooltip title |
| Cancel | TEXTKEY_CommonPopUp_Cancel | Cancel join, every box's Cancel |
| Load Campaign | TEXTKEY_SystemMenu_LoadCampaign | Esc tooltip title (the stock button's own label first) |

  Game words inside our own sentences (left in TR text; translators: use the game's term): Storage
  (TEXTKEY_BuildingType_Storage, "Own storage", "settlement storage"), Recruit (TEXTKEY_RecruitUI_Recruit,
  "Recruiting", "Recruits"), Dungeon (TEXTKEY_Shared_Dungeon_NAME, "Dungeon loot"), Portal
  (TEXTKEY_DungeonResultUI_Portal, "the Portal screen"), Level (TEXTKEY_Shared_Level_NAME, "levels"), Event
  (TEXTKEY_EventNodeUI_NAME, "Event choices"), Experience (TEXTKEY_Shared_EXP_NAME, "experience gained"), Population
  ("Population full", "population limit"), Load (TEXTKEY_SaveLoad_Load, "the load list"), Save (TEXTKEY_Common_Save).
  "Gear", "Clan", "New Campaign" have no row of their own in the table: our words.

## Compared by value: fixed
- Session ends (mpmenu_model.c ended / PLAYING / GUEST_JOINING): `mpm_obs.end` (new field, appended) = `MPM_END_*`
  (mpmenu.h). mp_game keeps the code beside the text (`end_session_c`, `mp_steam_end_code()`, new export; the struct
  `mp_steam_info` is unchanged because Custom Modes allocates it). The "Could not join: " start that the notice drops is
  found from the same `TR("Could not join: %s")`, in any language. Test command `mpm_fail_join` maps its English test
  text to the code. mpmenu_test: every message case sets the code, plus one French test (any text, the code decides; the
  translated start is dropped; a bad entry is dropped).
- mpmenu.c "End the game" box: `leave_box_text` returns the number of other players, and the button follows that.
- Friend tooltip: the name comes from `M.friends[i]`, not out of the label.
- "Leave multiplayer" (old-build label on the Esc Load button): checks the English and the TR form.
- Load-list tag `[Multiplayer]`: found and removed by its colour tag, in any language. Recent worlds "the host" stays stored
  data in English (the history file), shown through TR (`hist_shown_host`).
- Read-back (`str_utf8`, exported) is now full UTF-8. It was ASCII + '?', so a translated stock label (the greyed
  "Load Campaign") or a Korean / Chinese world name read back and written again came out as "???".

## UTF-8
- `tmp_set` (exported) and every il2cpp string of our boxes / notices / fields go through `u_str_ok` (drops a broken
  end on a copy: no blank labels). `mpmenu_kit.h`'s inline `u_str` is unchanged (Custom Modes compiles it).
- Names on the wire stay 32 bytes and are cut at a character (`utf8_len31` in dsnet_internal.h, `dsi_utf8_fix_end`):
  dsnet_server.c HELLO name, chat name, spectator list; dsnet_client.c chat / join-command / spectator names, world name
  in and out, HELLO name. New net test `utf8_names` (11 CJK characters -> 10, on every peer and in chat).

## Net and Steam texts
- `dsnet_reject_text`: TR on the receiver (codes on the wire, as before). MODSET: the host's own words only in English;
  otherwise our text (the game side builds the detailed list itself from the host's mod lines).
- The server's refusal words stay English on the wire (`dsnet_release_text_en`, server thread). `dsnet_release_text` is
  for display (TR).
- Steam join errors: they are set on the pump thread, so they keep an English key + arguments (`jfail`), and
  `dsnet_steam_join_error_text()` (new, main thread) translates them. The Windows stub has it too.
- `net/ds_modcheck.h` (shared with the loader's copy): the guest text reaches the player through mp, so it is wrapped with
  `DSMC_TR(TR_NOOP(...))`. `DSMC_TR` is the identity unless the includer defines it (mp_game.c defines it as TR). The
  loader's copy is not touched; the host one-liner `dsmc_host_text` (not used by mp) stays English.

## Custom Modes (CM) and the loader
- Host choices and Esc buttons registered by CM are drawn as registered. CM must register them again, translated, when
  `dsi_generation()` changes (its lane). Our own placeholder "Foreign Expedition" choice is registered again on a change
  (only while CM has not replaced it).
- `mpmenu_kit.h`: no API change. `txt_style`, `str_utf8` and `tmp_set` behave better for CM (language fonts, full UTF-8,
  safe ends).
- Loader: `dsl_ui.c` sorts the title buttons by `strcmp(label, "Multiplayer")`. Once "Multiplayer" is translated, that
  look-up needs the owner id (loader lane, i18n_sites.md 2d).
- `mp_api` busy reason ("a multiplayer game is running"): TR (same process as the loader, same language).

## Not wrapped (on purpose)
Logs, rig/test commands and their read-backs, desync report text, file names, JSON keys, the server's wire words, Steam
`g_err` (logs only), the "x" and arrow glyphs, the dsrig-only "Steam bridge not ported" stub text.

## Checks run
Mac dylib + Custom Modes build (no new warning), Windows baselib and module builds, unit.sh (net, mp_test, mpmenu, trn,
ui, teams, pvp, guard: all rc=0), `make -C src/net asan` all pass, ord_test OK, net/steam build. No game run (lane rule).
