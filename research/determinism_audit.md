# Dungeon Settlers build 25846389: can two copies stay identical in lockstep multiplayer? (determinism audit)

## Summary (about 200 words)

Lockstep multiplayer is possible on this build. Nothing I found makes it impossible. But the game is less ready than the June study said. The core simulation is a fixed tick of 1/60 s. It is driven by one per-frame loop: `MainFlow.Update` (RVA, the function's address in the binary, `0xD1B648`). It uses no `Time.deltaTime`, no physics and no DOTween. Skill and passive "coroutines" are tick-counted iterators, not Unity coroutines. These are all good signs.

**Five problems are new or worse than June:**
1. **Frame grouping.** Each frame runs K ticks back to back, and K depends on the frame rate. The simulation sends about 53 kinds of events (death, spawn, drop, pickup, task finished and others) to a global queue. That queue is only emptied once per frame. So the tick at which these events take effect depends on the frame rate. Region rebuild, story-event triggers and resolver actions work the same way, once per frame.
2. **Pathfinding is threaded.** Unit paths come from A* Pathfinding Project worker threads. The results are applied on whichever tick they happen to finish.
3. **Random numbers.** All gameplay rolls (hit, guard, crit, drops, recruits, events, weather) use the single global `UnityEngine.Random` stream, which nobody seeds. Sound, effects and UI code draw from the same stream. About 10 gameplay sites also use `new System.Random()`, which seeds itself from the clock.
4. **Random Guids decide order.** `Guid.NewGuid` gives runtime entities and tasks random IDs. `TaskMatcher` uses those IDs to break ties when it assigns work.
5. **Many player actions skip the command funnel.** Construction and demolition modes and several event handlers write world state directly. Map transitions wait in real seconds.

**Unordered Dictionary iteration is not a separate risk.** The order depends only on the history of adds and removes, not on hash values.

**Overall: hard, but no blocker.** Both players must be on Apple Silicon.

---

Terms used below:
- **IL2CPP:** Unity's C#-to-native compiler.
- **RVA:** a function's address in `GameAssembly`. The values come from `script.json` and match `dump.cs`.
- **BT:** behaviour tree.
- **RNG:** random number generator.
- **FX:** visual effects.
- **A\*:** the A* Pathfinding Project library.
- **[INFERRED]:** my reasoning. I did not see it directly in code.

Evidence comes from `~/ds_skill_truth/decomp_25846389/*.c` (Ghidra) and `dump_25846389/dump.cs`. The decompiled code covers all 35,274 `Refactor.*` methods; I checked this against `script.json`. Third-party code (Unity, A*, BehaviorDesigner, DOTween, the System libraries) is **not** decompiled.

**About the counts:** each decompiled file stores every function body twice. All counts below are de-duplicated.

## 1. The simulation clock

**Driver.** The only MonoBehaviour that runs gameplay is `Refactor.Main.MainFlow.Update` (`0xD1B648`). I scanned for Update, LateUpdate and FixedUpdate in `Refactor.*` and found no other gameplay MonoBehaviour. Order inside it, one pass per Unity frame (from `Refactor.Main.MainFlow.c`):
1. `TimeHandler.Update`
2. `InputDispatcher.Update`
3. `EventQueue.Update`
4. `EventProcessor.Update`
5. `TickFlow.Update` for Base (MapType 0)
6. `TickFlow.Update` for Dungeon (MapType 1)
7. `ObserverDriven.Update`
8. `LoadUnityObjects.Load`
9. `ViewUpdateDispatcher.UpdateView`
10. `StoreLifecycleCoordinator.Flush`

**Accumulator.** `TimeHandler.Update` (`0xD1BA98`):
- `_remainTime += Time.deltaTime * IGameSpeedReader.GetMultiplier(speed)`.
- Then, `while (_remainTime >= TICK_INTERVAL)`: `_currentTick++`, publish `TickRequested`, subtract the interval.
- It is a fixed step with an unlimited number of ticks per frame. There is no cap.
- `TICK_INTERVAL` = `SystemVar.Campaign.Tick`, read from `GAMESYSTEM_Campaign_TickInterval` = **0.0167 s (about 60 ticks/s at 1x)**. Source: `~/dungeonsettlersdb/data/versions/25846389/GameSystemTable.json`.

**Tick fan-out:**
1. `TickRequested` goes into the global `EventQueue`.
2. `SystemEventHandler.OnEvent(TickRequested)` (`0xDC4CF4`) calls `TimeData.AddTick` (the saved campaign clock, `TimeData.Tick` at +0x10), then the minute, hour and day rollovers, then `HandleOnTick` (`0xDC4DB4`).
3. `HandleOnTick` enqueues `TickRequested(logicalRefresh)` into each map's `TickFlowHandler`.
4. `TickFlow.Update` → `TickFlowHandler.Update` (`0xD28120`) empties **all** queued tick requests for that map.
5. Each one goes through `HandleEvent` to `TickFlow.Tick(bool logicalRefresh)` (`0xAADE88`).

**One tick (`TickFlow.Tick`), in order:**
1. `TickFlowHandler.Tick`: empties the per-map event and force queues.
2. `DungeonBehaviorSimulator.Tick`
3. `WeatherSystem.Tick`
4. `DecisionSystem.Tick`
5. `RandomEventSystem.Tick`
6. `TaskSystem.Tick`: this calls `BehaviorManager.Tick`, so behaviour trees are ticked by hand.
7. `ComponentTickHandler.Tick`
8. `ExecutionSystem.Tick`
9. `DataApplier.EnqueueViewUpdates`
10. `FXSystem.Tick`
11. `_DEBUG_Tick++`

**Constants:**
- Logical refresh: every 60 ticks (`LogicalRefreshInterval`).
- `TickPerMin` = 45, so one game day = 64,800 ticks.

**Speed and pause.**
- `GameSpeedSetting` has `GameSpeed` values Pause=0, Normal=1, Fast=2, VeryFast=3. The multipliers come from `GAMESYSTEM_Campaign_GameSpeed` = [0, 1, 3.5, 7], so up to about 420 ticks/s.
- Pause means multiplier 0, or a `GamePauseReason`: User, EssentialPopup, CheatConsole, Loading, PopupTimeLock, ConsecrationNotice.
- Popups therefore pause the game locally.

**Is gameplay fully tick-driven?**
- **Fully tick-driven:** movement, combat, behaviour trees, statuses and executions. For example, `Execution.Delay(float)` is turned into a tick count: `DelayNode` divides the delay by `Campaign.Tick`.
- **Run once per frame, not once per tick** (driven by frame count, not by frame time):
  - The 234 synchronous handlers of the global event queue, in classes `EntityEventHandler`, `ItemEventHandler`, `TaskEventHandler`, `CraftingEventHandler` and others. These handle spawn, death, drop, pickup, equip, recruit, level-up and trade.
  - `EventProcessor`: control modes, `ResolverActionProcessor` and `RegionProcessor` (`BuildBudget = 30000` per call).
  - `ObserverDriven.Update`: fires story events.

## 2. Gameplay that depends on frames or wall-clock time

| Kind | Count | Examples / evidence | Breaks lockstep? |
|---|---|---|---|
| `Time.deltaTime`/`time`/`unscaled*` in gameplay | **1** (the clock) | All other users are camera (`InputEventHandler` → `CameraSetting`), sound, UI, view and `MouseInputDispatcher` (double-click) | Only through the accumulator |
| **Frame grouping of global events** | about 53 event types sent from inside the tick | `Tick.DataApply.MainEventApplier` builds DeadUnit, SpawnUnit, SpawnItem, PickUpItem, Despawn*, Summon*, CreepClearBatch, MapTransitionRequest and others (about 40). `TaskApplier` adds TaskFinish, TaskGenerate and 6 more. Also `MoveApplier`, `ClanDataApplier`, `DecisionSystem`, `RandomEventSystem` | **Yes, the main issue.** These run on the next frame, after all K ticks of this frame |
| Map-by-map batching | 2 maps | `TickFlow.Update(Base)` runs K ticks, then Dungeon runs K ticks. They are not interleaved | Yes, if the two maps interact |
| Async event coroutines (Unity coroutines) | 14 handlers | `ChangeMapRequested`, `CreepClearBatchRequested` (1 yield), `BaseFieldDiscoverRequested`, `ClearDungeonRegionRequested`, `AutoSaveRequested`, load and quit. Run by `EventQueue.<ProcessAsyncEvent>d__14` | Yes |
| Real-time waits in map transitions | about 5 | `WaitForDefaultTicks` actually uses `WaitForSecondsRealtime` (despite its name). `WaitUntilGameStabilizes` uses `unscaledDeltaTime`. About 40 coroutine classes under `MapTransition*` | Yes, needs a barrier |
| A* async path jobs plus worker threads | 1 system | `MovePathfinder.IPathStarter.StartPath` (`0xA84A10`) calls `AstarPath.StartPath` with an `OnPathDelegate`. `MoveFirstRunner.OnDone` (`0xA81ABC`) takes `Monitor.Enter`, so it is called from worker threads. `FlushDone` in `MoveFirstRunner.Tick` (`0xA810F8`) applies whatever has finished. The synchronous `TryGetPath` uses `BlockUntilCalculated` and is safe | **Yes, serious** |
| A* graph updates | a few | `AStarMapSetting.ApplyToAStarAsync` coroutine (`AstarPath.Scan`); `AddWorkItem` from building spawn and despawn, `ConstructionMode` and creep clear | Yes [INFERRED: applied when A* next processes work items] |
| Per-frame processors | 3 | `RegionProcessor` (budget 30000 per frame, feeds `IsRegionSeperated`), `ResolverActionProcessor`, `ObserverDriven` | Yes (frame-count based) |
| Skill/passive iterators (`*_d__N` in Combat) | 581 classes | `Passive_SelfRepair.<AttackTimer>d__8.MoveNext` yields `Execution.None`, run by `Tick.ExecutionIterator` | **No**, tick-driven |
| FX coroutines | 481 classes | No FX file writes simulation state (no DataApplier, EventQueue or TaskSystem calls) | No (view only) |
| DOTween | about 25 call sites | Only `Refactor.View` and `CameraHandlerView` | No |
| Threads / `Task` / async | 1 in gameplay | Only `MoveFirstRunner` (the A* callback). The others are Edgar dungeon-generator tasks, Steam, and benchmarks | See A* |
| Wall-clock seeds | 2 | `CampaignEntityLoadHelper.SpawnDungeonNPC` falls back to `Environment.TickCount` when a seed provider is null. Edgar `GenerateLayout` early-stopping uses DateTime/Stopwatch | Low to medium |

## 3. Random sources

| Source | Call sites | Seeded from the save? | Examples |
|---|---|---|---|
| `UnityEngine.Random` (global stream) | 154 in 101 functions, plus 55 through `RandomUtil`/`WeightedRandomUtil` | **No.** It is never seeded globally. The only `InitState` calls are inside `CampaignRandomService.ExecuteAtSequence` (which restores the old state afterwards) and `EstablishUnitGenerator` | Combat: `DamageSimulator.CheckIsHit`, `CheckIsGuard`, `CheckIsCritical`. Also skills (`SKILL_BlackSerum`, `CheeringHowl`), passives (13), `RecruitHelper`, `LevelUpHelper` (inscriptions), trade pool, `DropHelper`, `RandomEventContainer`, `DecisionContainer`/`DecisionSelector`, `WeatherSystem`, TikiTaka (unit conversations), guild NPC idle |
| Same stream, from presentation code | about 20 | n/a | `FXPresenter.HitBloodFX` (runs inside `FXSystem.Tick`, so inside the tick), AffecterFX speech, sound (`OneTimeSFX`, `BGM`, `Ambience`, `LoopSFX`), `UnitGraphic.OnInit`, `UnitEntityHUD`, `MapCloudHandler.Tick`, `BuildingGraphicRandomHit`, `BuildingSpriteManager` |
| `CampaignRandomService` (deterministic mode) | 2 domains | Yes, but only `LevelUpPrayer` and `Recruitment`, and only when `IsDeterministicModeEnabled`. It hashes campaign Guid + context + sequence. The sequence state is saved (`CampaignRandomSaveData`) | — |
| `new System.Random()` (seeded from the clock) | about 13 | **No** | `ItemDropSelector..cctor` (one static RNG for drop selection), `WeatherSystem..ctor`, `EnvironmentAffectHelper..ctor`, `NPCSpawnPlanner.Plan`, `NPCSpawnHandler` (one branch), `ObserverDriven.EventInstance.FixTargetUnits`, BT actions `Task.SetRandomTarget` and `Task.SpendTick.CurrentTaskTick`, `Passive_SunHeartBloomAttack.<AttackRoutine>` |
| `new System.Random(seed)` | about 20 | Yes, from the seed passed in (dungeon generation, monster patrol and respawn, `DungeonCoreHandler`, `DungeonBehaviorSimulator.OnEnterDungeon`) | `DSRandom` exists but nothing uses it |
| `Guid.NewGuid` | 69 in 67 functions | **No** | Entity constructors (`ItemEntity`, `BuildingEntity`, `FarmEntity`, `BlueprintEntity`), every `*TaskData` constructor, `Execution` constructor, `SpawnSimulator`, `TaskDrop`, trade. (`Util.GuidGenerator` is a hash-based generator used only for FX keys) |
| BT random nodes | BehaviorDesigner ships `RandomSelector`, `RandomSequence`, `RandomProbability`, `RandomInt`/`Float`/`Bool` | Unknown | Whether the game's trees use them needs the tree assets [INFERRED risk]. Of the 192 custom `Refactor.Task` BT nodes, `SetRandomTarget` and `SpendTick` use the clock-seeded `System.Random` |

## 4. Unordered iteration

- There are about 581 foreach loops over a Dictionary or HashSet in game code, in 375 functions. About 230 are in simulation namespaces (Main, Tick, Component, Runtime, Task).
- The master stores are `UnitDataContainer._units`, `BuildingDataContainer._buildings` and `ItemDataContainer._items`, all `Dictionary<Guid, …>`.

**This is not a risk on its own.** [INFERRED from how the .NET/Mono Dictionary and HashSet work; not checked in the binary]:
- Entries are added at the end or into freed slots. Enumeration walks the entries in slot order.
- So the order depends only on the sequence of adds and removes, not on hash values. That holds even for Guid keys or object keys hashed by address.
- Two peers with the same history enumerate in the same order. Any divergence comes from other causes; iteration only spreads it.
- The June claim that this "needs a broad deterministic-sort retrofit" **no longer holds**.
- A cheap check: run the same add/remove script twice with different Guids and compare the order.

**The real ordering risk is random Guids used as tie-breakers:**
- `Task.TaskMatcher.ComesAfter` (`0xAECFFC`) compares priority first, then `Guid.CompareTo`.
- `TaskMatcher.SortTasks` and `BuildUnits`, and `StoreBatchPlanner.FindBest`, also compare Guids.
- So work assignment differs between peers unless Guids are deterministic.

## 5. Floats

- Stats are float: `StatusComponent` has 3 × `Dictionary<StatType, float>`, and there are 126 `Dictionary<StatType,float>` declarations in total.
- Positions are float: `EntityDataContainer._entityPositions` is `Dictionary<MapType, Dictionary<Guid, Vector2>>`.
- Delays and damage are float.
- 559 simulation functions use floats.
- Maths library calls are rare:
  - `sqrt` is exact under IEEE rules.
  - `atan2f` appears in `GridHelper` and `CombatUtility.SnapTo8Direction` (rounded to 8 directions).
  - `sinf`/`cosf` appear only in view code.
- **Same binary on Apple Silicon at both ends: low risk.** The same instructions give the same results, including fused multiply-add.
- There is a small leftover risk if macOS versions differ (the libm `atan2f` result could differ, and only at direction boundaries) [INFERRED].
- **Mixing in an Intel Mac or Windows PC is a real risk** (fused multiply-add and compiler differences).

## 6. Command surface: where player intent becomes game state

**Confirmed TaskSystem API:**
- `DirectOrder(IEntity, Task_Base)` (`0xAA6F60`)
- `NPCUnitOrder` (`0xAA6DDC`)
- `SetTask(IEntity, Task_Base)` (`0xAA8E5C`)
- `AbortTask(Guid)` and `AbortTask(IEntity, …)` (`0xAA85DC` / `0xAA93C8`)
- `ForceTickDirectOrder`

**Order events.** `TickFlowHandler.HandleEvent` (`0xD28214`) dispatches:
- `DirectOrderRequested` → `DirectOrder` + `CombatTargetApplyData`
- `UnitOrderRequested` → `NPCUnitOrder`
- `UnitAbortOrderRequested` → `AbortTask`
- `TaskRefreshRequested`, `DataApplyRequested`, equip/unequip/recruit callbacks

These events carry live `IEntity` and `Task_Base` objects, not Guids. The June note called the API "GUID-keyed"; that is only partly true.

**Skill use path:**
1. `PlayerControlEventHandler.OnEvent(UseQuickSlotSkillRequested)` (`0xDBE600`, carries only an index) switches `ControlModeProcessor` to UseSkill mode.
2. `Control.UseSkillMode` / `SkillOrder` then push orders into `ITickFlowHandler`.
3. `UseSkillTargetRequested` and `DirectTaskRequested` (in `EntityEventHandler`) are already Guid-based and easy to send over the network.

**There are three ways in, not one:**
1. **Global `EventQueue.AddEvent`** (`0xCCFAD4`) for all UI and input events. `InputDispatcher` turns input into events.
2. **`TickFlowHandler.EnqueueEvent`** (`0xD28034`), called from 56 classes. Callers outside the tick include Control (MoveOrder, SkillOrder, FormationMoveOrder, UseSkillMode), `PlayerControlEventHandler`, `EntityEventHandler`, `DirectTaskHelper`, `ItemControlProcessor`, `RegionProcessor`, task generators and the resolver actions.
3. **Direct writes with no event at all.** Each of these must be captured separately:
   - `Control.ConstructionMode`, `ConstructionBackUp`, `DemolitionMode`, `CancelMode`, `PickUpMode`, `GatheringMode` call `EntityLifecycleHelper.SpawnBuilding`, `SetTileFloor`/`RemoveTileFloor`, `BuildingRemoveHelper.RemoveBuildingProcess`, `DemolishFarm`, `BlueprintRemoveHelper.RemoveBlueprint`, `TaskGeneratorContainer.GenerateTasks`/`RemoveTaskByTargets`, `AStarMapSetting.SetBlock` and `ConstructionComponent.SetReplacementTarget`.
   - Handlers that change state directly when the UI event arrives: `UseQuickSlotSkillAutoRequested` → `SkillComponent.ChangeAutoSkillData`; `ToggleBehaviourOption` → `EntityInfoComponent.SetBehaviourOption`. More in `ItemEventHandler`: equip, transfer, split, dispose, trade commit.
   - These are fine in lockstep **if** the triggering event is replayed on both peers at the same tick.

**Also player commands:** story-event choices (`SelectChoiceRequested`), game speed and pause, and popups (EssentialPopup and PopupTimeLock pause the game).

## 7. State fingerprint (hash)

**What holds the live world:**
- `CampaignDataContainer`. Its `EntityDataContainer` has, per MapType:
  - `_unitContainers` (UnitDataContainer `_units` Dictionary<Guid,UnitEntity>, `_playerUnits`, `_allyUnits`)
  - `_buildingContainers`, `_itemContainers` (`_items`, `_onMapItems`)
  - `_blueprintContainers`, `_farmContainers`, `_thingContainers`
  - `_entityPositions`
- Also `EntityComponent` (the `StatusComponent` stats), `TaskContainer`, `ClanDataContainer`, `TimeData` (Tick, Min, Hour, Day), `CampaignRandomStateContainer` and `RuntimeDataContainer` (Base and Dungeon runtime data).
- Plus `UnityEngine.Random.state` and the per-map `TickFlowHandler` queue lengths.

**Cheap per-tick hash.** Walk `_units` in slot order and hash:
- the Guid
- the raw bits of the position from `_entityPositions`
- HP and energy from `StatusComponent._stats`
- the current task type

Add `TimeData.Tick`, the counts of items and buildings, and the Random state.

**Full hash every N ticks:** `EntityDataContainer.Serialize` (`0xC72E80`).

**Hook points:**
- **Per map, end of tick:** a post-hook on `Refactor.Tick.TickFlow$$Tick` at **RVA `0xAADE88`**. The map is at `TickFlow+0x18` (`_mapType`); the tick number is `TimeData.Tick`.
- **Start of each world tick, best for the lockstep gate:** `SystemEventHandler.OnEvent(TickRequested)` at **`0xDC4CF4`**.
- A true "both maps finished" point does not exist as a function today. Map 0 runs K ticks, then map 1 runs K ticks. Once the mod drives one tick per cycle, compute the hash after the second `TickFlow.Update` (`0xAADCF4`).

## 8. Verdict per problem

| Problem | Fix | Rating |
|---|---|---|
| Frame grouping (global queue, map batching, RegionProcessor/Resolver/Observer once per frame) | Own the loop: emit exactly 1 tick per cycle, and run [inject commands → `EventQueue.Update` → simulation processors → `TickFlow.Update`(0), (1) → `ObserverDriven.Update`] once per tick, several times per frame at high speed. Local-input processors (Select, ControlMode) must be split away from the simulation ones (Resolver, Region, EventFlow) inside `EventProcessor.Update` (`0xCEC070`) | **Hard** (central rewrite of the loop) |
| A* async paths | After every `StartPath` in `MovePathfinder.StartPath` (`0xA84A10`), force `BlockUntilCalculated` and flush graph work items before `FlushDone`. Costs CPU per tick | **Medium** |
| `UnityEngine.Random` unseeded and shared | Seed the simulation stream from the save, swap `Random.state` in and out around each cycle, and save/restore it around FX, sound and view calls made inside the cycle (about 20 sites, including `FXSystem.Tick`) | **Medium** |
| `new System.Random()` sites (about 10 in gameplay) | Replace with a seeded or derived RNG | **Easy to medium** |
| `Guid.NewGuid` (TaskMatcher ties, command references) | Hook `System.Guid$$NewGuid` (`0x425803C`) to a deterministic counter while inside the cycle | **Medium** |
| Commands (3 entry points + direct writes in Control modes) | Capture UI-origin `AddEvent`/`EnqueueEvent` calls outside the cycle, plus the Control-mode entry points. Turn object references into Guids, schedule at tick T+n on all peers. Needs a serializer per event type (100+ types) | **Hard** (largest amount of work) |
| Multi-frame flows (map transitions, creep clear, autosave, loading) with real-time waits | Lockstep barrier: hold ticks on all peers until every peer reports done | **Medium to hard** |
| Popups and speed pause locally | Make speed and pause shared commands; let a local popup request a global pause | **Easy to medium** |
| Floats | Require Apple Silicon at both ends, ideally the same macOS | **Easy** (same architecture). Mixed architecture: **hard** |
| Dictionary/HashSet order | None needed beyond the fixes above | **Not a risk** [INFERRED; one cheap runtime test] |
| BehaviorDesigner | Check at runtime that `BehaviorManager.UpdateInterval` is Manual (the code drives it from `TaskSystem.Tick`). Inspect the trees for Opsive Random*/Wait nodes | **Easy to check; risk unknown** |
| Edgar dungeon generation (time-based early stop), `TickCount` fallback seed | Disable the time limits, or let the host generate and share the result | **Low to medium** |

**Overall: hard, but no blocker.** The simulation design is friendly: fixed tick, no physics, tick-counted executions, one `MainFlow` driver. But it is **not** "near-deterministic" as June said. Lockstep needs three things:
1. Taking over the frame loop.
2. Making A* synchronous.
3. Separating the random stream and the Guid source.

On top of that come the command capture and serialization layer, and the loading barriers. Desync detection is easy with the hook points above.

**June study (build 23725069), re-checked on this build:**

| June claim | Status now |
|---|---|
| No gameplay physics | **Holds** |
| Fixed global tick | **Holds**, but the tick is grouped by frame |
| Stats are float | **Holds** |
| Same-architecture floats are fine | **Holds** |
| `UnityEngine.Random` is unseeded | **Holds**, and is wider than June said: combat rolls too, and the stream is shared with sound and FX |
| Movement is a custom integer-grid flow field | **Wrong.** Paths come from A* `ABPath` on threads; the flow field is only used for range and distance |
| Dictionary iteration needs a sort retrofit | **Wrong**, see section 4 |
| TaskSystem is GUID-keyed and UI-independent | **Partly true.** It takes objects, and several player actions skip it |
| Modding needs MelonLoader/BepInEx + Il2CppInterop | **Contradicted** by our own native loader in `~/ds_multibar/src` (dsmods.dylib injection + code caves) |

Note: the hard-coded addresses in `~/ds_multibar` are from build 25284551. The skill handler now sits at `0xDBE600`, not `0xDA16C4`.

**Scratch tools** (in the session scratchpad, safe to discard):
- `funcs.tsv`: one line per decompiled function.
- `cnt.py`: counts call sites by namespace.
- `dc.py`: prints the decompiled code of a class.
- `res.py`: resolves `DAT_` addresses.

I did not launch the game, patch anything, or start the decompiler daemon.