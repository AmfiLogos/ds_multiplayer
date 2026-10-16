# lib.sh - sourced by the v13 gate steps (rig/gate13/g*.sh). One verdict line per check into $SUM.
#   chk <name> <pass-regex> <cwd> <command...>   runs the command (nice 19), log $OUT/<name>.log;
#   PASS = exit 0 AND the log matches <pass-regex> AND no Traceback. A FAIL is run once more (gate rule: red twice = red).
# Env: GATE_DIR (default ext/gate13/out of this worktree). Exports DSRIG_RELEASE=1 (gate owns rig/release.lock).
export DSRIG_RELEASE=1
WT=${WT:-$HOME/ds_mp_gate13}; LD=${LD:-$HOME/ds_launcher}
D=$WT/build/dsrig.dylib; M=$WT/build/gate13_mouse/dsrig.dylib
OUT=${GATE_DIR:-$WT/ext/gate13/out}; SUM=$OUT/summary.txt; mkdir -p $OUT
_once() {   # <name> <regex> <cwd> <cmd...> -> 0 pass / 1 fail
  local n=$1 re=$2 d=$3; shift 3
  # a test output folder named like the check starts empty each try (a My Units library left there by an earlier
  # run made g5_myunits start with 15 units)
  [[ -d $OUT/$n ]] && { mkdir -p $OUT/old; mv $OUT/$n $OUT/old/$n-dir-$(date +%H%M%S); }
  print -r -- "--- $(date '+%F %T') $*" >> $OUT/$n.log
  if [[ $(whence -w $1) == *function ]]; then (cd $d && "$@"); else (cd $d && nice -n 19 "$@"); fi >> $OUT/$n.log 2>&1; local rc=$?
  [[ $rc == 0 ]] && grep -qE -- "$re" $OUT/$n.log && ! grep -q Traceback $OUT/$n.log
}
chk() {
  local n=$1 re=$2; local try=1
  [[ -n ${GATE_ONLY:-} && " $GATE_ONLY " != *" $n "* ]] && return 0   # GATE_ONLY="a b": rerun only those checks
  [[ -f $OUT/$n.log ]] && { mkdir -p $OUT/old; mv $OUT/$n.log $OUT/old/$n-$(date +%H%M%S).log; }
  if ! _once "$@"; then mkdir -p $OUT/old; mv $OUT/$n.log $OUT/old/$n-try1-$(date +%H%M%S).log; try=2; _once "$@"; fi
  local v=$?; local last=$(grep -E -- "$re" $OUT/$n.log | tail -1 | cut -c1-140)
  print "$n $([[ $v == 0 ]] && echo PASS || echo FAIL) try$try  ${last:-$(tail -1 $OUT/$n.log | cut -c1-140)}  log $OUT/$n.log" | tee -a $SUM
  return $v
}
prefs() { (cd $HOME/ds_multiplayer/rig && python3 rig.py check-prefs); }
# sha256 of every shipped / tested binary (frozen inputs of the gate)
inputs() {
  { echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  pin $(head -1 $LD/loader/mp_pin.txt)"
    for f in $WT/build/{dsrig,custom-modes,custom-modes-mp}.dylib $M $HOME/ds_multiplayer/build/det_ranges.tsv $LD/loader/build/dsload.dylib \
             $LD/loader/build/mods_all/{mac,win}/*/*.(dylib|dll|json)(N) "$LD/app/build/Dungeon Settlers Modded.zip" \
             "$LD/loader/build/win/Dungeon Settlers Modded - Windows.zip" $LD/loader/build/win/kit/files/*.(dll|exe)(N); do
      print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; }
}
