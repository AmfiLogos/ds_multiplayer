// DsmpBridge - lets BepInEx (IL2CPP) mods see the Dungeon Settlers multiplayer session and send gameplay actions
// through its lockstep order path. Native side: src/mp_bridge.h (exports of mods\multiplayer\multiplayer.dll with the
// mod loader or the baselib.dll proxy (old friend kit) on Windows, dsrig.dylib on Mac).
//
// Use from a mod (add a reference to DsmpBridge.dll and [BepInDependency(DsmpBridge.BridgePlugin.Guid)]):
//   Multiplayer.Register("mymod.heal", (player, data, step) => ApplyHeal(data));   // once, in Load()
//   Multiplayer.Do("mymod.heal", bytes, (p, d, s) => ApplyHeal(d));                // instead of ApplyHeal(bytes)
// Do() sends the action when a session is on (it then runs on EVERY peer at the same lockstep step, this one included)
// and calls the handler at once when it is not (single player). Never change game state directly in a session.
// Every peer must have the same mods with the same channels; "dsmp.*" channels are native and need no plugin.
// Random (native api 2): Multiplayer.Rand / RandRange / RandValue("mymod", stream) give the SAME numbers on every peer when
// drawn inside the lockstep step (a bridge handler, a game sim method); elsewhere and in single player a local stream.
// Chat (native api 2): Multiplayer.Chat("My Mod", "text", broadcast) shows a system line ("<My Mod> mod: text").
// No call here throws: an absent or older multiplayer mod gives Installed false / a local random / ChatResult.NoNative.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace DsmpBridge
{
    public enum Role { None = 0, Host = 1, Guest = 2, Spectator = 3 }

    public enum SendResult
    {
        Sent = 0, NoSession = 1, BadArgs = -1, Spectator = -2, Refused = -3, NotAllowed = -4,
        NoNative = -100,     // our multiplayer mod is not installed (vanilla baselib.dll)
        AppliedLocally = 2,  // Do(): no session, the handler ran at once
    }

    public enum ChatResult
    {
        Shown = 0,           // shown here (local) or sent to every player (broadcast)
        NoSession = 1,       // no multiplayer session: no chat box to show it in
        BadArgs = -1, Spectator = -2 /* broadcast refused */, RateLimited = -5, NotSent = -6 /* no connection */,
        NoNative = -100,     // the multiplayer mod is absent or too old for chat (native api < 2)
    }

    /// <summary>Runs on every peer at the lockstep step the server stamped. player = the sender's player id.</summary>
    public delegate void BridgeHandler(int player, byte[] data, uint step);

    public static unsafe class Multiplayer
    {
        /// <summary>Oldest native bridge this plugin works with (api 1 = session info, Send, Order). Rand / Chat need 2.</summary>
        public const int MinNativeApi = 1;
        /// <summary>The native bridge version found (0 = none).</summary>
        public static int NativeApi { get { Init(); return api; } }
        static int api;
        static IntPtr lib;
        static bool tried;
        static delegate* unmanaged[Cdecl]<int> f_api, f_active, f_role, f_player, f_applying;
        static delegate* unmanaged[Cdecl]<uint> f_step;
        static delegate* unmanaged[Cdecl]<byte*, int, int> f_modset;
        static delegate* unmanaged[Cdecl]<byte*, byte*, int, int> f_send;
        static delegate* unmanaged[Cdecl]<int, byte*, byte*, int, int> f_order;
        static delegate* unmanaged[Cdecl]<IntPtr, void> f_set_dispatch;
        static delegate* unmanaged[Cdecl]<byte*, uint, uint> f_rand_u32;          // api 2
        static delegate* unmanaged[Cdecl]<byte*, uint, int, int, int> f_rand_range;
        static delegate* unmanaged[Cdecl]<byte*, uint, float> f_rand_value;
        static delegate* unmanaged[Cdecl]<int> f_rand_shared;
        static delegate* unmanaged[Cdecl]<byte*, byte*, int, int> f_chat;
        static readonly Dictionary<string, BridgeHandler> handlers = new Dictionary<string, BridgeHandler>();
        public static Action<string> Log = s => { };

        /// <summary>Our native multiplayer mod is loaded and speaks this bridge version.</summary>
        public static bool Installed { get { Init(); return api >= MinNativeApi; } }
        /// <summary>A multiplayer session is on (this peer got the host's welcome).</summary>
        public static bool Active => Installed && f_active() != 0;
        public static Role Role => Installed ? (Role)f_role() : Role.None;
        public static int Player => Installed ? f_player() : -1;
        public static uint Step => Installed ? f_step() : 0;
        /// <summary>True while a bridge order is being replayed (inside a handler). A Harmony prefix that reroutes a
        /// game call through Send() must let the call pass while this is true.</summary>
        public static bool Applying => Installed && f_applying() != 0;

        /// <summary>The launcher's gameplay mod set for this world: rule ("mods", "vanilla", or "none" = started without
        /// the launcher), set hash, lines "id@version:...". Same on every peer that was allowed to join.</summary>
        public static (string rule, string hash, string[] lines) Modset
        {
            get
            {
                if (!Installed) return ("none", "", Array.Empty<string>());
                int n = f_modset(null, 0);
                var b = new byte[n + 1];
                fixed (byte* p = b) f_modset(p, b.Length);
                var parts = Encoding.UTF8.GetString(b, 0, n).Split('\n');
                var lines = parts.Length > 2 ? parts[2..] : Array.Empty<string>();
                return (parts[0], parts.Length > 1 ? parts[1] : "", lines);
            }
        }

        public static void Register(string channel, BridgeHandler handler)
        {
            if (string.IsNullOrEmpty(channel) || channel.StartsWith("dsmp.")) throw new ArgumentException("channel: non-empty, not dsmp.*");
            lock (handlers) handlers[channel] = handler;
            Init();
        }

        /// <summary>Send an action. Sent = it will run on every peer (this one too) through the registered handler.</summary>
        public static SendResult Send(string channel, byte[] data)
        {
            if (!Installed) return SendResult.NoNative;
            var ch = Encoding.UTF8.GetBytes(channel + "\0");
            data ??= Array.Empty<byte>();
            fixed (byte* c = ch) fixed (byte* d = data) return (SendResult)f_send(c, d, data.Length);
        }

        /// <summary>The usual call for a mod action: in a session send it, else run the handler now.</summary>
        public static SendResult Do(string channel, byte[] data, BridgeHandler local)
        {
            var r = Send(channel, data);
            if (r == SendResult.NoSession || r == SendResult.NoNative) { local(Player, data ?? Array.Empty<byte>(), Step); return SendResult.AppliedLocally; }
            return r;
        }

        // ---- random: the same numbers on every peer inside the lockstep step (native api 2) ----
        // Shared = drawn on the main thread inside the step (a bridge handler, or game sim code such as a Harmony patch on a
        // method the step runs): every peer gets the same value (session seed, mod id, stream, tick, step, nth draw of that
        // (mod, stream) in the step). Anywhere else (UI, other threads, single player): a local stream, NOT the same on peers.
        // Absent / old multiplayer mod: a local System.Random per (mod, stream).
        static readonly Dictionary<(string, uint), Random> local = new Dictionary<(string, uint), Random>();
        static readonly int localSeed = Environment.TickCount;

        /// <summary>The random calls reach the multiplayer mod (api 2). False: they use a local System.Random.</summary>
        public static bool HasRand { get { Init(); return f_rand_u32 != null && f_rand_range != null && f_rand_value != null && f_rand_shared != null; } }
        /// <summary>True when a draw made now is the shared one (same value on every peer).</summary>
        public static bool RandShared { get { try { return HasRand && f_rand_shared() != 0; } catch { return false; } } }

        /// <summary>32 random bits for (modId, stream). Use your mod id; streams let parts of a mod draw independently.</summary>
        public static uint Rand(string modId, uint stream = 0)
        {
            try { if (HasRand) { var m = Utf8z(modId); fixed (byte* p = m) return f_rand_u32(p, stream); } } catch { }
            var r = Local(modId, stream); lock (r) return (uint)r.Next(1 << 16) << 16 | (uint)r.Next(1 << 16);
        }
        /// <summary>lo .. hi-1 (like UnityEngine.Random.Range(int, int)); hi &lt;= lo returns lo.</summary>
        public static int RandRange(string modId, uint stream, int lo, int hi)
        {
            if (hi <= lo) return lo;
            try { if (HasRand) { var m = Utf8z(modId); fixed (byte* p = m) return f_rand_range(p, stream, lo, hi); } } catch { }
            return (int)(lo + (long)((ulong)Rand(modId, stream) * (ulong)((long)hi - lo) >> 32));
        }
        /// <summary>[0, 1) with 24 bits.</summary>
        public static float RandValue(string modId, uint stream = 0)
        {
            try { if (HasRand) { var m = Utf8z(modId); fixed (byte* p = m) return f_rand_value(p, stream); } } catch { }
            return (Rand(modId, stream) >> 8) * (1f / 16777216f);
        }
        static Random Local(string modId, uint stream)
        {
            lock (local)
            {
                var k = (modId ?? "", stream);
                if (!local.TryGetValue(k, out var r)) { uint h = 2166136261; foreach (char ch in k.Item1) h = (h ^ ch) * 16777619; local[k] = r = new Random((int)(h ^ stream * 2654435761u) ^ localSeed); }
                return r;
            }
        }

        // ---- chat (native api 2) ----
        /// <summary>A system line in the multiplayer chat, prefixed with your mod's name (at most 24 bytes; the text is cut to
        /// 160 bytes when broadcast, 200 local; rich-text tags are shown as typed). broadcast = every player sees it (refused
        /// for spectators); else only this player. Rate limited per mod. Outside a session there is no chat box: NoSession.</summary>
        public static ChatResult Chat(string modName, string text, bool broadcast = false)
        {
            try
            {
                Init();
                if (f_chat == null) return ChatResult.NoNative;
                if (string.IsNullOrEmpty(modName) || text == null) return ChatResult.BadArgs;
                var n = Utf8z(modName); var t = Utf8z(text);
                fixed (byte* pn = n) fixed (byte* pt = t) return (ChatResult)f_chat(pn, pt, broadcast ? 1 : 0);
            }
            catch (Exception e) { Log("bridge: chat failed: " + e.Message); return ChatResult.NoNative; }
        }

        static byte[] Utf8z(string s) => Encoding.UTF8.GetBytes((s ?? "") + "\0");

        // ---- built-in native actions (same C code on Mac and Windows; peers need no plugin) ----
        public static SendResult AddGold(int delta) => Send("dsmp.gold.add", BitConverter.GetBytes(delta));

        /// <summary>A native order with the unit-owner check: MOVE (type 1, args i32 x, i32 y) or ORD (type 2, ord wire bytes).</summary>
        public static SendResult Order(int type, Guid owner, byte[] args)
        {
            if (!Installed) return SendResult.NoNative;
            var g = owner.ToByteArray(); args ??= Array.Empty<byte>();
            fixed (byte* o = g) fixed (byte* a = args) return (SendResult)f_order(type, o, a, args.Length);
        }

        // ---- native lookup ----
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
        [DllImport("libSystem.dylib")] static extern IntPtr dlsym(IntPtr handle, string name);
        [DllImport("libSystem.dylib")] static extern IntPtr dlopen(string path, int mode);
        [DllImport("libSystem.dylib")] static extern uint _dyld_image_count();
        [DllImport("libSystem.dylib")] static extern IntPtr _dyld_get_image_name(uint i);

        // Mac: the loader opens mods\multiplayer\multiplayer.dylib with RTLD_LOCAL (not in RTLD_DEFAULT): find the
        // loaded image by name and take a handle on it (RTLD_NOLOAD: never loads anything). Rig copies: dsrig.dylib.
        static IntPtr MacImage()
        {
            foreach (var want in new[] { "/multiplayer.dylib", "/dsrig.dylib", "/dsrig_bridge.dylib" })
                for (uint i = 0, n = _dyld_image_count(); i < n; i++)
                {
                    string name = Marshal.PtrToStringUTF8(_dyld_get_image_name(i));
                    if (name == null || !name.EndsWith(want)) continue;
                    IntPtr h = dlopen(name, 0x10 | 0x2 /* RTLD_NOLOAD | RTLD_NOW */);
                    if (h != IntPtr.Zero && dlsym(h, "dsmp_bridge_api_version") != IntPtr.Zero) return h;
                }
            return new IntPtr(-2);   // RTLD_DEFAULT
        }

        static IntPtr Sym(string name)
        {
            if (OperatingSystem.IsWindows()) return lib != IntPtr.Zero && NativeLibrary.TryGetExport(lib, name, out var a) ? a : IntPtr.Zero;
            return dlsym(lib, name);
        }

        internal static void Init()
        {
            if (tried) return;
            lock (handlers) { if (tried) return; tried = true; }
            try
            {
                if (OperatingSystem.IsWindows())   // mod loader: mods\multiplayer\multiplayer.dll; old friend kit: the baselib.dll proxy
                {
                    lib = GetModuleHandleW("multiplayer.dll");
                    if (lib == IntPtr.Zero || !NativeLibrary.TryGetExport(lib, "dsmp_bridge_api_version", out _)) lib = GetModuleHandleW("baselib.dll");
                }
                else lib = MacImage();
                f_api = (delegate* unmanaged[Cdecl]<int>)Sym("dsmp_bridge_api_version");
                if (f_api == null) { Log("native multiplayer bridge not found (vanilla game or old multiplayer mod)"); return; }
                f_active = (delegate* unmanaged[Cdecl]<int>)Sym("dsmp_bridge_active");
                f_role = (delegate* unmanaged[Cdecl]<int>)Sym("dsmp_bridge_role");
                f_player = (delegate* unmanaged[Cdecl]<int>)Sym("dsmp_bridge_player");
                f_applying = (delegate* unmanaged[Cdecl]<int>)Sym("dsmp_bridge_applying");
                f_step = (delegate* unmanaged[Cdecl]<uint>)Sym("dsmp_bridge_step");
                f_modset = (delegate* unmanaged[Cdecl]<byte*, int, int>)Sym("dsmp_bridge_modset");
                f_send = (delegate* unmanaged[Cdecl]<byte*, byte*, int, int>)Sym("dsmp_bridge_send");
                f_order = (delegate* unmanaged[Cdecl]<int, byte*, byte*, int, int>)Sym("dsmp_bridge_order");
                f_set_dispatch = (delegate* unmanaged[Cdecl]<IntPtr, void>)Sym("dsmp_bridge_set_dispatch");
                if (f_active == null || f_role == null || f_player == null || f_applying == null || f_step == null || f_modset == null
                    || f_send == null || f_order == null || f_set_dispatch == null) { f_api = null; Log("native bridge incomplete: off"); return; }
                int v = f_api();
                if (v < MinNativeApi) { Log($"native bridge api {v}, plugin needs >= {MinNativeApi}: off"); f_api = null; return; }
                if (v >= 2)   // each one alone: a missing export = that feature absent, the rest still works
                {
                    f_rand_u32 = (delegate* unmanaged[Cdecl]<byte*, uint, uint>)Sym("dsmp_bridge_rand_u32");
                    f_rand_range = (delegate* unmanaged[Cdecl]<byte*, uint, int, int, int>)Sym("dsmp_bridge_rand_range");
                    f_rand_value = (delegate* unmanaged[Cdecl]<byte*, uint, float>)Sym("dsmp_bridge_rand_value");
                    f_rand_shared = (delegate* unmanaged[Cdecl]<int>)Sym("dsmp_bridge_rand_shared");
                    f_chat = (delegate* unmanaged[Cdecl]<byte*, byte*, int, int>)Sym("dsmp_bridge_chat");
                }
                delegate* unmanaged[Cdecl]<int, byte*, byte*, int, uint, int> d = &Dispatch;
                f_set_dispatch((IntPtr)d);
                api = v;
                Log($"native multiplayer bridge found (api {v}; random {(HasRand ? "shared" : "local")}, chat {(f_chat != null ? "on" : "off")})");
            }
            catch (Exception e) { f_api = null; api = 0; f_rand_u32 = null; f_chat = null; Log("native bridge lookup failed: " + e.Message); }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static int Dispatch(int player, byte* channel, byte* data, int len, uint step)
        {
            try
            {
                string ch = Marshal.PtrToStringUTF8((IntPtr)channel);
                BridgeHandler h;
                lock (handlers) handlers.TryGetValue(ch, out h);
                if (h == null) { Log($"bridge: no handler for '{ch}' on this peer (mod missing?)"); return -2; }
                var b = new byte[len];
                if (len > 0) Marshal.Copy((IntPtr)data, b, 0, len);
                h(player, b, step);
                return 0;
            }
            catch (Exception e) { Log("bridge: handler threw (this peer now differs): " + e); return -1; }
        }
    }
}
