#!/bin/zsh
# g5.sh - G5 My Units in loader copies (units13 / builder13 tests; test library through DSMP_UNITS_LIB in cmrig, never
# the owner's). Uses $WT/build (dsrig + custom-modes + units-driver, built by home_test) and ~/ds_launcher/loader/build.
source ${0:A:h}/lib.sh
U=$WT/ext/units13/test; B=$WT/ext/builder13/test
chk g5_home ' 22 PASS 0 FAIL' $WT python3 $U/home_test.py $OUT/g5_home
chk g5_myunits ' 26 PASS 0 FAIL' $WT python3 $U/myunits_test.py $OUT/g5_myunits
chk g5_myunits_075 ' 26 PASS 0 FAIL' $WT python3 $U/myunits_test.py $OUT/g5_myunits_075 --scale=0.75
chk g5_myunits_140 ' 21 PASS 0 FAIL' $WT python3 $U/myunits_test.py $OUT/g5_myunits_140 --scale=1.4 --net=0
chk g5_bring ' 32 PASS 0 FAIL' $WT python3 $U/bring_test.py $OUT/g5_bring
chk g5_bring_075 ' [0-9]+ PASS 0 FAIL' $WT python3 $U/bring_test.py $OUT/g5_bring_075 --scale=0.75
chk g5_bring_140 ' [0-9]+ PASS 0 FAIL' $WT python3 $U/bring_test.py $OUT/g5_bring_140 --scale=1.4
chk g5_bring_rig ' 5 PASS 0 FAIL' $WT python3 $U/bring_rig_test.py $OUT/g5_bring_rig
BO=$WT/ext/builder13/out/builder_$(date +%Y%m%d-%H%M%S)_gate13   # tf_check reads the newest ext/builder13/out/builder_2*/library_after.json
chk g5_builder ' 32 PASS 0 FAIL' $WT env CMRIG_INST=R python3 $B/builder_test.py $BO
chk g5_builder_075 ' 11 PASS 0 FAIL' $WT env CMRIG_INST=R python3 $B/builder_test.py $OUT/g5_builder_075 --scale=0.75 --short
chk g5_builder_140 ' 11 PASS 0 FAIL' $WT env CMRIG_INST=R python3 $B/builder_test.py $OUT/g5_builder_140 --scale=1.4 --short
chk g5_tf_check '0 errors, 0 problems.*0 different' $WT zsh $B/tf_check.sh
print "g5_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
