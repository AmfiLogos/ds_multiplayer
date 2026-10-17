#!/bin/zsh
# Build the mod/rig dylib (arm64 only). Output: ../build/dsrig.dylib (ad-hoc signed).
# rig + determinism (rig_*), multiplayer glue (mp_*), dsnet (net/, net/steam/), orders (ord_*), UI (ui_*).
# ord_test/ord_smoke.c only adds the ord_inject / ord_select / ord_hover / ord_stats test commands.
# Multiplayer menu (mpmenu*).
# i18n (ds_i18n.c = the one DSI_IMPLEMENTATION; language files lang/<code>.json next to the dylib, see mpmenu.c lang_dir_init).
# Transitions (trn_*): floor seeds, flow barrier, host-only autosave.
# Multiplayer API (mp_api.c / mp_api.h, MASTER_PLAN 2.2 B) + the menu kit (mpmenu_kit.h) for Custom Modes.
# Custom Modes (Foreign Expedition, PvP, units) is its OWN dylib since the M4 split and its own repo since post-v13
# (github.com/AmfiLogos/ds_custommodes; checkout CM_REPO, default ~/ds_custommodes): built after this one by
# $CM_REPO/build_module.sh into ../build/custom-modes.dylib + custom-modes-mp.dylib (the rig loads it from next to
# dsrig.dylib like the loader does; DSRIG_CUSTOM=0 = without it). Install name @rpath/multiplayer.dylib = the loader
# module's, so custom-modes-mp binds to whichever multiplayer image is loaded. OUT=<dir> builds into another folder.
# Mac math swap (windows lane, win/NOTES.md sections 7 + 17): ON by default since v9 (-DDSMATH_DEFAULT_ON=1, same as the
# Windows build); off only with DSMP_MATH=0 or <DSRIG_DIR>/dsmp_math.off (measurements, tests).
# Needs win/math/build/libdsmath_arm64.a (run win/math/build_math.sh once).
set -e; cd "${0:A:h}"; OUT=${OUT:-../build}; mkdir -p $OUT
nice -n 19 clang -arch arm64 -mmacosx-version-min=12.0 -O2 -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/multiplayer.dylib -o $OUT/dsrig.dylib.tmp \
  rig_main.c rig_hook.c rig_sock.c rig_cmd.c rig_win.c rig_det.c \
  mp_core.c mp_config.c mp_game.c mp_fog.c mp_api.c mp_bridge.c \
  net/dsnet_link.c net/dsnet_server.c net/dsnet_client.c net/steam/dsnet_steam.c \
  ord_core.c ord_game.c ord_mp.c ord_test/ord_smoke.c \
  ui_game.c ui_model.c ui_tip.c ui_teams.c ui_teams_model.c mpmenu.c mpmenu_model.c \
  trn_logic.c trn_game.c trn_mp.c ds_i18n.c \
  ../win/math/math_mac.c -I../win/math -DDSMATH_DEFAULT_ON=1 ../win/math/build/libdsmath_arm64.a -lz
codesign -f -s - $OUT/dsrig.dylib.tmp 2>/dev/null
mv -f $OUT/dsrig.dylib.tmp $OUT/dsrig.dylib     # new inode: never overwrite a mapped dylib
echo built $OUT/dsrig.dylib
CM=${CM_REPO:-$HOME/ds_custommodes}
[[ -f $CM/build_module.sh ]] || { echo "Custom Modes checkout not found: $CM (git clone https://github.com/AmfiLogos/ds_custommodes, or set CM_REPO)" >&2; exit 1; }
MP_DYLIB=${OUT:A}/dsrig.dylib OUT=${OUT:A} SDK=${PWD:h}/sdk MP_SRC=$PWD zsh $CM/build_module.sh
