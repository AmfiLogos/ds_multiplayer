#!/bin/zsh
# win_lane_only.sh <sha16> <frozen src tag>: the release gate's Windows suites (lane 3, second part) on an already built
# win/build/baselib.dll, for a gate whose Windows build step failed and was fixed in win/ only. Writes <dir>/*.res and
# appends to release/gate_<sha16>.summary as a separate block.
set -u; cd "${0:A:h}"; R=$PWD; ROOT=${R:h}; export DSRIG_RELEASE=1
S16=$1; TAG=$2; SRC=$R/release/${TAG}_src; U=$SRC/ui_test; W=$ROOT/win
D=$R/release/dsrig-$S16.dylib; RG=$R/release/det_ranges-$S16.tsv; export DSRIG_RANGES=$RG
T=$(date +%Y%m%d-%H%M); L=$R/det_out/win_lane_${S16}_$T; mkdir -p $L
BOT="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW"
cp -p "$BOT/baselib.dll" $W/old_versions/baselib-DSMP-$(date +%Y-%m-%d-%H%M).dll
cp $W/build/baselib.dll "$BOT/baselib.dll"; cp $W/build/det_ranges_win.tsv "$BOT/det_ranges_win.tsv"; cp -p $W/build/baselib.dll $L/baselib.dll
WSHA=$(shasum -a 256 $W/build/baselib.dll | cut -d' ' -f1)
verdict() { local f=$1 rc=$2
  local p=$(grep -cE '(^|[0-9]{2}:[0-9]{2}:[0-9]{2} )(RESULT )?PASS\b|^.*SUMMARY [0-9]+ PASS, 0 FAIL' $f)
  local x=$(grep -cE '^([0-9]{2}:[0-9]{2}:[0-9]{2} )?(RESULT )?FAIL\b|SUMMARY [0-9]+ PASS, [1-9][0-9]* FAIL|Traceback' $f)
  [[ $rc == 0 && $p -gt 0 && $x == 0 ]] && echo "PASS  pass-lines $p" || echo "FAIL  exit $rc, pass-lines $p, fail-lines $x"; }
run() { local n=$1 d=$2; shift 2; local o=$L/$n.out; echo "  $n $(date +%T)"; (cd $d && nice -n 5 "$@") > $o 2>&1; print "$n $(verdict $o $?)  log $o" > $L/$n.res; }
until P=$(python3 rig.py lease release_gate_win-$$ --pid $$ 2>/dev/null | tail -1); [[ $P == [A-J][A-J] ]]; do sleep 15; done
run win_mp2 $W python3 xplay_run.py $P xplay_wrap.py --math --pin-dylib $D $R/mp2_test.py
run win_v8 $W python3 xplay_run.py $P v8_xplay.py --math --dylib $D
run win_wrap $W python3 xplay_run.py $P wrap_xplay.py --math --dylib $D
run win_pingview $W python3 xplay_run.py $P v10_xplay.py --math --dylib $D
run win_partial $W python3 xplay_run.py $P xplay_wrap.py --math --pin-dylib $D $R/desync_partial_test.py --runs 2 --fallback 1
run win_floorgen10 $W python3 xplay_run.py $P xplay_wrap.py --math $U/teams_desync_pair.py --rounds 10 --dylib $D
(cd $W && python3 wrig.py stop >/dev/null 2>&1)
run win_base_soak $W python3 xplay_run.py $P xplay_wrap.py --math --pin-dylib $D $R/soak_test.py --minutes 15 --full-every 600
python3 rig.py stop $P >/dev/null 2>&1; (cd $W && python3 wrig.py stop >/dev/null 2>&1)
run win_kit $W python3 kit/kit_dryrun.py ${P[1]} --dylib $D --ranges $RG
python3 rig.py stop $P >/dev/null 2>&1; (cd $W && python3 wrig.py stop >/dev/null 2>&1); python3 rig.py release $P release_gate_win-$$ >/dev/null
SUM=$R/release/gate_$S16.summary
{ print -r -- "--- Windows suites rerun $T (Windows build fixed in win/ only; src unchanged): baselib.dll sha256 $WSHA"; cat $L/*.res
  NF=$(cat $L/*.res | awk '$2 == "FAIL"' | wc -l | tr -d ' '); print "WINDOWS RERUN: $NF failed of $(ls $L/*.res | wc -l | tr -d ' ')"; } >> $SUM
tail -12 $SUM
