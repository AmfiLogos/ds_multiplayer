#!/bin/zsh
# Package our arm64 MelonLoader build (inst.noindex/ml) as the release zip for the Mac launcher (mod kind "melon").
# Output: out.noindex/pkg/melonloader-mac-arm64-<ver>.zip + .sha256, manifest.json (sha256 of every file) inside.
# Only loader files: no mods, no generated interop (Il2CppAssemblies), no Cpp2IL / Unity libraries (MelonLoader
# downloads those itself at the first start), no logs.
set -e
L=${0:A:h:h}; I=$L/inst.noindex/ml; V=${1:-0.1.0}; N=melonloader-mac-arm64-$V
O=$L/out.noindex/pkg/$N; mkdir -p $L/out.noindex/pkg $L/old.noindex
[[ -e $O ]] && mv $O $L/old.noindex/pkg-$N-$(date +%Y%m%d-%H%M%S)
mkdir -p $O/licenses $O/UserData
cp -c -R $I/dotnet $O/
cp -c $I/MelonLoader.Bootstrap.dylib $O/
cp -c -R $I/MelonLoader $O/
for d in Il2CppAssemblies Logs; do [[ -e $O/MelonLoader/$d ]] && mv $O/MelonLoader/$d $L/old.noindex/pkgjunk-$d-$(date +%s); done
G=$O/MelonLoader/Dependencies/Il2CppAssemblyGenerator
for d in Cpp2IL UnityDependencies Il2CppInterop Config.cfg; do [[ -e $G/$d ]] && mv $G/$d $L/old.noindex/pkgjunk-$d-$(date +%s); done
[[ -e $O/MelonLoader/Latest.log ]] && mv $O/MelonLoader/Latest.log $L/old.noindex/pkgjunk-Latest-$(date +%s).log
mkdir -p $L/old.noindex/pkgjunk; find $O/MelonLoader -name '*.xml' -exec mv {} $L/old.noindex/pkgjunk/ \;
cp $I/UserData/Loader.cfg $O/UserData/
codesign -f -s - $O/MelonLoader.Bootstrap.dylib
S=$L/src.noindex
cp $S/ml/LICENSE.md $O/licenses/MelonLoader-Apache-2.0.txt; cp $S/ml/NOTICE.txt $O/licenses/MelonLoader-NOTICE.txt
cp $S/il2cppinterop/LICENSE $O/licenses/Il2CppInterop-LGPL-3.0.txt
cp $HOME/.dotnet/LICENSE.txt $O/licenses/dotnet-MIT.txt; cp $HOME/.dotnet/ThirdPartyNotices.txt $O/licenses/dotnet-ThirdPartyNotices.txt
cat > $O/licenses/SOURCE.txt <<SRC
Built from: MelonLoader alpha-development $(git -C $S/ml rev-parse --short HEAD) (https://github.com/LavaGang/MelonLoader) + our patches
(patches/melonloader-7c549cc.patch), Il2CppInterop $(git -C $S/il2cppinterop rev-parse --short HEAD) = our arm64 fork of BepInEx/Il2CppInterop
(ext/bepinexmac) packed as 1.5.3-dsmac.1, .NET runtime 6.0.36 osx-arm64 (Microsoft). Other libraries (HarmonyX, MonoMod,
Tomlet, AsmResolver, Iced, ...) come from MelonLoader's own NuGet references, unchanged.
Patches and build scripts: https://github.com/AmfiLogos/ds_multiplayer, ext/melon/ (patches/, tools/build_ml.sh, tools/package.sh).
SRC
(cd $O && python3 - "$V" <<'PY'
import hashlib, json, os, sys
files = {}
for root, _, names in os.walk('.'):
    for n in sorted(names):
        p = os.path.join(root, n)[2:]
        if p == 'manifest.json': continue
        files[p] = hashlib.sha256(open(p, 'rb').read()).hexdigest()
json.dump({'name': 'melonloader-mac-arm64', 'version': sys.argv[1], 'arch': 'arm64', 'melonloader': '0.7.4-alpha+7c549cc+ds',
           'bootstrap': 'MelonLoader.Bootstrap.dylib', 'il2cppinterop': '1.5.3-dsmac.1', 'files': files},
          open('manifest.json', 'w'), indent=1, sort_keys=True)
PY
)
(cd $L/out.noindex/pkg && ditto -c -k --keepParent --norsrc --noextattr --noqtn $N $N.zip && shasum -a 256 $N.zip > $N.zip.sha256)
ls -la $L/out.noindex/pkg/$N.zip; cat $L/out.noindex/pkg/$N.zip.sha256
