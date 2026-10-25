#!/bin/zsh
# hud_build_module.sh <ds_multiplayer tree> <out.dylib> [extra cflags...]   (lane dsuxmp, sub-lane uxmp-hud)
# The multiplayer LOADER MODULE (what hud_test.py installs) from a ds_multiplayer source tree, the way the loader's
# build.sh builds it from its pin: the tree's src/build.sh file list minus rig_main.c + rig_hook.c, plus the loader's
# modules/multiplayer/mp_module.c and sdk (shared loader worktree ~/ds_launcher_wt_dsuxmp, read only). The tree needs
# win/math/build/libdsmath_arm64.a. Tests: extra cflags -DTEAMS_MOUSE_TEST (fake mouse through UnityEngine.Input).
set -e
T=${1:A}; OUT=${2:A}; shift 2
L=${LOADER:-$HOME/ds_launcher_wt_dsuxmp/loader}
srcs=(${(f)"$(python3 - $T/src/build.sh <<'PY'
import sys
t = open(sys.argv[1]).read(); t = t[t.index('clang'):t.index('codesign')].replace('\\\n', ' ')
for w in t.split():
    if w.endswith('.c') and w not in ('rig_main.c', 'rig_hook.c') and not w.startswith('-') and not w.startswith('../'):
        print(w)
PY
)"})
(( ${#srcs} > 10 )) || { echo "cannot read the source list of $T/src/build.sh" >&2; exit 1; }
files=(); for f in $srcs; do files+=($T/src/$f); done
nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0 -DDSL_RELEASE=\"$(head -1 $L/../RELEASE)\" "$@" \
  -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/multiplayer.dylib -I$L/sdk -I$T/src -I$T/win/math -DDSMATH_DEFAULT_ON=1 \
  -o $OUT.tmp $L/modules/multiplayer/mp_module.c $files $T/win/math/build/libdsmath_arm64.a -lz
codesign -f -s - $OUT.tmp 2>/dev/null
mv -f $OUT.tmp $OUT
echo "built $OUT from $T (${#srcs} files) $*"
