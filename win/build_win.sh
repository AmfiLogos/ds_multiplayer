#!/bin/zsh
# Build the Windows mod: build/baselib.dll (baselib proxy + rig + det + dsnet + mp + Custom Modes' mp part, one DLL).
# DSMP_TARGET=module: modules of the mod loader instead (~/ds_launcher loader/start/win; the loader's baselib.dll proxy
# loads them; header dsl_sdk.h from DSL_SDK, default ~/ds_launcher/loader/sdk):
#   build/mods/multiplayer/   multiplayer.dll (+ import library build/libmultiplayer.dll.a)
#   build/mods/custom-modes/  custom-modes.dll (solo part, custom/**/sources.txt + cm_module.c) and custom-modes-mp.dll
#                             (custom/**/sources_mp.txt + cm_mp.c, linked to multiplayer.dll; loaded by custom-modes.dll
#                             only when multiplayer runs), like the Mac's custom-modes.dylib + custom-modes-mp.dylib.
# Needs ~/tools/llvm-mingw. Sources: src/ (shared with the Mac build, translated by port/gen_port.py) + port/.
set -e; export DSMP_ROOT=${0:A:h:h}   # the tools read src/ and write win/build of THIS checkout (worktrees too); before the cd (relative $0)
cd "${0:A:h}"; mkdir -p build
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
C=${DSMP_CUSTOM:-${S:h}/custom}; export DSMP_CUSTOM=$C   # Custom Modes sources (one DLL here: -DCM_SAME_IMAGE)
CINC=(-I$C); for d in $C/*(/N); do CINC+=(-I$d); done             # every custom/ folder (fe, pvp, units, home, ...)
INC=(-Ibuild/gen_inc -Iport/posix -Iport -Imath -I$S/ord_test -I$S/net/steam -I$S $CINC -I$S/net -I$DSMP_ROOT/sdk -I$MH/include -I$ZL)
CF=(-O2 -g -Wall -Wno-unused-function -Wno-format -D__USE_MINGW_ANSI_STDIO=1 -fno-strict-aliasing -DCM_SAME_IMAGE)
TARGET=${DSMP_TARGET:-baselib}
if [[ $TARGET == module ]]; then CF+=(-DDSMP_MODULE); INC+=(-I${DSL_SDK:-~/ds_launcher/loader/sdk}); fi
GEN=(build/gen/*.c build/gen/net/dsnet_link.c build/gen/net/dsnet_server.c build/gen/net/dsnet_client.c)
CM_SOLO=(); CM_MP=(); while read k f; do if [[ $k == solo ]]; then CM_SOLO+=(build/gen_cm/$f); else CM_MP+=(build/gen_cm/$f); fi; done < build/gen_cm/lists.txt
[[ $TARGET == module ]] || GEN+=($CM_MP)                           # rig baselib.dll: Custom Modes' mp part in the same DLL
NET=()
O=build/obj; [[ $TARGET == module ]] && O=build/obj_module
mkdir -p $O build/old_obj; olds=($O/*.o(N)); (( ${#olds} )) && mv -f $olds build/old_obj/
for f in port/rig_main_win.c port/win_test_cmds.c port/win_kit.c port/steam_stub_win.c math/dsmath_win.c $GEN $NET; do nice -n 5 $TC $CF $INC -include dswin.h -c $f -o $O/${f:t:r}.o; done
for f in $ZL/adler32.c $ZL/crc32.c $ZL/inffast.c $ZL/inflate.c $ZL/inftrees.c $ZL/zutil.c; do nice -n 5 $TC -O2 -w -c $f -o $O/zl_${f:t:r}.o; done
if [[ $TARGET == module ]]; then
nice -n 5 $TC $CF -c port/dswin.c -o $O/dswin.o
mkdir -p build/link                                              # the import library names the DLL by its output file name
nice -n 5 $TC -shared -o build/link/multiplayer.dll $O/*.o math/build/libdsmath_win64.a -Wl,--export-all-symbols -Wl,--out-implib,build/libmultiplayer.dll.a -static -static-libgcc -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/link/multiplayer.dll build/multiplayer.dll
echo built build/multiplayer.dll
# Custom Modes, its own module. mp part: -DCM_SAME_IMAGE here means "multiplayer's rig_hook" (imported from
# multiplayer.dll: its Windows rules for folded / arm64-only hooks apply to Custom Modes' hooks too).
OC=build/obj_cm; mkdir -p $OC/solo $OC/mp; for o in $OC/solo/*.o(N) $OC/mp/*.o(N); do mv -f $o build/old_obj/; done
for f in $CM_MP; do nice -n 5 $TC $CF $INC -include dswin.h -c $f -o $OC/mp/${f:t:r}.o; done
nice -n 5 $TC -shared -o build/custom-modes-mp.dll.tmp $OC/mp/*.o build/libmultiplayer.dll.a -Wl,--export-all-symbols -static -static-libgcc -lws2_32 -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/custom-modes-mp.dll.tmp build/custom-modes-mp.dll
# solo part: no multiplayer link; dlopen / dlsym / dladdr = LoadLibrary / GetProcAddress (port/cm/dlfcn.h)
for f in $CM_SOLO; do nice -n 5 $TC ${CF:#-DCM_SAME_IMAGE} -Iport/cm ${INC:#-Iport/posix} -include cm_win.h -c $f -o $OC/solo/${f:t:r}.o; done
nice -n 5 $TC -shared -o build/custom-modes.dll.tmp $OC/solo/*.o -Wl,--export-all-symbols -static -static-libgcc -lws2_32 -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/custom-modes.dll.tmp build/custom-modes.dll
echo "built build/custom-modes.dll (${#CM_SOLO} files) + build/custom-modes-mp.dll (${#CM_MP} files)"
# module folder for the loader: build/mods/multiplayer/ = the dll, its ranges and mod.json (the loader's Mac mod.json
# from DSL_MODJSON, default ~/ds_launcher/loader/modules/multiplayer/mod.json, + platforms.win-x64 with file sha256s)
M=build/mods/multiplayer; mkdir -p $M
cp build/multiplayer.dll $M/multiplayer.dll.tmp && mv -f $M/multiplayer.dll.tmp $M/multiplayer.dll
cp build/det_ranges_win.tsv $M/det_ranges_win.tsv.tmp && mv -f $M/det_ranges_win.tsv.tmp $M/det_ranges_win.tsv
python3 - ${DSL_MODJSON:-~/ds_launcher/loader/modules/multiplayer/mod.json} $M <<'PY'
import hashlib, json, os, sys
m = json.load(open(sys.argv[1])); d = sys.argv[2]
files = {f: hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest() for f in ("multiplayer.dll", "det_ranges_win.tsv")}
m.setdefault("platforms", {})["win-x64"] = {"entry": "multiplayer.dll", "files": files}
json.dump(m, open(os.path.join(d, "mod.json"), "w"), indent=1)
print("module folder", d, files["multiplayer.dll"][:16])
PY
M=build/mods/custom-modes; mkdir -p $M
for f in custom-modes.dll custom-modes-mp.dll; do cp build/$f $M/$f.tmp && mv -f $M/$f.tmp $M/$f; done
python3 - ${CM_MODJSON:-$C/mod.json} $M <<'PY'
import hashlib, json, os, sys
m = json.load(open(sys.argv[1])); d = sys.argv[2]
files = {f: hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest() for f in ("custom-modes.dll", "custom-modes-mp.dll")}
m.setdefault("platforms", {})["win-x64"] = {"entry": "custom-modes.dll", "files": files}
json.dump(m, open(os.path.join(d, "mod.json"), "w"), indent=1)
print("module folder", d, files["custom-modes.dll"][:16], files["custom-modes-mp.dll"][:16])
PY
else
for f in port/dswin.c $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c; do nice -n 5 $TC $CF -I$MH/include -c $f -o $O/${f:t:r}.o; done
nice -n 5 $TC -shared -o build/baselib.dll.tmp $O/*.o math/build/libdsmath_win64.a build/baselib_exports.def -static -static-libgcc -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/baselib.dll.tmp build/baselib.dll
echo built build/baselib.dll
fi
