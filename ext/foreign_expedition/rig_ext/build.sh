#!/bin/zsh
# Build build/dsrig_fe.dylib = the stock rig dylib sources (read only, from ../../../src) + fe_rig.c.
# Same file list and flags as src/build.sh. rig_cmd.c is compiled from a generated copy in ../build/ whose
# rig_exec DEFINITION is renamed rig_exec_orig (its caller in the same file keeps calling rig_exec = fe_rig.c).
set -e; cd "${0:A:h}"; S=../../../src; O=../build; mkdir -p $O/obj
CF=(-arch arm64 -mmacosx-version-min=12.0 -O2 -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -I$S -I$S/../win/math -DDSMATH_DEFAULT_ON=1)
sed 's/^char \*rig_exec(const char \*line) {/char *rig_exec_orig(const char *line) {/' $S/rig_cmd.c > $O/rig_cmd_fe.c
grep -q '^char \*rig_exec_orig(const char \*line) {' $O/rig_cmd_fe.c || { echo 'rig_exec definition not found'; exit 1; }
nice -n 19 clang $CF -c $O/rig_cmd_fe.c -o $O/obj/rig_cmd.o
nice -n 19 clang $CF -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/dsrig.dylib -o $O/dsrig_fe.dylib.tmp fe_rig.c $O/obj/rig_cmd.o \
  $S/rig_main.c $S/rig_hook.c $S/rig_sock.c $S/rig_win.c $S/rig_det.c \
  $S/mp_core.c $S/mp_config.c $S/mp_game.c \
  $S/net/dsnet_link.c $S/net/dsnet_server.c $S/net/dsnet_client.c $S/net/steam/dsnet_steam.c \
  $S/ord_core.c $S/ord_game.c $S/ord_mp.c $S/ord_test/ord_smoke.c \
  $S/ui_game.c $S/ui_model.c $S/ui_tip.c $S/ui_teams.c $S/ui_teams_model.c $S/mpmenu.c $S/mpmenu_model.c \
  $S/trn_logic.c $S/trn_game.c $S/trn_mp.c \
  $S/../win/math/math_mac.c $S/../win/math/build/libdsmath_arm64.a
codesign -f -s - $O/dsrig_fe.dylib.tmp 2>/dev/null
mv -f $O/dsrig_fe.dylib.tmp $O/dsrig_fe.dylib
echo built $O/dsrig_fe.dylib
