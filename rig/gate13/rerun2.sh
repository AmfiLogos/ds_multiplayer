#!/bin/zsh
# rerun2.sh - second rerun after runner fixes: G7 (native Mac host, as lane mpfix ran it), g11 win_mods / melon / PvP xplay
source ${0:A:h}/lib.sh; H=${0:A:h}
print "rerun2 $(date '+%F %T')" >> $SUM
zsh $H/g7.sh
GATE_ONLY="g11_win_mods g11_melon g11_pvp_v1_xplay" zsh $H/g11.sh
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "rerun2_inputs_unchanged $([[ $(inputs | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
