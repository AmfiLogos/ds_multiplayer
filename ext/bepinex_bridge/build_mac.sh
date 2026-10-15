#!/bin/zsh
# Lane build of the Mac dylib with the bridge, from the bepinex-bridge worktree src: build/dsrig_bridge.dylib.
# rig + determinism (rig_*), multiplayer glue (mp_*), dsnet (net/, net/steam/), orders (ord_*), UI (ui_*).
# ord_test/ord_smoke.c only adds the ord_inject / ord_select / ord_hover / ord_stats test commands.
# Multiplayer menu (mpmenu*).
# Foreign Expedition (fe/: save library, game glue, menu windows in fe/fe_menu.inc; ext/foreign_expedition).
# Transitions (trn_*): floor seeds, flow barrier, host-only autosave.
# PvP (pvp_*; ext/pvp/NOTES.md): rules, hooks, windows, own / wiki units, Vanilla check.
# Mac math swap (windows lane, win/NOTES.md sections 7 + 17): ON by default since v9 (-DDSMATH_DEFAULT_ON=1, same as the
# Windows build); off only with DSMP_MATH=0 or <DSRIG_DIR>/dsmp_math.off (measurements, tests).
# Needs win/math/build/libdsmath_arm64.a (run win/math/build_math.sh once).
set -e; L="${0:A:h}"; cd ${BRIDGE_SRC:-~/ds_mp_bepinex/src}; mkdir -p $L/build; W=~/ds_multiplayer/win
nice -n 19 clang -arch arm64 -mmacosx-version-min=12.0 -O2 -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/dsrig.dylib -o $L/build/dsrig_bridge.dylib.tmp \
  rig_main.c rig_hook.c rig_sock.c rig_cmd.c rig_win.c rig_det.c \
  mp_core.c mp_config.c mp_game.c mp_fog.c mp_bridge.c \
  net/dsnet_link.c net/dsnet_server.c net/dsnet_client.c net/steam/dsnet_steam.c \
  ord_core.c ord_game.c ord_mp.c ord_test/ord_smoke.c \
  ui_game.c ui_model.c ui_tip.c ui_teams.c ui_teams_model.c mpmenu.c mpmenu_model.c \
  trn_logic.c trn_game.c trn_mp.c \
  fe/fe_json.c fe/fe_save.c fe/fe_game.c \
  pvp_core.c pvp_modcheck.c pvp_mods.c pvp_game.c pvp_units.c pvp_wiki.c pvp_build.c \
  $W/math/math_mac.c -I$W/math -DDSMATH_DEFAULT_ON=1 $W/math/build/libdsmath_arm64.a -lz
codesign -f -s - $L/build/dsrig_bridge.dylib.tmp 2>/dev/null
mv -f $L/build/dsrig_bridge.dylib.tmp $L/build/dsrig_bridge.dylib     # new inode: never overwrite a mapped dylib
echo built $L/build/dsrig_bridge.dylib
