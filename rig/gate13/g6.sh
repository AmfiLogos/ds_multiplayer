#!/bin/zsh
# g6.sh - G6 Test Fights + Gallery in loader copy O (fights13 tf_test; installs from $WT/build_c4/game = the frozen build).
source ${0:A:h}/lib.sh
G=$WT/build_c4/game; mkdir -p $G
cp $WT/build/{dsrig,custom-modes,custom-modes-mp,units-driver}.dylib $G/ && cp $CM/mod.json $G/mod.json && cp $WT/ext/units13/driver/mod.json $G/units-driver.mod.json
T=$WT/ext/fights13/test/tf_test.py
chk g6_gallery_boss ' 22 PASS 0 FAIL' $WT python3 $T $OUT/g6_gallery_boss gallery boss
chk g6_arena_dps ' 17 PASS 0 FAIL' $WT env FIGHTS_LIB=$OUT/g6_gallery_boss/library.json python3 $T $OUT/g6_arena_dps arena dps
chk g6_solo ' 5 PASS 0 FAIL' $WT python3 $T $OUT/g6_solo solo
print "g6_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
