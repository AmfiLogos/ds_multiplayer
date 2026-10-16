#!/bin/sh
# HTTP inside the game, without the game: which Mac backend of custom/units/wk_http.c works when our code is injected into a
# process signed like Dungeon Settlers (hardened runtime; disable-library-validation, allow-dyld-environment-variables,
# disable-executable-page-protection, read from the installed game when present). Output: one line per backend and
# thread. DSMP_HTTP=curl / cf forces one backend for wkl_resolve.
set -e
D=$(cd "$(dirname "$0")" && pwd); U="$D/../.."; C="$D/../../.."; O="$D/../../../../build/units/http"; mkdir -p "$O"
G="$HOME/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app"
if [ -d "$G" ]; then codesign -d --entitlements :- "$G" > "$O/game.entitlements" 2>/dev/null; echo "entitlements: from the installed game"
else cat > "$O/game.entitlements" <<'PL'
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>com.apple.security.cs.allow-dyld-environment-variables</key><true/>
<key>com.apple.security.cs.disable-executable-page-protection</key><true/><key>com.apple.security.cs.disable-library-validation</key><true/></dict></plist>
PL
echo "entitlements: the game's three (game not installed)"; fi
nice -n 5 cc -O1 -Wall -o "$O/http_host" "$D/http_host.c" -framework CoreFoundation -framework CFNetwork -framework AppKit
codesign -f -s - --options runtime --entitlements "$O/game.entitlements" "$O/http_host" 2>/dev/null
nice -n 5 cc -O1 -Wall -dynamiclib -I"$U" -I"$C" -I"$C/fe" -o "$O/probe.dylib" "$D/probe_dylib.c" "$U/wk_link.c" "$U/wk_http.c" "$U/pvp_wiki.c" "$C/fe/fe_json.c" \
  -framework CoreFoundation -framework CFNetwork -lz -lm
codesign -f -s - "$O/probe.dylib" 2>/dev/null
codesign -dv "$O/http_host" 2>&1 | grep -o "flags=[^ ]*" | sed 's/^/host signed: /'
DYLD_INSERT_LIBRARIES="$O/probe.dylib" "$O/http_host"
