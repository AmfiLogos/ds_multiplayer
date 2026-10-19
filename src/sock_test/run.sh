#!/bin/zsh
# Host test of the control channel guard (no game). Output binary in a new temp folder.
cd "${0:A:h}/.." || exit 1
O=$(mktemp -d "${TMPDIR:-/tmp}/ctlguard_build.XXXXXX")
clang -O1 -Wall -o "$O/ctl_guard_test" sock_test/ctl_guard_test.c rig_sock.c -lpthread || exit 1
"$O/ctl_guard_test"
