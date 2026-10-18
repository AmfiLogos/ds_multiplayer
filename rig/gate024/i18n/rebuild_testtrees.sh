#!/bin/zsh
# rebuild_testtrees.sh <mp head> <loader head> <cm head>
# The i18n game-test trees (LOCAL TEST ONLY, never pushed, never on a named branch): the release heads + the
# "local game test only" commits (test hooks: mpm_alltexts, mpm_test_lang, test_set_lang, DSI_MISS_LOG, DSRIG_MISS_LOG).
#   MP_T  (default ~/ds_mp_ship024t, worktree of MP_REPO ~/ds_multiplayer)
#   LD_T  (default ~/ds_ld_ship024t, worktree of LD_REPO ~/ds_int013/ds_modloader)
#   CM_T  (default ~/ds_cm_ship024t, worktree of CM_REPO ~/ds_int013/ds_custommodes)
# A tree that exists: its test commits (the run of "local game test only:" commits on top of HEAD) are rebased onto
# the new head. A tree that does not exist: detached worktree at the new head + the test commits of the source tree
# (MP_SRC / LD_SRC / CM_SRC, default the 023t trees) by format-patch | am -3. Conflict: the rebase / am is aborted,
# the tree stays as it was, exit 1.
# Then the build (each step waits while the 1-minute load is above 40; nice -n 19):
#   MP_T/build: dsrig.dylib + custom-modes(-mp).dylib (src/build.sh, CM_REPO=CM_T) + lang/ (mp table, then cm table over it)
#   LD_T/loader/build: loader/build.sh with MP_PIN / CM_PIN = the MP_T / CM_T test heads (CM gets SDK = loader/sdk)
# Old outputs are moved aside (MP_T/build/old/<sha>-<time>, LD_T/loader/old_builds/build-<sha>-<time>), never deleted.
# No game copy is started. Env: SKIP_BUILD=1 = rebase only.
set -e
(( $# == 3 )) || { echo "usage: $0 <mp head> <loader head> <cm head>" >&2; exit 2; }
MP_REPO=${MP_REPO:-$HOME/ds_multiplayer}; LD_REPO=${LD_REPO:-$HOME/ds_int013/ds_modloader}; CM_REPO=${CM_REPO:-$HOME/ds_int013/ds_custommodes}
MP_T=${MP_T:-$HOME/ds_mp_ship024t}; LD_T=${LD_T:-$HOME/ds_ld_ship024t}; CM_T=${CM_T:-$HOME/ds_cm_ship024t}
MP_SRC=${MP_SRC:-$HOME/ds_mp_ship023t}; LD_SRC=${LD_SRC:-$HOME/ds_ld_ship023t}; CM_SRC=${CM_SRC:-$HOME/ds_cm_ship023t}
TAG='local game test only:'
ST=$(date +%Y%m%d-%H%M%S)

ntest() {   # number of consecutive test commits on top of HEAD in tree $1
  local n=0 s
  while s=$(git -C $1 log -1 --format=%s HEAD~$n 2>/dev/null) && [[ $s == "$TAG"* ]]; do n=$((n+1)); done
  echo $n
}

move_tree() {   # move_tree <repo> <tree> <new head> <source tree>
  local repo=$1 t=$2 new src=$4 n
  new=$(git -C $repo rev-parse --verify "$3^{commit}") || { echo "$3 not found in $repo" >&2; exit 1; }
  if [[ -d $t ]]; then
    [[ -z $(git -C $t branch --show-current) ]] || { echo "$t is on a named branch; stop" >&2; exit 1; }
    [[ -z $(git -C $t status --porcelain --untracked-files=no) ]] || { echo "$t has uncommitted changes; stop" >&2; exit 1; }
    n=$(ntest $t); (( n > 0 )) || { echo "$t: no '$TAG' commits on top; stop" >&2; exit 1; }
    if [[ $(git -C $t rev-parse HEAD~$n) == $new ]]; then echo "$t: already on $new"; return; fi
    git -C $t rebase --onto $new HEAD~$n >/dev/null 2>&1 || { git -C $t rebase --abort 2>/dev/null; echo "$t: rebase conflict onto $new (aborted, tree unchanged)" >&2; exit 1; }
  else
    n=$(ntest $src); (( n > 0 )) || { echo "$src: no '$TAG' commits to copy; stop" >&2; exit 1; }
    git -C $repo worktree add --detach $t $new >/dev/null
    git -C $src format-patch -$n HEAD --stdout | git -C $t am -3 >/dev/null 2>&1 || { git -C $t am --abort 2>/dev/null; echo "$t: am conflict (aborted; tree is at $new without the test commits)" >&2; exit 1; }
  fi
  echo "$t: $(git -C $t rev-parse HEAD) = ${new:0:7} + $(ntest $t) test commits"
}

loadwait() { while (( $(uptime | awk -F'load averages?: ' '{print int($2)}') > 40 )); do echo "load above 40, waiting"; sleep 30; done; }

move_tree $MP_REPO $MP_T $1 $MP_SRC
move_tree $LD_REPO $LD_T $2 $LD_SRC
move_tree $CM_REPO $CM_T $3 $CM_SRC
[[ -n $SKIP_BUILD ]] && exit 0
MPH=$(git -C $MP_T rev-parse HEAD); CMH=$(git -C $CM_T rev-parse HEAD)

# ---- MP test tree: standalone dsrig.dylib + Custom Modes + build/lang
B=$MP_T/build; mkdir -p $B $MP_T/win/math/build
O=$B/old/$ST; for f in dsrig.dylib custom-modes.dylib custom-modes-mp.dylib lang build_testtree.log; do
  [[ -e $B/$f ]] && { mkdir -p $O; mv $B/$f $O/; }; done
[[ -f $B/det_ranges.tsv ]] || cp -p $HOME/ds_multiplayer/build/det_ranges.tsv $B/
[[ -f $MP_T/win/math/build/libdsmath_arm64.a ]] || cp -p $HOME/ds_multiplayer/win/math/build/libdsmath_arm64.a $MP_T/win/math/build/
[[ -e $MP_T/win/25846389 ]] || ln -s $HOME/ds_multiplayer/win/25846389 $MP_T/win/25846389
loadwait
CM_REPO=$CM_T nice -n 19 zsh $MP_T/src/build.sh > $B/build_testtree.log 2>&1 || { tail -20 $B/build_testtree.log; echo "MP test build FAILED ($B/build_testtree.log)" >&2; exit 1; }
mkdir -p $B/lang
python3 - $MP_T/lang $CM_T/lang $B/lang <<'E'
import json, os, sys
mp, cm, out = sys.argv[1:4]
for f in sorted(os.listdir(mp)):
    if not f.endswith('.json'): continue
    m = json.load(open(os.path.join(mp, f))); p = os.path.join(cm, f); c = json.load(open(p)) if os.path.exists(p) else {}
    open(os.path.join(out, f), 'w').write(json.dumps({**m, **c}, ensure_ascii=False, indent=1))
    d = [k for k in m if k in c and m[k] != c[k]]
    print(f'lang {f}: {len(m)} mp + {len(c)} cm = {len({**m, **c})}' + (f'; cm wins on {d}' if d else ''))
E

# ---- loader test tree: dsload + mods (pins = the MP / CM test heads)
L=$LD_T/loader
[[ -d $L/build ]] && { mkdir -p $L/old_builds; mv $L/build $L/old_builds/build-$ST; }
mkdir -p $L/build
loadwait
(cd $L && MP_REPO=$MP_REPO CM_REPO=$CM_REPO MP_PIN=$MPH CM_PIN=$CMH DSMP_ROOT=$HOME/ds_multiplayer \
  MP_DET_RANGES=$HOME/ds_multiplayer/build/det_ranges.tsv nice -n 19 zsh build.sh > build/build_testtree.log 2>&1) \
  || { tail -20 $L/build/build_testtree.log; echo "loader test build FAILED ($L/build/build_testtree.log)" >&2; exit 1; }

# ---- the test hooks are in the binaries
ok=1
chk() { local f=$1; shift; for s in "$@"; do strings $f | grep -q "$s" || { echo "MISSING $s in $f"; ok=0; }; done; }
H=(mpm_alltexts mpm_test_lang test_set_lang DSI_MISS_LOG DSRIG_MISS_LOG)
chk $B/dsrig.dylib $H
chk $L/build/mods/multiplayer/multiplayer.dylib $H
chk $L/build/dsload.dylib DSI_MISS_LOG
chk $L/build/mods/custom-modes/custom-modes.dylib DSI_MISS_LOG
chk $B/custom-modes.dylib DSI_MISS_LOG
echo "MP test head $MPH -> $B/dsrig.dylib"
echo "loader test head $(git -C $LD_T rev-parse HEAD) -> $L/build (pin $(ls $L/build/pin), cmpin $(ls $L/build/cmpin))"
echo "CM test head $CMH"
(( ok )) && echo "TESTTREES PASS (hooks present)" || { echo "TESTTREES FAIL"; exit 1; }
