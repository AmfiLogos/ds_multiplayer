#!/bin/zsh
# Wait (max 3 h) until rig/release.lock is gone and a pair can be leased, then run det_test_rand.py once.
R=~/ds_multiplayer/rig; T=~/ds_multiplayer/ext/core_next/tools; end=$((SECONDS + 10800))
while (( SECONDS < end )); do
  if [ ! -e $R/release.lock ]; then
    python3 $T/det_test_rand.py "$@"; rc=$?
    [ $rc -ne 3 ] && exit $rc
  fi
  sleep 120
done
echo "gave up after 3 h (release.lock or no free pair)"; exit 4
