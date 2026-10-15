# Spectator mode (ext/spectator) - notes (2026-10-14)

Not part of the multiplayer release. Separate source copy: `ext/spectator/src/` (rsync of `src/` at 19:12, no
old_versions; baseline copies of every edited file in `src/old_versions/`). `src/build.sh` builds
`ext/spectator/build/dsrig.dylib` only. Plan: `research/plan_foreign_expedition_spectator.md` part 2. Owner answers:
max 8 spectators, no switching to player.

## Design (what differs from the plan)
- Spectators live OUTSIDE the simulation. The server sends no PLAYER_JOIN / LEAVE for them, so they are never in
  `present_mask`, never in saves, hashes or the command log. Every rule that counts players (votes, ready, population,
  story / map votes, proposals) ignores them by construction. The plan's "spectator_mask in the sim" and
  "RULES byte 7" were dropped: the allow flag and the list are server state, sent in the WELCOME and in a ~5 Hz
  `P_SPECT` list (id, state, ping, name). Turning spectators on / off is `P_SPECT_RULE` (host only).
- `DSNET_PROTO` 2: HELLO carries `u8 role` before the name; WELCOME echoes role + allow + max.

## S1 dsnet (net/)
- Server: `sconn.spectator`; refusals `DSNET_REJ_NO_SPECTATORS` ("this world does not allow spectators"),
  `DSNET_REJ_SPECTATORS_FULL` (cfg.max_spectators, default 8). Every P_CMD from a spectator is dropped; P_FRAMETIME,
  P_BUSY, P_VIEW, P_PATCH_REQ and report blobs too. Timer: spectators never count for min_players, behind / ok_behind,
  busy, slow-down, frame time. No auto pause on their join or desync; never a PLAYER_LEFT hold.
- Off in play: every spectator gets `P_BYE` reason 2 (`dsnet_bye_reason` = `DSNET_BYE_SPECTATORS_OFF`) and is dropped.
- Client: `dsnet_client_connect_tcp_role`, `dsnet_is_spectator`, `dsnet_spectator_mask / _info / _allowed / _max`,
  `dsnet_set_spectators` (host). `dsnet_submit` refuses locally for a spectator; `dsnet_may_order / _equip /
  _use_item` return 0 for the spectator's own slot. Chat: `dsnet_chat_msg.watching`, kinds WATCHING / STOPPED_WATCHING
  (from the list). A slow spectator catches up in bursts (up to 200 steps a frame), never holds anyone.
- Steam lobby size 8 + 8.

## S2 join without disturbing the players
- The server keeps the latest join-point save. Every join point already makes the host reload that save, so a
  spectator that loads it and replays the command log after it ends in the host's state. A spectator join (or its
  resync) gets that save + log: no join point, no pause, no reload for anyone. No save yet, or older than
  `spectator_replay_max` steps (default 30 min at 1x): one join point WITHOUT pause (players reload once).
  A host world change (P_WORLD with another id) drops the cached save.
- Spectators never ask for a partial resync (no host work): a desync = the cached save again.

## S3 UI
- Menus (mpmenu*): Join page "Watch" (MPB_WATCH 68), Recent worlds row "Watch" (MPB_HISTW0..5, not on own hosted
  worlds), Rules row 8 "Spectators: Allowed (· n watching) / Not allowed" (host page + lobby, saved in mp_menu.json
  and as the 9th field of mp_worlds.txt), confirm box "Remove the n spectators now?" (BX_SPECT_OFF), end notice
  "<Host> turned spectators off", "Joining <host>'s game as a spectator...". Exports `mpmenu_spectators_set`,
  `mpmenu_spectators_off_ask`. Chat: "Ana (watching): ..." in grey, "Ana is watching" / "stopped watching" lines,
  spectators' "is typing".
- In game (ui_game.c): viewer box on every map "Watching <host>'s world" / "You are a spectator. You can chat and ping."
  with Follow <player>'s camera / Free camera / Next player (follow = the lan_v10 P_VIEW, outside the sim); clock speed
  and pause buttons hidden (hotkeys ignored in mp_game); every order, claim, recruit, story choice, map change,
  expedition window refused with "You are watching: ..."; no map-vote box (a notice "The owner wants to ..."); story popups
  read only; skill bar and character windows "Spectator: view only"; Esc Save greyed with the reason; Players panel
  "Watching (n)" divider + grey rows with connection bars (PL_MAXROWS 18); host cog row "Spectators (host) On (n) / Off";
  spectators grey everywhere (`ui_spectators`, `UI_SPECTATOR_COLOR`). Tooltips in the game format.
- Teams (ui_teams.c): "Teams (watching)", every click refused; proposals never open a panel for a spectator (a notice).

## S4 tests
Pure C: `make -C src/net test` (all 33 incl. `spectator`, `spectator_nosave`), `make -C src/mp_test test`
(incl. `spectator`), `make -C src/ui_test test`, `make -C src/mpmenu_test test`.
Pair (`/opt/homebrew/bin/python3 ext/spectator/tests/<file> --tag NAME`, output `ext/spectator/out/NAME/`):
- `spect_pair.py` (1 pair, lane "spectator"): SP-1 + SP-3 + SP-4 (see its header).
- `spect_three.py` (2 pairs, lanes "spectator" + "spectator2"): SP-2, host + player + spectator.
Results: see "Runs" below.

## S5 mini overview + HUD A (2026-10-15 night, owner 2026-10-14 ~19:55, board SpectatorHUDAMini)
- ui_game.c 8k: while a spectator follows a player, a movable "Overview" window (bottom right by default) shows the
  whole map: a second camera (new GameObject + Camera, Camera.CopyFrom / set_orthographic / set_cullingMask through
  il2cpp_resolve_icall because the managed ones are stripped) renders into a RenderTexture shown by a RawImage; it is
  enabled one frame in 6. On top: every player's view rectangle + name in their colour, the own view (grey, free
  camera only), player units as dots, pings as rings; legend line; floor ("Floor N" / "Settlement").
- Title bar: drag (left or right button), saved in mp_ui.json (mini_pos_set / mini_x / mini_y); - / + = Small /
  Medium / Large (260 / 340 / 440 wide); x hides (mini = 0). O shows / hides. Cog rows (spectators only): "Mini
  overview (O)" Show / Hide, "Overview size". Players' views / pings follow the existing cog rows.
- Click on the map: look there; the follow stops and the window stays (free camera) until "Free camera".
- HUD A: viewer button "Follow the owner" / "Following the owner"; a "Following the owner's view" chip at the top centre and a thin
  gold frame round the screen while following (click-through).
- Pure geometry + settings in ui_model.c (ui_mini_*), tests in ui_test (test_mini).
- Test command ui_mini {action: o | smaller | bigger | hide | click (fx, fy) | move (x, y) | reset}.

## Runs
- 2026-10-15 05:05 spect_pair.py sp1_run4 (pair CD): 16 / 16 PASS (incl. 4f mini overview).
- 2026-10-15 07:57 spect_mini.py mini_run3 (AB + CD, 3 copies, lane build): 14 / 15 PASS. The failed item: the 10 min
  run had 200 common fingerprints, all equal, host 0 desyncs, but the player (5) and the spectator (2) flagged
  "FULL-STATE CHECK DIFFERS: CampaignSettingSaveData/CameraSettingSaveData": the lane's 10-14 base compared the
  camera; src excludes it since the 10-14 soak (mp_game.c). Not a spectator fault. Frames out/mini_run3/C_*.png.
- 2026-10-15 09:12 merged build (build/merge_spectator.dylib, src after core next): gate smoke 3/3, mp2 19 PASS,
  left_prompt 16 PASS; 09:39 spect_mini.py mini_merged1 15 / 15 PASS (10 min: 37350 common fingerprints equal,
  0 desyncs on host, player and spectator; the camera full-state flags are gone); 09:51 spect_pair.py sp1_merged1
  16 / 16 PASS. Merged into src 09:53 (PLAN.md "spectator merge"). Tests now take --dylib (default build/dsrig.dylib),
  MPB_WATCH = 70.
- 05:54 / 06:54 runs void: the host had "Spectators: Not allowed" saved from an older run; at 06:54 copy A was
  started by another lane outside the lease. spect_mini.py now sets "spectators":1 in mp_menu.json and refuses
  to drive a copy that is already running.

## Not done / open
- Watch from a Steam friend row / accepted Steam invite: needs the Steam build (no Steam in the rig); the API
  (`mp_game_next_spectator`) is in place for Recent worlds Steam rows only.
- Story popup and map vote read-only states are coded, not exercised in a pair run.
