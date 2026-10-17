#!/bin/zsh
# Lane fepvp: rig smoke (pair) + mp2 3000 ticks on THIS worktree's build (build/dsrig.dylib + custom-modes next to it).
# Leases one pair (lane fepvp, waits for a free one), releases it after. Logs: ext/fepvp13/out/gate_<time>_*.log
set -u; W=${0:A:h:h:h}; R=~/ds_multiplayer/rig; RW=~/ds_multiplayer/ext/foreign_expedition/tools/run_with_dylib.py
D=$W/build/dsrig.dylib; T=$(date +%Y%m%d-%H%M); L=$W/ext/fepvp13/out/gate_$T
until PAIR=$(python3 $R/rig.py lease fepvp-$$ --pid $$ 2>/dev/null | tail -1); [[ $PAIR == [A-Z][A-Z] ]]; do sleep 60; done
trap 'python3 $R/rig.py release $PAIR fepvp-$$ >/dev/null' EXIT
cd $R
RIG_PAIR=$PAIR python3 $RW $D rig.py smoke > ${L}_smoke.log 2>&1
s=$(grep -c "fp_tick_advanced': True" ${L}_smoke.log)
RIG_PAIR=$PAIR MP2_PAIR=$PAIR python3 $RW $D mp2_test.py --ticks 3000 > ${L}_mp2.log 2>&1
p=$(grep -c " PASS " ${L}_mp2.log); f=$(grep -c " FAIL " ${L}_mp2.log); tb=$(grep -c Traceback ${L}_mp2.log)
pr=$(python3 rig.py check-prefs)
echo "gate $T pair $PAIR: smoke $s fp_tick_advanced; mp2 $p PASS, $f FAIL, traceback $tb; prefs $pr"
grep " FAIL " ${L}_mp2.log | cut -c1-200
