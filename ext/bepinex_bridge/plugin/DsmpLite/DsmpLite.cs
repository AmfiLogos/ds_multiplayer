// DsmpLite - copy this ONE file into your BepInEx (IL2CPP, net6) mod to use the Dungeon Settlers multiplayer random and
// chat WITHOUT a reference to DsmpBridge.dll. It finds the multiplayer mod's C exports itself; when the multiplayer mod
// is absent (or older than api 2) every call still works: a local random, Chat returns NoNative, Installed is false.
// No call throws. No unsafe code needed. Change the namespace to your mod's if you like (the class is internal).
//
//   uint  r = DsmpLite.Rand("mymod", 0);                 // 32 random bits
//   int   i = DsmpLite.RandRange("mymod", 0, 1, 7);      // 1..6
//   float f = DsmpLite.RandValue("mymod", 0);            // [0, 1)
//   DsmpLite.Chat("My Mod", "The portal opened.", true); // every player sees "My Mod mod: The portal opened."
//
// Shared random: a draw made on the main thread INSIDE the lockstep step (game sim code, e.g. a Harmony patch on a method
// the simulation runs each tick) gives the same value on every peer. Elsewhere (UI, input, other threads) and in single
// player it is a local stream: never use those draws to change game state in a session. RandShared tells which.
// Sending your own actions to every peer (channels, handlers) needs DsmpBridge.dll (Multiplayer.Register / Do).
// Source: github.com/AmfiLogos/ds_multiplayer, ext/bepinex_bridge/plugin/DsmpLite/DsmpLite.cs (C contract: src/mp_bridge.h).
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace DsmpLiteShim
{
    internal static class DsmpLite
    {
        internal enum ChatResult { Shown = 0, NoSession = 1, BadArgs = -1, Spectator = -2, RateLimited = -5, NotSent = -6, NoNative = -100 }

        /// <summary>The multiplayer mod is loaded (any bridge version).</summary>
        internal static bool Installed { get { Init(); return api >= 1; } }
        /// <summary>Its bridge version (0 = absent). Random and chat need 2.</summary>
        internal static int NativeApi { get { Init(); return api; } }
        /// <summary>A multiplayer session is on.</summary>
        internal static bool Active { get { try { return Installed && f_active != null && f_active() != 0; } catch { return false; } } }
        /// <summary>0 none, 1 host, 2 guest, 3 spectator.</summary>
        internal static int Role { get { try { return Installed && f_role != null ? f_role() : 0; } catch { return 0; } } }
        internal static bool RandShared { get { try { Init(); return f_shared != null && f_shared() != 0; } catch { return false; } } }

        internal static uint Rand(string modId, uint stream = 0)
        {
            try { Init(); if (f_u32 != null) return f_u32(Z(modId), stream); } catch { }
            var r = Local(modId, stream); lock (r) return (uint)r.Next(1 << 16) << 16 | (uint)r.Next(1 << 16);
        }
        /// <summary>lo .. hi-1; hi &lt;= lo returns lo.</summary>
        internal static int RandRange(string modId, uint stream, int lo, int hi)
        {
            if (hi <= lo) return lo;
            try { Init(); if (f_range != null) return f_range(Z(modId), stream, lo, hi); } catch { }
            return (int)(lo + (long)((ulong)Rand(modId, stream) * (ulong)((long)hi - lo) >> 32));
        }
        /// <summary>[0, 1), 24 bits.</summary>
        internal static float RandValue(string modId, uint stream = 0)
        {
            try { Init(); if (f_value != null) return f_value(Z(modId), stream); } catch { }
            return (Rand(modId, stream) >> 8) * (1f / 16777216f);
        }
        /// <summary>System chat line "&lt;modName&gt; mod: text". broadcast = every player (not for spectators); else this player only.</summary>
        internal static ChatResult Chat(string modName, string text, bool broadcast = false)
        {
            try
            {
                Init();
                if (f_chat == null) return ChatResult.NoNative;
                if (string.IsNullOrEmpty(modName) || text == null) return ChatResult.BadArgs;
                return (ChatResult)f_chat(Z(modName), Z(text), broadcast ? 1 : 0);
            }
            catch { return ChatResult.NoNative; }
        }

        // ---- lookup (same rules as DsmpBridge: Windows multiplayer.dll / baselib.dll, Mac the loaded multiplayer.dylib) ----
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int FInt();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate uint FU32(byte[] mod, uint stream);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int FRange(byte[] mod, uint stream, int lo, int hi);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate float FValue(byte[] mod, uint stream);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int FChat(byte[] name, byte[] text, int broadcast);
        static FInt f_active, f_role, f_shared; static FU32 f_u32; static FRange f_range; static FValue f_value; static FChat f_chat;
        static int api; static bool tried; static IntPtr lib;
        static readonly object gate = new object();
        static readonly Dictionary<(string, uint), Random> local = new Dictionary<(string, uint), Random>();
        static readonly int seed = Environment.TickCount;

        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
        [DllImport("libSystem.dylib")] static extern IntPtr dlsym(IntPtr handle, string name);
        [DllImport("libSystem.dylib")] static extern IntPtr dlopen(string path, int mode);
        [DllImport("libSystem.dylib")] static extern uint _dyld_image_count();
        [DllImport("libSystem.dylib")] static extern IntPtr _dyld_get_image_name(uint i);

        static byte[] Z(string s) => Encoding.UTF8.GetBytes((s ?? "") + "\0");
        static Random Local(string modId, uint stream)
        {
            lock (local)
            {
                var k = (modId ?? "", stream);
                if (!local.TryGetValue(k, out var r)) { uint h = 2166136261; foreach (char ch in k.Item1) h = (h ^ ch) * 16777619; local[k] = r = new Random((int)(h ^ stream * 2654435761u) ^ seed); }
                return r;
            }
        }
        static IntPtr Sym(string name)
        {
            if (lib == IntPtr.Zero) return IntPtr.Zero;
            if (OperatingSystem.IsWindows()) return NativeLibrary.TryGetExport(lib, name, out var a) ? a : IntPtr.Zero;
            return dlsym(lib, name);
        }
        static T Fn<T>(string name) where T : Delegate { var p = Sym(name); return p == IntPtr.Zero ? null : Marshal.GetDelegateForFunctionPointer<T>(p); }
        static void Init()
        {
            lock (gate)
            {
                if (tried) return;
                tried = true;
                try
                {
                    if (OperatingSystem.IsWindows())
                    {
                        lib = GetModuleHandleW("multiplayer.dll");
                        if (lib == IntPtr.Zero || !NativeLibrary.TryGetExport(lib, "dsmp_bridge_api_version", out _)) lib = GetModuleHandleW("baselib.dll");
                    }
                    else if (OperatingSystem.IsMacOS())
                    {
                        foreach (var want in new[] { "/multiplayer.dylib", "/dsrig.dylib" })
                            for (uint i = 0, n = _dyld_image_count(); i < n && lib == IntPtr.Zero; i++)
                            {
                                string nm = Marshal.PtrToStringUTF8(_dyld_get_image_name(i));
                                if (nm == null || !nm.EndsWith(want)) continue;
                                IntPtr h = dlopen(nm, 0x10 | 0x2);   // RTLD_NOLOAD | RTLD_NOW: never loads anything
                                if (h != IntPtr.Zero && dlsym(h, "dsmp_bridge_api_version") != IntPtr.Zero) lib = h;
                            }
                    }
                    var fa = Fn<FInt>("dsmp_bridge_api_version");
                    if (fa == null) return;
                    int v = fa();
                    if (v < 1) return;
                    f_active = Fn<FInt>("dsmp_bridge_active"); f_role = Fn<FInt>("dsmp_bridge_role");
                    if (v >= 2)
                    {
                        f_u32 = Fn<FU32>("dsmp_bridge_rand_u32"); f_range = Fn<FRange>("dsmp_bridge_rand_range");
                        f_value = Fn<FValue>("dsmp_bridge_rand_value"); f_shared = Fn<FInt>("dsmp_bridge_rand_shared");
                        f_chat = Fn<FChat>("dsmp_bridge_chat");
                        if (f_u32 == null || f_range == null || f_value == null || f_shared == null) { f_u32 = null; f_range = null; f_value = null; f_shared = null; }   // random: all four or local
                    }
                    api = v;
                }
                catch { api = 0; f_u32 = null; f_range = null; f_value = null; f_shared = null; f_chat = null; }
            }
        }
    }
}
