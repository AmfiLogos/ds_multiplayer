# dsuxmp notes

## uxmp-hud

Scope: M7 (Bring Units window under our Teams button) and M5 (our HUD windows follow the game's UI Scale) of
`UX_REVIEW_2026-10-25.md`, files `src/ui_game.c`, `ui_teams.c`, `ui_friends.c`, `mu_ui.c`, `mu_bring.c`.

Decisions (question / pick / why):
- M7: hide our HUD under the card window, or draw the window above it? Pick: the window above. The game's own windows
  sit above its HUD (the portrait bar, clock and build menu stay visible under a popup and take no click through it).
  So the Teams button goes under the loader kit's layer while a card window is open (canvas order 30800 < 30900; else 31000
  as before), and none of our HUD takes a click, drag or hover on the window's rect (`mu_bring_hit`, asked by
  `ui_game.c ui_hit` for every Players panel / menu / viewer box / mini overview / off-screen mark hit test, and by
  `ui_teams.c hud_hit` for the Teams button). The button stays where it is; nothing comes and goes.
- M5: how to scale the Players panel and the other boxes of `ui_game.c`? Pick: the whole top layer (Players panel + cog
  rows, lend / player menu, viewer box, "sees this too" line, spectator mini overview, off-screen marks, our tooltip box)
  gets localScale = the game's UI scale (`mu_ui_scale`, read about 3 times a second in `plist_frame`). Why: every box on
  that layer already places itself in layer units through `scr_to_layer` / `pl_scale` (the layer's lossy scale), so places,
  drags, keep-inside and hit tests follow with no per-box change. The vote strip (root canvas, layer units) is drawn at the
  same scale. The panel's top under the portrait bar is converted from canvas to layer units (`pl_y0`).
- M5 fit: the panel (open cog list at 960x540, UI 1.4) is drawn smaller only as much as needed to fit from its top down to
  the screen's bottom with 16 px free; its top may go up to the portrait bar's bottom, never over the game's HUD (clock,
  resources), unless the player dragged it higher. Before, a too-tall panel was pushed up over the top-left HUD.
- M5 Teams button (`ui_teams.c`): drawn at the UI scale too (it sits right of the panel, which grows); its place right of
  the panel converts the panel's layer units to canvas units (`ui_layer_scale`).
- M5 Teams window: verified, it already follows the UI scale (`Refactor.UIScaleRoot`) and fits the screen (`win_fit`): review
  shots en_1728x1000 26_teams_host: window 1215 px wide at UI 1.0, 1430 px at 1.4 (fit-clamped), texts 1.17x. Not changed.
  The proposal side panel has UIScaleRoot too. The game's tooltip and notices are the game's own.
- M5 Steam friends picker (`ui_friends.c`): drawn at the UI scale, then scaled down to fit with 16 px free (the lobby
  window's rule in `mpmenu.c`).

Test: `ext/dsuxmp/hud_test.py` (one hidden loader copy UXH, `hud_make_instance.sh UXH` once). The multiplayer build under
test needs `-DTEAMS_MOUSE_TEST` (fake mouse through the game's own `UnityEngine.Input`). "Old" = origin/main plus only the
test probes (`hud_probes_on_main.patch`: the `ui_scale_probe` command, the drawn rects in `mpm_bring_state`,
`mpm_friends`, the Teams button's canvas order in `ui_teams`); no behaviour change.

## uxmp-text

Fixes M4, M8, M11 (+ Title Case), M12, M13 of `UX_REVIEW_2026-10-25.md`. Branch `dsuxmp-text`.

### What changed
- M4 `mpmenu_model.c` mpm_list_mode: "Your save" > "Host from a Save" with no world that has the Multiplayer tag in the
  open Load list (a first-time host) = every world shows (MPM_LIST_ALL): no toggle, no "N single-player worlds are
  hidden" line. With one or more multiplayer worlds: the multiplayer list as before. The count is new model field
  `mp_worlds` (appended at the end of mpm_model), set by `mpmenu.c` lf_apply before it picks the mode; rig.log line
  "load list filter (...): mode M (N multiplayer world(s))".
- M8: the host's Esc menu button says "Turn Off Multiplayer" (the lobby's word, owner 2026-10-20); a guest keeps
  "Leave Multiplayer". New model field `role` (last obs.role). The host's box in play = the lobby's box: "Turn off
  multiplayer?" / "Turn Off". The Esc tooltip title follows the button and its body says only that role's effect
  (two new strings, 9 languages). The Esc menu's note under Load Campaign said "Leave multiplayer to load another
  world" to the host too: now role-neutral "You can load another world when multiplayer ends" (9 languages).
- M11: English Title Case on our button labels, menu rows, settings rows (Rules, cog list, PvP rules) and box buttons.
  Sentences, notes, values and box questions stay sentence case. A sentence that names a renamed button or rule follows
  the new name ("use Join by Address", "Bring Units, then press Start", "Vanilla Check is on"). Keys renamed in every
  lang/*.json with their translations kept; `i18n_tool.py extract` + `check` = 0 missing, 0 stale, 0 bad.
- M12 `mpmenu_model.c` fail(): the reason's first letter is upper case in Latin-1, Latin Extended-A, Greek and Cyrillic
  (was a-z only); the full stop is added after any letter (was after ASCII only), "。" after Chinese / Japanese, nothing
  when the reason already ends with . ! ? … 。 ！ ？.
- M13: "Only the host starts the Hub.", "The Hub could not start." (fe_game.c), "Fixed in the Hub: ..." (Gear row
  tooltip), "Hub · Death: ..." (hub rules summary); each language with its own hub word (ko 허브, fr/de/es/pt-BR hub,
  ru хаб, zh-Hans 枢纽, zh-Hant 樞紐, ja ハブ).

### Decisions (question, pick, why)
- Title Case scope: buttons, menu rows, settings-row labels, box buttons, clickable headers (Teams "Your Units" / "Free
  Units"), the headers of the Multiplayer page column ("Host Your Save", "Host a New Save", "Join a Multiplayer World":
  they sit in the same column as the rows), the Load window title we set ("Host a World", it replaces the game's Title
  Case "Load Campaign"). Not window titles otherwise (not in the brief; most are drawn in Cinzel caps), not values ("Anyone can pause", "Own
  storage"), not statuses. Why: the game's own buttons, settings rows and dropdown rows are Title Case.
- Style: the game's (Chicago) rule: articles, short conjunctions and prepositions lower case ("Host from a Save",
  "Show Address for Friends without Steam", "Ping Lines in Chat"); a phrasal-verb particle capital ("Turn Off", "Give
  Back"); last word capital ("Bring Home To", like the game's "Transfer To {0}"). "Speed-up" and "Off-screen" keep their
  existing tooltip spelling.
- Where a renamed label met an existing tooltip-title key with another translation (17 entries: "Chat Fade Time" in 9
  languages, de/ru "Host from a Save", ja "Units per Player", fr/pt-BR "Expedition Results", es "Lend Control to %s",
  de "Show Friends' Views", pt-BR "Turn Off Multiplayer"): the button's own (shorter, layout-checked) translation wins.
- Legacy check kept: mpmenu.c esc_load_rule still compares TR("Leave multiplayer") (an older build's Load label).
- Esc tooltip body split by role instead of one "Host: ... Guest: ..." text: the button now has a role name.
- ext/ tests of older lanes that compare an old English label (menu_pair.py, mptag_pair.py, fe_dive_test.py,
  hub_coop_pvp.py, pvp_ui.py, pvpfix_pair.py, e3_cards_pair.py, ...) are not changed: they are historical lane tests;
  src/ui_test/*.py and the host tests are updated.

### Tests (host)
mpmenu_test: M4 (line "UX M4"), M8 ("UX M8"), M12 (ru and ja reason), M13 (hub summary, Gear tooltip). On origin/main's
mpmenu_model.c the same test file fails 18 checks (M4 1, M8 2, M12 2, M13 2, the rest are the renamed labels); on this
branch: all passed. pvp_test 725/0 (fr key renamed), mp_api_test 11/0, mp_test all pass, mpsolo_test 10/0,
extaddr_test 13/0, ui test_ui 557/0, teams_test 0 failed, pvpr_test 28/0, rv_gate ok, i18n check 0/0/0, Mac dylib +
Windows module (multiplayer.dll) build.

### Shots
ext/dsuxmp/shoot_text.py (one hidden copy UXT, ext/dsuxmp/make_instance_text.sh; run_text.sh takes the lane slot) on
the loader's multiplayer MODULE built from this tree (ext/dsuxmp/build_module_text.sh; the rig build of src/build.sh is
refused by the loader: "no dsl_plugin_api_version", and then the loader's filter panel and Custom Modes cannot see it).
~/ToDelete_2026-10-25/dsuxmp/shots/text/{en,de,ru}_1728x1000_ui1.0/ (local only), 13 states each: 01 Multiplayer page,
04 join page, 09 host page, 13 refusal box, 19 Your save list, 21-23 lobby + Rules + Turn Off box, 25 cog list, 26 Teams,
29 / 29b / 30 host Esc menu, its tooltip, its box. Results (notes.json): 19 = mode -1, 1 world shown, 0 hidden, no toggle
(en/de/ru); 13 ru = "Игра заполнена."; 29 = Turn Off Multiplayer (de "Mehrspieler ausschalten", ru "Отключить
мультиплеер"), 30 = the lobby's box; copy guard: never visible, never frontmost.

### Labels changed (English, old -> new)
- "Host a game" -> "Host a Game"
- "Join a game" -> "Join a Game"
- "Your save" -> "Your Save"
- "New save" -> "New Save"
- "Join by address" -> "Join by Address"
- "Join a friend on Steam" -> "Join a Friend on Steam"
- "Join %s's world" -> "Join %s's World"
- "Join a friend's world" -> "Join a Friend's World"
- "Host from a save" -> "Host from a Save"
- "Host a new campaign" -> "Host a New Campaign"
- "Steam invite" -> "Steam Invite"
- "Connect with the copied address" -> "Connect with the Copied Address"
- "Copy direct address" -> "Copy Direct Address"
- "Expedition results" -> "Expedition Results"
- "Your name" -> "Your Name"
- "Host address" -> "Host Address"
- "Units per player" -> "Units per Player"
- "Event choices" -> "Event Choices"
- "Admin mode" -> "Admin Mode"
- "Shared fog" -> "Shared Fog"
- "Dungeon loot" -> "Dungeon Loot"
- "Show address for friends without Steam" -> "Show Address for Friends without Steam"
- "Quit to title" -> "Quit to Title"
- "Turn off multiplayer" -> "Turn Off Multiplayer"
- "Bring units" -> "Bring Units"
- "Join anyway" -> "Join Anyway"
- "Save and join" -> "Save and Join"
- "Get the host's mods" -> "Get the Host's Mods"
- "Remove tag" -> "Remove Tag"
- "Host a world" -> "Host a World"
- "Show single-player worlds" -> "Show Single-Player Worlds"
- "Show multiplayer worlds" -> "Show Multiplayer Worlds"
- "Host your save" -> "Host Your Save"
- "Host a new save" -> "Host a New Save"
- "Join a multiplayer world" -> "Join a Multiplayer World"
- "More saves ›" -> "More Saves ›"
- "More units ›" -> "More Units ›"
- "Expedition version" -> "Expedition Version"
- "Home version" -> "Home Version"
- "Bring these units" -> "Bring These Units"
- "Apply to a new save" -> "Apply to a New Save"
- "Bring this unit" -> "Bring This Unit"
- "Bring home to" -> "Bring Home To"
- "Build a unit" -> "Build a Unit"
- "Free camera" -> "Free Camera"
- "Next player ›" -> "Next Player ›"
- "Ask %s to lend a unit" -> "Ask %s to Lend a Unit"
- "Take back" -> "Take Back"
- "Give back" -> "Give Back"
- "Keep playing" -> "Keep Playing"
- "Fade other units" -> "Fade Other Units"
- "Names above units" -> "Names Above Units"
- "Admin mode (host)" -> "Admin Mode (Host)"
- "Spectators (host)" -> "Spectators (Host)"
- "Hover tooltips" -> "Hover Tooltips"
- "Panel size" -> "Panel Size"
- "Teams button" -> "Teams Button"
- "Chat fade time" -> "Chat Fade Time"
- "Join / leave lines" -> "Join / Leave Lines"
- "Show pings" -> "Show Pings"
- "Ping sound" -> "Ping Sound"
- "Ping lines in chat" -> "Ping Lines in Chat"
- "Show friends' views" -> "Show Friends' Views"
- "Off-screen friends" -> "Off-screen Friends"
- "Unit outline" -> "Unit Outline"
- "Fast join" -> "Fast Join"
- "Mini overview (O)" -> "Mini Overview (O)"
- "Overview size" -> "Overview Size"
- "<color=#a69085>%s's colour</color>" -> "<color=#a69085>%s's Colour</color>"
- "Take back control" -> "Take Back Control"
- "Lend control to %s" -> "Lend Control to %s"
- "Send to friends" -> "Send to Friends"
- "Teams (watching)" -> "Teams (Watching)"
- "Save as preset..." -> "Save as Preset..."
- "Delete preset" -> "Delete Preset"
- "Propose fight" -> "Propose Fight"
- "Not yet" -> "Not Yet"
- "To win" -> "To Win"
- "Round time" -> "Round Time"
- "Vanilla check" -> "Vanilla Check"
- "Enemy vision" -> "Enemy Vision"
- "Enemy windows" -> "Enemy Windows"
- "Enemy portraits" -> "Enemy Portraits"
- "Enemy health bars" -> "Enemy Health Bars"
- "Enemy status" -> "Enemy Status"
- "Enemy view range" -> "Enemy View Range"
- "End for everyone" -> "End for Everyone"
- "Turn off" -> "Turn Off"
- "Your units" -> "Your Units" (ui_teams.c only: the Teams section headers)
- "Free units" -> "Free Units" (ui_teams.c only: the Teams section headers)

## uxmp-pvp

Files: `src/pvp_ui.c`, `src/pvp_game.c`, `src/pvp_rules.c`, `src/pvp_rules.h`, `src/pvp_rules_core.c`, `src/pvp_units.c`,
`src/pvp_modcheck.c`, `lang/*.json`. No `src/net/` change: P1 uses dsnet mail (two new app mail kinds, 17 and 18).

### What changed (player view)
- M2: Start Match with a team that has no unit (or no player) keeps the window open and shows the reason in red
  (#F16071, the lobby's refusal colour). Nothing is sent. The teams are counted as the match start counts them
  (`pvp_core.c` start: the units each player of the team owns; brought units: the host's roster).
- P1: a guest's PvP window shows the host's rules and teams. The host mails what its window shows (mail kind 17,
  `pvpr_view_format` text) when it changes, when a player joins, and when a guest opens its window (kind 18 = "ask").
  Display only: `pvp_rule_get`, the fingerprint and the join-point block never read it; the match rules still go in
  the stamped order at Start. Rate: at most 4 sends a second, checked every 10 frames.
- P2: every PvP match message (planning pause, countdown, "Fight!", flags, preparation) shows on a dark plate under the
  score panel, under whatever shows below the bars (Ready / Propose Fight, Bring Units, the vote box, the round screen).
  The planning one stays while the pause lasts, the others 4 s. Without the match HUD (world back): the game's notice.
- M5: the PvP window, its Bring Units list (the text list; the card window already followed the scale) and the result
  window: root scale = the game's UI scale (`mu_ui_scale`), then smaller only to fit the screen with 16 px free.
- M11: Title Case on my buttons and row labels: Save as Preset..., Delete Preset, Propose Fight, Not Yet, To Win,
  Round Time, Vanilla Check, Enemy Vision / Windows / Portraits / Health Bars / Status / View Range. The two refusal
  sentences that name the rule say "Vanilla Check" too. Keys renamed in all 10 tables (translations kept).

### Decisions (question, pick, why)
- Which messages move under the panel? All PvP match messages, not only the planning one: they all came from the same
  call and sat in the same place over our HUD.
- M2 with no player on a team: a new line ("Each team needs at least one player. Click a player's name to move them to
  the other team.") in 9 languages, because "Right-click a unit's portrait" cannot fix a team with nobody in it.
- P1 transport: mail (outside the sim) instead of the stamped rules order: a display change must never touch the
  match state, the desync fingerprint or the join-point save.
- A guest with no window data yet shows the default rules (it showed its own saved rules before).

### Tests
- `ext/dsuxmp/pvp_pair.py` (pair, two hidden loader copies UPA / UPB, OS port; instances from
  `ext/dsuxmp/make_instance_pvp.sh`). The dylib = `ext/dsuxmp/build_mouse.sh <out>`: the LOADER'S multiplayer module
  (mp_module.c + src, as ds_modloader loader/build.sh) with the fake mouse. Trap: the rig's own dylib (src/build.sh) also
  loads as a mod but has no loader host: no kit, so no UI scale (M5 looked broken) and no Bring card window.
  Before-build = commit 2b33cf6 (origin/main + the probe only): `SRC=<git archive 2b33cf6>/src build_mouse.sh <out>`.
- Traps met: shot paths must be under `~/ds_*` (DSRIG_WRITE_DIRS); the guest that arrives with no unit gets the Teams
  window (close it first); the base save's units can be out in 4 s (the round ended before the first planning pause):
  the test adds +60 Constitution; `UIScaleManager.SetScale(v, true)` while running rescales our windows (they read the
  scale) but the game's HUD keeps its layout, so P2 is measured at the start scale only (runs at 1.0 and 1.4).
- Not updated (other lane's old test, not in gate 0126): `ext/pvp/tests/pvp_ui.py` hovers the tip title 'Round time'.
