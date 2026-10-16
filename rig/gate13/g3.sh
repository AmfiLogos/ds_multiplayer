#!/bin/zsh
# g3.sh - G3 (team-lead 2026-10-16): weapon slot --auto last --flip, host with the mod + stock guest (copies
# ~/ds_launcher/test_env/pair), full-state check on: 0 desyncs, prefs unchanged; plus a mixed gameplay mod set refused at
# join with the mod named (mixed_set.py, gcd-cdr: the weapon-slot mod is no loader module and does not ship). The old
# 6-minute cast run is dropped: the mod stays gameplay (mpfix), so a mixed set differs in QuickSlots by design.
source ${0:A:h}/lib.sh
R=$HOME/ds_multiplayer/rig/rig.py; HERE=${0:A:h}
ws() {   # <log name> <min casts> <weaponslot args...>
  local n=$1 c=$2; shift 2; local P=$(python3 $R lease gate13 --pid $$ | tail -1)
  [[ $P == (AB|CD|EF) ]] || { echo "no AB/CD/EF lease ($P)"; [[ $P == [A-J][A-J] ]] && python3 $R release $P gate13; return 1; }
  python3 $LD/tests/weaponslot_pair.py $P "$@" --dylib $D; local rc=$?
  python3 $R release $P gate13 >/dev/null; python3 $HERE/ws_check.py $OUT/$n.log $c && return $rc
}
chk g3_flip 'WS PASS' $LD ws g3_flip 0 --auto last --flip
chk g3_mixed_set 'RESULT 4/4 pass' $WT python3 rig/gate13/mixed_set.py
