#!/bin/zsh
# Lane tool (nvchat2): an HM_BUILD kit for the lane's game tests = a dev-key loader (the release gate's hm_build, or
# HM_SRC=<folder with dsload.dylib + mods/multiplayer>) + the multiplayer module built from THIS worktree's files, the same
# way ds_modloader loader/build.sh builds it from a pin (src/build.sh's source list minus rig_main.c + rig_hook.c,
# + loader/modules/multiplayer/mp_module.c). For quick rounds before a commit; the merge proof builds from the commit.
# Usage: zsh ext/nvchat2/build_hm.sh [OUT]   (default <worktree>/build/hm; HM_SRC, LD_REF=origin/main)
set -e
WT=${0:A:h:h:h}; OUT=${1:-$WT/build/hm}; HM_SRC=${HM_SRC:-$HOME/ds_rel030/gate/hm_build}; LD_REF=${LD_REF:-origin/main}
[[ -f $HM_SRC/dsload.dylib && -d $HM_SRC/mods/multiplayer ]] || { echo "no kit at $HM_SRC" >&2; exit 1; }
[[ -f $WT/win/math/build/libdsmath_arm64.a ]] || { echo "no $WT/win/math/build/libdsmath_arm64.a (win/math/build_math.sh)" >&2; exit 1; }
mkdir -p $OUT/mods
LDV=$OUT/ld_src; SHA=$(git -C $HOME/ds_launcher rev-parse $LD_REF)
if [[ ! -f $LDV/$SHA ]]; then
  [[ -d $LDV ]] && mv $LDV $OUT/ld_src.old.$(date +%s)
  mkdir -p $LDV; git -C $HOME/ds_launcher archive $LD_REF loader/modules/multiplayer loader/sdk RELEASE | tar -x -C $LDV; touch $LDV/$SHA
fi
cp -p $HM_SRC/dsload.dylib $OUT/dsload.dylib.tmp; mv -f $OUT/dsload.dylib.tmp $OUT/dsload.dylib
M=$OUT/mods/multiplayer; [[ -d $M ]] || ditto $HM_SRC/mods/multiplayer $M
mpsrc=(${(f)"$(python3 - $WT/src/build.sh <<'PY'
import sys
t = open(sys.argv[1]).read(); t = t[t.index('clang'):t.index('codesign')].replace('\\\n', ' ')
for w in t.split():
    if w.endswith('.c') and w not in ('rig_main.c', 'rig_hook.c') and not w.startswith('-'):
        print(w)
PY
)"})
(( ${#mpsrc} > 10 )) || { echo "cannot read the source list of $WT/src/build.sh" >&2; exit 1; }
srcs=(); for f in $mpsrc; do srcs+=($WT/src/$f); done
nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0 -DDSL_RELEASE=\"$(head -1 $LDV/RELEASE)\" \
  -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/multiplayer.dylib -I$LDV/loader/sdk -I$WT/src -I$WT/win/math -DDSMATH_DEFAULT_ON=1 \
  -o $M/multiplayer.dylib.tmp $LDV/loader/modules/multiplayer/mp_module.c $srcs $WT/win/math/build/libdsmath_arm64.a -lz 2>&1 | grep -v "^ld: warning" || true
[[ -f $M/multiplayer.dylib.tmp ]] || { echo "module build failed" >&2; exit 1; }
codesign -f -s - $M/multiplayer.dylib.tmp 2>/dev/null; mv -f $M/multiplayer.dylib.tmp $M/multiplayer.dylib
[[ -d $WT/lang ]] && { mkdir -p $M/lang; cp $WT/lang/*.json $M/lang/; }
print -r -- "worktree $(git -C $WT rev-parse HEAD) + uncommitted: $(git -C $WT status --short src | wc -l | tr -d ' ') file(s)" > $M/PINNED_COMMIT
echo "built $M/multiplayer.dylib (${#mpsrc} files, loader $SHA)"
