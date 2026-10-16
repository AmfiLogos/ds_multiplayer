#!/bin/zsh
# g1ld.sh - loader copy tests (G1 + G8 + G10) on ~/ds_launcher/loader/build: l1_test, l1_v13_test x3 (flake watch:
# "still in the world after refused load"), l1_ml_test with the PUBLISHED MelonLoader zip (sha = pin), l1_bie_test with
# the shipped BepInEx installer (pin 0.1.2), uikit_test --no-build.
source ${0:A:h}/lib.sh
P=${0:A:h}/ld_run.py; ML=$WT/ext/gate13/out/g12/melonloader-mac-arm64-0.1.0.zip
chk g1_l1 '13/13 checks, violations 0' $LD/loader python3 $P l1_test.py $OUT/l1
for i in 1 2 3; do chk g8_l1_v13_$i '18/18 checks, violations 0' $LD/loader python3 $P l1_v13_test.py $OUT/l1_v13_$i; done
chk g10_l1_ml '17/17 checks, violations 0' $LD/loader python3 $P --no-lease l1_ml_test.py $OUT/l1_ml $ML
chk g10_l1_bie '14/14 checks, violations 0' $LD/loader python3 $P l1_bie_test.py $OUT/l1_bie
chk g1_uikit 'done: [0-9]+ passed, 0 failed, guard violations 0' $LD/loader python3 $P uikit_test.py $OUT/uikit --no-build
