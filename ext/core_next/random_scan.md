# Random-source scan, build 25846389 (2026-10-15)

Scope: every caller of UnityEngine.Random.*, System.Random ctors, Guid.NewGuid and the RandomUtil/WeightedRandomUtil
helpers in GameAssembly, plus time / identity-hash sources. Method: (1) BL/B scan of the whole `il2cpp` section of
`~/tools/rea_work/GameAssembly_arm64_25846389.dylib` (direct callers only; virtual/interface calls are not seen),
(2) xrefs to the icall name strings (no inlined Random icall exists outside the UnityEngine.Random wrappers),
(3) decompiled code in `~/ds_skill_truth/decomp_25846389`, (4) the 10 `instances.noindex/*/trace.bin` + teams desync
traces (what really ran, with SIM/OUT and VIEW flags), (5) the live `data.unity3d` (UnityPy) for Behavior Designer tasks.
Scripts: `/private/tmp/claude-502/-Users-user/bdaa4039-1c35-4536-98c6-538d80610db9/scratchpad/tools/`.

## Summary: UNSAFE / UNPROVEN

1. **UNSAFE: `PostProcessingManager.<Co_Shake>d__4.MoveNext` 0xA5F3E0** draws `Random.Range(0,3)` on its first step, which runs
   inside `StartCoroutine` from `ShakeCamera` 0xA5EDFC, called by SkillFX code (StoneStomp, EarthCrash, ChainGrap, PhantomChain, ...).
   FX enumerators run INSIDE the sim cycle (trace: `FXRequestData.CreateVFX <- HealResultFX.<HandleFX>` with in_sim) and
   `FXPresenter.PlayFX` 0xA454FC drops FX whose map != the presenter's current map. Class is not view (gen_ranges tests only the
   last name segment `<Co_Shake>d__4`), so a shake eats one **sim** draw only on peers that show that map. Fix: classify as view
   (add `PostProcessing` to VIEW_CLS and test every segment of nested class names in gen_ranges.py).
2. **UNSAFE (semantic, det-load only): Random.InitState-scoped sequences.** `CampaignRandomService.ExecuteAtSequence<T>`
   0x17D3BC4/0x17D3DF0/0x17D401C and `EstablishUnitGenerator.CreateCandidateFromPreset` 0x81EFE4 / `CreateRuleBasedCandidate` 0x81EC98
   do get_state -> InitState(seed) -> draws -> set_state. While det is on and not locked, gameplay draws are swapped to `sim_state`,
   so the seeded state is skipped (still identical on peers, but "same seed = same result" breaks: preview != real).
   Fix: hook InitState 0x50E7710 + set_state 0x50E7800; between a game InitState and the next set_state, draw from the global state (no swap).
3. **UNSAFE (latent, neutralised today): `TextSheet.GetRandomTip` 0xA6A8A8** (`new Random()` for the loading tip, from `LoadingUI.RefreshTip`)
   is not view-classified, so during det load it takes **sim** counter values. Traces: host/joiner 2 vs 3 (pairs A/B, E/F, G/H).
   Harmless now only because the joiner adopts the host det state at lock and the 4 known long-lived Randoms are re-seeded.
   Fix: classify `TextSheet$$GetRandomTip` and `DataSheetManager$$GetRandomTip` 0xA5E960 as view.
4. **UNPROVEN: `HashHelpers.RandomSeed`** (mscorlib cctor 0x4187188 = `new Random().Next()`, TypeInfo 0x664E3A0 static+0x0; two more copies
   0x664E390/0x664E398 use Guid hash). `ValueTuple.CombineHashCodes` 0x42886C0 reads it, so every ValueTuple hash is per process.
   Only Dictionary lookups seen in gameplay (order safe). Fix (cheap): pin the three statics to a constant at det on, like HashCode.s_seed.
5. **UNPROVEN (low): Guid in view-named gameplay class `Refactor.Main.UnitSpeechAction.Execute` 0xCE5AF4** goes to the OUT counter
   (class name contains "Speech"); looks like an FX key only. `SpawnDungeonNPC` 0xDA8B30 seeds NPC spawn from `Environment.TickCount`
   if its seed reader (+0x48) is null (normal path uses the dungeon seed). Combat draws (`DamageSimulator.CheckIsHit/Guard/Critical`)
   are in the tick path but were not present in any trace file, so routing for them is static evidence only.

## Coverage facts

- UnityEngine.Random in this build has only: InitState, state get/set, Range(float), Range(int), RandomRangeInt, value,
  insideUnitSphere, onUnitSphere. insideUnitCircle, rotation, ColorHSV do not exist (stripped). Range(int) does not call the
  RandomRangeInt wrapper (own icall), so there is no nested-hook problem. No code outside the wrappers resolves a Random icall.
- `RandomRangeInt` has 0 callers. `DSRandom` (InitSeed 0x810E58, Range 0x81128C) has 0 callers (dead code).
- Behavior Designer tasks in the live data.unity3d: Sequence, Selector, BehaviorTreeReference, EntryTask, Inverter, Until*, Return*,
  Log, RestartBehaviorTree, **RandomSelector x3 (useSeed false)**. No Wait/Wander/Patrol/Search/RandomProbability/RandomSequence/Math.Random*.
- No game code (Refactor/Util) references Seeker/AIPath/RichAI/AlternativePath/RandomPath, so A* random modifiers are unused.
- `String.GetHashCode` 0x409ADDC is a plain loop (no per-process seed). Dictionary/HashSet iteration order does not depend on hash values.

## Table

| Call site (address) | API | Seed source | Sim / view | Verdict |
|---|---|---|---|---|
| PostProcessingManager.<Co_Shake>d__4.MoveNext 0xA5F3E0 (via ShakeCamera 0xA5EDFC) | Random.Range(int) (+PerlinNoise, deltaTime) | sim stream (cls 0) | view work, first step inside sim cycle, gated by viewed map | **UNSAFE**: add to view class (see summary 1) |
| CampaignRandomService.ExecuteAtSequence<bool/object/G> 0x17D3BC4 / 0x17D3DF0 / 0x17D401C | get_state, InitState, set_state + Range in operation | MurmurHash3("CampaignRandom:v1", campaignGuid, domain, sequence, context) via CreateDeterministicSeed 0xC79574 | sim (orders: recruit, level-up prayer) | SAFE in cycle / out of sim; **UNSAFE-semantic in det load** (summary 2) |
| EstablishUnitGenerator.CreateCandidateFromPreset 0x81EFE4, CreateRuleBasedCandidate 0x81EC98 | get_state, InitState, set_state | MurmurHash3 of selection token (BuildGenerationSeed 0x81F264) | new-campaign start (host, before session) + CreateCampaignPresenter preview | SAFE for sync (host only, result saved); **UNSAFE-semantic if a new campaign starts with det armed** (summary 2) |
| TextSheet.GetRandomTip 0xA6A8A8 (from LoadingUI.RefreshTip) | new Random() | hooked: sim counter during det load | view | **UNSAFE latent** (summary 3): classify as view |
| HashHelpers..cctor 0x4187188 (+0x12B11BC, 0x46E0BA8) | new Random().Next / Guid hash | clock at boot (before det) | ValueTuple.GetHashCode | **UNPROVEN**: pin statics 0x664E3A0/390/398 |
| UnitSpeechAction.Execute 0xCE5AF4 | Guid.NewGuid | OUT counter (class name "Speech" = view) | story action (sim) | **UNPROVEN** (looks FX key only) |
| CampaignEntityLoadHelper.SpawnDungeonNPC 0xDA8B30 | Environment.TickCount -> NPCSpawnHandler.GetNPCSpawnPositions(seed) 0xFCFA2C | dungeon seed reader; TickCount only if reader null | transition (trn scope) | SAFE on normal path; **UNPROVEN** fallback |
| BehaviorDesigner RandomSelector.ShuffleChilden 0xA2B370 | Range(int) | sim stream | sim (TaskSystem.Tick 0xAA6038 -> BehaviorManager.Tick) | SAFE (trace SIM/sim x1184) |
| RandomSelector/RandomSequence/RandomProbability.OnAwake 0xA2B238/0xA2B794/0xA2E84C | InitState(seed) if useSeed | asset value | n/a | SAFE: only RandomSelector used, useSeed=false |
| BehaviorDesigner Movement.Search.TrySetTarget 0xD11568 | insideUnitSphere (unhooked) | Unity global | - | SAFE: task not in game trees |
| Pathfinding.PathUtilities.GetPointsAroundPoint 0x108207C | onUnitSphere (unhooked) | Unity global | - | SAFE: no game caller |
| Pathfinding AutoRepathPolicy, GridNodeBase/TriangleMeshNode.RandomPointOnSurface, GetPointsOnNodes, GraphModifier.Reset, LightweightRVO, RVOExampleAgent | Range/value | - | - | SAFE: not reached from game code |
| Pathfinding.AlternativePath..ctor 0x10687F8, RandomPath..ctor 0x107768C | new Random() field | - | - | SAFE: unused (no Seeker in game code) |
| Pathfinding.Util.Guid..cctor 0x10AE0D0 | static new Random() | hooked (load) | graph ids only | SAFE |
| URP LensFlareCommonSRP 0x4C6AFAC, LightUtility.GenerateShapeMesh 0x4D41B3C, PostProcessUtils.ConfigureDithering/FilmGrain 0x4DFB1F8/0x4DFB408 | get_state, InitState, Range, set_state | constant | view | SAFE: in a cycle their draws swap to view_saved and set_state restores the sim stream |
| RuleTile/AnimatedTile.GetTileAnimationData, Light2D etc. (UnityEngine.*) | Range/value | view stream | view | SAFE |
| Refactor.FX.* / View.* / UI.* / Sound.* callers (FXPresenter.HitBloodFX, AffecterFX_*.OnSpeechFX, SkillFX_ChargingWhirlwind, UnitGraphic.OnInit, UnitEntityHUD, BuildingGraphicRandomHit, MapCloudHandler, BuildingSpriteManager, BGM/SFX handlers via RandomUtil, CreateCampaignPresenter) | Range/value/Guid | view stream / OUT counter | view | SAFE |
| EntityMajorStatUI.CreateStableRng 0xEBC434 | new Random(int) | UI value | view | SAFE |
| DamageSimulator.CheckIsHit/CheckIsGuard/CheckIsCritical 0x94C008/0x94C094/0x94C0F0, CalculateApplyDamageToUnit 0x94D014 | Range | sim stream | sim (execution) | SAFE (static; not in traces) |
| SKILL_*.<SkillRoutine>, AFFECTER_*.<...Routine>/<AddAffecter> (BlackSerum, CheeringHowl, NoneAttack, SelfRepair x3, SolarBloom, SunHeartBloom, GodnessStatue, SearchDirtyCorpses, ExtraBubble) | Range | sim stream | IEnumerator<IExecutionData> stepped by the execution system, not Unity coroutines | SAFE |
| Passive_SunHeartBloomAttack.<AttackRoutine>d__9 0x9A86EC | new Random() local | sim counter | sim | SAFE |
| Passive_BeeHive.OnEnable, EchoFlower.InitBuilding, SlotMachine.OnInit, Heroic.Check, Panic.Check, JesterInscription.OnDamage | Range | sim stream | sim (PassiveAction.Attach; trace SIM/sim) | SAFE |
| RecruitHelper.* (CreateRecruitCandidates, Determine*, CalculateRecruitPrice, AccumulateModifiers, GetSubTreeCount), LevelUpHelper.GetOfferedMajorStat/GetRandomInscriptions/GetRandomInscriptionRarity | Range + WeightedRandomUtil | sim stream (or seeded via ExecuteAtSequence) | orders in sim_scope | SAFE (trace SIM/sim) |
| ItemEventHandler.PopulateTradeInventoryFromPool 0xDA46C4 | Range | sim stream | event in sim | SAFE (trace SIM/sim on both peers) |
| WeatherSystem.GetWeatherByRegion 0xBCBB98; WeatherSystem..ctor 0xBCA858 (_random @0x68) | Range; new Random() | sim / load counter | sim | SAFE: _random re-seeded at lock (reseed_persistent_randoms) |
| EnvironmentAffectHelper..ctor 0xD55470 (_rnd @0x60) | new Random() | load counter | sim | SAFE: re-seeded at lock |
| ItemDropSelector..cctor 0xF9F700 (static Random) | new Random() | first use | sim drops | SAFE: re-seeded at lock |
| DSDungeonPreDataGenerator._runtimeSeedGenerator (field ctor 0xC5D220, used in GetRandomNumbersGenerator 0xC5ABDC) | new Random() at MonoBehaviour creation (often before det) | clock | dungeon generation when no forced seed | SAFE: trn hook on 0xC5ABDC replaces the seed (TRN_NOTES 1) |
| Edgar LevelGeneratorBase..ctor 0x21A5B0C (seedsGenerator), ChainBasedGenerator..ctor 0x3591D24 | new Random() | hooked | generation | SAFE: overridden by DS GetRandomNumbersGenerator / injected generator |
| DungeonBehaviorSimulator.OnEnterDungeon 0xBC4F6C (_systemRandom @0x88), DungeonCoreHandler.Init 0xFA99A8 (@0x38), MonsterPatrolHandler.Init 0xBC8DD4 (@0x30), MonsterRespawnHandler.Init 0xBC9C20 (@0x30) | new Random(int) | RawDungeonDataContainer._currentSeed (save) | sim | SAFE: re-created at every load/dungeon enter; the join flow reloads on all peers. State is not in the save (mid-dungeon join without reload would break) |
| DungeonGeneratorStrategy.HandleRandomMonsterSpawn 0xCBA6BC / HandleStaticMonsterSpawn 0xCBAC94, DungeonStaticBuildingSpawnPlanner.Plan 0xC6AFC8, DungeonBindingRoomValidator.Validate 0xC5C018 | new Random(int) | roomLocalSeed / attempt seed from the generator | generation | SAFE (seed chain from the deterministic floor seed; trn test map hashes equal) |
| NPCSpawnHandler.GetNPCSpawnPositions(+WithDebug) 0xFCFA2C/0xFCE9A8, NPCSpawnPlanner.Plan 0xFCEBA8 | new Random(int) / new Random() fallback | dungeon seed | transition | SAFE (see TickCount row) |
| BaseAreaPreviewService.GenerateAreaCircles 0xC40648 / ShuffleRoomInfos 0xC43CE0 / GetAreaPositions (PerlinNoise), BaseAreaRoomContentResolver 0xC462A8, BaseAreaSettingStorage 0xC47478, Vector2IntExtensions.GenerateDarkThornData 0xE4E6B4, BaseGeneratorStrategy.PlaceEnvironment (GetDeterministicRandom01) | new Random(int), PerlinNoise | base seed = MurmurHash3(campaign seed string) (GenerateBaseMap 0xD71B04) | base generation / load | SAFE |
| TextSheet.GetRandomClanName(Guid) 0xA6A5DC | new Random(int) | campaignGuid.GetHashCode (value hash) | ClanDataContainer..ctor | SAFE |
| TextSheet.GetRandomClanName() 0xA6A754, StringTools.GenerateRandomAlphanumeric 0xE494BC | new Random() | hooked | CreateCampaign UI (pre-session) | SAFE |
| TextSheet.GetRandomNameKey 0xA6A2A4 (UnitProfileData..ctor), GetBackgroundToBio 0xA6A534 | Range | sim stream | sim | SAFE |
| SpendTick.CurrentTaskTick 0xB2BA34, SetRandomTarget.SetTarget 0xB3A748, EventInstance.FixTargetUnits 0xB63950 | new Random() | sim counter | sim | SAFE (trace SIM/sim) |
| GuildGuardIdle/GuildMerchantIdle (SetTarget, SetRandomNearbyTarget, OnStart, GetRandomFromHashSet) | Range/value | sim stream | BT tasks in sim | SAFE |
| RandomEventContainer.InitializeStartTick 0xC91414 / GetRandomEventInterval 0xC914DC (tail call) | Range | sim stream (also in det load) | sim + load | SAFE (equal loads on host and joiner; joiner adopts host det state) |
| TikiTaka: RuntimePolicy.ShouldRecordTopic 0xA76610 / ShouldTriggerConversation 0xA76684, DataHandler.TrySelectRandomTopic 0xBEFC78 / ConsumeCheckInterval 0xBEF984, ComponentTickHandler.TrySelectTikiTakaTarget 0xA9A7F0 | Range | sim stream | tick (no local setting gate) | SAFE |
| MainEventApplier.PlayGuildGuardSpeechVFX 0xABBCD4 | Range | sim stream | VFX, but called unconditionally from DataApply | SAFE |
| StatusApplier.CalculateSideEffect, DropHelper.CreateEnvironmentBonusDrops, SpawnProcessed.*, PositionDefiner, CombatUtility.GetEmptyAdjacentDestination, DirectTaskHelper.*, DecisionContainer.GetInterval, TagHelper.Shuffle/TryParseTag, TaskEventHandler.OnEvent, EventStateContainer.PickAvailable/PickRandomAvailable | Range / helpers | sim stream | sim | SAFE |
| BuildingRemoveHelper.GenerateDestroyFX / GenerateBasicBuildingDestroyFX | Guid | sim counter | event handlers, data-gated | SAFE |
| Entity / task Guids (BuildingEntity, UnitEntity, ItemEntity, Execution, TaskGroup, *TaskData, MonsterPatrol/RespawnHandler.SpawnMonsterGroup, TradeStartHelper.*) | Guid | sim counter | sim / load | SAFE (trace) |
| UnitVisualEditorTool.*, TestRandomTileShower.RandomizeSeed, DungeonMapStructureShower.GenerateAreas (OnDrawGizmos), Edgar DungeonDrawerBase / DungeonGeneratorEvolution, Pathfinding.Examples.ProceduralWorld | Range / Random | - | editor / debug | SAFE (not reached in play) |
| Febucci TypewriterCore.ShuffleArray, Google APIs, Ionic.Zip, YamlDotNet, GoogleSheetLoader._retryRandom, ThreadPool locals | new Random() | - | not gameplay | SAFE |

## Other nondeterministic sources (brief)

| Source | Gameplay readers found | Verdict |
|---|---|---|
| DateTime.Now/UtcNow | save metadata (CampaignDataContainer.Serialize/.ctor/Reset, CampaignSaveLoader.Save), PlayLogHelper.GetCampaignAccumulatedPlayTimeMinute, MapTransitionResult/State timestamps, SeedScanRunner (editor) | SAFE for the sim; play time and save time differ per peer (keep out of the fingerprint) |
| Environment.TickCount | SpawnDungeonNPC fallback | UNPROVEN fallback (see table) |
| Time.deltaTime/unscaled*/frameCount/realtimeSinceStartup | TimeHandler (owned by rig), InputEventHandler (local input), WaitUntilGameStabilizes coroutines (trn), ScreenTransitionHandoff, Co_Shake | SAFE except Co_Shake (view) |
| Mathf.PerlinNoise | BaseAreaPreviewService.GetAreaPositions (seed input), Co_Shake | SAFE (pure function) |
| HashCode.s_seed | EntityTargetSelector, ExecutionData, SerializableVector2Int | SAFE (pinned by det layer) |
| HashHelpers.RandomSeed | ValueTuple hashes | UNPROVEN (summary 4) |
| RuntimeHelpers/Object.GetHashCode, GetInstanceID | only BehaviorDesigner instance-id keys; no ordering use found | SAFE (static, direct calls only) |
| LINQ OrderBy(Guid.NewGuid) shuffles | none (no NewGuid in Refactor lambdas) | SAFE |

Limits: virtual/interface calls are invisible to the BL scan; caller chains through them were checked in the decompiled
code where it mattered, not everywhere. Combat draws were not present in the available traces.
