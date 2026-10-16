#!/bin/zsh
# g7.sh - G7 bridge (as lane mpfix ran it; the --mac-bie path uses a stale l1 loader + bie 0.1.1 whose bridge plugin does not
# load): Mac host with the native bridge + Windows guest W_BRG (C:\DSW_BRG in bottle DSMP: BepInEx be.788
# + DsmpBridge plugins + the gated rig baselib.dll from $WT/win): C#-handled action applied on both, full checks equal,
# 0 desyncs. The previous baselib.dll / det_ranges_win.tsv in DSW_BRG move to DSW_BRG/old_gate13/ (never removed).
source ${0:A:h}/lib.sh
BRG="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW_BRG"; R=$HOME/ds_multiplayer/rig/rig.py
LOCK=$HOME/ds_multiplayer/rig/wine.lock
brg() {
  (cd $WT/win && nice -n 19 zsh build_win.sh) > $OUT/g7_build_win.log 2>&1 || { echo "win build failed"; return 1; }
  mkdir -p "$BRG/old_gate13"; local t=$(date +%Y%m%d-%H%M%S)
  for f in baselib.dll det_ranges_win.tsv; do mv "$BRG/$f" "$BRG/old_gate13/$f.$t"; cp $WT/win/build/$f "$BRG/$f"; done
  [[ -z $(ps -axo etime=,comm= | command grep -iE 'wine|\.exe$' | command grep -ve '-[0-9][0-9]:') ]] || { echo "wine busy"; return 1; }
  mkdir $LOCK || { echo "wine.lock held"; return 1; }; echo gate13 > $LOCK/owner
  local P=$(python3 $R lease gate13 --pid $$ | tail -1) rc=1
  if [[ $P == [A-J][A-J] ]]; then
    (cd $HOME/ds_multiplayer/ext/bepinex_bridge && python3 tools/brg_wrig.py xplay $P tests/bridge_xplay.py --dylib $D); rc=$?
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/ext/bepinex_bridge && python3 tools/brg_wrig.py stop) >/dev/null 2>&1
    python3 $R release $P gate13 >/dev/null
  fi
  mkdir -p $HOME/ds_multiplayer/rig/old; mv $LOCK/owner $HOME/ds_multiplayer/rig/old/wine.lock.owner.gate13.$(date +%s); rmdir $LOCK
  return $rc
}
chk g7_bridge 'RESULT PASS' $WT brg
