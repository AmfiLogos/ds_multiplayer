# lib.sh - sourced by the v13 gate steps (rig/gate13/g*.sh). One verdict line per check into $SUM.
#   chk <name> <pass-regex> <cwd> <command...>   runs the command (nice 19), log $OUT/<name>.log;
#   PASS = exit 0 AND the log matches <pass-regex> AND no Traceback AND no prefs leak line ("PREFS CHANGED", "prefs
#   DIFFERENT", "prefs identical after: False": rig.py check_prefs() restores the owner's prefs and only prints that, so a
#   later prefs check would see them restored). A FAIL is run once more (gate rule: red twice = red).
#   GATE_ONLY="a b": run only those checks (a rerun); the others keep their last line in $SUM.
# verdict <gate script...>: the expected checks are read from the scripts themselves (every `chk <name>` line and every
#   `print "<name> $(` line). A gate is ALL GREEN only when every expected check has a line in $SUM and its LAST line is
#   PASS, and no "group_* FAIL" line exists (a g*.sh that exited non-zero). Missing = FAIL.
# Env: GATE_DIR (default ext/gate13/out of this worktree). Exports DSRIG_RELEASE=1 (gate owns rig/release.lock).
export DSRIG_RELEASE=1
WT=${WT:-$HOME/ds_mp_gate13}; LD=${LD:-$HOME/ds_launcher}
# Custom Modes: its own repo since post-v13 (checkout CM_REPO, default ~/ds_custommodes); its test tools build into $MP_ROOT
export CM=${CM_REPO:-$HOME/ds_custommodes} MP_ROOT=$WT
D=$WT/build/dsrig.dylib; M=$WT/build/gate13_mouse/dsrig.dylib
OUT=${GATE_DIR:-$WT/ext/gate13/out}; SUM=$OUT/summary.txt; mkdir -p $OUT
_once() {   # <name> <regex> <cwd> <cmd...> -> 0 pass / 1 fail
  local n=$1 re=$2 d=$3; shift 3
  # a test output folder named like the check starts empty each try (a My Units library left there by an earlier
  # run made g5_myunits start with 15 units)
  [[ -d $OUT/$n ]] && { mkdir -p $OUT/old; mv $OUT/$n $OUT/old/$n-dir-$(date +%H%M%S); }
  print -r -- "--- $(date '+%F %T') $*" >> $OUT/$n.log
  if [[ $(whence -w $1) == *function ]]; then (cd $d && "$@"); else (cd $d && nice -n 19 "$@"); fi >> $OUT/$n.log 2>&1; local rc=$?
  # suites run through merger13's gate.sh keep their own log (named in our log as det_out/...log): read those too
  local sub=(${(f)"$(command grep -v '^--- ' $OUT/$n.log | command grep -oE 'det_out/[^ )]+\.log' | sed "s|^|$HOME/ds_multiplayer/rig/|")"})
  [[ $rc == 0 ]] && grep -qE -- "$re" $OUT/$n.log && ! grep -q Traceback $OUT/$n.log && \
    ! { command grep -v '^--- ' $OUT/$n.log; cat ${sub:#} /dev/null 2>/dev/null; } | command grep -qiE 'PREFS CHANGED|prefs DIFFERENT|prefs identical after: False'
}
chk() {
  local n=$1 re=$2; local try=1
  [[ -n ${GATE_ONLY:-} && " $GATE_ONLY " != *" $n "* ]] && return 0   # GATE_ONLY="a b": rerun only those checks
  [[ -f $OUT/$n.log ]] && { mkdir -p $OUT/old; mv $OUT/$n.log $OUT/old/$n-$(date +%H%M%S).log; }
  if ! _once "$@"; then sleep 2; mkdir -p $OUT/old; mv $OUT/$n.log $OUT/old/$n-try1-$(date +%H%M%S).log; try=2; _once "$@"; fi
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

# expected check names of gate scripts (static: every check must be written out, no names built in loops)
expected() { { command grep -hoE '^[[:space:]]*(chk|print "?)[[:space:]]*[a-z][a-z0-9_]+' "$@" | sed -E 's/^[[:space:]]*(chk|print "?)[[:space:]]*//'
               command grep -h '^# expect:' "$@" | sed 's/^# expect://' | tr ' ' '\n'; } | command grep '_' | command grep -v '^group_' | awk '!s[$0]++'; }
# a gate script arms its verdict at top level (a trap set inside a function fires when that function returns):
#   trap "verdict ${0:A}" EXIT      -> the verdict also runs when the script exits early (missing checks = FAIL)
verdict() {   # <gate script...> -> last line "ALL GREEN ..." / "NOT GREEN ..." appended to $SUM; exit 0 only when green
  local e=(${(f)"$(expected "$@")"}) bad=() np=0 n st
  for n in $e; do
    st=$(awk -v n=$n '$1 == n {s = $2} END {print s}' $SUM)
    if [[ $st == PASS ]]; then (( np++ )); else bad+=("${n}:${st:-missing}"); fi
  done
  for n in $(awk '$1 ~ /^group_/ && $2 == "FAIL" {print $1}' $SUM); do bad+=("${n}:FAIL"); done
  local IN=$(shasum -a 256 $OUT/inputs.txt 2>/dev/null | cut -c1-64)
  if (( ${#bad} == 0 && np == ${#e} && np > 0 )); then print "ALL GREEN ${IN:-no-inputs} ($np of ${#e} expected checks)" | tee -a $SUM
  else print "NOT GREEN: $np of ${#e} expected checks PASS; not green: ${bad[*]}" | tee -a $SUM; return 1; fi
}
