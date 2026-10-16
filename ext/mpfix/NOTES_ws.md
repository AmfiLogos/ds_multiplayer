# mpfix-ws: weapon slot host-only (v13 brief step 5), 2026-10-16

## What changed (branch mpfix-ws)
- 64d6c32: re-applies 0e8b819. The full-state check skips PlayerUnitsSaveData/QuickSlots (src/mp_game.c full_local).
- a6b34bf: quick-slot index orders go by skill key (src/ord_core.c/.h, src/ord_game.c, ord_test).
  - ToggleAutoSkill / SetQuickSlot / UnsetQuickSlot / MoveQuickSlot get the flag ORD_EF_QSLOT. The order carries the
    issuer's skill key at Index (and at TargetIndex for Move) and one flag `qreg`.
  - Each copy finds the key in its own quick slots (MainFlow +0xC8 -> +0x48 -> +0x20 -> Dictionary<Guid, QuickSlotData>
    -> string[]; read in place, offsets static_assert-checked in ord_test/offsets_manual.cpp) and runs the stock
    handler at that slot.
  - A copy without a slot for the key does only the skill part: Toggle = SkillComponent.ChangeAutoSkillData(key)
    (a flip, decompiled at 0xDDAF2C). Unset / Set lend slot 0 to the stock handler, then give slot 0 back its key.
  - `qreg`: the stock handlers decide "learn" / "forget" from IsRegistered (key in any slot). Set: AddSkill only when no
    slot holds the skill, and AddSkill = AddOrMoveToLast (0xDDA26C: it re-orders _activeSkills even for a known skill).
    So a 4-slot copy could learn again a skill that sits only in the host's 5th slot. The order carries the issuer's
    answer; each copy clears or lends one slot so its handler takes the same branch. Unset: same for "forget".
  - Move with one side missing on this copy: layout write only (no handler).
  - Wire version stays 2 (ui_teams.c builds a v2 ChangeMapRequested by hand). An older peer rejects only these 4 orders.
- Decompiles used: InputEventHandler.OnEvent Toggle 0xD97474, Set 0xD96708, Unset 0xD9789C, Move 0xD96040;
  UnitQuickSlotContainer GetSkillAt 0xCC4690 (throws for an index the copy does not have), AddQuickSlotAt 0xCC42E8,
  RemoveQuickSlotAt 0xCC4390.

## Proofs
- ord_test: 65 607 checks, 0 failures. New: skill-key round trip for the 4 events; encode / resolve cases (hint, moved
  key, receiver lacks the slot, no slots, empty key); plans for host 5 / guest 4 per event; a two-copy model (5-slot
  and 4-slot copy, stock handler semantics, random orders from both, captures applied late) over 300 games x 120 steps:
  learned skills (order included) and auto-use flags equal. Mutation check: removing `qreg` or the auto-only path makes
  the model fail (2071 / 4565 differing applies).
- mp_test, net, ui_test (530), mpmenu_test, trn_test, teams_test (437 097): all pass.
- Windows cross-build (win/build_win.sh, generated port, no win/ source change): built build/baselib.dll.
- weaponslot_pair.py (pair EF: E = host with the mod, F = stock guest), dylib build_ws/dsrig.dylib, full-state ON:

| run | args | minutes | ticks equal | desyncs | full checks ok / fail | EmergencyEscape auto host / guest |
|---|---|---|---|---|---|---|
| flip1 | --auto last --flip | 6.2 | 21 996, no diff | 0 / 0 | 2 / 0 | off / off (both units) |
| flip2 | --auto last --flip | 5.2 | 18 411, no diff | 0 / 0 | 1 / 0 | off / off |
| flip3 | --auto last --flip | 5.2 | 18 374, no diff | 0 / 0 | 1 / 0 | off / off |
| casts | --minutes 6 --cast-every 6 | 6.2 | 20 902, no diff | 0 / 0 | 2 / 0 | (106 weapon casts, 138 orders applied on both) |

  Guest log per toggle: `ToggleAutoSkillRequested by skill key 'SKILL_EmergencyEscape' ... here 4 slots -> plan 2`
  (auto-only, rc 0). Units equal at the paused tick, owner prefs unchanged, no copy left running.
  Before (lan_v12 dylib, 2026-10-15): same flip -> guest kept the flag ON -> full-state desync at step 10800.

## Not covered / risk for "scope client"
- Equip with a full bar (code-read, not run): stock ItemControlProcessor.ProcessEquip (0xD496D4) learns the weapon's
  skill only if it got a quick slot (OnAddExtraSkill -> TryRegisterQuickSlot, then IsRegistered gates AddSkill). The mod
  always puts it in slot 0. A stock guest whose 4 slots are all full (for example after the guest put its own skill
  into its slot 0 = the host's weapon slot) does not learn the new weapon's skill: SkillComponent differs -> desync.
  A normal weapon swap frees slot 0 first, so it should stay equal; not tested.
- Move with one side missing writes the slot without the stock view refresh: the stock guest's bar may show the old
  layout until it refreshes (UI only).
