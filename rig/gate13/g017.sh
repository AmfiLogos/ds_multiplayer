#!/bin/zsh
# g017.sh - the v0.1.7 gate (loader bundle v0.1.7 in ds_modloader; mods in their own repos). Owner rule: only tests whose
# failure means a real break; every lane's own gate line (ext/gate13/v134_gate_todo.md, 2026-10-17) is a check here.
# Env: WT = MP checkout (its build/ = the rig dylibs), LD = loader checkout (its build/ = the shipped files),
#      CMR = Custom Modes checkout, REL = artifact folder (.dmg, Setup.exe, Windows kit zip, mod zips), PKGZ = folder with
#      the BepInEx / MelonLoader release zips the build pins. Windows steps: hold rig/wine.lock as gate13-gate before
#      (held_lock_run.py makes each test's own take/release a no-op on the held lock).
# After publishing: g017_post.sh (the real GitHub release is offered to an older test copy).
source ${0:A:h}/lib.sh; H=${0:A:h}
REL=${REL:-$WT/ext/gate13/release_v017}; CMR=${CMR:-$HOME/ds_custommodes}; PKGZ=${PKGZ:-$WT/ext/gate13/pkgzips}
[[ -z ${GATE_ONLY:-} && -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }   # a GATE_ONLY rerun appends to the same summary
if [[ -z ${GATE_ONLY:-} ]]; then
{ echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"
  for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } > $OUT/inputs.txt
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v0.1.7 gate $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
else
  IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64); print "rerun $(date '+%F %T') GATE_ONLY=$GATE_ONLY" >> $SUM
fi
trap "verdict ${0:A}" EXIT
WH=(); WE=()   # held wine lock: python tests via held_lock_run.py, wine_lease.py users (shell tests) on a private lock
[[ $(head -1 $HOME/ds_multiplayer/rig/wine.lock/owner 2>/dev/null) == gate13-gate ]] && { WH=($H/held_lock_run.py); WE=(WINE_LEASE_LOCK=$HOME/ds_multiplayer/rig/wine.lock.gate13-inner); }
LR=$H/ld_run.py
# ---- release files: names, bytes, saves, licenses
scan() { local M=$(mktemp -d); hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/*.dmg >/dev/null || return 1
  python3 $H/banscan.py $M $REL/* $LD/loader/build/win/kit; local rc=$?; hdiutil detach $M -quiet; (( rc == 0 )) && echo "SCAN ALL PASS"; return $rc; }
chk g017_scan 'SCAN ALL PASS' $WT scan
lic() {   # both license files in every release file (app, Windows kit and zip, each mod zip) - reviewdocs
  local M=$(mktemp -d) bad=0; hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/*.dmg >/dev/null || return 1
  for f in LICENSE.txt THIRD_PARTY_NOTICES.txt; do [[ -s "$M/Dungeon Settlers Modded.app/Contents/Resources/licenses/$f" ]] || { echo "app lacks licenses/$f"; bad=1; }
    [[ -s $LD/loader/build/win/kit/files/licenses/$f || -s $LD/loader/build/win/kit/licenses/$f ]] || { echo "Windows kit lacks licenses/$f"; bad=1; }; done
  hdiutil detach $M -quiet
  for z in $REL/*.zip; do local n=$(unzip -Z1 $z | command grep -cE '(^|/)licenses/(LICENSE|THIRD_PARTY_NOTICES)\.txt$'); (( n >= 2 )) || { echo "${z:t} has $n of the 2 license files"; bad=1; }; done
  (( bad )) || echo "LICENSES PASS"; return $bad; }
chk g017_licenses 'LICENSES PASS' $WT lic
chk g017_notices 'notices up to date' $LD env MP_REPO=$WT CM_REPO=$CMR LLVM_MINGW=$HOME/tools/llvm-mingw NSIS_SRC=$HOME/tools/nsis_build/nsis-3.11 python3 licenses/make_notices.py --check
# ---- network (netrev2)
chk g017_net_test 'ALL PASS: 0 failure' $WT make -C src/net test
chk g017_net_asan 'ALL PASS: 0 failure' $WT zsh -c 'make -C src/net asan 2>&1 | tee /dev/stderr | grep -q "ERROR: AddressSanitizer" && exit 1; exit 0'
netone() { make -C src/net all >/dev/null || return 1; local t rc=0; for t in full_big hello_max fe_stale; do src/net/build/test_dsnet --only $t || rc=1; done; return $rc; }
chk g017_net_one 'PASS fe_stale: .*equal' $WT netone
# ---- builder (required references) + installer lock (harness)
refs() { local C=$OUT/g017_cargo_raw.txt; (cd $LD && DS_REQUIRE_REFS=1 nice -n 19 cargo test --release -p ds-unit-builder --no-fail-fast) > $C 2>&1
  command grep -E '^REF |test result|FAILED|panicked' $C
  (( $(command grep -c '^REF RAN' $C) == 4 )) && ! command grep -qE '^REF (SKIP|FAIL)|FAILED|panicked' $C && command grep -q 'test result: ok' $C && echo "REFS PASS"; }
chk g017_builder_refs 'REFS PASS' $LD refs
chk g017_installer_lock 'installer_lock: 0 failure\(s\)' $LD env DSML_BEPINEX_ZIP=$(print $PKGZ/bepinex-mac-arm64-*.zip(N[-1])) DSML_MELON_ZIP=$(print $PKGZ/melonloader-mac-arm64-*.zip(N[-1])) zsh app/test/installer_lock.sh
# ---- Mac app and loader copies
chk g017_app_test '^== 0 failure' $LD zsh app/test/run.sh
chk g017_dmg_start 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $(print $REL/*.dmg) $OUT/g017_dmg_start
chk g017_mod_zip 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $(print $REL/*.dmg) $OUT/g017_mod_zip $(print $REL/gcd-cdr-*.zip(N[1]))
hell() {   # Hell on (copy Z): 20/20 patches + table half loaded from the delta, staff strip on Hell only (crossmods)
  python3 $WT/ext/crossmods13/mac_mods_test.py $LD/loader --game-s 120 || return 1
  local L=$LD/loader/test_env/instances/Z/dsload.log
  local a=$(command grep -c "Hell tables: the game loads" $L) b=$(command grep -c "no delta" $L); echo "Hell tables lines $a, no-delta lines $b"
  (( a >= 1 && b == 0 )) && echo "HELL TABLES PASS"; }
chk g017_hell 'HELL TABLES PASS' $WT hell
chk g017_notch '(^| )4 PASS 0 FAIL' $LD/loader python3 test/notch_test.py build/mods_all/mac
chk g017_l1_world '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_world_test.py $OUT/g017_l1_world
chk g017_l1_soft '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_test.py $OUT/g017_l1_soft
chk g017_l1_pkg '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_pkg_test.py $OUT/g017_l1_pkg
chk g017_progress_strip '8/8 checks, violations 0' $LD/loader env DESIGN=strip python3 $LR l1_progress_test.py $OUT/g017_progress_strip
chk g017_progress_ornament '8/8 checks, violations 0' $LD/loader env DESIGN=ornament python3 $LR l1_progress_test.py $OUT/g017_progress_ornament
chk g017_screen '6/6 checks, violations 0' $LD/loader python3 $LR l1_screen_test.py $OUT/g017_screen
# ---- saves (cloud backup, import, safety, save warning: fake cloud and test copy K only)
chk g017_cloud_unit '== 25 ok, 0 failed' $LD/loader zsh test/cloud/run.sh
chk g017_cloud '7/7 checks, violations 0' $LD/loader python3 $LR l1_cloud_test.py $OUT/g017_cloud
chk g017_safety '7/7 checks, violations 0' $LD/loader python3 $LR l1_safety_test.py $OUT/g017_safety
chk g017_savewarn '7/7 checks, violations 0' $LD/loader python3 $LR l1_savewarn_test.py $OUT/g017_savewarn
# ---- auto-update with local test releases (packaging)
chk g017_update_mac 'RESULT 24/24 pass' $LD zsh loader/test/update/mac_gate.sh $OUT/g017_update_mac $LD/loader/test_env/instances/Z/DungeonSettlers.app
# ---- Windows (wine bottle DSMP; the gate holds the wine lock for all of them)
chk g017_update_win '== 0 failed' $LD env $WE KIT=$LD/loader/build/win/kit zsh loader/start/win/test/update_test.sh
chk g017_uninst 'RESULT 10/10 pass' $WT python3 $WH $LD/loader/start/win/installer/test/uninst_test.py $(print $REL/*Setup*.exe) $LD/loader/build/win/kit
chk g017_setup 'RESULT ([0-9]+)/\1 pass' $WT python3 $WH $H/setup_test.py $(print $REL/*Setup*.exe) $OUT/g017_setup
chk g017_winstart '== 0 failed' $LD env $WE zsh loader/start/win/test/winstart_test.sh
chk g017_nonascii 'RESULTS 9 / 9' $LD python3 $WH loader/start/win/test/nonascii_test.py loader/build/win/kit $LD/loader/build/mods_all/win/gcd-cdr
chk g017_w_pkg '([0-9]+)/\1 checks' $WT python3 $WH $H/w_pkg_gate.py $REL $OUT/g017_w_pkg
# ---- multiplayer in the rig
G=$HOME/ds_multiplayer/ext/merger13/tools/gate.sh
chk g017_smoke 'smoke 3/3' $WT zsh $G $D gate13 smoke
chk g017_mp2 ' 19 PASS, 0 FAIL, tb 0' $WT zsh $G $D gate13 mp2
fullcheck() { local L=$OUT/g017_fastjoin_${1:-off}_raw.log; (cd $WT/ext/latejoin && python3 fastjoin_pair.py --mode ${1:-off} --dylib $D --full-every 300) > $L 2>&1; local rc=$?; cat $L
  command grep -qE 'PASS full-state checks ran, none failed ok [1-9][0-9]* fails 0' $L && command grep -q 'PASS no desync, no full / deep difference' $L && echo "FULLCHECK PASS" && return $rc; return 1; }
chk g017_fullcheck 'FULLCHECK PASS' $WT fullcheck off
chk g017_fullcheck_on 'FULLCHECK PASS' $WT fullcheck on   # Fast join On is the default since 2164719
pvpx() {   # PvP v1, Mac host + Windows guest W (C:\DSW, this tree's baselib.dll); every check incl. 6 (0 desyncs, 500+ ticks)
  local DSW="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW" R=$HOME/ds_multiplayer/rig/rig.py LOCK=$HOME/ds_multiplayer/rig/wine.lock t=$(date +%Y%m%d-%H%M%S)
  (cd $WT/win && nice -n 19 zsh build_win.sh > $OUT/g017_build_win.log 2>&1) || { echo "win build failed"; return 1; }
  git -C $WT checkout -q win/win_map.tsv
  mkdir -p "$DSW/old_gate13"; for f in baselib.dll det_ranges_win.tsv; do mv "$DSW/$f" "$DSW/old_gate13/$f.$t"; cp $WT/win/build/$f "$DSW/$f"; done
  [[ $(head -1 $LOCK/owner 2>/dev/null) == gate13-gate ]] || { echo "the gate must hold wine.lock (gate13-gate)"; return 1; }
  local P=$(python3 $R lease gate13-$$ --pid $$ | tail -1) rc=1 XL=$OUT/g017_pvp_raw.log
  if [[ $P == [A-J][A-J] ]]; then
    (cd $HOME/ds_multiplayer/win && python3 xplay_run.py $P $WT/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib $D --tag gate017_xv1) > $XL 2>&1
    command grep -E '(PASS|FAIL) [0-9]' $XL; echo "raw log (xplay_run's release() stub raises at the end): $XL"
    local f=$(command grep -cE '^[0-9:]+ FAIL [0-9]' $XL) p=$(command grep -cE '^[0-9:]+ PASS [0-9]' $XL)
    (( f == 0 && p >= 10 )) && { echo "XPV PASS all $p checks"; rc=0; } || echo "XPV FAIL $f failed"
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/win && python3 wrig.py stop) >/dev/null 2>&1; python3 $R release $P gate13-$$ >/dev/null
  fi
  return $rc
}
chk g017_pvp_xplay 'XPV PASS' $WT pvpx
chk g017_mixed_set 'RESULT 4/4 pass' $WT python3 $H/mixed_set.py
print "g017_inputs_unchanged $([[ $( { echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"; for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "g017_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
