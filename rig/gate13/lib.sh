# lib.sh - sourced by the v13 gate steps (rig/gate13/g*.sh). One verdict line per check into $SUM.
#   chk <name> <pass-regex> <cwd> <command...>   runs the command (nice 19), log $OUT/<name>.log;
#   PASS = exit 0 AND the log matches <pass-regex> AND no Traceback AND no prefs leak line ("PREFS CHANGED", "prefs
#   DIFFERENT", "prefs identical after: False": rig.py check_prefs() restores the owner's prefs and only prints that, so a
#   later prefs check would see them restored) AND no "EXTRA PLAYER" line (rigports.py players guard).
#   Second try (v0.1.22): a FAIL is run once more ONLY when try1's log shows an environment error (machine load, no free
#   pair, nothing ran, owner check, prefs leak: GATE_ENV_RE) and try1 did not print a result with failed checks
#   (GATE_RESFAIL_RE) or a hard error (GATE_HARD_RE). WHY (v0.1.19-21): 45 of 58 second tries failed again (1.7 h of
#   check time in v0.1.21); the 13 saves were all load / harness / owner-check cases. A retry also runs on copies that try1
#   changed, and can hide try1's real fault. GATE_RETRY_ALL=1 = the old rule (every FAIL gets a second try).
#   GATE_ONLY="a b": run only those checks (a rerun); the others keep their last line in $SUM.
#   First list (v0.1.22): the checks named in GATE_FIRST="a b" or in first.list next to the gate script (names, # comments)
#   run BEFORE all other checks (a child run of the same script with GATE_ONLY = that list, at the first chk), then the
#   script runs the rest. Put there the newest release's tests and the last release's red / dropped checks. WHY: in
#   v0.1.21 the newest tests ran last, 5-7 h after their merges, and their faults cost a rerun round.
# verdict <gate script...>: the expected checks are read from the scripts themselves (every `chk <name>` line and every
#   `print "<name> $(` line). A gate is ALL GREEN only when every expected check has a line in $SUM and its LAST line is
#   PASS, and no "group_* FAIL" line exists (a g*.sh that exited non-zero). Missing = FAIL.
# Env: GATE_DIR (default ext/gate13/out of this worktree). Exports DSRIG_RELEASE=1 (gate owns rig/release.lock).
export DSRIG_RELEASE=1
GATE_LIB_DIR=${${(%):-%x}:A:h}                                # this folder (gate_clean.sh)
WT=${WT:-$HOME/ds_mp_gate13}; LD=${LD:-$HOME/ds_launcher}
# Custom Modes: its own repo since post-v13 (checkout CM_REPO, default ~/ds_custommodes); its test tools build into $MP_ROOT
export CM=${CM_REPO:-$HOME/ds_custommodes} MP_ROOT=$WT
D=$WT/build/dsrig.dylib; M=$WT/build/gate13_mouse/dsrig.dylib
OUT=${GATE_DIR:-$WT/ext/gate13/out}; SUM=$OUT/summary.txt; mkdir -p $OUT
# free disk first (2026-10-17: the gates' leftovers filled the disk mid-gate): below GATE_MIN_FREE_GB (15) the gate stops
{ local_free=$(df -g "$HOME" | awk 'NR==2 {print $4}')
  if (( local_free < ${GATE_MIN_FREE_GB:-15} )); then
    print -u2 "GATE STOPPED: only $local_free GB free on the disk (need ${GATE_MIN_FREE_GB:-15} GB). Free space, or run rig/gate13/gate_clean.sh <gate out dir> for old gate leftovers."
    print "GATE STOPPED $(date '+%F %T'): only $local_free GB free (need ${GATE_MIN_FREE_GB:-15} GB)" >> $SUM; exit 3
  fi; unset local_free; }
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
    ! { command grep -v '^--- ' $OUT/$n.log; cat ${sub:#} /dev/null 2>/dev/null; } | command grep -qiE 'PREFS CHANGED|prefs DIFFERENT|prefs identical after: False' && \
    ! command grep -q 'EXTRA PLAYER' $OUT/$n.log    # rig/rigports.py: another run's player was in this run's session (2026-10-25)
}
# second-try rule: environment signs (retry) / a finished run with failed checks / hard errors (never retry)
GATE_ENV_RE=${GATE_ENV_RE:-'took too long|timed? ?out|timeout|process died|no room|no free pair|[^0-9]0 PASS, 0 FAIL|ok 0 fails 0|CalledProcessError|owner is playing|owner.s game runs|PREFS CHANGED|prefs DIFFERENT|prefs identical after: False'}
GATE_RESFAIL_RE=${GATE_RESFAIL_RE:-'RESULT:? FAIL|[0-9]+ PASS, [1-9][0-9]* FAIL|passed, [1-9][0-9]* failed|\([1-9][0-9]* failed\)|[0-9]+ passed; [1-9][0-9]* failed|test result: FAILED'}
GATE_HARD_RE=${GATE_HARD_RE:-'no cmd|unknown cmd|linker command failed|^skip:|does not trust'}
retry_why() {   # <try1 log> -> prints the reason and returns 0 when a second try is allowed
  local L=$1 body; body=$(command grep -v '^--- ' $L)
  [[ ${GATE_RETRY_ALL:-0} == 1 ]] && { print "GATE_RETRY_ALL=1"; return 0; }
  local h=$(print -r -- $body | command grep -m1 -E -- "$GATE_HARD_RE")
  [[ -n $h ]] && { print -r -- "no: hard error: ${h:0:100}"; return 1; }
  local r=$(print -r -- $body | command grep -m1 -E -- "$GATE_RESFAIL_RE")
  [[ -n $r ]] && { print -r -- "no: the run ended with failed checks: ${r:0:100}"; return 1; }
  local e=$(print -r -- $body | command grep -m1 -iE -- "$GATE_ENV_RE")
  [[ -n $e ]] && { print -r -- "env: ${e:0:100}"; return 0; }
  print "no: no environment error in the log"; return 1
}
# first list: GATE_FIRST, else first.list next to the gate script ($ZSH_ARGZERO)
GATE_SCRIPT=${GATE_SCRIPT:-${ZSH_ARGZERO:A}}; GATE_ARGV=("$@")
if [[ -z ${GATE_FIRST:-} && -f ${GATE_SCRIPT:h}/first.list ]]; then
  GATE_FIRST=${(j: :)${(f)"$(sed -e 's/#.*//' ${GATE_SCRIPT:h}/first.list | tr -s ' \t' '\n' | command grep .)"}}
fi
_first_done=0
_run_first() {   # once, at the first chk of a full run: the first-list checks in a child run of the same script
  (( _first_done )) && return 0; _first_done=1
  [[ -z ${GATE_FIRST:-} || -n ${GATE_ONLY:-} || -n ${GATE_PHASE:-} ]] && return 0
  print "first list $(date '+%F %T'): $GATE_FIRST" | tee -a $SUM
  GATE_PHASE=first GATE_ONLY="$GATE_FIRST" GATE_DIR=$OUT zsh $GATE_SCRIPT "${GATE_ARGV[@]}"
  print "first list done $(date '+%F %T'); the other checks now" | tee -a $SUM
}
chk() {
  local n=$1 re=$2; local try=1 why=''
  _run_first
  [[ -n ${GATE_ONLY:-} && " $GATE_ONLY " != *" $n "* ]] && return 0   # GATE_ONLY="a b": rerun only those checks
  [[ -z ${GATE_PHASE:-} && -n ${GATE_FIRST:-} && -z ${GATE_ONLY:-} && " $GATE_FIRST " == *" $n "* ]] && return 0   # ran in the first list
  [[ -f $OUT/$n.log ]] && { mkdir -p $OUT/old; mv $OUT/$n.log $OUT/old/$n-$(date +%H%M%S).log; }
  _once "$@"; local v=$?
  if (( v )); then
    why=$(retry_why $OUT/$n.log)
    if [[ $? == 0 ]]; then
      sleep 2; mkdir -p $OUT/old; local t1=$OUT/old/$n-try1-$(date +%H%M%S).log; mv $OUT/$n.log $t1; try=2
      (( $+functions[owner_wait] )) && owner_wait       # a prefs leak / owner sign: wait while the owner plays
      print -r -- "--- second try ($why); try1 log $t1" >> $OUT/$n.log
      _once "$@"; v=$?
    fi
  fi
  local last=$(grep -E -- "$re" $OUT/$n.log | tail -1 | cut -c1-140)
  print -r -- "$n $([[ $v == 0 ]] && echo PASS || echo FAIL) try$try  ${last:-$(tail -1 $OUT/$n.log | cut -c1-140)}  log $OUT/$n.log${why:+  [retry $why]}" | tee -a $SUM
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
  [[ ${GATE_PHASE:-} == first ]] && return 0          # the first-list child: the parent run gives the verdict
  zsh $GATE_LIB_DIR/gate_clean.sh $OUT 2>/dev/null   # every run, passed or failed: the copies it left (bottle, check folders)
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
