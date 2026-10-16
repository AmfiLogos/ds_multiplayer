using System;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using MelonLoader;

[assembly: MelonInfo(typeof(DSMelonGuard.Plugin), "DS loader guard", "1.0.0", "Dungeon Settlers Modded")]
[assembly: MelonGame(null, null)]

namespace DSMelonGuard;

/// MelonLoader answers il2cpp_resolve_icall for an icall the game stripped with a managed stand-in (its ICall injector).
/// That is meant for C# mods (Il2CppInterop's unstripped Unity methods). A native mod (the DS loader's mods, written in C)
/// that resolves the same icall gets the stand-in too, and calling it from C code kills the game (.NET fatal error:
/// 0x80131506 on Windows). This plugin puts its own hook in front of MelonLoader's: a native caller gets il2cpp's own
/// answer (null for a stripped icall, which the native mods handle); a C# caller goes on to MelonLoader's injector.
public class Plugin : MelonPlugin
{
    const string Injector = "MelonLoader.Fixes.Il2CppInterop.Il2CppICallInjector";
    delegate IntPtr Resolve(IntPtr signature);
    static Resolve ours;                                     // kept alive: native code calls it
    static Func<IntPtr, IntPtr> mlDetour, original;
    static int nativeAsks;

    public override void OnPreInitialization()
    {
        try
        {
            var t = typeof(MelonMod).Assembly.GetType(Injector);
            var field = t?.GetField("il2cpp_resolve_icall_hook", BindingFlags.NonPublic | BindingFlags.Static);
            var detour = t?.GetMethod("il2cpp_resolve_icall_Detour", BindingFlags.NonPublic | BindingFlags.Static);
            object mlHook = field?.GetValue(null);
            if (mlHook == null || detour == null) { LoggerInstance.Warning("ICall injector not active: guard off"); return; }
            var hookType = mlHook.GetType();                  // NativeHook<dil2cpp_resolve_icall>
            var target = (IntPtr)hookType.GetProperty("Target").GetValue(mlHook);
            mlDetour = (Func<IntPtr, IntPtr>)Delegate.CreateDelegate(typeof(Func<IntPtr, IntPtr>), detour);
            hookType.GetMethod("Detach").Invoke(mlHook, null);
            ours = Detour;
            object hook = Activator.CreateInstance(hookType, target, Marshal.GetFunctionPointerForDelegate(ours));
            hookType.GetMethod("Attach").Invoke(hook, null);
            var tramp = (Delegate)hookType.GetProperty("Trampoline").GetValue(hook);
            original = (Func<IntPtr, IntPtr>)Delegate.CreateDelegate(typeof(Func<IntPtr, IntPtr>), tramp, "Invoke");
            field.SetValue(null, hook);                       // MelonLoader's detour calls the original through it
            LoggerInstance.Msg("native icall guard on");
        }
        catch (Exception e) { LoggerInstance.Error($"guard off: {e}"); }
    }

    static IntPtr Detour(IntPtr signature)
    {
        var st = new StackTrace(1, false);
        for (int i = 0; i < st.FrameCount; i++)
        {
            var dt = st.GetFrame(i)?.GetMethod()?.DeclaringType;   // IL stubs (ILStubClass) are the native->managed entry
            if (dt != null && dt.FullName != "ILStubClass" && !dt.FullName.StartsWith("DSMelonGuard"))
                return mlDetour(signature);                  // a C# caller: MelonLoader's injector as usual
        }
        IntPtr r = original(signature);
        if (r == IntPtr.Zero && nativeAsks++ < 20)
            MelonLogger.Msg($"[DS loader guard] native code asked for the stripped icall {Marshal.PtrToStringAnsi(signature)}: left null");
        return r;
    }
}
