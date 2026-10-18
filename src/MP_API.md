# Multiplayer API for mod makers (MP_API_VERSION 2, bridge api 2)

Two ways in:
- **C / native mods** (loader modules): `mp_api.h`. Find the multiplayer image with `module_loaded("multiplayer")`,
  then `dlsym(<image>, "mp_api_get")` (Windows: `GetProcAddress` on `multiplayer.dll`). Ask for the version you need:
  `mp_api_get(2)` returns NULL on an older multiplayer. Fields are only appended: check `version` / `size`.
- **C# / BepInEx mods**: `DsmpBridge.dll` (mod id `dsmp-bridge`, plugin id `dsmp.bridge`), or the copy-in file
  `ext/bepinex_bridge/plugin/DsmpLite/DsmpLite.cs`. Both call the `dsmp_bridge_*` exports of `mp_bridge.h`.

With no multiplayer mod installed, every call below still works: random draws are local, chat returns "no session".

## 1. Seeded random (same value on every player)

| C (`mp_api_table`) | Export | C# (`DsmpBridge.Multiplayer`) | Returns |
|---|---|---|---|
| `rand_u32(mod_id, stream)` | `dsmp_bridge_rand_u32` | `Rand(modId, stream)` | 32 random bits |
| `rand_range(mod_id, stream, lo, hi)` | `dsmp_bridge_rand_range` | `RandRange(modId, stream, lo, hi)` | lo .. hi-1 (hi <= lo: lo) |
| `rand_value(mod_id, stream)` | `dsmp_bridge_rand_value` | `RandValue(modId, stream)` | 0 <= v < 1 (24 bits) |
| `rand_shared()` | `dsmp_bridge_rand_shared` | `RandShared` | 1 = a draw now is the shared one |

- `mod_id`: your mod's id (for example `"delvers"`). `stream`: any number you choose (one per feature keeps features
  independent: a new draw in one feature never shifts another).
- **Shared draw** = called on the game's main thread **inside the lockstep step**:
  a `register_tick` or `register_sim_cmd` callback, a bridge channel handler (`Register` / `dsmp_bridge_set_dispatch`),
  or game code the world tick runs (a Harmony patch or a hook on a sim method). The value is a pure function of
  (session seed, mod id, stream, campaign tick, lockstep step, how many draws of this (mod id, stream) came before in
  this step). Nothing is saved or sent; a player who joins late gets the same values.
- **Local draw** = anywhere else: UI code, a key press, a frame callback, another thread, the title screen, single
  player. It never moves the shared counters, so a UI draw on one player cannot shift the others.
  Never use a local draw to change game state in a session (the players would differ = desync).
- Same rule as the game itself: all players must make the same shared draws in the same order. Do not draw a
  different number of times on one player (for example only when a window is open there).

### UnityEngine.Random and System.Random in a mod
- Our determinism layer already routes the game's `UnityEngine.Random` (Range, value) inside the step to its sim
  stream, and `new System.Random()` (no seed) to a counter seed. A mod's call goes through the same hooks.
- Proven on a hidden Mac pair, both players with BepInEx (section 5): `UnityEngine.Random.Range` / `.value` called
  from C# inside the step (a bridge handler, a Harmony postfix on `TickFlow.Tick`) went to the sim stream and gave the
  same values on both players. So the rule is the same as for `Rand`: inside the step it is shared, outside it is not.
  `Rand` is still the better choice: its own streams per mod, so another mod's extra draw never shifts yours.
  Windows: same hooks, not yet run with BepInEx on a pair.
- `new System.Random(seed)` with your own seed is deterministic only if the seed is the same on every player.

## 2. Chat lines

| C | Export | C# | |
|---|---|---|---|
| `chat_line(mod_name, text, MP_CHAT_LOCAL)` | `dsmp_bridge_chat(mod, text, 0)` | `Chat(modName, text)` | a line on this screen only |
| `chat_line(mod_name, text, MP_CHAT_BROADCAST)` | `dsmp_bridge_chat(mod, text, 1)` | `Chat(modName, text, true)` | a line every player sees |

- Shown as `<Mod> mod: text` in the system colour (the word "mod" is translated). A broadcast line starts with the
  sender's name in the player's colour.
- Limits: mod name 24 bytes, text 200 bytes (local) / 160 bytes (broadcast), cut on a character boundary. Rich-text
  tags are shown as typed (escaped). Newlines and other control bytes become spaces.
- Rate limits per mod name: local 5 at once, then 1 a second; broadcast 3 at once, then 1 every 2 seconds. Each
  player's screen also takes at most 5 mod lines at once, then 1 a second, from one sender.
- Results (`MP_CHAT_*`, C# `ChatResult`): 0 shown / sent; 1 no multiplayer session (there is no chat on screen in
  single player: show it your own way); -1 bad arguments (empty name or text); -2 spectators cannot broadcast;
  -5 rate limited (the line is dropped); -6 not sent (no connection). C# also has `NoNative` (no multiplayer mod).
- Chat runs outside the simulation. Never use a chat line to carry game state; use a bridge channel or a sim command.
- The player's "Chat: Off" setting hides mod lines too.

## 3. Actions through the lockstep (unchanged, api 1)
`dsmp_bridge_send(channel, data)` / C# `Multiplayer.Send` / `Do`: one order that runs on every player at the same step.
`register_sim_cmd`, `register_tick`, `register_fingerprint`, `register_joinpoint` for native modules (mp_api.h).

## 4. C# (BepInEx)
- Install `dsmp-bridge` (DsmpBridge.dll). Reference DsmpBridge.dll and use the static class `DsmpBridge.Multiplayer`.
  No call throws.
- `Installed`: a multiplayer mod with bridge api >= 1 is loaded. `HasRand`: api 2 random is there. With no multiplayer
  mod, or an older one, `Rand*` give a local random and `Chat` returns `NoNative`.
- If your mod must also run without DsmpBridge.dll:
  - soft dependency: `[BepInDependency("dsmp.bridge", BepInDependency.DependencyFlags.SoftDependency)]`; put every
    DsmpBridge call in its own class with `[MethodImpl(MethodImplOptions.NoInlining)]` methods; call them only when
    `IL2CPPChainloader.Instance.Plugins.ContainsKey("dsmp.bridge")` is true;
  - or no reference at all: paste `ext/bepinex_bridge/plugin/DsmpLite/DsmpLite.cs` into your mod (same Rand and Chat
    calls, no channels).
- `dsmp-bridge` is a gameplay mod: every player in a session needs the same version (the join check compares it).

## 4b. Harmony on a method the multiplayer mod also hooks
- The multiplayer mod hooks about 90 game methods (every `rig_hook(...)` call in `src/`, for example
  `MainFlow.Update`, `TickFlow.Update`, `EventQueue.Update`, `UnityEngine.Random.*`, `Guid.NewGuid`, the save calls).
  Its hooks go in first, when the game code is mapped; Harmony patches later, in your plugin's `Load()`.
- Mac: proven to chain (loader test `l1_bie_test.py`: a Harmony postfix on `MainFlow.Update` and our hook both run).
- Windows: one Wine dry run CRASHED inside Harmony's detour (dobby.dll) when it patched `MainFlow.Update` on top of
  our hook. The real Windows test (`loader/test/winharmony`) runs on the VM next. Until it passes: do not Harmony-patch
  a method the multiplayer mod hooks; patch a method next to it (for example `TickFlow.Tick` instead of
  `TickFlow.Update`). Calling those methods (not patching them) is always fine.

## 5. Test results
- Unit (`src/mp_api_test`, `make test`): two players with the same seed draw the same values even when one also draws
  in its UI; a late joiner matches from its first step; flood limit; text cleaning. Each check fails on a real break.
- Hidden Mac pair, chat + local random (`ext/mpapi/chat_pair.py`, 2026-10-18): broadcast shown on both with the
  sender's name and escaped tags; local line only on its own screen; 20 broadcasts at once = 3 sent, 17 rate limited,
  3 lines on the other screen; draws outside the step local; 0 desyncs, full-state checks equal.
- Hidden Mac pair, both players with BepInEx (`ext/bepinex_bridge/tests/rand_pair.py`, 2026-10-18, mpapi 47280ad):
  12 draw sets (6 in bridge handlers sent from both players, 6 in a `TickFlow.Tick` Harmony postfix), each 6 x
  Range(int) + 6 x value + 6 x Range(float) + 6 x `Rand`: all 12 equal on both players, `RandShared` 1 inside, 0 in
  `Update`; det counters draws_view_in_sim 0; full-state checks equal; fingerprints equal on 2283 ticks; 0 desyncs.
