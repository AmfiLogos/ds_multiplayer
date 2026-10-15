using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using BepInEx;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;
using Il2CppInterop.Common.Attributes;
using Il2CppInterop.Common.XrefScans;
using Il2CppInterop.Runtime.XrefScans;

namespace XrefArm64;

/// Lane test plugin: XrefScanner on arm64. For a few game methods, compares the live arm64 scan (callees
/// resolved to methods, string literals read) with the generator's x86-64 cache resolved through its own map,
/// and lists UsedBy. Logs "XREF-RESULT" lines.
[BepInPlugin("dsmp.xrefarm64", "XrefArm64", "1.0.0")]
public class Plugin : BasePlugin
{
    static ManualLogSource L;

    public override void Load()
    {
        L = Log;
        Probe("Il2CppSystem.String", "IsNullOrEmpty");
        Probe("Il2CppSystem.Math", "Min");
        foreach (var (type, name) in new[] { ("Refactor.SaveLoadHelper", "DeserializeCampaignSaveDataSafely"),
                                             ("Refactor.SaveLoadHelper", "SaveFile"),
                                             ("Refactor.Main.QuickSlotData", "Deserialize"),
                                             ("Refactor.UI.SystemMenuView", "QuitToTitleSceneCallback"),
                                             ("Refactor.TitleScenePreviewController", "EvaluateTitleGlobalLightColor") })
        {
            try { Check(AccessTools.Method(AccessTools.TypeByName(type), name)); }
            catch (Exception e) { L.LogError($"XREF-FAIL {type}.{name}: {e}"); }
        }
    }

    static void Probe(string type, string method)
    {
        try
        {
            var gaBase = (long)typeof(XrefScanMethodDb).GetField("GameAssemblyBase", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null);
            var t = AccessTools.TypeByName(type);
            foreach (var m in t.GetMethods().Where(x => x.Name == method))
            {
                var f = Il2CppInterop.Common.Il2CppInteropUtils.GetIl2CppMethodInfoPointerFieldForGeneratedMethod(m);
                var mi = f == null ? IntPtr.Zero : (IntPtr)f.GetValue(null);
                var code = mi == IntPtr.Zero ? 0 : System.Runtime.InteropServices.Marshal.ReadIntPtr(mi).ToInt64() - gaBase;
                L.LogInfo($"XREF-PROBE {type}.{method}({m.GetParameters().Length}) field={f?.Name} mi={mi:x} code={code:x} resolve={N(XrefScanMethodDb.TryResolvePointer((IntPtr)(code + gaBase)))}");
            }
        }
        catch (Exception e) { L.LogError($"XREF-PROBE {type}.{method}: {e.Message}"); }
    }

    static string N(MethodBase m) => m == null ? "?" : $"{m.DeclaringType?.Name}.{m.Name}";

    static void Check(MethodBase m)
    {
        var live = XrefScanner.XrefScan(m).ToList();
        var calls = live.Where(x => x.Type == XrefType.Method).Select(x => N(x.TryResolve())).ToList();
        var strings = live.Where(x => x.Type == XrefType.Global).Select(x => x.ReadAsObject()?.ToString()).ToList();
        L.LogInfo($"XREF {N(m)}: {calls.Count} calls ({calls.Count(c => c != "?")} resolved), {strings.Count} globals");
        var gaBase = (long)typeof(XrefScanMethodDb).GetField("GameAssemblyBase", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null);
        foreach (var x in live.Where(x => x.Type == XrefType.Method))
            L.LogInfo($"  XREF-CALL {N(m)} {(long)x.Pointer - gaBase:x} {N(x.TryResolve())}");
        L.LogInfo($"  strings: {string.Join(" | ", strings.Take(8).Select(s => s?.Length > 60 ? s[..60] : s))}");

        // x86-64 cache, resolved through the generator's own (x86) map: the reference call list.
        var attr = m.GetCustomAttribute<CachedScanResultsAttribute>(false);
        L.LogInfo($"  x86 cache ranges: xref {attr?.XrefRangeStart}-{attr?.XrefRangeEnd}, users {attr?.RefRangeStart}-{attr?.RefRangeEnd}");
        var db = typeof(XrefScanMethodDb);
        var cached = (IEnumerable<XrefInstance>)db.GetMethod("CachedXrefScan", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, new object[] { attr });
        var map = db.GetField("MethodMap", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null);
        var lookup = map.GetType().GetMethod("Lookup")!;
        var baseAddr = (long)db.GetField("GameAssemblyBase", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null);
        var refCalls = cached.Where(x => x.Type == XrefType.Method)
            .Select(x => N((MethodBase)lookup.Invoke(map, new object[] { (long)x.Pointer - baseAddr }))).ToList();
        var a = calls.Where(c => c != "?").Distinct().ToHashSet();
        var r = refCalls.Where(c => c != "?").Distinct().ToHashSet();
        L.LogInfo($"XREF-RESULT {N(m)}: arm64 {a.Count} distinct methods, x86 cache {r.Count}, common {a.Intersect(r).Count()}");
        L.LogInfo($"  only arm64: {string.Join(", ", a.Except(r).Take(10))}");
        L.LogInfo($"  only x86:   {string.Join(", ", r.Except(a).Take(10))}");
        var users = XrefScanner.UsedBy(m).Select(x => N(x.TryResolve())).Distinct().ToList();
        L.LogInfo($"XREF-RESULT UsedBy {N(m)}: {users.Count} ({users.Count(u => u != "?")} resolved): {string.Join(", ", users.Take(8))}");
    }
}
