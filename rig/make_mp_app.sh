#!/bin/zsh
# make_mp_app.sh [--portable] [path/to/dsrig.dylib]
# --portable: "Dungeon Settlers MP (LAN).app" + zip in rig/lan/ for the OWNER's second Mac (no Steam there):
#   Steam gate on (runs without Steam), arm64 only, own bundle id / save folder, starts with the two rig test saves.
#   Personal test copy only: never upload or share it (it contains game files).
# Builds "~/Applications/Dungeon Settlers MP.app" from YOUR OWN Steam install of Dungeon Settlers
# (build 25846389) plus the multiplayer mod (dsrig.dylib). Nothing is downloaded, no game file is shipped.
# The real install, your saves and your settings are only READ, never written.
#   - own bundle id com.CanOpener.Dungeon-Settlers.MP  -> own settings (prefs) domain
#   - own save folder ~/Library/Application Support/CanOpener/Dungeon Settlers MP/data
#     (the first start copies your saves into it)
#   - stock game code + Steam on (no Steam gate patch), sound on
set -e
PORTABLE=0; [[ $1 == --portable ]] && { PORTABLE=1; shift; }
REAL="$HOME/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app"
OUT="$HOME/Applications/Dungeon Settlers MP.app"
NAME="Dungeon Settlers MP"; BID=com.CanOpener.Dungeon-Settlers.MP; DIRNAME="Dungeon Settlers MP"
if (( PORTABLE )); then
  NAME="Dungeon Settlers MP (LAN)"; BID=com.CanOpener.Dungeon-Settlers.MPLAN; DIRNAME="Dungeon Settlers MP LAN"
  LANDIR="${LANDIR:-${0:A:h}/lan}"            # LANDIR=... builds elsewhere (never move an app bundle someone is running)
  OUT="$LANDIR/$NAME.app"; mkdir -p "$LANDIR"
fi
HERE="${0:A:h}"
DYLIB="${1:-$HERE/dsrig.dylib}"; [[ -f $DYLIB ]] || DYLIB="$HERE/../build/dsrig.dylib"
BUILD=25846389; UUID=3C5DD8BD-E371-3C0E-A674-A81042B0AC9F; TEAM=2DKWCPLJT6

die() { echo "ERROR: $*" >&2; exit 1; }
[[ -d $REAL ]] || die "Dungeon Settlers is not installed by Steam at: $REAL"
[[ -f $DYLIB ]] || die "dsrig.dylib not found (give its path as the first argument)"
# det_ranges.tsv (caller ranges for the deterministic random stream) must sit next to dsrig.dylib in the app;
# without it view code draws from the game's random stream and peers desync.
RANGES="${DYLIB:h}/det_ranges.tsv"; [[ -f $RANGES ]] || RANGES="$HERE/../build/det_ranges.tsv"
[[ -f $RANGES ]] || die "det_ranges.tsv not found next to dsrig.dylib"
[[ -n $(lsof -t +D "$OUT" 2>/dev/null) ]] && die "a process uses $OUT: build elsewhere (LANDIR=...) or quit it"
pgrep -f "$NAME.app/Contents/MacOS" >/dev/null && (( ! PORTABLE )) && die "Dungeon Settlers MP is running: quit it first"

# 1. a stock GameAssembly of build 25846389 (Steam-signed). A modded live file is skipped.
GA=""
for c in "$REAL/Contents/Frameworks/GameAssembly.dylib" "$REAL/Contents/Frameworks/GameAssembly.dylib.stock_$BUILD"; do
  [[ -f $c ]] || continue
  codesign -v "$c" 2>/dev/null || continue
  codesign -dv "$c" 2>&1 | grep -q "TeamIdentifier=$TEAM" || continue
  dwarfdump --uuid "$c" 2>/dev/null | grep -q "$UUID (arm64)" || continue
  GA=$c; break
done
[[ -n $GA ]] || die "no stock GameAssembly of build $BUILD found. Update the game in Steam (or use 'Verify integrity of game files')."
echo "stock GameAssembly: $GA"

# 2. build in a temporary folder next to the target, then move it in place
TMP="$OUT.building"; [[ -e $TMP ]] && mv "$TMP" "$TMP.old-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$TMP/Contents/MacOS" "$TMP/Contents/Frameworks"
C=$TMP/Contents
cp -c -R "$REAL/Contents/Resources" "$C/Resources"            # APFS clone: no extra disk space
cp -c -R "$REAL/Contents/PlugIns" "$C/PlugIns"
cp -c "$REAL/Contents/Info.plist" "$C/Info.plist"
cp -c "$REAL/Contents/MacOS/Dungeon Settlers" "$C/MacOS/Dungeon Settlers"
cp -c "$REAL/Contents/Frameworks/UnityPlayer.dylib" "$C/Frameworks/UnityPlayer.dylib"
cp -c "$GA" "$C/Frameworks/GameAssembly.dylib"
cp "$DYLIB" "$C/Frameworks/dsrig.dylib"
cp "$RANGES" "$C/Resources/det_ranges.tsv"
ln -s ../Resources/det_ranges.tsv "$C/Frameworks/det_ranges.tsv"   # data file: a plain file in Frameworks breaks codesign
xattr -c "$C/Frameworks/dsrig.dylib"      # a downloaded kit is quarantined; macOS would block the mod
# dsrig.dylib is built on macOS 27 with minos 27.0: dyld refuses an inserted library newer than the running
# macOS and the game never starts. It only imports old libSystem/CF/objc symbols, so declare macOS 12 (M1 era).
MINOS=$(xcrun vtool -show-build "$C/Frameworks/dsrig.dylib" | awk '/minos/{print $2; exit}')
if [[ ${MINOS%%.*} -gt 12 ]]; then
  xcrun vtool -set-build-version macos 12.0 ${MINOS} -replace -output "$C/Frameworks/dsrig.dylib.v12" "$C/Frameworks/dsrig.dylib"
  mv "$C/Frameworks/dsrig.dylib.v12" "$C/Frameworks/dsrig.dylib"
  codesign -f -s - "$C/Frameworks/dsrig.dylib"
  echo "dsrig.dylib minos $MINOS -> 12.0 (code unchanged)"
fi
# old asset backups some mod tools leave in Data/ are not part of the game: keep them out
EXTRAS="$HOME/Library/Application Support/CanOpener/$DIRNAME/not_used_game_backups"
for f in "$C"/Resources/Data/*.pre_*(N) "$C"/Resources/Data/*.weaponslot_*(N); do mkdir -p "$EXTRAS"; mv "$f" "$EXTRAS/"; done

# 3. every resource must match Steam's own seal; mod-changed files are put back from verified stock copies
python3 - "$REAL" "$C" <<'PY'
import plistlib, hashlib, os, shutil, sys
real, dst = sys.argv[1], sys.argv[2]
seal = plistlib.load(open(os.path.join(real, 'Contents/_CodeSignature/CodeResources'), 'rb'))['files2']
stock_dirs = [os.path.expanduser('~/ds_weaponslot/b25846389/assets/work/stock_aa')]
sha = lambda p: hashlib.sha256(open(p, 'rb').read()).digest()
bad = []
for rel, v in seal.items():
    if not isinstance(v, dict) or 'hash2' not in v or not rel.startswith('Resources/'): continue
    p = os.path.join(dst, rel)
    if os.path.exists(p) and sha(p) == v['hash2']: continue
    for d in stock_dirs:
        c = os.path.join(d, os.path.basename(rel))
        if os.path.exists(c) and sha(c) == v['hash2']:
            shutil.copyfile(c, p); print('restored stock', rel); break
    else:
        bad.append(rel)
if bad:
    sys.exit('these game files are modded and no stock copy was found: ' + ', '.join(bad) +
             "\nUse Steam 'Verify integrity of game files', then run this script again.")
print('all game resources match Steam')
PY

# 4. launcher = the bundle's executable: a signed arm64 binary (mp_launcher.c). A zsh script here never
#    started on a quarantined copy (AirDrop) on the owner's second Mac. Prebuilt "mp_launcher" next to this
#    script is used if present (the friend kit ships it), else it is compiled.
if [[ -f "$HERE/mp_launcher" ]]; then cp "$HERE/mp_launcher" "$C/MacOS/DS MP Launcher"
else clang -arch arm64 -O2 -Wall -mmacosx-version-min=11.0 -o "$C/MacOS/DS MP Launcher" "$HERE/mp_launcher.c"; fi
xattr -c "$C/MacOS/DS MP Launcher"
printf 'folder=%s\nsteam=%d\n' "$DIRNAME" $(( PORTABLE ? 0 : 1 )) > "$C/Resources/mp_launcher.conf"

PL=$C/Info.plist
plutil -replace CFBundleIdentifier -string "$BID" $PL
plutil -replace CFBundleName -string "$NAME" $PL

if (( PORTABLE )); then
  # arm64 only (the mod is arm64 only); Steam gate on, like the rig copies
  for f in "$C/MacOS/Dungeon Settlers" "$C/Frameworks/UnityPlayer.dylib" "$C/Frameworks/GameAssembly.dylib"; do
    lipo "$f" -thin arm64 -output "$f.arm64" && mv "$f.arm64" "$f"
  done
  python3 - "$C/Frameworks/GameAssembly.dylib" <<'PY'
import sys
p = sys.argv[1]; b = bytearray(open(p, 'rb').read())
va = 0xD25730   # StoreRuntimeHandler.SteamRuntime.get_IsEnabled: mov w0,#1 -> mov w0,#0 (thin arm64: file offset = VA)
assert b[va:va + 8].hex() == '20008052c0035fd6', b[va:va + 8].hex()
b[va:va + 4] = bytes.fromhex('00008052'); open(p, 'wb').write(b); print('Steam gate on')
PY
  codesign -f -s - "$C/Frameworks/GameAssembly.dylib" "$C/Frameworks/UnityPlayer.dylib"
  # uniform signing: the game's PlugIns keep the developer's Developer ID + hardened runtime. A bundle mixing
  # that with ad-hoc code hung at _dyld_start on the second Mac (syspolicyd notarization / XProtect wait).
  # Thin to arm64 and re-sign every plugin ad-hoc, inside-out: Mach-O files first, then the bundle folders.
  find "$C/PlugIns" -type f -print0 | while IFS= read -r -d '' f; do
    file "$f" | grep -q "Mach-O" || continue
    if lipo -archs "$f" | grep -q " "; then lipo "$f" -thin arm64 -output "$f.arm64" && mv "$f.arm64" "$f"; fi
    codesign -f -s - "$f"
  done
  for b in "$C"/PlugIns/*.bundle(N); do [[ -d $b ]] && codesign -f -s - "$b"; done
  # starting saves: the two rig test saves + OutGame/UserSetting from the rig seed (not his live folder)
  SEED="$HOME/DungeonSettlers_SaveBackups/2026-10-11-1827"
  mkdir -p "$C/Resources/mp_seed/Saves"
  cp "$SEED"/Saves/*.json "$C/Resources/mp_seed/Saves/"
  cp "$SEED/OutGame.json" "$SEED/UserSetting.json" "$C/Resources/mp_seed/"
fi
plutil -replace CFBundleExecutable -string "DS MP Launcher" $PL

# 5. ad-hoc signatures (the game binary loses Steam's hardened runtime so the mod can load)
codesign -v "$C/Frameworks/dsrig.dylib" 2>/dev/null || codesign -f -s - "$C/Frameworks/dsrig.dylib"   # keep the gated bytes when already signed
codesign -f -s - "$C/MacOS/Dungeon Settlers"
codesign -f -s - -i com.CanOpener.Dungeon-Settlers.MP.launcher "$C/MacOS/DS MP Launcher"
nice -n 10 codesign -f -s - "$TMP"
codesign -v "$TMP" && echo "signature ok"

OLD="${OUT:h}/old_versions"
[[ -e $OUT ]] && { mkdir -p "$OLD"; mv "$OUT" "$OLD/$NAME-$(date +%Y-%m-%d-%H%M%S).app"; }
mv "$TMP" "$OUT"
echo "built: $OUT"
if (( PORTABLE )); then
  Z="$LANDIR/DungeonSettlersMP_LAN.zip"
  [[ -e $Z ]] && { mkdir -p "$OLD"; mv "$Z" "$OLD/DungeonSettlersMP_LAN-$(date +%Y-%m-%d-%H%M%S).zip"; }
  cp "${0:A:h}/lan_README.txt" "$LANDIR/README.txt"
  (cd "$LANDIR" && nice -n 10 ditto -c -k --sequesterRsrc --keepParent "$NAME.app" "$Z.tmp" && zip -q "$Z.tmp" README.txt && mv "$Z.tmp" "$Z")
  ls -la "$Z"
fi
