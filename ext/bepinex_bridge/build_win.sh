#!/bin/zsh
# Lane build of the Windows mod with the bridge: build/win/baselib.dll (same as win/build_win.sh, but every output stays
# in this lane folder: gen sources, det_ranges_win.tsv, objects, .def). Sources: ./src (lane copy). No win_map.py run
# (reads the shared win/win_map.tsv as it is) and no inline audit (the bridge adds no hook).
set -e; L="${0:A:h}"; W=~/ds_multiplayer/win; cd $W
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
export DSMP_SRC=$L/src
nice -n 19 python3 $L/tools/gen_port_lane.py | tail -2
nice -n 19 python3 $L/tools/gen_def_lane.py
# fe/fe_game.c is not in the Windows port yet (win lane TODO): file dates only (save lists), never sim state
sed -i '' -e 's/st\.st_mtimespec\.tv_sec/st.st_mtime/g' -e 's/st\.st_mtimespec\.tv_nsec/0/g' \
  -e 's/localtime_r(\([^,]*\), &tm)/localtime_s(\&tm, \1)/g' $L/build/win/gen/fe_game.c $L/build/win/gen/fe_save.c
S=$DSMP_SRC; B=$L/build/win; MH=third_party/minhook
INC=(-I$S/fe -I$HOME/tools/ghidra_12.1.4_PUBLIC/Ghidra/Features/Decompiler/src/decompile/zlib -Iport/posix -Iport -Imath -I$S/ord_test -I$S/net/steam -I$S -I$S/net -I$MH/include)
CF=(-O2 -g -Wall -Wno-unused-function -Wno-format -D__USE_MINGW_ANSI_STDIO=1 -fno-strict-aliasing)
GEN=($B/gen/*.c $B/gen/net/dsnet_link.c $B/gen/net/dsnet_server.c $B/gen/net/dsnet_client.c)
mkdir -p $B/obj; [[ -z "$(ls $B/obj)" ]] || { mkdir -p $L/old/obj_$(date +%H%M%S); mv $B/obj/*.o $L/old/obj_$(date +%H%M%S)/ 2>/dev/null || true; }
[[ -f math/build/libdsmath_win64.a ]] || { echo "need win/math/build/libdsmath_win64.a"; exit 1; }
for f in port/rig_main_win.c port/win_test_cmds.c port/win_kit.c port/steam_stub_win.c math/dsmath_win.c $GEN; do
  nice -n 19 $TC $CF $INC -include dswin.h -c $f -o $B/obj/${f:t:r}.o
done
# zlib (src/pvp_wiki.c inflate since 12:08; win/ has none): raw inflate only, no gzip (NO_GZIP: no crc32)
Z=~/tools/ghidra_12.1.4_PUBLIC/Ghidra/Features/Decompiler/src/decompile/zlib
for f in $Z/adler32.c $Z/inflate.c $Z/inffast.c $Z/inftrees.c $Z/zutil.c; do nice -n 19 $TC -O2 -DNO_GZIP -I$Z -c $f -o $B/obj/z_${f:t:r}.o; done
for f in port/dswin.c $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c; do nice -n 19 $TC $CF -I$MH/include -c $f -o $B/obj/${f:t:r}.o; done
nice -n 19 $TC -shared -o $B/baselib.dll.tmp $B/obj/*.o math/build/libdsmath_win64.a $B/baselib_exports.def -static -static-libgcc -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
mv -f $B/baselib.dll.tmp $B/baselib.dll
echo built $B/baselib.dll
