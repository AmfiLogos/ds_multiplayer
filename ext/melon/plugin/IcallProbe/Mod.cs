using System;
using System.Runtime.InteropServices;
using Il2CppInterop.Runtime;
using MelonLoader;

[assembly: MelonInfo(typeof(IcallProbe.Mod), "IcallProbe", "1.0.0", "ds_multiplayer")]
[assembly: MelonGame(null, null)]

namespace IcallProbe;

/// Lane probe: resolves the icall "UnityEngine.Input::get_inputString()" the way native code does (il2cpp_resolve_icall)
/// and calls the pointer it gets, after the title (OnSceneWasInitialized #3). Lines start with ICALLPROBE.
public class Mod : MelonMod
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr Fn();
    static int scenes;
    public override void OnSceneWasInitialized(int buildIndex, string sceneName)
    {
        if (++scenes < 2) return;
        IntPtr p = IL2CPP.il2cpp_resolve_icall("UnityEngine.Input::get_inputString()");
        LoggerInstance.Msg($"ICALLPROBE resolve -> 0x{(long)p:X}");
        if (p == IntPtr.Zero) return;
        LoggerInstance.Msg("ICALLPROBE calling it");
        IntPtr s = Marshal.GetDelegateForFunctionPointer<Fn>(p)();
        LoggerInstance.Msg($"ICALLPROBE returned 0x{(long)s:X}");
    }
}
