#!/bin/zsh
# g016.sh - hotfix gate: loader v0.1.6 bundle (ds_modloader hotfix-0.1.6 = bundle-v0.1.16 + uninstaller fix) and
# multiplayer-v0.1.6 (ds_multiplayer hotfix-0.1.6 = bundle-v0.1.16 + network fixes F3/F4, N1, N2). Short gate (owner rule):
# byte + save scan of every artifact; net unit tests big_cmd / full_big / hello_max (+ ASan); full-state checks on the blob path (fastjoin_pair); Windows: install into a folder with other files + uninstall keeps them (packaging's
# uninst_test), Setup.exe install + start + uninstall keeps saves; Mac: .dmg install + hidden start; the oversize P_CMD
# test (N1); rig smoke + mp2 on the hotfix multiplayer; PvP Mac host + Windows guest; mixed mod set refused; prefs.
# Env: WT = the MP hotfix worktree (build/ = its dylibs), LD = the loader hotfix worktree (its build/), REL = artifacts.
source ${0:A:h}/lib.sh; H=${0:A:h}
REL=${REL:-$WT/ext/gate13/release_v016}
[[ -z ${GATE_ONLY:-} && -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }   # a GATE_ONLY rerun appends to the same summary
if [[ -z ${GATE_ONLY:-} ]]; then
{ echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  pin $(head -1 $LD/loader/mp_pin.txt)  cm $(head -1 $LD/loader/cm_pin.txt)"
  for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } > $OUT/inputs.txt
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v0.1.6 hotfix gate $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
else
  IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64); print "rerun $(date '+%F %T') GATE_ONLY=$GATE_ONLY" >> $SUM
fi
trap "verdict ${0:A}" EXIT
scan() { local M=$(mktemp -d); hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/Dungeon-Settlers-Modded-Mac.dmg >/dev/null || return 1
  python3 $H/banscan.py $M $REL/Dungeon-Settlers-Modded-Mac.dmg $REL/Dungeon-Settlers-Modded-Setup.exe $REL/Dungeon-Settlers-Modded-Windows.zip $LD/loader/build/win/kit $REL/multiplayer-0.1.6.zip; local rc=$?
  hdiutil detach $M -quiet; (( rc == 0 )) && echo "SCAN ALL PASS"; return $rc; }
chk g016_scan 'SCAN ALL PASS' $WT scan
chk g016_big_cmd 'PASS big_cmd: .* dropped, server and host fine' $WT zsh -c 'make -C src/net all >/dev/null && src/net/build/test_dsnet --only big_cmd'
chk g016_full_big 'PASS full_big: .*reached both guests whole, 8 MB \+ 1 refused, nobody cut' $WT zsh -c 'make -C src/net all >/dev/null && src/net/build/test_dsnet --only full_big'
chk g016_hello_max 'PASS hello_max: .*host and guest welcomed' $WT zsh -c 'make -C src/net all >/dev/null && src/net/build/test_dsnet --only hello_max'
chk g016_net_asan 'ALL PASS: 0 failure' $WT zsh -c 'make -C src/net asan 2>&1 | tee /dev/stderr | grep -q "ERROR: AddressSanitizer" && exit 1; exit 0'
# Windows steps: when the gate holds rig/wine.lock itself (owner gate13-hotfix, take it before this script), each test
# runs through held_lock_run.py so no lane can take wine between the steps
WH=(); [[ $(head -1 $HOME/ds_multiplayer/rig/wine.lock/owner 2>/dev/null) == gate13-hotfix ]] && WH=($H/held_lock_run.py)
chk g016_uninst 'RESULT ([0-9]+)/\1 pass' $WT python3 $WH $LD/loader/start/win/installer/test/uninst_test.py $REL/Dungeon-Settlers-Modded-Setup.exe $LD/loader/build/win/kit
chk g016_setup 'RESULT ([0-9]+)/\1 pass' $WT python3 $WH $H/setup_test.py $REL/Dungeon-Settlers-Modded-Setup.exe $OUT/g016_setup
chk g016_dmg_start 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $REL/Dungeon-Settlers-Modded-Mac.dmg $OUT/g016_dmg_start
G=$HOME/ds_multiplayer/ext/merger13/tools/gate.sh
chk g016_smoke 'smoke 3/3' $WT zsh $G $D gate13 smoke
chk g016_mp2 ' 19 PASS, 0 FAIL, tb 0' $WT zsh $G $D gate13 mp2
# the full-state checks on the blob path (code review #3), end to end in the game (no real save gives > 60 KB; full_big covers that)
fullcheck() { local L=$OUT/g016_fastjoin_raw.log; (cd $WT/ext/latejoin && python3 fastjoin_pair.py --mode off --dylib $D --full-every 300) > $L 2>&1; local rc=$?; cat $L
  command grep -qE 'PASS full-state checks ran, none failed ok [1-9][0-9]* fails 0' $L && command grep -q 'PASS no desync, no full / deep difference' $L && echo "FULLCHECK PASS" && return $rc; return 1; }
chk g016_fullcheck 'FULLCHECK PASS' $WT fullcheck
pvpx() {   # PvP v1, Mac host + Windows guest W (C:\DSW, this tree's baselib.dll); every check incl. 6 (0 desyncs, 500+ ticks)
  local DSW="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW" R=$HOME/ds_multiplayer/rig/rig.py LOCK=$HOME/ds_multiplayer/rig/wine.lock t=$(date +%Y%m%d-%H%M%S)
  (cd $WT/win && nice -n 19 zsh build_win.sh > $OUT/g016_build_win.log 2>&1) || { echo "win build failed"; return 1; }
  git -C $WT checkout -q win/win_map.tsv
  mkdir -p "$DSW/old_gate13"; for f in baselib.dll det_ranges_win.tsv; do mv "$DSW/$f" "$DSW/old_gate13/$f.$t"; cp $WT/win/build/$f "$DSW/$f"; done
  local held=0; [[ $(head -1 $LOCK/owner 2>/dev/null) == gate13-hotfix ]] && held=1
  if (( ! held )); then
    [[ -z $(ps -axo etime=,comm= | command grep -iE 'wine|\.exe$' | command grep -ve '-[0-9][0-9]:') ]] && mkdir $LOCK || { echo "wine busy / locked"; return 1; }
    echo gate13 > $LOCK/owner
  fi
  local P=$(python3 $R lease gate13 --pid $$ | tail -1) rc=1 XL=$OUT/g016_pvp_raw.log
  if [[ $P == [A-J][A-J] ]]; then
    (cd $HOME/ds_multiplayer/win && python3 xplay_run.py $P $WT/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib $D --tag gate016_xv1) > $XL 2>&1
    command grep -E '(PASS|FAIL) [0-9]' $XL; echo "raw log (xplay_run's release() stub raises at the end): $XL"
    local f=$(command grep -cE '^[0-9:]+ FAIL [0-9]' $XL) p=$(command grep -cE '^[0-9:]+ PASS [0-9]' $XL)
    (( f == 0 && p >= 10 )) && { echo "XPV PASS all $p checks"; rc=0; } || echo "XPV FAIL $f failed"
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/win && python3 wrig.py stop) >/dev/null 2>&1; python3 $R release $P gate13 >/dev/null
  fi
  (( held )) || { mkdir -p $HOME/ds_multiplayer/rig/old; mv $LOCK/owner $HOME/ds_multiplayer/rig/old/wine.lock.owner.gate13.$(date +%s); rmdir $LOCK; }
  return $rc
}
chk g016_pvp_xplay 'XPV PASS' $WT pvpx
chk g016_mixed_set 'RESULT 4/4 pass' $WT python3 $H/mixed_set.py
print "g016_inputs_unchanged $([[ $( { echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  pin $(head -1 $LD/loader/mp_pin.txt)  cm $(head -1 $LD/loader/cm_pin.txt)"; for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "g016_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
