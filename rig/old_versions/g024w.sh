#!/bin/zsh
# g024w.sh - the v0.1.14 Wine checks (bottle DSMP), split out of g024.sh (owner 2026-10-18): optional, run last, after the
# Mac gate and the Windows VM signal are green, only with load < 40 and swap < 20 GB; never blocks the release.
# Env as g024.sh; GATE_DIR default $WT/ext/gate024/out_wine.
export GATE_DIR=${GATE_DIR:-${WT:-$HOME/ds_mp_gate13}/ext/gate024/out_wine}
H=${0:A:h:h}/gate13; source $H/lib.sh
REL=${REL:-$WT/ext/gate024/release_v024}; CMR=${CMR:-$HOME/ds_custommodes}; PKGZ=${PKGZ:-$HOME/ds_mp_gate13/ext/gate13/pkgzips}
export GATE_WINE_OWNER=ship018-gate; WL=$HOME/ds_multiplayer/rig/wine_lease.py
[[ -z ${GATE_ONLY:-} && -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }   # a GATE_ONLY rerun appends to the same summary
if [[ -z ${GATE_ONLY:-} ]]; then
{ echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"
  for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } > $OUT/inputs.txt
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v0.1.14 Wine checks $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
else
  IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64); print "rerun $(date '+%F %T') GATE_ONLY=$GATE_ONLY" >> $SUM
fi
trap "verdict ${0:A}" EXIT
WH=($H/held_lock_run.py); WE=(WINE_LEASE_LOCK=$HOME/ds_multiplayer/rig/wine.lock.gate13-inner)
LR=$H/ld_run.py
# ---- Windows (wine bottle DSMP; the gate holds the wine lock for this block only, then gives it back)
print "g024_wine_lock $(python3 $WL take ship018-gate --pid $$ --wait 3600 >/dev/null && echo PASS || echo FAIL)" | tee -a $SUM
# a Windows test that takes rig/wine.lock with its own mkdir (update_test.sh) gets the lock from the gate for its run
handover() { local hp=$(sed -n 2p $HOME/ds_multiplayer/rig/wine.lock/owner) rc
  python3 $HOME/ds_multiplayer/rig/wine_lease.py release ship018-gate >/dev/null || return 1
  "$@"; rc=$?
  python3 $HOME/ds_multiplayer/rig/wine_lease.py take ship018-gate --pid $hp --wait 600 >/dev/null || { echo "could not retake wine.lock"; return 1; }
  return $rc; }
# the default install folder is moved aside (never removed) before a Windows test that installs there: the new installer
# refuses a folder that already holds files (a previous test's kept saves)
fresh_root() { local R="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/users/crossover/AppData/Local/Dungeon Settlers Modded"
  [[ -e $R ]] && mv "$R" "$R.gate018-$(date +%Y%m%d-%H%M%S)"; true; }
updwin() { fresh_root; handover env KIT=$LD/loader/build/win/kit zsh loader/start/win/test/update_test.sh; }
chk g024_update_win '== 0 failed' $LD updwin
chk g024_uninst 'RESULT ([0-9]+)/\1 pass' $WT python3 $WH $LD/loader/start/win/installer/test/uninst_test.py $(print $REL/*Setup*.exe) $LD/loader/build/win/kit
chk g024_setup 'RESULT ([0-9]+)/\1 pass' $WT python3 $WH $H/setup_test.py $(print $REL/*Setup*.exe) $OUT/g024_setup
winst() { fresh_root; env $WE zsh loader/start/win/test/winstart_test.sh; }
chk g024_winstart '== 0 failed' $LD winst
chk g024_nonascii 'RESULTS 8 / 8' $LD python3 $WH loader/start/win/test/nonascii_test.py loader/build/win/kit $LD/loader/build/mods_all/win/gcd-cdr
chk g024_w_pkg '([0-9]+)/\1 checks' $WT python3 $WH $H/w_pkg_gate.py $REL $OUT/g024_w_pkg
chk g024_pathwarn '== ([0-9]+)/\1 passed' $LD python3 $WH loader/start/win/test/pathwarn_test.py $(print $REL/*Setup*.exe) $LD/loader/build/win/kit
chk g024_win_cjk '([0-9]+)/\1 checks' $WT python3 $WH $H/w_cjk_gate.py $LD/loader/build/win/kit $OUT/g024_win_cjk ja
chk g024_win_repair '([0-9]+)/\1 checks' $WT python3 $WH $H/w_repair_gate.py $LD/loader/build/win/kit $REL/custom-modes-0.1.6.zip $OUT/g024_win_repair
pvpx() {   # PvP v1, Mac host + Windows guest W (C:\DSW, this tree's baselib.dll); every check incl. 6 (0 desyncs, 500+ ticks)
  local DSW="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW" R=$HOME/ds_multiplayer/rig/rig.py LOCK=$HOME/ds_multiplayer/rig/wine.lock t=$(date +%Y%m%d-%H%M%S)
  (cd $WT/win && nice -n 19 zsh build_win.sh > $OUT/g024_build_win.log 2>&1) || { echo "win build failed"; return 1; }
  git -C $WT checkout -q win/win_map.tsv
  mkdir -p "$DSW/old_gate13"; for f in baselib.dll det_ranges_win.tsv; do mv "$DSW/$f" "$DSW/old_gate13/$f.$t"; cp $WT/win/build/$f "$DSW/$f"; done
  [[ $(head -1 $LOCK/owner 2>/dev/null) == ship018-gate ]] || { echo "the gate must hold wine.lock (ship018-gate)"; return 1; }
  local P=$(python3 $R lease ship018-$$ --pid $$ | tail -1) rc=1 XL=$OUT/g024_pvp_raw.log
  if [[ $P == [A-J][A-J] ]]; then
    (cd $HOME/ds_multiplayer/win && python3 xplay_run.py $P $WT/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib $D --tag gate024_xv1) > $XL 2>&1
    command grep -E '(PASS|FAIL) [0-9]' $XL; echo "raw log (xplay_run's release() stub raises at the end): $XL"
    local f=$(command grep -cE '^[0-9:]+ FAIL [0-9]' $XL) p=$(command grep -cE '^[0-9:]+ PASS [0-9]' $XL)
    (( f == 0 && p >= 10 )) && { echo "XPV PASS all $p checks"; rc=0; } || echo "XPV FAIL $f failed"
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/win && python3 wrig.py stop) >/dev/null 2>&1; python3 $R release $P ship018-$$ >/dev/null
  fi
  return $rc
}
chk g024_pvp_xplay 'XPV PASS' $WT pvpx
python3 $WL release ship018-gate >/dev/null
print "g024w_inputs_unchanged $([[ $( { echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"; for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
