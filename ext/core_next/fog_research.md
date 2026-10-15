# Per-player fog of war: research (build 25846389)

Date: 2026-10-15. Read-only research. Sources: `~/ds_skill_truth/decomp_25846389/*.c` (Ghidra, real names),
`~/ds_skill_truth/dump_25846389/dump.cs` (fields, slots), `script.json` (TypeInfo / MethodInfo addresses),
`~/ds_multiplayer/build/methods.tsv`, `build/det_ranges.tsv` (class 1 = view).
Interface calls in the decompile have no names. I resolved them by TypeInfo + slot:
IVisionDataReader `DAT_0664fe18`, IRuntimeDataReader `DAT_0664f8f0`, IInMapDataSupplier `DAT_0664f210`,
IMapDataReader `DAT_0664f3e8`, IEntity `DAT_0664ee28`. Script: scratchpad `ifc.py` (slot per call site).

## 0. Short answer

- The sim DOES read fog state, but only the two SHARED stores: `VisionArea` (each unit's seen tiles = its combat /
  aggro area) and `RawMapData._visitedPositions` (saved as `VisitedPositions`, part of the full-state hash).
  Do not change either.
- The renderer gets its data from `FogOfWarPresenter`, which calls 4 small leaf methods on `FogOfWarView`
  (SetVisible / SetHalfVisible / LoadVisible / SetNotVisited). Each call is for one tile. All are view (class 1).
  This is the clean hook point: keep the shared calls, and change only which leaf the call ends in, using a local
  "my visible" + "my explored" mask.
- Enemy hiding is also view only: `EntityObjectView.SetVisible(Guid, bool)` (0xE24640) is the choke point.
- There is a per-unit source: `VisionDataHandler._visions` = `Dictionary<Guid, VisionArea>`, keyed by unit/building
  guid. `_playerVisions` = `HashSet<Guid>` of player-side vision owners. This gives the per-unit filter.

## 1. How fog of war works (data flow)

Classes and fields (dump.cs):

- `Refactor.Runtime.VisionDataHandler : IVisionDataReader` (one per `RuntimeMapData`, at `RuntimeMapData+0x48`):
  `_mapData 0x10`, `_rawMapData 0x18`, `_visions Dictionary<Guid,VisionArea> 0x20`, `_playerVisions HashSet<Guid> 0x28`,
  `_playerVisionStacks int[,] 0x30` (per tile: how many player vision sources see it), `_refreshedPlayerVisions
  HashSet<V2I> 0x38`, `_refreshedHideVision HashSet<V2I> 0x40`, `_visibleVisionCaches HashSet<V2I> 0x48`,
  `_temporaryPlayerVisions Dictionary<V2I,int> 0x50`, buffers 0x58/0x60, `_temporaryVisionRestoreFog HashSet<V2I> 0x68`.
- `Refactor.Runtime.Map.VisionArea : IAreaData`: `Owner IEntity 0x10`, `VisionRange int 0x18`, `Positions List<V2I> 0x20`.
- `Refactor.Map.RawMapData`: `_visitedPositions bool[,] 0x48`; `SetVisited` 0xC4FC34, `GetVisited` 0xC4FD00,
  `IsVisited` 0xC4FF10. Saved as `RawMapSaveData.VisitedPositions` (bool[,], 0x38) -> in the save JSON -> in `g_full`.
- `Status_VisionRange` (StatType 0x2F): `OnAddUnit` reads `StatusReader.GetValue(0x2F)` as the vision radius.

Flow (sim side, all on every peer):

1. Spawn: `UnitSpawner.SetVision` 0xCAE388 / `BuildingSpawner.SetVision` 0xCA7974 / `EntityLifecycleHelper.AddBuildingVision`
   0xC97A44 -> `RuntimeMapData` -> `VisionDataHandler.OnAddUnit` 0xBEBD9C / `OnAddBuilding` 0xBEC82C:
   radius = Status 0x2F; `GetVisiblePositions(pos, r)` 0xBF0330 (line-of-sight: `IsPathVisible` 0xBF1104,
   `IsVisible(pos,dir)` 0xBF123C); new `VisionArea`; `_visions[guid] = area`. If faction 1 or 2 (player side):
   add guid to `_playerVisions`, `_playerVisionStacks[tile]++`, tiles into 0x38 and 0x40.
   Then `RuntimeMapData.AddArea` 0xBE6CF0 -> `AreaDataHandler.AddArea` + `CombatDataHandler.OnAddVision` 0xBD0CD4
   (`FactionAggroData.AddArea`): the vision area IS the combat/aggro area.
2. Move: `MoveApplier.RefreshUnitVision` 0xABDD98: `GetVision(unit)` (slot 0), `GetOnMoveUnitDeltas` (slot 2)
   -> `RuntimeMapData.RefreshArea` 0xBEDCF8 -> `AreaDataHandler.RefreshArea` + `VisionDataHandler.ApplyVisionRefresh`
   0xBEDEA8 + `CombatDataHandler.ApplyVisionRefresh` 0xBD1788. If faction 1/2: `RawMapData.SetVisited` for every
   added tile. `ApplyVisionRefresh`: player side -> stacks +/-1, tile into 0x38 and 0x40; other factions -> tile into 0x40 only.
3. Vision range change: `ComponentTickHandler.ChangeUnitVision` 0xA98C84, `StatusApplier.CalculateSideEffect` 0xABFBB0
   (`ChangeUnitVisionRange`, slot 3) -> same path, also `SetVisited`.
4. Unit changes side: `EntityLifecycleHelper.MarkTransferredUnitVisionVisited` 0xC97EC0 -> `SetVisited`.
5. Temporary reveal: `TemporaryVisionRevealApplier.Apply` 0xAC78FC -> `RuntimeMapData.AddTemporaryPlayerVision` 0xBEE550
   -> `VisionDataHandler.AddTemporaryPlayerVision` 0xBEE568 (`_temporaryPlayerVisions[pos] = ticks`, stacks++).
   `TickTemporaryPlayerVision` 0xBEEB54 counts down; at expiry stacks--, and a tile with 0 stacks and NOT visited goes
   into 0x68 (fog comes back). Temporary vision does NOT call SetVisited.

View side (presenter, class 1; one `FogOfWarPresenter` per map; `_targetMap 0x20`, `_mapData IMapDataReader 0x28`,
`_runtimeData IRuntimeDataReader 0x30`; view at `+0x10`). `GetUpdateMethods` 0xC2A94C (resolved via script.json):

| UpdateType | value | handler |
|---|---|---|
| LogicalRefesh | 3 | SetDayNight 0xC2C1F0 |
| RefreshWorld | 22 | RefreshFogDomain 0xC2ABFC |
| LoadVision | 24 | SetDefaultVision 0xC2B09C |
| RefreshVision | 25 | RefreshVision 0xC2B9D4 |
| RefreshOnCreepRemove | 30 | RefreshImplementedArea 0xC2C3DC |
| HandleFog | 1147 | SetFogActivate 0xC2C2E0 (cheat on/off) |

- `SetDefaultVision(this, IViewUpdateData)` 0xC2B09C (the data argument is not read): `view.ResetFog` (all tiles
  -> fog domain, not visited) ; for each `IMapDataReader.GetVisited()` (slot 0; local index, + `GetMaxSize` when
  `_targetMap == 1`) -> `view.LoadVisible(pos)` ; for each `Vision.GetPlayerVisions()` (slot 6, read only: union of
  all `_playerVisions` areas + temporary tiles) -> `view.SetVisible(pos)` ; `view.ApplyFogBatch`.
- `RefreshVision(this, IViewUpdateData)` 0xC2B9D4: `(add, remove) = Vision.GetPlayerVisionDeltas()` (slot 4).
  This call is DESTRUCTIVE: it drains 0x38 and removes "add" tiles from 0x68. For each tile in 0x38:
  stack > 0 -> "add", stack == 0 -> "remove". Note: every tile whose stack changed is re-sent, even if it stays > 0.
  For "remove": if `ConsumeTemporaryVisionRestoreFog(pos)` (slot 10) and NOT `IsPositionKnown(pos)` (IRuntimeDataReader
  slot 21) -> `view.SetNotVisited`, else `view.SetHalfVisible`. For "add": `view.SetVisible`. Then `ApplyFogBatch`.
- `FogOfWarView` leaf methods, all `void f(FogOfWarView* this, Vector2Int pos)`, pos packed in x1 (x low 32, y high 32),
  world fog coordinates; each does bounds check + `GetConvertedPosition` + one `FogMaskController` call:
  `SetVisible` 0xC2B838 -> `FogMaskController.SetVisible` 0xC29DFC ; `SetHalfVisible` 0xC2C054 and `LoadVisible`
  0xC2B69C -> `FogMaskController.SetHalfVisible` 0xC29EA4 ; `SetNotVisited` 0xC2BEB8 -> `FogMaskController.SetNotVisited`
  0xC29F4C. `ApplyFogBatch` 0xC2AFD8 uploads dirty pixels. `ResetFog` 0xC2B51C -> `ResetToFogDomain` 0xC29C84.
- `FogMaskController` holds two textures (`_notVisitedMaskTexture`, `_notVisibleMaskTexture`) + bool arrays.
  `FogRuleLayerRenderer` + `FogRuleBakeData` (atlas/lookup textures, resources `Fog/Baked/NotVisitedFogRuleData`,
  `NotVisibleFogRuleData`) draw them with shader `DS/URP/FogRuleOverlay`. `FogOfWarOverlayComponent` is the
  MonoBehaviour holding the renderer. `FogDomainHelper` only computes which tiles belong to the fog domain
  (dungeon rooms / implemented area). None of these know about units.

Three render states per tile: Visible, HalfVisible (explored, not seen now), NotVisited (black).

## 2. Who reads revealed / visited / visible state (verdict sim / view)

| Reader | Address | Reads | Verdict |
|---|---|---|---|
| CombatDataHandler.OnAddVision / ApplyVisionRefresh (FactionAggroData) | 0xBD0CD4 / 0xBD1788 | VisionArea positions | SIM (aggro / combat area) |
| AreaDataHandler.AddArea / RefreshArea (via RuntimeMapData.AddArea 0xBE6CF0 / RefreshArea 0xBEDCF8) | - | VisionArea | SIM (area enter/exit; details not traced) |
| InMapDataInterface.GetCombatableArea | 0xFC868C | GetVision(unit).Positions (+ dungeon rooms) | SIM (combat targeting) |
| RawMapData.SetVisited callers (MoveApplier, spawners, StatusApplier, ComponentTickHandler, EntityLifecycleHelper) | see section 1 | write visited | SIM (writer) |
| RawMapData.Serialize | 0xC50CFC | _visitedPositions | SIM / save (hashed by g_full) |
| VisionDataHandler.TickTemporaryPlayerVision | 0xBEEB54 | RawMapData.IsVisited | SIM-side bookkeeping, only feeds the view set 0x68 |
| CommandCheck.IsKnown / RunCheck.IsKnown | 0xC063F8 / 0xC0D128 | IInMapDataSupplier.IsVisited (66) + IsEntityKnownForInteraction (72) | INPUT (local, at command issue). ORD_NOTES: the attack quick action fails IsKnown on unknown targets. Whether replay re-runs it on peers: UNPROVEN |
| CancelMode / DemolitionMode / GatheringMode / PickUpMode .LeftClickInteraction, DragSelection, DefaultPicker, DefaultSelect.SelectNamed, CursorUtility.IsCursorDefault | Control.* | IsVisited (66) / IsEntityKnownForInteraction (72) | INPUT (local) |
| EntityUtility.GetRaycastEntities -> IsEntityHiddenByKnowledge | 0xFA5714 / 0xFA6898 | slot 72 | INPUT (mouse pick) |
| InMapDataInterface.IsEntityKnownForInteraction | 0xFC7544 | player unit -> true; enemy (faction 3) or subfaction 2 -> IsEntityInPlayerVision (= VisionDataHandler.IsVisible = set 0x48); other -> IsEntityInVisitedArea (RawMapData visited) | used by INPUT only (callers above) |
| TaskGeneratorContainer.GetRegisteredEntityGuids (+ lambda b__21_1, slot 70 IsEntityInVisited) | 0xD30C88 / 0xD31868 | visited | VIEW: only caller is UI.ModeObjectMarkerPresenter (markers). Note the getter can call RemoveTaskByTarget (not fog related; not traced) |
| ModeObjectMarkerPresenter.Build*DesiredVisuals | 0xE9B7FC... | IsEntityInVisited (70) | VIEW |
| EntityInteractOutlinerPresenter.ShouldHideSkillOutlineTarget | 0xE1D18C | IsEntityInPlayerVision (71) | VIEW |
| GridHoverPresenter.RefreshMouseGridPos / IsRoomKnown, HousingRoomFeedbackPresenter.IsRoomKnown | 0xF09230 / 0xF09CCC / 0xC2FE10 | IsPositionKnown (21) = visited and not base-unknown | VIEW (hover tooltip) |
| FogOfWarPresenter.* | see section 1 | all | VIEW |
| EntityObjectPresenter.IsVisible / OnRefreshHideVision / OnEntityPositionChanged / OnRefreshWorld | 0xE1E3A8 / 0xE1FD90 / 0xE1E8C4 / 0xE1F89C | Vision.IsVisible (8), GetHideVisionDeltas (5), GetPlayerVisions (6) | VIEW (hides enemies) |
| VisitMapProvider.GetValue | 0xB8EACC | map-level flag "current map == target MapType", saved bool | SIM (story/observer) but NOT tile fog; unrelated |
| IInMapDataSupplier.IsInPlayerVision (slot 67) | 0xFC6B94 | set 0x48 | no interface caller found |

Not found anywhere: AI aggro by fog, pathing limited to explored tiles, loot by fog, quests by tile fog.
No minimap reads fog (the only "Minimap" classes are the Edgar dungeon generator).

Important oddity: the "currently visible" set `_visibleVisionCaches` (0x48) is updated ONLY by `GetHideVisionDeltas`,
and that is called ONLY by the view (`EntityObjectPresenter.OnRefreshHideVision`). Same for the drain of 0x38/0x68 by
`GetPlayerVisionDeltas` (FogOfWarPresenter.RefreshVision). So vanilla already lets the view drive this state. It is not
in the save. Rule for us: hooks must still call these getters exactly as vanilla does (call the original presenter),
so this state stays the same on all peers.

Verdict: the sim reads VisionArea and the shared visited bitmap. A per-player fog must NOT edit them, nor
`_playerVisionStacks`, `_visibleVisionCaches`, `IsVisited`, `IsVisible`, `IsEntityKnownForInteraction`.
Use a separate view-only mask.

## 3. Hook points for a per-player VIEW mask

All in `build/det_ranges.tsv` class 1 (view) unless noted.

| Method | Address | Signature (decompile) | Role |
|---|---|---|---|
| FogOfWarPresenter.RefreshVision | 0xC2B9D4 | `void (FogOfWarPresenter* this, IViewUpdateData* d)` | per-change refresh; wrap: rebuild my mask, call original |
| FogOfWarPresenter.SetDefaultVision | 0xC2B09C | `void (FogOfWarPresenter* this, IViewUpdateData* d)` (d unused) | full redraw at load; wrap: rebuild mask, cache `this`, call original |
| FogOfWarView.SetVisible | 0xC2B838 | `void (FogOfWarView* this, Vector2Int pos)` | leaf: tile seen now |
| FogOfWarView.SetHalfVisible | 0xC2C054 | same | leaf: explored, not seen |
| FogOfWarView.LoadVisible | 0xC2B69C | same | leaf: explored, at load |
| FogOfWarView.SetNotVisited | 0xC2BEB8 | same | leaf: black (call the original, no hook needed) |
| EntityObjectView.SetVisible | 0xE24640 | `void (EntityObjectView* this, Guid g /*x1,x2*/, bool visible)` | show/hide entity GameObject; 4 callers, all in EntityObjectPresenter |
| EntityObjectPresenter.IsVisible | 0xE1E3A8 | `bool (EntityObjectPresenter* this, IEntity* e)` | faction 1 -> true; subfaction 2 -> true; else Vision.IsVisible(pos) |

Reach the vision data from the presenter: `presenter+0x30` = RuntimeMapData (IRuntimeDataReader) ; `+0x48` =
VisionDataHandler (field `_vision`, dump.cs). `presenter+0x20` = MapType. `presenter+0x10` = FogOfWarView.

Enemies and objects inside fog: hidden by the VIEW only. `EntityObjectPresenter` calls
`EntityObjectView.SetVisible(guid, visible)`; visible comes from `Vision.IsVisible(tile)` (set 0x48). Player-side units
(faction 1, subfaction 2) are always shown. The sim does not hide anything; enemies keep fighting by VisionArea.

## 4. Per-unit data we can filter by guid

- `VisionDataHandler._visions` (0x20): `Dictionary<Guid, VisionArea>`. Key = unit guid (UnitEntity+0x24) or building
  guid (BuildingEntity+0x10). Entry layout expected like `ord_game.c entity_cell` (Dictionary<Guid,Vector2>, stride
  0x20): `int hash @0, int next @4, Guid key @8, value @0x18` -> VisionArea*. Entries array at dict+0x18, count at +0x20.
  Same stride for a pointer value: expected, UNPROVEN until read at runtime.
- `VisionArea.Positions` (0x20): `List<Vector2Int>`: items array at +0x10, size at +0x18, element i at items+0x20+8*i
  (x int, y int). World coordinates, same as the positions given to the FogOfWarView leaves (RefreshVision passes
  delta tiles from these lists unchanged). UNPROVEN for the dungeon map offset; check with a log on map 1.
- `VisionDataHandler._playerVisions` (0x28): `HashSet<Guid>` of player-side owners (units faction 1 or 2, and
  player-side buildings). HashSet entry layout (`int hash, int next, Guid`) stride 24: UNPROVEN.
- `_temporaryPlayerVisions` (0x50): `Dictionary<Vector2Int,int>`, not tied to a unit.

## 5. Recommended hook design

Option `mp_shared_fog` (host setting, sent with the session settings; default 0 = per player). When it is 1, every
hook calls the original at once (vanilla).

C state per map (index 0 = base, 1 = dungeon; key on `presenter+0x20`): `my_vis` bitmap (rebuilt) and
`my_seen` bitmap (grows), both on world fog coords, size from `GetMaxSize` x 2 to cover the dungeon offset.
Memory: a few hundred kB max.

1. `h_refresh_vision(P, d)` and `h_set_default_vision(P, d)`:
   - cache P per map (for forced redraws);
   - `vh = AT(AT(P,0x30,Obj),0x48,Obj)`; clear `my_vis`; walk `_visions` entries: if the guid is in `_playerVisions`
     (or simpler: VisionArea.Owner is a player-side unit) AND `mp_unit_owned_by_me(guid)`, set `my_vis` for each
     tile of `Positions`. Add `_temporaryPlayerVisions` keys too (temporary reveals stay shared; see decisions).
     Set `my_seen |= my_vis`;
   - set `g_fog_ctx = map`; call the original; clear `g_fog_ctx`.
   The original still calls `GetPlayerVisionDeltas` / `ConsumeTemporaryVisionRestoreFog` / `GetPlayerVisions` exactly as
   vanilla, so the shared handler state stays identical on every peer.
2. `h_fog_set_visible(V, pos)`: if `g_fog_ctx` is unset or shared fog -> original. Else `my_vis(pos)` -> original;
   `my_seen(pos)` -> `o_set_half_visible(V,pos)`; else `SetNotVisited(V,pos)` (call address 0xC2BEB8 directly).
3. `h_fog_set_half(V, pos)` and `h_fog_load_visible(V, pos)`: `my_seen(pos)` -> original, else `SetNotVisited`.
   (`my_vis` cannot be true here for half: half only comes when the shared stack is 0.)
   Why this is enough: RefreshVision re-sends every tile whose stack changed, also when it stays > 0. So when my unit
   enters or leaves a tile a teammate also sees, that tile comes back through a leaf and is decided again by my mask.
4. `h_entity_set_visible(EV, guid_lo, guid_hi, vis)`: shared fog or `vis == 0` -> original. If the guid is a
   player-side unit (in `_playerVisions` of the current map, or known to dsnet ownership) -> original. Else find the
   tile (`entity_cell`, ord_game.c 504, make it non-static; note it floors, the game uses banker's round of x-0.5:
   same for tile centres) and pass `vis = my_vis(tile)`. Same re-decide logic: `GetHideVisionDeltas` re-sends every
   tile touched by any vision change (0x40 gets all player and enemy deltas).
5. Forced full redraw: on ownership change (claim / release / assign), on the option toggle, and on join, call
   `o_set_default_vision(P, NULL)` for each cached presenter on the main thread (same as UpdateType 24: ResetFog,
   reload visited, reload visions; our hooks filter). Entities: also needs a pass; simplest is to post
   UpdateType RefreshWorld 22 (UNPROVEN that it re-runs entity visibility) or keep a list of hidden guids and
   re-run step 4 on them.

Hook count: 6 stubs (RefreshVision, SetDefaultVision, FogOfWarView.SetVisible, SetHalfVisible, LoadVisible,
EntityObjectView.SetVisible). DETERMINISM_NOTES says about 62 of 80 used (date of note); current count UNPROVEN, check
`rig_hook` total before adding. Style: `rig_hook(VA, h, &o, "name")` as in `mp_game.c install_hooks`.

## 6. Risks

- Determinism: none expected if the rules hold (only view leaves change; shared getters still called by the original
  presenters). Proof needed: one 2-player soak with fog Off, full-state check must stay green.
- Interaction leak (UNPROVEN impact): `IsEntityKnownForInteraction`, `CommandCheck.IsKnown` use SHARED vision/visited.
  A player can hover/click/attack an enemy that a teammate sees but that is hidden in his own view, and the hover
  tooltip (`GridHoverPresenter`) works on tiles a teammate explored. Do not hook these in v1: if the order replay
  runs `CommandCheck` on every peer, a per-player answer would desync. Hooking only the local mouse pick
  (`EntityUtility.IsEntityHiddenByKnowledge` 0xFA6898) is possible later, after checking it never runs in a replay.
- Markers (`ModeObjectMarkerPresenter`) and skill outlines use shared state: small leak.
- Persistence: `my_seen` is not in the game save. Guests have no save. After a reload, per-player "explored" is lost.
- Unowned units, faction-2 units, buildings and temporary reveals have no player owner. Need a rule (see decisions).
- Struct layouts of Dictionary/HashSet entries are expected, not proven. Read and log once (count must match).
- Dungeon map (MapType 1) coordinates use an offset (`GetMaxSize`). Check one tile on the dungeon map.
- Performance: mask rebuild per RefreshVision = units x ~(2r+1)^2 tiles; small. The entity hook does a linear
  `entity_cell` scan per call; OnRefreshHideVision can call it many times per frame. Cache positions per frame if slow.

## 7. Decisions for the owner (experience tradeoffs, ask before building)

1. Explored area after a reload: (a) shared again (everything any player explored), or (b) per player, saved by the
   mod in its own file next to the world save (host holds it and sends it to guests at join; more work).
2. Teammates' units in my fog: shown (vanilla rule for player units) or hidden.
3. Vision of base buildings, unowned units, faction-2 units, temporary reveals: shared by all, or only the owner.
4. Accept the interaction leak in v1 (can click enemies a teammate sees), or block it (more hooks, desync check needed).
