using BepInEx;
using BepInEx.Unity.IL2CPP;

namespace DsmpBridge
{
    /// <summary>Loads first and connects to the native multiplayer bridge. Mods that use it:
    /// [BepInDependency(DsmpBridge.BridgePlugin.Guid)].</summary>
    [BepInPlugin(Guid, "DSMP Bridge", "1.0.0")]
    public class BridgePlugin : BasePlugin
    {
        public const string Guid = "dsmp.bridge";
        public override void Load()
        {
            Multiplayer.Log = s => Log.LogInfo(s);
            Multiplayer.Init();
            Log.LogInfo($"DSMP Bridge 1.0.0: native {(Multiplayer.Installed ? "found" : "not found")}");
        }
    }
}
