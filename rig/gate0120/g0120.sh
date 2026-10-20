#!/bin/zsh
# g0120.sh - the v0.1.20 Mac gate: copy of gate0119/g0119.sh + the v0.1.20 tests (section at the end): Units Laboratory
# split (PvP + Foreign Expedition without Units Laboratory, ulnoblock, pvp unit test), conninfo (conn_via, conninfo_pair),
# upnpport (host lobby, DSRIG_UPNP off = no router mapping), joinpopup (join_window, popup_join --hold). Steam cross-play = VM lane.
# (g0119 header) the v0.1.19 Mac gate: copy of gate017/g017.sh (v0.1.17) + the v0.1.19 tests: mpmenu / extaddr unit tests,
# Remove File question (l1_savedel_test), no input during the soft restart (soft_input_test), number field + Host port
# setting (numfield_test, hostport_pair), lobby internet address (extaddr_lobby), Steam friends picker without Steam
# (picker_test), new-campaign Cancel / Leave / tutorial block (foc_proof). Windows = the VM lane (WIN_GREEN.txt).
# Mac only (owner 2026-10-18): the Windows side is the real Windows VM lane (WIN_GREEN.txt); Wine is dropped completely
# (owner 2026-10-18 evening): no Wine step in any gate; the old Wine script is in rig/old_versions/
# (g016 header) the v0.1.16 gate (signed updates + release keys, Windows needs Steam, ModData + join sync, hook list,
# mp_api v2 + DSMP Bridge, Harmony chaining on Windows, popup join fix, stricter test-only control channel). Copy of
# g024.sh (v0.1.15) + check_keys on the kit, signed-update / moddata / sock / mp_api unit tests, the moddata and popup-join
# pair tests. i18n game tests on the test trees ~/ds_*_ship024t moved onto the int017 heads (rebuild_testtrees.sh).
# g018 notes: the gate's tools from rig/gate13; the wine lock taken by the gate only around the Windows steps (owner
# ship018-gate, GATE_WINE_OWNER); rig pairs leased under a unique name (ship018-<pid>) with the dylib pinned (pin_run.py).
# Env: WT = MP checkout (its build/ = the rig dylibs), LD = loader checkout (its build/ = the shipped files),
#      CMR = Custom Modes checkout, REL = artifact folder (.dmg, Setup.exe, Windows kit zip, mod zips), PKGZ = folder with
#      the BepInEx / MelonLoader release zips the build pins, GATE_DIR = output folder.
H=${0:A:h:h}/gate13; source $H/lib.sh
REL=${REL:-$HOME/ds_rel020/release_v020}; CMR=${CMR:-$HOME/ds_custommodes}; PKGZ=${PKGZ:-$HOME/ds_int017/pkgzips}
export GATE_WINE_OWNER=ship018-gate; WL=$HOME/ds_multiplayer/rig/wine_lease.py
[[ -z ${GATE_ONLY:-} && -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }   # a GATE_ONLY rerun appends to the same summary
if [[ -z ${GATE_ONLY:-} ]]; then
{ echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"
  for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } > $OUT/inputs.txt
IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v0.1.20 gate $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
else
  IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64); print "rerun $(date '+%F %T') GATE_ONLY=$GATE_ONLY" >> $SUM
fi
trap "verdict ${0:A}" EXIT
# owner rule (2026-10-20): never bind TCP 47777 while the owner's game runs (menu-hosted test copies listen there):
# every check waits while his launcher / game runs (launcher by name, his app's path, the launcher's own game copy under
# DSModLauncher/games/ (binary "Dungeon Settlers" with a space: missed until 2026-10-20 22:35), any game outside the test instances folders)
owner_wait() { local n=0; while pgrep -x DSModLauncher >/dev/null || pgrep -f "^($HOME)?/Applications/Dungeon Settlers Modded.app/" >/dev/null || pgrep -f 'DSModLauncher/games/' >/dev/null || pgrep -fl 'MacOS/(DungeonSettlers|Dungeon Settlers)( |$)' | command grep -vE '/instances/|^[0-9]+ /private/tmp/|^[0-9]+ /tmp/' | command grep -q .; do
  (( n++ == 0 )) && print "$(date '+%T') owner's game runs: next check waits (no TCP 47777 bind)" | tee -a $SUM; sleep 30; done; }
functions[chk_lib]=${functions[chk]}
chk() { owner_wait; chk_lib "$@"; }
# held wine lock (Windows section): python tests via held_lock_run.py, wine_lease.py users (shell tests) on a private lock
WH=($H/held_lock_run.py); WE=(WINE_LEASE_LOCK=$HOME/ds_multiplayer/rig/wine.lock.gate13-inner)
LR=$H/ld_run.py
chk g020_host_tests '== ([0-9]+) passed, 0 failed' $LD zsh loader/test/run_tests.sh
chk g020_mpmenu_unit 'mpmenu_test: all passed' $WT make -C src/mpmenu_test test
chk g020_extaddr_unit '== [0-9]+ passed, 0 failed' $WT make -C src/extaddr_test test
# ---- release files: names, bytes, saves, licenses
scan() { local M=$(mktemp -d); hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/*.dmg >/dev/null || return 1
  python3 $H/banscan.py $M $REL/* $LD/loader/build/win/kit; local rc=$?; hdiutil detach $M -quiet; (( rc == 0 )) && echo "SCAN ALL PASS"; return $rc; }
chk g020_scan 'SCAN ALL PASS' $WT scan
modrepos() {   # every module the build ships is an official mod (MOD_REPOS: auto-update, "Get the host's mods"); v0.1.15 shipped zetu-suite outside it at first
  local bad=0 id; for d in $LD/loader/build/mods_all/mac/*(/); do id=${d:t}
    command grep -qE "\\{ \"$id\", \"AmfiLogos/ds_[a-z]+\" \\}" $LD/loader/src/dsl_update.c && echo "ok $id" || { echo "MISSING $id"; bad=1; }; done
  (( bad )) || echo "MOD REPOS PASS"; return $bad; }
chk g020_mod_repos 'MOD REPOS PASS' $WT modrepos
chk g020_check_keys 'ok +Dungeon-Settlers-Modded-Setup.exe' $LD python3 tools/release_sign/check_keys.py $REL/Dungeon-Settlers-Modded-Mac.dmg $REL/Dungeon-Settlers-Modded-Windows.zip $REL/Dungeon-Settlers-Modded-Setup.exe   # every kit loader holds both release keys, no dev key
chk g020_signed '== ([0-9]+) passed, 0 failed' $LD zsh loader/test/update/signed_test.sh   # updates refused without a valid release signature
chk g020_moddata_unit 'moddata: all cases pass' $LD zsh loader/test/moddata/run.sh
chk g020_sock 'RESULT PASS \(0 failed\)' $WT zsh src/sock_test/run.sh   # test control channel: HTTP lines closed, token first
chk g020_mp_api 'PASS: [0-9]+ checks, 0 failed' $WT make -C src/mp_api_test test
chk g020_ctl_off 'CTL OFF PASS' $WT python3 rig/gate13/ctl_off_check.py --loader-build $LD/loader/build   # a player start (no DSRIG_CTL / DSRIG_PORT) opens no test control channel (Mac: no ctl.sock; Windows DLLs: no default port)
chk g020_mods_check 'modules .*: OK' $LD env MP_REPO=$HOME/ds_multiplayer CM_REPO=$CMR DSMP_ROOT=$HOME/ds_multiplayer zsh loader/modules/build_mods.sh --check   # every built module complete, Mac + Windows
chk g020_rv_gate 'rv gate: ok' $WT python3 tools/rv_gate.py   # every game address goes through rv() (per-game-version tables)
chk g020_cargo_core 'test result: ok. [1-9][0-9]* passed; 0 failed' $LD nice -n 19 cargo test -p dsml-core
chk g020_kitonly '== ([0-9]+) passed, 0 failed' $LD zsh loader/test/update/kitonly_test.sh   # unknown game build: mods off, the kit update still found
chk g020_getmods '== ([0-9]+) passed, 0 failed' $LD zsh loader/test/update/getmods_test.sh   # "Get the host's mods": official releases only, sha256
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
# ---- v0.1.17 additions: Mac Steam check (steamcheck), hook list, Steam gate on the release files, featsplit, Hell new game
chk g020_steam_check '^0 failure' $LD zsh app/test/steam/steam_check.sh   # a running but silent Steam is never called "not running"
chk g020_hooklist 'hooklist check: ok' $LD python3 tools/hooks/hooklist.py check
sgate() { local T=$(mktemp -d) rc=0; mkdir -p $T/zip $T/setup; unzip -q $REL/Dungeon-Settlers-Modded-Windows.zip -d $T/zip || return 1
  $HOME/tools/7zip/7zz x -y -o$T/setup $REL/Dungeon-Settlers-Modded-Setup.exe >/dev/null || return 1
  python3 $LD/loader/start/win/steam_gate.py $T/zip || rc=1; python3 $LD/loader/start/win/steam_gate.py $T/setup || rc=1
  (( rc == 0 )) && echo "STEAM GATE RELEASE PASS"; return $rc; }
chk g020_steam_gate 'STEAM GATE RELEASE PASS' $WT sgate   # no Steam-off path in the files players download
kitmods() {   # the Windows kit ships the same mod set as the Mac app (v0.1.16 decision; dsmp-bridge in the kit = BepInEx first start for every player)
  local M=$(mktemp -d) a w; hdiutil attach -nobrowse -readonly -noautoopen -mountpoint $M $REL/*.dmg >/dev/null || return 1
  a=$(find "$M/Dungeon Settlers Modded.app" -name mod.json | sed -E 's#.*/([^/]+)/mod.json#\1#' | sort -u | tr '\n' ' '); hdiutil detach $M -quiet
  w=$(unzip -Z1 $REL/Dungeon-Settlers-Modded-Windows.zip | command grep -oE 'mods/[^/]+/' | sed -E 's#mods/([^/]+)/#\1#' | sort -u | tr '\n' ' ')
  echo "mac: $a"; echo "win: $w"; [[ -n $a && $a == $w ]] && echo "KIT MODS SAME"; }
chk g020_kit_mods 'KIT MODS SAME' $WT kitmods
chk g020_featsplit '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR featsplit_test.py $OUT/g020_featsplit   # a launcher feature never refuses a world
chk g020_hell_newgame 'RESULT PASS' $LD/loader python3 $LR hell_newgame_test.py $OUT/g020_hell_newgame   # new game on combat Hell reaches the world
chk g020_dmg_start 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $(print $REL/*.dmg) $OUT/g020_dmg_start
chk g020_mod_zip 'RESULT ([0-9]+)/\1 pass' $WT python3 $H/dmg_start.py $(print $REL/*.dmg) $OUT/g020_mod_zip $(print $REL/gcd-cdr-*.zip(N[1]))
hell() {   # Hell on (copy Z): 20/20 patches + table half loaded from the delta, staff strip on Hell only (crossmods)
  python3 $WT/ext/crossmods13/mac_mods_test.py $LD/loader --game-s 120 || return 1
  local L=$HOME/ds_launcher/loader/test_env/instances/Z/dsload.log   # test copies live in ~/ds_launcher (lrig.py / make_instance.sh)
  local a=$(command grep -c "Hell tables: the game loads" $L) b=$(command grep -c "no delta" $L); echo "Hell tables lines $a, no-delta lines $b"
  (( a >= 1 && b == 0 )) && echo "HELL TABLES PASS"; }
chk g020_hell 'HELL TABLES PASS' $WT hell
chk g020_notch '(^| )[4-9] PASS 0 FAIL' $LD/loader python3 test/notch_test.py build/mods_all/mac
nthrow() { local B=$OUT/g020_notch_throw_bin; clang -fobjc-arc -framework AppKit -I$LD/loader/src $LD/loader/test/notch_throw_test.m $LD/loader/start/mac/dsl_notch_mac.m -o $B || return 1; $B; }
chk g020_notch_throw '^PASS window borderless' $WT nthrow
chk g020_l1_world '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_world_test.py $OUT/g020_l1_world
chk g020_l1_soft '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_test.py $OUT/g020_l1_soft
chk g020_l1_pkg '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_pkg_test.py $OUT/g020_l1_pkg
chk g020_progress_strip '8/8 checks, violations 0' $LD/loader env DESIGN=strip python3 $LR l1_progress_test.py $OUT/g020_progress_strip
chk g020_progress_ornament '8/8 checks, violations 0' $LD/loader env DESIGN=ornament python3 $LR l1_progress_test.py $OUT/g020_progress_ornament
chk g020_screen '6/6 checks, violations 0' $LD/loader python3 $LR l1_screen_test.py $OUT/g020_screen
# ---- i18n in the game (Mac, hidden copies; test builds = this release + the local test commands: ~/ds_ld_ship024t,
#      ~/ds_mp_ship024t (build/lang = mp + cm tables), ~/ds_cm_ship024t; never shipped)
IG=${0:A:h:h}/gate016/i18n
i18ncycle() { local O=$HOME/ds_int017/i18n_out/cycle_ja; [[ -d $O ]] && { mkdir -p $O:h/old; mv $O $O:h/old/cycle_ja.$(date +%H%M%S); }
  (cd $IG && python3 leased.py python3 cycle.py 7 0,1,2,3,4,5,6,8,9) > $OUT/g020_i18n_cycle_raw.log 2>&1; tail -5 $OUT/g020_i18n_cycle_raw.log
  python3 $IG/i18n_eval.py $O; }
chk g020_i18n_cycle 'RESULT 4/4 pass' $WT i18ncycle
i18npair() { local O=$HOME/ds_int017/i18n_out/pair2_ja_ru; [[ -d $O ]] && { mkdir -p $O:h/old; mv $O $O:h/old/pair2_ja_ru.$(date +%H%M%S); }
  (cd $IG && python3 pairtest2.py 7 4) > $OUT/g020_i18n_pair_raw.log 2>&1; tail -3 $OUT/g020_i18n_pair_raw.log
  python3 $IG/i18n_eval.py $O --pair ja ru; }
chk g020_i18n_pair 'RESULT 4/4 pass' $WT i18npair
chk g020_fastboot_ui '([0-9]+) PASS 0 FAIL' $LD/loader python3 test/soft/fastboot_ui_test.py $OUT/g020_fastboot_ui   # boot ~3 s, Unity splash at the stock place, switch: after the boot, no tooltip, click flips
# ---- saves (cloud backup (import tests gone with Import a save, v0.1.13: 17 unit, 6 game checks), safety, save warning: fake cloud and test copy K only)
chk g020_cloud_unit '== 21 ok, 0 failed' $LD/loader zsh test/cloud/run.sh
chk g020_cloud '6/6 checks, violations 0' $LD/loader python3 $LR l1_cloud_test.py $OUT/g020_cloud
chk g020_safety '7/7 checks, violations 0' $LD/loader python3 $LR l1_safety_test.py $OUT/g020_safety
chk g020_savewarn '7/7 checks, violations 0' $LD/loader python3 $LR l1_savewarn_test.py $OUT/g020_savewarn
chk g020_saves '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_saves_test.py $OUT/g020_saves   # game saves played in place: game file gets the progress, modded-only saves apart, Hell asks, delete asks (v0.1.13)
chk g020_hidden_unit ' 0 failed$' $LD/loader zsh test/saves_hidden/run.sh   # hidden-worlds count (host test, temp folders)
chk g020_hidden '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_hidden_test.py $OUT/g020_hidden   # worlds the Load list hides get a line (hidden, not lost)
# ---- crash reports (ask first; scrubbed reports to the owner's private receiver)
chk g020_crash_scrub '== crash report scrub tests all ok' $LD/loader zsh test/crashrep/run.sh   # no user / full / computer name or home path in a report
chk g020_crash_rig 'ALL PASS' $LD/loader env CRASHREP_MP_MODULE=$LD/loader/build/mods/multiplayer python3 $LR crashrep/crash_rig_test.py $OUT/g020_crash_rig   # consent first, crash -> report sent, read back scrubbed
# ---- auto-update with local test releases (packaging)
chk g020_update_mac 'RESULT 24/24 pass' $LD zsh loader/test/update/mac_gate.sh $OUT/g020_update_mac $HOME/ds_launcher/loader/test_env/instances/Z/DungeonSettlers.app
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
fogpair() { local L=$OUT/g020_fogfix_raw.log; (cd $WT/ext/fogfix && python3 fogfix_pair.py --dylib $D --cycles 3) > $L 2>&1; local rc=$?; command grep -E 'RESULT|FAIL' $L | tail -5; return $rc; }
chk g020_fogfix 'RESULT PASS' $WT fogpair   # v0.1.14 hotfix crash path: 3 dungeon entries with a watcher, fog redraws, quit to title
tbpair() { local L=$OUT/g020_takeback_raw.log; (cd $WT/ext/takeback && python3 takeback_pair.py --dylib $D) > $L 2>&1; local rc=$?; command grep -E 'RESULT|FAIL' $L | tail -5; return $rc; }
chk g020_takeback 'RESULT PASS' $WT tbpair   # taking back a lent unit off the current map names the unit (4-player test)
hmpair() {   # signed updates (v0.1.16): the test's local release server signs with the dev key, so the guest's loader must trust it
  local HB=$OUT/hm_build; [[ -d $HB ]] && { mkdir -p $OUT/old; mv $HB $OUT/old/hm_build-$(date +%H%M%S); }
  (cd $LD && EXTRA_CFLAGS=-DDSL_SIGN_DEV_KEY LOADER_ONLY=1 zsh loader/build.sh) > $OUT/g020_hm_devbuild.log 2>&1 || return 1
  mkdir -p $HB && cp -p $LD/loader/build/dsload.dylib $HB/ && ditto $LD/loader/build/mods $HB/mods
  (cd $LD && LOADER_ONLY=1 zsh loader/build.sh) >> $OUT/g020_hm_devbuild.log 2>&1 || return 1       # the release loader back in loader/build
  (cd $LD/loader && HM_BUILD=$HB python3 test/hostmods_pair.py --settings --play 120); }
chk g020_hostmods 'RESULT PASS' $WT hmpair   # "Get the host's mods": refused guest downloads the host's official mod + setting, rejoins, 0 desyncs
chk g020_moddata_pair 'RESULT PASS' $LD/loader python3 test/moddata/moddata_pair.py   # mod data files synced at the join, the guest's own back after
popjoin() { local L=$OUT/g020_popup_raw.log; (cd $WT/rig && python3 pin_run.py $D popup_join_test.py --answer yes --play 60) > $L 2>&1; local rc=$?; command grep -E 'RESULT|FAIL' $L | tail -5; return $rc; }   # pinned: this release's dylib
chk g020_popup_join 'RESULT PASS' $WT popjoin   # a host with the Lock Consecration popup open on load lets guests in
# ---- v0.1.19 additions
chk g020_savedel '([0-9]+)/\1 checks, violations 0' $LD/loader python3 $LR l1_savedel_test.py $OUT/g020_savedel   # Remove File asks before deleting a game save, both lists
chk g020_soft_input 'RESULT PASS ([0-9]+)/\1' $LD/loader python3 $LR soft_input_test.py $OUT/g020_soft_input   # no click reaches the title during the soft restart
chk g020_numfield 'RESULT ALL PASS' $LD/loader env HP_BUILD=$LD/loader/build HP_PAIR_DIR=$WT/ext/hostport python3 test/settings/numfield_test.py $OUT/g020_numfield   # typed port 12345, refused values, host listens on it
chk g020_hostport 'RESULT ALL PASS' $WT env HP_BUILD=$LD/loader/build python3 ext/hostport/hostport_pair.py   # host_port 12345: listen, lobby line, join; mod-set hash unchanged
chk g020_extaddr 'RESULT PASS' $WT python3 ext/extaddr/extaddr_lobby.py --dylib $D --out $OUT/g020_extaddr   # lobby internet address: upnp / web / unknown
chk g020_picker 'RESULT PASS' $WT python3 ext/steaminv/picker_test.py --dylib $D --out $OUT/g020_picker   # Steam friends picker (fake list, no Steam)
chk g020_titlepos 'RESULT PASS ([0-9]+)/\1' $LD/loader python3 $LR l1_titlepos_test.py $OUT/g020_titlepos   # Mods button under Load Campaign / Multiplayer when mods are off
chk g020_steamaccept 'RESULT PASS' $WT env STEAMACCEPT_DYLIB=$D STEAMACCEPT_OUT=$OUT/g020_steamaccept_out python3 rig/steamaccept_test.py   # accepted Steam invites reach the multiplayer code (callback slot)
chk g020_reopen '([0-9]+)/\1 checks, summary' $LD/loader python3 $LR reopen_test.py $OUT/g020_reopen   # Mods window reopens after Apply (prewarm on/off), never at a later start
chk g020_foc 'RESULT ALL PASS' $WT env FOC_DYLIB=$D FOC_TAG=_g019 python3 ext/fixfoc/foc_proof.py   # new-campaign Cancel, host Leave, tutorial not hosted
# ---- v0.1.20 additions
chk g020_pvp_unit 'pvp_test: all [0-9]+ checks passed' $WT make -C src/pvp_test test   # PvP core + mod check (moved into multiplayer)
netnew() { make -C src/net all >/dev/null || return 1; local t rc=0; for t in conn_via join_window; do src/net/build/test_dsnet --only $t || rc=1; done; return $rc; }
chk g020_net_new 'PASS join_host_window' $WT netnew   # conninfo: via per player, address on the host only; joinpopup: guest waits while the host holds a window
noul() { local L=$OUT/g020_$1_raw.log; shift; (cd $WT && python3 rig/ul_side.py none "$@") > $L 2>&1; local rc=$?; command grep -E 'ul_side:|RESULT|[0-9]+/[0-9]+ PASS|FAIL' $L | tail -8; return $rc; }
chk g020_pvp_noul 'RESULT PASS' $WT noul pvp_noul ext/fepvp13/tests/pvp_v1_pair.py --dylib $D --tag g020_pvp_noul   # PvP match, neither copy has Units Laboratory
chk g020_fe_noul '^.*([0-9]+)/\1 PASS' $WT noul fe_noul ext/fepvp13/tests/hub_dive_test.py --dylib $D   # Foreign Expedition dive + loot home, no Units Laboratory
chk g020_ulnoblock 'RESULT PASS' $LD/loader python3 test/ulnoblock_pair.py   # Units Laboratory never blocks a join; gcd-cdr still refused
chk g020_conninfo 'RESULT PASS' $WT python3 ext/conninfo/conninfo_pair.py --dylib $D --out $OUT/g020_conninfo   # Players rows Steam / Direct, guest line Connected to ...
chk g020_upnpport 'RESULT PASS' $WT python3 ext/upnpport/upnpport_lobby.py --dylib $D --out $OUT/g020_upnpport   # router port lines (fake router; DSRIG_UPNP off by default)
popjoin2() { local L=$OUT/g020_popup_hold_raw.log; (cd $WT/rig && POPUP_OUT=$OUT/g020_popup_hold python3 pin_run.py $D popup_join_test.py --answer yes --hold 100 --play 60) > $L 2>&1; local rc=$?; command grep -E 'RESULT|FAIL' $L | tail -5; return $rc; }
chk g020_popup_hold 'RESULT PASS' $WT popjoin2   # host keeps the popup open 100 s while a guest joins: guest waits, joins after, 0 desyncs
chk g020_lonebring 'RESULT PASS' $WT python3 ext/rel020/lonehost_bring.py --dylib $D --tag g020_lonebring   # a lone host (no guest yet, no Units Laboratory) brings 2 units in the PvP lobby (bringcards fix)
chk g020_bringcards 'RESULT PASS' $LD/loader env HM_BUILD=$OUT/hm_build BC_MP=$WT python3 test/bringcards_pair.py   # Bring Units card window in PvP + FE (host alone first), 0 desyncs (needs hm_build of g020_hostmods)
chk g020_fe_testc 'ALL PASS' $WT/ext/foreign_expedition python3 tests/test_c.py   # FE results: loot on the ground comes home (feloot)
print "g020_inputs_unchanged $([[ $( { echo "MP $(git -C $WT rev-parse HEAD)  LD $(git -C $LD rev-parse HEAD)  CM $(git -C $CMR rev-parse HEAD)  pins $(head -1 $LD/loader/mp_pin.txt) $(head -1 $LD/loader/cm_pin.txt)  RELEASE $(cat $LD/RELEASE $LD/loader/RELEASE 2>/dev/null | head -1)"; for f in $REL/* $D $WT/build/custom-modes*.dylib; do print -r -- "$(shasum -a 256 "$f" | cut -c1-64)  $f"; done; } | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "g020_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
