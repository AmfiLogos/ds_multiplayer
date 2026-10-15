using System;
using BepInEx;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;
using UnityEngine;

namespace HelloArm64;

/// Lane test plugin: logs on load, Harmony-patches two game methods (Assembly-CSharp), and adds an injected
/// MonoBehaviour (Il2CppInterop class injection) that logs every 300 frames.
[BepInPlugin("dsmp.helloarm64", "HelloArm64", "1.0.0")]
public class Plugin : BasePlugin
{
    internal static ManualLogSource L;
    internal static int SceneLoads, TitleCalls;

    public override void Load()
    {
        L = Log;
        L.LogInfo($"HELLO from {System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture} plugin");
        var h = new Harmony("dsmp.helloarm64");
        Patch(h, "Reporter", "_OnLevelWasLoaded", nameof(OnLevelPostfix));
        Patch(h, "Refactor.TitleScenePreviewController", "GetPercentOfDay", nameof(TitlePostfix));
        AddComponent<Ticker>();
        L.LogInfo("Ticker component added");
    }

    static void Patch(Harmony h, string type, string method, string postfix)
    {
        var t = AccessTools.TypeByName(type);
        var m = t == null ? null : AccessTools.Method(t, method);
        if (m == null) { L.LogWarning($"PATCH-MISS {type}.{method}"); return; }
        h.Patch(m, postfix: new HarmonyMethod(typeof(Plugin), postfix));
        L.LogInfo($"PATCHED {type}.{method}");
    }

    static void OnLevelPostfix() => L.LogInfo($"PATCH-HIT Reporter._OnLevelWasLoaded #{++SceneLoads}");

    static void TitlePostfix(ref float __result)
    {
        if (++TitleCalls % 600 == 1) L.LogInfo($"PATCH-HIT TitleScenePreviewController.GetPercentOfDay #{TitleCalls} -> {__result}");
    }
}

public class Ticker : MonoBehaviour
{
    public Ticker(IntPtr ptr) : base(ptr) { }
    int frames;
    void Update()
    {
        if (++frames % 300 == 0) Plugin.L.LogInfo($"TICK injected Update frame {frames}, time {Time.realtimeSinceStartup:F1}");
    }
}
