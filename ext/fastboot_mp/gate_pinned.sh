#!/bin/zsh
# rig/gate.sh's smoke + mp2 + left_prompt steps on a GIVEN dylib (no build, build/dsrig.dylib untouched):
#   zsh ext/fastboot_mp/gate_pinned.sh <dylib> [outdir]
# Leases one pair (unique name), runs rig.py smoke and mp2_test.py --ticks 3000 pinned to <dylib> (rig/pin_run.py),
# releases it, then left_prompt_pair.py --start (it leases its own pair). Same pass rule as gate.sh.
set -u
R=$HOME/ds_multiplayer/rig; D=${1:A}; O=${2:-$PWD}; mkdir -p $O
L=fastboot_mp-$$
PAIR=$(python3 $R/rig.py lease $L 2>/dev/null | tail -1)
[[ $PAIR == [A-Z][A-Z] ]] || { echo "no free pair"; exit 2; }
trap 'python3 $R/rig.py release $PAIR $L >/dev/null 2>&1' EXIT
export RIG_PAIR=$PAIR MP2_PAIR=$PAIR
cd $R
python3 pin_run.py $D rig.py smoke > $O/smoke.log 2>&1
s=$(grep -c "fp_tick_advanced': True" $O/smoke.log)
python3 pin_run.py $D mp2_test.py --ticks 3000 > $O/mp2.log 2>&1; m=$?
p=$(grep -c " PASS " $O/mp2.log); f=$(grep -c " FAIL " $O/mp2.log); tb=$(grep -c Traceback $O/mp2.log)
python3 rig.py release $PAIR $L >/dev/null
python3 pin_run.py $D ../src/ui_test/left_prompt_pair.py --start > $O/leftprompt.log 2>&1
lp=$(grep -c "^PASS\| PASS " $O/leftprompt.log); lf=$(grep -c "^FAIL\| FAIL " $O/leftprompt.log); ltb=$(grep -c Traceback $O/leftprompt.log)
pr=$(python3 rig.py check-prefs)
echo "gate_pinned $PAIR: smoke $s/3 copies ok; mp2 $p PASS (need 19), $f FAIL, traceback $tb, exit $m; left_prompt --start $lp PASS, $lf FAIL, traceback $ltb; prefs $pr"
grep " FAIL " $O/mp2.log | cut -c1-200; grep "FAIL" $O/leftprompt.log | cut -c1-200
[[ $s == 3 && $p -ge 19 && $m == 0 && $f == 0 && $tb == 0 && $lf == 0 && $ltb == 0 && $lp -gt 0 && $pr == identical ]]
