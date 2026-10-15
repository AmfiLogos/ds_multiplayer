#!/bin/zsh
# Package the arm64 BepInEx build (inst.noindex/bie) as a release zip for the Mac launcher.
# Output: out.noindex/bepinex-mac-arm64-<ver>.zip + .sha256 + manifest.json (sha256 of every file).
# Only loader files: no plugins, no config, no interop/cache (those are made per machine at first start).
set -e
L=${0:A:h:h}; I=$L/inst.noindex/bie; V=${1:-0.1.0}; N=bepinex-mac-arm64-$V
O=$L/out.noindex/pkg/$N; mkdir -p $L/out.noindex/pkg
[[ -e $O ]] && mv $O $L/old.noindex/pkg-$N-$(date +%Y%m%d-%H%M)
mkdir -p $O/BepInEx $O/licenses
cp -c -R $I/dotnet $O/
cp -c -R $I/BepInEx/core $O/BepInEx/
mkdir -p $L/old.noindex/pkgjunk; mv $O/BepInEx/core/libdobby_x64.dylib.orig $L/old.noindex/pkgjunk/ 2>/dev/null || true
find $O/BepInEx/core -name '*.xml' -exec mv {} $L/old.noindex/pkgjunk/ \;
cp $I/libdoorstop.dylib $O/
codesign -f -s - $O/libdoorstop.dylib $O/BepInEx/core/libdobby.dylib
S=$L/src.noindex
cp $S/bepinex/LICENSE $O/licenses/BepInEx-LGPL-2.1.txt
cp $S/il2cppinterop/LICENSE $O/licenses/Il2CppInterop-LGPL-3.0.txt
cp $S/doorstop/LICENSE $O/licenses/UnityDoorstop-LGPL-2.1.txt
cp $S/dobby/LICENSE $O/licenses/Dobby-Apache-2.0.txt
cp $I/dotnet/LICENSE.txt $O/licenses/dotnet-MIT.txt 2>/dev/null || cp $HOME/.dotnet/LICENSE.txt $O/licenses/dotnet-MIT.txt
cp $L/dl.noindex/bie788_macx64/changelog.txt $O/licenses/BepInEx-be.788-changelog.txt
cat > $O/licenses/SOURCE.txt <<SRC
Built from: BepInEx 5b766a3 + macOS PR branches + patches, Il2CppInterop 81a6f78 + PR branches + patches,
UnityDoorstop $(git -C $S/doorstop rev-parse --short HEAD), Dobby $(git -C $S/dobby rev-parse --short HEAD) (cdobbyn/macos-arm64-dobby-8b41),
.NET runtime 6.0.36 osx-arm64 (Microsoft). Other BepInEx core libraries (HarmonyX, MonoMod, Cpp2IL, AsmResolver, Iced...)
are the be.788 release files, unchanged. Patches and build script: https://github.com/AmfiLogos/ds_multiplayer
branch bepinex-mac, ext/bepinexmac/ (patches/, tools/build_install.sh).
SRC
(cd $O && python3 - "$V" <<'PY'
import hashlib, json, os, sys
files = {}
for root, _, names in os.walk('.'):
    for n in sorted(names):
        p = os.path.join(root, n)[2:]
        files[p] = hashlib.sha256(open(p, 'rb').read()).hexdigest()
json.dump({'name': 'bepinex-mac-arm64', 'version': sys.argv[1], 'arch': 'arm64', 'bepinex': '6.0.0-be.788+mac',
           'target_assembly': 'BepInEx/core/BepInEx.Unity.IL2CPP.dll', 'coreclr': 'dotnet/libcoreclr.dylib',
           'corlib_dir': 'dotnet', 'doorstop': 'libdoorstop.dylib', 'files': files},
          open('manifest.json', 'w'), indent=1, sort_keys=True)
PY
)
(cd $L/out.noindex/pkg && ditto -c -k --keepParent --norsrc --noextattr --noqtn $N $N.zip && shasum -a 256 $N.zip > $N.zip.sha256)
ls -la $L/out.noindex/pkg/$N.zip; cat $L/out.noindex/pkg/$N.zip.sha256
