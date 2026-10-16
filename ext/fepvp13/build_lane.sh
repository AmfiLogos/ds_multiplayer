#!/bin/zsh
# Lane fepvp build until lane units' skeleton (custom/*/sources_mp.txt) is on main: src/build.sh, then custom-modes-mp.dylib
# again with this lane's new files (custom/build_module.sh belongs to lane units). Output: <worktree>/build.
set -e; W="${0:A:h:h:h}"; cd $W
OUT=$W/build zsh src/build.sh | tail -2
cd custom
CF=(-arch arm64 -mmacosx-version-min=12.0 -O2 -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -I../sdk -I../src -I. -Ife -Ipvp -Iunits)
nice -n 19 clang $CF -dynamiclib -install_name @rpath/custom-modes-mp.dylib -o $W/build/custom-modes-mp.dylib.tmp \
  cm_mp.c \
  fe/fe_json.c fe/fe_save.c fe/fe_game.c fe/fe_menu.c fe/fe_hub.c \
  pvp/pvp_core.c pvp/pvp_modcheck.c pvp/pvp_mods.c pvp/pvp_game.c pvp/pvp_ui.c \
  units/pvp_units.c units/pvp_wiki.c units/pvp_build.c \
  $W/build/dsrig.dylib -lz
codesign -f -s - $W/build/custom-modes-mp.dylib.tmp 2>/dev/null; mv -f $W/build/custom-modes-mp.dylib.tmp $W/build/custom-modes-mp.dylib
echo "built $W/build/custom-modes-mp.dylib (lane files)"
