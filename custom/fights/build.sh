#!/bin/zsh
# Build the Test Fights / Gallery code from the same lists the module build reads (custom/fights/sources.txt,
# custom/gallery/sources.txt), with the module's flags, then the test tools and a solo test dylib (link check).
# Output in <repo>/build_c4/ (git-ignored). usage: custom/fights/build.sh [win]   (win = llvm-mingw cross compile check)
set -e
H=${0:A:h}; C=${H:h}; R=${C:h}; B=$R/build_c4; mkdir -p $B/obj $B/objwin
SRCS=($(cat $C/fights/sources.txt $C/gallery/sources.txt))
# same as custom/build_module.sh CF (+ -fwrapv, which QuickJS needs)
CF=(-O2 -Wall -Wno-unused-function -fwrapv -I$R/sdk -I$R/src -I$C -I$C/fe -I$C/pvp -I$C/units)
obj() { print -r -- ${1//\//_}; }
if [[ $1 == win ]]; then
  TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
  for s in $SRCS; do
    o=$B/objwin/$(obj ${s:r}).o
    [[ $o -nt $C/$s && $o -nt $H/sg_prelude.js && $o -nt $H/sg_glue.js ]] || nice -n 19 $TC $CF -D__USE_MINGW_ANSI_STDIO -c $C/$s -o $o
  done
  echo "win objects ok: $B/objwin"
  exit 0
fi
CF=(-arch arm64 -mmacosx-version-min=12.0 -D_DARWIN_C_SOURCE $CF)
OBJS=()
for s in $SRCS; do
  o=$B/obj/$(obj ${s:r}).o; OBJS+=($o)
  [[ $o -nt $C/$s && $o -nt $H/sg_prelude.js && $o -nt $H/sg_glue.js ]] || nice -n 19 clang $CF -c $C/$s -o $o &
done
wait
for o in $OBJS; do [[ -f $o ]] || { echo "missing $o" >&2; exit 1; }; done
# solo dylib of just these lists: proves they link with no multiplayer and no game symbols
nice -n 19 clang $CF -dynamiclib -o $B/fights_solo_check.dylib $OBJS -lm
clang $CF -o $B/sg_cli $H/test/sg_cli.c $OBJS -lm
clang $CF -o $B/sg_cache_test $H/test/sg_cache_test.c $OBJS -lm
clang $CF -o $B/gal_test $C/gallery/test/gal_test.c $OBJS -lm
echo "built: $B/sg_cli $B/sg_cache_test $B/gal_test $B/fights_solo_check.dylib"
