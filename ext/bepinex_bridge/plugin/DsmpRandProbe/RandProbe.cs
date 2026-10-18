// TEST ONLY (tests/rand_pair.py). Question: inside the lockstep step, does plain UnityEngine.Random called from a BepInEx
// plugin give the same numbers on every peer, and does Multiplayer.Rand?
// Trigger file <DSRIG_DIR>/rand_probe.trigger (read every 0.5 s on the peer that has it):
//   "send N"  -> Multiplayer.Send("probe.rand", {0, N}): the handler runs on EVERY peer at the same step and draws N of each
//   "tick N"  -> Multiplayer.Send("probe.rand", {1, N}): the handler arms a Harmony postfix on Refactor.Tick.TickFlow.Tick
//                (a sim method our hooks do not hook); the next N Tick calls each draw one set (armed at the same step on
//                every peer, so the draw count stays the same on all peers)
//   "chat T"  -> Multiplayer.Chat("Rand Probe", T, broadcast) (result in the status "last")
// One JSON line per draw set in <DSRIG_DIR>/rand_probe.log:
//   {"src":"h"|"t","seq":k,"step":s,"shared":0|1,"ri":[Random.Range(int)..],"rv":[Random.value..],"rf":[Random.Range(float)..],"mp":[Multiplayer.Rand..]}
// <DSRIG_DIR>/rand_probe.status: one JSON line (bridge seen, api, harmony patched, counts).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using BepInEx;
using BepInEx.Unity.IL2CPP;
using DsmpBridge;
using HarmonyLib;
using UnityEngine;

namespace DsmpRandProbe
{
    [BepInPlugin("dsmp.randprobe", "DSMP Rand Probe (test)", "1.0.0")]
    [BepInDependency(BridgePlugin.Guid)]
    public class ProbePlugin : BasePlugin
    {
        internal static ProbePlugin I;
        internal static string Dir;
        internal static int seqH, seqT, tickLeft, tickN = 4, patched;
        internal static string last = "";

        public override void Load()
        {
            I = this;
            Dir = Environment.GetEnvironmentVariable("DSRIG_DIR") ?? Paths.GameRootPath;
            Multiplayer.Register("probe.rand", (player, data, step) =>
            {
                int mode = data.Length > 0 ? data[0] : 0, n = data.Length > 1 ? data[1] : 4;
                if (mode == 0) Draw("h", ++seqH, n);
                else { tickLeft = n; Log.LogInfo($"probe: Tick postfix armed for {n} calls at step {step}"); }
            });
            try
            {
                var m = AccessTools.Method(typeof(Refactor.Tick.TickFlow), "Tick");
                new Harmony("dsmp.randprobe").Patch(m, postfix: new HarmonyMethod(typeof(ProbePlugin).GetMethod(nameof(TickPost))));
                patched = 1;
            }
            catch (Exception e) { patched = -1; Log.LogError("probe: Harmony patch on TickFlow.Tick failed: " + e.Message); }
            AddComponent<ProbeDriver>();
            Log.LogInfo($"DSMP Rand Probe loaded (bridge api {Multiplayer.NativeApi}, rand {(Multiplayer.HasRand ? "native" : "local")}, Tick patch {patched})");
        }

        public static void TickPost()
        {
            if (tickLeft <= 0) return;
            tickLeft--;
            try { Draw("t", ++seqT, tickN); } catch (Exception e) { I.Log.LogError("probe tick: " + e.Message); }
        }

        internal static void Draw(string src, int seq, int n)
        {
            int shared = Multiplayer.RandShared ? 1 : 0;
            var ri = new List<int>(); var rv = new List<string>(); var rf = new List<string>(); var mp = new List<uint>();
            for (int i = 0; i < n; i++) ri.Add(UnityEngine.Random.Range(0, 1 << 30));
            for (int i = 0; i < n; i++) rv.Add(UnityEngine.Random.value.ToString("R"));
            for (int i = 0; i < n; i++) rf.Add(UnityEngine.Random.Range(0f, 1000f).ToString("R"));
            for (int i = 0; i < n; i++) mp.Add(Multiplayer.Rand("probe", 1));
            string line = $"{{\"src\":\"{src}\",\"seq\":{seq},\"step\":{Multiplayer.Step},\"shared\":{shared},\"ri\":[{string.Join(",", ri)}]," +
                          $"\"rv\":[{string.Join(",", rv)}],\"rf\":[{string.Join(",", rf)}],\"mp\":[{string.Join(",", mp)}]}}";
            File.AppendAllText(Path.Combine(Dir, "rand_probe.log"), line + "\n");
        }
    }

    public class ProbeDriver : MonoBehaviour
    {
        public ProbeDriver(IntPtr p) : base(p) { }
        float next;
        int done;

        void Update()
        {
            if (Time.unscaledTime < next) return;
            next = Time.unscaledTime + 0.5f;
            try
            {
                string trig = Path.Combine(ProbePlugin.Dir, "rand_probe.trigger");
                if (File.Exists(trig))
                {
                    string[] t = File.ReadAllText(trig).Trim().Split(' ');
                    File.Move(trig, trig + "." + (++done) + ".done", true);
                    string r;
                    if (t[0] == "chat") r = Multiplayer.Chat("Rand Probe", string.Join(" ", t.Skip(1)), true).ToString();
                    else { byte n = t.Length > 1 ? byte.Parse(t[1]) : (byte)4; r = Multiplayer.Send("probe.rand", new byte[] { (byte)(t[0] == "tick" ? 1 : 0), n }).ToString(); }
                    ProbePlugin.last = $"{string.Join(" ", t)} -> {r} at step {Multiplayer.Step}";
                    ProbePlugin.I.Log.LogInfo("probe trigger: " + ProbePlugin.last);
                }
                string st = $"{{\"installed\":{(Multiplayer.Installed ? 1 : 0)},\"api\":{Multiplayer.NativeApi},\"has_rand\":{(Multiplayer.HasRand ? 1 : 0)}," +
                            $"\"active\":{(Multiplayer.Active ? 1 : 0)},\"role\":\"{Multiplayer.Role}\",\"step\":{Multiplayer.Step},\"patched\":{ProbePlugin.patched}," +
                            $"\"shared_in_update\":{(Multiplayer.RandShared ? 1 : 0)},\"h\":{ProbePlugin.seqH},\"t\":{ProbePlugin.seqT},\"triggers\":{done}," +
                            $"\"last\":\"{ProbePlugin.last.Replace("\"", "'")}\"}}";
                File.WriteAllText(Path.Combine(ProbePlugin.Dir, "rand_probe.status.tmp"), st + "\n");
                File.Move(Path.Combine(ProbePlugin.Dir, "rand_probe.status.tmp"), Path.Combine(ProbePlugin.Dir, "rand_probe.status"), true);
            }
            catch (Exception e) { ProbePlugin.I.Log.LogError("probe: " + e.Message); }
        }
    }
}
