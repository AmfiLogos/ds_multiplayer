#!/bin/zsh
# Game build for lane fights tests: tree (mktree.sh: units13 + this worktree's fights/gallery) -> custom-modes.dylib +
# custom-modes-mp.dylib, the multiplayer dylib (MP_DYLIB, default main's build/dsrig.dylib) and lane units' test driver.
# Output: <worktree>/build_c4/game/. usage: build_game.sh [units branch]
set -e
W=${0:A:h:h:h:h}; S=/private/tmp/claude-502/-Users-user/bdaa4039-1c35-4536-98c6-538d80610db9/scratchpad/f13
B=$W/build_c4/game; mkdir -p $B
$W/ext/fights13/tools/mktree.sh $S ${1:-units13} >/dev/null
MP=${MP_DYLIB:-$HOME/ds_multiplayer/build/dsrig.dylib}
cp $MP $B/dsrig.dylib.tmp && mv -f $B/dsrig.dylib.tmp $B/dsrig.dylib
(cd $S/tree/custom && OUT=$B MP_DYLIB=$B/dsrig.dylib zsh build_module.sh) 2>&1 | grep -v "quickjs/" | grep -E "error|warning:|built" || true
cp $S/tree/custom/mod.json $B/mod.json
DRV=$HOME/ds_mp_units13/ext/units13/driver
nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -I$S/tree/sdk -dynamiclib -framework CoreFoundation -framework CoreGraphics \
  -lobjc -install_name @rpath/units-driver.dylib -o $B/units-driver.dylib.tmp $DRV/units_driver.c
codesign -f -s - $B/units-driver.dylib.tmp 2>/dev/null; mv -f $B/units-driver.dylib.tmp $B/units-driver.dylib
cp $DRV/mod.json $B/units-driver.mod.json
ls -la $B | awk '{print $5, $9}' | tail -n +4
