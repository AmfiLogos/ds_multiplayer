#!/bin/sh
# Windows build of the units code + win_probe.exe (llvm-mingw, ~/tools/llvm-mingw), then a run under CrossOver wine
# when no other wine runs (LANE_RULES 7: one Windows copy machine-wide). NORUN=1 = build only.
set -e
D=$(cd "$(dirname "$0")" && pwd); R="$D/../../../.."; S="$R/src"; U="$D/../.."; C="$R/custom"; ZL="$R/win/third_party/zlib"; O="$R/build/units/win"
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
mkdir -p "$O"
nice -n 5 $TC -O1 -g -Wall -Wno-unused-function -Wno-format -D__USE_MINGW_ANSI_STDIO=1 -include dswin.h '-Dlocaltime_r(t,m)=(localtime_s((m),(t)) ? NULL : (m))' -I"$S" -I"$U" -I"$C" -I"$C/fe" -I"$ZL" -I"$R/win/port/posix" -I"$R/win/port" -o "$O/win_probe.exe" "$D/win_probe.c" \
  "$U/mu_card.c" "$U/mu_link.c" "$U/mu_clip.c" "$U/myunits.c" "$U/wk_link.c" "$U/wk_http.c" "$U/pvp_wiki.c" "$U/pvp_build.c" \
  "$C/fe/fe_json.c" "$C/fe/fe_save.c" "$ZL/inflate.c" "$ZL/inffast.c" "$ZL/inftrees.c" "$ZL/adler32.c" "$ZL/crc32.c" "$ZL/zutil.c" \
  -static -lwinhttp -lbcrypt
echo "built $O/win_probe.exe"
[ -n "$NORUN" ] && exit 0
if pgrep -fl wine | grep -v "win_run" | grep -q .; then pgrep -fl wine | head -3; echo "wine already runs (another lane): not started"; exit 0; fi
C=~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
[ -x "$C/wine" ] || C=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
cd "$O" && nice -n 19 "$C/wine" --bottle "${BOTTLE:-DSMP}" "$O/win_probe.exe" 2>/dev/null
