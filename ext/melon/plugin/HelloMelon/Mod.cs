using System;
using HarmonyLib;
using Il2CppInterop.Runtime.Injection;
using MelonLoader;
using UnityEngine;

[assembly: MelonInfo(typeof(HelloMelon.Mod), "HelloMelon", "1.0.0", "ds_multiplayer")]
[assembly: MelonGame(null, null)]

namespace HelloMelon;

/// Lane test mod: logs on load, Harmony-patches two game methods (Assembly-CSharp) and adds an injected
/// MonoBehaviour (Il2CppInterop class injection) that logs every 300 frames. Lines start with HELLOMELON.
public class Mod : MelonMod
{
    internal static MelonLogger.Instance L;
    static int sceneLoads, titleCalls;
    static bool added;

    public override void OnInitializeMelon()
    {
        L = LoggerInstance;
        L.Msg($"HELLOMELON load on {System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture}");
        Patch("Reporter", "_OnLevelWasLoaded", nameof(OnLevelPostfix));
        Patch("Refactor.TitleScenePreviewController", "GetPercentOfDay", nameof(TitlePostfix));
        ClassInjector.RegisterTypeInIl2Cpp<Ticker>();
        L.Msg("HELLOMELON Ticker type registered");
    }

    public override void OnSceneWasInitialized(int buildIndex, string sceneName)
    {
        if (added) return;
        added = true;
        var go = new GameObject("HelloMelonTicker");
        UnityEngine.Object.DontDestroyOnLoad(go);
        go.AddComponent<Ticker>();
        L.Msg($"HELLOMELON Ticker component added (scene {sceneName})");
    }

    void Patch(string type, string method, string postfix)
    {
        // ML names game types Il2Cpp<namespace>.<type>, or Il2Cpp.<type> for the global namespace
        var t = AccessTools.TypeByName("Il2Cpp" + type) ?? AccessTools.TypeByName("Il2Cpp." + type) ?? AccessTools.TypeByName(type);
        var m = t == null ? null : AccessTools.Method(t, method);
        if (m == null) { L.Warning($"HELLOMELON PATCH-MISS {type}.{method}"); return; }
        HarmonyInstance.Patch(m, postfix: new HarmonyMethod(typeof(Mod), postfix));
        L.Msg($"HELLOMELON PATCHED {t.FullName}.{method}");
    }

    static void OnLevelPostfix() => L.Msg($"HELLOMELON PATCH-HIT Reporter._OnLevelWasLoaded #{++sceneLoads}");

    static void TitlePostfix(ref float __result)
    {
        if (++titleCalls % 600 == 1) L.Msg($"HELLOMELON PATCH-HIT GetPercentOfDay #{titleCalls} -> {__result}");
    }
}

public class Ticker : MonoBehaviour
{
    public Ticker(IntPtr ptr) : base(ptr) { }
    int frames;
    void Update()
    {
        if (++frames % 300 == 0) Mod.L.Msg($"HELLOMELON TICK injected Update frame {frames}, time {Time.realtimeSinceStartup:F1}");
    }
}
