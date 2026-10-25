#!/bin/zsh
# Run menu_test.py combos on the hidden copy UMN while holding one game-copy slot of lane dsuxmp2 (slot.sh: load < 15,
# a free slot; released on exit, errors and signals). Max 25 min per hold: give at most about 6 combos per call.
# usage: run_menu.sh "en 960x540 1.4 [parts] --mp DYLIB [--tag NAME]" ["de 1728x1000 1.0 ..." ...]
# DYLIB: LOADER=~/ds_launcher_wt_dsuxmp2/loader zsh ext/dsuxmp/build_mouse.sh <out> (the fake mouse; SRC=<tree>/src for another tree).
# Logs: ~/ToDelete_2026-10-25/dsuxmp2/dsuxmp2-menu/runs/log_*.txt
cd ${0:A:h}
S=$HOME/ToDelete_2026-10-25/dsuxmp2/slot.sh; WHO=dsuxmp2-menu
$S take $WHO 1 1800 || exit 3
trap "$S release $WHO >/dev/null" EXIT INT TERM
D=$HOME/ToDelete_2026-10-25/dsuxmp2/dsuxmp2-menu/runs; mkdir -p $D
for combo in "$@"; do
  a=(${=combo})
  tag=${a[(r)--tag]:+_${a[$(( ${a[(i)--tag]} + 1 ))]}}
  f=$D/log_${a[1]}_${a[2]}_${a[3]}${tag}_$(date +%H%M%S).txt
  python3 menu_test.py $a > $f 2>&1
  echo "$combo rc $? $f"; grep '^RESULT' $f | sort -u
done
