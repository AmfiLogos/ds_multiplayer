#!/bin/zsh
# Test build only: the Mac rig dylib (same files and flags as src/build.sh) + math_mac.c + musl, math forced on.
# Output: win/build/dsrig_math.dylib (used by xplay_test.py --math). The real build gets the registration lines
# from win/NOTES.md section 7.
set -e; cd "${0:A:h}"; [[ -f build/libdsmath_arm64.a ]] || ./build_math.sh
S=~/ds_multiplayer/src
FILES=($(grep -oE '[A-Za-z_/]+\.c' $S/build.sh))        # source list of the real build, nothing else from it
FILES=(${(u)FILES})
cd $S
nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics \
  -install_name @rpath/dsrig.dylib -Wl,-U,_trn_submit_map_change -o ../win/build/dsrig_math.dylib.tmp \
  $FILES ../win/math/math_mac.c -I../win/math -DDSMATH_DEFAULT_ON=1 ../win/math/build/libdsmath_arm64.a
codesign -f -s - ../win/build/dsrig_math.dylib.tmp 2>/dev/null
mv -f ../win/build/dsrig_math.dylib.tmp ../win/build/dsrig_math.dylib
echo built win/build/dsrig_math.dylib "(${#FILES} src files)"
