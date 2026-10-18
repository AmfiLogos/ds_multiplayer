#!/bin/zsh
# "Safe when absent" test: DsmpBridge.dll and DsmpLite with no / old (api 1) / new (api 2) multiplayer native, and a mod
# built against DsmpBridge.dll running WITHOUT it (soft dependency). Plain dotnet console, no game. Exit 1 = a real break.
# Output: build/absent/ (gitignored). Needs ~/.dotnet/dotnet and clang.
set -e; T=${0:A:h}; L=${T:h:h}; O=$L/build/absent; DN=~/.dotnet/dotnet
mkdir -p $O/v1 $O/v2 $O/plugin
for v in 1 2; do clang -arch arm64 -dynamiclib -O1 -DSTUB_API=$v -install_name @rpath/multiplayer.dylib -o $O/v$v/multiplayer.dylib $T/stub.c; done
(cd $L/plugin/DsmpBridge && nice -n 19 $DN build -c Release -o $O/plugin 2>&1 | grep -E " error |rror CS" | sort -u) || true
[[ -f $O/plugin/DsmpBridge.dll ]] || { echo "DsmpBridge.dll did not build" >&2; exit 1; }
(cd $T && nice -n 19 $DN build -c Release -p:BridgeDll=$O/plugin/DsmpBridge.dll -o $O/app 2>&1 | grep -E "rror CS" | sort -u) || true
[[ -f $O/app/AbsentTest.dll ]] || { echo "AbsentTest did not build" >&2; exit 1; }
# the same app with DsmpBridge.dll taken away (moved, never deleted)
mkdir -p $O/app_nobridge $O/old; [[ -d $O/app_nobridge ]] && mv $O/app_nobridge $O/old/app_nobridge-$(date +%s); cp -R $O/app $O/app_nobridge
mv $O/app_nobridge/DsmpBridge.dll $O/old/DsmpBridge.dll.$(date +%s)
rc=0
$DN $O/app/AbsentTest.dll none || rc=1
$DN $O/app/AbsentTest.dll v1 $O/v1/multiplayer.dylib || rc=1
$DN $O/app/AbsentTest.dll v2 $O/v2/multiplayer.dylib || rc=1
$DN $O/app_nobridge/AbsentTest.dll nobridge || rc=1
echo "absent test: $([[ $rc == 0 ]] && echo PASS || echo FAIL)"; exit $rc
