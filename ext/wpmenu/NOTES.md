# M2 menu lane (branch wp-menu; MASTER_PLAN 5.2 M2)

Owns src/mpmenu.c, mpmenu_model.c, mpmenu.h, mpmenu_test. Starts from uifix11 (host choices, World row removed, hover card
off) + mptag (Multiplayer world tag). Merged main 2026-10-15 (PLAN.md conflict: both sections kept).

## Built here
- **Save lists split** (owner 2026-10-15): stock load list from the title or the Esc menu = single-player worlds; "Your save"
  > Host from a save = multiplayer worlds (multiplayer = the Multiplayer tag, mp_tags.txt). Checkbox under the list (stock
  CheckBox_Normal / _Checked sprites): "Show multiplayer worlds" / "Show single-player worlds"; off each time the list opens.
  Foreign Expedition save pick: no filter (an expedition copies any world). The world being played always shows. The Save
  panel is never filtered. Model: mpm_list_mode / mpm_list_show / mpm_list_toggle_label (unit-tested).
  Game side: post-hook SaveLoadView.RefreshLoadPanel (Mac 0x85ED7C) -> world rows (_campaignCases +0xE0) and their save rows
  inactive, RefreshPanelSize (0x85FBC8). Band for the checkbox: ScrollView_SettingData offsetMin.y + 44 (stock value back
  when the list closes or has no checkbox). Toggling off: UnselectAllSlots + LockLoadButtons (no pick left in a hidden world).
  Moves to the loader's LOAD-ROW API (dsl_loadlist_filter) when L1 has it.
- **Third host choice behind a registration** (MASTER_PLAN 2.2 B): `mpmenu_register_host_choice(id, label, tip, draw_page,
  on_start)` (exported). "Foreign Expedition" shows only once registered; until the split (M4) mpmenu_install registers the
  built-in expedition with NULL callbacks (= the menu's own page and fe_menu_start). on_start, when given, replaces
  fe_menu_start (0 = started). draw_page is stored for M4 (not called yet: the rules rows are still drawn here).
- **Death permanent Off by default** (owner: "death is permanent should be off by default"): mpm_init fe_death 0. A saved
  fe_death in mp_menu.json counts only with "fe_v": 2 (older files held the old default, not a choice).
- **Remove tag click folded the world row** (mptag bug found by this lane's real click): the text was under the row's
  open/close button, so the click bubbled to it. Now placed in the name's space, then moved under the row itself
  (SetParent world position kept, SetAsLastSibling).

## Proof
- Unit: `make -C src/mpmenu_test test` all passed.
- Pair: `ext/wpmenu/tests/menu_pair.py --dylib build/wpmenu_mouse.dylib` (fake-mouse build: `ext/wpmenu/build_mouse.sh`,
  -DTEAMS_MOUSE_TEST, clicks through the game's Input.GetMouseButtonDown / mousePosition). Run3 pair GH: **17/17**, incl.
  host choices, multiplayer list, checkbox real clicks, tag on both copies, **0 desync (2440 common ticks)**, Remove tag
  (box, Cancel, Remove), list membership after untag, restart, save byte-identical and loads. Frames: ext/wpmenu/out/run3.
- Gate (rig/gate.sh on this branch): smoke 3/3, mp2 19 PASS 0 FAIL, left_prompt 16 PASS, prefs identical.
- Test commands: `mpm_list {flip, real, close, scroll_to:guid}`, `mpm_tree {what:"load"}`.

## For the Windows build (L3)
New Mac RVAs to map (win_map.py re-run): 0x85ED7C RefreshLoadPanel (hook), 0x85FBC8 RefreshPanelSize, 0x860030
UnselectAllSlots, 0x86047C LockLoadButtons, 0x51077B4 / 0x510794C RectTransform.offsetMin get/set, 0x510992C
Transform.parent, 0x510B2C4 SetParent(t, bool), 0x510ED6C SetAsLastSibling, 0x549A6F0 ScrollRect.verticalNormalizedPosition
(test only). Not built for Windows in this lane.

## Not done here (other packages)
FE page rules drawn by Custom Modes (C2 via draw_page), hub kit, Bring Units: not M2.
