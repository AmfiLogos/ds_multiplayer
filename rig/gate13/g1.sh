#!/bin/zsh
# g1.sh - G1 unit suites on the frozen worktree (no pair): MP units (net, mp_test, mpmenu, trn, ui, teams, pvp, FE guard),
# ord_test, mu_test, bridge unit, fights run_eq 64/64 (blobs = merger13's fixed copy), sg_cache, gal, builder 300/300,
# loader host tests, app tests (dry run, default mod profile), build_mods.sh --check.
source ${0:A:h}/lib.sh
U=$HOME/ds_multiplayer/ext/merger13/tools/unit.sh
units() { local o=$(zsh $U $WT $OUT/g1_units); echo $o; [[ $(echo $o | command grep -c 'rc=0') == 8 ]] && echo "UNITS 8/8 rc=0"; }
chk g1_mp_units 'UNITS 8/8 rc=0' $WT units
chk g1_ord 'OK: [0-9]+ checks, 0 failures' $WT zsh src/ord_test/run_tests.sh
chk g1_mu '148 PASS, 0 FAIL' $WT make -C $CM/units/test test
chk g1_bridge_unit 'unit: all PASS' $WT env BRIDGE_SRC=$WT/src zsh $HOME/ds_multiplayer/ext/bepinex_bridge/tests/run_unit.sh
[[ -f $WT/build_c4/blobs.json ]] || { mkdir -p $WT/build_c4; cp -p $HOME/ds_mp_merge13/build_c4/blobs.json $WT/build_c4/; }
chk g1_fights_build 'built:' $WT zsh $CM/fights/build.sh
chk g1_run_eq 'files identical: 64, different/missing: 0' $WT zsh $CM/fights/test/run_eq.sh
mkdir -p $OUT/g1_sg
chk g1_sg_cache '24 checks, 0 failed' $WT build_c4/sg_cache_test $OUT/g1_sg $HOME/dungeonsettlersdb/site
chk g1_gal '28 checks, 0 failed' $WT build_c4/gal_test $CM/gallery/test/data
chk g1_builder_eq '300 builds, 300 equal, 0 different' $WT zsh -c 'zsh $CM/builder/build.sh && zsh $CM/builder/test/run_eq.sh 300 13'
chk g1_ld_host '31 passed, 0 failed' $LD/loader zsh test/run_tests.sh
chk g1_app_test '^== 0 failure' $LD zsh app/test/run.sh
chk g12_mods_check '4 modules .*: OK' $LD/loader/modules zsh build_mods.sh --check
