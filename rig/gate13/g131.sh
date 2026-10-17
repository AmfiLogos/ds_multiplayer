#!/bin/zsh
# g131.sh - the short v13.1 gate (owner: tests only where a failure means a real break). Builds are made before (MP
# worktree build/, LD build_mods + app + kit); this runs: zip ban scan, Mac first-start mod set (app dry run), smoke, l1
# soft restart, PvP Mac host + Windows guest (all checks: 0 desyncs, 500+ common ticks), Windows mods kit, My Units dive
# on Mac, mixed mod set refused at join.
source ${0:A:h}/lib.sh; H=${0:A:h}
REL=${REL:-$WT/ext/gate13/release_v131}
[[ -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }
inputs > $OUT/inputs.txt; IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v13.1 gate $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
trap "verdict ${0:A}" EXIT
chk g131_banscan 'PASS .*Windows.zip' $WT python3 $H/banscan.py $REL/Dungeon-Settlers-Modded-Mac.zip $REL/Dungeon-Settlers-Modded-Windows.zip
chk g131_first_start '^ok   active.json default' $LD zsh app/test/run.sh
chk g131_smoke 'smoke 3/3' $WT zsh $HOME/ds_multiplayer/ext/merger13/tools/gate.sh $D gate13 smoke
chk g131_l1_soft '13/13 checks, violations 0' $LD/loader python3 $H/ld_run.py l1_test.py $OUT/g131_l1
chk g131_uikit 'done: [0-9]+ passed, 0 failed, guard violations 0' $LD/loader python3 $H/ld_run.py uikit_test.py $OUT/g131_uikit --no-build
pvpx() {   # PvP v1, Mac host + Windows guest W (C:\DSW, this build's baselib.dll); every check incl. 6 (0 desyncs, 500+ ticks)
  local DSW="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW" R=$HOME/ds_multiplayer/rig/rig.py t=$(date +%Y%m%d-%H%M%S)
  (cd $WT/win && nice -n 19 zsh build_win.sh > $OUT/g131_build_win.log 2>&1) || { echo "win build failed"; return 1; }
  mkdir -p "$DSW/old_gate13"; for f in baselib.dll det_ranges_win.tsv; do mv "$DSW/$f" "$DSW/old_gate13/$f.$t"; cp $WT/win/build/$f "$DSW/$f"; done
  python3 $HOME/ds_multiplayer/rig/wine_lease.py take gate13 --pid $$ || { echo "wine busy / locked"; return 1; }
  local P=$(python3 $R lease gate13-$$ --pid $$ | tail -1) rc=1 XL=$OUT/g131_pvp_raw.log
  if [[ $P == [A-J][A-J] ]]; then
    (cd $HOME/ds_multiplayer/win && python3 xplay_run.py $P $WT/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib $D --tag gate131_xv1) > $XL 2>&1
    command grep -E '(PASS|FAIL) [0-9]' $XL; echo "raw log (xplay_run's release() stub raises at the end): $XL"
    local f=$(command grep -cE '^[0-9:]+ FAIL [0-9]' $XL) p=$(command grep -cE '^[0-9:]+ PASS [0-9]' $XL)
    (( f == 0 && p >= 10 )) && { echo "XPV PASS all $p checks"; rc=0; } || echo "XPV FAIL $f failed"
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/win && python3 wrig.py stop) >/dev/null 2>&1; python3 $R release $P gate13-$$ >/dev/null
  fi
  python3 $HOME/ds_multiplayer/rig/wine_lease.py release gate13 >/dev/null; return $rc
}
chk g131_pvp_xplay 'XPV PASS' $WT pvpx
# expect: g11_win_mods
GATE_ONLY="g11_win_mods" zsh $H/g11.sh
chk g131_mu_dive_mac 'RESULT ([0-9]+)/\1 pass' $WT python3 rig/gate13/mu_dive_mac.py
chk g131_mixed_set 'RESULT 4/4 pass' $WT python3 rig/gate13/mixed_set.py
print "g131_inputs_unchanged $([[ $(inputs | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "g131_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
