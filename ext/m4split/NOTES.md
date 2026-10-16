# M4 SPLIT (lane m4split, 2026-10-15 night)

MASTER_PLAN 5.2 row "M4 SPLIT". This is a structure change. Player behaviour is the same as v11 (gates below).

## What moved
- `git mv` (history follows): src/fe/* -> custom/fe/ (fe_menu.inc -> fe_menu.c); src/pvp_core/modcheck/mods/game/ui.inc/pvp_test ->
  custom/pvp/ (pvp_ui.inc -> pvp_ui.c); units code src/pvp_units.c, pvp_unit_tables.h, pvp_wiki.*, pvp_wiki_tables.h, pvp_build.* ->
  custom/units/. Main had no myunits.c / wk_* (those are on c1-units: C1 moves them into custom/units when it rebases).
- .gitignore: `!/custom/` and `!/sdk/` (custom/ was ignored by `/*`; the renamed files stayed tracked, new ones did not).

## Multiplayer API (src/mp_api.h, src/mp_api.c)
`mp_api_get(version)` returns `mp_api_table` (the type is not named `mp_api`, because mp.h already has the function `mp_api(session)`).
Calls: role, is_host, my_uid, players, present_mask, session_busy, register_host_choice, register_esc_button, mail_send,
register_mail, register_sim_cmd, send_sim_cmd, register_tick, register_fingerprint, register_joinpoint, register_world_build,
register_order_filter, ui_set_flag, register_unit_tag, register_teams_portal, modset_lines, register_rig_cmd. The split also
needed 3 more: register_frame (ALWAYS = old pvp_frame place in rig_frame, SESSION = old fe_poll place in mp_game), register_save_keep
(old fe_is_protected in the save-delete hook), register_menu (mpmenu_kit.h hooks: the FE / PvP windows that were #included in mpmenu.c).
Every old call site now calls a registry (`mpx_*`) at the same place. Registration order = the old call order (FE, then PvP).
- Join-point blocks: same bytes ("PVPB" tag + u32 len + block). Override path writes the block even at size 0, as before.
- UI flags: FE sets MPUI_HIDE_GUILD_CONTACT + MPUI_NO_RECRUIT_POPUP from fe_recruit_allowed() every frame (ALWAYS). Before the split
  ui_game asked fe_recruit_allowed() directly at hook time, so the value can now be up to 1 frame late.
- Implemented but NOT used yet (later lanes switch to them): register_esc_button (C3: PvP keeps its own "PvP Match" clone for now),
  register_mail / mail_send (FE fe_poll still reads the mailbox itself and hands PvP kinds to pvp_mail_take), send_sim_cmd,
  modset_lines, players/role/my_uid. draw_page of register_host_choice is not called: the Expedition Rules rows are still mpmenu's own
  (M.set.fe_*, mpmenu_test covers them). C2 moves them to Custom Modes via draw_page.
- Custom code still calls multiplayer internals (dsnet_*, rig_*, mp_*, ord_*, trn_*, unit helpers) and the menu kit by name, through
  the two-level link below. Moving them onto the public API is C1-C3 work.

## Menu kit (src/mpmenu_kit.h)
The fe_menu / pvp_ui windows use about 40 mpmenu.c helpers and state (mk_img, mk_txt, mk_button, tip_try, M, frames, h_lwr, h_tb...).
They are now exported, with only `static` removed. Types, K_*/S_* enums, colours and the u_* inlines moved to the header, unchanged.
ext/m4split/tools/kit_unstatic.py does the mpmenu.c part (re-run it if mpmenu.c changes before a rebase).

## Dylibs (Mac)
- src/build.sh: dsrig.dylib (install name now @rpath/multiplayer.dylib, = the loader module's), then custom/build_module.sh into the
  same folder: custom-modes.dylib (cm_module.c: SDK entry points, no link to multiplayer) + custom-modes-mp.dylib (cm_mp.c + fe/ pvp/
  units/, linked two-level to @rpath/multiplayer.dylib).
- Why 2 files: the loader dlopens modules RTLD_LOCAL and has no optional load order. So a single custom-modes.dylib linked to
  multiplayer could load BEFORE it and stay unbound. custom-modes.dylib does dsl_module_early (all modules are dlopened then). If
  module_loaded("multiplayer") is true, it gets mp_api_get with dlopen("@rpath/multiplayer.dylib", RTLD_NOLOAD) + dlsym, then dlopens
  custom-modes-mp.dylib, which binds to the loaded multiplayer image by install name (tested with dyld: loader layout, rig layout
  (dsrig.dylib with the same install name), and custom alone). Multiplayer absent = FE / PvP hidden (log line). Solo tabs (C1/C4/C5)
  go into custom-modes.dylib.
- The rig hosts it like the loader does: rig_main.c dlopens custom-modes.dylib (RTLD_LOCAL) from next to the running dsrig.dylib
  (DSRIG_CUSTOM=<path>, DSRIG_CUSTOM=0 = none) and calls dsl_module_load + dsl_module_early with a small DSLHost (hook_rva -> rig_hook,
  module_loaded("multiplayer") = 1). sdk/dsl_sdk.h = copy of ds_launcher 9884bda loader/sdk/dsl_sdk.h.
- pvp_units fe_reads_mail used dlsym(RTLD_DEFAULT, "fe_poll"). That fails for an RTLD_LOCAL image and would give 2 mailbox readers,
  so it is now a weak reference in the same image. pvp_game's dlsym(RTLD_DEFAULT, "ui_center_message") is unchanged: it works in the
  rig (dsrig inserted = global) and did not work in loader mode before either.
- custom/mod.json: id custom-modes, scope gameplay, api 3, no "requires" on multiplayer (works alone).

## Loader (ds_launcher branch m4-busy 7a43fa6, pushed, NOT merged: L1 owns loader/)
- The SDK already had `set_session_busy`. modules/multiplayer/mp_module.c polls mp_session_busy_reason() each frame, finds it with
  dlsym(RTLD_SELF) so older pins still link, and calls dsl_set_session_busy when it changes. Busy = a session exists, or the menu is in
  HOST_LOADING / LOBBY / PLAYING / GUEST_JOINING / GUEST_LOADING / GUEST_LOBBY.
- loader/build.sh: git archive also takes custom/ and sdk/ when the pinned commit has them. With MP_PIN = m4split it builds
  mods/custom-modes (both dylibs + mod.json). Not run in a loader game copy: loader test copies are the loader lane's (test_env).

## Windows (for L3)
- No separate custom-modes.dll yet. Custom Modes is compiled into the same DLL (baselib.dll and the module multiplayer.dll) with
  -DCM_SAME_IMAGE. rig_main_win.c calls cm_mp_start(NULL, NULL, mp_api_get(1)) where pvp_install was. gen_port.py generates the
  custom-modes-mp file list from custom/ (DSMP_CUSTOM for a frozen tree). win_map.py also harvests custom/.
- Found on main: in the v11 Windows DLL, fe_menu.inc and pvp_ui.inc were #included raw into the generated mpmenu.c. So their
  F(0x<Mac RVA>) calls were never translated, and the FE / PvP windows on Windows called Mac addresses. They are now separate files
  and get translated (no gen_port problem reported).
- Found on main: build_win.sh stopped since the viewui merge. gen_port's float-pair check flagged `(int (*)(float, float))dl("ui_panel_hit")`,
  which is our own function. "float, float" alone is now allowed (a game call always ends in void * MethodInfo).
- To do for L3: a separate custom-modes.dll (custom_modes entry + GetProcAddress(multiplayer, "mp_api_get"); custom code then needs
  its imports from multiplayer.dll: an import lib, or the same-image build kept).

## Release tools: NOT changed (release-gate owner)
rig/release_gate.sh, release_all.sh and make_mp_app.sh freeze and ship dsrig.dylib only. A kit built from a post-split tree with them
as they are has NO Foreign Expedition / PvP: custom-modes.dylib + custom-modes-mp.dylib must sit next to dsrig.dylib (Frameworks/).
The release gate (MASTER_PLAN 5.4) must add them, or ship through the loader's mods/ folders.

## Gates (build/merge_m4split/: dsrig.dylib + custom-modes.dylib + custom-modes-mp.dylib, after merging main e048453)
- Unit: net ALL PASS, mp_test ALL PASS, ui_test 530, teams_test 437097, mpmenu_test, trn_test, pvp_test 585, test_custom_guard ALL PASS.
- Pairs (lease m4split, ext/m4split/tools/run_with_dylib.py): smoke 3/3 (rig.log: "Foreign Expedition + PvP registered"),
  mp2 3000 ticks 19 PASS 0 FAIL, left_prompt --start 16 PASS 0 FAIL, PvP m4_van 7/7, m4_bring 8/8, m4_ui 9/9 (ext/pvp/out/m4_*),
  FE dive 23/23 (ext/foreign_expedition/out/dive_20261015-233336; ids from tests/mpm_ids_m4.json = this build's MPB_* values),
  multiplayer alone (DSRIG_CUSTOM=0) smoke 3/3. Prefs identical after every run.
- Windows: build_win.sh baselib.dll and DSMP_TARGET=module multiplayer.dll compile (no wine run).
