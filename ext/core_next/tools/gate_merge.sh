#!/bin/zsh
# rig/gate.sh, but the merged src is built to build/merge_corenext.dylib (night rule 9) and every test uses it.
set -u; cd ~/ds_multiplayer/rig
D=~/ds_multiplayer/build/merge_corenext.dylib; W=~/ds_multiplayer/ext/core_next/tools/run_with_dylib.py
T=$(date +%Y%m%d-%H%M); L=det_out/gate_corenext_$T
sed 's|-o ../build/dsrig.dylib.tmp|-o ../build/merge_corenext.dylib.tmp|; s|codesign -f -s - ../build/dsrig.dylib.tmp|codesign -f -s - ../build/merge_corenext.dylib.tmp|; s|mv -f ../build/dsrig.dylib.tmp ../build/dsrig.dylib|mv -f ../build/merge_corenext.dylib.tmp ../build/merge_corenext.dylib|; s|echo built ../build/dsrig.dylib|echo built ../build/merge_corenext.dylib|' ../src/build.sh > ../src/.build_merge_corenext.sh
zsh ../src/.build_merge_corenext.sh > ${L}_build.log 2>&1; mv ../src/.build_merge_corenext.sh ~/ds_multiplayer/ext/core_next/old/build_merge_corenext_$T.sh
grep -q "^built" ${L}_build.log || { echo "BUILD FAILED"; exit 1; }
PAIR=$(python3 rig.py lease corenext_gate-$$ 2>/dev/null | tail -1)
[[ $PAIR == [A-Z][A-Z] ]] || { echo "no free pair"; exit 2; }
export RIG_PAIR=$PAIR MP2_PAIR=$PAIR
trap 'python3 rig.py release $PAIR corenext_gate-$$ >/dev/null' EXIT
python3 $W $D rig.py smoke > ${L}_smoke.log 2>&1
s=$(grep -c "fp_tick_advanced': True" ${L}_smoke.log)
python3 $W $D mp2_test.py --ticks 3000 > ${L}_mp2.log 2>&1
p=$(grep -c " PASS " ${L}_mp2.log); f=$(grep -c " FAIL " ${L}_mp2.log); tb=$(grep -c Traceback ${L}_mp2.log)
python3 rig.py release $PAIR corenext_gate-$$ >/dev/null
python3 $W $D ../src/ui_test/left_prompt_pair.py --start > ${L}_leftprompt.log 2>&1
lp=$(grep -c "^PASS\| PASS " ${L}_leftprompt.log); lf=$(grep -c "^FAIL\| FAIL " ${L}_leftprompt.log); ltb=$(grep -c Traceback ${L}_leftprompt.log)
PAIR=$(python3 rig.py lease corenext_gate-$$ 2>/dev/null | tail -1)
pr=$(python3 rig.py check-prefs)
echo "gate_corenext $T: smoke $s/3; mp2 $p PASS, $f FAIL, tb $tb; left_prompt $lp PASS, $lf FAIL, tb $ltb; prefs $pr"
grep " FAIL " ${L}_mp2.log | cut -c1-200; grep "FAIL" ${L}_leftprompt.log | cut -c1-200
