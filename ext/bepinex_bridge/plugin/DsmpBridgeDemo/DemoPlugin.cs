// Test plugin for the bridge (not for players). Every 0.5 s it looks for <DSRIG_DIR>/bridge_demo.trigger:
//   "gold <n>"   -> Multiplayer.AddGold(n)               (native action: runs on Mac peers without BepInEx too)
//   "note <txt>" -> Multiplayer.Do("demo.note", txt)     (managed action: every peer needs this plugin)
//   "cgold <n>"  -> Multiplayer.Do("demo.gold", n)       (managed action that changes game state: the C# handler adds n to
//                   ClanDataContainer.ClanGold through the interop types, on every peer at the same step)
// The trigger file is renamed to bridge_demo.trigger.<n>.done. <DSRIG_DIR>/bridge_demo.status gets the plugin's view
// of the session (one JSON line) and the last results; bridge_demo.notes gets one line per applied demo.note.
using System;
using System.IO;
using System.Text;
using BepInEx;
using BepInEx.Unity.IL2CPP;
using DsmpBridge;
using UnityEngine;

namespace DsmpBridgeDemo
{
    [BepInPlugin("dsmp.bridge.demo", "DSMP Bridge Demo", "1.0.0")]
    [BepInDependency(BridgePlugin.Guid)]
    public class DemoPlugin : BasePlugin
    {
        internal static DemoPlugin I;
        internal static string Dir;
        internal static string last = "";
        public override void Load()
        {
            I = this;
            Dir = Environment.GetEnvironmentVariable("DSRIG_DIR") ?? Paths.GameRootPath;
            Multiplayer.Register("demo.note", (player, data, step) =>
            {
                string line = $"{step} {player} {Encoding.UTF8.GetString(data)}";
                File.AppendAllText(Path.Combine(Dir, "bridge_demo.notes"), line + "\n");
                Log.LogInfo("demo.note applied: " + line);
            });
            Multiplayer.Register("demo.gold", (player, data, step) => AddClanGold(BitConverter.ToInt32(data, 0), $"step {step} player {player}"));
            AddComponent<DemoDriver>();
            Log.LogInfo("DSMP Bridge Demo loaded, watching " + Path.Combine(Dir, "bridge_demo.trigger"));
        }

        static Refactor.Main.MainFlow mf;
        internal static void AddClanGold(int delta, string who)
        {
            if (mf == null) mf = UnityEngine.Object.FindObjectOfType<Refactor.Main.MainFlow>();
            var clan = mf?._campaignData?.ClanDataContainer;
            if (clan == null) throw new InvalidOperationException("no campaign loaded");
            int before = clan.ClanGold;
            clan.ClanGold = before + delta;
            I.Log.LogInfo($"demo.gold {delta} ({who}): {before} -> {clan.ClanGold}");
        }
    }

    public class DemoDriver : MonoBehaviour
    {
        public DemoDriver(IntPtr p) : base(p) { }
        float next;
        int done;

        void Update()
        {
            if (Time.unscaledTime < next) return;
            next = Time.unscaledTime + 0.5f;
            try
            {
                string trig = Path.Combine(DemoPlugin.Dir, "bridge_demo.trigger");
                if (File.Exists(trig))
                {
                    string t = File.ReadAllText(trig).Trim();
                    File.Move(trig, trig + "." + (++done) + ".done");
                    string res;
                    if (t.StartsWith("gold ")) res = Multiplayer.AddGold(int.Parse(t.Substring(5))).ToString();
                    else if (t.StartsWith("cgold ")) { int n = int.Parse(t.Substring(6));
                        res = Multiplayer.Do("demo.gold", BitConverter.GetBytes(n), (p, d, s) => DemoPlugin.AddClanGold(n, "local")).ToString(); }
                    else if (t.StartsWith("note ")) res = Multiplayer.Do("demo.note", Encoding.UTF8.GetBytes(t.Substring(5)),
                        (p, d, s) => File.AppendAllText(Path.Combine(DemoPlugin.Dir, "bridge_demo.notes"), $"local {p} {Encoding.UTF8.GetString(d)}\n")).ToString();
                    else res = "unknown trigger";
                    DemoPlugin.last = $"{t} -> {res} at step {Multiplayer.Step}";
                    DemoPlugin.I.Log.LogInfo("trigger: " + DemoPlugin.last);
                }
                var m = Multiplayer.Modset;
                string st = $"{{\"installed\":{(Multiplayer.Installed ? 1 : 0)},\"active\":{(Multiplayer.Active ? 1 : 0)},\"role\":\"{Multiplayer.Role}\"," +
                            $"\"player\":{Multiplayer.Player},\"step\":{Multiplayer.Step},\"rule\":\"{m.rule}\",\"modset_lines\":{m.lines.Length}," +
                            $"\"triggers\":{done},\"last\":\"{DemoPlugin.last.Replace("\"", "'")}\"}}";
                File.WriteAllText(Path.Combine(DemoPlugin.Dir, "bridge_demo.status.tmp"), st + "\n");
                File.Move(Path.Combine(DemoPlugin.Dir, "bridge_demo.status.tmp"), Path.Combine(DemoPlugin.Dir, "bridge_demo.status"), true);
            }
            catch (Exception e) { DemoPlugin.I.Log.LogError("demo: " + e.Message); }
        }
    }
}
