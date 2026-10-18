#!/bin/zsh
# Mac only (owner 2026-10-18): the Windows side is the real Windows VM lane (WIN_GREEN.txt); the Wine checks are in
# g024w.sh, an optional last check that never blocks the release.
# g024.sh - the v0.1.15 gate (loader v0.1.15: crash reports ask first, notice box fits its text, "Global cooldown fix",
# hidden-worlds line, mod settings, "Get the host's mods", ZET-U Mod Suite; multiplayer 0.1.12, custom-modes 0.1.7,
# gcd-cdr 0.1.5, hell-difficulty 0.1.5, zetu-suite 0.1.0; includes the v0.1.14 hotfix). Copy of g023.sh + the hidden-worlds checks
# (host test + in-game l1_hidden_test) and the crash report checks (scrubber unit test on this Mac's own logs; one hidden
# copy crashes and its report reaches the private receiver with none of this Mac's names). i18n game tests on the v0.1.15
# test trees (~/ds_ld_ship024t, ~/ds_mp_ship024t, ~/ds_cm_ship024t = the int014 heads + the local test commits, never shipped).
# g018 notes: the gate's tools from rig/gate13; the wine lock taken by the gate only around the Windows steps (owner
# ship018-gate, GATE_WINE_OWNER); rig pairs leased under a unique name (ship018-<pid>) with the dylib pinned (pin_run.py).
# Env: WT = MP checkout (its build/ = the rig dylibs), LD = loader checkout (its build/ = the shipped files),
#      CMR = Custom Modes checkout, REL = artifact folder (.dmg, Setup.exe, Windows kit zip, mod zips), PKGZ = folder with
#      the BepInEx / MelonLoader release zips the build pins, GATE_DIR = output folder.
H=${0:A:h:h}/gate13; source $H/lib.sh
REL=${REL:-$WT/ext/gate024/release_v024}; CMR=${CMR:-$HOME/ds_custommodes}; PKGZ=${PKGZ:-$HOME/ds_mp_gate13/ext/gate13/pkgzips}
export GATE_WINE_OWNER=ship018-gate; WL=$HOME/ds_multiplayer/rig/wine_lease.py
[[ -z ${GATE_ONLY:-} && -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }   # a GATE_ONLY rerun appends to the same summary
if [[ -z ${GATE_ONLY:-} ]]; then
{ echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"
  for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } > $OUT/inputs.txt
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v0.1.15 gate $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
else
  IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64); print "rerun $(date '+%F %T') GATE_ONLY=$GATE_ONLY" >> $SUM
fi
trap "verdict ${0:A}" EXIT
# held wine lock (Windows section): python tests via held_lock_run.py, wine_lease.py users (shell tests) on a private lock
WH=($H/held_lock_run.py); WE=(WINE_LEASE_LOCK=$HOME/ds_multiplayer/rig/wine.lock.gate13-inner)
LR=$H/ld_run.py
chk g024_host_tests '== ([0-9]+) passed, 0 failed' $LD zsh loader/test/run_tests.sh
# ---- release files: names, bytes, saves, licenses
scan() { local M=$(mktemp -d); hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/*.dmg >/dev/null || return 1
  python3 $H/banscan.py $M $REL/* $LD/loader/build/win/kit; local rc=$?; hdiutil detach $M -quiet; (( rc == 0 )) && echo "SCAN ALL PASS"; return $rc; }
chk g024_scan 'SCAN ALL PASS' $WT scan
modrepos() {   # every module the build ships is an official mod (MOD_REPOS: auto-update, "Get the host's mods"); v0.1.15 shipped zetu-suite outside it at first
  local bad=0 id; for d in $LD/loader/build/mods_all/mac/*(/); do id=${d:t}
    command grep -qE "\\{ \"$id\", \"AmfiLogos/ds_[a-z]+\" \\}" $LD/loader/src/dsl_update.c && echo "ok $id" || { echo "MISSING $id"; bad=1; }; done
  (( bad )) || echo "MOD REPOS PASS"; return $bad; }
chk g024_mod_repos 'MOD REPOS PASS' $WT modrepos
chk g024_ctl_off 'CTL OFF PASS' $WT python3 rig/gate13/ctl_off_check.py --loader-build $LD/loader/build   # a player start (no DSRIG_CTL / DSRIG_PORT) opens no test control channel (Mac: no ctl.sock; Windows DLLs: no default port)
chk g024_mods_check 'modules .*: OK' $LD env MP_REPO=$HOME/ds_multiplayer CM_REPO=$CMR DSMP_ROOT=$HOME/ds_multiplayer zsh loader/modules/build_mods.sh --check   # every built module complete, Mac + Windows
chk g024_rv_gate 'rv gate: ok' $WT python3 tools/rv_gate.py   # every game address goes through rv() (per-game-version tables)
chk g024_cargo_core 'test result: ok. [1-9][0-9]* passed; 0 failed' $LD nice -n 19 cargo test -p dsml-core
chk g024_kitonly '== ([0-9]+) passed, 0 failed' $LD zsh loader/test/update/kitonly_test.sh   # unknown game build: mods off, the kit update still found
chk g024_getmods '== ([0-9]+) passed, 0 failed' $LD zsh loader/test/update/getmods_test.sh   # "Get the host's mods": official releases only, sha256
lic() {   # both license files in every release file (app, Windows kit and zip, each mod zip) - reviewdocs
  local M=$(mktemp -d) bad=0; hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/*.dmg >/dev/null || return 1
  for f in LICENSE.txt THIRD_PARTY_NOTICES.txt; do [[ -s "$M/Dungeon Settlers Modded.app/Contents/Resources/licenses/$f" ]] || { echo "app lacks licenses/$f"; bad=1; }
    [[ -s $LD/loader/build/win/kit/files/licenses/$f || -s $LD/loader/build/win/kit/licenses/$f ]] || { echo "Windows kit lacks licenses/$f"; bad=1; }; done
  hdiutil detach $M -quiet
  for z in $REL/*.zip; do local l=$(unzip -Z1 $z | command grep -cE '(^|/)licenses/LICENSE[^/]*$') t=$(unzip -Z1 $z | command grep -cE '(^|/)licenses/THIRD_PARTY_NOTICES\.txt$')
    (( l >= 1 && t >= 1 )) || { echo "${z:t}: licenses/LICENSE* $l, THIRD_PARTY_NOTICES.txt $t"; bad=1; }; done
  (( bad )) || echo "LICENSES PASS"; return $bad; }
chk g024_licenses 'LICENSES PASS' $WT lic
chk g024_notices 'notices up to date' $LD env MP_REPO=$WT CM_REPO=$CMR LLVM_MINGW=$HOME/tools/llvm-mingw NSIS_SRC=$HOME/tools/nsis_build/nsis-3.11 python3 licenses/make_notices.py --check
# ---- network (netrev2)
i18ntool() {   # every table of the three repos: 0 missing, 0 stale, 0 bad (i18n_tool.py check; exit 1 on a bad entry)
  local rc=0 n=0 l
  for d in $LD/loader/lang $WT/lang $CMR/lang; do
    l=$(python3 $LD/tools/i18n_tool.py check $d 2>&1) || rc=1; print -r -- "$d"; print -r -- "$l"
    n=$(( n + $(print -r -- "$l" | command grep -cE ': [0-9]+/[0-9]+ done, 0 missing, 0 stale, 0 bad') ))
  done
  (( rc == 0 && n == 27 )) && echo "I18N TABLES PASS 27/27"; }
chk g024_i18n_tool 'I18N TABLES PASS 27/27' $WT i18ntool
chk g024_net_test 'ALL PASS: 0 failure' $WT make -C src/net test
chk g024_net_asan 'ALL PASS: 0 failure' $WT zsh -c 'make -C src/net asan 2>&1 | tee /dev/stderr | grep -q "ERROR: AddressSanitizer" && exit 1; exit 0'
netone() { make -C src/net all >/dev/null || return 1; local t rc=0; for t in full_big hello_max fe_stale; do src/net/build/test_dsnet --only $t || rc=1; done; return $rc; }
chk g024_net_one 'PASS fe_stale: .*equal' $WT netone
# ---- builder (required references) + installer lock (harness)
refs() { local C=$OUT/g024_cargo_raw.txt; (cd $LD && DS_REQUIRE_REFS=1 nice -n 19 cargo test --release -p ds-unit-builder --no-fail-fast) > $C 2>&1
  command grep -E '^REF |test result|FAILED|panicked' $C
  (( $(command grep -c '^REF RAN' $C) == 4 )) && ! command grep -qE '^REF (SKIP|FAIL)|FAILED|panicked' $C && command grep -q 'test result: ok' $C && echo "REFS PASS"; }
chk g024_builder_refs 'REFS PASS' $LD refs
chk g024_installer_lock 'installer_lock: 0 failure\(s\)' $LD env DSML_BEPINEX_ZIP=$(print $PKGZ/bepinex-mac-arm64-*.zip(N[-1])) DSML_MELON_ZIP=$(print $PKGZ/melonloader-mac-arm64-*.zip(N[-1])) zsh app/test/installer_lock.sh
# ---- Mac app and loader copies
chk g024_app_test '^== 0 failure' $LD zsh app/test/run.sh
chk g024_dmg_start 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $(print $REL/*.dmg) $OUT/g024_dmg_start
chk g024_mod_zip 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $(print $REL/*.dmg) $OUT/g024_mod_zip $(print $REL/gcd-cdr-*.zip(N[1]))
hell() {   # Hell on (copy Z): 20/20 patches + table half loaded from the delta, staff strip on Hell only (crossmods)
  python3 $WT/ext/crossmods13/mac_mods_test.py $LD/loader --game-s 120 || return 1
  local L=$HOME/ds_launcher/loader/test_env/instances/Z/dsload.log   # test copies live in ~/ds_launcher (lrig.py / make_instance.sh)
  local a=$(command grep -c "Hell tables: the game loads" $L) b=$(command grep -c "no delta" $L); echo "Hell tables lines $a, no-delta lines $b"
  (( a >= 1 && b == 0 )) && echo "HELL TABLES PASS"; }
chk g024_hell 'HELL TABLES PASS' $WT hell
chk g024_notch '(^| )[4-9] PASS 0 FAIL' $LD/loader python3 test/notch_test.py build/mods_all/mac
nthrow() { local B=$OUT/g024_notch_throw_bin; clang -fobjc-arc -framework AppKit -I$LD/loader/src $LD/loader/test/notch_throw_test.m $LD/loader/start/mac/dsl_notch_mac.m -o $B || return 1; $B; }
chk g024_notch_throw '^PASS window borderless' $WT nthrow
chk g024_l1_world '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_world_test.py $OUT/g024_l1_world
chk g024_l1_soft '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_test.py $OUT/g024_l1_soft
chk g024_l1_pkg '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_pkg_test.py $OUT/g024_l1_pkg
chk g024_progress_strip '8/8 checks, violations 0' $LD/loader env DESIGN=strip python3 $LR l1_progress_test.py $OUT/g024_progress_strip
chk g024_progress_ornament '8/8 checks, violations 0' $LD/loader env DESIGN=ornament python3 $LR l1_progress_test.py $OUT/g024_progress_ornament
chk g024_screen '6/6 checks, violations 0' $LD/loader python3 $LR l1_screen_test.py $OUT/g024_screen
# ---- i18n in the game (Mac, hidden copies; test builds = this release + the local test commands: ~/ds_ld_ship024t,
#      ~/ds_mp_ship024t (build/lang = mp + cm tables), ~/ds_cm_ship024t; never shipped)
IG=${0:A:h}/i18n
i18ncycle() { local O=$HOME/ds_mp_ship024/ext/gate024/i18n/out/cycle_ja; [[ -d $O ]] && { mkdir -p $O:h/old; mv $O $O:h/old/cycle_ja.$(date +%H%M%S); }
  (cd $IG && python3 leased.py python3 cycle.py 7 0,1,2,3,4,5,6,8,9) > $OUT/g024_i18n_cycle_raw.log 2>&1; tail -5 $OUT/g024_i18n_cycle_raw.log
  python3 $IG/i18n_eval.py $O; }
chk g024_i18n_cycle 'RESULT 4/4 pass' $WT i18ncycle
i18npair() { local O=$HOME/ds_mp_ship024/ext/gate024/i18n/out/pair2_ja_ru; [[ -d $O ]] && { mkdir -p $O:h/old; mv $O $O:h/old/pair2_ja_ru.$(date +%H%M%S); }
  (cd $IG && python3 pairtest2.py 7 4) > $OUT/g024_i18n_pair_raw.log 2>&1; tail -3 $OUT/g024_i18n_pair_raw.log
  python3 $IG/i18n_eval.py $O --pair ja ru; }
chk g024_i18n_pair 'RESULT 4/4 pass' $WT i18npair
chk g024_fastboot_ui '([0-9]+) PASS 0 FAIL' $LD/loader python3 test/soft/fastboot_ui_test.py $OUT/g024_fastboot_ui   # boot ~3 s, Unity splash at the stock place, switch: after the boot, no tooltip, click flips
# ---- saves (cloud backup (import tests gone with Import a save, v0.1.13: 17 unit, 6 game checks), safety, save warning: fake cloud and test copy K only)
chk g024_cloud_unit '== 21 ok, 0 failed' $LD/loader zsh test/cloud/run.sh
chk g024_cloud '6/6 checks, violations 0' $LD/loader python3 $LR l1_cloud_test.py $OUT/g024_cloud
chk g024_safety '7/7 checks, violations 0' $LD/loader python3 $LR l1_safety_test.py $OUT/g024_safety
chk g024_savewarn '7/7 checks, violations 0' $LD/loader python3 $LR l1_savewarn_test.py $OUT/g024_savewarn
chk g024_saves '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_saves_test.py $OUT/g024_saves   # game saves played in place: game file gets the progress, modded-only saves apart, Hell asks, delete asks (v0.1.13)
chk g024_hidden_unit ' 0 failed$' $LD/loader zsh test/saves_hidden/run.sh   # hidden-worlds count (host test, temp folders)
chk g024_hidden '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_hidden_test.py $OUT/g024_hidden   # worlds the Load list hides get a line (hidden, not lost)
# ---- crash reports (ask first; scrubbed reports to the owner's private receiver)
chk g024_crash_scrub '== crash report scrub tests all ok' $LD/loader zsh test/crashrep/run.sh   # no user / full / computer name or home path in a report
chk g024_crash_rig 'ALL PASS' $LD/loader env CRASHREP_MP_MODULE=$LD/loader/build/mods/multiplayer python3 $LR crashrep/crash_rig_test.py $OUT/g024_crash_rig   # consent first, crash -> report sent, read back scrubbed
# ---- auto-update with local test releases (packaging)
chk g024_update_mac 'RESULT 24/24 pass' $LD zsh loader/test/update/mac_gate.sh $OUT/g024_update_mac $HOME/ds_launcher/loader/test_env/instances/Z/DungeonSettlers.app
# ---- multiplayer in the rig
rigstep() {   # smoke | mp2 on a pair leased as ship018-<pid> (waits up to 8 min), the dylib pinned by rig/pin_run.py
  local R=$HOME/ds_multiplayer/rig N=ship018-$$ P= i rc L=$OUT/g024_$1_raw.log
  for i in {1..48}; do P=$(python3 $R/rig.py lease $N --pid $$ 2>/dev/null | tail -1); [[ $P == [A-J][A-J] ]] && break; P=; sleep 10; done
  [[ -n $P ]] || { echo "no free pair after 8 min"; return 2; }
  if [[ $1 == smoke ]]; then (cd $R && RIG_PAIR=$P MP2_PAIR=$P python3 pin_run.py $D rig.py smoke) > $L 2>&1; rc=$?
    echo "pair $P smoke $(command grep -c "fp_tick_advanced': True" $L)/3"
  else (cd $R && RIG_PAIR=$P MP2_PAIR=$P python3 pin_run.py $D mp2_test.py --ticks 3000) > $L 2>&1; rc=$?
    echo "pair $P mp2 $(command grep -c ' PASS ' $L) PASS, $(command grep -c ' FAIL ' $L) FAIL, tb $(command grep -c Traceback $L)"; command grep ' FAIL ' $L | cut -c1-200; fi
  python3 $R/rig.py release $P $N >/dev/null; echo "raw log $L; prefs $(cd $R && python3 rig.py check-prefs)"; return $rc; }
chk g024_smoke 'smoke 3/3' $WT rigstep smoke
chk g024_mp2 ' 19 PASS, 0 FAIL, tb 0' $WT rigstep mp2
fullcheck() { local L=$OUT/g024_fastjoin_${1:-off}_raw.log; (cd $WT/ext/latejoin && python3 fastjoin_pair.py --mode ${1:-off} --dylib $D --full-every 300) > $L 2>&1; local rc=$?; cat $L
  command grep -qE 'PASS full-state checks ran, none failed ok [1-9][0-9]* fails 0' $L && command grep -q 'PASS no desync, no full / deep difference' $L && echo "FULLCHECK PASS" && return $rc; return 1; }
chk g024_fullcheck 'FULLCHECK PASS' $WT fullcheck off
chk g024_fullcheck_on 'FULLCHECK PASS' $WT fullcheck on   # Fast join On is the default since 2164719
chk g024_mixed_set 'RESULT 4/4 pass' $WT python3 $H/mixed_set.py
fogpair() { local L=$OUT/g024_fogfix_raw.log; (cd $WT/ext/fogfix && python3 fogfix_pair.py --dylib $D --cycles 3) > $L 2>&1; local rc=$?; command grep -E 'RESULT|FAIL' $L | tail -5; return $rc; }
chk g024_fogfix 'RESULT PASS' $WT fogpair   # v0.1.14 hotfix crash path: 3 dungeon entries with a watcher, fog redraws, quit to title
tbpair() { local L=$OUT/g024_takeback_raw.log; (cd $WT/ext/takeback && python3 takeback_pair.py --dylib $D) > $L 2>&1; local rc=$?; command grep -E 'RESULT|FAIL' $L | tail -5; return $rc; }
chk g024_takeback 'RESULT PASS' $WT tbpair   # taking back a lent unit off the current map names the unit (4-player test)
chk g024_hostmods 'RESULT PASS' $LD/loader python3 test/hostmods_pair.py --settings --play 120   # "Get the host's mods": refused guest downloads the host's official mod + setting, rejoins, 0 desyncs
print "g024_inputs_unchanged $([[ $( { echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"; for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "g024_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
