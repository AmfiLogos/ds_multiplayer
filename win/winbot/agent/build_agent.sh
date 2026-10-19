#!/bin/sh
# Builds agent/build/winagent.exe (x64, GUI subsystem, static, no host path or user name inside).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-$HOME/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang}"
mkdir -p "$HERE/build"
cd "$HERE"
"$CC" -O2 -Wall -Wextra -Wno-unused-parameter -mwindows -static -static-libgcc \
  -ffile-prefix-map="$HOME"=/src -ffile-prefix-map="$HERE"=/src/agent \
  -Wl,--no-insert-timestamp -s \
  -o build/winagent.exe winagent.c -lws2_32 -lshell32 -ladvapi32 -luser32
if strings build/winagent.exe | grep -i -F -e "/Users/" -e "$(id -un)" -e "$HOME" >/dev/null; then
  echo "FAIL: host path or user name inside build/winagent.exe" >&2; exit 1
fi
ls -l build/winagent.exe | awk '{print $5, "bytes"}'
shasum -a 256 build/winagent.exe
