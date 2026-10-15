# Rig access spec: Dungeon Settlers build 25846389 (arm64), C access from the MainFlow.Update hook

All addresses are slice VAs (runtime = `ds_base + VA`), same convention as `~/ds_multibar/b25846389/game.h`
(`F(va, T)`, `AT(o, off, T)`, `md(slot)`, `obj_new(ti)`, `itf()`, `str_new()`).
Every field offset below was read from `dump_25846389/dump.cs` field comments; every method VA was matched
in `script.json` (ScriptMethod) and the `dump.cs` RVA line; every `TI_`/`MI_` slot was matched in
`script.json` (ScriptMetadata / ScriptMetadataMethod). Decompiled evidence: `decomp_25846389/<Class>.c`.
**[INFERRED]** = my reasoning, not seen directly.

**MethodInfo rule:** every non-generic method below accepts `NULL` as the last (hidden) MethodInfo* arg;
the game itself passes 0 for them. Only the shared-generic `EntityExtensions.GetComponent<T>` needs a real
MethodInfo: `md(MI_...)`. Static fields: `klass->static_fields` is at `klass+0xB8`.

```c
typedef struct { int32_t x, y; } V2I;            /* Vector2Int: passed as ONE uint64 (x low, y high) */
static inline uint64_t v2i(int x, int y) { return (uint32_t)x | ((uint64_t)(uint32_t)y << 32); }
typedef struct { uint32_t s[4]; } RState;        /* UnityEngine.Random.State */
typedef struct { void *begin; int32_t length; } SpanW; /* UnityEngine.Bindings.ManagedSpanWrapper */
typedef struct { float x, y, w, h; } RectF;    /* UnityEngine.Rect: by value = HFA in s0-s3 */
```

---

## 0. Common managed layouts (verified)

| Type | Layout |
|---|---|
| Object header | klass +0x0, monitor +0x8. Class name: `*(const char**)(klass+0x10)`, namespace `klass+0x18` (il2cpp.h `Il2CppClass_1`) |
| `System.String` | length `int32 @+0x10`, UTF-16 chars `@+0x14` (il2cpp.h `System_String_Fields`) - **confirmed** |
| Array `T[]` | max_length `uintptr @+0x18`, elements start `@+0x20` |
| `List<T>` | `_items @+0x10` (T[]), `_size int32 @+0x18`, `_version @+0x1C` |
| `Queue<T>` | `_array @+0x10`, `_head @+0x18`, `_tail @+0x1C`, `_size @+0x20` |
| `Dictionary<K,V>` | `_buckets @+0x10`, `_entries @+0x18` (Entry[]), `_count int32 @+0x20`, `_freeList @+0x24`, `_freeCount @+0x28`, `_version @+0x2C`, `_comparer @+0x30`. Live count = `_count - _freeCount` |

**Dictionary iteration** (exactly what `Enumerator.MoveNext` does, disassembled at `0x3DBC204` <Guid,object>,
`0x3DE3330` <Int32Enum,object>, `0x3DC42E8` <Guid,Vector2>):
`for (i = 0; i < _count; i++) { e = entries + 0x20 + i*stride; if (*(int32_t*)e >= 0) /* hashCode>=0 = live */ ... }`
(free slots have hashCode = -1; this is the old corefx layout, not .NET 5 `next < -1`).

| Entry type | stride | hashCode | next | key | value |
|---|---|---|---|---|---|
| `<Guid, UnitEntity>` (any Guid->ref) | 0x20 | +0x0 | +0x4 | Guid 16 B @+0x8 | ptr @+0x18 |
| `<Guid, Vector2>` | 0x20 | +0x0 | +0x4 | Guid @+0x8 | float x @+0x18, y @+0x1C |
| `<MapType(int enum), ref>` | 0x18 | +0x0 | +0x4 | int32 @+0x8 | ptr @+0x10 |

Dictionary insertion order = slot order (deterministic given the same add/remove history; audit section 4).
`Dictionary<MapType,X>` lookup: linear scan for key 0 (Base) / 1 (Dungeon) is fine, or call the game's
`Dictionary<Int32Enum,object>.get_Item` `0x3A1AED8 (dict, int key, MethodInfo*)` with a per-instantiation MI (as skilluse.c does).

---

## 1. From MainFlow `this` to the world

`MainFlow` (TypeDefIndex 6070, dump.cs line 188677). The hook's x0 is `this`. MainFlow has no TypeInfo slot
in script.json, so keep the `this` pointer from the hook.

| MainFlow field | off | type |
|---|---|---|
| `_sceneLoader` | 0x38 | SceneLoader (`_loadCount @+0x10`, `_currentScene @+0x14`) |
| `_timeHandler` | 0x40 | TimeHandler |
| `_eventQueue` | 0x48 | EventQueue (global queue; `AddEvent` `0xCCFAD4`) |
| `_inputDispatcher` | 0x68 | |
| `_campaignData` | 0xC8 | **CampaignDataContainer** |
| `_runtimeData` | 0xD0 | RuntimeDataContainer |
| `_campaignSetting` | 0xD8 | CampaignSettingContainer |
| `_mapLoadingChecker` | 0xF0 | MapLoadingChecker |
| `_mapTransitionState` | 0xF8 | MapTransitionState |
| `_mapFlows` | 0x138 | Dictionary<MapType, TickFlow> |
| `_tickFlowHandlers` | 0x140 | **Dictionary<MapType, ITickFlowHandler>** (values are always `TickFlowHandler`, the only implementor) |
| `_moveSystems` | 0x168 | Dictionary<MapType, MoveSystem> |
| `_campaignSaveLoader` | 0x180 | CampaignSaveLoader |
| `_bootReady` | 0x188 | bool (Update returns at once if false) |
| `_currentScene` | 0x18C | SceneType (int) |

`CampaignDataContainer` (TypeDefIndex 5760):
`CurrentMap int @+0x10` · `PlayedTimeMinute @+0x14` · **`EntityDataContainer @+0x20`** · `EntityComponent @+0x28` ·
`RawDungeonDataContainer @+0x40` · `PlayerUnitDataContainer @+0x48` · `ClanDataContainer @+0x50` ·
**`TimeData @+0xA8`** · `TaskContainer @+0xB0` · `CampaignRandomStateContainer @+0xC8`.

`TimeData` (5935): **`Tick int64 @+0x10`**, `Min int32 @+0x18`, `Hour @+0x1C`, `Day int64 @+0x20`,
`LogicalRefresh bool @+0x28`, `TickPerMin @+0x2C`, `LogicalRefreshInterval @+0x40`.

`EntityDataContainer` (5783): static `_instance` (static field 0x0; `TI_EntityDataContainer 0x664D0A0` -> `+0xB8` -> `[0]`, alternate entry point) ·
`_unitContainers @+0x18` Dictionary<MapType, UnitDataContainer> · `_buildingContainers @+0x20` · `_itemContainers @+0x28` ·
`_blueprintContainers @+0x30` · `_farmContainers @+0x38` · `_thingContainers @+0x40` ·
**`_entityPositions @+0x50`** Dictionary<MapType, Dictionary<Guid, Vector2>> · `_runtimeMapDatas @+0x58`.

`UnitDataContainer` (5799): **`_units @+0x10`** Dictionary<Guid, UnitEntity> · **`_playerUnits @+0x18`** List<Guid> (party order) · `_allyUnits @+0x20` List<Guid>.

```c
Obj cdc  = AT(mf, 0xC8, Obj);                       /* CampaignDataContainer */
Obj edc  = AT(cdc, 0x20, Obj);
int64_t tick = AT(AT(cdc, 0xA8, Obj), 0x10, int64_t);
Obj ucd  = dict_int_get(AT(edc, 0x18, Obj), map);   /* stride 0x18 scan, value @+0x10 */
Obj units = AT(ucd, 0x10, Obj);                     /* Dictionary<Guid,UnitEntity> */
Obj ent = AT(units, 0x18, Obj); int n = AT(units, 0x20, int32_t);
for (int i = 0; i < n; i++) { char *e = (char*)ent + 0x20 + i*0x20;
    if (*(int32_t*)e < 0) continue; Guid g = *(Guid*)(e+8); Obj u = *(Obj*)(e+0x18); ... }
```
Evidence: `MainFlow.get_CurrentMap 0xD19434` reads `[this+0xC8]+0x10`; `get_IsCampaignStarted 0xD19408`;
`EntityDataContainer.MoveUnit 0xC83AC0` / `OnSpawnUnit 0xC812C4` write `_entityPositions`.

---

## 2. Per unit (`UnitEntity`, TypeDefIndex 1022, `TI_UnitEntity 0x6656458`)

| Item | How | Evidence |
|---|---|---|
| Guid | `AT(u, 0x24, Guid)` (16 B, 4-aligned). Also `get_Guid 0xFA856C` | dump.cs field `<Guid>k__BackingField // 0x24` |
| Table key | `AT(u, 0x38, Obj)` string (e.g. `UNIT_GuildMerchant`) | field `<Key>` 0x38 |
| Summoner | `Nullable<Guid> @+0x10` (hasValue byte @+0x10, Guid @+0x14) | field 0x10 |
| **Display name** | `Obj s = F(0xFA47A0, Obj(*)(Obj, void*))(u, NULL);` = `EntityDisplayNameExtensions.GetDisplayName(UnitEntity)`. Player units: profile name; others: localized table text. Read `AT(s,0x10,int32_t)` UTF-16 chars at `s+0x14` | decomp `Refactor.EntityDisplayNameExtensions.c` |
| Name text key (cheap, no localisation) | `F(0xFA49A0, Obj(*)(Obj,void*))(u,NULL)` `get_NameTextKey` | |
| **Faction** | `AT(AT(AT(u,0x40,Obj),0x10,Obj),0x18,int32_t)`; or `get_Faction 0xFA4950 int(*)(Obj,void*)`. FactionType: Neutral 0, Player 1, Ally 2, Enemy 3 | `UnitEntity._info` 0x40 = EntityInfoReader; reader+0x10 = EntityInfoComponent (`_faction` 0x18, `_isDead` 0x1C, `_behaviourOption` 0x20) |
| **Dead** | `AT(AT(AT(u,0x40,Obj),0x10,Obj),0x1C,bool)`; or `unit_dead()` `0xFA5640` | decomp `get_IsDead` |
| **Position** | `_entityPositions[map][guid]` -> `Vector2` world = cell + 0.5 (raw float bits for hashing). Cell = `VectorExtensions.ToLogicalPosition(Vector2) 0x80C474`: `uint64_t(*)(float,float,void*)` -> V2I (round-half-even of `w-0.5`). Also `EntityDataContainer.GetWorldPosition(map,guid) 0xC83C6C` (logs errors if missing; prefer the dict) | `ToWorldPosition 0x80C458` = `(float)i + 0.5` |
| **HP / max HP** | `Obj sr = F(0x185FEDC, Obj(*)(Obj,void*))(u, md(0x6691B40));` (`GetComponent<StatusReader>`, **real MI required**) then `float v = F(0xFA0EE4, float(*)(Obj,int,void*))(sr, stat, NULL);` HP = `HealthTotal 9`, max = `MaxHealthTotal 22`. Also HealthHead 13 / MaxHealthHead 7, HealthBody 14 / MaxHealthBody 8, Energy 15 / MaxEnergy 41 | Game reads exactly this pair, e.g. `Passive_DevrickPendant` (GetComponent MI `DAT_06691b40`, GetValue 0x16 and 9) |
| HP raw (no calc) | `StatusReader+0x10` -> StatusComponent; `_stats @+0x20` Dictionary<StatType, IStatus> (value is an interface object; final value needs `GetFinalValue 0xDDCE68` -> DataValue; base `@+0x10`, multiplier `@+0x14`). Use GetValue above unless you need the raw parts | decomp `StatusReader.GetValue` |
| **Task type** | `Obj tr = F(0x185FEDC, ...)(u, md(0x6691B48));` (`GetComponent<TaskReader>`) then `int t = F(0xFA3C54, int(*)(Obj,void*))(tr, NULL);` or `AT(AT(tr,0x10,Obj),0x28,int32_t)` (TaskComponent `<TaskType>` 0x28; `<Target>` 0x20 IEntity; `<SharedTaskData>` 0x38). TaskType: None -1, Idle 0, Move 1, Patrol 2, Chase 3, Sleeping 4, Eating 5, Interaction 6, Carry 7, ... CombatBehaviour 15, ... Deadly 1000, Exhausted 1001 | dump.cs `enum TaskType` line 148256 |
| Other readers (same GetComponent fn `0x185FEDC`) | MI slots: AffecterReader `0x6691AF0`, CombatInfoReader `0x6691B00`, EntityInfoReader `0x6691B10`, EquipmentReader `0x6691B18`, InventoryReader `0x6691B30`, SkillReader `0x6691B38`, UnitProfileReader `0x6691B58` | ScriptMetadataMethod |

---

## 3. Scene / map / speed

**Scene:** `AT(mf,0x18C,int)` SceneType: Entry 0, **Title 1** (main menu), **Campaign 2**, FXTest 3, Loading 9.
`AT(AT(mf,0x38,Obj),0x10,int) > 0` = Unity scene load in progress (Update skips everything then).
**In campaign:** `scene==2 && cdc && AT(cdc,0x10,int) != -1` (this is `get_IsCampaignStarted 0xD19408`).
**Active map:** `AT(cdc,0x10,int)`: Base 0, Dungeon 1, None -1. Or `MainFlow.get_CurrentMap 0xD19434 int(*)(Obj,void*)`.
**Busy flags:** map transition `AT(AT(mf,0xF8,Obj),0x14,bool)` (MapTransitionState.IsTransitionRunning); map loading
`F(0xC98A8C, bool(*)(Obj,void*))(AT(mf,0xF0,Obj), NULL)` (MapLoadingChecker.get_IsLoading).
**Dungeon id:** `Obj dd = AT(cdc,0x40,Obj)` RawDungeonDataContainer: `_currentRegion @+0x10` (DungeonRegion: VeilSilence 0, FesteringBurrow 1, AbandonedMine 2, ForgottenAbyss 3, DeadSwamp 4, TowerApocalypse 5, Abyss 6, Tutorial 7), **`_currentFloor @+0x14`**, `_currentPhase @+0x18`, `_requestedSeed @+0x1C`, `_currentSeed @+0x20`.

**Speed (read):** `Obj gs = AT(AT(mf,0xD8,Obj),0x10,Obj);` (CampaignSettingContainer.GameSpeedSetting; the same object is `TimeHandler._gameSpeed @+0x28`).
GameSpeedSetting: **effective `_gameSpeed @+0x10`**, `_requestedGameSpeed @+0x14` (what resumes after pause), `_pauseReasons @+0x18` Dictionary<GamePauseReason,int> (any entry => effective = Pause), `_gameSpeedLockCount @+0x20` (>0 => SetSpeed refused), `_gameSpeedMults @+0x28`.
GameSpeed: Pause 0, Normal 1, Fast 2, VeryFast 3 (mults 0/1/3.5/7). GamePauseReason: User 0, EssentialPopup 1, CheatConsole 2, Loading 3, PopupTimeLock 4, ConsecrationNotice 5.
`float m = F(0xD24B34, float(*)(Obj,int,void*))(gs, speed, NULL);` GetMultiplier.
TimeHandler (`mf+0x40`): `TICK_INTERVAL float @+0x10`, `_currentTick int64 @+0x38`, `_remainTime float @+0x44` (accumulator).

**Speed (set), the UI path** = global EventQueue, handled by `SystemEventHandler` (also pushes the UI indicator update `ViewIntData 0x431`):
```c
Obj eq = AT(mf, 0x48, Obj);
/* SetGameSpeedRequested(speed): UI buttons/keys. speed 1..3 = set + clear User pause; speed 0 = toggle User pause */
Obj e = obj_new(0x66542F0); F(0xDCF018, void(*)(Obj,int,void*))(e, speed, NULL);
F(0xCCFAD4, void(*)(Obj,Obj,void*))(eq, e, NULL);
/* ChangeGameSpeedRequested(speed, pauseState): pauseState 1 = AbsolutePause (speed 0 => always pause) */
Obj c = obj_new(0x664B1A8); F(0xDCF088, void(*)(Obj,int,int,void*))(c, speed, pauseState, NULL);
/* SetGameSpeedStopOrRollBackRequested(): space-bar pause toggle */
Obj p = obj_new(0x66542F8); F(0xDCF008, void(*)(Obj,void*))(p, NULL);
```
Handlers: `0xDC44F0` (SetGameSpeedRequested; returns early if already at that speed), `0xDC47E0` (StopOrRollBack = `SetSpeed(0, RollBack)`), `0xDC4948` (ChangeGameSpeedRequested). Takes effect when `EventQueue.Update` runs (step 3 of the same `MainFlow.Update` if you add it in a pre-hook).
Direct, no UI refresh: `bool F(0xD24114, bool(*)(Obj,int,int,void*))(gs, speed, pauseState, NULL)` (SetSpeed; RollBack 0, AbsolutePause 1). `AddPauseReason 0xD24848` / `RemovePauseReason 0xD249BC` `bool(*)(Obj,int,void*)`.

---

## 4. Load a save without clicks

**Path:** `Application.persistentDataPath + "/Saves/" + <saveName> + ".json"`.
`SaveLoadHelper.GetCampaignSavePath(string) 0x871EA4` = `Path.Combine(GetSaveDirectory() 0x877010, saveName + ".json")`;
`GetSaveDirectory` = `Path.Combine(persistentDataPath, "Saves")` (literal `0x66D54B0`).
On this Mac: `~/Library/Application Support/CanOpener/Dungeon Settlers/Saves/`.
`Application.get_persistentDataPath 0x507B74C Obj(*)(void*)`.

**Name rule (hard check):** `CampaignSaveLoader.IsValidLoadData 0x814718` requires the JSON's
`CampaignSaveHeader.SaveName` to equal the requested name (OrdinalIgnoreCase), else "Loaded save name does not match the requested file",
and `CampaignSaveHeader.CurrentMap` < 2. So the file stem must equal the `"SaveName"` inside the file.
Example checked on disk: `Saves/ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json` has
`"SaveName": "ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0"`. Copy a save under a new name only if you also rewrite `SaveName`.

**SaveHeaderCache.json** (at `persistentDataPath/SaveHeaderCache.json`, NOT under Saves) is **not needed**:
the load preflight `CampaignLoadingEventHandler.TryGetSaveHeader 0xD6CA44` calls `CampaignSaveLoader.TryRefreshSave 0x812AB8`
-> `SaveLoadHelper.RefreshSaveCache 0x8711D4`, which rebuilds the cache if missing/unreadable and `SynchronizeCacheWithFiles` otherwise,
then matches the header by `SaveName` (OrdinalIgnoreCase) in `CampaignSaveLoadSet.GuidSaveDataDictionary`.
If no header matches: "Save header was not found during load preflight." and the load is cancelled.

**Trigger (what the Load menu does):** `InputEventFactory` builds `LoadCampaignRequested(string saveName)` and it goes to the
global EventQueue; async handler `CampaignLoadingEventHandler.OnEvent(LoadCampaignRequested) 0xD6C76C` -> `LoadCampaign(name)`
iterator (`<LoadCampaign>d__73.MoveNext 0xD7209C`): preflight header -> iron-mode policy -> fade -> if a campaign is running, go to Title first ->
`CampaignSaveLoader.TryLoad 0x8143CC` -> scene transition -> load base/dungeon. Works from the Title scene and from inside a campaign.
```c
Obj ev = obj_new(0x6651118);                         /* TI LoadCampaignRequested */
F(0xDCFD78, void(*)(Obj,Obj,void*))(ev, str_new("ElvenExpedition__AutoSave_f816..._0"), NULL); /* CampaignName @+0x10 */
F(0xCCFAD4, void(*)(Obj,Obj,void*))(AT(mf,0x48,Obj), ev, NULL);
```
`AddEvent` drops events whose type has no registered handler, and asks `MapTransitionRequestGate` (only gates map-transition requests).
Async events run one at a time (`EventQueue._isProcessiningAsync @+0x40`). Done when `scene==2`, map transition not running, `MapLoadingChecker.get_IsLoading` false.
"Continue latest": `QuickLoadRequested` (TI `0x6652F10`, ctor `0xDCE578 (Obj,void*)`, handler `0xD6C800`) [INFERRED: loads the newest save].
**[INFERRED]** iron-mode saves loaded from inside a campaign first run `StartAutoSaveBeforeCampaignExit`; from Title there is no popup in the path I read. `WaitForLegacyStoveLoadNotice` is a no-op in this build.

---

## 5. Order one player unit to move to cell (x,y)

Right-click path: `Control.DefaultCommand` -> `MoveOrder.All 0xC07164 (this, ISelectReader, Vector2Int)` ->
`FormationTarget.Resolve` -> `FormationMoveOrder.TryAssign` -> `SendOrders 0xC0A530`, else `MoveOrder.SameTarget 0xC0D7B0`.
Both end the same way, per unit: `new MoveTask(cell, 0, 0)` -> `new DirectOrderRequested(unit, task)` ->
`ITickFlowHandler.EnqueueEvent(evt, force=false)` (interface slot 0) on the current map's handler. Reproduce for one unit:
```c
int map = AT(cdc, 0x10, int);
Obj tfh = dict_int_get(AT(mf, 0x140, Obj), map);              /* TickFlowHandler */
Obj task = obj_new(0x6651C78);                                /* TI MoveTask */
F(0xB1BF60, void(*)(Obj,uint64_t,int,int,void*))(task, v2i(x,y), 0, 0, NULL); /* (dest, interactionRange, pathLimit) */
Obj ord = obj_new(0x664C9A0);                                 /* TI DirectOrderRequested */
F(0xD64F1C, void(*)(Obj,Obj,Obj,void*))(ord, unit, task, NULL); /* target @+0x10, task @+0x18 */
F(0xD28034, void(*)(Obj,Obj,bool,void*))(tfh, ord, false, NULL); /* TickFlowHandler.EnqueueEvent */
/* or via interface: VID *v = itf(tfh, 0x664FBC8, 0); ((void(*)(Obj,Obj,bool,void*))v->fn)(tfh, ord, false, v->mi); */
```
`EnqueueEvent` puts non-tick events in `_eventQueue @+0x18` (`force` => `_forceQueue @+0x20`); drained at the start of the next
`TickFlow.Tick` of that map, then `TickFlowHandler.HandleEvent 0xD28214` -> `TaskSystem.DirectOrder 0xAA6F60` + CombatTargetApplyData.
**[INFERRED]** while paused no tick runs, so the order waits in the queue. The game also sends a cosmetic `ViewMouseData` marker (skipped here).
Note: `TaskFXKeys` is allocated in the MoveTask ctor; the Task Guid is assigned later (audit: `Guid.NewGuid`, a determinism issue).
Variant `MoveOrder.Near 0xC0D30C` uses `MoveNearByTask` (TI `0x6651BD0`).

---

## 6. UnityEngine.Random.state

`Random.State` = 4 x int32 (s0..s3 @0,4,8,0xC).
```c
RState st; F(0x50E77B0, void(*)(RState*,void*))(&st, NULL);      /* get_state_Injected (icall wrapper) */
RState st2 = F(0x50E7760, RState(*)(void*))(NULL);                  /* get_state: 16-B struct returned in x0:x1 */
F(0x50E7864, void(*)(RState*,void*))(&st, NULL);                   /* set_state_Injected */
F(0x50E7710, void(*)(int,void*))(seed, NULL);                       /* InitState(int) */
```
Icall name `UnityEngine.Random::get_state_Injected(UnityEngine.Random/State&)` (literal used by `0x50E7760`).
Game users of set_state/InitState: `CampaignRandomService`, `EstablishUnitGenerator`.

---

## 7. Engine settings and screenshots (managed wrappers in GameAssembly; all static, NULL MI)

| Want | Call |
|---|---|
| targetFrameRate | `F(0x507C0BC, void(*)(int,void*))(fps, NULL)` (MainFlow.Awake sets 60) |
| vSyncCount | `F(0x5097CD8, void(*)(int,void*))(n, NULL)` (GraphicSetting also sets it when settings apply) |
| runInBackground | `F(0x507B3FC, void(*)(bool,void*))(true, NULL)` |
| AudioListener.volume | `F(0x5067948, void(*)(float,void*))(v, NULL)`. `BackgroundAudioMute.OnApplicationFocus 0x8805A4` re-sets volume on focus change: re-apply each frame or use pause |
| AudioListener.pause | no managed wrapper (stripped). Icall: `void(*)(bool) = il2cpp_resolve_icall("UnityEngine.AudioListener::set_pause")`; `il2cpp_resolve_icall` export = `0x6F8B90` (`const char*` -> fnptr). Getter `"UnityEngine.AudioListener::get_pause"` `bool(*)(void)`. Names confirmed in UnityPlayer.dylib registration strings; **[INFERRED]** that the name without `(System.Boolean)` resolves (IL2CPP falls back to the name without the signature) |
| Getters (stripped) | same icall route: `UnityEngine.Application::get_targetFrameRate`, `::get_runInBackground`, `UnityEngine.QualitySettings::get_vSyncCount` (all `int/bool(*)(void)`, names present in UnityPlayer) |
| Time | `Time.get_deltaTime 0x5104258 float(*)(void*)`, `get_frameCount 0x510442C`, `get/set_timeScale 0x5104398/0x51043D8` (the sim does not use timeScale) |

**Screenshot to PNG.**
`ScreenCapture.CaptureScreenshot(string)` managed wrapper is **stripped** (not in script.json). Options:

A. **Simplest, includes UI, safe from Update** **[INFERRED signature]**: icall
`void(*)(SpanW *filename, int superSize, int stereoMode) = il2cpp_resolve_icall("UnityEngine.ScreenCapture::CaptureScreenshot_Injected")`
(name confirmed in UnityPlayer.dylib). Build `SpanW w = { (char*)s + 0x14, AT(s,0x10,int32_t) }` from a managed string `s` (absolute path, UTF-16),
call `(&w, 1, 1 /*LeftEye*/)`. Unity writes the PNG itself at the end of the frame. Same span convention as `Application.OpenURL 0x507BECC`.

B. Managed, camera only (no Screen-Space-Overlay UI) **[INFERRED: works under URP]**:
```c
Obj cam = F(0x5083208, Obj(*)(void*))(NULL);                        /* Camera.main */
int w = F(0x5092C94, int(*)(void*))(NULL), h = F(0x5092CD4, int(*)(void*))(NULL);
Obj rt = F(0x50D2E38, Obj(*)(int,int,int,void*))(w, h, 24, NULL);   /* RenderTexture.GetTemporary */
F(0x5081CB8, void(*)(Obj,Obj,void*))(cam, rt, NULL);                /* set_targetTexture */
void (*render)(void*) = il2cpp_resolve_icall("UnityEngine.Camera::Render_Injected"); /* Camera.Render stripped */
render(AT(cam, 0x10, void*));                                       /* _Injected takes the native ptr m_CachedPtr @+0x10 */
F(0x50CE6A4, void(*)(Obj,void*))(rt, NULL);                         /* RenderTexture.active = rt */
Obj tex = obj_new(0x6655970);                                       /* TI Texture2D */
F(0x50C4E5C, void(*)(Obj,int,int,int,bool,void*))(tex, w, h, 3 /*RGB24*/, false, NULL);
RectF r = { 0, 0, w, h };
F(0x50C63B0, void(*)(Obj, RectF, int, int, void*))(tex, r, 0, 0, NULL); /* ReadPixels */
F(0x50C5F34, void(*)(Obj,void*))(tex, NULL);                        /* Apply */
F(0x50CE6A4, void(*)(Obj,void*))(NULL, NULL);
F(0x5081CB8, void(*)(Obj,Obj,void*))(cam, NULL, NULL);
F(0x50CFE6C, void(*)(Obj,void*))(rt, NULL);                         /* ReleaseTemporary */
Obj png = F(0x51657D8, Obj(*)(Obj,void*))(tex, NULL);               /* ImageConversion.EncodeToPNG -> byte[] */
F(0x421AE98, void(*)(Obj,Obj,void*))(str_new(path), png, NULL);    /* File.WriteAllBytes, or fwrite(png+0x20, len@+0x18) */
F(0x50FC8F8, void(*)(Obj,void*))(tex, NULL);                        /* Object.Destroy */
```
C. `ScreenCapture.CaptureScreenshotAsTexture() 0x51FA520 Obj(*)(void*)` exists, but Unity requires it after rendering ends; from Update the result is undefined. Use A or B.

---

## Not pinned down / caveats
- `CaptureScreenshot_Injected` and `AudioListener.set_pause` signatures: names verified in UnityPlayer.dylib, argument lists follow the Unity 6 binding convention, not disassembled (UnityPlayer is not in the dump).
- `Camera.Render` under URP and the exact icall-name resolution are [INFERRED].
- `QuickLoadRequested` semantics not traced.
