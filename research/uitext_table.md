# Player-visible text pass (owner feedback v13 item 2)

Scope: every string our mods show in the game. Full inventories (1 920 strings, file:line, where shown,
slot width): `research/uitext_inventory/strings_{mp,loader,cm}.tsv` (local only, git-ignored). Most strings were already plain and short;
this table lists only the ones that change. The new text went through the /humanizer skill (audit notes at the end).

Rules used: name things the way the game and our menus name them (Join by address, Gear rule, Mods window,
My Units); no file names, no internal words (dsrig, dsload, hook, byte patch, lockstep, module, GameAssembly, bridge,
resync, ip:port, sha256, API names, raw ids); no " - " used as a dash; "they" for players; no build-state hedges.

## Mod descriptions (mod.json, Mods window: 2 lines, about 150 characters before the " ..." cut)

| file | old | new |
|---|---|---|
| LD loader/modules/multiplayer/mod.json:21 | Lockstep multiplayer (dsrig) as a dsload module. Needs DSRIG_DIR (the launcher's profile folder). | Play your worlds with friends. Host one of your saves, or join a friend by address or through Steam. Each player controls their own units. |
| LD loader/modules/gcd-cdr/mod.json:33 | Global cooldown is divided by (1 + cooldown reduction / 100), like skill cooldowns. Runtime hook; the old byte patch (~/ds_skill_truth/patch_gcd_cdr.py) as a code module. | Cooldown reduction also shortens the global cooldown, the same way it shortens skill cooldowns. |
| LD loader/modules/hell-difficulty/mod.json:37 | Hell, a sixth difficulty after Devastation. Code half (in-memory patches + recruit staff strip) and table half (...; your game files are not changed). | Adds Hell, a sixth difficulty above Devastation. On Hell, recruits arrive without their starting staff. Your game files are not changed. |
| CM mod.json:23 | Custom Modes: Foreign Expedition and PvP (need Multiplayer; hidden without it). Later: My Units, Gallery, Test Fights, Unit Builder. | Adds My Units, Unit Builder, Gallery and Test Fights to the title screen. With Multiplayer on, you can also host Foreign Expedition and PvP. |

## Mod loader (ds_modloader)

| file | old | new |
|---|---|---|
| loader/ui/dsl_win.c:227 | The group could not be deleted (see dsload.log). | The group could not be deleted. |
| loader/ui/dsl_win.c:277 | The group could not be saved (see dsload.log). | The group could not be saved. |
| loader/ui/dsl_win.c:289 | The mods for this world could not be saved (see dsload.log). | The mods for this world could not be saved. |
| loader/ui/dsl_win.c:295 | refused (see dsload.log) | the game did not allow the change |
| loader/ui/dsl_win.c:382 | the restart did not finish (see dsload.log). | the restart did not finish. |
| loader/src/dsl_soft.c:419 | could not start B | the restart could not begin |
| loader/src/dsl_soft.c:432 | B exited before the swap | the restarted game closed before it was ready |
| loader/src/dsl_soft.c:433 | timeout | the restart took too long |
| loader/src/dsl_reg.c:370 | cannot write %s | (path) -> the mod settings could not be saved |
| loader/src/dsl_reg.c:382 | %s mods need the Dungeon Settlers Modded app (no %s folder in this start) | %s mods need the Dungeon Settlers Modded app |
| loader/src/dsl_reg.c:113 | BepInEx did not start (see starter.log) | BepInEx did not start |
| loader/src/dsl_reg.c:126 | not loaded (see dsload.log) | did not load |
| loader/src/dsl_main.c:232 | not loaded (data mod, or not for this game build) | not loaded: not made for this game version or this computer |
| loader/src/dsl_main.c:232 | failed to start (see dsload.log) | it crashed while starting |
| loader/src/dsl_main.c:233 | not loaded (game build, requirement, conflict or crash guard: see dsload.log) | not loaded: it needs another mod, clashes with one, or crashed last time |
| loader/src/dsl_main.c:701 | The game loaded a different GameAssembly (%s) than the one checked at start (build %s). Mods stay off. | The game files changed after the start check. Mods stay off. Restart the game. |
| loader/src/dsl_main.c:740-741 | The game was updated (GameAssembly %s is not a build this loader knows%s%s). Mods stay off until they are updated for this game version. | The game was updated. Mods stay off until they are updated for this game version. |
| loader/src/dsl_main.c:746 | This copy of the game is build %s, but the launcher pinned build %s for this world. Mods stay off. Start the world from the launcher again. | This game is build %s, but the launcher started this world for build %s. Mods stay off. Start the world from the launcher again. |
| loader/start/win/bie_win.c:131 | The %s download is not the expected file (sha256 %s). | The %s download is damaged. Try again. |
| loader/start/win/dsl_bie_win.c:40, dsl_ml_win.c:40 | no thread for the download | the download could not start |

## Multiplayer (ds_multiplayer). mpmenu.c / mpmenu_model.c rows wait for the mpfix merge.

| file | old | new |
|---|---|---|
| src/mp_game.c:950 | This is %s's unit: the host's gear rule lets each player equip only their own units | This is %s's unit. The host's Gear rule lets you equip only your own units |
| src/ord_game.c:614 | You cannot equip this unit (gear rule) | The host's Gear rule does not let you equip this unit |
| src/mpmenu.c:550 | ip:port, e.g. 192.168.1.20:47777 | Address, like 192.168.1.20:47777 (fits the old 33-character field) |
| src/mpmenu.c:1632 | Not an address: "%.40s" (use ip:port) | "%.40s" is not an address. It looks like 192.168.1.20:47777 |
| src/mpmenu.c:2089 | Watching through Steam is not in this build. | Spectators cannot join through Steam. |
| src/mpmenu.c:2622, 2623 | Copies your address (ip:port) so you can send it to a friend. They use Join by Address. | Copies your address so you can send it to a friend. They paste it in Join by address. |
| src/mpmenu.c:2626, ui_game.c:5024, ui_teams.c:1985 | Poor connection, or loading, or resyncing. | Slow connection, or the game is loading or catching up. |
| src/mpmenu.c:2657 | Join a friend's world with their address (ip:port). Use it when Steam is not available, or on the same home network. | Join a friend's world with their address. Use it without Steam, or on the same home network. |
| src/mpmenu_model.c:22 | Each player can recruit up to the clan limit for himself | Each player can recruit up to the full clan limit |
| src/mpmenu_model.c:412 | Host address (ip:port) | Host address |
| src/mpmenu_model.c:437 | Copy the host's address (ip:port), then press Connect | Copy the host's address, then press Connect |
| src/mpmenu_model.c:455 | Connect (address from the clipboard) | Connect with the copied address |
| src/net/ds_modcheck.h:106 | This host uses the Vanilla check: no gameplay mods. Turn off: | This host allows no gameplay mods. Turn off: |
| src/net/dsnet_server.c:230 | the host's match is vanilla only, but the host runs gameplay mods (%s) | the host allows no gameplay mods but has some on (%s) |
| src/net/dsnet_server.c:231 | this match is vanilla only: turn off your gameplay mods (%s) in the launcher | this game allows no gameplay mods: turn off %s in the Mods window |
| src/net/dsnet_client.c:544 | the multiplayer version is different from the host's: both players need the same Dungeon Settlers MP app | your Multiplayer mod is a different version from the host's: both players need the same version |
| src/net/dsnet_client.c:546 | the mod version is different from the host's: both players need the same Dungeon Settlers MP app | your mods are a different version from the host's: both players need the same Dungeon Settlers Modded version |
| src/net/dsnet_client.c:547 | your Mac does math differently from the host's Mac, so the two games would drift apart | your computer does the game's math differently from the host's, so the two games would not stay the same |
| src/net/steam/dsnet_steam.c:519 | could not enter lobby (response %u) | could not enter the host's Steam lobby |
| src/net/steam/dsnet_steam.c:531 | lobby has no other owner | the host left the Steam lobby |
| src/net/steam/dsnet_steam.c:580 | could not reach the host over Steam (state %d) | could not reach the host through Steam |
| src/net/steam/dsnet_steam.c:597 | ConnectP2P failed | Steam could not connect you to the host |
| src/net/steam/dsnet_steam.c:758 | JoinLobby failed | Steam could not open the host's lobby |
| src/net/steam/dsnet_steam.c:777 | no bridge | the connection could not be set up on this computer |
| src/ui_game.c:1035 | A map change vote (no prompt window: counted as yes) | A map change vote (counted as yes) |
| src/ui_game.c:4995 | Desync report saved: %s | The game went out of sync. A report was saved: %s |
| src/ui_game.c:5065 | On: your loading screen ends as soon as the world is ready, without the game's 1.5 to 3.5 second settle wait. Joins, resyncs and reloads finish sooner; the first moments can stutter. Only your own loads change, never the game. | On: your loading screen closes as soon as the world is ready, 1.5 to 3.5 seconds sooner. Joins and reloads finish faster, but the first moments can stutter. Only your own screen changes. |
| src/ui_model.c:143 | Resyncing with host... | Catching up with the host... |
| src/ui_model.c:144 | Resyncing with host... (loading) | Catching up with the host... (loading) |
| src/ui_model.c:453 | Your game went out of sync: reloading everyone from %s's save | Your game went out of sync. Everyone reloads %s's save |
| src/ui_model.c:454 | %s's game went out of sync: reloading everyone from %s's save | %s's game went out of sync. Everyone reloads %s's save |
| src/ui_teams.c:1296 | Ready is not available in this build | Ready does not work with this Multiplayer version. Update the mod |

## Custom Modes (ds_custommodes)

| file | old | new |
|---|---|---|
| pvp/pvp_ui.c:108 | Dungeon floor %d (tested) | Dungeon floor %d |
| pvp/pvp_ui.c:655 | Opens the PvP match setup: two teams of players fight each other on a dungeon floor with no monsters. The world comes back as it was after the match. | Opens the PvP match setup, where two teams of players fight each other. After the match, the world is back as it was. |
| pvp/pvp_ui.c:665 | The host clicks a player to move him to the other team. Each player brings the units he owns. | The host clicks a player to move them to the other team. Each player brings the units they own. |
| pvp/pvp_ui.c:477 | Could not bring the units: %s (raw JSON reply) | Could not bring the units: %s (the reply's error text only, not the JSON) |
| pvp/pvp_ui.c:830 | The match could not start (%d). | The match could not start. Try again. |
| pvp/pvp_game.c:939 | Planning pause - give your orders, then press Ready | Planning pause: give your orders, then press Ready |
| pvp/pvp_mods.c:148 | Client mods are fine. Gameplay mods: none allowed; turn off %s. | Mods that only change your screen are fine. Gameplay mods are not: turn off %s. |
| pvp/pvp_modcheck.c:83 | ... %s must turn %s off (Switch to vanilla profile in the launcher). | ... %s must turn %s off in the Mods window. |
| fights/tf_ui.c:436 | A practice %s with %s durability that taunts you - hit it and read your squad's damage. | A practice %s with %s durability that taunts your units. Hit it to see how much damage your squad does. |
| fights/tf_ui.c:511 | No kit ticked - the squad walks in with nothing but its gear. | No kit ticked: the squad walks in with only its gear. |
| builder/bld_ui.c:348 | Pick a background first - it decides which trees this unit has. | Pick a background first. It decides which skill trees this unit has. |
| fights/sg_cache.c:137 | Test Fights needs the wiki's builder files once: %s (doubled the caller's sentence) | %s |
| fights/sg_cache.c:109 | the wiki answered %d for %s | the wiki did not send its files (error %d) |
| fights/sg_cache.c:113 | the wiki's builder files do not carry a build tag | the wiki's files do not say which game version they are for |
| fights/sg_cache.c:119 | could not write %s (%s) | could not save the wiki's files on this computer (%s) |
| fights/sg_cache.c:189 | the save template %s arrived incomplete | a file from the wiki arrived incomplete |
| fights/sg_cache.c:189 | the wiki has no save template %s (%d) | the wiki is missing a file for this fight |
| fights/tf_ui.c:183, 192 | The wiki's save generator did not start: %s (script name + JS stack) | Test Fights could not start the wiki's fight maker. Try again later. (detail to the log) |
| fights/tf_ui.c:226 | %s (generator error) | %s |
| builder/bld_ui.c:50 | The wiki's Unit Builder did not start: %s (JS stack) | The wiki's Unit Builder could not start. Try again later. (detail to the log) |
| builder/bld_ui.c:171 | The build could not be read: %s (parser error) | This build could not be read. |
| builder/bld_ui.c:220 | %s (model error) | %s |
| builder/bld_glue.js:15 | The wiki builder no longer has ' + decl + ' - the Unit Builder needs an update. | The wiki's Unit Builder changed. This mod needs an update to use it. |
| fights/sg_glue.js:26 | The wiki builder no longer has ' + decl + ' - Test Fights needs an update. | The wiki changed. Test Fights needs an update to use it. |
| builder/bld_glue.js:28, fights/sg_glue.js:39 | Could not read ' + decl + ' from the wiki builder. | Could not read the wiki's builder files. |
| fights/cm_http.c:62-67, units/wk_http.c:28-33 | WinHttpOpen / WinHttpConnect / WinHttpOpenRequest failed (%lu) | could not connect (error %lu) |
| fights/cm_http.c:77, units/wk_http.c:37 | WinHTTP error %lu | connection error %lu |
| fights/cm_http.c:84 | read failed (%lu) | the download broke off (error %lu) |
| fights/cm_http.c:86, 130 | answer larger than %zu bytes | the wiki sent more data than expected |
| fights/cm_http.c:112, units/wk_http.c:96 | libcurl not loadable (: %s) | the system's download library is missing |
| fights/cm_http.c:119, units/wk_http.c:103 | libcurl: missing functions | the system's download library is missing |
| fights/cm_http.c:120, units/wk_http.c:104 | curl_easy_init failed | the download could not start |
| fights/cm_http.c:133 | curl error | connection error |
| units/wk_http.c:138 | CFReadStreamCreateForHTTPRequest failed | the download could not start |
| units/wk_http.c:156 | %s (%s %ld) (error domain + code) | %s (error %ld) |
| units/wk_http.c:161 | no HTTP answer | the server did not answer |
| units/wk_link.c:140 | This link is damaged or cut short (%s). Copy it again. | This link is damaged or cut short. Copy it again. |
| units/wk_link.c:174 | The wiki gave an unexpected answer for this short link (HTTP %d). Try again later, or open it on the wiki and copy the long link. | The wiki did not open this short link (error %d). Try again later, or copy the long link on the wiki. |
| gallery/gallery.c:181 | the gallery answered %d | the Gallery did not answer (error %d) |
| units/myunits.c:74 | My Units file is damaged (%s): %s (path + parser error) | The My Units file is damaged and could not be read: %s (path only) |
| units/myunits.c:75 | My Units file has another format: %s | The My Units file is not in the right format: %s |
| units/myunits.c:76 | My Units file was written by a newer launcher (version %d) | The My Units file comes from a newer version of this mod. Update the mod. |
| units/myunits.c:210, 239 | unit %s is not in My Units any more (16-hex id) | this unit is not in My Units any more |
| units/mu_save.c:63 | The save could not be read (%s). (parser error) | The save could not be read. It may be damaged. |
| fe/fe_save.c:112 | unit %s is not a clan unit of this save (guid) | a unit is not in this save's clan |
| fe/fe_save.c:141 | item %s of unit %s has no entity in this save | an item of one unit is missing from this save |
| fe/fe_save.c:580 | these results are damaged (hash mismatch) | these results are damaged |
| fe/fe_save.c:602 | Already brought home (marker in this save). | These results are already in this save. |
| fe/fe_save.c:694 | the merged save failed the reference check: %s | the new save failed its safety check, so it was not written (details in the log) |
| fe/fe_save.c:697 | could not write %s (exists already?) | could not write the new save (a save with that name may exist already) |

Not changed on purpose: internal guards that the normal game never reaches (cm_http "bad url", "only %s is allowed",
myunits "internal: ...", gallery "bad gallery id", pvp_units fallback JSON), the sg_prelude browser error names
(code reads them), the fe_json / fe_check_refs / pvp_wiki / pvp_build detail lines (no longer shown: the callers above
stop printing them; they still go to the log), and the hover cards that are long but plain (Rules rows).

## /humanizer audit

Patterns found in the old text and removed: dash-chained fragments (#25, " - " joins in 4 strings), build-state hedges
(#26, "not in this build", "Later: ..."), internal names and file names in place of plain words, "he/him" for players.
Second pass ("what still reads as AI?") on the new text: the Custom Modes description was a verbless list of features
in one draft (#27); it is now two sentences with verbs. The Hell line "new recruits come without their staff" was vague
about which recruits; now "recruits arrive without their starting staff". No em dashes, no promotional words.
Mod descriptions measured: 138, 95, 136 and 137 characters (cut at 150).
