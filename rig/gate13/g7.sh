#!/bin/zsh
# g7.sh - G7 bridge (as lane mpfix ran it; the --mac-bie path uses a stale l1 loader + bie 0.1.1 whose bridge plugin does not
# load): Mac host with the native bridge + Windows guest W_BRG (C:\DSW_BRG in bottle DSMP: BepInEx be.788
# + DsmpBridge plugins + the gated rig baselib.dll from $WT/win): C#-handled action applied on both, full checks equal,
# 0 desyncs. The previous baselib.dll / det_ranges_win.tsv in DSW_BRG move to DSW_BRG/old_gate13/ (never removed).
source ${0:A:h}/lib.sh
BRG="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW_BRG"; R=$HOME/ds_multiplayer/rig/rig.py
brg() {
  (cd $WT/win && nice -n 19 zsh build_win.sh) > $OUT/g7_build_win.log 2>&1 || { echo "win build failed"; return 1; }
  mkdir -p "$BRG/old_gate13"; local t=$(date +%Y%m%d-%H%M%S)
  for f in baselib.dll det_ranges_win.tsv; do mv "$BRG/$f" "$BRG/old_gate13/$f.$t"; cp $WT/win/build/$f "$BRG/$f"; done
  python3 $HOME/ds_multiplayer/rig/wine_lease.py take gate13 --pid $$ || { echo "wine busy / locked"; return 1; }
  local P=$(python3 $R lease gate13 --pid $$ | tail -1) rc=1
  if [[ $P == [A-J][A-J] ]]; then
    (cd $HOME/ds_multiplayer/ext/bepinex_bridge && python3 tools/brg_wrig.py xplay $P tests/bridge_xplay.py --dylib $D); rc=$?
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/ext/bepinex_bridge && python3 tools/brg_wrig.py stop) >/dev/null 2>&1
    python3 $R release $P gate13 >/dev/null
  fi
  python3 $HOME/ds_multiplayer/rig/wine_lease.py release gate13 >/dev/null
  return $rc
}
chk g7_bridge 'RESULT PASS' $WT brg
