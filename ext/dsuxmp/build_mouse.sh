#!/bin/zsh
# Test build of src/ as the LOADER'S multiplayer module (like ds_modloader loader/build.sh: modules/multiplayer/mp_module.c +
# the src/build.sh file list minus rig_main.c + rig_hook.c, the loader's sdk/), with the fake mouse (-DTEAMS_MOUSE_TEST:
# ui_teams {action:"mouse", x, y, button}) -> $1. The rig's own dylib has no loader host: no kit, no UI scale, no card window.
# LOADER = the loader tree that gives mp_module.c and sdk/ (default the dsuxmp2 shared loader worktree, ds_modloader 83636d0: the kit red X, UI Scale, right-drag move; only read).
set -e; out=${1:?usage: build_mouse.sh <out.dylib>}; out=${out:A}; cd ${SRC:-"${0:A:h}/../../src"}; mkdir -p ${out:h}   # SRC = another src/ tree (a before-build)
L=${LOADER:-$HOME/ds_launcher_wt_dsuxmp2/loader}
python3 ../tools/rv_gate.py > /dev/null || { python3 ../tools/rv_gate.py >&2; exit 1; }
srcs=(${(f)"$(python3 - build.sh <<'PY'
import sys
t = open(sys.argv[1]).read(); t = t[t.index('clang'):t.index('codesign')].replace('\\\n', ' ')
for w in t.split():
    if w.endswith('.c') and w not in ('rig_main.c', 'rig_hook.c') and not w.startswith('-'):
        print(w)
PY
)"})
(( ${#srcs} > 10 )) || { echo "cannot read the source list of src/build.sh" >&2; exit 1; }
nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0 -DTEAMS_MOUSE_TEST -D_DARWIN_C_SOURCE -dynamiclib \
  -framework AppKit -framework CoreGraphics -framework CoreFoundation -install_name @rpath/multiplayer.dylib -I$L/sdk -I. -I../win/math \
  -DDSMATH_DEFAULT_ON=1 -o $out.tmp $L/modules/multiplayer/mp_module.c $srcs ../win/math/build/libdsmath_arm64.a -lz
codesign -f -s - $out.tmp 2>/dev/null
mv -f $out.tmp $out
echo built $out
