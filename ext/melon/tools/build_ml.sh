#!/bin/zsh
# Rebuild our arm64 MelonLoader (Mac, Apple chips) into inst.noindex/ml. nice 19, 4 threads.
# Sources (not committed, *.noindex):
#   src.noindex/il2cppinterop = our Il2CppInterop fork (ext/bepinexmac, e7c52f6 + patches/il2cppinterop-*.patch), packed as
#                               1.5.3-dsmac.1 into feed.noindex (local NuGet feed);
#   src.noindex/ml            = MelonLoader alpha-development 7c549cc + patches/melonloader-7c549cc.patch (local feed,
#                               Il2CppInteropVersion 1.5.3-dsmac.1, 6 DS patches: see NOTES.md).
# Layout of the install: MelonLoader.Bootstrap.dylib, MelonLoader/, dotnet/ (.NET 6.0.36 osx-arm64 runtime), UserData/Loader.cfg.
set -e
L=${0:A:h:h}; S=$L/src.noindex; I=$L/inst.noindex/ml; D=$HOME/.dotnet/dotnet; V=1.5.3-dsmac.1
export DOTNET_CLI_TELEMETRY_OPTOUT=1
PM=(-p:PathMap=$S=/src -p:ContinuousIntegrationBuild=true)          # no local paths in the shipped DLLs / PDBs
mkdir -p $L/feed.noindex $L/build.noindex $L/old.noindex
(cd $S/il2cppinterop && for p in Common Runtime HarmonySupport Generator; do       # build (every target), then pack
   nice -n 19 $D build Il2CppInterop.$p/Il2CppInterop.$p.csproj -c Release -m:4 -p:Version=$V $PM -p:GeneratePackageOnBuild=false >$L/build.noindex/build_$p.log
   nice -n 19 $D pack Il2CppInterop.$p/Il2CppInterop.$p.csproj -c Release --no-build -p:Version=$V $PM -o $L/feed.noindex >$L/build.noindex/pack_$p.log; done)
# a package version is cached once per machine: drop the cached copy so a rebuilt feed is used
for p in common runtime harmonysupport generator; do
  [[ -d $HOME/.nuget/packages/il2cppinterop.$p/$V ]] && mv $HOME/.nuget/packages/il2cppinterop.$p/$V $L/old.noindex/nuget-$p-$V-$(date +%Y%m%d-%H%M%S)
done
(cd $S/ml && nice -n 19 $D build -p:Platform=arm64 -p:ForceRID=osx-arm64 -c Release -m:4 $PM >$L/build.noindex/ml_build.log && \
   git checkout MelonLoader.Bootstrap/osxexports.def)                        # the build appends to it
O=$S/ml/Output/Release/osx-arm64
mkdir -p $I/UserData
[[ -d $I/MelonLoader ]] && mv $I/MelonLoader $L/old.noindex/inst-MelonLoader-$(date +%Y%m%d-%H%M%S)
cp -c -R $O/MelonLoader $I/
cp -c $O/MelonLoader.Bootstrap.dylib $I/MelonLoader.Bootstrap.dylib.tmp && mv -f $I/MelonLoader.Bootstrap.dylib.tmp $I/MelonLoader.Bootstrap.dylib
[[ -d $I/dotnet ]] || { mkdir -p $I/dotnet/shared/Microsoft.NETCore.App $I/dotnet/host/fxr
  cp -c -R $HOME/.dotnet/shared/Microsoft.NETCore.App/6.0.36 $I/dotnet/shared/Microsoft.NETCore.App/
  cp -c -R $HOME/.dotnet/host/fxr/6.0.36 $I/dotnet/host/fxr/; cp $HOME/.dotnet/LICENSE.txt $HOME/.dotnet/ThirdPartyNotices.txt $I/dotnet/ 2>/dev/null || true; }
print -r -- $'[loader]\ndisable_start_screen = true\n\n[console]\nhide_console = true' > $I/UserData/Loader.cfg
echo "install ready: $I"
