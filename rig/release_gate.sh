#!/bin/zsh
# release_gate.sh <tag>   (e.g. v10) - the FULL regression on one frozen build; release_all.sh refuses without its green summary.
# Needs the frozen source rig/release/<tag>_src (rsync -a --exclude old_versions ../src/ release/<tag>_src/) equal to src/.
#  1. gate.sh (builds build/dsrig.dylib from src = the frozen source; smoke, mp2, left_prompt), then freezes the dylib
#     as release/dsrig-<sha16>.dylib; builds the mouse test variant (-DTEAMS_MOUSE_TEST) and the Windows baselib.dll
#     from the frozen source; installs the DLL in the DSMP bottle (hidden tests only).
#  2. three lanes at once (one pair each, max 3 on this Mac; other lanes must stay off):
#     L1 uxgame full, teams_desync 10 rounds, coop_pair
#     L2 every Teams suite (mouse build where needed), desync_partial batch, ux_core_pair
#     L3 build_ux menu suites, then Windows guest W (Mac host): mp2, v8, wrap, ping/view, partial resync,
#        teams_desync 10 rounds (10 dungeon floor generations)
#  3. one summary: release/gate_<sha16>.summary, one line per suite (PASS/FAIL, counts, log), last line
#     "ALL GREEN <sha256>" only when every suite passed, the dylib and src are unchanged and the prefs are identical.
# A suite passes when its exit code is 0, it logged at least one PASS and no FAIL / Traceback / "SUMMARY n PASS, m>0 FAIL".
# Logs: det_out/release_gate_<sha16>_<time>/
set -u; cd "${0:A:h}"; R=$PWD; ROOT=${R:h}
export DSRIG_RELEASE=1      # only processes started by the gate may lease while rig/release.lock exists
TAG=${1:-}; [[ -n $TAG ]] || { echo "usage: $0 <tag>  (frozen source in release/<tag>_src)"; exit 1; }
SRC=$R/release/${TAG}_src
[[ -d $SRC ]] || { echo "no $SRC: freeze first (rsync -a --exclude old_versions ../src/ $SRC/)"; exit 1; }
same_src() { diff -rq --exclude old_versions --exclude __pycache__ --exclude '*.dSYM' --exclude teams_test --exclude test_ui $ROOT/src $SRC >/dev/null; }
same_src || { echo "src/ differs from $SRC: refreeze or wait for the freeze"; exit 1; }
leased() { python3 rig.py leases | grep -vc ' free$'; }
(( $(leased) == 0 )) || { echo "pairs are leased by other lanes:"; python3 rig.py leases; exit 1; }
# a test still running without its lease drives the gate's copies (2026-10-14: a soak failed mp2's ownership check)
pgrep -fl '[a-z0-9_]+_(test|pair)\.py' && { echo "rig tests still running (above): stop them first"; exit 1; }

T=$(date +%Y%m%d-%H%M)
# Foreign Expedition results left in a copy's data/fe open a results window over the game at load and take Escape
# (v11 candidate round 1: uxgame + teams_v10 Escape checks). The gate starts every copy without them (moved, not deleted).
for d in $ROOT/instances/[A-J]/data/fe(N/) $ROOT/win/instances/W/data/fe(N/); do mv $d ${d}_gatestash_$T; done
echo "gate.sh $(date +%T)"
zsh gate.sh > det_out/release_gate_gate_$T.out 2>&1; GRC=$?
SHA=$(shasum -a 256 $ROOT/build/dsrig.dylib | cut -d' ' -f1); S16=${SHA:0:16}
L=$R/det_out/release_gate_${S16}_$T; mkdir -p $L; mv det_out/release_gate_gate_$T.out $L/gate.out
SUM=$R/release/gate_$S16.summary
mkdir -p old_versions; [[ -e $SUM ]] && mv $SUM old_versions/gate_$S16-$(date +%Y-%m-%d-%H%M).summary
D=$R/release/dsrig-$S16.dylib
[[ -e $D ]] || cp -p $ROOT/build/dsrig.dylib $D
[[ $(shasum -a 256 $D | cut -d' ' -f1) == $SHA ]] || { echo "frozen copy $D is not $SHA" | tee $SUM; exit 1; }
# det_ranges.tsv (caller ranges, read by every Mac copy from build/) is frozen with the dylib: other lanes' builds
# rewrite build/det_ranges.tsv (2026-10-15: a kit dry run read a newer file and differed from tick 26)
RG=$R/release/det_ranges-$S16.tsv; [[ -e $RG ]] || cp -p $ROOT/build/det_ranges.tsv $RG
export DSRIG_RANGES=$RG
print -l "release gate $TAG  dylib sha256 $SHA  source $SRC  started $T" "logs $L" > $SUM
print "gate.sh $([[ $GRC == 0 ]] && echo PASS || echo FAIL)  $(tail -1 $L/gate.out | cut -c1-200)  log $L/gate.out" >> $SUM
echo "dylib $SHA"

# mouse test variant, from the frozen source (same compile line as src/build.sh)
M=$L/dsrig_mouse.dylib
sed -e "s|cd \"\${0:A:h}\"; mkdir -p ../build|cd $SRC|" -e "s|\.\./win/|$ROOT/win/|g" -e "s|\.\./build/dsrig.dylib.tmp|$M.tmp|g" \
    -e "s|\.\./build/dsrig.dylib|$M|g" -e "s|-DDSMATH_DEFAULT_ON=1 |-DDSMATH_DEFAULT_ON=1 -DTEAMS_MOUSE_TEST |" $ROOT/src/build.sh > $L/build_mouse.sh
zsh $L/build_mouse.sh > $L/build_mouse.out 2>&1 && grep -q TEAMS_MOUSE_TEST $L/build_mouse.sh
print "mouse_build $([[ $? == 0 && -f $M ]] && echo PASS || echo FAIL)  log $L/build_mouse.out" >> $SUM

# Windows baselib.dll from the frozen source -> DSMP (the previous DLL is kept in win/old_versions)
W=$ROOT/win; BOT="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW"
(cd $W && cp -p build/baselib.dll old_versions/baselib-$(date +%Y-%m-%d-%H%M).dll && DSMP_SRC=$SRC nice -n 5 zsh build_win.sh) > $L/build_win.out 2>&1
WRC=$?; grep -q "^ERROR" $L/build_win.out && WRC=1
if [[ $WRC == 0 ]]; then
  cp -p "$BOT/baselib.dll" $W/old_versions/baselib-DSMP-$(date +%Y-%m-%d-%H%M).dll
  cp $W/build/baselib.dll "$BOT/baselib.dll"; cp $W/build/det_ranges_win.tsv "$BOT/det_ranges_win.tsv"; cp -p $W/build/baselib.dll $L/baselib.dll
fi
WSHA=$(shasum -a 256 $W/build/baselib.dll | cut -d' ' -f1)
print "windows_build $([[ $WRC == 0 ]] && echo PASS || echo FAIL)  baselib.dll sha256 $WSHA  log $L/build_win.out" >> $SUM
# fail fast (lead 2026-10-15): a failed build makes every later suite of that build meaningless
if grep -qE '^(mouse_build|windows_build) FAIL' $SUM; then print "NOT GREEN: build failed (fail fast, no suites run)" >> $SUM; cat $SUM; exit 1; fi

# one suite: run <name> <dir> <command...>; verdict line into $L/<name>.res
verdict() {   # log rc -> PASS|FAIL + counts
  local f=$1 rc=$2
  local p=$(grep -cE '(^|[0-9]{2}:[0-9]{2}:[0-9]{2} )(RESULT )?PASS\b|^.*SUMMARY [0-9]+ PASS, 0 FAIL' $f)
  local x=$(grep -cE '^([0-9]{2}:[0-9]{2}:[0-9]{2} )?(RESULT )?FAIL\b|SUMMARY [0-9]+ PASS, [1-9][0-9]* FAIL|Traceback' $f)
  [[ $rc == 0 && $p -gt 0 && $x == 0 ]] && echo "PASS  pass-lines $p" || echo "FAIL  exit $rc, pass-lines $p, fail-lines $x"
}
run() {
  local n=$1 d=$2; shift 2
  local o=$L/$n.out; echo "  $n $(date +%T)"
  (cd $d && nice -n 5 "$@") > $o 2>&1
  print "$n $(verdict $o $?)  log $o" > $L/$n.res
}
wait_pair() { while (( $(leased) >= ${GATE_PAIRS:-1} )); do sleep 15; done; }   # 2026-10-14: one pair at a time by default; 3 at once crashed the Mac
lease_own() { local p; until p=$(python3 rig.py lease $1 --pid $$ 2>/dev/null | tail -1); [[ $p == [A-J][A-J] ]]; do sleep 15; done; echo $p; }
U=$SRC/ui_test

lane1() {
  wait_pair; run uxgame $U python3 uxgame_pair.py --dylib $D
  wait_pair; run teams_desync $U python3 teams_desync_pair.py --rounds 10 --dylib $D
  wait_pair; run coop $U python3 coop_pair.py --dylib $D
}
lane2() {
  for s in teams_pair teams_click_pair teams_expedition_pair teams_hud_pair teams_layer_pair teams_lend_pair teams_proposal_pair \
           teams_stockgo_pair teams_viewer_pair teams_v10_pair teams_chatlink_pair teams_invite_pair; do
    [[ -f $U/$s.py ]] || { print "$s FAIL  missing in $U" > $L/$s.res; continue; }
    case $s in teams_click_pair|teams_v10_pair|teams_chatlink_pair|teams_invite_pair) dl=$M ;; *) dl=$D ;; esac
    wait_pair; run $s $U python3 $s.py --dylib $dl
  done
  wait_pair; run desync_partial $R python3 pin_run.py $D desync_partial_test.py
  wait_pair; run ux_core $R python3 pin_run.py $D ux_core_pair.py
}
lane3() {
  local P=$(lease_own release_gate_ux)
  for s in v10_menu v9_menu tips chat chatkeys typing hist esc_center; do
    run ux_$s $ROOT/build_ux env PAIR=$P DSRIG_DYLIB=$D python3 $s.py; python3 rig.py stop $P >/dev/null 2>&1
  done
  python3 rig.py release $P release_gate_ux >/dev/null
  [[ $WRC == 0 ]] || { print "windows_suites FAIL  no Windows build" > $L/win.res; return; }
  P=$(lease_own release_gate_win)
  run win_mp2 $W python3 xplay_run.py $P xplay_wrap.py --math --pin-dylib $D $R/mp2_test.py
  run win_v8 $W python3 xplay_run.py $P v8_xplay.py --math --dylib $D
  run win_wrap $W python3 xplay_run.py $P wrap_xplay.py --math --dylib $D
  run win_pingview $W python3 xplay_run.py $P v10_xplay.py --math --dylib $D
  run win_partial $W python3 xplay_run.py $P xplay_wrap.py --math --pin-dylib $D $R/desync_partial_test.py --runs 2 --fallback 1
  run win_floorgen10 $W python3 xplay_run.py $P xplay_wrap.py --math $U/teams_desync_pair.py --rounds 10 --dylib $D
  # base-work save cross-play (review 2026-10-14 finding 3): 15 min of normal base play, full-state check every 600 steps
  (cd $W && python3 wrig.py stop >/dev/null 2>&1)     # the soak starts its own guest at the title
  run win_base_soak $W python3 xplay_run.py $P xplay_wrap.py --math --pin-dylib $D $R/soak_test.py --minutes 15 --full-every 600
  python3 rig.py stop $P >/dev/null 2>&1; (cd $W && python3 wrig.py stop >/dev/null 2>&1)
  # kit dry run = the friend's Windows kit, recorded on the Mac and replayed alone in the DSMP bottle: IDENTICAL every tick
  run win_kit $W python3 kit/kit_dryrun.py ${P[1]} --dylib $D --ranges $RG
  python3 rig.py stop $P >/dev/null 2>&1; (cd $W && python3 wrig.py stop >/dev/null 2>&1); python3 rig.py release $P release_gate_win >/dev/null
}
echo "lanes $(date +%T)"
if (( ${GATE_PAIRS:-1} <= 1 )); then   # serial: three lanes started together raced past wait_pair (each saw 0 leased)
  lane1 > $L/lane1.log 2>&1; lane2 > $L/lane2.log 2>&1; lane3 > $L/lane3.log 2>&1
else
  lane1 > $L/lane1.log 2>&1 &
  lane2 > $L/lane2.log 2>&1 &
  lane3 > $L/lane3.log 2>&1 &
  wait
fi
cat $L/*.res(N) >> $SUM

print "dylib_unchanged $([[ $(shasum -a 256 $D | cut -d' ' -f1) == $SHA ]] && echo PASS || echo FAIL)" >> $SUM
print "src_unchanged $(same_src && echo PASS || echo FAIL)" >> $SUM
print "prefs $([[ $(python3 rig.py check-prefs) == identical ]] && echo PASS || echo FAIL)" >> $SUM
print "build_dylib_unchanged $([[ $(shasum -a 256 $ROOT/build/dsrig.dylib | cut -d' ' -f1) == $SHA ]] && echo PASS || echo FAIL)" >> $SUM
EXP=40    # gate, mouse, windows build, 3 (L1), 14 (L2), 8 + 8 (L3), 4 checks above
NF=$(awk '$2 == "FAIL"' $SUM | wc -l | tr -d ' '); NP=$(awk '$2 == "PASS"' $SUM | wc -l | tr -d ' ')
if (( NF == 0 && NP == EXP )); then print "ALL GREEN $SHA" >> $SUM; else print "NOT GREEN: $NF failed, $NP of $EXP passed" >> $SUM; fi
cat $SUM
