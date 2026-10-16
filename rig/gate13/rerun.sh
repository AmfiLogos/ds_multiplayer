#!/bin/zsh
# rerun.sh - reruns of the final pass's red items after runner fixes (same frozen inputs; checked against inputs.txt)
source ${0:A:h}/lib.sh; H=${0:A:h}
print "rerun $(date '+%F %T')" >> $SUM
zsh $H/g5.sh
chk g9_mac_mods '(^| )8 PASS 0 FAIL' $WT python3 ext/crossmods13/mac_mods_test.py $LD/loader --game-s 120
chk g8_apply_cold 'B on screen [0-9.]+ s after the request' $LD/loader zsh -c "zsh test/install_mods.sh test_env/instances/V/DungeonSettlers.app test/profile_game.json && python3 $WT/rig/gate13/v13t_run.py cold 3 $OUT/g8_cold"
chk g8_apply_prewarm 'B on screen [0-9.]+ s after the request' $LD/loader python3 $WT/rig/gate13/v13t_run.py prewarm 3 $OUT/g8_prewarm
zsh $H/g7.sh
zsh $H/g11.sh
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "rerun_inputs_unchanged $([[ $(inputs | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
