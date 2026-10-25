#!/bin/zsh
# Run menu_test.py combos while holding the dsuxmp lane's game-copy slot (one copy at a time for the whole lane).
# usage: run_menu.sh "en 960x540 1.4 [parts] [--mp DYLIB --tag NAME]" ["de 1728x1000 1.0" ...]
# The slot: load under 15, then mkdir ~/ds_multiplayer/rig/dsuxmp_copies.lock (taken = another sub-lane runs: exit 3);
# released on exit, errors and signals. Logs: ~/ToDelete_2026-10-25/dsuxmp/menu/runs/log_*.txt
cd ${0:A:h}
L=$HOME/ds_multiplayer/rig/dsuxmp_copies.lock
load=$(sysctl -n vm.loadavg | awk '{print int($2)}')
(( load < 15 )) || { echo "load $load: not now"; exit 2; }
mkdir $L 2>/dev/null || { echo "slot taken: $(cat $HOME/ToDelete_2026-10-25/dsuxmp/copies_holder.txt 2>/dev/null)"; exit 3; }
echo "uxmp-menu $(date '+%H:%M:%S')" > $HOME/ToDelete_2026-10-25/dsuxmp/copies_holder.txt
trap 'rmdir $L 2>/dev/null' EXIT INT TERM
D=$HOME/ToDelete_2026-10-25/dsuxmp/menu/runs; mkdir -p $D
for combo in "$@"; do
  a=(${=combo})
  f=$D/log_${a[1]}_${a[2]}_${a[3]}_$(date +%H%M%S).txt
  python3 menu_test.py $a > $f 2>&1
  echo "$combo rc $? $f"; grep '^RESULT' $f | sort -u
done
