#!/bin/zsh
# Integration gate (determinism lane): build the joined dylib, then rig smoke + mp2 quick run on a free pair.
# Leases a free pair (lane "gate") and releases it after. Logs: det_out/gate_<time>_{build,smoke,mp2}.log
set -u; cd "${0:A:h}"
PAIR=$(python3 rig.py lease gate-$$ 2>/dev/null | tail -1)
[[ $PAIR == [A-Z][A-Z] ]] || { echo "no free pair (rig.py leases)"; exit 2; }
export RIG_PAIR=$PAIR MP2_PAIR=$PAIR
trap 'python3 rig.py release $PAIR gate-$$ >/dev/null' EXIT
T=$(date +%Y%m%d-%H%M); L=det_out/gate_$T
../src/build.sh > ${L}_build.log 2>&1 && grep -q "^built" ${L}_build.log || { echo "BUILD FAILED: ${L}_build.log"; exit 1; }
grep -E "warning|error" ${L}_build.log | head -5
python3 rig.py smoke > ${L}_smoke.log 2>&1
s=$(grep -c "fp_tick_advanced': True" ${L}_smoke.log)
python3 mp2_test.py --ticks 3000 > ${L}_mp2.log 2>&1; m=$?
p=$(grep -c " PASS " ${L}_mp2.log); f=$(grep -c " FAIL " ${L}_mp2.log); tb=$(grep -c Traceback ${L}_mp2.log)
python3 rig.py release $PAIR gate-$$ >/dev/null      # left_prompt_pair leases its own pair
python3 ../src/ui_test/left_prompt_pair.py --start > ${L}_leftprompt.log 2>&1
lp=$(grep -c "^PASS\| PASS " ${L}_leftprompt.log); lf=$(grep -c "^FAIL\| FAIL " ${L}_leftprompt.log); ltb=$(grep -c Traceback ${L}_leftprompt.log)
PAIR=$(python3 rig.py lease gate-$$ 2>/dev/null | tail -1)
pr=$(python3 rig.py check-prefs)
echo "gate $T: smoke $s/3 copies ok; mp2 $p PASS (need 19), $f FAIL, traceback $tb, exit $m; left_prompt --start $lp PASS, $lf FAIL, traceback $ltb; prefs $pr"
grep " FAIL " ${L}_mp2.log | cut -c1-200
grep "FAIL" ${L}_leftprompt.log | cut -c1-200
[[ $s == 3 && $p -ge 19 && $m == 0 && $f == 0 && $tb == 0 && $lf == 0 && $ltb == 0 && $lp -gt 0 && $pr == identical ]]
