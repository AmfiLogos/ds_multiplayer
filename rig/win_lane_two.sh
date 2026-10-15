#!/bin/zsh
# rerun win_base_soak + win_kit after the 16:33 fixes (W stopped before the soak; Mac ranges kept from Windows)
set -u; cd "${0:A:h}"; R=$PWD; ROOT=${R:h}; export DSRIG_RELEASE=1; W=$ROOT/win
S16=$1; D=$R/release/dsrig-$S16.dylib; RG=$R/release/det_ranges-$S16.tsv; export DSRIG_RANGES=$RG
T=$(date +%Y%m%d-%H%M); L=$R/det_out/win_lane2_${S16}_$T; mkdir -p $L
BOT="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW"
cp -p "$BOT/baselib.dll" $W/old_versions/baselib-DSMP-$(date +%Y-%m-%d-%H%M).dll; cp $W/build/baselib.dll "$BOT/baselib.dll"; cp $W/build/det_ranges_win.tsv "$BOT/det_ranges_win.tsv"
WSHA=$(shasum -a 256 $W/build/baselib.dll | cut -d' ' -f1)
verdict() { local f=$1 rc=$2
  local p=$(grep -cE '(^|[0-9]{2}:[0-9]{2}:[0-9]{2} )(RESULT )?PASS\b|^.*SUMMARY [0-9]+ PASS, 0 FAIL' $f)
  local x=$(grep -cE '^([0-9]{2}:[0-9]{2}:[0-9]{2} )?(RESULT )?FAIL\b|SUMMARY [0-9]+ PASS, [1-9][0-9]* FAIL|Traceback' $f)
  [[ $rc == 0 && $p -gt 0 && $x == 0 ]] && echo "PASS  pass-lines $p" || echo "FAIL  exit $rc, pass-lines $p, fail-lines $x"; }
run() { local n=$1 d=$2; shift 2; local o=$L/$n.out; echo "  $n $(date +%T)"; (cd $d && nice -n 5 "$@") > $o 2>&1; print "$n $(verdict $o $?)  log $o" > $L/$n.res; }
until P=$(python3 rig.py lease release_gate_win --pid $$ 2>/dev/null | tail -1); [[ $P == [A-J][A-J] ]]; do sleep 15; done
(cd $W && python3 wrig.py stop >/dev/null 2>&1)
run win_base_soak $W python3 xplay_run.py $P xplay_wrap.py --math --pin-dylib $D $R/soak_test.py --minutes 15 --full-every 600
python3 rig.py stop $P >/dev/null 2>&1; (cd $W && python3 wrig.py stop >/dev/null 2>&1)
run win_kit $W python3 kit/kit_dryrun.py ${P[1]} --dylib $D --ranges $RG
python3 rig.py stop $P >/dev/null 2>&1; (cd $W && python3 wrig.py stop >/dev/null 2>&1); python3 rig.py release $P release_gate_win >/dev/null
{ print -r -- "--- Windows rerun 2 $T (base soak + kit after fixes in win/ and the gate script; src unchanged): baselib.dll sha256 $WSHA"; cat $L/*.res; } >> $R/release/gate_$S16.summary
cat $L/*.res
