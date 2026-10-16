using System;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using HarmonyLib;
using MelonLoader;

[assembly: MelonInfo(typeof(DSMelonGuard.Plugin), "DS loader guard", "1.0.0", "Dungeon Settlers Modded")]
[assembly: MelonGame(null, null)]

namespace DSMelonGuard;

/// MelonLoader answers il2cpp_resolve_icall for an icall the game stripped with a managed stand-in (its ICall injector).
/// That is meant for C# mods (Il2CppInterop's unstripped Unity methods). A native mod (the DS loader's mods, written in C)
/// that resolves the same icall gets the stand-in too, and calling it from C code kills the game (.NET fatal error in
/// JIT_ReversePInvokeEnterRare on Mac, 0x80131506 on Windows). This plugin keeps il2cpp's own answer (null for a
/// stripped icall, which the native mods handle) when no managed code asked: only C# callers get the stand-in.
public class Plugin : MelonPlugin
{
    const string Injector = "MelonLoader.Fixes.Il2CppInterop.Il2CppICallInjector";
    static int nativeAsks, managedAsks;

    public override void OnPreInitialization()
    {
        var t = typeof(MelonMod).Assembly.GetType(Injector);
        var should = t?.GetMethod("ShouldInject", BindingFlags.NonPublic | BindingFlags.Static);
        if (should == null) { LoggerInstance.Warning("ICall injector not found: guard off"); return; }
        HarmonyInstance.Patch(should, prefix: new HarmonyMethod(typeof(Plugin).GetMethod(nameof(Prefix), BindingFlags.NonPublic | BindingFlags.Static)));
        LoggerInstance.Msg("native icall guard on");
    }

    /// ShouldInject(ref string requestedSignature, out MethodInfo unityShimMethod): false = no stand-in (null to the caller)
    static bool Prefix(ref string requestedSignature, ref MethodInfo unityShimMethod, ref bool __result)
    {
        var st = new StackTrace(1, false);
        for (int i = 0; i < st.FrameCount; i++)
        {
            var dt = st.GetFrame(i)?.GetMethod()?.DeclaringType;   // IL stubs (ILStubClass) are the native->managed entry
            if (dt != null && dt.FullName != Injector && dt.FullName != "ILStubClass" && !dt.FullName.StartsWith("DSMelonGuard"))
            {
                if (managedAsks++ < 3) MelonLogger.Msg($"[DS loader guard] C# caller for {requestedSignature}: {dt.FullName}");
                return true;                         // a C# caller: MelonLoader's injector as usual
            }
        }
        unityShimMethod = null; __result = false;
        if (nativeAsks++ < 20) MelonLogger.Msg($"[DS loader guard] native code asked for the stripped icall {requestedSignature}: left null");
        return false;
    }
}
