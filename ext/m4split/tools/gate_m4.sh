#!/bin/zsh
# rig/gate.sh test part (smoke 3 copies, mp2 3000 ticks, left_prompt --start) on build/merge_m4split/dsrig.dylib + the
# custom-modes dylibs next to it (rig_main.c loads them like the loader). Copy of ext/pvp/tools/gate_merge.sh. Lane m4split_gate.
set -u; cd ~/ds_multiplayer/rig
D=${M4_DYLIB:-~/ds_multiplayer/build/merge_m4split/dsrig.dylib}; W=~/ds_multiplayer/ext/m4split/tools/run_with_dylib.py
T=$(date +%Y%m%d-%H%M); L=det_out/gate_m4split_$T
PAIR=$(python3 rig.py lease m4split_gate-$$ 2>/dev/null | tail -1)
[[ $PAIR == [A-Z][A-Z] ]] || { echo "no free pair"; exit 2; }
export RIG_PAIR=$PAIR MP2_PAIR=$PAIR
trap 'python3 rig.py release $PAIR m4split_gate-$$ >/dev/null' EXIT
python3 $W $D rig.py smoke > ${L}_smoke.log 2>&1
s=$(grep -c "fp_tick_advanced': True" ${L}_smoke.log)
python3 $W $D mp2_test.py --ticks 3000 > ${L}_mp2.log 2>&1
p=$(grep -c " PASS " ${L}_mp2.log); f=$(grep -c " FAIL " ${L}_mp2.log); tb=$(grep -c Traceback ${L}_mp2.log)
python3 rig.py release $PAIR m4split_gate-$$ >/dev/null
python3 $W $D ../src/ui_test/left_prompt_pair.py --start > ${L}_leftprompt.log 2>&1
lp=$(grep -c "^PASS\| PASS " ${L}_leftprompt.log); lf=$(grep -c "^FAIL\| FAIL " ${L}_leftprompt.log); ltb=$(grep -c Traceback ${L}_leftprompt.log)
pr=$(python3 rig.py check-prefs)
echo "gate_m4split $T: smoke $s/3; mp2 $p PASS, $f FAIL, tb $tb; left_prompt $lp PASS, $lf FAIL, tb $ltb; prefs $pr"
grep " FAIL " ${L}_mp2.log | cut -c1-200; grep "FAIL" ${L}_leftprompt.log | cut -c1-200
