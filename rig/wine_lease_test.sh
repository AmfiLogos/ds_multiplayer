#!/bin/zsh
# wine_lease_test.sh - rig/wine_lease.py on a temp lock and a fake bottle folder (never the real lock or bottle):
# two takers at once, busy bottle, stale lock, release by a non-owner, release kills the bottle's leftovers.
set -u
HERE=${0:A:h}; T=$(mktemp -d -t wine_lease_test)
export WINE_LEASE_LOCK=$T/wine.lock DSMP_BOTTLE=$T/Bottles/DSMP; mkdir -p $DSMP_BOTTLE/drive_c
W=(python3 $HERE/wine_lease.py); fails=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }

# two takers at once: exactly one gets it
$W take laneA --pid $$ > $T/a.out 2>&1 & pa=$!
$W take laneB --pid $$ > $T/b.out 2>&1 & pb=$!
wait $pa; ra=$?; wait $pb; rb=$?
check "two takers: exactly one wins (rc $ra / $rb)" "(( ra + rb == 1 ))"
win=laneA; (( ra )) && win=laneB; lose=laneB; (( ra )) && lose=laneA
check "two takers: the loser says held" "grep -q 'NOT taken: held' $T/a.out $T/b.out"

# release by a non-owner is refused, the lock stays
$W release $lose > $T/r.out 2>&1; r=$?
check "non-owner release refused (rc $r)" "(( r == 1 )) && grep -q 'NOT released: held by' $T/r.out && [[ \$(head -1 $WINE_LEASE_LOCK/owner) == $win ]]"
$W release $win > /dev/null 2>&1; check "owner release frees the lock" "[[ ! -e $WINE_LEASE_LOCK ]]"

# busy bottle: a process with its cwd in the bottle (stands for a leaked wine copy)
(cd $DSMP_BOTTLE/drive_c && exec sleep 60) & busy=$!; sleep 0.5
$W take laneA --pid $$ > $T/busy.out 2>&1; r=$?
check "busy bottle: take refused (rc $r)" "(( r == 1 )) && grep -q 'bottle is busy' $T/busy.out"
check "busy bottle: lock given back" "[[ ! -e $WINE_LEASE_LOCK ]]"
check "status shows the busy process" "$W status | grep -q $busy"

# release kills the owner's leftover bottle processes
mkdir $WINE_LEASE_LOCK; printf 'laneA\n%d\nnow\n' $$ > $WINE_LEASE_LOCK/owner      # held while the copy runs
$W release laneA > $T/rel.out 2>&1; r=$?; sleep 0.3
check "release stops the leftover (rc $r)" "(( r == 0 )) && ! kill -0 $busy 2>/dev/null && [[ ! -e $WINE_LEASE_LOCK ]]"

# a process outside the bottle (another bottle) never counts
mkdir -p $T/Bottles/Steam; (cd $T/Bottles/Steam && exec sleep 30) & other=$!; sleep 0.5
$W take laneA --pid $$ > /dev/null 2>&1; r=$?
check "another bottle's process does not count (rc $r)" "(( r == 0 ))"
$W release laneA > /dev/null 2>&1
check "release leaves another bottle's process alone" "kill -0 $other 2>/dev/null"; kill $other 2>/dev/null

# stale lock: the holder died
sleep 0 & dead=$!; wait $dead
$W take laneA --pid $dead > /dev/null 2>&1
$W take laneB --pid $$ > $T/stale.out 2>&1; r=$?
check "stale lock reclaimed (rc $r) with a log line" "(( r == 0 )) && grep -q 'reclaiming a STALE wine.lock' $T/stale.out && [[ \$(head -1 $WINE_LEASE_LOCK/owner) == laneB ]]"
# a live holder is never reclaimed; an old-style owner file (no pid) neither
$W take laneC --pid $$ > /dev/null 2>&1; r=$?
check "live holder kept (rc $r)" "(( r == 1 )) && [[ \$(head -1 $WINE_LEASE_LOCK/owner) == laneB ]]"
$W release laneB > /dev/null 2>&1; mkdir $WINE_LEASE_LOCK; echo oldlane > $WINE_LEASE_LOCK/owner
$W take laneC --pid $$ > /dev/null 2>&1; r=$?
check "old-style owner file kept (rc $r)" "(( r == 1 )) && [[ \$(cat $WINE_LEASE_LOCK/owner) == oldlane ]]"

echo "wine_lease_test: $fails failure(s) ($T)"
(( fails == 0 ))
