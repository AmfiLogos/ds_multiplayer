#!/bin/zsh
# Lane build of the Mac dylib with the bridge: since the port onto main (2026-10-16) mp_bridge.c is in src/build.sh, so this
# only builds that into the lane folder: build/mac/dsrig.dylib (+ custom-modes dylibs). BRIDGE_SRC: another src folder.
set -e; L="${0:A:h}"; S=${BRIDGE_SRC:-$L/../../src}
OUT=$L/build/mac zsh $S/build.sh
