#!/bin/zsh
# build/steam_check.exe: the Windows build of src/net/steam/steam_check.c (standalone Steam transport check, game closed).
# Needs build/gen/ from build_win.sh (gen_port.py output: dsnet core + dsnet_steam.c with the dsw_* socket calls).
# Run on Windows: steam_check.exe [steam_api64.dll path] (default: the game's copy under Program Files (x86)\Steam).
set -e; DSMP_ROOT=${0:A:h:h}; S=$DSMP_ROOT/src   # before the cd ($0 may be relative)
cd "${0:A:h}"
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
[[ -f build/gen/net/dsnet_steam.c ]] || { echo "run build_win.sh first (build/gen/net/dsnet_steam.c missing)" >&2; exit 1; }
O=build/obj_steamcheck; mkdir -p $O build/old_obj; olds=($O/*.o(N)); (( ${#olds} )) && mv -f $olds build/old_obj/
CF=(-ffile-prefix-map=$HOME=/src -ffile-prefix-map=$DSMP_ROOT=/mp -O2 -g -Wall -Wno-unused-function -Wno-format -D__USE_MINGW_ANSI_STDIO=1 -fno-strict-aliasing)
INC=(-Ibuild/gen_inc -Iport/posix -Iport -I$S/net/steam -I$S -I$S/net)
for f in build/gen/net/dsnet_link.c build/gen/net/dsnet_server.c build/gen/net/dsnet_client.c build/gen/net/dsnet_steam.c build/gen/ds_i18n.c $S/net/steam/steam_check.c; do
  nice -n 19 $TC $CF -fwrapv $INC -include dswin.h -c $f -o $O/${f:t:r}.o; done
nice -n 19 $TC $CF -c port/dswin.c -o $O/dswin.o
nice -n 19 $TC -Wl,--no-insert-timestamp -o build/steam_check.exe $O/*.o -static -static-libgcc -lws2_32 -liphlpapi -lbcrypt -Wl,-Bstatic -lpthread
echo built build/steam_check.exe
