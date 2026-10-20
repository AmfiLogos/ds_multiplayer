#!/bin/zsh
# make_instance_steam.sh <loader checkout> <X>: a hidden Mac test copy WITH Steam (lane steamwin, live Steam tests only).
# Same as ds_modloader loader/test/make_instance.sh (APFS clone, stock GameAssembly 25846389, stock assets, own bundle id,
# LSBackgroundOnly + LSUIElement) but WITHOUT the Steam gate patch, so the game itself starts Steam as the Mac's signed-in
# account. lrig runs copies with HOME = <copy>/home: the Steam API finds the client through
# $HOME/Library/Application Support/Steam, so that one folder is a symlink to the real one (everything else of the copy
# stays in its own home). steam_appid.txt in the copy folder (lrig's cwd): no "restart through Steam".
set -e
LD=${1:A}; X=$2; [[ -d $LD/test && $X == [K-Z] ]] || { echo "usage: $0 <ds_modloader>/loader K..Z" >&2; exit 1; }
REAL="$HOME/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app"
I=$LD/test_env/instances/$X; APP=$I/DungeonSettlers.app; FW=$APP/Contents/Frameworks
mkdir -p $I
[[ -d $APP ]] || cp -c -R "$REAL" $APP
codesign -v "$REAL/Contents/Frameworks/GameAssembly.dylib.stock_25846389"
cp -c "$REAL/Contents/Frameworks/GameAssembly.dylib.stock_25846389" "$FW/GameAssembly.dylib.rig_tmp"
codesign -f -s - "$FW/GameAssembly.dylib.rig_tmp" 2>/dev/null
mv -f "$FW/GameAssembly.dylib.rig_tmp" "$FW/GameAssembly.dylib"
python3 - "$APP" <<'PY'
import plistlib, hashlib, os, shutil, sys
app = sys.argv[1]
real = os.path.expanduser('~/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app/Contents/_CodeSignature/CodeResources')
seal = plistlib.load(open(real, 'rb'))['files2']
stock = os.path.expanduser('~/ds_weaponslot/b25846389/assets/work/stock_aa')
sha = lambda p: hashlib.sha256(open(p, 'rb').read()).digest()
for rel, v in seal.items():
    if not isinstance(v, dict) or 'hash2' not in v: continue
    p = os.path.join(app, 'Contents', rel)
    if os.path.exists(p) and sha(p) == v['hash2']: continue
    cand = os.path.join(stock, os.path.basename(rel))
    if not os.path.exists(cand) or sha(cand) != v['hash2']: sys.exit(f'no verified stock copy for {rel}')
    shutil.copyfile(cand, p + '.rigtmp'); os.replace(p + '.rigtmp', p); print('restored stock', rel)
print('all sealed resources match Steam')
PY
mkdir -p $I/fw_unused
for f in $FW/*(N); do b=${f:t}; [[ $b == GameAssembly.dylib || $b == UnityPlayer.dylib ]] || mv "$f" $I/fw_unused/; done
[[ -f "$APP/Contents/MacOS/steam_appid.txt" ]] && mv "$APP/Contents/MacOS/steam_appid.txt" $I/steam_appid.txt.moved
PL=$APP/Contents/Info.plist
plutil -replace CFBundleIdentifier -string "com.CanOpener.Dungeon-Settlers.mp$X" $PL
plutil -replace CFBundleName -string "DS rig $X" $PL
plutil -replace LSBackgroundOnly -bool YES $PL
plutil -replace LSUIElement -bool YES $PL
codesign -f -s - "$APP/Contents/MacOS/Dungeon Settlers" 2>&1 | tail -1
mkdir -p $I/home/Library/"Application Support" $I/data/Saves
S=$I/home/Library/"Application Support"/Steam
[[ -L $S ]] || ln -s "$HOME/Library/Application Support/Steam" $S
printf '2798330\n' > $I/steam_appid.txt
[[ $(plutil -extract LSBackgroundOnly raw $PL) == true && $(plutil -extract LSUIElement raw $PL) == true ]] || { echo "copy $X lacks the hidden-app flags" >&2; exit 1; }
echo "Steam copy $X ready: $APP"
