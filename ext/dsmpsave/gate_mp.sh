#!/bin/zsh
# rig/gate.sh on a given build (DYLIB=<path>, default this worktree's build/dsrig.dylib) without its build step: rig.py
# hard-codes the shared ~/ds_multiplayer/build/dsrig.dylib, so gate.sh run from a worktree tests that file, not the lane's.
# smoke + mp2 on a leased pair, then left_prompt_pair --start (its own lease). Logs: <worktree>/rig/det_out/gatemp_<time>_*.log
set -u; W=${0:A:h:h:h}; cd $W/rig
export DYLIB=${DYLIB:-$W/build/dsrig.dylib}; R=(python3 $W/ext/dsmpsave/with_dylib.py)
PAIR=$(python3 rig.py lease gatemp-$$ --pid $$ 2>/dev/null | tail -1)
[[ $PAIR == [A-Z][A-Z] ]] || { echo "no free pair (rig.py leases)"; exit 2; }
export RIG_PAIR=$PAIR MP2_PAIR=$PAIR
trap 'python3 rig.py release $PAIR gatemp-$$ >/dev/null' EXIT
mkdir -p det_out; T=$(date +%Y%m%d-%H%M); L=det_out/gatemp_$T
$R rig/rig.py smoke > ${L}_smoke.log 2>&1
s=$(grep -c "fp_tick_advanced': True" ${L}_smoke.log)
$R rig/mp2_test.py --ticks 3000 > ${L}_mp2.log 2>&1; m=$?
p=$(grep -c " PASS " ${L}_mp2.log); f=$(grep -c " FAIL " ${L}_mp2.log); tb=$(grep -c Traceback ${L}_mp2.log)
python3 rig.py release $PAIR gatemp-$$ >/dev/null      # left_prompt_pair leases its own pair
$R src/ui_test/left_prompt_pair.py --start > ${L}_leftprompt.log 2>&1
lp=$(grep -c "^PASS\| PASS " ${L}_leftprompt.log); lf=$(grep -c "^FAIL\| FAIL " ${L}_leftprompt.log); ltb=$(grep -c Traceback ${L}_leftprompt.log)
PAIR=$(python3 rig.py lease gatemp-$$ --pid $$ 2>/dev/null | tail -1)
pr=$(python3 rig.py check-prefs)
echo "gatemp $T on $DYLIB: smoke $s/3 copies ok; mp2 $p PASS (need 19), $f FAIL, traceback $tb, exit $m; left_prompt --start $lp PASS, $lf FAIL, traceback $ltb; prefs $pr"
grep " FAIL " ${L}_mp2.log | cut -c1-200
grep "FAIL" ${L}_leftprompt.log | cut -c1-200
[[ $s == 3 && $p -ge 19 && $m == 0 && $f == 0 && $tb == 0 && $lf == 0 && $ltb == 0 && $lp -gt 0 && $pr == identical ]]
