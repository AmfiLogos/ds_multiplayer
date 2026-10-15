#!/bin/zsh
# run_when_free.sh <test.py> <tag> [wait_s] [run_s]: wait (max wait_s) until < 2 pairs leased and no reserve.lock, then run
# the PvP test (max run_s). Prints the PASS / FAIL lines. Exit 3 = no slot in time.
R=~/ds_multiplayer; T=$1; TAG=$2; W=${3:-240}; RS=${4:-340}
for i in $(seq 1 $((W / 20))); do
  n=$(python3 $R/rig/rig.py leases | grep -vc free); [[ $n -lt 2 && ( ! -e $R/rig/reserve.lock || -n $(grep pvp $R/rig/reserve.lock) ) ]] && break; sleep 20
done
n=$(python3 $R/rig/rig.py leases | grep -vc free); [[ $n -lt 2 && ( ! -e $R/rig/reserve.lock || -n $(grep pvp $R/rig/reserve.lock) ) ]] || { echo "no slot $(date +%H:%M)"; exit 3; }
cd $R && timeout $RS /opt/homebrew/bin/python3 ext/pvp/tests/$T --tag $TAG ${@:5} > ext/pvp/out/$TAG.log 2>&1; echo "rc $?"
grep -E "PASS|FAIL|RESULT|Error|timeout|lease refused" ext/pvp/out/$TAG.log | cut -c1-220 | tail -14
