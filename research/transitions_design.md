# Map transitions and other multi-frame flows in lockstep: design (build 25846389)

## Summary (about 200 words)

All multi-frame gameplay flows start in one place: the async half of the global `EventQueue`. It runs one async event at a time as a Unity coroutine (busy flag `EventQueue+0x40`). The map transition body has one entry point, `MapTransitionCoordinator.<RunGuarded>d__3.MoveNext` (`0xDB6794`), which drives every phase through its own stack. During a transition the stock game pauses and locks the game speed, so no world ticks run.

The barrier does not need the network to stay correct. A flow always starts inside a sim cycle, so it starts at the same tick on every peer. Each peer then stops its own steps, even in the middle of a step, until the flow is done. After that it finishes the step. The tick results are the same everywhere, and dsnet's "slowest peer sets the pace" rule makes the faster peers wait. An optional freeze bit in dsnet only makes the waiting look better.

**Floor generation is NOT seeded.** The seed comes from a clock-seeded `new System.Random()` (`DSDungeonPreDataGenerator..ctor 0xC5D220`). Edgar's time limit (`DateTime.Now`, worker thread) can also cancel an attempt and push the game onto another seed. Fix: one hook makes the seed come from saved state, and one hook replaces the time limit with an iteration cap.

Autosave: host only, never through the queue on clients. "Save, then load, same fingerprint" probably fails if only the client reloads. Every peer must reload.

Terms: **RVA** = function address in `GameAssembly` (from `script.json`/`dump.cs`). **RNG** = random number generator. **BT** = behaviour tree. **A\*** = A* Pathfinding Project. **UX** = what the player sees and feels. **Scope** = the determinism lane's `rig_det_sim_scope` (sim RNG stream, counter Guids, `in_sim=1`). **[INFERRED]** = my reasoning, not seen in code.

---

## 1. How async flows run (stock)

`EventQueue` (`mf+0x48`). Fields: `_syncEvents +0x18`, `_asyncEvents +0x20`, sync handler map `+0x28`, async handler map `+0x30`, admission gate `+0x38`, `_isProcessiningAsync +0x40` (sic).

- `AddEvent` `0xCCFAD4`: only types with a registered handler are accepted. The `MapTransitionRequestGate.TryAcquire` (`0xCD0420`) allows only one pending `ChangeMapRequested`; a second one is dropped with a warning.
- `Update` `0xCCF5E4`: first `ProcessSyncEvent` (`0xCCF7C4`) empties the whole sync queue. Then, if the async queue is not empty and `+0x40 == 0`, it calls `CoroutineHelper.StartRoutine(ProcessAsyncEvent)`.
- `<ProcessAsyncEvent>d__14.MoveNext` `0xCD00FC`: sets `+0x40 = 1`, takes one async event off the queue, and yields the handler's `IEnumerator` to Unity. `<>m__Finally1` (`0xCD0300`) clears `+0x40` when Unity resumes it after the handler has finished.
- Unity's `StartCoroutine` runs the first `MoveNext` synchronously. So `+0x40` is set **inside** the `EventQueue.Update` call, which means inside `sim_cycle`. Whether the nested handler's first `MoveNext` also runs synchronously is Unity-internal **[INFERRED: yes]**. The design below does not depend on it.
- Only `EventQueue` and `CampaignSaveHelper` (autosave) start gameplay coroutines (grep for `CoroutineHelper.StartRoutine` / `StartCoroutine` in `Refactor.*`). `AStarMapSetting.ApplyToAStarAsync` uses `MonoDelegator` for the graph scan.

Async handlers (14). The gameplay ones:

| Event | Handler RVA → iterator `MoveNext` | Origin |
|---|---|---|
| `ChangeMapRequested` | `CampaignLoadingEventHandler.OnEvent` `0xD6F114` → `d__100` `0xD74C08` | UI 102 GoToDungeon / 108 GoToBase (`InputEventFactory.ConvertUIEventToEventData` `0xD37008`); 109 GoDownFloor → `GoDownDungeonRequested` → `MapTransitionRoutingService.QueueGoDownDungeon` `0xDBA118` → `QueueMapTransition` `0xDBA5FC`; sim: `MainEventApplier.Apply` `0xABA618`; tutorial; cheat |
| `CreepClearBatchRequested` | `CampaignEventHandler.OnEvent` `0xD66F34` → `d__31` `0xD6A2A4` | sim (`MainEventApplier.Apply`), cheat |
| `BaseFieldDiscoverRequested` | `0xD66E7C` → `d__30` `0xD69D78` | sim (`MoveApplier`) |
| `ClearDungeonRegionRequested` | `0xD69228` → `d__51` `0xD6B5AC` | UI 107 |
| `SetRecentlySelectedRegionRequested` | `0xD692CC` → `d__52` `0xD6B6E4` | UI 100 |
| `SetSafeFloorSkipRequested` | `0xD69384` → `d__53` `0xD6B820` | UI 101, sim |
| `AutoSaveRequested` | `CampaignLoadingEventHandler.OnEvent` `0xD70E28` | UI input stream (`AutoSavePresenter.CallAutoSave` `0xE84070`, game-hour interval) |
| `LoadCampaignRequested` / QuickLoad / QuitToTitle / EnterTitle / CreateCampaign / QuitToDesktop | `0xD6C76C` / `0xD6C800` / `0xD6BF3C` / `0xD6BA08` / `0xD6BFD0` / `0xD666A4` | UI; not lockstep flows (see 6.4) |

## 2. The flows, step by step

### 2.1 Map transition (enter dungeon, next floor, return to base)

Chain (each one is a Unity-nested coroutine): `ProcessAsyncEvent d__14` → `OnEvent d__100` (`0xD74C08`) → `MapTransitionCoordinator.Run` `0xDB60D4` (`d__1` `0xDB6370`: `MapTransitionState.Begin` `0xDB6640` sets `IsTransitionRunning` at `+0x14` and `_nextTransitionId++`) → `RunGuarded` `0xDB627C` (`d__3` `0xDB6794`).

`RunGuarded` keeps a `Stack<IEnumerator>`. It calls `MoveNext` on the top itself. If the top yields an `IEnumerator`, it pushes it. It hands only leaf yield instructions to Unity (`null`, `WaitForSecondsRealtime`, `WaitUntil`, scene operations). Its recovery routine (abort path) runs on the same stack. **So `d__3.MoveNext` runs every phase of the transition.** `RunTransition` `0xDB61A8` (`d__2` `0xDB6FF0`) calls the phase runner in interface order (`CampaignMapTransitionPhaseRunner`). The order after slot 10 is taken from the interface order; Ghidra's switch output is incomplete there **[INFERRED]**.

| Phase (RVA) | Wall time / frames / threads / RNG / Guid / load speed | State changed |
|---|---|---|
| CreateContext `0xD77868`, Sanitize `0xD77974`, StartLoading `0xD77A98` | none | context, `MapLoadingChecker.StartLoading` `0xCA3B50` |
| EnterVisualGuard `0xD77ACC` (`d__46` `0xD7B174`) | `GameSpeedSetting.SetSpeed(Pause, AbsolutePause)` `0xD24114` + `SetLock(true)` `0xD24B98`; `WaitUntil(ScreenTransitionHandoff.IsLoadingAudioFadeOutCompleted)` and fade-out (frames, view) | local speed paused and locked (stock: **no world ticks during the transition**) |
| PrepareTargetMap `0xD77B84` → `MapTransitionTargetPrepareService.PrepareDungeonTargetMap d__6` `0xDBBFA4` → `DSDungeonPreDataGenerator.Generate` `0xC5ACF8` | **clock seed, Edgar worker thread (`Task.Run`), Edgar time limit**: see 2.2 | generation result (`MapTransitionState.RecordGenerationResult` `0xDB8090`) |
| ClearTargetMoveCache `0xD77E28`, CollectSourceUnits `0xD7804C` (kills summons), DismissPendingEvent `0xD786AC`, CheckAndApplyUnitRelations `0xD78828` | none seen | units, summons, events |
| PrepareSource `0xD78F68` (`d__57`) | none | `TaskWaitLoadingRequest` into tick flow, `EnvironmentAffectHelper.RemoveAllEnvironmentAffecters`, forced tick-flow flush |
| MoveWithinCampaignScene `0xD79280` (`d__60`) | `ISceneLoader` (frames, load speed) | scene |
| BuildTransferInfos `0xD79324`, CleanupSource `0xD7936C` (`d__62`) | none | `ClearDungeon`, dead units and summons, `RuntimeMapData.ClearOnMapChanged` |
| SetupTargetMap `0xD79424` → `SetupDungeonTarget d__11` `0xDBC744` | `DungeonGeneratorStrategy.GeneratePrepared`; monster spawns use `System.Random(seed)` (`HandleRandomMonsterSpawn` `0xCBA6BC`, `HandleStaticMonsterSpawn` `0xCBAC94`); **Guid.NewGuid** in entity constructors | whole target map, monsters |
| Commit `0xD794DC` → `MapTransitionCommitService.Commit d__11` `0xDB47EC` → `PostTransferEnterHandlers d__12` `0xDB498C` | `SpawnDungeonNPC` (`CampaignEntityLoadHelper`): seed = `IDungeonDataReader.GetCurrentSeed()`, with an `Environment.TickCount` fallback only if the reader is null; `DungeonBehaviorSimulator.OnEnterDungeon` = `System.Random(dungeon seed)`; Guids | units moved, NPCs, dropped items |
| Activate `0xD795A8` → `MapTransitionActivationService.Activate d__2` `0xDB3AB8` | `LoadAllEntity`, `InitializeViewsForMap` (view RNG draws, e.g. `UnitGraphic.OnInit`); `AStarMapSetting.ApplyToAStarAsync` `0xC54094` (graph scan through a coroutine; frames, possibly threads) | entities live, A* graph |
| Complete `0xD79660` → `MapTransitionCompleteService.Complete d__11` `0xDB522C` | `WaitForDefaultTicks d__15` `0xDB5A00` = `WaitForSecondsRealtime(3 × 0.0167 s)` (a real-time wait despite its name); `MapLoadingChecker.IsLoading` waits on object loading (load speed) | `RegionProcessor.BuildMap`, forced flush, completion effects, user log file |
| InitializeTargetEntities `0xD79718` (`d__68` `0xD7B834`) | **up to 8 passes, `yield null` (1 frame) each, needs 2 stable passes**; each pass calls `TickFlowHandler.CompleteEntityInitializationForTransition` `0xD28DBC`, which **force-ticks `ExecutionSystem`** and flushes related events | executions and passives of the new map |
| Finalize → `MapTransitionFinalizationService.FinalizeSuccess` `0xDB8824` (`d__7` `0xDB94B4`) | `WaitUntilGameStabilizes d__13` `0xDB9BEC`: `unscaledDeltaTime`; waits for 1.5 s of frames under 33 ms, at most 3.5 s; `StartAutoSave` | `PostCommitService.Apply`, `EnqueueFinalMapEvents` `0xDB8FFC` (`RegenerateCurrentMapWeatherRequested`, `InvokeOnMapChangedEventRequested` into the target tick flow), `MapTransitionState.Complete` `0xDB9A50`, `UnlockAndResumeGameSpeed` `0xDB92D8` (`SetLock(false)`, `SetSpeed(Normal, RollBack)`) |
| Abort / exception: `MapTransitionAbortService.Abort` `0xDB29C0`, `RecoverFromException` `0xDB2A8C` | `WaitUntilGameStabilizes` `0xDB2D48`, `UnlockAndResumeGameSpeed` `0xDB2BF8` | restores the source map |

`MapTransitionState` and `MapTransitionResult` read `DateTime.UtcNow`, for logs only **[INFERRED]**.

### 2.2 Floor generation (Edgar)

- **Seed source.** `Generate d__24` (`0xC5FE18`) builds the generation info from `ChangeMapRequested` (`HasForcedDungeonSeed +0x2D`, `ForcedDungeonSeed +0x30`). The Tutorial region (7) uses a fixed `SystemVar.Dungeon` seed. Every other region calls `GenerateInternal(useRandomSeed = true, fixedSeed = null)`. `GetRandomNumbersGenerator` `0xC5ABDC`: if `useRandomSeed`, then `seed = _runtimeSeedGenerator.Next()` (`+0xA0`), which is `new System.Random()` from the ctor `0xC5D220` (**clock seed**). It writes `+0x7C`/`+0x9C` and returns `new System.Random(seed)`.
  - Stock UI and sim requests pass `hasForcedDungeonSeed = 0`.
  - **So every floor is random per machine. It is not seeded from the campaign or the floor.** This `new System.Random()` is not in the determinism audit's list.
- **Retries.** `GenerateInternal d__26` (`0xC60F0C`) tries candidate seeds (`BuildCandidateSeeds d__37` `0xC5D49C` = requested seed, then the deduplicated default seeds from the level data). Each attempt is `GenerateAndExtractOnce d__27` (`0xC605B8`), followed by validation: no-repeat, portals, binding rooms (`DungeonBindingRoomValidator.Validate` `0xC5C018` uses `System.Random(param)` and `NPCSpawnPlanner.Plan` with that seeded rng). On the random-seed path, a retry draws a new clock-stream seed.
- **Thread.** `Edgar.Unity.DungeonGeneratorTaskGrid2D.<Process>d__2.MoveNext` `0x11CAA60` runs `GraphBasedGeneratorGrid2D.GenerateLayout` through `Task.Run` (worker thread) and polls `Task.IsCompleted` once per frame. The injected RNG is the seeded `System.Random`.
- **Time limit.** `Process` sets `EarlyStopIfTimeExceeded = TimeSpan.FromMilliseconds(DungeonGeneratorConfigGrid2D.Timeout)`. The config is at generator `+0x50`, `Timeout` at `+0x18`. The value is unknown; Edgar's default is 10000 **[INFERRED]**. The early-stop closure `<GetEarlyStoppingHandler>b__0` `0x1D0F1F0` checks `DateTime.Now` every 100 iterations and cancels.
  - Config offsets (`GraphBasedGeneratorConfiguration`): `EarlyStopIfIterationsExceeded` = `Nullable<int>` at `+0x14`/`+0x18`; `EarlyStopIfTimeExceeded` = `Nullable<TimeSpan>` at `+0x20`/`+0x28`.
  - A null layout throws `Edgar.Unity.TimeoutException`. The attempt fails and DS moves to the next candidate seed.
  - **Answer: yes.** With the same seed, a slow Mac can pass the limit where a fast Mac does not. The slow Mac then goes on to another seed and gets a different layout. Today this hardly matters, because the seeds already differ per machine.
- After success, the seed lands in the dungeon data. `GetCurrentSeed` then drives NPC spawn and `DungeonBehaviorSimulator`. If the generation seed becomes deterministic, these become deterministic too.
- Loading a save does **not** run Edgar again. `LoadDungeonMapFromSaveData d__94` `0xD74488` uses the saved map data.

### 2.3 Creep clear batch, base field discover

- `d__31` (`0xD6A2A4`) does all of its work in the **first** `MoveNext`: `RemoveDarkThornPosition`, `DespawnDarkThornBuildings`, `TryDiscoverField`, `TryRemoveCreepAtPosition`, `RuntimeMapData.MarkRegionDirty`, `AStarMapSetting.SetBlock` `0xC53E5C`, `ImplementTerrainAtPosition`, `UpdateExpandedBounds`, `UpdateViews`, and `SpawnPendingBuildings` (**Guid.NewGuid**). It may then `yield StartAutoSave(force)`.
- `d__30` (`0xD69D78`, base field discover) follows the same pattern.
- **Frame trap:** `+0x40` stays set until Unity resumes `ProcessAsyncEvent`, one frame or more later. With several cycles per frame, the next async event (another creep batch) starts after a number of cycles that depends on frame grouping. That breaks determinism even for these "instant" handlers.

### 2.4 Clear region, safe-floor skip, recently selected region

These write `IClanDungeonInfoWriter` in the first `MoveNext` and do not yield. They still hold the async slot until the next Unity resume, which is the same frame trap as 2.3.

### 2.5 Autosave

- Interval autosave: the view layer (`AutoSavePresenter.CallAutoSave` `0xE84070`) puts a `UIInput` into the stream → `AutoSaveRequested` → async queue.
- In-flow autosave: `StartAutoSave` `0xFCA970` (`d__53` `0xFCD068`) is called from `d__30`, `d__31` and `FinalizeSuccess`.
- Story autosave: `AutoSaveAction.Execute` `0xCD56B8` → `RequestAutoSave` `0xFCA3C0`. It completes at once, so the resolver does not wait for the save. `RequestAutoSave` starts its own coroutine `ProcessPendingAutoSave d__50` `0xFCCE18`, which waits until `IsAutoSaveTimingSafe` `0xFCAC00` is true (frames).
- `SaveAutoSaveFile` `0xFCB300` → `CampaignSaveLoader.Save` `0x814890`: `DateTime.Now` for the display name, `CampaignDataContainer.Serialize`, `TryUnlockDownfall`, and in iron mode `KeepOnlyRetainedCampaignSave` (deletes other saves). I believe the save only reads simulation state **[INFERRED]**.

## 3. Lockstep barrier design

### 3.1 What already exists

`rig_det` already holds cycles while `busy()` (`rig_cmd.c:101`). `busy()` checks `mf+0x38` (`+0x10 > 0`), `MapTransitionState.IsTransitionRunning` (`mf+0xF8 → +0x14`) and `MapLoadingChecker.IsLoading` (`mf+0xF0`, `0xC98A8C`). `mp`'s `ready()` = `rig_det_mp_ready` = locked && `runnable()`.

Gaps:
- (a) `ready()` is checked once per frame. A flow that starts in tick k of a step does not stop ticks k+1..n of that step or of later steps in the same frame.
- (b) `EventQueue+0x40` is not in `busy()`, so creep, field and region flows are not held.
- (c) Flow coroutines run outside `sim_cycle`. Their RNG draws and Guids go to the out-of-sim streams (`guid_out_n`), which differ per peer.
- (d) The floor seed and Edgar time limit (2.2).
- (e) Autosave and local-only async events (3.6).

### 3.2 Start: deterministic, no new command

A flow can only start in `EventQueue.Update`. While locked, that runs only inside `sim_cycle` (`h_eq`). UI-origin triggers must already be orders (orders lane), applied by `mp_apply_cmds` at a step boundary:
- `ChangeMapRequested`: payload = From u8, To u8, Region i32, Floor i32, flag bits (`IsSkipSafeFloor`, `HasForcedDungeonSeed`, `ClearSourceDungeonFloorOnCommit`, `DecreaseSourceDungeonDepletionOnCommit`, `ClearSourceDungeonRegionOnCommit`), ForcedSeed i32, VisualGuardMode i32, then n + Guid[n]. Apply with ctor `0xD621E8` + `AddEvent` `0xCCFAD4`.
- `GoDownDungeonRequested` (UI 109), `ClearDungeonRegionRequested` (107), `SetSafeFloorSkipRequested` (101), `SetRecentlySelectedRegionRequested` (100).

Sim-origin triggers (`MainEventApplier.Apply`, `MoveApplier`) already run inside the cycle. So every peer starts the same async event in the same cycle.

### 3.3 Hold: local, mid-step, and enough for correctness

New predicate `flow_busy()` = `busy() || AT(AT(mf,0x48), 0x40, bool)`. Put it in `busy()` so that `ready()` sees it.

`mp_core` change: check `ready()` after **every** `sim_tick`, not only once per frame. When it turns false after tick k of step S:
1. Store `held_step = S` and `held_ticks = step_ticks - k`. Leave the loop. Do **not** call `mp_step_done` (no fingerprint, timer unchanged).
2. On later frames: no steps while `!ready()`. Keep calling `dsnet_can_run_tick`, `dsnet_report_frame_time` and housekeeping.
3. When `ready()` is true again: run the `held_ticks` that are left, then `mp_step_done(S)`, then continue normally (catch-up pacing as usual).

Why this is enough: every peer stops at the same (step, tick). While stopped, only the flow changes the world, and it does so in scope (3.4). No commands and no ticks run on that peer until the flow is done. So the world at the next tick is the same everywhere. The fingerprint of step S is computed after the flow, so it covers the transition.

The faster peers run ahead until dsnet's `max_behind` (90 steps, about 1.5 s) stops the host timer. This is a pacing effect only, not a desync.

### 3.4 Determinism inside the flow (scope hooks)

Wrap these `MoveNext` functions with `rig_det_sim_scope(1)` … `(0)`. The scope must be nesting-safe: the first `ProcessAsyncEvent` call already runs inside `sim_cycle`. Inside the scope, `mp_game`'s `SetSpeed`/`AddPauseReason` hooks see `rig_det_in_sim()` and pass the transition's own pause, lock and resume calls through to the original. They do not become votes.

| Hook | RVA |
|---|---|
| `EventQueue.<ProcessAsyncEvent>d__14.MoveNext` | `0xCD00FC` |
| `CampaignLoadingEventHandler.<OnEvent>d__100.MoveNext` (ChangeMap) | `0xD74C08` |
| `MapTransitionCoordinator.<Run>d__1.MoveNext` | `0xDB6370` |
| `MapTransitionCoordinator.<RunGuarded>d__3.MoveNext` (whole transition, including abort and recovery) | `0xDB6794` |
| `CampaignEventHandler.<OnEvent>d__30 / d__31 / d__51 / d__52 / d__53.MoveNext` | `0xD69D78` / `0xD6A2A4` / `0xD6B5AC` / `0xD6B6E4` / `0xD6B820` |
| `CampaignSaveHelper.<StartAutoSave>d__53.MoveNext` (see 3.6) | `0xFCD068` |

Notes for the determinism lane:
- View code inside the transition (`InitializeViewsForMap`, `UnitGraphic.OnInit`, fades, camera snap, sound) draws `UnityEngine.Random`. `rig_det` already routes view call sites by return address (`is_view`). The view address ranges must also cover these view paths when they are reached from flow scope.
- Edgar's `GenerateLayout` runs on a worker thread at the same time as main-thread frames. The counter-based `new System.Random()` / `Guid.NewGuid` hooks must not count calls from non-main threads (use `pthread_main_np()`, as `rig_det.c:186` already does for one hook). Whether Edgar makes such calls on the worker is not checked **[INFERRED]**.
- Risk **[INFERRED]**: the number of `InitializeTargetEntities` passes (1 to 8) could depend on frame-time loading, and each pass force-ticks `ExecutionSystem`. The scope does not remove this risk. Log the pass count per peer (test T1). If it differs, keep `RunGuarded` from advancing past `Activate` until `MapLoadingChecker.IsLoading` is false. This is a gate inside the `0xDB6794` hook: return `true` with `current = null` without calling the original.

### 3.5 Floor generation fix

Recommended (A): deterministic seed plus an iteration cap. No network wait.

1. **Seed:** pre-hook `DSDungeonPreDataGenerator.GetRandomNumbersGenerator` `0xC5ABDC`. If `w1` (useRandomSeed) is not 0, set `w1 = 0` and `w2 = splitmix32(campaignGuidHash ^ TimeData.Tick ^ attempt << 24)`.
   - `TimeData.Tick` = `CampaignDataContainer+0xA8 → +0x10`. It is saved and identical on all peers, because ticks are held.
   - `attempt` is a counter that resets when `TimeData.Tick` changes.
   - The seed comes from saved state, so resync and late join keep working. The stock control flow (retries, default seeds) is unchanged.
   - Forced-seed requests and the Tutorial region go through untouched.
2. **Time limit:** post-hook `GraphBasedGeneratorGrid2D<object>..ctor` `0x1E71D84` (`x2` = config). Set `config+0x20` (time limit HasValue) = 0, `config+0x14` = 1, `config+0x18` = N.
   - The iteration count is deterministic for a given seed **[INFERRED from Edgar's single-threaded annealing]**, so success or failure is the same on every Mac.
   - Pick N by measurement (test T4): `get_IterationsCount` on successful runs, then N ≈ 4 × the highest value seen. Without any cap, a seed that cannot be solved would hang forever.

Alternative (B): the host generates with the stock limits and broadcasts the used seed (`+0x9C`). Clients force that seed (`HasForcedDungeonSeed`) with no time limit, and the client transition waits at `PrepareTargetMap` until the seed command arrives. This needs no tuning, but it adds a network wait in the middle of a transition and more code.

(C), sending the whole layout, is too heavy.

### 3.6 Queue rule and autosave

**Rule: the async queue must have the same content on every peer.**

Here is why. If the host queues a local `AutoSaveRequested` before a sim `CreepClearBatchRequested`, the host starts the creep clear one cycle later than the clients. That is a desync.

Autosave design (host only):
- Hook `EventQueue.AddEvent` `0xCCFAD4` for `AutoSaveRequested`. Never queue it. The host calls `CampaignSaveHelper.RequestAutoSave` `0xFCA3C0` directly (it has its own coroutine, outside the queue and outside the barrier). Clients drop it. The orders lane must also never capture it as a command.
- Clients: `RequestAutoSave` `0xFCA3C0` → no-op, and `<StartAutoSave>d__53.MoveNext` `0xFCD068` → return 0 at state 0. This stops autosave files, iron-mode deletion and the in-flow saves.
- Host: in-flow `StartAutoSave` may stay stock. It only makes the host's own hold longer. The hold makes the length harmless, and the queue order stays the same.
- The save time does not need to be the same on all peers, because `Save` does not change simulation state **[INFERRED; check with the RNG/Guid trace: no draws, same fingerprint before and after]**.
- Resuming an MP campaign later from the host's autosave: everyone loads the same file and seeds the determinism streams from a hash of the file. Or the host also writes `mp_sync` with the mp header at each autosave.
- Block `LoadCampaignRequested`, QuickLoad and QuitToTitle as game events in MP. The host uses the resync path instead; quitting ends the session.

### 3.7 Pacing and UX (optional, network)

Add a "busy" bit to the client keepalive (`dsnet_set_busy(c, flow_busy())`). The server stops `gameTimer` while any playing peer is busy (RimWorld freeze). Then everyone stops within about 3 to 7 steps of the transition, instead of playing about 1.5 s ahead and then stalling. Show "Waiting for <name> (loading)". If one peer stays busy for more than about 60 s, offer kick or resync.

After release, call `show_speed(shown_speed, shown_paused)` once. Stock `UnlockAndResumeGameSpeed` set the local display to Normal; the mp votes stay as they were.

### 3.8 Join points, fingerprints, edge cases

- A JOIN_POINT always runs at a step boundary on a peer that is not held, so it never saves in the middle of a flow.
- The fingerprint is never sent during a hold, because the step does not finish.
- Paused steps run no `sim_cycle`. A "go to dungeon" order given while paused therefore waits until play resumes. In stock it runs while paused. Changing this needs an event-only cycle on paused steps (determinism lane).

## 4. Will "save → load → same fingerprint" hold?

**No, if only the client reloads. Probably yes, if every peer reloads the same bytes and the load is deterministic.**

State that is NOT in the save JSON (`CampaignDataContainer` fields `+0x10..+0xE8`; the `*SaveData` classes in `dump.cs`):

- **Tick-flow queues** (per-map event and forced queues). This is why the menu save runs `RunPreSaveStabilization` `0xD9BA8C` → `PreSaveStabilizer.TryStabilize` `0xDCFE2C` first. The **EventQueue** sync and async queues are not saved either; a pending `ChangeMapRequested` is lost.
- **Executions in flight:** skill and passive iterators (`*_d__N`) and delays. There is no Execution save class.
- **BT runtime state:** running node, `SpendTick` counters. `TaskComponentSaveData` keeps only type, guids, target and progress.
- **Movement:** paths, waypoints, `MoveFirstRunner`. There is no Move save class; only positions are saved.
- **Combat targets:** `CombatComponentSaveData` = State, Faction, IsDead, Option only.
- **`RuntimeDataContainer`** (base and dungeon runtime data), the A* graph and region caches. These are rebuilt from map data on load (`DungeonGeneratorStrategy.Load d__12`, A* scan, `RegionProcessor.BuildMap`), so they are fine if the rebuild is deterministic.
- **RNG:** `UnityEngine.Random` state (the mp header holds it); the positions of long-lived `System.Random` streams (`ItemDropSelector` static, `WeatherSystem`, `EnvironmentAffectHelper`, `DSDungeonPreDataGenerator._runtimeSeedGenerator`, `DungeonBehaviorSimulator`'s Random). Statics survive loads inside a process.
- **Dictionary slot order.** A reloaded `Dictionary` is compacted in save order, but the host's live dictionary has holes from removals. Iteration order then differs (determinism audit section 4).
- Saved and fine: cooldowns and skill durations (floats; Newtonsoft round-trip **[INFERRED]**), stats, affecters, inventory, tasks, `CampaignRandomSaveData`.

Requirements for resync (to the determinism lane and the mp glue):
1. Every peer, host included, reloads at the join point.
2. Before the host saves, run `TryStabilize` on **all** peers at that step, inside scope. Allow join points only when `flow_busy()` is false and both EventQueue queues are empty.
3. Apply the header's determinism state **before** the load, not at auto-lock. Run load code on the sim streams. Give load-time Guids their own domain so they cannot collide with the counter afterwards **[INFERRED risk]**.
4. Reseed the long-lived `System.Random` instances from the header on every load.

## 5. Tests (harness, two copies)

- **T1:** same save, a GoToDungeon order at step S, one copy with frame jitter or a slowdown. Compare per peer: used seed, Edgar iterations, `InitializeTargetEntities` pass count, `guid_sim_n` and RNG draws before and after, and the fingerprint at step S and S+600.
- **T2:** creep batches under jitter at 7× speed.
- **T3:** (a) A continues while B reloads A's save; (b) both reload. Compare every tick for 3600 ticks, in combat and while idle.
- **T4:** 200 seeds per region and floor: `IterationsCount` and time.
- **T5:** log whether `d__100`'s first `MoveNext` runs inside `sim_cycle` (`in_sim`).
- **T6:** `CampaignSaveLoader.Save` gives no RNG or Guid draws and the same fingerprint before and after.

## 6. Open questions for the lead and the owner (tradeoffs, ask)

1. Who may start a transition in MP? A transition switches the global map and locks everyone. Options: any owner of a unit in `IndexList`, the host only, or a vote.
2. After a transition, should the speed stay at the voted speed, or reset to 1× as in stock?
3. Waiting UX: freeze everyone right away (3.7, recommended), or let the others play about 1.5 s ahead and then stall.
4. Autosave on the host only (recommended): clients get no autosave files.
5. A transition ordered while paused waits until play resumes, unless the determinism lane adds event-only cycles.