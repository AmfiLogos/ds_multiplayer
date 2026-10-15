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

    /// <summary>Runs on every peer at the lockstep step the server stamped. player = the sender's player id.</summary>
    public delegate void BridgeHandler(int player, byte[] data, uint step);

    public static unsafe class Multiplayer
    {
        public const int NativeApi = 1;
        static IntPtr lib;
        static bool tried;
        static delegate* unmanaged[Cdecl]<int> f_api, f_active, f_role, f_player, f_applying;
        static delegate* unmanaged[Cdecl]<uint> f_step;
        static delegate* unmanaged[Cdecl]<byte*, int, int> f_modset;
        static delegate* unmanaged[Cdecl]<byte*, byte*, int, int> f_send;
        static delegate* unmanaged[Cdecl]<int, byte*, byte*, int, int> f_order;
        static delegate* unmanaged[Cdecl]<IntPtr, void> f_set_dispatch;
        static readonly Dictionary<string, BridgeHandler> handlers = new Dictionary<string, BridgeHandler>();
        public static Action<string> Log = s => { };

        /// <summary>Our native multiplayer mod is loaded and speaks this bridge version.</summary>
        public static bool Installed { get { Init(); return f_api != null && f_api() == NativeApi; } }
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
            tried = true;
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
                if (f_api() != NativeApi) { Log($"native bridge api {f_api()}, plugin wants {NativeApi}: off"); f_api = null; return; }
                delegate* unmanaged[Cdecl]<int, byte*, byte*, int, uint, int> d = &Dispatch;
                f_set_dispatch((IntPtr)d);
                Log("native multiplayer bridge found (api " + NativeApi + ")");
            }
            catch (Exception e) { f_api = null; Log("native bridge lookup failed: " + e.Message); }
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
