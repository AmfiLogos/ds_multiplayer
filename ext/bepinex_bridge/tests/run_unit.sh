#!/bin/zsh
set -e; cd "${0:A:h}"; mkdir -p ../build
nice -n 19 clang -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -I../src -I../src/net -o ../build/test_bridge test_bridge.c ../src/mp_bridge.c
../build/test_bridge
