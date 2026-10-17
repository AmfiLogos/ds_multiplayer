#!/bin/zsh
# rig/gate.sh test part on build/merge_pvp.dylib (night rule 9; built by ext/pvp/tools/merge_full.sh). Copy of
# ext/core_next/tools/gate_merge.sh without the build. Lease lane "pvp_gate".
set -u; cd ~/ds_multiplayer/rig
D=~/ds_multiplayer/build/merge_pvp.dylib; W=~/ds_multiplayer/ext/pvp/tools/run_with_dylib.py
T=$(date +%Y%m%d-%H%M); L=det_out/gate_pvp_$T
PAIR=$(python3 rig.py lease pvp_gate-$$ 2>/dev/null | tail -1)
[[ $PAIR == [A-Z][A-Z] ]] || { echo "no free pair"; exit 2; }
export RIG_PAIR=$PAIR MP2_PAIR=$PAIR
trap 'python3 rig.py release $PAIR pvp_gate-$$ >/dev/null' EXIT
python3 $W $D rig.py smoke > ${L}_smoke.log 2>&1
s=$(grep -c "fp_tick_advanced': True" ${L}_smoke.log)
python3 $W $D mp2_test.py --ticks 3000 > ${L}_mp2.log 2>&1
p=$(grep -c " PASS " ${L}_mp2.log); f=$(grep -c " FAIL " ${L}_mp2.log); tb=$(grep -c Traceback ${L}_mp2.log)
python3 rig.py release $PAIR pvp_gate-$$ >/dev/null
python3 $W $D ../src/ui_test/left_prompt_pair.py --start > ${L}_leftprompt.log 2>&1
lp=$(grep -c "^PASS\| PASS " ${L}_leftprompt.log); lf=$(grep -c "^FAIL\| FAIL " ${L}_leftprompt.log); ltb=$(grep -c Traceback ${L}_leftprompt.log)
pr=$(python3 rig.py check-prefs)
echo "gate_pvp $T: smoke $s/3; mp2 $p PASS, $f FAIL, tb $tb; left_prompt $lp PASS, $lf FAIL, tb $ltb; prefs $pr"
grep " FAIL " ${L}_mp2.log | cut -c1-200; grep "FAIL" ${L}_leftprompt.log | cut -c1-200
