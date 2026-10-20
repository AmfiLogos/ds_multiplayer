#!/bin/zsh
# Build the Windows mod: build/baselib.dll (baselib proxy + rig + det + dsnet + mp incl. Foreign Expedition / PvP, one DLL).
# DSMP_TARGET=module: modules of the mod loader instead (~/ds_launcher loader/start/win; the loader's baselib.dll proxy
# loads them; header dsl_sdk.h from DSL_SDK, default ~/ds_launcher/loader/sdk):
#   build/mods/multiplayer/   multiplayer.dll (+ import library build/libmultiplayer.dll.a); Foreign Expedition + PvP inside
#   build/mods/custom-modes/  custom-modes.dll: Units Laboratory (custom/**/sources.txt + cm_module.c), like the Mac's
#                             custom-modes.dylib. Since the Units Laboratory split it never links multiplayer.dll.
# Needs ~/tools/llvm-mingw. Sources: src/ (shared with the Mac build, translated by port/gen_port.py) + port/.
set -e; export DSMP_ROOT=${0:A:h:h}   # the tools read src/ and write win/build of THIS checkout (worktrees too); before the cd (relative $0)
cd "${0:A:h}"; mkdir -p build
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
C=${DSMP_CUSTOM:-${${DSMP_SRC:-$DSMP_ROOT/src}:h}/custom}   # Units Laboratory sources: custom/ of a tree that has them (the loader's pin)
if [[ ! -f $C/build_module.sh ]]; then   # since post-v13 its own repo: a clean copy of that checkout (its build/ holds other trees; the tools scan all)
  CM=${CM_REPO:-$HOME/ds_custommodes}
  [[ -f $CM/build_module.sh ]] || { echo "Custom Modes checkout not found: $CM (git clone https://github.com/AmfiLogos/ds_custommodes, or set CM_REPO)" >&2; exit 1; }
  mkdir -p build/old; [[ -d build/cm_src ]] && mv build/cm_src build/old/cm_src-$(date +%Y%m%d-%H%M%S)
  rsync -a --exclude /build/ --exclude /old/ --exclude /.git/ $CM/ build/cm_src/; C=$PWD/build/cm_src
fi
export DSMP_CUSTOM=$C
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
CINC=(-I$C); CEMB=(--embed-dir=$C)                               # every custom/ folder (fe, pvp, units, home, fights/tinf ...)
for d in $C/**/*(/N); do [[ ${d:t} == test || $d == */test/* ]] || { CINC+=(-I$d); CEMB+=(--embed-dir=$d); }; done
INC=(-Ibuild/gen_inc -Iport/posix -Iport -Imath -I$S/ord_test -I$S/net/steam -I$S -I$S/net -I$DSMP_ROOT/sdk -I$MH/include -I$ZL)
INC_CM=(-Ibuild/gen_inc -Iport/posix -Iport -Imath $CINC -I$S/ord_test -I$S/net/steam -I$S -I$S/net -I$DSMP_ROOT/sdk -I$MH/include -I$ZL)   # Units Laboratory: its own headers first
PM=(-ffile-prefix-map=$HOME=/src)                               # no build path (user name) in the DLLs (release gate G12)
# reproducible DLLs: the same bytes from any build folder (Custom Modes' quickjs __FILE__ paths; the more specific map wins)
PM+=(-ffile-prefix-map=$C=/custom -ffile-prefix-map=$DSMP_ROOT=/mp)
RL=(-Wl,--no-insert-timestamp)                                   # and no build time in the PE headers
CF=($PM -O2 -g -Wall -Wno-unused-function -Wno-format -D__USE_MINGW_ANSI_STDIO=1 -fno-strict-aliasing)
TARGET=${DSMP_TARGET:-baselib}
if [[ $TARGET == module ]]; then CF+=(-DDSMP_MODULE); INC=(-I${DSL_SDK:-~/ds_launcher/loader/sdk} $INC); fi   # the loader's dsl_sdk.h before sdk/ (an older copy)
GEN=(build/gen/*.c build/gen/net/dsnet_link.c build/gen/net/dsnet_server.c build/gen/net/dsnet_client.c)
CM_SOLO=(); while read k f; do [[ $k == solo ]] && CM_SOLO+=(build/gen_cm/$f); done < build/gen_cm/lists.txt
# rig baselib.dll (the old Wine rig): multiplayer only; Units Laboratory is a loader module
NET=()
O=build/obj; [[ $TARGET == module ]] && O=build/obj_module
mkdir -p $O build/old_obj; olds=($O/*.o(N)); (( ${#olds} )) && mv -f $olds build/old_obj/
# i18n: one string table per DLL (each has its own DSI_IMPLEMENTATION)
for f in port/rig_main_win.c port/win_test_cmds.c port/win_kit.c port/steam_stub_win.c math/dsmath_win.c $GEN $NET; do nice -n 5 $TC $CF -fwrapv $INC -include dswin.h -include port/cm/xm_bsd_win.h -c $f -o $O/${f:t:r}.o; done
for f in $ZL/adler32.c $ZL/crc32.c $ZL/inffast.c $ZL/inflate.c $ZL/inftrees.c $ZL/zutil.c; do nice -n 5 $TC $PM -O2 -w -c $f -o $O/zl_${f:t:r}.o; done
if [[ $TARGET == module ]]; then
nice -n 5 $TC $CF -c port/dswin.c -o $O/dswin.o
mkdir -p build/link                                              # the import library names the DLL by its output file name
nice -n 5 $TC -shared $RL -o build/link/multiplayer.dll $O/*.o math/build/libdsmath_win64.a -Wl,--export-all-symbols -Wl,--out-implib,build/libmultiplayer.dll.a -static -static-libgcc -lwinhttp -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread   # WinHTTP: the hub template (fe_http.c)
mv -f build/link/multiplayer.dll build/multiplayer.dll
echo built build/multiplayer.dll
# Units Laboratory, its own module: no multiplayer link; dlopen / dlsym / dladdr = LoadLibrary / GetProcAddress
# (port/cm/dlfcn.h); WinHTTP for fights/cm_http.c; zlib inflate objects of this build.
OC=build/obj_cm; mkdir -p $OC/solo; for o in $OC/solo/*.o(N) $OC/mp/*.o(N); do mv -f $o build/old_obj/; done
for f in $CM_SOLO; do nice -n 5 $TC $CF -fwrapv $CEMB -Iport/cm $INC_CM -include dswin.h -include xm_bsd_win.h -c $f -o $OC/solo/${f:t:r}.o; done
nice -n 5 $TC $CF -c port/dswin.c -o $OC/solo/zz_dswin.o                 # its own copy of the POSIX shims (dsw_rename ...)
nice -n 5 $TC -shared $RL -o build/link/custom-modes.dll $OC/solo/*.o $O/zl_*.o -Wl,--export-all-symbols -static -static-libgcc -lwinhttp -lws2_32 -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/link/custom-modes.dll build/custom-modes.dll
echo "built build/custom-modes.dll (${#CM_SOLO} files)"
# module folder for the loader: build/mods/multiplayer/ = the dll, its ranges and mod.json (the loader's Mac mod.json
# from DSL_MODJSON, default ~/ds_launcher/loader/modules/multiplayer/mod.json, + platforms.win-x64 with file sha256s)
M=build/mods/multiplayer; mkdir -p $M
cp build/multiplayer.dll $M/multiplayer.dll.tmp && mv -f $M/multiplayer.dll.tmp $M/multiplayer.dll
cp build/det_ranges_win.tsv $M/det_ranges_win.tsv.tmp && mv -f $M/det_ranges_win.tsv.tmp $M/det_ranges_win.tsv
# i18n: the language files (repo lang/*.json) in lang/ beside the dll. Not in mod.json's files list (not hashed: a translation fix
# never stops players from joining each other)
if [[ -d $DSMP_ROOT/lang ]]; then mkdir -p $M/lang; for f in $DSMP_ROOT/lang/*.json(N); do cp $f $M/lang/${f:t}.tmp && mv -f $M/lang/${f:t}.tmp $M/lang/${f:t}; done; fi
python3 - ${DSL_MODJSON:-~/ds_launcher/loader/modules/multiplayer/mod.json} $M <<'PY'
import hashlib, json, os, sys
m = json.load(open(sys.argv[1])); d = sys.argv[2]
files = {f: hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest() for f in ("multiplayer.dll", "det_ranges_win.tsv")}
m.setdefault("platforms", {})["win-x64"] = {"entry": "multiplayer.dll", "files": files}
json.dump(m, open(os.path.join(d, "mod.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print("module folder", d, files["multiplayer.dll"][:16])
PY
M=build/mods/custom-modes; mkdir -p $M
cp build/custom-modes.dll $M/custom-modes.dll.tmp && mv -f $M/custom-modes.dll.tmp $M/custom-modes.dll
[[ -e $M/custom-modes-mp.dll ]] && { mkdir -p build/old; mv -f $M/custom-modes-mp.dll build/old/custom-modes-mp.dll.$(date +%Y%m%d-%H%M%S); }   # gone since the split
if [[ -d $C/lang ]]; then mkdir -p $M/lang; for f in $C/lang/*.json(N); do cp $f $M/lang/${f:t}.tmp && mv -f $M/lang/${f:t}.tmp $M/lang/${f:t}; done; fi   # its translations, not hashed
python3 - ${CM_MODJSON:-$C/mod.json} $M <<'PY'
import hashlib, json, os, sys
m = json.load(open(sys.argv[1])); d = sys.argv[2]
files = {f: hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest() for f in ("custom-modes.dll",)}
m.setdefault("platforms", {})["win-x64"] = {"entry": "custom-modes.dll", "files": files}
json.dump(m, open(os.path.join(d, "mod.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print("module folder", d, files["custom-modes.dll"][:16])
PY
else
for f in port/dswin.c $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c; do nice -n 5 $TC $CF -I$MH/include -c $f -o $O/${f:t:r}.o; done
nice -n 5 $TC -shared $RL -o build/baselib.dll.tmp $O/*.o math/build/libdsmath_win64.a build/baselib_exports.def -static -static-libgcc -lwinhttp -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
mv -f build/baselib.dll.tmp build/baselib.dll
echo built build/baselib.dll
fi
