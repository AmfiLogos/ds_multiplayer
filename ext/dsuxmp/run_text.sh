#!/bin/zsh
# uxmp-text: take the dsuxmp lane's copy slot (load under 15, one copy at a time), run one shot combo, release (also on
# errors / signals).  run_text.sh <en|de|ru> <W>x<H> <ui scale> [parts]
cd ${0:A:h}
LOCK=$HOME/ds_multiplayer/rig/dsuxmp_copies.lock
load=$(sysctl -n vm.loadavg | awk '{print $2}')
(( ${load%.*} < 15 )) || { echo "load $load: not now"; exit 2; }
mkdir $LOCK 2>/dev/null || { echo "slot busy: $(cat $HOME/ToDelete_2026-10-25/dsuxmp/copies_holder.txt 2>/dev/null)"; exit 3; }
trap 'rmdir $LOCK 2>/dev/null' EXIT INT TERM
echo "uxmp-text $(date +%H:%M:%S)" > $HOME/ToDelete_2026-10-25/dsuxmp/copies_holder.txt
L=$HOME/ToDelete_2026-10-25/dsuxmp/shots/text/runs; mkdir -p $L
python3 shoot_text.py "$@" > $L/log_$1_$2_$3_$(date +%H%M%S).txt 2>&1
echo "rc $?"
