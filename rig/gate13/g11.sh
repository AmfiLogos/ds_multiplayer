#!/bin/zsh
# g11.sh - G11 Windows (DSMP bottle, graphics + audio off; one wine copy; wine.lock). Kit = ~/ds_launcher/loader/build/win/kit.
#  win_mods_test: clean AppData copy, install.bat, Mods window (4 mods), Custom Modes on + title button, gcd-cdr lines, Hell
#                 23/23 + tables + staff strip (test kit = release kit + gcd-probe/hell-probe test mods, never shipped)
#  xplay_loader:  Mac host + Windows guest through the loader kit, mp2 19 PASS 0 FAIL with gcd-cdr on both
#  MelonLoader:   hello-melon (Harmony + MonoBehaviour) next to dsload + multiplayer; off again with multiplayer only
source ${0:A:h}/lib.sh
BOT="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/users/crossover/AppData/Local"
K=$OUT/win_testkit; [[ -e $K ]] && { mkdir -p $OUT/old; mv $K $OUT/old/win_testkit.$(date +%s); }
cp -R $LD/loader/build/win/kit $K && cp -R $WT/ext/gate13/probes_win/{gcd-probe,hell-probe} $K/files/mods/ || { echo "no Windows probes"; exit 1; }
clean() { [[ -e "$BOT/Dungeon Settlers Modded" ]] && mv "$BOT/Dungeon Settlers Modded" "$BOT/Dungeon Settlers Modded.gate13-$(date +%Y%m%d-%H%M%S)"; true; }
clean
chk g11_win_mods '(^| )12 PASS 0 FAIL' $WT python3 ext/crossmods13/win_mods_test.py $K
pf() {   # <log> <min pass>: PASS/FAIL lines of a mp2-style log -> "COUNT PASS n/0" or "COUNT FAIL"
  local p=$(command grep -cE '(^| )PASS ' $1) f=$(command grep -cE '(^| )FAIL ' $1)
  (( p >= $2 && f == 0 )) && echo "COUNT PASS $p PASS $f FAIL" || { echo "COUNT FAIL $p PASS $f FAIL"; return 1; }
}
xp() { local L=$OUT/g11_xplay_raw.log; python3 ext/crossmods13/xplay_loader.py $LD/loader --ticks 3000 > $L 2>&1; local rc=$?; cat $L; pf $L 19 && return $rc; }
chk g11_xplay_mp2 'COUNT PASS' $WT xp
W=${0:A:h}/g11_wml.py
wml() {   # every "x/y checks" line must have x == y (run 11/11, off 7/7)
  python3 $W lock || return 1; local rc=0 L=$OUT/g11_ml_raw.log
  { python3 $W install; echo "install exit $?"; python3 $W run $OUT/g11_ml_run; echo "run exit $?"; python3 $W off $OUT/g11_ml_off; echo "off exit $?"; } > $L 2>&1
  python3 $W unlock; cat $L   # verdict = the two check lines (run 11/11, off 7/7); exit codes logged for the record
  local c=$(command grep -oE '[0-9]+/[0-9]+ checks' $L); echo "ML lines: ${(f)c}"
  [[ $(print -l ${(f)c} | wc -l | tr -d ' ') == 2 ]] && print -l ${(f)c} | awk -F'[/ ]' '$1 != $2 {exit 1}' && echo "WML PASS" || { echo "WML FAIL"; return 1; }
}
chk g11_melon 'WML PASS' $WT wml
# PvP v1 on the base map, Mac host + Windows guest W (C:\DSW, gated rig baselib.dll): checks 1, 4, 7 (fepvp's pvp_v1_pair
# through win/xplay_run.py; first run of this pairing, no lane ran it before)
pvpx() {
  local DSW="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW" R=$HOME/ds_multiplayer/rig/rig.py LOCK=$HOME/ds_multiplayer/rig/wine.lock t=$(date +%Y%m%d-%H%M%S)
  [[ -f $WT/win/build/baselib.dll ]] || (cd $WT/win && nice -n 19 zsh build_win.sh > $OUT/g11_build_win.log 2>&1) || return 1
  mkdir -p "$DSW/old_gate13"; for f in baselib.dll det_ranges_win.tsv; do mv "$DSW/$f" "$DSW/old_gate13/$f.$t"; cp $WT/win/build/$f "$DSW/$f"; done
  [[ -z $(ps -axo etime=,comm= | command grep -iE 'wine|\.exe$' | command grep -ve '-[0-9][0-9]:') ]] && mkdir $LOCK || { echo "wine busy / locked"; return 1; }
  echo gate13 > $LOCK/owner; local P=$(python3 $R lease gate13 --pid $$ | tail -1) rc=1
  if [[ $P == [A-J][A-J] ]]; then
    local XL=$OUT/g11_pvp_xplay_raw.log
    (cd $HOME/ds_multiplayer/win && python3 xplay_run.py $P $WT/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib $D --tag gate13_xv1) > $XL 2>&1
    command grep -E '(PASS|FAIL) [0-9]' $XL; echo "raw log (ends with xplay_run's release() TypeError): $XL"
    # verdict = PLAN_V13 G11 checks 1, 4, 7 (the other checks are logged; xplay_run's release() stub raises a TypeError at
    # the very end of pvp_v1_pair, after its results: not a check)
    command grep -q 'PASS 1 ' $XL && command grep -q 'PASS 4 ' $XL && command grep -q 'PASS 7 ' $XL && { echo "XPV PASS checks 1 4 7"; rc=0; } || echo "XPV FAIL"
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/win && python3 wrig.py stop) >/dev/null 2>&1; python3 $R release $P gate13 >/dev/null
  fi
  mkdir -p $HOME/ds_multiplayer/rig/old; mv $LOCK/owner $HOME/ds_multiplayer/rig/old/wine.lock.owner.gate13.$(date +%s); rmdir $LOCK; return $rc
}
chk g11_pvp_v1_xplay 'XPV PASS' $WT pvpx
# Windows My Units: checks 1, 2, 5, 6 (3/4 need the Mac-only units-driver fake mouse; the dive itself is proven by
# mu_dive_mac). Verdict line MUW PASS when those four pass.
muw() { local XL=$OUT/g11_muw_raw.log; python3 rig/gate13/mu_dive_win.py > $XL 2>&1; cat $XL
  for c in 1 2 5 6; do command grep -qE "^PASS $c " $XL || { echo "MUW FAIL check $c"; return 1; }; done; echo "MUW PASS checks 1 2 5 6"; }
chk g11_mu_dive_win 'MUW PASS' $WT muw
