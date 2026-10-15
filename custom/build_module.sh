#!/bin/zsh
# Build Custom Modes (MASTER_PLAN 2.1 / M4 split). Output in $OUT (default ../build):
#   custom-modes.dylib      the module (cm_module.c; SDK entry points; no link to multiplayer)
#   custom-modes-mp.dylib   FE + PvP + units (cm_mp.c, fe/, pvp/, units/), two-level link to @rpath/multiplayer.dylib
#   mod.json                (with OUT given by the loader build: its mods/custom-modes folder)
# Env: MP_DYLIB = the multiplayer dylib to link against (default $OUT/dsrig.dylib, else $OUT/../multiplayer/multiplayer.dylib),
#      SDK = folder with dsl_sdk.h (default ../sdk), MP_SRC = multiplayer headers (default ../src).
# Called by src/build.sh (rig: next to dsrig.dylib) and by ~/ds_launcher/loader/build.sh (OUT=build/mods/custom-modes).
set -e; H="${0:A:h}"; cd "$H"
OUT=${OUT:-../build}; OUT=${OUT:A}; mkdir -p $OUT
SDK=${SDK:-../sdk}; SDK=${SDK:A}; MP_SRC=${MP_SRC:-../src}; MP_SRC=${MP_SRC:A}
if [[ -z "$MP_DYLIB" ]]; then
  if [[ -f $OUT/dsrig.dylib ]]; then MP_DYLIB=$OUT/dsrig.dylib; else MP_DYLIB=$OUT/../multiplayer/multiplayer.dylib; fi
fi
[[ -f $MP_DYLIB ]] || { echo "custom-modes: no multiplayer dylib to link against ($MP_DYLIB)" >&2; exit 1; }
CF=(-arch arm64 -mmacosx-version-min=12.0 -O2 -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -I$SDK -I$MP_SRC -I. -Ife -Ipvp -Iunits)
nice -n 19 clang $CF -dynamiclib -install_name @rpath/custom-modes.dylib -o $OUT/custom-modes.dylib.tmp cm_module.c
codesign -f -s - $OUT/custom-modes.dylib.tmp 2>/dev/null; mv -f $OUT/custom-modes.dylib.tmp $OUT/custom-modes.dylib
nice -n 19 clang $CF -dynamiclib -install_name @rpath/custom-modes-mp.dylib -o $OUT/custom-modes-mp.dylib.tmp \
  cm_mp.c \
  fe/fe_json.c fe/fe_save.c fe/fe_game.c fe/fe_menu.c \
  pvp/pvp_core.c pvp/pvp_modcheck.c pvp/pvp_mods.c pvp/pvp_game.c pvp/pvp_ui.c \
  units/pvp_units.c units/pvp_wiki.c units/pvp_build.c \
  $MP_DYLIB -lz
codesign -f -s - $OUT/custom-modes-mp.dylib.tmp 2>/dev/null; mv -f $OUT/custom-modes-mp.dylib.tmp $OUT/custom-modes-mp.dylib
[[ ${OUT:t} == custom-modes ]] && cp mod.json $OUT/mod.json
echo "built $OUT/custom-modes.dylib + custom-modes-mp.dylib (linked to ${MP_DYLIB:t})"
