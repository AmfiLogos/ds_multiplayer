#!/bin/zsh
# uxmp-text: this worktree's src/ as the loader's multiplayer MODULE -> build/module/multiplayer.dylib (what a player's game
# loads; the rig build of src/build.sh is refused by the loader: "no dsl_plugin_api_version"). Same recipe as ds_modloader
# loader/build.sh: the source list of src/build.sh minus rig_main.c + rig_hook.c, plus the loader's
# modules/multiplayer/mp_module.c and sdk/ (the shared dsuxmp loader worktree, read only).
set -e; W=${0:A:h:h:h}; L=${DSL_LOADER:-$HOME/ds_launcher_wt_dsuxmp/loader}; out=$W/build/module/multiplayer.dylib; mkdir -p ${out:h}
python3 $W/tools/rv_gate.py > /dev/null || { python3 $W/tools/rv_gate.py >&2; exit 1; }
srcs=(${(f)"$(python3 - $W/src/build.sh <<'PY'
import re, sys
t = open(sys.argv[1]).read(); t = t[t.index('clang'):t.index('codesign')].replace('\\\n', ' ')
for w in t.split():
    if w.endswith('.c') and w not in ('rig_main.c', 'rig_hook.c') and not w.startswith('-'):
        print(w)
PY
)"})
(( ${#srcs} > 10 )) || { echo "cannot read the source list of src/build.sh" >&2; exit 1; }
cd $W/src
nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0 -DDSL_RELEASE=\"$(head -1 $L/../RELEASE)\" \
  -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/multiplayer.dylib -I$L/sdk -I$W/src -I$W/win/math -DDSMATH_DEFAULT_ON=1 \
  -o $out.tmp $L/modules/multiplayer/mp_module.c $srcs $W/win/math/build/libdsmath_arm64.a -lz
codesign -f -s - $out.tmp 2>/dev/null
mv -f $out.tmp $out                                           # new inode: never overwrite a mapped dylib
echo built $out
