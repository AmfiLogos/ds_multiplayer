#!/bin/zsh
# Build the Windows mod: build/baselib.dll (baselib proxy + rig + det + dsnet + mp).
# DSMP_TARGET=module: build/multiplayer.dll instead, a module of the mod loader (~/ds_launcher loader/start/win; the
# loader's baselib.dll proxy loads it; header dsl_sdk.h from DSL_SDK, default ~/ds_launcher/loader/sdk).
# Needs ~/tools/llvm-mingw. Sources: src/ (shared with the Mac build, translated by port/gen_port.py) + port/.
set -e; cd "${0:A:h}"; mkdir -p build
export DSMP_ROOT=${0:A:h:h}   # the tools read src/ and write win/build of THIS checkout (worktrees too)
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
S=${DSMP_SRC:-$DSMP_ROOT/src}   # DSMP_SRC: build from a frozen snapshot (release)
export DSMP_SRC=$S
MH=third_party/minhook
ZL=third_party/zlib    # inflate only (pvp_wiki.c builder links): zlib 1.3.1 from the local Unity il2cpp sources
INC=(-Iport/posix -Iport -Imath -I$S/ord_test -I$S/net/steam -I$S -I$S/fe -I$S/net -I$MH/include -I$ZL)
CF=(-O2 -g -Wall -Wno-unused-function -Wno-format -D__USE_MINGW_ANSI_STDIO=1 -fno-strict-aliasing)
TARGET=${DSMP_TARGET:-baselib}
if [[ $TARGET == module ]]; then CF+=(-DDSMP_MODULE); INC+=(-I${DSL_SDK:-~/ds_launcher/loader/sdk}); fi
GEN=(build/gen/*.c build/gen/net/dsnet_link.c build/gen/net/dsnet_server.c build/gen/net/dsnet_client.c)
NET=()
O=build/obj; [[ $TARGET == module ]] && O=build/obj_module
mkdir -p $O build/old_obj; olds=($O/*.o(N)); (( ${#olds} )) && mv -f $olds build/old_obj/
for f in port/rig_main_win.c port/win_test_cmds.c port/win_kit.c port/steam_stub_win.c math/dsmath_win.c $GEN $NET; do nice -n 5 $TC $CF $INC -include dswin.h -c $f -o $O/${f:t:r}.o; done
for f in $ZL/adler32.c $ZL/crc32.c $ZL/inffast.c $ZL/inflate.c $ZL/inftrees.c $ZL/zutil.c; do nice -n 5 $TC -O2 -w -c $f -o $O/zl_${f:t:r}.o; done
if [[ $TARGET == module ]]; then
nice -n 5 $TC $CF -c port/dswin.c -o $O/dswin.o
nice -n 5 $TC -shared -o build/multiplayer.dll.tmp $O/*.o math/build/libdsmath_win64.a -Wl,--export-all-symbols -static -static-libgcc -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/multiplayer.dll.tmp build/multiplayer.dll
echo built build/multiplayer.dll
else
for f in port/dswin.c $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c; do nice -n 5 $TC $CF -I$MH/include -c $f -o $O/${f:t:r}.o; done
nice -n 5 $TC -shared -o build/baselib.dll.tmp $O/*.o math/build/libdsmath_win64.a build/baselib_exports.def -static -static-libgcc -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/baselib.dll.tmp build/baselib.dll
echo built build/baselib.dll
fi
