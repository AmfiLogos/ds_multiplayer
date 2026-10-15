# Dungeon Settlers build 25846389: player-action catalog for lockstep orders

## Summary

Every player input enters the game through one pipeline:

1. The UI calls `UIInputStream.AddInput` (`0xD44C50`). Keyboard and mouse input also lands in `InputStream`.
2. `InputDispatcher.Update` (`0xD3B550`) converts the input through `InputBinding` and `InputEventFactory`.
3. It hands every resulting `IEventData` to `InputDispatcher.<Update>b__8_0` (`0xD3BAA4`), which calls `EventQueue.AddEvent` (`0xCCFAD4`).
4. `EventQueue` runs the handlers later in the same frame, by reflection, through `<>c__DisplayClass8_0.b__2` (`0xCCFF30`, sync) and `<>c__DisplayClass8_1.b__3` (`0xCCFFEC`, async).

**`b__8_0` sees only input-origin events.** Sim-origin events (`MainEventApplier`, `TaskApplier`, `DecisionSystem`) never pass through it. So "where did this event come from" is answered by where it enters, not by its type.

The capture design has two tiers:

- **Tier A (event level).** About 75 command event types are already Guid- or index-based. Serialize them at `b__8_0`, drop them locally, and replay them at tick T. The replay must carry the issuer's selection, because 20+ handlers read `ISelectReader`.
- **Tier B (function level).** Mouse-driven inputs (right-click, left-click in build modes, quick-slot skill, item use) stay local. They read hover caches, raycasts and mouse position. Capture instead the boundary functions they call: `ActionOrder.Execute`, `MoveOrder.*`, `WaypointOrder.*`, `UseSkillMode.UseSkill`, and the build-mode click and drag methods. All of these have UI-only callers (checked by grep).

Other things that need special handling:

- Tutorial input observers.
- Trade session Guids.
- Copy and paste buffers.
- Game speed and pause.
- Cheat console and quick-load (block them).

---

## 0. The input pipeline (verified in the decompile)

| Step | Function | RVA | Notes |
|---|---|---|---|
| UI widget → stream | `UIInputStream.AddInput(UIInput)` | `0xD44C50` | Calls `InputStream.AddUIInput` `0xD3F514`. Keys and mouse use `AddKeyInput` `0xD406AC` and `AddMouseInput` `0xD40738` |
| Per frame (MainFlow step 2) | `InputDispatcher.Update` | `0xD3B550` | Runs `InputBinding.ConvertInputStreamToEventData` `0xD345A4` |
| Conversion | `InputEventFactory.ConvertUIEventToEventData(UIInput, ModifierKey)` | `0xD37008` | Also `ConvertKeyInputToEventData` `0xD357FC` and `ConvertMouseInputToEventData` `0xD362DC`. Fills `Map` from `_currentMap` |
| Tutorial side channel | `InputObserverMapper.Map` → `ObserverAggregator.Record(IObserver)` | `0xBBF998` | Input becomes `InputObserver(InputObserveType)`: MoveCamera, ZoomInOut, GameGuide, GameSpeedFast, ControlModeGather, ControlModePickUp, SelectUnitAll. **This feeds ObserverDriven story and tutorial triggers.** |
| Every input event | `InputDispatcher.<Update>b__8_0(IEventData)` | `0xD3BAA4` | Only calls `IEventQueue.AddEvent`. **This is the Tier A capture point.** |
| Queue | `EventQueue.AddEvent` | `0xCCFAD4` | Checks `MapTransitionRequestGate.TryAcquire` `0xCD0420` (only one pending map change), then queues |
| Dispatch | `EventQueue.Update` `0xCCF5E4` → `ProcessSyncEvent` `0xCCF7C4` → `DisplayClass8_0.b__2` `0xCCFF30` (`Delegate.DynamicInvoke` of `OnEvent(T)`) | | **Hook `b__2` and `b__3` to set the per-event `inInterface` flag.** The handler map is built by reflection over the `IEventHandler` fields of `EventHandler` |
| Per-map sim queue | `TickFlowHandler.EnqueueEvent(evt, force)` | `0xD28034` | `force=true` goes to `_forceQueue` (+0x20). `Update` `0xD28120` drains the force queue **when no TickRequested is queued, that is while paused**. So instant skills run during pause, outside any tick |

Handler classes, with OnEvent counts (248 in total): Item 36, Entity 40, Input 69, PlayerControl 23, UserSetting 26, Campaign 12, CampaignLoading 8, Task 8, System 9, Resolver 6, Crafting 6, Farming 3, Interaction 1, TaskFinish 1.

## 1. Capture model

- **Flags** (same idea as RimWorld): `inInterface`, `executingCmd`, `ticking`.
- **Tier A.** At `b__8_0`, check the event's class:
  - COMMAND: serialize it, broadcast it, and return without calling `AddEvent`.
  - LOCAL: let it through and remember the pointer.
- **Tier B.** In `b__2` and `b__3`, if the event being dispatched is a remembered LOCAL input event, set `inInterface=1` while its handler runs. Tier B hooks act only when `inInterface && !executingCmd`. Note that all Tier B functions have UI-only callers anyway (grep of all 6802 decomp files):
  - `UseSkillMode.UseSkill`: SkillOrder, UseSkillMode, EntityEventHandler.
  - `ActionOrder.Execute`: InteractionRunner.
  - `MoveOrder.*`: CommandFlow, DefaultCommand.
  - `DirectTaskHelper.Order*`: ActionOrder, EntityEventHandler.
  - `ControlModeProcessor.Left/RightClick/Drag`: PlayerControlEventHandler.
- **Every command carries this context:**
  - `playerId`
  - `MapType`
  - the issuer's selection: the Guid list plus `SelectGroup`
  - for build modes only: construction key and rotation
- **Replay steps:**
  1. Set `executingCmd=1`.
  2. Swap in the selection by writing straight to `SelectDataContainer._selectGroup` (+0x10) and `._selectedEntities` (+0x18), with no view dispatch.
  3. Call the handler or the function.
  4. Restore the selection.
- **Object refs.** Entity → Guid uses `get_Guid`:
  - UnitEntity `0xFA856C` (field +0x24)
  - BuildingEntity `0xFA4280`
  - ItemEntity `0xFA806C`
  - ThingEntity `0xFA84C8`
  - FarmEntity `0xFA7A48`
  - BlueprintEntity `0xFA3EAC`
- **Guid → entity:**
  - `InMapDataInterface.TryGetEntity(guid, out)` `0xFC16FC`, per map. The supplier dictionaries are, for example, `ControlModeFactory._inMapDatas` +0x68 and `PlayerControlEventHandler._inMapDatas` +0x70.
  - Fallbacks: `EntityDataHandler.TryGetEntity` `0xBD5F48`, `UnitDataContainer.GetEntity` `0xC897EC`, `ItemDataContainer.GetEntity` `0xC88F08`.
  - [INFERRED] Inventory items may not be registered on the map, so use the item container as the fallback.
- **Skills.** Send the key only. Resolve it back with `CombatDataManager.GetActiveSkillData(string)` `0x7F44A8`. The skills are shared data objects; every UI path gets them from there.
- **Finding instances for Tier B on a peer that never clicked.** Walk from the root `EventHandler`. It is created in `MainFlow.InitCore` `0xD19728`; capture its `this`.
  - `EventHandler`+0x38 → `PlayerControlEventHandler`, then +0x20 → `ControlModeProcessor`.
  - `GetProcessor(mode)` `0xD45B2C` returns the mode objects. Map: 0=Default, 1=Gathering, 2=Cancel, 3=PickUp, 4=Demolition, 5=Construction, 6=UseSkill.
  - `DefaultMode`+0x18 → `DefaultCommand`, then +0x20 → `CommandFlow`. From `CommandFlow`: +0x10 → `InteractionRunner`, +0x18 → `MoveOrder`, +0x20 → `WaypointOrder`.
  - `InteractionRunner`+0x18 → `ActionOrder`. From `ActionOrder`: +0x10 → `DirectTaskHelper`, +0x18 → `SkillOrder`.
  - **Mode objects are cached per current map** (`ControlModeFactory._modeMapCacheDictionary` +0x18). Execution therefore assumes both peers have the same `_currentMap`. See the open questions.

## 2. Catalog

Column meanings:

- **Ctx** = UI context read at execution: Sel = `ISelectReader`, Mouse = mouse or raycast, Hover = the `InteractionOptionControl` cache, Mode = the control-mode context, CTC = `ConstructionTargetControl`, Party = `ExpeditionPlanPartyContext`, CurMap = the handler's `_currentMap`.
- **Safe@evt** = replaying the event object gives the same state change.

### 2.1 Unit orders (dungeon core)

| Action | UI origin | Intercept (RVA) | Args → Guid | Ctx | Safe@evt |
|---|---|---|---|---|---|
| Right-click move (group, formation) | Mouse `InteractGrid` → `InteractGridRequested` → `PlayerControlEventHandler.OnEvent` `0xDBD324` → `ControlModeProcessor.RightClick` `0xD45628` → `DefaultMode` → `DefaultCommand.RightClick` `0xC054A8` (no known interaction) | **`MoveOrder.All(ISelectReader, Vector2Int)` `0xC07164`**, preceded by `WaypointOrder.ClearWayPoint(select)` `0xC06F5C` | selection Guids, cell | Sel (passed in) | No. The event reads Hover. Hook the function. It runs `FormationTarget.Resolve` / `TryAssign` / `SendOrders` and then `DirectOrderRequested(unit, MoveTask)` to `EnqueueEvent` |
| Right-click on a target (attack, heal, gather, operate, pick up item, carry, NPC, chest, trade, portal walk…) | same → `InteractionRunner.Run(pos, select)` `0xC0678C` (reads Hover through `RunCheck.GetActions` / `IsKnown`) | **`ActionOrder.Execute(Interaction, UnitEntity, RunCheck)` `0xC04A38`**, once per unit; plus `MoveOrder.Near(unit, pos)` `0xC0D30C` for units with no action | `Interaction` struct: `InteractionType`@0, `UserEntity`@0x28, `TargetEntity`@0x30, `ItemEntity`@0x38 (all Guids), `Index`@0x40, `InteractionInputType`@0x44, `TaskType`@0xD0. Skip `UIInput`@0x48 and the strings | `RunCheck` is only used for FirstAid (`RescueCheck.CanRescue`). Use the local instance [INFERRED: map state only] | No |
| → Attack / Heal (types 2/4) | inside `ActionOrder` | `SkillOrder.Use(Interaction)` `0xC04D64` | user and target Guid | none | Covered by `ActionOrder`. It enqueues `DataApplyRequested(CombatTargetApplyData(user,target))` and calls `UseSkill(user, target, basicAttack, SpecificTarget(target), null)` |
| → FirstAid (6) | inside `ActionOrder` | `DirectTaskHelper.ReturnSharedTask` `0xDD2204` + `OrderPersonal` | | | covered |
| → Recreation (25) | inside `ActionOrder` | `DirectTaskHelper.OrderRecreation(Interaction)` `0xDD0BE0` | | | **No at `DirectOrderRequested` level.** `TryRegisterRecreation` writes slot state before it enqueues |
| → Portal (29) | inside `ActionOrder` | `PortalOrder.Open` `0xC050B0` | | | Only opens a popup (local). The real travel is §2.6 |
| → Personal and shared tasks (everything else) | inside `ActionOrder` | `DirectTaskHelper.OrderPersonal` `0xDD05D8` / `OrderSharedTask(Interaction)` `0xDD0274` | | | **No at event level.** Shared tasks change `TaskGroup` / `SharedTaskData` directly (`AssignExistTask`, `PrepareTask`, `TryAssignIngredientTask`) |
| Shift + right-click: waypoint queue | `ReserveWaypointRequested` `0xDBCEAC` → `RightClick(shift)` → `DefaultCommand.ReserveWayPoint` `0xC0535C` | **`WaypointOrder.Reserve(select, pos)` `0xC072D4`** + `MoveOrder.TryMove(select)` `0xC07440` | selection, cell | Sel | No. Waypoints are sim state in `PlayerUnitDataContainer.WayPointWriter`. Continuing to the next waypoint is sim-origin (`ContinueWaypointRequested` from `TaskApplier`) |
| Alt + right-click: options menu | `DefaultCommand.AltClick` `0xC0543C` → `CommandView.ShowOption` | LOCAL (opens a menu). The chosen option arrives as one of the next three events | | | |
| Option: "do task X" | `UIInput 211` → `DirectTaskRequested` | **`EntityEventHandler.OnEvent(DirectTaskRequested)` `0xD8AED8`** | `Map`, `TaskType`, `Index`, `UserGuid`, `TargetGuid`, `ItemGuid?` | none | **Yes** (Tier A). Internally it reaches `UseSkill` / `DirectTaskHelper`, which pass through because `executingCmd` is set |
| Option: use skill on target | `UIInput 213` → `UseSkillTargetRequested` | **`EntityEventHandler.OnEvent` `0xD8BA38`** | `Map`, `UserGuid`, `TargetGuid`, `SkillKey`, `ItemGuid?` | none | **Yes** |
| Skill use (quick slot key 1–5 or button, then target click; or instant cast) | `UseQuickSlotSkillRequested(Index)` `0xDBE600`: reads Sel, `Input.mousePosition`, `MainCamera.ScreenToWorldPoint`; switches to UseSkill mode or calls `LeftClick`. Then `SelectGridRequested` → `UseSkillMode.LeftClickInteraction` `0xC110B4` (raycast through `EntityUtility.GetRaycastEntities` `0xFA5714`) | **`UseSkillMode.UseSkill(user, target, ActiveSkill_Base, ICombatTargetSelector, ItemEntity)` `0xC0E2A0`** | user Guid, target Guid, skill key, item Guid, **selector descriptor** (see the note below this table) | Sel, Mouse, Mode (all resolved before `UseSkill`) | Events: No. Function: Yes. It builds `ActCombatBehaviourTask(skill, selector, item, target)` → `DirectOrderRequested`; instant skills use the force queue plus `ForceTickCombatBehaviour` |
| Item use (potion, scroll; from inventory option or item quick slot) | `UseItemRequested` `0xDBF2D4` / `UseItemQuickSlotRequested` `0xDBFB78` → `TryUseItem` `0xDBF888` (mouse → `LeftClick`) | same `UseSkillMode.UseSkill` | same, with `ItemEntity` | Sel, Mouse | Events: No (LOCAL). Function: Yes |
| Auto-skill toggle (quick-slot right-click) | `UseQuickSlotSkillAutoRequested(Index)` `0xDBE248`: reads Sel and the `IQuickSlotViewer` (UI) | **Function: `SkillComponent.ChangeAutoSkillData(comp, key)` `0xDDAF2C`**. Capture `{selected unit Guid, key}`. On replay: `EntityComponent.GetComponentOf<SkillComponent>` `0x185F284` | unit Guid, skill key | Sel + UI viewer | **No at event level** (UI viewer) |
| Auto-skill toggle (option menu, `UIInput 203–210`) | `ToggleAutoSkillRequested` | `InputEventHandler.OnEvent` `0xD97474` | `Map`, `UnitGuid`, `Index` | none [INFERRED] | Yes |
| Behaviour options F/G/H (AutoAttack=1, WaitForOrder=2, RestFirst=4; also PickUp=8, Gathering=16) | `KeyInput 48–50` or `UIInput 148` → `ToggleBehaviourOption` | `PlayerControlEventHandler.OnEvent` `0xDC03E0` → `EntityInfoComponent.SetBehaviourOption` `0xDD85F4` | `Target` (IEntity → Guid, may be null), `Option`, `IsOn?` | **Sel when `Target==null`** | Yes, with the selection context. **`TaskApplier` also creates this event (sim origin), so only capture it at `b__8_0`** |
| Task options (pick-up / gather) | `UIInput 149` → `ToggleTaskOptionRequested` | `TaskEventHandler.OnEvent` `0xDC7400` | `Option`, `Map`, `IsOn` | Sel | Yes + Sel |
| Carry unit: put down | `UIInput 159` → `CarryUnitGetDownRequested` | `InputEventHandler.OnEvent` `0xD980E8` | `Map`, `UnitGuid` | none | Yes |
| Abort / stop | (none) | `UnitAbortOrderRequested` is created only by `EntityEventHandler.OnEvent(AbortOrderRequested)`, and that only by `MainEventApplier` (sim) | | | Nothing to sync. **I found no player "stop" command** |
| Formation drag stroke | (none) | `FormationMoveOrder.TryOrder` `0xC0A0EC` has no callers outside its class; `FormationStroke` is empty | | | dead code |

**Skill target selector.** `ICombatTargetSelector` is a closure chain and cannot be read back. Record a descriptor when it is built, by hooking the factories while `inInterface` is set, and keep a pointer → descriptor map. The factories are:

- `EntityTargetSelector.SpecificTarget(e)` `0x84927C`
- `SpecificUsableTarget(e)` `0x8493CC`
- `SelectFromSpecificTargets(list)` `0x848F4C`
- `Self()` `0x848DC4`
- `ClosestHostile()` `0x848394`
- `GridTargetSelector.SpecificGrid(GridData{Position@0x10})` `0x7B56C0`
- `ToGrid(sel)` `0x7B54E0`

`UseSkillMode.GetPossibleTarget` `0xC1158C` returns only `SelectFromSpecificTargets`, `Self`, or `SpecificGrid`. `SkillOrder` and `DirectTaskRequested` use `SpecificTarget`; `UseSkillTargetRequested` uses `SpecificUsableTarget`.

### 2.2 Items

All of these are `UIInput` → `ItemEventHandler`, with Guid- or index-based fields. Tier A, safe at the event unless the Ctx column says otherwise. `ICP` = `ItemControlProcessor`.

| Action | Event (handler RVA) | Fields | Ctx / note |
|---|---|---|---|
| Equip | `EquipItemRequested` `0xDA2DF0` | `Map`, `targetGuid`, `SelectedIndex`, `TargetGuid`, `Index`, `Amount?` | Sets the equip cooldown (in combat) |
| Unequip | `UnequipItemRequested` `0xDA3044` | `Map`, `TargetGuid`, `SelectedIndex`, `Index`, `Amount?` | |
| Transfer inventory → other unit | `TransferItemRequested` `0xDA11A8` | `Map`, `From`, `Index`, `To`, `Modifier`, `Amount?` | |
| Equip → inventory / other / transfer-and-equip | `TransferFromEquipRequested` `0xDA1600`, `TransferEquipRequested` `0xDA1910`, `TransferAndEquipRequested` `0xDA1CB0` | `Map`, `Index`, `From`, `To` | |
| Storage transfer | `TransferStorageItemRequested` `0xDA24B8` | `Map`, `Index`, `SourceGuid`, `ToGuid`, `SlotIndex` | |
| Split / half / send | `SplitDropItemRequested` `0xD9F6EC`, `SplitSendItemRequested` `0xD9FB24`, `SplitMoveItemRequested` `0xD9FCA4`, `HalfSendItemRequested` `0xDA0484` | `Map`, `UnitGuid`, `Amount`, `Index`, `ToIndex` / `ToUnitGuid` | The `ShowSplit*` popups before them are LOCAL |
| Drop (drag out, or onto a unit) | `DropItemRequested` `0xD9CDD4` | `IsInventory`, `Map`, `UnitIndex`, `Modifier`, `UnitGuid`, `Index`, `Amount?` | **Mouse: `EntityUtility.GetRayCastPlayerEntities` `0xFA6CAC`.** Record the returned UnitEntity Guid and hook the function to return it during replay. `UnitIndex` depends on player-unit order (see party swap) |
| Move slot / sort | `ChangeItemSlotRequested` `0xDA22BC`, `SortItemRequested` `0xDA20C8` | `Map`, `Owner`, `From/ToIndex`, `Modifier` / `Owner` | |
| Dispose | `DisposeItemRequested` `0xDA347C`, `DisposeItemAmountRequested` `0xDA3628` | `Map`, `SelectedIndex`, `TargetGuid`, `Index` / `Guid`, `Index`, `Amount` | |
| Auto-repair allow/ban | `Allow/BanAutoFixItemRequested` `0xDA3188` / `0xDA32E0` | `Map`, `SelectedIndex`, `TargetGuid`, `Index`, `Inventory` | |
| Ammo select | `SelectAmmoRequested` `0xDA3B80` | `Map`, `Key` | **Sel** (single selected unit) |
| Expedition-panel moves | `ExpeditionDropItemRequested` `0xD9E27C`, `ExpeditionTransferItemRequested` `0xD9E6B8`, `ExpeditionEquipFromGroundRequested` `0xD9EA58`, `ExpeditionEquipFromInventoryRequested` `0xD9EFC8` | Guids + indexes | **Party: `IsExpeditionPartyUnit` `0xD9E578` reads the UI `ExpeditionPlanPartyContext`.** Carry the party Guid list or record the result |
| Item quick slot set/unset | `SetItemQuickSlotRequested` `0xD970BC`, `UnsetItemQuickSlotRequested` `0xD97EB0` (InputEventHandler) | `Map`, `UnitGuid`, `ItemKey` | |
| Head-gear visibility | `ToggleHeadEquipmentVisibilityRequested` `0xD86D64` | `Map`, `UnitGuid` | Cosmetic, but saved [INFERRED]. Sync it anyway (cheap) |
| Pick up from ground (dungeon) | Done by right-click → `ActionOrder` (`PickUpItem` = 37) or by tasks. `PickUpItemRequested` / `CarryUp` / `CarryDown` / `TransferCarry` / `CarryInventory` / `RemoveCarry` / `ConsumeAmmo` / `DespawnItem` / `RemoveFrom*` all come from `MainEventApplier` (sim) | | Nothing to sync. **`PickUpItemRequested` and `DespawnItemRequested` call `SelectProcessor.Toggle`**, a local selection side effect that is harmless |
| Auto-pickup on/off, weight rule | `ToggleAutoPickUpRequested` `0xD66C7C`, `PickUpWeightRuleChanged` `0xD66D80` (Campaign) | — / `Weight` | Sim campaign setting. The handler body shows only view calls, so the field write is inline [INFERRED]. **Sync it** |
| Pick-up mode (drag area) | §2.3 | | |

### 2.3 Base and construction (control modes)

Every build mode takes a click as `(pos, shift)` and a drag as `(start, end)` from `SelectGridRequested` `0xDBCC94` and `DragSelectRequested` `0xDBCC60`. **Intercept at the mode method.** It writes directly; there is no event.

| Action | Intercept (RVA) | Args | Ctx | Direct writes seen |
|---|---|---|---|---|
| Build (place blueprint or immediate floor/wall/door) | `ConstructionMode.LeftClickInteraction` `0xBFBF88`, `.DragSelection` `0xC013F4` | map, pos(s), shift | **CTC: `_constructionKey`@0x18, `_constructionRotation`@0x20, `_isFreeConstructionMode`@0x10, `_isRemoveFloorCreepClearMode`@0x11** (the last two are cheats; force false). Replay with `SetConstructionKey` `0xFA9288` / `SetConstructionRotation` `0xFA9320` | `EntityLifecycleHelper.SpawnBlueprint` `0xC976C0`, `BuildingSpawnHelper.Construct` `0xC92714`, `RemoveTileFloor` `0xC9842C` / `Deco` `0xC98694`, `BlueprintRemoveHelper.RemoveBlueprint`, `BuildingRemoveHelper.RemoveBuildingProcess` `0xFB4E70`, `TaskGeneratorContainer.GenerateTask` `0xD312AC`, `ConstructionComponent.SetReplacementTarget`, `AStarMapSetting.SetBlock`, `ConstructionCheckTool.RemoveRequireItemsFromPlayerOwned` |
| Demolish | `DemolitionMode.LeftClickInteraction` `0xC0EFC8`, `.DragSelection` `0xC0F770` | map, pos(s) | CTC (field held) [INFERRED: not used for the target] | `GenerateTask`, `RemoveBuildingProcess`, `DemolishFarm` `0xFB5130` |
| Cancel orders and blueprints | `CancelMode.LeftClickInteraction` `0xBF8254`, `.DragSelection` `0xBF890C` | map, pos(s) | none | `CancelPublishedTasks` `0xD306D8`, `RemoveBlueprint`, `GenerateTasks` |
| Gather designation | `GatheringMode.LeftClickInteraction` `0xC0FE88`, `.DragSelection` `0xC1029C` | map, pos(s) | none | `GenerateTask` / `GenerateTasks` `0xD314B0` |
| Pick-up designation | `PickUpMode.LeftClickInteraction` `0xC10818`, `.DragSelection` `0xC10BEC` | map, pos(s) | none | `RemoveTaskByTargets` `0xD30B44`, `GenerateTask(s)` |
| Mode switch, category, building key, rotate | `BaseControlModeRequested` `0xDBD690`, `SelectBuildingCategoryRequested` `0xDBD464`, `SelectBuildingKeyRequested` `0xDBD4A4`, `SelectConstructionRotateRequested` `0xDBD540` | | | **LOCAL.** Sent as context on the commands above. Also emits `InputObserver` (ControlModeGather/PickUp) → §2.9 |
| `ConstructionBackUp` mode | `0xBF9A84` / `0xBFA7F8` | | | [INFERRED] unused (the factory's `CreateMode` only builds Default/Cancel/Construction/Demolition directly). Hook it anyway for safety |

Alternative: hook `ControlModeProcessor.LeftClick` `0xD4548C` / `Drag` `0xD457DC` once and sync when `CurrentMode ∉ {Default, UseSkill}`. That is one hook instead of ten, and it is RimWorld's "Designator" pattern. The same context still applies.

### 2.4 Recruit, shop, trade, crafting, research, work, policies (base and town)

All Tier A unless noted.

| Action | Event → handler (RVA) | Fields | Ctx / hazard |
|---|---|---|---|
| Recruit candidate | `CandidateRecruitRequested` → Entity `0xD80FE8` | `Index`, `Map`, `Position`, `Faction` | Candidate list is sim state (`RecruitHelper`) |
| Reroll candidates | `RecruitCandidatesRefreshRequested` `0xD80C1C` | — | Uses `ICampaignRandomService`, which must be synced or seeded |
| Retire / rename | `RetireUnitRequested` `0xD86008`, `RenamePlayerUnitRequested` `0xD86980` | `Map`, `TargetGuid`, `IsPaidRetire` / `NewName` | `OpenRetireFlow` / `OpenRenameFlow` are LOCAL |
| Bed owner | `AssignBedOwnerRequested` `0xD86EF4` | `Map`, `BedGuid`, `OwnerGuid` | |
| Level-up roll / accept | `RollLevelUpRequested` `0xD872C4`, `AcceptLevelUpRequested` `0xD88CF4` | `Map`, `OfferingType`, locks / `ChoosedInscriptionIndex`, `Target` | Target = `LevelUpHelper._levelUpTargetUnit` (+0x48). It is set by sim `InteractBuildingRequested` → `LevelUpHandler.Process` `0xF9E860` → `SetLevelUpTargetUnit` `0xC9F8B8`. **There is one global target, so only one level-up at a time.** The roll uses `ICampaignRandomService` |
| Learn skill / skill tree / talent | `LearnSkillRequested` (UI) `0xD89FC8`. `AddSkill`, `AddSkillTree`, `RemoveSkill*`, `ImproveTalent` come from `MainEventApplier` (sim) | `Map`, `UnitGuid`, `SkillKey` | |
| Quick-slot set / unset / move | `SetQuickSlotRequested` `0xD96708`, `UnsetQuickSlotRequested` `0xD9789C`, `MoveQuickSlotRequested` `0xD96040` | `Map`, `UnitGuid`, `Skill` / `Index`, `TargetIndex` | Adds or removes skills and registers passives |
| Party / portrait order swap | `SwapShortCutRequested` → Input `0xD92804` → `EntityDataContainer.SwapPlayerUnitOrder` `0xC808B4` | `Map`, `UnitGuid`, `UnitGuid2` | Player-unit order affects `UnitIndex` fields and select hotkeys. **Sync it** |
| Trade (merchant) | Session: sim `TradeRequested` (`MainEventApplier`) → `TradeStartHelper.TryPrepareTrade` `0xCB3D7C` → `TradeHelper.BeginSession` `0xCB2178` (**`Guid.NewGuid` for `_sessionId` +0x44**). Commit: `UIInput 245` → `TradeCommitRequested` → Item `0xD9C800` → `TradeHelper.TryCommit` `0xCB2D14` | `TradeCommitRequest{Map@0x10, SessionId@0x14, Deliveries: [TradeCommitDelivery{SlotId, Destination, TargetOwnerId, Amount}]}` | **`TryCommit` checks `SessionId == _sessionId`**, which is a per-peer `NewGuid`. Fix it with the deterministic-Guid patch, or rewrite `SessionId` to the local `_sessionId` on replay. Only one session at a time. All `TradeUI_*` slot drags are LOCAL until commit |
| Trade pool init/set | `TradePoolInitializeRequested` / `TradePoolSetRequested` from `MainEventApplier` | | sim (`PopulateTradeInventoryFromPool` uses `UnityEngine.Random.Range`, a determinism issue) |
| Crafting: add / remove / option / pending / repeat / priority | `CraftingTaskGenerateRequested` `0xD7CAC0`, `CraftingTaskRemoveRequested` `0xD7D0EC`, `CraftingOptionRequested` `0xD7D7F8`, `CraftingPendingRequested` `0xD7DB9C`, `CraftingRepeatRequested` `0xD7E374`, `CraftingPriorityRequested` `0xD7E8A0` | `MapType`, `RecipeKey`, `RepeatTimes` / `Index`, `Option`, `Delta`, `Count?` | **Sel** (selected building, through `GetSelectedTaskGroup` `0xD7E088`) |
| Farming plan +/- | `AddFarmingRequested` `0xD8E8E4`, `ReduceFarmingRequested` `0xD8F334` | `FarmingKey`, `Amount` | **Sel** (`InitializeSelectedFarm` `0xD8EBB8`) |
| Research select / add / unlock / progress | `ResearchSelectRequested` [handler not found in the OnEvent list; INFERRED], `ResearchTaskGenerateRequested` → Task `0xDC6FFC`, `UnlockResearchRequested` → Entity `0xD8BEF8`, `ResearchProgressedRequested` → Input `0xD983E4` (`UpdateWorkload`) | `ResearchKey`, `Map`, `AddedWorkload` | `ResearchProgressedRequested` is a UI event that adds workload. Sync it |
| Work on/off, work priority | `WorkToggleRequested` `0xDC0AF4`, `WorkPriorityRequested` `0xDC17D4` | `Map`, `Active` / `Priority` | **Sel**. Cancels running tasks and sends `TaskRefresh` |
| Per-unit task priority | `SetTaskPriorityRequested` → Input `0xD9A818` | `Priority`, `TaskType`, `UnitGuid` | Body shows only a view call; inline write [INFERRED] |
| Storage policy: single / durability / allow-all / ban-all | `SinglePolicyRequested` `0xD9A2E4`, `DurabilityPolicyRequested` `0xD9A580`, `AllowAll` `0xD99BDC`, `BanAll` `0xD9A0FC` | `ItemKey`, `IsAllowed` / `Min`, `Max` | **Sel** (`TryGetStorages` `0xD99DC4`) |
| Storage policy copy / paste | `CopyPolicyRequested` `0xD995A4`, `PastePolicyRequested` `0xD99974` | — | **Sel + a per-peer clipboard.** Sync Copy as a command too, so both buffers match |
| Consume (food) policies | `IndividualPolicyRequested` `0xD984E4`, `BatchPolicyRequested` `0xD986E4`, `ResetPolicyRequested` `0xD99000`, `UnitConsumePresetRequested` `0xD988E0`, `UnitConsumeCopy` `0xD98AE0` / `Paste` `0xD98CD4` / `ClearCopy` `0xD98ECC` | `ItemKey`, `UnitGuid`, `IsAllowed`, `Preset` | Bodies show only view calls (inline writes) [INFERRED]. The copy buffer works like the storage clipboard |
| Remove player-owned item | `RemoveFromPlayerOwnedItemRequested` → Entity `0xD8BEC8` | `Map`, `ItemKey`, `Amount` | |

### 2.5 Story events and alerts

| Action | Event → handler | Fields | Ctx / note |
|---|---|---|---|
| Choose a story option | `UIInput 229` → `SelectChoiceRequested` → Resolver `0xDC3960` (`ActionContext`, `ContainsShowNextNode` `0xDC3B84`) | `ChoiceKey` | Reads `_currentMap` and the current event node. The node is opened on both peers by sim `EventRequested` (from `ObserverDriven`, `DecisionSystem` and `RandomEventSystem`) [INFERRED: node state is sim state]. Decide who may choose: first come or host |
| Alert click / right-click | `AlertClickRequested` `0xDC2D5C`, `AlertRightClickRequested` `0xDC339C` | `Map`, `AlertKey` | Click → `OnClickEvent` (may start actions) [INFERRED effects]. Right-click dismisses. Sync both |
| Event dismiss / NPC event | `EventDismissRequested`, `NPCEventRequested` | | sim origin only |

### 2.6 Map travel

| Action | Origin | Intercept | Fields | Note |
|---|---|---|---|---|
| Go to dungeon (expedition panel) | `UIInput 102` → `MapTransitionRequest` (`.ToEvent` `0xD62060`) → `ChangeMapRequested` | **`CampaignLoadingEventHandler.OnEvent(ChangeMapRequested)` `0xD6F114` (async, coroutine `d__100`)** | `FromMap`, `ToMap`, `Region`, `IndexList` (party Guids), `Floor`, `IsSkipSafeFloor`, `HasForcedDungeonSeed`, `ForcedDungeonSeed`, three commit flags, `VisualGuardMode` | Admission gate allows one pending request. The transition uses real-time waits, so it needs a barrier. **Send a forced dungeon seed.** Party chosen in the UI-only `ExpeditionPlanPartyContext` `0xEDFB74` is carried in `IndexList` |
| Return to base | `UIInput 108` → `ChangeMapRequested` | same | same | |
| Next floor | `GoDownDungeonRequested` → `0xD6F2C8` → `MapTransitionRoutingService.QueueGoDownDungeon` `0xDBA118` | `IndexList` | Abyss variant `0xDBA410` |
| Floor clear / region clear | `DungeonFloorClearRequested` → System `0xDC462C`; `ClearDungeonRegionRequested` → Campaign `0xD69228` (async) | — | [INFERRED: UI `DungeonResult` buttons 107/110] |
| Region / safe-floor choice | `SetRecentlySelectedRegionRequested` `0xD692CC`, `SetSafeFloorSkipRequested` `0xD69384` (async) | `Region` / `IsSkipSafeFloor` | Campaign data. Sync |
| Portal interaction | `InteractPortalRequested` `0xDBF040` | | Handler only calls `Debug.Log` (dead). Portal walking is an `ActionOrder` task |

### 2.7 Speed, pause, popups

| Action | Origin | Handler → setter | Note |
|---|---|---|---|
| Set speed (keys 25–32, speed button `UIInput 112`) | `SetGameSpeedRequested` / `SetGameSpeedStopOrRollBackRequested` / `ChangeGameSpeedRequested(Speed, PauseState)` | System `0xDC44F0` / `0xDC47E0` / `0xDC4948` → `GameSpeedSetting.SetSpeed(speed, pauseState)` `0xD24114` | **Make these net commands that change "ticks per step"** (RimWorld lesson 1). `SetGameSpeedRequested` is also created by `MainEventApplier`, and Stop by `ObserverDriven.StopTimeAction`; `Main.GameSpeedAction.Execute` `0xCD7B94` is a story action. Those sim-origin calls happen on both peers, but they conflict with the vote model |
| Speed lock | `GameSpeedLockRequested` | `0xDC4908` → `SetLock` `0xD24B98` | |
| Pause reasons (User 0, EssentialPopup 1, CheatConsole 2, Loading 3, PopupTimeLock 4, ConsecrationNotice 5) | `UIInput 97/98` → `Add/RemoveGamePauseReasonRequested` (UI-only creator) | `0xDC4A74` / `0xDC4BA0` → `AddPauseReason` `0xD24848` / `RemovePauseReason` `0xD249BC` | Popups pause through these events, which gives the "one pauses, all pause" vote for free if synced. Loading is set directly by the transition code (`EnterVisualGuard` and others) on both peers |
| Force-queue execution while paused | `TickFlowHandler.Update` `0xD28120` drains `_forceQueue` when no tick is queued | | **Commands issued during pause (instant skills) run with no tick.** The sequencer must give them a "paused sub-step" order |

### 2.8 Settings

Sim-relevant, so sync them: auto-pickup and weight rule (§2.2), region and safe-floor (§2.6), consume and storage policies (§2.4), `ReplyLockConsecrationNoticeRequested` → UserSetting `0xDCD1FC` (campaign Guid + `Enabled`) [INFERRED: campaign rule], game speed.

Local only: every other `UserSettingEventHandler` event (audio, mute, video, VSync, resolution, UI/tooltip/cursor scale, camera speed, key bindings, keyboard layout, input compatibility, language, log collection, edge-screen log, building lighting, update notice, keyboard notice, narrative intro). Also `ChangeAutoSaveIntervalRequested`; autosave should be host-only.

### 2.9 Hidden UI → sim channels (must be handled)

- **Tutorial / ObserverDriven input observers.** `InputBinding.AddKey/Mouse/UIInputToEventData` call `ObserverAggregator.Record(InputObserver)` `0xBBF998` for camera moves, zoom, guide, speed, mode switches and select-all. These feed story and tutorial triggers. Fix: either sync `Record(InputObserver)` as a command, or turn tutorials off in MP.
- **Cheat console** (`ToggleConsoleRequested`, KeyInput 47/69, `CheatConsoleCommand.*`, `CheatConsoleRuntimeController`) changes the sim directly. **Block it.**
- **`TEST_*` keys 65–70.** Block [INFERRED: probably compiled out].
- **`QuickLoadRequested`** (KeyInput 73), `LoadCampaignRequested`, `SaveFileRequested`, `QuitTo*`. Block, make host-only, or treat as a disconnect.

## 3. LOCAL-only events (never sync)

- **Camera:** `MoveCameraRequested`, `ZoomInOutRequested`, `UIZoomInOutRequested`, `WheelDrag{Started,ging,Ended}Requested` (they use `Time.deltaTime`), `CampaignEventCompletedRequested`.
- **Selection:** `SelectPlayerUnitRequested`, `ToggleSelectUnitRequested`, `SelectAllUnitRequested`. Also `SelectGridRequested`, `DoubleClickSelectGridRequested` and `DragSelectRequested` in Default mode. In build or skill modes these produce Tier B calls.
- **Hover / pointer:** `RefreshMousePosRequested` (fills the interaction cache), `LeftClickRequested`, `SelectUIRequested`, `InteractUIRequested`, `DragStart/Dragging/DragEndRequested`, `BlockInteractionRequested`, `Left{Shift,Alt,Ctrl}Requested`.
- **Right-click input:** `InteractGridRequested`, `ReserveWaypointRequested`. Their results are captured in Tier B.
- **Mode and UI context:** `BaseControlModeRequested`, `SelectBuilding{Category,Key}Requested`, `SelectConstructionRotateRequested`, `EscapeActionRequested`, `UseQuickSlotSkillRequested`, `UseItemQuickSlotRequested`, `UseItemRequested` (all captured in Tier B).
- **UI open/close:** `ToggleEntityUIRequested`, all `ShowInteraction*`, `ShowPopUpRequested`, `ClosePopUpRequested`, `ShowGuideRequested`, `ShowRecruit`, `ShowTaskPriority`, `ShowConsumePolicy`, `ShowDungeonResult`, `ShowDemoClearWishlist`, `ShowGameSetting`, `ShowSplit*`, `OpenRetireFlow`, `OpenRenameFlow`, `OpenSaveUI`, `RefreshPolicyView`, `ResearchRefresh`, `ReturnToGame`, `SetScreen`, `SetStartLanguage`, `OpenedItemCategoryToggleRequested` (panel UI preference).
- **Every `TradeUI_*` drag before commit.**

There are `IEventData` types with no handler in the OnEvent list: `ShowSystemMenu`, `Dragging`, `DragEnd`, `WayPoint`, `QuickPickUp`, `SelectExploreDungeon`, `InteractUI`'s siblings and `ResearchSelect`. They are either unused or handled outside the 14 classes [INFERRED]. Log any unknown type at `b__8_0` during testing.

## 4. Hazards and open questions

1. **Current map.** Mode objects are cached per `_currentMap`, and many handlers keep their own `_currentMap`. v1 rule: all players stay on the same current map. It is unverified whether one player can view Base while the party is in the Dungeon.
2. **Per-player popups opened by sim:** level-up, trade, story nodes (`InteractBuildingRequested` handlers, `EventRequested`). Both peers open them. You need an ownership filter for UI only (PLAN step 4).
3. **Global single-instance helpers:** `LevelUpHelper` target, `TradeHelper` session, `MapTransitionRequestGate`. Two players at once will collide, so serialize them through ownership or a lock.
4. **Randomness on the command path:** `ICampaignRandomService` (recruit reroll, level-up roll), `UnityEngine.Random.Range` in `CodexUnlockResearchRequested` and `PopulateTradeInventoryFromPool`, and `Guid.NewGuid` in `TradeHelper.BeginSession`. These are for the determinism lane.
5. Some handler bodies show only view calls, so their state writes are inlined field stores: policies, auto-pickup, `SetTaskPriority`. They are marked [INFERRED]. They are still safe as Tier A events.
6. The `UIInputType` → event mapping is inferred from names; the decompiled switch is a jump table and was not reliable. It does not matter, because capture happens after conversion.

## 5. Minimal interception sets

### Set 1: first playable, dungeon (13 hooks + 2 support hooks)

| # | Hook | RVA | Covers |
|---|---|---|---|
| 1 | `InputDispatcher.<Update>b__8_0` | `0xD3BAA4` | Tier A classifier and capture. Commands: `DirectTaskRequested`, `UseSkillTargetRequested`, `ToggleBehaviourOption`, `ToggleTaskOptionRequested`, `ToggleAutoSkillRequested`, `CarryUnitGetDownRequested`, all §2.2 item events, `SelectAmmoRequested`, `SetQuickSlot`/`Unset`/`Move`, `SetItemQuickSlot`/`Unset`, `SwapShortCutRequested`, `ToggleAutoPickUpRequested`, `PickUpWeightRuleChanged`, `SelectChoiceRequested`, `AlertClick`/`RightClick`, `ChangeMapRequested`, `GoDownDungeonRequested`, `DungeonFloorClearRequested`, `ClearDungeonRegionRequested`, `SetSafeFloorSkip`, `SetRecentlySelectedRegion`, all speed and pause events, `RollLevelUp`/`AcceptLevelUp`, `LearnSkillRequested`. Blocks console and quick-load |
| 2 | `EventQueue <>c__DisplayClass8_0.b__2` / `8_1.b__3` | `0xCCFF30` / `0xCCFFEC` | Sets `inInterface` per LOCAL input event; also the replay entry |
| 3 | `MoveOrder.All` | `0xC07164` | Group move |
| 4 | `MoveOrder.TryMove` | `0xC07440` | Waypoint start |
| 5 | `MoveOrder.Near` | `0xC0D30C` | Fallback move |
| 6 | `WaypointOrder.Reserve` / `.ClearWayPoint` | `0xC072D4` / `0xC06F5C` | Waypoints |
| 7 | `ActionOrder.Execute` | `0xC04A38` | Attack, heal, interact, gather, pick up, carry, first aid, recreation, portal-walk |
| 8 | `UseSkillMode.UseSkill` | `0xC0E2A0` | All skill and item use |
| 9 | Selector factories (`SpecificTarget`, `SpecificUsableTarget`, `SelectFromSpecificTargets`, `Self`, `SpecificGrid`) | `0x84927C` `0x8493CC` `0x848F4C` `0x848DC4` `0x7B56C0` | Selector descriptors for #8 |
| 10 | `SkillComponent.ChangeAutoSkillData` (only under `UseQuickSlotSkillAutoRequested`) | `0xDDAF2C` | Auto-skill from the quick slot |
| 11 | `EntityUtility.GetRayCastPlayerEntities` (record / replay) | `0xFA6CAC` | `DropItemRequested` context |
| 12 | `ObserverAggregator.Record` (`InputObserver` only) | `0xBBF998` | Tutorial triggers (or turn tutorials off) |
| 13 | `TradeHelper.TryCommit` (rewrite `SessionId`) | `0xCB2D14` | Dungeon merchant trade |

Support (not hooks):

- Capture the root `EventHandler` (from `MainFlow.InitCore` `0xD19728`).
- Swap the selection through `SelectDataContainer` +0x10/+0x18.
- Guid lookups as listed in §1.

### Set 2: base and town (add these)

| Hook | RVA | Covers |
|---|---|---|
| `ControlModeProcessor.LeftClick` / `.Drag` (when `CurrentMode ∈ {1..5}`), or the ten per-mode methods in §2.3 | `0xD4548C` / `0xD457DC` | Build, demolish, cancel, gather, pick-up zones. Context: key and rotation through `ConstructionTargetControl` `0xFA9288` / `0xFA9320` |
| Extend the `b__8_0` command list | — | `CandidateRecruit`, `RecruitCandidatesRefresh`, `RetireUnit`, `RenamePlayerUnit`, `AssignBedOwner`, all `Crafting*`, `Add`/`ReduceFarming`, `ResearchTaskGenerate`, `UnlockResearch`, `ResearchProgressed`, `ResearchSelect`, `WorkToggle`, `WorkPriority`, `SetTaskPriority`, all storage and consume policy events including Copy/Paste/ClearCopy, `ToggleHeadEquipmentVisibility`, `RemoveFromPlayerOwnedItem`, `ReplyLockConsecrationNotice` |
| Context for Expedition item events | `IsExpeditionPartyUnit` `0xD9E578` (record / replay), or carry the party list | Expedition panel item moves |

I did not write any files. My scratch work (class index, handler map, event-creator index) is in `/private/tmp/claude-502/-Users-user/bdaa4039-1c35-4536-98c6-538d80610db9/scratchpad/` (`handlers.txt`, `evref_by.txt`, `eventfields.txt`, `cidx.tsv`).