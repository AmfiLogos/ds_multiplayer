#!/bin/zsh
# g020.sh - the v0.1.10 gate (loader bundle v0.1.10: the game's 10 languages in the loader, multiplayer and Custom
# Modes; mp page buttons). Copy of g019.sh (g018, g017) + i18n: i18n_tool check in the 3 repos, a live switch through the 10 languages (Mac), a ja host + ru guest pair, Windows boot + Mods window in ja. g018 notes: the gate's tools from rig/gate13; the wine lock taken by the gate
# only around the Windows steps (owner ship018-gate, GATE_WINE_OWNER); rig pairs leased under a unique name (ship018-<pid>)
# with the dylib pinned (pin_run.py); the loader host tests (fastboot) as a check.
# Env: WT = MP checkout (its build/ = the rig dylibs), LD = loader checkout (its build/ = the shipped files),
#      CMR = Custom Modes checkout, REL = artifact folder (.dmg, Setup.exe, Windows kit zip, mod zips), PKGZ = folder with
#      the BepInEx / MelonLoader release zips the build pins, GATE_DIR = output folder.
# After publishing: the real GitHub release is offered to an older test copy (loader/start/win/test/real_release_test.sh,
#      loader/test/update/mac_gate.sh ... real 0.1.8).
H=${0:A:h:h}/gate13; source $H/lib.sh
REL=${REL:-$WT/ext/gate020/release_v020}; CMR=${CMR:-$HOME/ds_custommodes}; PKGZ=${PKGZ:-$HOME/ds_mp_gate13/ext/gate13/pkgzips}
export GATE_WINE_OWNER=ship018-gate; WL=$HOME/ds_multiplayer/rig/wine_lease.py
[[ -z ${GATE_ONLY:-} && -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }   # a GATE_ONLY rerun appends to the same summary
if [[ -z ${GATE_ONLY:-} ]]; then
{ echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"
  for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } > $OUT/inputs.txt
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v0.1.10 gate $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
else
  IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64); print "rerun $(date '+%F %T') GATE_ONLY=$GATE_ONLY" >> $SUM
fi
trap "verdict ${0:A}" EXIT
# held wine lock (Windows section): python tests via held_lock_run.py, wine_lease.py users (shell tests) on a private lock
WH=($H/held_lock_run.py); WE=(WINE_LEASE_LOCK=$HOME/ds_multiplayer/rig/wine.lock.gate13-inner)
LR=$H/ld_run.py
chk g020_host_tests '== ([0-9]+) passed, 0 failed' $LD zsh loader/test/run_tests.sh
# ---- release files: names, bytes, saves, licenses
scan() { local M=$(mktemp -d); hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/*.dmg >/dev/null || return 1
  python3 $H/banscan.py $M $REL/* $LD/loader/build/win/kit; local rc=$?; hdiutil detach $M -quiet; (( rc == 0 )) && echo "SCAN ALL PASS"; return $rc; }
chk g020_scan 'SCAN ALL PASS' $WT scan
lic() {   # both license files in every release file (app, Windows kit and zip, each mod zip) - reviewdocs
  local M=$(mktemp -d) bad=0; hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/*.dmg >/dev/null || return 1
  for f in LICENSE.txt THIRD_PARTY_NOTICES.txt; do [[ -s "$M/Dungeon Settlers Modded.app/Contents/Resources/licenses/$f" ]] || { echo "app lacks licenses/$f"; bad=1; }
    [[ -s $LD/loader/build/win/kit/files/licenses/$f || -s $LD/loader/build/win/kit/licenses/$f ]] || { echo "Windows kit lacks licenses/$f"; bad=1; }; done
  hdiutil detach $M -quiet
  for z in $REL/*.zip; do local l=$(unzip -Z1 $z | command grep -cE '(^|/)licenses/LICENSE[^/]*$') t=$(unzip -Z1 $z | command grep -cE '(^|/)licenses/THIRD_PARTY_NOTICES\.txt$')
    (( l >= 1 && t >= 1 )) || { echo "${z:t}: licenses/LICENSE* $l, THIRD_PARTY_NOTICES.txt $t"; bad=1; }; done
  (( bad )) || echo "LICENSES PASS"; return $bad; }
chk g020_licenses 'LICENSES PASS' $WT lic
chk g020_notices 'notices up to date' $LD env MP_REPO=$WT CM_REPO=$CMR LLVM_MINGW=$HOME/tools/llvm-mingw NSIS_SRC=$HOME/tools/nsis_build/nsis-3.11 python3 licenses/make_notices.py --check
# ---- network (netrev2)
i18ntool() {   # every table of the three repos: 0 missing, 0 stale, 0 bad (i18n_tool.py check; exit 1 on a bad entry)
  local rc=0 n=0 l
  for d in $LD/loader/lang $WT/lang $CMR/lang; do
    l=$(python3 $LD/tools/i18n_tool.py check $d 2>&1) || rc=1; print -r -- "$d"; print -r -- "$l"
    n=$(( n + $(print -r -- "$l" | command grep -cE ': [0-9]+/[0-9]+ done, 0 missing, 0 stale, 0 bad') ))
  done
  (( rc == 0 && n == 27 )) && echo "I18N TABLES PASS 27/27"; }
chk g020_i18n_tool 'I18N TABLES PASS 27/27' $WT i18ntool
chk g020_net_test 'ALL PASS: 0 failure' $WT make -C src/net test
chk g020_net_asan 'ALL PASS: 0 failure' $WT zsh -c 'make -C src/net asan 2>&1 | tee /dev/stderr | grep -q "ERROR: AddressSanitizer" && exit 1; exit 0'
netone() { make -C src/net all >/dev/null || return 1; local t rc=0; for t in full_big hello_max fe_stale; do src/net/build/test_dsnet --only $t || rc=1; done; return $rc; }
chk g020_net_one 'PASS fe_stale: .*equal' $WT netone
# ---- builder (required references) + installer lock (harness)
refs() { local C=$OUT/g020_cargo_raw.txt; (cd $LD && DS_REQUIRE_REFS=1 nice -n 19 cargo test --release -p ds-unit-builder --no-fail-fast) > $C 2>&1
  command grep -E '^REF |test result|FAILED|panicked' $C
  (( $(command grep -c '^REF RAN' $C) == 4 )) && ! command grep -qE '^REF (SKIP|FAIL)|FAILED|panicked' $C && command grep -q 'test result: ok' $C && echo "REFS PASS"; }
chk g020_builder_refs 'REFS PASS' $LD refs
chk g020_installer_lock 'installer_lock: 0 failure\(s\)' $LD env DSML_BEPINEX_ZIP=$(print $PKGZ/bepinex-mac-arm64-*.zip(N[-1])) DSML_MELON_ZIP=$(print $PKGZ/melonloader-mac-arm64-*.zip(N[-1])) zsh app/test/installer_lock.sh
# ---- Mac app and loader copies
chk g020_app_test '^== 0 failure' $LD zsh app/test/run.sh
chk g020_dmg_start 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $(print $REL/*.dmg) $OUT/g020_dmg_start
chk g020_mod_zip 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $(print $REL/*.dmg) $OUT/g020_mod_zip $(print $REL/gcd-cdr-*.zip(N[1]))
hell() {   # Hell on (copy Z): 20/20 patches + table half loaded from the delta, staff strip on Hell only (crossmods)
  python3 $WT/ext/crossmods13/mac_mods_test.py $LD/loader --game-s 120 || return 1
  local L=$LD/loader/test_env/instances/Z/dsload.log
  local a=$(command grep -c "Hell tables: the game loads" $L) b=$(command grep -c "no delta" $L); echo "Hell tables lines $a, no-delta lines $b"
  (( a >= 1 && b == 0 )) && echo "HELL TABLES PASS"; }
chk g020_hell 'HELL TABLES PASS' $WT hell
chk g020_notch '(^| )4 PASS 0 FAIL' $LD/loader python3 test/notch_test.py build/mods_all/mac
chk g020_l1_world '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_world_test.py $OUT/g020_l1_world
chk g020_l1_soft '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_test.py $OUT/g020_l1_soft
chk g020_l1_pkg '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_pkg_test.py $OUT/g020_l1_pkg
chk g020_progress_strip '8/8 checks, violations 0' $LD/loader env DESIGN=strip python3 $LR l1_progress_test.py $OUT/g020_progress_strip
chk g020_progress_ornament '8/8 checks, violations 0' $LD/loader env DESIGN=ornament python3 $LR l1_progress_test.py $OUT/g020_progress_ornament
chk g020_screen '6/6 checks, violations 0' $LD/loader python3 $LR l1_screen_test.py $OUT/g020_screen
# ---- i18n in the game (Mac, hidden copies; test builds = this release + the local test commands: ~/ds_ld_ship020t,
#      ~/ds_mp_ship020t (build/lang = mp + cm tables), ~/ds_cm_ship020t; never shipped)
IG=${0:A:h}/i18n
i18ncycle() { local O=$HOME/ds_mp_ship020/ext/gate020/i18n/out/cycle_ja; [[ -d $O ]] && { mkdir -p $O:h/old; mv $O $O:h/old/cycle_ja.$(date +%H%M%S); }
  (cd $IG && python3 leased.py python3 cycle.py 7 0,1,2,3,4,5,6,8,9) > $OUT/g020_i18n_cycle_raw.log 2>&1; tail -5 $OUT/g020_i18n_cycle_raw.log
  python3 $IG/i18n_eval.py $O; }
chk g020_i18n_cycle 'RESULT 4/4 pass' $WT i18ncycle
i18npair() { local O=$HOME/ds_mp_ship020/ext/gate020/i18n/out/pair2_ja_ru; [[ -d $O ]] && { mkdir -p $O:h/old; mv $O $O:h/old/pair2_ja_ru.$(date +%H%M%S); }
  (cd $IG && python3 pairtest2.py 7 4) > $OUT/g020_i18n_pair_raw.log 2>&1; tail -3 $OUT/g020_i18n_pair_raw.log
  python3 $IG/i18n_eval.py $O --pair ja ru; }
chk g020_i18n_pair 'RESULT 4/4 pass' $WT i18npair
chk g020_fastboot_ui '([0-9]+) PASS 0 FAIL' $LD/loader python3 test/soft/fastboot_ui_test.py $OUT/g020_fastboot_ui   # boot ~3 s, Unity splash at the stock place, switch: after the boot, no tooltip, click flips
# ---- saves (cloud backup, import, safety, save warning: fake cloud and test copy K only)
chk g020_cloud_unit '== 25 ok, 0 failed' $LD/loader zsh test/cloud/run.sh
chk g020_cloud '7/7 checks, violations 0' $LD/loader python3 $LR l1_cloud_test.py $OUT/g020_cloud
chk g020_safety '7/7 checks, violations 0' $LD/loader python3 $LR l1_safety_test.py $OUT/g020_safety
chk g020_savewarn '7/7 checks, violations 0' $LD/loader python3 $LR l1_savewarn_test.py $OUT/g020_savewarn
# ---- auto-update with local test releases (packaging)
chk g020_update_mac 'RESULT 24/24 pass' $LD zsh loader/test/update/mac_gate.sh $OUT/g020_update_mac $LD/loader/test_env/instances/Z/DungeonSettlers.app
# ---- Windows (wine bottle DSMP; the gate holds the wine lock for this block only, then gives it back)
print "g020_wine_lock $(python3 $WL take ship018-gate --pid $$ --wait 3600 >/dev/null && echo PASS || echo FAIL)" | tee -a $SUM
# a Windows test that takes rig/wine.lock with its own mkdir (update_test.sh) gets the lock from the gate for its run
handover() { local hp=$(sed -n 2p $HOME/ds_multiplayer/rig/wine.lock/owner) rc
  python3 $HOME/ds_multiplayer/rig/wine_lease.py release ship018-gate >/dev/null || return 1
  "$@"; rc=$?
  python3 $HOME/ds_multiplayer/rig/wine_lease.py take ship018-gate --pid $hp --wait 600 >/dev/null || { echo "could not retake wine.lock"; return 1; }
  return $rc; }
# the default install folder is moved aside (never removed) before a Windows test that installs there: the new installer
# refuses a folder that already holds files (a previous test's kept saves)
fresh_root() { local R="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/users/crossover/AppData/Local/Dungeon Settlers Modded"
  [[ -e $R ]] && mv "$R" "$R.gate018-$(date +%Y%m%d-%H%M%S)"; true; }
updwin() { fresh_root; handover env KIT=$LD/loader/build/win/kit zsh loader/start/win/test/update_test.sh; }
chk g020_update_win '== 0 failed' $LD updwin
chk g020_uninst 'RESULT ([0-9]+)/\1 pass' $WT python3 $WH $LD/loader/start/win/installer/test/uninst_test.py $(print $REL/*Setup*.exe) $LD/loader/build/win/kit
chk g020_setup 'RESULT ([0-9]+)/\1 pass' $WT python3 $WH $H/setup_test.py $(print $REL/*Setup*.exe) $OUT/g020_setup
winst() { fresh_root; env $WE zsh loader/start/win/test/winstart_test.sh; }
chk g020_winstart '== 0 failed' $LD winst
chk g020_nonascii 'RESULTS 9 / 9' $LD python3 $WH loader/start/win/test/nonascii_test.py loader/build/win/kit $LD/loader/build/mods_all/win/gcd-cdr
chk g020_w_pkg '([0-9]+)/\1 checks' $WT python3 $WH $H/w_pkg_gate.py $REL $OUT/g020_w_pkg
chk g020_pathwarn '== ([0-9]+)/\1 passed' $LD python3 $WH loader/start/win/test/pathwarn_test.py $(print $REL/*Setup*.exe) $LD/loader/build/win/kit
chk g020_win_cjk '([0-9]+)/\1 checks' $WT python3 $WH $H/w_cjk_gate.py $LD/loader/build/win/kit $OUT/g020_win_cjk ja
pvpx() {   # PvP v1, Mac host + Windows guest W (C:\DSW, this tree's baselib.dll); every check incl. 6 (0 desyncs, 500+ ticks)
  local DSW="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/DSW" R=$HOME/ds_multiplayer/rig/rig.py LOCK=$HOME/ds_multiplayer/rig/wine.lock t=$(date +%Y%m%d-%H%M%S)
  (cd $WT/win && nice -n 19 zsh build_win.sh > $OUT/g020_build_win.log 2>&1) || { echo "win build failed"; return 1; }
  git -C $WT checkout -q win/win_map.tsv
  mkdir -p "$DSW/old_gate13"; for f in baselib.dll det_ranges_win.tsv; do mv "$DSW/$f" "$DSW/old_gate13/$f.$t"; cp $WT/win/build/$f "$DSW/$f"; done
  [[ $(head -1 $LOCK/owner 2>/dev/null) == ship018-gate ]] || { echo "the gate must hold wine.lock (ship018-gate)"; return 1; }
  local P=$(python3 $R lease ship018-$$ --pid $$ | tail -1) rc=1 XL=$OUT/g020_pvp_raw.log
  if [[ $P == [A-J][A-J] ]]; then
    (cd $HOME/ds_multiplayer/win && python3 xplay_run.py $P $WT/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib $D --tag gate020_xv1) > $XL 2>&1
    command grep -E '(PASS|FAIL) [0-9]' $XL; echo "raw log (xplay_run's release() stub raises at the end): $XL"
    local f=$(command grep -cE '^[0-9:]+ FAIL [0-9]' $XL) p=$(command grep -cE '^[0-9:]+ PASS [0-9]' $XL)
    (( f == 0 && p >= 10 )) && { echo "XPV PASS all $p checks"; rc=0; } || echo "XPV FAIL $f failed"
    python3 $R stop $P >/dev/null 2>&1; (cd $HOME/ds_multiplayer/win && python3 wrig.py stop) >/dev/null 2>&1; python3 $R release $P ship018-$$ >/dev/null
  fi
  return $rc
}
chk g020_pvp_xplay 'XPV PASS' $WT pvpx
python3 $WL release ship018-gate >/dev/null
# ---- multiplayer in the rig
rigstep() {   # smoke | mp2 on a pair leased as ship018-<pid> (waits up to 8 min), the dylib pinned by rig/pin_run.py
  local R=$HOME/ds_multiplayer/rig N=ship018-$$ P= i rc L=$OUT/g020_$1_raw.log
  for i in {1..48}; do P=$(python3 $R/rig.py lease $N --pid $$ 2>/dev/null | tail -1); [[ $P == [A-J][A-J] ]] && break; P=; sleep 10; done
  [[ -n $P ]] || { echo "no free pair after 8 min"; return 2; }
  if [[ $1 == smoke ]]; then (cd $R && RIG_PAIR=$P MP2_PAIR=$P python3 pin_run.py $D rig.py smoke) > $L 2>&1; rc=$?
    echo "pair $P smoke $(command grep -c "fp_tick_advanced': True" $L)/3"
  else (cd $R && RIG_PAIR=$P MP2_PAIR=$P python3 pin_run.py $D mp2_test.py --ticks 3000) > $L 2>&1; rc=$?
    echo "pair $P mp2 $(command grep -c ' PASS ' $L) PASS, $(command grep -c ' FAIL ' $L) FAIL, tb $(command grep -c Traceback $L)"; command grep ' FAIL ' $L | cut -c1-200; fi
  python3 $R/rig.py release $P $N >/dev/null; echo "raw log $L; prefs $(cd $R && python3 rig.py check-prefs)"; return $rc; }
chk g020_smoke 'smoke 3/3' $WT rigstep smoke
chk g020_mp2 ' 19 PASS, 0 FAIL, tb 0' $WT rigstep mp2
fullcheck() { local L=$OUT/g020_fastjoin_${1:-off}_raw.log; (cd $WT/ext/latejoin && python3 fastjoin_pair.py --mode ${1:-off} --dylib $D --full-every 300) > $L 2>&1; local rc=$?; cat $L
  command grep -qE 'PASS full-state checks ran, none failed ok [1-9][0-9]* fails 0' $L && command grep -q 'PASS no desync, no full / deep difference' $L && echo "FULLCHECK PASS" && return $rc; return 1; }
chk g020_fullcheck 'FULLCHECK PASS' $WT fullcheck off
chk g020_fullcheck_on 'FULLCHECK PASS' $WT fullcheck on   # Fast join On is the default since 2164719
chk g020_mixed_set 'RESULT 4/4 pass' $WT python3 $H/mixed_set.py
print "g020_inputs_unchanged $([[ $( { echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"; for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "g020_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
