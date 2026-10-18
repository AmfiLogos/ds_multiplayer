#!/bin/zsh
# Build DsmpBridge.dll and the loader mod folder mods/dsmp-bridge/ (DLL + mod.json with its sha256; L3 loader format:
# kind bepinex, scope gameplay, hidden = a library for other mods, no switch). The loader copies it to BepInEx\plugins\dsmp-bridge\.
# Release builds: ds_modloader loader/modules/build_bridge.sh (pinned tree, same bytes). This one is for lane tests.
set -e; L="${0:A:h:h}"; cd $L/plugin/DsmpBridge
nice -n 19 ~/.dotnet/dotnet build -c Release -o $L/build/plugins | grep -E " error|Build succeeded" | sort -u
mkdir -p $L/mods/dsmp-bridge; cp $L/build/plugins/DsmpBridge.dll $L/mods/dsmp-bridge/
V=$(sed -n 's:.*<Version>\(.*\)</Version>.*:\1:p' $L/plugin/DsmpBridge/DsmpBridge.csproj | head -1)
H=$(shasum -a 256 $L/mods/dsmp-bridge/DsmpBridge.dll | cut -d' ' -f1)
printf '{"id":"dsmp-bridge","name":"DSMP Bridge","version":"%s","kind":"bepinex","scope":"gameplay","hidden":true,\n "platforms":{"any":{"files":{"DsmpBridge.dll":"%s"}}}}\n' $V $H > $L/mods/dsmp-bridge/mod.json
echo "mod folder $L/mods/dsmp-bridge (DsmpBridge.dll sha256 $H)"
