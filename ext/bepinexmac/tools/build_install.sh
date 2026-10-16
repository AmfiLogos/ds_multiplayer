#!/bin/zsh
# Rebuild the arm64 BepInEx 6 IL2CPP install (inst.noindex/bie) from the lane forks. nice 19, 4 threads.
# Sources: src.noindex/{bepinex,il2cppinterop,doorstop,dobby} (upstream + merged PR branches + patches/, see NOTES.md).
# Base layout (config, core libs, x64 dotnet extras) comes from the be.788 macos-x64 zip in dl.noindex.
set -e
L=${0:A:h:h}; S=$L/src.noindex; I=$L/inst.noindex/bie; D=$HOME/.dotnet/dotnet
export DOTNET_CLI_TELEMETRY_OPTOUT=1 PATH=$HOME/Library/Python/3.10/bin:$PATH
mkdir -p $L/build.noindex $I/dotnet
[[ -d $I/BepInEx ]] || cp -c -R $L/dl.noindex/bie788_macx64/BepInEx $I/
cp -c -R $HOME/.dotnet/shared/Microsoft.NETCore.App/6.0.36/ $I/dotnet/          # arm64 .NET 6 runtime
for f in Microsoft.Bcl.AsyncInterfaces Microsoft.Extensions.DependencyInjection.Abstractions \
         Microsoft.Extensions.DependencyInjection Microsoft.Extensions.Logging.Abstractions Microsoft.Extensions.Logging \
         Microsoft.Extensions.Options Microsoft.Extensions.Primitives; do
  cp $L/dl.noindex/bie788_macx64/dotnet/$f.dll $I/dotnet/
done
(cd $S/doorstop && nice -n 19 clang -arch arm64 -shared -Os -fPIC -Isrc src/*.c src/config/*.c src/util/*.c \
   src/runtimes/*.c src/nix/*.c src/nix/plthook/plthook_osx.c -ldl -o $I/libdoorstop.dylib)
(cd $S/dobby && cmake -G Ninja -DCMAKE_SYSTEM_PROCESSOR=arm64 -DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_OSX_ARCHITECTURES=arm64 \
   -DCMAKE_BUILD_TYPE=Release -DDynamicBinaryInstrument=OFF -DPlugin.SymbolResolver=OFF \
   "-DCMAKE_C_FLAGS=-ffile-prefix-map=$S/dobby=/src/dobby" "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$S/dobby=/src/dobby" \
   -B buildarm64_pm >/dev/null && \
   nice -n 19 ninja -j4 -C buildarm64_pm dobby >/dev/null && cp buildarm64_pm/libdobby.dylib $I/BepInEx/core/libdobby.dylib)   # no local paths
(cd $S/bepinex && nice -n 19 $D build Runtimes/Unity/BepInEx.Unity.IL2CPP/BepInEx.Unity.IL2CPP.csproj -c Release -m:4 \
   -p:GeneratePackageOnBuild=false >$L/build.noindex/bepinex_build.log && \
   cp Runtimes/Unity/BepInEx.Unity.IL2CPP/bin/Unity.IL2CPP/BepInEx*.dll $I/BepInEx/core/)
(cd $S/il2cppinterop && nice -n 19 $D build Il2CppInterop.HarmonySupport/Il2CppInterop.HarmonySupport.csproj -c Release -m:4 \
   -p:GeneratePackageOnBuild=false >$L/build.noindex/iil_build.log && \
   cp bin/Il2CppInterop.HarmonySupport/net6.0/Il2CppInterop.{Runtime,HarmonySupport,Common}.dll $I/BepInEx/core/)
echo "install ready: $I"
