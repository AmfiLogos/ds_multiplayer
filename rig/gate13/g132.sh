#!/bin/zsh
# g132.sh - the v13.2 gate (owner: only tests whose failure means a real break). Artifacts in $REL (copied from the LD
# build: .dmg, Setup.exe, mod zips). Runs: byte scan of every artifact (mounted .dmg, Setup.exe, the kit it packs, mod
# zips; banned name, "/Users/", "._" files, save data by name), .dmg install + hidden first start, mod zip dropped into a clean install,
# Setup.exe silent install + start + uninstall keeps saves (wine), l1_world_test, l1_test, smoke, mp2, PvP Mac host +
# Windows guest (all checks: 0 desyncs, 500+ ticks), mixed mod set refused at join, owner prefs.
source ${0:A:h}/lib.sh; H=${0:A:h}
REL=${REL:-$WT/ext/gate13/release_v132}
[[ -z ${GATE_ONLY:-} && -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }   # a GATE_ONLY rerun appends to the same summary
{ echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  pin $(head -1 $LD/loader/mp_pin.txt)  cm $(head -1 $LD/loader/cm_pin.txt)"
  for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } > $OUT/inputs.txt
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v13.2 gate $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
trap "verdict ${0:A}" EXIT
scan() { local M=$(mktemp -d); hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/Dungeon-Settlers-Modded-Mac.dmg >/dev/null || return 1
  python3 $H/banscan.py $M $REL/Dungeon-Settlers-Modded-Mac.dmg $REL/Dungeon-Settlers-Modded-Setup.exe $LD/loader/build/win/kit $REL/*.zip; local rc=$?
  hdiutil detach $M -quiet; (( rc == 0 )) && echo "SCAN ALL PASS"; return $rc; }
chk g132_scan 'SCAN ALL PASS' $WT scan
chk g132_dmg_start 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $REL/Dungeon-Settlers-Modded-Mac.dmg $OUT/g132_dmg_start
chk g132_mod_zip 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $REL/Dungeon-Settlers-Modded-Mac.dmg $OUT/g132_mod_zip $REL/gcd-cdr-2.0.0.zip
chk g132_setup 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/setup_test.py $REL/Dungeon-Settlers-Modded-Setup.exe $OUT/g132_setup
chk g132_l1_world '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $H/ld_run.py l1_world_test.py $OUT/g132_l1_world
chk g132_l1_soft '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $H/ld_run.py l1_test.py $OUT/g132_l1_soft
G=$HOME/ds_multiplayer/ext/merger13/tools/gate.sh
chk g132_smoke 'smoke 3/3' $WT zsh $G $D gate13 smoke
chk g132_mp2 ' 19 PASS, 0 FAIL, tb 0' $WT zsh $G $D gate13 mp2
pvpx() {   # PvP v1, Mac host + Windows guest W (C:\DSW, this tree's baselib.dll); every check incl. 6 (0 desyncs, 500+ ticks)
  local DSW="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW" R=$HOME/ds_multiplayer/rig/rig.py t=$(date +%Y%m%d-%H%M%S)
  (cd $WT/win && nice -n 19 zsh build_win.sh > $OUT/g132_build_win.log 2>&1) || { echo "win build failed"; return 1; }
  git -C $WT checkout -q win/win_map.tsv     # build_win.sh regenerates it (test tree stays clean for later tests)
  mkdir -p "$DSW/old_gate13"; for f in baselib.dll det_ranges_win.tsv; do mv "$DSW/$f" "$DSW/old_gate13/$f.$t"; cp $WT/win/build/$f "$DSW/$f"; done
  python3 $HOME/ds_multiplayer/rig/wine_lease.py take gate13 --pid $$ || { echo "wine busy / locked"; return 1; }
  local P=$(python3 $R lease gate13 --pid $$ | tail -1) rc=1 XL=$OUT/g132_pvp_raw.log
  if [[ $P == [A-J][A-J] ]]; then
    (cd $HOME/ds_multiplayer/win && python3 xplay_run.py $P $WT/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib $D --tag gate132_xv1) > $XL 2>&1
    command grep -E '(PASS|FAIL) [0-9]' $XL; echo "raw log (xplay_run's release() stub raises at the end): $XL"
    local f=$(command grep -cE '^[0-9:]+ FAIL [0-9]' $XL) p=$(command grep -cE '^[0-9:]+ PASS [0-9]' $XL)
    (( f == 0 && p >= 10 )) && { echo "XPV PASS all $p checks"; rc=0; } || echo "XPV FAIL $f failed"
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/win && python3 wrig.py stop) >/dev/null 2>&1; python3 $R release $P gate13 >/dev/null
  fi
  python3 $HOME/ds_multiplayer/rig/wine_lease.py release gate13 >/dev/null; return $rc
}
chk g132_pvp_xplay 'XPV PASS' $WT pvpx
chk g132_mixed_set 'RESULT 4/4 pass' $WT python3 $H/mixed_set.py
print "g132_inputs_unchanged $([[ $( { echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  pin $(head -1 $LD/loader/mp_pin.txt)  cm $(head -1 $LD/loader/cm_pin.txt)"; for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "g132_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
