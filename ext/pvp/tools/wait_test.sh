#!/bin/zsh
# wait_test.sh <tag> [max_s]: wait until the test log has its RESULT line (or the process is gone), print the results.
R=~/ds_multiplayer; L=$R/ext/pvp/out/$1.log; M=${2:-570}
for i in $(seq 1 $((M / 15))); do grep -q " RESULT \|Traceback" $L && break; pgrep -f "tag $1" >/dev/null || break; sleep 15; done
grep -E "PASS|FAIL|RESULT|Error|refused" $L | cut -c1-230 | tail -16
pgrep -f "tag $1" >/dev/null && echo "STILL RUNNING $(date +%H:%M)" || echo "ENDED $(date +%H:%M)"
