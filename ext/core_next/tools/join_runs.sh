#!/bin/zsh
# lane_join: wait for rig/release.lock to go (max 3 h), then join_freeze runs one after another on one leased pair:
# stock dylib, lane dylib (fast), lane dylib with DSRIG_JOIN_FAST=0. Logs: ext/core_next/lane_join/runs/
R=~/ds_multiplayer; T=$R/ext/core_next/tools; O=$R/ext/core_next/lane_join/runs
t0=$(date +%s)
while [ -e $R/rig/release.lock ]; do
  [ $(( $(date +%s) - t0 )) -gt 10800 ] && { echo "release.lock still there after 3 h"; exit 3; }
  sleep 120
done
echo "lock gone $(date)"
run() {   # name, args...
  local name=$1; shift
  while [ $(cd $R && python3 rig/rig.py leases | grep -vc ' free$') -ge 2 ]; do sleep 120; done
  echo "== $name $(date)"
  (cd $R/rig && nice -n 0 python3 $T/join_freeze_dylib.py "$@") > $O/$name.log 2>&1
  grep -E "SUMMARY|ERROR" $O/$name.log | tail -3
}
for spec in ${=RUNS:-stock fast nofast}; do
  case $spec in
    stock)  run stock_$(date +%H%M) --dylib $R/build/dsrig.dylib --joins ${JOINS:-3} ;;
    fast)   run fast_$(date +%H%M) --dylib $R/ext/core_next/build/dsrig_join.dylib --joins ${JOINS:-3} ;;
    nofast) run nofast_$(date +%H%M) --dylib $R/ext/core_next/build/dsrig_join.dylib --env DSRIG_JOIN_FAST=0 --joins ${JOINS:-3} ;;
  esac
done
echo "all done $(date)"
