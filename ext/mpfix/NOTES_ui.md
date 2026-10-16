# mpfix-ui (v13, lane mpfix, sub-lane UI): notes

Branch `mpfix-ui`, worktree ~/ds_mp_mpfix_ui. Brief: research/briefs_v13/mpfix.md steps 1-4 (owner feedback v12 items 3, 4, 5, 8).
Build: `OUT=~/ds_mp_mpfix_ui/build_ui zsh src/build.sh` (needs the gitignored link win/math/build -> ~/ds_multiplayer/win/math/build).
Fake-mouse build for menu_pair: same clang line with `-DTEAMS_MOUSE_TEST`, output build_ui/mouse/ (custom modes next to it).

## 1. Hover cards outside the panel (src/ui_tip.c, one place for every caller of game_tip_show_rect)

What the game does (measured on a hidden copy, 960x540):
- Title column: button -> MenuButtonHolderTransform (size 0) -> Img_BackGroundBox (the dark column, x 74..252, a Graphic)
  -> TitleMenuUI (full screen). The old card went to the button's bottom-left corner: x 94..246, over the column and the
  next button (owner shot shots_v12/host_page.png).
- Our Rules / FE Rules / lobby / Recent Worlds boxes: the element's ancestor is the box image.
- Esc menu: button -> holder -> Transform_ScaleAdjust (no Graphic) -> SystemMenuUI (full screen): no box; the card goes
  right of the button column.
- HUD Players panel and its settings box (ui_game.c): rows hang on size-0 holders; the box image is a SIBLING, not a parent.

Rule now (panel_of + place_card):
- panel = the widest of: an ancestor that holds the element horizontally and has a Graphic; a sibling of the element or of an
  ancestor that holds the whole element, has a Graphic and is active. Only boxes narrower than 70% of the screen; the scan
  stops at the first full-screen ancestor (its children are scanned). None found: the element itself.
- card top-left = (panel right + gap, element top); if it leaves the screen on the right: (panel left - gap - card width).
  No room on either side: the wider side, kept on screen ("inside"). The card is kept on screen at the bottom. Gap = 0.5% of
  the screen width (min 2 px).
- placed by a hook on Tooltip_Base.SetTooltipAlignment 0xF387DC (after the stock run: no flash; first instruction 0x6dba2beb
  added to the build guard) and kept there by unstick() each frame (moves it back if anything else moved it). Only our own
  tooltip session (TooltipID == TIP_ID at +0x6C). Mouse-placed cards (game_tip_show) keep the stock placement.
- ui_tip status (rig `{"cmd":"ui_tip","action":"probe"}`): new keys anchored, side, panel_rect, elem_rect, tip_xy, screen;
  action "chain" adds the element's ancestors (name, screen rect, graphic) for diagnosis.

## 2. Multiplayer page spacing (src/mpmenu.c title_build)

Measured: stock title buttons (root = frame image) 28.6 px high, step 35.2 px: gap 6.6 px (960x540). Our Your save / New save /
Foreign Expedition rows were shrunk to 40 canvas units (22 px) and Back to 44 (24 px), but the frame image child keeps the stock
size (29 px), so the frames touched. Fix: those rows keep the stock button size (no row_height call); the column's own spacing
then gives the stock gap. Measured after: 6.6 px between every frame from Your save down to Back (menu_pair check 0).

## 3. Rules always open beside the host page (src/mpmenu.c, src/mpmenu_model.c)

- Host page: the Rules panel (or the Expedition Rules panel for a Foreign Expedition) shows whenever the page shows; no Rules
  cog row in the column (MPB_SETTINGS left out of mpm_visible), no Done (hidden; the panels are 52 units shorter, top kept).
- Rows, arrows, tooltips and saved values unchanged (rule arrows already worked without settings_open). Escape on the host page
  = Back (nothing to close first). mpmenu_test updated (2 lines).
- Lobby Rules (wide panel, cog in the lobby window) unchanged: not in the brief.

## 4. Foreign Expedition fallback text (src/mpmenu.c mpmenu_install)

"Everyone brings units into a neutral hub with everything unlocked. Nobody's save changes until they bring the results home."
The real text is registered by custom/cm_mp.c (lane fepvp), still the old "copy of your world" text on this branch: that lane
changes it.

## Tests

- ext/mpfix/tests/tip_pos.py --dylib D: one hidden copy; 8 hovers (title column x2, host page column, host page Rules row,
  lobby Start, Esc Leave Multiplayer, HUD gear, HUD settings box row); checks card on screen, anchored, outside the panel
  (right: x0 >= panel x1, left: x1 <= panel x0), no overlap, top = element top. Closes an old expedition results window first.
- ext/mpfix/tests/smoke_pinned.py D: rig smoke on a dylib with a leased pair.
- ext/wpmenu/tests/menu_pair.py: check 0 now also measures the stock gap (mpm_status stock_frames) and the column gaps
  (mpm_status frames), and the host page Rules panel (mpm_status rules_panel: title, title_rows 9, done 0, no cog row).
  New option --lane (default m2menu). Still 17 checks.
- mpm_status: new keys frames, stock_frames, rules_panel.{fe,title_rows,done,fe_done}. mpm_tree {what:"title"}.

## Known

- Instance A's data has old expedition results: their window covers the lobby. tip_pos / menu_pair press "Later" first.
- The card for a column button now sits over the left part of the Recent Worlds panel (right of the column, as asked).

## 5. register_host_choice2 + host_choice_now (MPF API for lane fepvp, commit 740a268)

- mp_api.h: `mp_host_choice` and two table fields appended after register_menu (MP_API_VERSION 1 unchanged; check size).
- Registry in mpmenu_model.c (`mpm_choices`, unit-tested): up to 4, registration order, same id = replace in place (also
  across the two APIs), 5th refused, v2 texts over id 31 / label 47 / tip 299 bytes refused (old API: cut, as before).
- Buttons: choice 1 = MPB_HOST_FE (123), 2..4 = MPB_HOST_C1..C3 (124..126), under "Host a game" after "New save"; stock
  row size (stock gap). Host page Start of a v2 choice = MPB_HOST_START (127), red primary style.
- v2 host page: header "Host: <label>", Your name, Start + Back (no Load save / New campaign). draw_page set: box where the
  Rules panel sits, `panel` = an empty RectTransform (new GameObject + RectTransform, 12 units inside the box), called
  every frame while the page shows, draw_page(NULL) once when it closes (Back, Start -> loading, scene change).
  draw_page NULL: M.set.fe = 1 and the Expedition Rules panel open without Done (custom fe_menu's fe_host_mode sees fe = 1).
- Start: page_start(err) -> 0: HOST_LOADING -> (campaign ready) LOBBY + MPA_HOST_START, as Your save; != 0: err in the
  page's INFO line. Lobby Start: lobby_start(err) -> 0: PLAYING + LOBBY_HIDE + START_GAME; != 0: ui_notice(err).
- host_choice_now: id from the host page to the end of that session (cleared on Back / IDLE / title pages).
- Old register_host_choice: one choice per id; its page and lobby Start (MPA_FE_START -> on_start of the choice this
  session came from, else the first old one with on_start) unchanged. The src fallback FE registration stays.
- Test: `mpm_testchoice {action: register|set|status, id, label, tip, draw, save, page_err, lobby_err}`;
  ext/mpfix/tests/choice2_pair.py (8 checks); lease_wait.py takes a lease before a test on a busy machine.
