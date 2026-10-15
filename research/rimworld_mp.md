# RimWorld Multiplayer (rwmt/Multiplayer): how its lockstep works

Repo is cloned at `~/ds_multiplayer/research/rwmt_Multiplayer`, commit `4a3be276` from 2026-04-29. It was not built or run. All paths below are relative to `.../rwmt_Multiplayer/Source/`. Anything marked **[INFERRED]** is my reasoning and was not seen in code.

## Summary (about 200 words)

RimWorld Multiplayer uses deterministic lockstep with a server that sets the order of commands. Every game has a server. Usually it runs as a thread inside the host's game, and the host's own client talks to it in memory. A standalone headless server also exists.

Clients never pick a tick for their own commands. The server stamps each command with its own timer at the moment it receives it, then sends it to everyone. About 30 times a second the server also sends `tickUntil`. Clients may only simulate up to that tick, so every command is known before its tick comes. That is the whole sync contract.

Game speed and pause are commands too. They change how many game ticks run per timer tick, so the server never needs to know the speed.

Any game function the UI calls to change state is intercepted. Instead of running, it is turned into a command, and it runs on every machine when that command executes.

For determinism, the mod keeps its own RNG state per map, per world and per command. It swaps that state in only while the simulation runs. It also removes many frame-, camera- and thread-dependent effects.

To detect desyncs, the host sends the RNG states plus stack-hash traces of every random call every 30 ticks, and clients compare. To recover, every player saves and reloads at a "join point". A joiner or a desynced player loads that save and fast-forwards through the logged commands.

---

## A. Tick model

**There are two clocks.**
- **Server `gameTimer`**: a "timer tick" that targets 60 per second (`Common/MultiplayerServer.cs:69` `StandardTimePerTick = 1000/60`). The server loop (`MultiplayerServer.cs:104-158`) increments `gameTimer` at most 2 times per loop pass (`:120`). The net loop sleeps to about 30 Hz (`:32`, `:155`).
- **Client `TickPatch.Timer`**: the client's copy of that timer. A client may simulate only `Timer < tickUntil` (`Client/Patches/TickPatch.cs:22`, `:81-84`, `:236`).

**How the client learns `tickUntil`.** Every net tick (about 30 Hz) the server sends `ServerTimeControlPacket(gameTimer, sentCmdsSnapshot, serverTimePerTick)`. It goes **unreliably** (`MultiplayerServer.cs:192`; packet `Common/Networking/Packet/TimeControlPacket.cs`). The client stores it, but only advances `tickUntil` once `receivedCmds >= remoteSentCmds` (`Client/Session/MultiplayerSession.cs:123-127`; `Client/Networking/State/ClientBaseState.cs:21-29`).

This is the key trick. A timer value is only accepted after every command the server sent before that timer advanced has arrived. Commands travel on the reliable ordered channel.

**How a command gets its tick.**
1. The client sends `ClientCommandPacket(type, mapId, data)` (`Client/Util/Extensions.cs:78`).
2. The server's `CommandHandler.Send` stamps `ticks = server.gameTimer` and the sender's player id and faction id (`Common/CommandHandler.cs:40-46`).
3. The server logs the command for join points (`:50-51`) and broadcasts it to all playing clients (`:64`).
4. Clients queue it (`MultiplayerSession.cs:130-143`). Since issue #733 all commands go into one queue to keep a single global order (`:139-140`).
5. Commands run when `cmd.ticks == Timer`, before that timer step's ticks (`TickPatch.cs:168-190`, `:197-203`).

A command stamped T can always arrive before the client reaches T. `tickUntil` is never greater than the server timer at send time, and the client runs only ticks below `tickUntil`.

**Input delay.** There is no fixed input-delay constant. Your own command runs only after the server round trip; there is no local prediction. The client keeps a buffer of about 3 to 7 timer ticks behind `tickUntil`:
- If 3 or fewer ticks behind, it runs 1.2× slower.
- If 7 or more behind, it runs 0.8× faster (shorter time per tick).
- Source: `TickPatch.cs:73-76`.

**[INFERRED]** So the delay is about RTT to the server plus 3 to 7 ticks (about 50 to 120 ms). For the host the RTT is about zero, because it uses an in-memory connection (`Client/Networking/NetworkingInMemory.cs`).

**One timer step per rendered frame.** `ticksToRun` is set to 1 per frame outside replays, and spare time is clamped to 0 (`TickPatch.cs:85-94`). A low-FPS client therefore falls behind. Each client reports its average frame time every 32 ms (`:96-100`). The server sets `serverTimePerTick` to the **maximum** client frame time, clamped to 16.7 to 66.7 ms (`MultiplayerServer.cs:194-200`). The slowest machine sets the pace.

**Slow peers (everyone waits).**
- The server stops advancing `gameTimer` if any in-game "Playing" player is more than 90 ticks behind (extrapolated). It also needs at least one player under 40 ticks behind (`MultiplayerServer.cs:122-135`).
- The server slows down by up to 3× in proportion to the largest `ticksBehind` divided by 60 (`:137-142`).
- `ticksBehind` comes from keep-alive replies, sent 5 times a second (`:183-188`; `ClientBaseState.cs:12-18`; `Common/Networking/State/ServerPlayingState.cs:125-139`). It is extrapolated in `Common/ServerPlayer.cs:20`.
- Players still loading or catching up (status `Simulating`) do not block, because only `PlayingIngamePlayers` count (`MultiplayerServer.cs:44`).

**Catch-up.** When a client is "Simulating" (joining or rejoining), it runs as many ticks as fit in 25 ms of each frame until it reaches the target (`TickPatch.cs:197`). Everything else is normal ticking.

**Freeze.** When a client starts a long event inside the simulation (for example map generation), it sends `ClientFreezePacket` (`Client/Patches/LongEvents.cs:55,65`). `FreezeManager` follows the host's freeze state. On unfreeze it waits for all players, up to `MaxFreezeWaitTime` (`Common/FreezeManager.cs:28-49`). Clients stop at the `frozenAt` tick (`TickPatch.cs:44`).

**[INFERRED]** That wait constant is 600 NetTimer ticks. NetTimer runs at about 30 Hz, so this is about 20 seconds, not the 10 seconds the code comment says.

**Game speed.** Speed is not a server concept. Each "tickable" (the world, and each map) has a `DesiredTimeSpeed`. `TickRateMultiplier` maps it to game ticks per timer tick:
- Paused 0, Normal 1, Fast 3, Superfast 6 (12 if nothing is happening), Ultrafast 15 (`Client/AsyncTime/AsyncTimeComp.cs:21-51`).
- `DoTick` adds 1 to `TimeToTickThrough` and runs `Tick()` that many times (`TickPatch.cs:216-263`).

Speed changes are commands (`GlobalTimeSpeed`, `MapTimeSpeed`, `TimeSpeedVote`, `PauseAll`; see `Common/CommandType.cs`). They take effect at a known tick on all machines (`Client/AsyncTime/AsyncWorldTimeComp.cs:202-215`, `:292-324`).

When paused, the timer keeps advancing but no game ticks run, so commands still execute while paused. An optional "async time" mode gives each map its own speed and tick counter (`AsyncTimeComp.cs:81`, `:120-122`). Otherwise every tickable uses the slowest rate (`TickPatch.cs:286-296`).

## B. Command format and what becomes a command

**Envelope (`Common/ScheduledCommand.cs:5-32`).** `type` (byte enum), `ticks` (int), `factionId`, `mapId` (-1 means global), `playerId`, and `data` (prefixed bytes). The command types are listed in `Common/CommandType.cs`: GlobalTimeSpeed, TimeSpeedVote, PauseAll, CreateJoinPoint, InitPlayerData, Sync, DebugTools, MapTimeSpeed, Designator, PlayerCount.

**`Sync` payload.**
1. `syncId` (int index into `Sync.handlers`).
2. Optional context: mouse cell, the selected objects, the world selection, and whether the "queue order" key was down (`Client/Syncing/SyncUtil.cs:98-119`).
3. Target object, then arguments, serialized by type (`Client/Syncing/Handler/SyncMethod.cs:73-137`).

Object references are written as stable IDs. For example, a Thing is written as `thingIDNumber` and resolved through a `ThingsById` map (`Client/Syncing/Dict/SyncDictRimWorld.cs:802`, `:864`).

The `syncId` is the handler's index after a stable sort by version (`Client/Syncing/Sync.cs:23-29`). So all players need the same mod set; this is checked at join (see F).

**Handler kinds (`Client/Syncing/Handler/`).**

| Handler | What it does | Where |
|---|---|---|
| **SyncMethod** | A patch at the start of a method. If `Multiplayer.ShouldSync`, it serializes `(instance, args)`, sends them, and **skips the original**. When the command executes, it calls the original with the deserialized args. | `Client/Syncing/SyncTemplates.cs:16-24`, `:58-92`; `SyncMethod.Handle` `:145-188` |
| **SyncField** | `Watch()` saves a field's value before a UI block runs. After the block, if the UI changed it, the code puts the **local value back** and sends a command that sets it. A buffered mode for sliders sends only after 200 ms without changes. | `Handler/SyncField.cs:35-124`; `Client/Syncing/SyncFieldUtil.cs:27-108` |
| **SyncDelegate / SyncMethod.Lambda** | Syncs compiler-generated closures (gizmo buttons, float-menu options) by serializing the captured fields. | `Handler/SyncDelegate.cs:14-45`; examples `Client/Syncing/Game/SyncDelegates.cs:20-45` |
| **SyncAction** | Float menus: re-generates the option list on every machine and picks the option by index. | `Handler/SyncAction.cs` |
| **Designator** | A dedicated command type for map tools: the mode plus the cells or thing. | `Client/AsyncTime/AsyncTimeComp.cs:343-446` |

**Which functions are synced.** The list is hand-written: hundreds of registrations. By grep line count there are about 311 lines in `Syncing/Game/SyncMethods.cs`, 222 in `SyncDelegates.cs`, 60 in `SyncFields.cs`, plus 59 `[SyncMethod]` attributes. Examples are at `SyncMethods.cs:24-60` (draft, work priority, `TryTakeOrderedJob`, bills, zones, and so on).

The rule is to sync the **narrowest state-changing function the UI calls**, not the UI itself. The runtime gate is:

```
ShouldSync = InInterface && !dontSync
InInterface = Client != null && !Ticking && !ExecutingCmds && !reloading && ProgramState == Playing && no long event
```
(`Client/Multiplayer.cs:60-73`)

The same function runs normally when the simulation or a command calls it, and becomes a command when the UI calls it. Some handlers are host-only or debug-only, and the server drops them from other players (`Common/CommandHandler.cs:21-31`). "Ghost mode" sends nothing (`Handler/SyncHandler.cs:22`).

While a command executes, the issuer's UI context is replayed. Mouse cell and the queue key are patched to return the recorded values (`Client/Patches/Patches.cs:172-208`). The selection is swapped in, then restored (`SyncUtil.cs:52-93`; `AsyncTimeComp.cs:254-305`). The current map is temporarily set to the command's map (`AsyncTimeComp.cs:242-243`, `:316-317`). The issuer's dev mode and god mode are applied (`PlayerData.SetContext`, `Client/Comp/Game/PlayerData.cs:23-27`).

## C. Authority and transport

**The server orders everything.** It is a relay plus sequencer. It does not simulate the game; it holds only save bytes and command logs (`Common/WorldData.cs`). It stamps ticks, applies policy (host-only commands, dev mode, time control), and keeps the command logs and the latest join-point save.
- **Normal setup**: the server runs as a thread in the host's process (`Client/Networking/HostUtil.cs:199-207`). The host's client uses an in-memory `LocalConnection` (`Client/Networking/NetworkingInMemory.cs:8-30`).
- **Standalone server**: `Server/Server.cs` is a headless .NET program with `settings.toml` and `save.zip`. It supports LiteNetLib only; Steam and the Arbiter are not supported there (`Server/Server.cs:~20-60`).

**The host is special.**
- The host's sync opinions are the reference for desync checks (`Common/Networking/State/ServerPlayingState.cs:142-154`).
- The host uploads join-point saves (`:83-105`).
- The host answers trace requests (`:27-31`; `Common/PlayerManager.cs:112`).
- Freeze follows the host (`FreezeManager.cs:32-41`).

**The Arbiter.** The host can start a second headless copy of the game: `-batchmode -nographics -arbiter`, connected over localhost (`HostUtil.cs:214-247`). When the Arbiter is present, it replaces the host as the reference for sync opinions and world uploads (`ServerPlayingState.cs:86`, `:144-145`). It is a neutral "truth" machine.

**Transport.**
- LiteNetLib over UDP: `ReliableOrdered` for commands, `Unreliable` for time control, keep-alive and cursors (`Common/Networking/LiteNetConnection.cs:13`; `Common/LiteNetManager.cs`).
- Steam P2P with reliable and unreliable channels (`Client/Networking/NetworkingSteam.cs:31-35`).
- Large data is sent in fragments of 1024 bytes, up to 32 MB in total (`Common/Networking/ConnectionBase.cs:72-106`).
- Default port 30502 (`MultiplayerServer.cs:26`).

## D. Determinism

**RNG.**
- Each tickable owns a `randState` (ulong). It is seeded from the world's constant seed and the map id (`AsyncTimeComp.cs:93-109`; `AsyncWorldTimeComp.cs:66-78`).
- `PreContext` does `Rand.PushState(); Rand.StateCompressed = randState`. `PostContext` saves the state back and pops it. This happens around every tick and every command (`AsyncTimeComp.cs:179-204`; `AsyncWorldTimeComp.cs:144-169`).
- UI code that uses `Rand` outside these windows therefore touches only the outer, non-simulation state.
- The state is saved with the game (`AsyncTimeComp.cs:206-214`).
- Cosmetic random consumers are wrapped in their own `Rand` push/pop, so they do not consume simulation RNG: sounds, effecters, motes, flecks, lightning mesh, hair (`Client/MultiplayerStatic.cs:322-354`; `Client/Patches/Seeds.cs:146-161`). `Def` constructors are wrapped too (`Client/EarlyInit.cs:20-25`).
- Per-object seeding is used where RNG is called from unclear places: spawn rotation from thingId plus the world seed, grammar, pawn graphics, precepts (`Seeds.cs:114-238`). Map load and long events are seeded too (`Seeds.cs:14-112`).

**IDs.** `UniqueIDsManager.GetNextID` returns **local negative IDs** when called from the UI. This stops the UI from advancing the simulation's ID counters (`Client/Patches/UniqueIds.cs:11-31`). Negative IDs are fixed up on save and load (`:124-160`).

**Unordered collections and hash codes.**
- Reference-identity hash codes differ per machine and change the order of `HashSet` iteration. The mod replaces them with ID-based hashes, for example `GlowGrid.litGlowers` hashed by `thingIDNumber` (`Client/Patches/HashCodes.cs:42-58`).
- `TargetInfo`/`GlobalTargetInfo` hashes use the map id (`:60-83`).
- `WindManager` uses `map.Tile` (`:14-40`).
- `System.HashCode.Combine` (2 to 8 ints) is replaced with `Gen.HashCombineInt`, because .NET seeds it per process (`:97-240`).
- Archive sorting uses IDs (`Client/Patches/Determinism.cs:190-207`).
- Zone and Plan cells are shuffled only while ticking (`Determinism.cs:150-188`).

**Floats.**
- There is no fixed-point maths. The mod relies on identical IEEE behaviour.
- The CPU floating-point rounding mode is checked at join (`Common/Networking/State/ServerJoiningState.cs:120-122`) and in every sync opinion (`Client/Desyncs/ClientSyncOpinion.cs:35-36`; `Common/RoundMode.cs:10-19`).
- Floats and doubles are saved at full precision (G9/G17), so save then load gives the same bits (`Client/Patches/Patches.cs:126-141`; hooked at `MultiplayerStatic.cs:405-413`).

**Time- and frame-dependent code.**
- `deltaTime` in `MapBrightnessTracker` is replaced with the constant 1/60 (`Determinism.cs:508-539`).
- The ship countdown moved from Update to the tick (`Client/ConstantTicker.cs:100-123`).
- Region, room, power and glow "Update" managers are now called inside the tick and after each command, not in the frame update (`AsyncTimeComp.cs:165-175`, `:289`).
- The camera-dependent "variable tick rate" is replaced by one driven by synced player count per map (`Client/Patches/VTRSyncPatch.cs:10-63`; `PlayerCount` command at `AsyncWorldTimeComp.cs:229-248`).
- Pawn draw position: the UI sees the interpolated position, the simulation sees the root position (`Determinism.cs:19-33`).
- Motes skip "current map" and counter-saturation checks (`Determinism.cs:601-636`).
- Fixes for vanilla code that uses `Find.CurrentMap` where it should use the target map (`Determinism.cs:687-726`).

**Threads.**
- Pathfinder jobs are force-completed before each map tick (`AsyncTimeComp.cs:124-128`). An alternative snapshot patch is disabled (`Client/Patches/PathFinderPatch.cs`, `#if false`; its comment explains the race).
- `FastTileFinder` is forced to a single batch (`Determinism.cs:728-754`).

**UI code that touches state.**
- Caches computed in the UI get a special `CachedInInterface` status, so the simulation recomputes them: pawn capacities and `StatWorker.GetValue` (`Determinism.cs:396-506`).
- UI-triggered recalculations with side effects are blocked: thoughts, abilities, `PriorityWork.Clear`, story watcher (`Determinism.cs:227-394`).
- The UI reads per-map time through a `TimeSnapshot` swap (`Client/AsyncTime/SetMapTime.cs:13-40`).

**Categories, as the code names them.**
- "Non-deterministic patches 1": RNG isolation of effects (`MultiplayerStatic.cs:322`).
- "Non-deterministic patches 2": thing and faction context on Tick, TakeDamage and Kill, plus full-precision saving (`:356-413`).
- "Map time gui patches" (`:415`).
- The files `Determinism.cs`, `Seeds.cs`, `HashCodes.cs`, `UniqueIds.cs`, `VTRSyncPatch.cs`, `Feedback.cs`, `TimestampFixer.cs`.

## E. Desync detection

**What is collected.** A `ClientSyncOpinion` covers each block of 30 timer ticks (`ConstantTicker.cs:77-98`; `Client/Desyncs/ClientSyncOpinion.cs:12-31`). It holds:
- The high 32 bits of the RNG state after **every map tick**, per map (`AsyncTimeComp.cs:152`).
- The same after every **world tick** (`AsyncWorldTimeComp.cs:140`) and after every **command** (`AsyncTimeComp.cs:321`; `AsyncWorldTimeComp.cs:267`).
- A list of **trace hashes**, one per `Rand.Value`/`Rand.Int` call, `GetNextID`, Thing spawn and Thing despawn, made only while ticking or running commands (`Client/Desyncs/DeferredStackTracing.cs:20-88`).
- Each trace hash combines the stack hash, its depth and the RNG state (`Client/Desyncs/SyncCoordinator.cs:215-242`).
- The stack is walked natively through frame pointers and Mono's last-managed-frame data. Only return addresses are stored. Method-name hashes feed the trace hash, and full names are resolved only after a desync (`Common/DeferredStackTracingImpl.cs:114-174`; `Common/Native.cs`).
- Noisy subsystems are excluded from traces (`DeferredStackTracing.cs:100-140`).

**How the comparison works.**
1. Only the host (or the Arbiter) sends its opinion: every 30 ticks, fragmented, to the server (`ConstantTicker.cs:87-89`).
2. The server keeps the last 10 for joiners and relays each one to all other players (`ServerPlayingState.cs:142-154`).
3. Each client matches the host's opinion to its own local opinion with the same `startTick` (`SyncCoordinator.cs:47-108`).
4. It compares, in order: rounding mode, the map list, per-map RNG lists, world RNG, command RNG, then the trace hashes (`ClientSyncOpinion.cs:33-60`).

**What the user sees.**
1. `session.desynced = true` stops ticking (`SyncCoordinator.cs:95-97`; `TickPatch.cs:42`).
2. The client finds the first differing trace index (`:138-156`) and sends `ClientDesyncedPacket(tick, diffAt)` (`:129`).
3. A `DesyncedWindow` opens with five buttons: **Try resync**, Save, Chat, Open desync folder, Quit (`Client/Windows/DesyncedWindow.cs:47-90`).
4. The server marks the player `Desynced`. It asks the host for traces around `diffAt` and forwards them to that player (`Common/PlayerManager.cs:109-121`; `ServerPlayingState.cs:27-31`).
5. It pauses everyone if `pauseOnDesync` is on (the default) and forces a new join point (default flag `Desync`) (`Common/ServerSettings.cs:26,32`).

**Debug bundle.** `MpDesyncs/*.zip` contains `desync_info`, local and host traces (± radius around the diverging call), local and host jitted-method tables, logs, metadata, and a **replay** (save plus command log) (`Client/Desyncs/SaveableDesyncInfo.cs:35-70`; `Client/Saving/Replay.cs`).

## F. Joining and recovery

**Join flow (`Common/Networking/State/ServerJoiningState.cs:14-40`).**
1. Protocol check, then username and password.
2. The client sends its def list with per-def hashes and counts, plus its rounding modes. The server compares and rejects on mismatch (`:116-162`).
3. By default the server pauses everyone (`pauseOnJoin`) and starts a join point.

**Join point (`Common/WorldData.cs:32-56`).**
- The server issues a `CreateJoinPoint` command.
- At that exact tick, **every client saves and reloads its game** (`AsyncWorldTimeComp.cs:217-220`, `:274-281`; `Client/Saving/SaveLoad.cs:21-58`). In multifaction games they always save as the same faction to keep this deterministic (`SaveLoad.cs:52-56`).
- The host or Arbiter uploads the compressed world, maps and session data (`ServerPlayingState.cs:83-105`).
- The server swaps in a new command log that starts from that point (`WorldData.cs:49-56`).

**Sending the world.**
- The server waits for the join point (`ServerLoadingState.cs:30-39`).
- It sends faction id, `gameTimer`, `SentCmds`, the frozen flag, the save, the per-map command lists, the per-map data and the last 10 sync opinions (`:41-90`).
- The client loads the save and sets `tickUntil` (`Client/Networking/State/ClientLoadingState.cs:108-118`).
- It enters `Simulating` mode toward `tickUntil`, and replays the logged commands at their ticks (up to 25 ms of ticks per frame).
- When done it sends `Client_WorldReady`, and its status becomes Playing (`:130-148`; `ServerPlayingState.cs:9-13`).

**Rejoin after a desync is the same path.**
1. `Client_RequestRejoin` (`Client/Session/Rejoiner.cs:10-34`).
2. The server moves the player to the Loading state and resets that player's time votes (`ServerPlayingState.cs:15-20`).
3. The player waits for the forced join point, loads it and catches up.

**How long it takes.** The code has no timing figures. **[INFERRED]** Total time = save size / bandwidth + load time + (ticks since the join point × cost per tick). Auto join points on join and on desync keep the catch-up part short, and joins are throttled (`WorldData.cs:34`, at least 30 work ticks between join points).

## G. Pause and speed with several players

`ServerSettings.timeControl` (`Common/ServerSettings.cs:135-140`) has three modes:
- **EveryoneControls**: any player's speed command applies (`Client/AsyncTime/TimeControlUI.cs:240-244`).
- **LowestWins**: each player votes per tickable (`TimeSpeedVote`). The effective speed is the **minimum** vote, and with no votes it is Paused. One player voting pause pauses the game (`Client/Comp/Game/PlayerData.cs:29-37`; `Client/Comp/Game/MultiplayerGameComp.cs:60-67`; `AsyncWorldTimeComp.cs:308-324`). Votes are reset when a player leaves or desyncs (`Common/ServerPlayer.cs:120-129`).
- **HostOnly**: the server drops speed commands from non-hosts while the host is present (`Common/CommandHandler.cs:33-37`).

**Automatic pauses.**
- `pauseOnJoin` and `pauseOnDesync` (`PauseAll`).
- `pauseOnLetter` (default `AnyThreat`) pauses the affected map deterministically inside the simulation (`Client/AsyncTime/AsyncTimePatches.cs:181-209`).
- Shared "sessions" (trade, caravan forming, rituals and similar) pause their map while open (`AsyncTimeComp.cs:25-29`; `Client/Persistent/SessionManager.cs:260-269`).
- Freeze during long events (see A).

Hotkeys are ignored for 0.4 s after another player changed the speed (`TimeControlUI.cs:130-132`).

## H. Per-player things

**Local per player.** Camera, current map, selection, open windows and inspect panels are local. During a command the code temporarily swaps in the issuer's map, selection and mouse cell, then restores the local ones (`AsyncTimeComp.cs:241-319`; `SyncUtil.cs:52-93`).

**Shared for display only.** These are not simulation state:
- Cursors: 20 Hz, unreliable, with a sequence number (`Client/UI/PlayerCursors.cs:19-59`; `ServerPlayingState.cs:107-115`).
- Selections: 5 Hz (`PlayerCursors.cs:27-30`, `:62-90`).
- Map pings and chat.

**Feedback only for the issuer.** Sounds, targeting, tutor dialogs and camera jumps from a command play only if this player issued it (`Client/Patches/Feedback.cs:16-31`, `:37-56`). There is no camera jumping during catch-up (`:164-175`).

**Ownership.** It is per **faction**, not per pawn.
- Each player has a `FactionId`.
- In "multifaction" mode, the host gets the main faction and new players start as spectators until they create or choose a faction (`Common/PlayerManager.cs:136-141`; `ServerPlayingState.cs:184-198`).
- Commands carry the faction, and `Faction.OfPlayer` is swapped in during execution (`Client/Factions/FactionContext.cs:11-36`; `AsyncTimeComp.cs:250`).
- I found no per-pawn owner concept; a grep for owner and control-by fields found nothing. Players in the same faction control all of its pawns.
- Dev mode and god mode are per player (`PlayerData.cs`).
- The server tracks which map each player is on with the synced `PlayerCount` command (`ServerPlayingState.cs:38-51`).

## I. Five lessons for a native IL2CPP game hooked from a C dylib

1. **Copy the clock design exactly: server stamps, `tickUntil` window, and a `sentCmds` counter.**
   - Hook the game's simulation step (the function or functions the frame loop calls to advance game logic). Replace "run N ticks this frame" with "run while `Timer < tickUntil`, at most one step per frame (more when catching up). Before each step, execute the queued commands whose tick equals `Timer`."
   - Commands carry `{type, tick, playerId, mapId/scope, payload}`, and only the host-side sequencer assigns ticks.
   - Make speed and pause commands that change "game ticks per timer step", so the network clock never changes.
   - Put the sequencer in the host process and connect to it in memory. That gives one code path for host and clients. This is about 300 lines in their code (`MultiplayerServer.cs`, `CommandHandler.cs`, `TickPatch.cs`).

2. **Intercept at the UI-to-simulation boundary with a global context flag.**
   - Keep three flags in the dylib: `ticking`, `executingCmd`, `inInterface`. These are equivalent to `Multiplayer.cs:60-73`.
   - For each state-changing game function the UI calls (place building, assign job, change setting, buy), install an inline hook. If `inInterface`, serialize the args, send them and **return without calling the original**. If not, call the original. When the command executes, set `executingCmd` and call the trampoline.
   - Serialize object references as stable game IDs, never pointers.
   - Record and replay any UI context the function reads (mouse cell, selected units, modifier keys): hook those getters to return recorded values while `executingCmd`.
   - For settings panels with no clean setter, use their SyncField trick: snapshot before the UI pass, diff after, put the local value back, send the change.
   - **[INFERRED]** You need a list of these functions from the IL2CPP dump. Expect tens to hundreds; RimWorld has hundreds.

3. **Own the simulation RNG and ID counters, and isolate everything cosmetic.**
   - Keep the simulation RNG state in the dylib's own storage, and write it into the game's RNG only during a tick or command. Save it back afterwards.
   - Wrap VFX, sound and particle functions in RNG save and restore.
   - Make IDs allocated from the UI come from a separate local range.
   - **[INFERRED]** First find out which RNG Dungeon Settlers uses. If it is `UnityEngine.Random`, its state is a native static readable through `Random.state`. If it uses `System.Random` instances or a custom RNG, you must hook every instance's state.
   - Replace identity-hash-ordered sets and dictionaries with ID-based hashing, as `HashCodes.cs` does. **[INFERRED]** IL2CPP object hash codes are believed to come from the object's address, so they will differ between machines.

4. **Build cheap, always-on desync detection with call-site traces from day one.**
   - After every tick and command, record the RNG state (and optionally a small state hash). Send it from the host every 30 ticks and compare on clients.
   - On every RNG draw or ID allocation during simulation, hash the return-address chain. For a native binary, make each address relative to the base of `GameAssembly`. **[INFERRED]** Those offsets are then identical on all machines running the same build, so they can be hashed directly and symbolized against the IL2CPP dump only after a desync.
   - On a mismatch, collect both sides' traces around the first differing index, plus a replay (save plus command log).
   - Their experience shows this trace diff is what makes desyncs fixable (`DeferredStackTracing*.cs`, `SyncCoordinator.cs`).

5. **Recover by save and replay, and remove frame and camera coupling.**
   - Join and rejoin need a mid-game save to bytes and a load from bytes. At a commanded tick, everyone saves and reloads, so hidden in-memory state matches the serialized state. The joiner loads the save and fast-forwards the logged commands.
   - Check that save then load gives the same bits, including floats (`Patches.cs:126-141`).
   - Systematically hunt simulation code that reads `Time.deltaTime`, the frame count, the camera or viewport, the current selection or map, "is visible" tests, or results from worker threads or jobs. Move it into the tick or make it constant, as they do for map brightness, the ship countdown, the VTR rate and the pathfinder jobs.
   - **[INFERRED]** One risk RimWorld does not face in the same form: a Mac arm64 build and a Windows x64 build are two separate native compiles. Float results can differ (fused multiply-add contraction, libm `sin`/`cos`/`pow`). Test same-OS lockstep first, and treat cross-platform play as a separate, probably hard, problem.