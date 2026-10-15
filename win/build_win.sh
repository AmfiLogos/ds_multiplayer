#!/bin/zsh
# Build the Windows mod: build/baselib.dll (baselib proxy + rig + det + dsnet + mp).
# Needs ~/tools/llvm-mingw. Sources: src/ (shared with the Mac build, translated by port/gen_port.py) + port/.
set -e; cd "${0:A:h}"; mkdir -p build
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
if [[ -z "$SKIP_GEN" ]]; then nice -n 5 python3 win_map.py > /dev/null
python3 port/gen_port.py; fi
python3 port/gen_def.py
# inline audit: a hook MSVC inlined (Mac callers, no Windows caller) never fires on Windows; stop on a NEW one
# (not in inline_known.tsv, which lists the ones with a tested Windows wrapper). Review 2026-10-14 finding 5.
NEWINL=$(nice -n 5 python3 inline_audit.py | sed -n 's/^INLINED ON WINDOWS: \([^ ]*\) .*/\1/p' | while read m; do cut -f1 inline_known.tsv | grep -qxF "$m" || echo $m; done)
[[ -z $NEWINL ]] || { print -l "ERROR: hooks inlined on Windows with no wrapper (add a wrapper, then a line in inline_known.tsv):" $NEWINL; exit 1; }
echo "inline audit: no new inlined hook"
[[ -f math/build/libdsmath_win64.a ]] || math/build_math.sh
S=${DSMP_SRC:-~/ds_multiplayer/src}   # DSMP_SRC: build from a frozen snapshot (release)
export DSMP_SRC=$S
MH=third_party/minhook
INC=(-Iport/posix -Iport -Imath -I$S/ord_test -I$S/net/steam -I$S -I$S/net -I$MH/include)
CF=(-O2 -g -Wall -Wno-unused-function -Wno-format -D__USE_MINGW_ANSI_STDIO=1 -fno-strict-aliasing)
GEN=(build/gen/*.c build/gen/net/dsnet_link.c build/gen/net/dsnet_server.c build/gen/net/dsnet_client.c)
NET=()
mkdir -p build/obj; rm -f build/obj/*.o(N)
for f in port/rig_main_win.c port/win_test_cmds.c port/win_kit.c port/steam_stub_win.c math/dsmath_win.c $GEN $NET; do nice -n 5 $TC $CF $INC -include dswin.h -c $f -o build/obj/${f:t:r}.o; done
for f in port/dswin.c $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c; do nice -n 5 $TC $CF -I$MH/include -c $f -o build/obj/${f:t:r}.o; done
nice -n 5 $TC -shared -o build/baselib.dll.tmp build/obj/*.o math/build/libdsmath_win64.a build/baselib_exports.def -static -static-libgcc -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/baselib.dll.tmp build/baselib.dll
echo built build/baselib.dll
