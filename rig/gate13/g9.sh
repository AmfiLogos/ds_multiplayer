#!/bin/zsh
# g9.sh - G9 our mods on Mac + G8 Apply timing. mac_mods_test (copy Z, mods_all/mac: Hell 20/20 patches + tables, staff
# strip on difficulty 5 only, gcd lines with CDR), gcd_world_test (copy L: stock vs gcd-cdr global cooldown measured,
# world with another mod set refused), soft-restart Apply time cold and prewarmed (copy V, v13_test.py).
source ${0:A:h}/lib.sh
gcd() {   # gcd_world_test + verdict: both runs measured, mod cooldown shorter than stock, refused world line, prefs
  (cd $LD/loader && zsh test/install_mods.sh test_env/instances/L/DungeonSettlers.app test/profile_game.json) || return 1
  (cd $LD/loader && GAME_S=150 python3 test/gcd_world_test.py both) | tee $OUT/g9_gcd_raw.txt
  python3 - $OUT/g9_gcd_raw.txt <<'PY'
import json, sys
t = open(sys.argv[1]).read().splitlines(); r = {}
for l in t:
    if l.startswith('RESULT '): k, j = l[7:].split(' ', 1); r[k] = json.loads(j)
ms = {k: v.get('measured') for k, v in r.items()}
ok = bool(ms.get('stock')) and bool(ms.get('mod')) and any(l.startswith('WORLD ') for l in t) \
     and 'owner prefs identical after: True' in t
print('GCD', 'PASS' if ok else 'FAIL', json.dumps(ms)[:300]); sys.exit(0 if ok else 1)
PY
}
chk g9_mac_mods '(^| )8 PASS 0 FAIL' $WT python3 ext/crossmods13/mac_mods_test.py $LD/loader --game-s 120
chk g9_gcd_world 'GCD PASS' $WT gcd
chk g8_apply_cold 'B on screen [0-9.]+ s after the request' $LD/loader zsh -c "zsh test/install_mods.sh test_env/instances/V/DungeonSettlers.app test/profile_game.json && python3 $WT/rig/gate13/v13t_run.py cold 3 $OUT/g8_cold"
chk g8_apply_prewarm 'B on screen [0-9.]+ s after the request' $LD/loader python3 $WT/rig/gate13/v13t_run.py prewarm 3 $OUT/g8_prewarm
print "g9_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
