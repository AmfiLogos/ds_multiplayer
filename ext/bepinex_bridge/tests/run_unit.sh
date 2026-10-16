#!/bin/zsh
set -e; cd "${0:A:h}"; mkdir -p ../build; S=${BRIDGE_SRC:-../../../src}
nice -n 19 clang -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -I$S -I$S/net -o ../build/test_bridge test_bridge.c $S/mp_bridge.c
../build/test_bridge
