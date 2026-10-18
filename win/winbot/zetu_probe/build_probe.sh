#!/bin/zsh
# The loader's test module zetu-probe (loader/test/zetu_probe.c, TEST ONLY, never shipped) built for win-x64, for
# win_checklist.py item 2. Source and SDK header come from the loader repo (default ~/ds_launcher at origin/int014).
# Output: build/zetu-probe/{zetu-probe.dll, mod.json}
set -e; cd ${0:A:h}
L=${DSL_REPO:-~/ds_launcher}; REF=${DSL_REF:-origin/int014}
mkdir -p build/src build/zetu-probe
git -C $L show ${REF}:loader/test/zetu_probe.c > build/src/zetu_probe.c
git -C $L show ${REF}:loader/sdk/dsl_sdk.h > build/src/dsl_sdk.h
~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang -O2 -Wall -Wno-unused-function -D__USE_MINGW_ANSI_STDIO=1 -ffile-prefix-map=$HOME=/src \
  -Ibuild/src -shared -Wl,--no-insert-timestamp -o build/zetu-probe/zetu-probe.dll build/src/zetu_probe.c -Wl,--export-all-symbols -static -static-libgcc
cat > build/zetu-probe/mod.json <<'J'
{ "id": "zetu-probe", "name": "zetu probe (test)", "version": "0.0.1", "kind": "code", "api": 2, "scope": "client", "hidden": true,
  "game_builds": ["25846389"], "platforms": { "win-x64": { "entry": "zetu-probe.dll" } } }
J
echo "built build/zetu-probe ($(git -C $L rev-parse --short $REF))"
