#!/bin/zsh
# Build the deterministic math library + its test for: Mac arm64, Mac x86_64 (Rosetta), Windows x64.
#   build/libdsmath_<arch>.a, build/dsmath_test_<arch>[.exe]
set -e; cd "${0:A:h}"; mkdir -p build
FL=(-O2 -std=gnu11 -D_USE_MATH_DEFINES -ffp-contract=off -fno-fast-math -U__FP_FAST_FMA -U__FP_FAST_FMAF -Ishim -Imusl -include dsm_rename.h -w)
build() {  # $1 name, $2.. compiler + flags
  local n=$1; shift
  rm -rf build/obj_$n; mkdir -p build/obj_$n
  for f in musl/*.c; do nice -n 5 "$@" $FL -c $f -o build/obj_$n/${f:t:r}.o; done
  rm -f build/libdsmath_$n.a; ~/tools/llvm-mingw/bin/llvm-ar rcs build/libdsmath_$n.a build/obj_$n/*.o
}
build arm64 clang -arch arm64 -mmacosx-version-min=12.0
build x86_64 clang -arch x86_64 -mmacosx-version-min=12.0
build win64 ~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
clang -arch arm64 -mmacosx-version-min=12.0 -O1 -ffp-contract=off dsmath_test.c build/libdsmath_arm64.a -o build/dsmath_test_arm64
clang -arch x86_64 -mmacosx-version-min=12.0 -O1 -ffp-contract=off dsmath_test.c build/libdsmath_x86_64.a -o build/dsmath_test_x86_64
~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang -O1 -ffp-contract=off dsmath_test.c build/libdsmath_win64.a -o build/dsmath_test_win64.exe
echo built
