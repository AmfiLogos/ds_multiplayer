#!/bin/zsh
# Units made by the Unit Builder are usable in Test Fights: the wiki's save generator (Test Fights' engine, custom/fights
# sg_cli = sg_js.c in QuickJS) takes them as a squad (4 per fight: boss golem / sentinel, DPS check) with 0
# problems, and node/V8 (custom/fights/test/ref_node.js) writes the byte-same files.
# Builds: the units saved in the game test (library_after.json of the newest builder_test run) + the equality test's
# builds (build_bld/eq, run_eq.sh first).
# usage: ext/builder13/test/tf_check.sh [groups]
set -e
H=${0:A:h}; R=${H:h:h:h}; B=$R/build_bld; CM=${CM_REPO:-$HOME/ds_custommodes}; F=$CM/fights; SITE=$HOME/dungeonsettlersdb/site
G=${1:-24}
# sg_cli from Test Fights' engine files (custom/fights/build.sh also builds the game windows, which need more include
# folders than it passes on this tree; only the engine is needed here)
CF=(-arch arm64 -mmacosx-version-min=12.0 -D_DARWIN_C_SOURCE -O2 -w -fwrapv -I$CM -I$CM/fe -I$F)
mkdir -p $B/tfobj; OBJS=()
for s in qjs_quickjs qjs_cutils qjs_dtoa qjs_regexp qjs_unicode tinf/tinflate sg_embed sg_js; do
  o=$B/tfobj/${s:t}.o; OBJS+=($o); [[ $o -nt $F/$s.c ]] || nice -n 19 clang $CF -c $F/$s.c -o $o
done
nice -n 19 clang $CF -o $B/sg_cli $F/test/sg_cli.c $OBJS $CM/fe/fe_json.c -lm
O=$B/tf; [[ -d $O ]] && mkdir -p $B/old && mv $O $B/old/tf_$(date +%s); mkdir -p $O/ref $O/qjs
LIBF=$(ls -td $R/ext/builder13/out/builder_2*/library_after.json(N) | head -1)
python3 - "$LIBF" $B/eq $G $O <<'PY'
import json, os, sys
libf, eq, g, o = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
lib = [u['build'] for u in json.load(open(libf))['units']] if libf and os.path.exists(libf) else []
eqb = [json.load(open(os.path.join(eq, f)))['build'] for f in sorted(os.listdir(eq)) if f.startswith('b')]
modes = [{'mode': 'boss', 'boss': 'golem', 'combat': 'Normal'}, {'mode': 'dps', 'combat': 'Hard'},
         {'mode': 'boss', 'boss': 'sentinel', 'combat': 'Easy'}, {'mode': 'boss', 'boss': 'golem', 'combat': 'Hard'}]
cases = []
if lib:
    cases.append({'id': 'game_saved_units', 'builds': 'lib', 'opts': dict(modes[0], kits={'basic': True}, team='Builder Test')})
for i in range(g):
    idx = ','.join(str(4 * i + k) for k in range(4))
    cases.append({'id': f'eq{i:03d}', 'builds': f'eq:{idx}', 'opts': dict(modes[i % 4], kits={'basic': True}, team=f'Team {i}')})
json.dump(cases, open(os.path.join(o, 'cases.json'), 'w'), indent=1)
json.dump({'lib': lib[:4], 'eq': eqb}, open(os.path.join(o, 'builds.json'), 'w'))
print('cases', len(cases), 'game-saved units', len(lib))
PY
export TZ=UTC DET_SEED=12345 DET_NOW=1760000000000 DET_JS=$F/test/det_prelude.js
$B/sg_cli $SITE/builder $SITE/assets/saves $O/qjs $O/cases.json $O/builds.json >/dev/null
node $F/test/ref_node.js $SITE/builder $SITE/assets/saves $O/ref $O/cases.json $O/builds.json >/dev/null
same=0; diff=0
for f in $O/ref/*; do n=${f:t}; [[ $n == timing.txt ]] && continue; if cmp -s $f $O/qjs/$n; then same=$((same+1)); else diff=$((diff+1)); echo "DIFFERENT $n"; fi; done
sv=($O/qjs/*.save.json(N)); er=($O/qjs/*.error.txt(N)); saves=${#sv}; errs=${#er}
probs=$(python3 -c "import json,glob;print(sum(len(json.load(open(f)).get('problems',[])) for f in glob.glob('$O/qjs/*.meta.json')))")
echo "Test Fights with Unit Builder units: $saves saves, $errs errors, $probs problems; QuickJS == node files: $same same, $diff different"
for f in $O/qjs/*.error.txt(N); do echo "${f:t}: $(head -c 300 $f)"; done
[[ $errs == 0 && $probs == 0 && $diff == 0 ]]
