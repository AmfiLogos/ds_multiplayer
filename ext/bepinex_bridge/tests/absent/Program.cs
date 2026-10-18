// "Safe when absent" test (tests/absent/run.sh). One process per case (the bridge looks the native side up once):
//   none     DsmpBridge.dll present, no multiplayer native        -> Installed false, every call answers, nothing throws
//   v1       old multiplayer mod (api 1, no random / chat exports) -> Installed true (api >= 1), local random, chat NoNative
//   v2       api 2 exports                                          -> the calls reach C with intact strings / stream / bounds
//   nobridge DsmpBridge.dll NOT installed: a mod built against it with the soft-dependency pattern, and DsmpLite, run
// A failed check exits 1. A real break caught here = a mod (Delvers) crashing or wrongly cut off from the API in the game.
using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using DsmpLiteShim;

static class Program
{
    static int fails;
    static void Check(string what, bool ok, object info = null) { Console.WriteLine((ok ? "PASS " : "FAIL ") + what + (info != null ? "  [" + info + "]" : "")); if (!ok) fails++; }
    static uint H(string s, uint st) { uint x = 2166136261; foreach (byte b in System.Text.Encoding.UTF8.GetBytes(s)) x = (x ^ b) * 16777619; return x ^ (st * 2654435761u); }

    static int Main(string[] a)
    {
        string mode = a[0];
        try
        {
            if (mode != "none" && mode != "nobridge") NativeLibrary.Load(a[1]);   // the stand-in multiplayer.dylib, loaded like the mod loader does
            if (mode == "nobridge") NoBridge(); else Bridge(mode);
            Lite(mode == "v2");
        }
        catch (Exception e) { Check("no exception reaches the mod", false, e.GetType().Name + ": " + e.Message); }
        Console.WriteLine($"{mode}: {(fails == 0 ? "PASS" : fails + " FAIL")}");
        return fails == 0 ? 0 : 1;
    }

    static void Bridge(string mode)
    {
        bool v2 = mode == "v2";
        Check($"{mode}: Installed {mode != "none"}, NativeApi {(mode == "none" ? 0 : v2 ? 2 : 1)}",
              BridgeCalls.Installed() == (mode != "none") && BridgeCalls.Api() == (mode == "none" ? 0 : v2 ? 2 : 1), $"{BridgeCalls.Installed()} {BridgeCalls.Api()}");
        Check($"{mode}: HasRand {v2}", BridgeCalls.HasRand() == v2);
        uint r = BridgeCalls.Rand("mymod", 3); int i = BridgeCalls.Range("mymod", 3, 10, 20); float f = BridgeCalls.Value("mymod", 3);
        if (v2) Check("v2: Rand/RandRange/RandValue reach the native side (mod id, stream, bounds)",
                      r == H("mymod", 3) && i == 10 + (int)(H("mymod", 3) % 10) && f == (H("mymod", 3) >> 8) / 16777216f, $"{r} {i} {f}");
        else Check($"{mode}: local random in range", i >= 10 && i < 20 && f >= 0 && f < 1, $"{r} {i} {f}");
        Check($"{mode}: RandRange(hi <= lo) = lo", BridgeCalls.Range("mymod", 0, 5, 5) == 5 && BridgeCalls.Range("mymod", 0, 5, -3) == 5);
        var c = BridgeCalls.Chat("My Mod", "héllo <b>", true);
        Check($"{mode}: Chat -> {(v2 ? "Shown (strings intact)" : "NoNative")}", v2 ? c == DsmpBridge.ChatResult.Shown : c == DsmpBridge.ChatResult.NoNative, c);
        Check($"{mode}: Chat(null, null) does not throw", BridgeCalls.Chat(null, null, false) == (v2 ? DsmpBridge.ChatResult.BadArgs : DsmpBridge.ChatResult.NoNative));
        int ran = 0;
        var d = BridgeCalls.Do(() => ran++);
        Check($"{mode}: Do() outside a session runs the handler locally", d == DsmpBridge.SendResult.AppliedLocally && ran == 1, d);
    }

    // DsmpBridge.dll is NOT next to the mod. The guard (in BepInEx: IL2CPPChainloader.Instance.Plugins.ContainsKey("dsmp.bridge"))
    // is false, so the NoInlining wrapper class is never JIT-compiled and nothing throws.
    static void NoBridge()
    {
        bool present = BridgePresent();
        Check("nobridge: guard sees DsmpBridge absent", !present);
        if (present) BridgeCalls.Rand("mymod", 0);
        bool threw = false;
        try { BridgeCalls.Rand("mymod", 0); } catch (System.IO.FileNotFoundException) { threw = true; }
        Check("nobridge: (setup) an unguarded call does fail, so the guard is what keeps the mod alive", threw);
    }
    static bool BridgePresent() { try { Assembly.Load("DsmpBridge"); return true; } catch { return false; } }

    static void Lite(bool v2)
    {
        uint r = DsmpLite.Rand("mymod", 3); int i = DsmpLite.RandRange("mymod", 3, 10, 20); float f = DsmpLite.RandValue("mymod", 3);
        if (v2) Check("lite v2: same native values as the bridge", r == H("mymod", 3) && i == 10 + (int)(H("mymod", 3) % 10) && f == (H("mymod", 3) >> 8) / 16777216f
                      && DsmpLite.RandShared && DsmpLite.Chat("My Mod", "héllo <b>", true) == DsmpLite.ChatResult.Shown, $"{r} {i} {f}");
        else Check("lite: local random in range, chat NoNative, RandShared false", i >= 10 && i < 20 && f >= 0 && f < 1 && !DsmpLite.RandShared
                   && DsmpLite.Chat("My Mod", "x", true) == DsmpLite.ChatResult.NoNative, $"{i} {f}");
    }
}

// The soft-dependency pattern: every DsmpBridge call lives here, behind the guard; NoInlining keeps the JIT from pulling
// these bodies (and so DsmpBridge.dll) into the caller.
static class BridgeCalls
{
    [MethodImpl(MethodImplOptions.NoInlining)] public static bool Installed() => DsmpBridge.Multiplayer.Installed;
    [MethodImpl(MethodImplOptions.NoInlining)] public static int Api() => DsmpBridge.Multiplayer.NativeApi;
    [MethodImpl(MethodImplOptions.NoInlining)] public static bool HasRand() => DsmpBridge.Multiplayer.HasRand;
    [MethodImpl(MethodImplOptions.NoInlining)] public static uint Rand(string m, uint s) => DsmpBridge.Multiplayer.Rand(m, s);
    [MethodImpl(MethodImplOptions.NoInlining)] public static int Range(string m, uint s, int lo, int hi) => DsmpBridge.Multiplayer.RandRange(m, s, lo, hi);
    [MethodImpl(MethodImplOptions.NoInlining)] public static float Value(string m, uint s) => DsmpBridge.Multiplayer.RandValue(m, s);
    [MethodImpl(MethodImplOptions.NoInlining)] public static DsmpBridge.ChatResult Chat(string n, string t, bool b) => DsmpBridge.Multiplayer.Chat(n, t, b);
    [MethodImpl(MethodImplOptions.NoInlining)] public static DsmpBridge.SendResult Do(Action a) => DsmpBridge.Multiplayer.Do("test.ch", new byte[] { 1 }, (p, d, s) => a());
}
