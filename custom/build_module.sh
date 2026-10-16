#!/bin/zsh
# Build Custom Modes (MASTER_PLAN 2.1 / M4 split; PLAN_V13 section 4 "fixed interfaces"). Output in $OUT (default ../build):
#   custom-modes.dylib      the SOLO module: cm_module.c + every custom/<folder>/sources.txt; no link to multiplayer
#                           (title home, My Units, Gallery, Test Fights, Unit Builder work without it)
#   custom-modes-mp.dylib   cm_mp.c + every custom/<folder>/sources_mp.txt; two-level link to @rpath/multiplayer.dylib AND
#                           to @rpath/custom-modes.dylib (FE / PvP call My Units); loaded by cm_module.c only with multiplayer
#   mod.json                (with OUT given by the loader build: its mods/custom-modes folder)
# Folder lists: one .c path per line, relative to custom/ ("#" lines and blank lines skipped). Each folder that has a list
# is also an include folder. Lane units owns this script; other lanes edit only their own lists (XM reads the same lists).
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
SOLO=(); MPS=(); INC=()
for d in */(N/); do
  d=${d%/}
  [[ -f $d/sources.txt || -f $d/sources_mp.txt ]] && INC+=(-I$d)
  [[ -f $d/sources.txt ]] && SOLO+=(${(f)"$(grep -v '^[[:space:]]*\(#\|$\)' $d/sources.txt || true)"})
  [[ -f $d/sources_mp.txt ]] && MPS+=(${(f)"$(grep -v '^[[:space:]]*\(#\|$\)' $d/sources_mp.txt || true)"})
done
for f in $SOLO $MPS; do [[ -f $f ]] || { echo "custom-modes: listed source $f not found" >&2; exit 1; }; done
CF=(-arch arm64 -mmacosx-version-min=12.0 -O2 -fwrapv -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -I$SDK -I$MP_SRC -I. $INC)
# -fwrapv: QuickJS (custom/fights) needs wrapping signed overflow; harmless for the rest
# solo: AppKit / CoreFoundation for the clipboard (units/mu_clip.c), CFNetwork + zlib for wiki short links
nice -n 19 clang $CF -dynamiclib -install_name @rpath/custom-modes.dylib -o $OUT/custom-modes.dylib.tmp cm_module.c $SOLO \
  -framework CoreFoundation -framework CFNetwork -lobjc -lz -Wl,-U,_cm_builder_open
codesign -f -s - $OUT/custom-modes.dylib.tmp 2>/dev/null; mv -f $OUT/custom-modes.dylib.tmp $OUT/custom-modes.dylib
nice -n 19 clang $CF -dynamiclib -install_name @rpath/custom-modes-mp.dylib -Wl,-rpath,@loader_path -o $OUT/custom-modes-mp.dylib.tmp \
  cm_mp.c $MPS $MP_DYLIB $OUT/custom-modes.dylib -lz
codesign -f -s - $OUT/custom-modes-mp.dylib.tmp 2>/dev/null; mv -f $OUT/custom-modes-mp.dylib.tmp $OUT/custom-modes-mp.dylib
[[ ${OUT:t} == custom-modes ]] && cp mod.json $OUT/mod.json
echo "built $OUT/custom-modes.dylib (${#SOLO} sources) + custom-modes-mp.dylib (${#MPS} sources; linked to ${MP_DYLIB:t} + custom-modes.dylib)"
