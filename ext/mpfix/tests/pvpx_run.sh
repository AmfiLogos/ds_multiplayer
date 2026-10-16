#!/bin/zsh
# PvP v1 Mac host + Windows guest (copy W in the bottle DSMP, C:\DSW), like rig/gate13/g11.sh pvpx, on this checkout's builds:
# Mac build/dsrig.dylib, Windows win/build/baselib.dll (+ det_ranges_win.tsv). Lane mpfix. Old DSW files -> DSW/old_mpfix/.
# usage: pvpx_run.sh <tag>      log: ext/mpfix/out/pvpx_<tag>.log
WT=${0:A:h:h:h:h}; OUT=$WT/ext/mpfix/out; mkdir -p $OUT; T=${1:-run}
DSW="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW" R=$HOME/ds_multiplayer/rig/rig.py t=$(date +%Y%m%d-%H%M%S)
python3 $HOME/ds_multiplayer/rig/wine_lease.py take mpfix --pid $$ || { echo "wine busy / locked"; exit 1; }
mkdir -p "$DSW/old_mpfix"; for f in baselib.dll det_ranges_win.tsv; do mv "$DSW/$f" "$DSW/old_mpfix/$f.$t"; cp $WT/win/build/$f "$DSW/$f"; done
P=$(python3 $R lease mpfix --pid $$ | tail -1)
if [[ $P == [A-J][A-J] ]]; then
  (cd $HOME/ds_multiplayer/win && timeout 540 python3 xplay_run.py $P $WT/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib $WT/build/dsrig.dylib --tag mpfix_$T) > $OUT/pvpx_$T.log 2>&1
  command grep -E '(PASS|FAIL) [0-9]' $OUT/pvpx_$T.log
  np=$(command grep -cE 'PASS [0-9]' $OUT/pvpx_$T.log); nf=$(command grep -cE 'FAIL [0-9]|Traceback' $OUT/pvpx_$T.log)
  (( np > 0 && nf == 0 )) && rc=0 || { echo "FAIL pvpx: $np PASS, $nf FAIL/Traceback"; rc=1; }
  python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/win && python3 wrig.py stop) >/dev/null 2>&1; python3 $R release $P mpfix >/dev/null
else echo "FAIL not run: no pair: $P"; rc=1; fi
python3 $HOME/ds_multiplayer/rig/wine_lease.py release mpfix >/dev/null
exit $rc
