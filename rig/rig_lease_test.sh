#!/bin/zsh
# rig_lease_test.sh - rig.py pair leases on a temp lease folder and fake instances/ (never the real leases or copies):
# two lanes, a leaked copy without a lease, release by a non-owner, release stops the owner's leftovers, dead holder.
# A "copy" here is a sleep whose working folder is instances/<X>/ (rig.py finds it by its open files).
set -u
HERE=${0:A:h}; T=$(mktemp -d -t rig_lease_test)
export DSRIG_INSTANCES=$T/inst DSRIG_LEASE_DIR=$T/l; mkdir -p $DSRIG_LEASE_DIR; for x in A B C D E F G H I J; do mkdir -p $DSRIG_INSTANCES/$x; done
echo '{}' > $DSRIG_LEASE_DIR/leases.json
R=(python3 $HERE/rig.py); fails=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }
copy() { (cd $DSRIG_INSTANCES/$1 && exec sleep 120) & }      # prints nothing; pid in $!
holder() { python3 -c "import json,sys; print(json.load(open('$DSRIG_LEASE_DIR/leases.json')).get('$1',{}).get(sys.argv[1],''))" $2; }

# two lanes get different pairs; the holder pid is recorded
p1=$($R lease laneA --pid $$ 2>/dev/null); p2=$($R lease laneB --pid $$ 2>/dev/null)
check "two lanes: different pairs ($p1, $p2)" "[[ -n \$p1 && -n \$p2 && \$p1 != \$p2 ]]"
check "lease records the holder pid" "[[ \$(holder $p1 pid) == $$ ]]"
s=$(sh -c "$R[*] lease laneZ" 2>/dev/null); zp=$(holder $s pid)
check "lease without --pid: holder = the caller, not empty ($zp)" "[[ -n \$zp && \$zp != None ]]"
$R release $s laneZ >/dev/null 2>&1
# one lane name, two live holders (two runs of the same test): never the same pair; the same holder gets its pair back
sleep 120 & h1=$!; sleep 120 & h2=$!
s1=$($R lease fepvp --pid $h1 2>/dev/null); s2=$($R lease fepvp --pid $h2 2>/dev/null); s3=$($R lease fepvp --pid $h1 2>/dev/null)
check "same lane, two live holders: different pairs ($s1, $s2)" "[[ -n \$s1 && -n \$s2 && \$s1 != \$s2 ]]"
check "same holder again: its own pair ($s3)" "[[ \$s3 == \$s1 ]]"
kill $h1 $h2; $R release $s1 fepvp >/dev/null 2>&1; $R release $s2 fepvp >/dev/null 2>&1
$R release $p1 laneA >/dev/null 2>&1; $R release $p2 laneB >/dev/null 2>&1

# a leaked copy (no lease) in A: AB is not given
copy A; leak=$!; sleep 0.5
p=$($R lease laneC --pid $$ 2>$T/c.err)
check "leaked copy in A: AB not given (got $p)" "[[ -n \$p && \$p != AB ]] && grep -q 'AB skipped' $T/c.err"
check "status shows AB busy without a lease" "$R[*] status 2>/dev/null | grep -q '^AB: no lease; processes: $leak'"
kill $leak

# release by a non-owner is refused, by lane name and without one
$R release $p laneA > $T/r1.out 2>&1
check "release by another lane refused" "grep -q 'not released' $T/r1.out && [[ \$(holder $p lane) == laneC ]]"
sleep 120 & other=$!
python3 - <<EOF
import json; f='$DSRIG_LEASE_DIR/leases.json'; d=json.load(open(f)); d['$p']['pid']=$other; json.dump(d, open(f,'w'))
EOF
$R release $p > $T/r2.out 2>&1
check "release without lane by a non-holder refused" "grep -q 'release refused' $T/r2.out && [[ \$(holder $p lane) == laneC ]]"
kill $other

# release by the owner stops its leftover copies
copy ${p[1]}; left=$!; sleep 0.5
$R release $p laneC > $T/r3.out 2>&1; sleep 0.3
check "owner release: leftover copy stopped, lease gone" "grep -q '^released' $T/r3.out && ! kill -0 $left 2>/dev/null && [[ -z \$(holder $p lane) ]]"

# dead holder: its copies are stopped, then the pair is reclaimed (after the 10 min grace)
sh -c 'exit 0' & dead=$!; wait $dead
p=$($R lease laneD --pid $dead 2>/dev/null); copy ${p[1]}; c1=$!; sleep 0.5
q=$($R lease laneE --pid $$ 2>/dev/null)
check "dead holder within 10 min: kept, copy alive ($p / $q)" "[[ \$q != \$p ]] && kill -0 $c1 2>/dev/null"
$R release $q laneE >/dev/null 2>&1
python3 - <<EOF
import json; f='$DSRIG_LEASE_DIR/leases.json'; d=json.load(open(f)); d['$p']['since'] -= 700; json.dump(d, open(f,'w'))
EOF
q=$($R lease laneE --pid $$ 2>$T/e.err); sleep 0.3
check "dead holder: copy stopped before the reclaim" "! kill -0 $c1 2>/dev/null"
check "dead holder: pair reclaimed, logged ($q)" "[[ \$q == $p ]] && grep -q 'holder dead; copies stopped; freed' $DSRIG_LEASE_DIR/lease_free.log"

echo "rig_lease_test: $fails failure(s) ($T)"
(( fails == 0 ))
