#!/bin/zsh
# Lane build of the Windows mod with the bridge: build/win/baselib.dll. Same steps as win/build_win.sh, but sources =
# the bepinex-bridge worktree src (BRIDGE_SRC, default ~/ds_mp_bepinex/src) and every output stays in this lane folder
# (gen sources, det_ranges_win.tsv, objects, .def). No win_map.py run (reads the shared win/win_map.tsv) and no inline
# audit (the bridge adds no hook). Bridge exports: __declspec(dllexport) in mp_bridge.h (no .def entry needed).
set -e; L="${0:A:h}"; cd ~/ds_multiplayer/win
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
export DSMP_SRC=${BRIDGE_SRC:-~/ds_mp_bepinex/src}; S=$DSMP_SRC; B=$L/build/win
mkdir -p $B/obj
nice -n 19 python3 $L/tools/gen_port_lane.py | tail -1
nice -n 19 python3 $L/tools/gen_def_lane.py
MH=third_party/minhook; ZL=third_party/zlib
INC=(-Iport/posix -Iport -Imath -I$S/ord_test -I$S/net/steam -I$S -I$S/fe -I$S/net -I$MH/include -I$ZL)
CF=(-O2 -g -Wall -Wno-unused-function -Wno-format -D__USE_MINGW_ANSI_STDIO=1 -fno-strict-aliasing)
GEN=($B/gen/*.c $B/gen/net/dsnet_link.c $B/gen/net/dsnet_server.c $B/gen/net/dsnet_client.c)
if [[ -n "$(ls $B/obj)" ]]; then O=$L/old/obj_$(date +%H%M%S); mkdir -p $O; mv $B/obj/*.o $O/; fi
for f in port/rig_main_win.c port/win_test_cmds.c port/win_kit.c port/steam_stub_win.c math/dsmath_win.c $GEN; do nice -n 19 $TC $CF $INC -include dswin.h -c $f -o $B/obj/${f:t:r}.o; done
for f in $ZL/adler32.c $ZL/crc32.c $ZL/inffast.c $ZL/inflate.c $ZL/inftrees.c $ZL/zutil.c; do nice -n 19 $TC -O2 -w -c $f -o $B/obj/zl_${f:t:r}.o; done
for f in port/dswin.c $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c; do nice -n 19 $TC $CF -I$MH/include -c $f -o $B/obj/${f:t:r}.o; done
nice -n 19 $TC -shared -o $B/baselib.dll.tmp $B/obj/*.o math/build/libdsmath_win64.a $B/baselib_exports.def -static -static-libgcc -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
mv -f $B/baselib.dll.tmp $B/baselib.dll
echo built $B/baselib.dll
