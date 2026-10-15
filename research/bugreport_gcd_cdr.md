# Cooldown Reduction does not shorten the global cooldown (the adjusted value is computed, then thrown away)

**Game:** Dungeon Settlers, DS_B.0.4.29 (Steam build 25846389), macOS arm64 build. All code addresses below are from this build.

## Summary

Cooldown Reduction makes a skill's own cooldown shorter, but it does not change the global cooldown that starts after every skill. That global cooldown is always the base 5 seconds from `GAMESYSTEM_Combat_GlobalCoolDown`, whatever the unit's Cooldown Reduction is.

The current build looks like it was meant to handle this. `CooldownCondition` calls the new `SkillCooldownCalculator.GetAdjustedGlobalCooldown`, which divides the 5 s by (1 + Cooldown Reduction / 100). It logs `[ExecutionCondition] Adjusted Global Cooldown: {0}` and puts that value in a `GlobalCooldownApplyData`. But `TickEventApplier.Apply(GlobalCooldownApplyData)` then calls the **parameterless** `SkillComponent.SetGlobalCooldown()`. That method never reads `data.Amount`. It writes the unadjusted `_globalCooldownValue` (5 s). The overload that takes a value, `SetGlobalCooldown(float duration)`, exists but nothing in the build calls it.

## Steps to reproduce

1. Take a combat unit with active skills, with "Auto Skill" on or used by hand.
2. Note the time between two different skills that are both off their own cooldown. That gap is the global cooldown: about 5 s. The skill quick slots show the global cooldown sweep.
3. Give the unit a large Cooldown Reduction. Any of these works:
   - **Skill Cooldown Potion**: +30% Cooldown Reduction for 180 s (`SKILL_SkillCooldownPotion` FloatValues [180, 30]).
   - **High-grade Cooldown Reduction Inscription**: +30 (`CooldownReduction_30`).
   - **Swordsman's Resolve** passive: +12. **Goddess' Blessing** (`AFFECTER_BlessSkillCooldown`): +10. Will Power also adds +0.3 per point.
4. Repeat step 2. Also watch `Player.log`.

**Expected:** the gap between two skills gets shorter in the same way as skill cooldowns. With +30% it should be 5 / 1.30 = 3.85 s.
**Actual:** the gap stays at 5 s. Skill cooldowns do get shorter. `Player.log` prints `[ExecutionCondition] Adjusted Global Cooldown: 3.846154` (or similar), but the global cooldown that is applied is 5.

| Cooldown Reduction | Expected global cooldown (5 / (1 + CDR/100)) | Actual |
|---|---|---|
| 0% | 5.00 s | 5.00 s |
| 30% | 3.85 s | 5.00 s |
| 50% | 3.33 s | 5.00 s |
| 100% | 2.50 s | 5.00 s |

*Not tested in a running game. Everything in this report comes from reading the shipped code and data of build 25846389, not from a timed play session. The `Player.log` line comes from the code (a `Debug.Log` call with that format string). I did not capture it from a live run.*

## What the game tells the player

- Cooldown Reduction (`TEXTKEY_STATUS_CooldownReduction_DESC`): *"Increases the rate at which skill cooldowns count down. Higher values make cooldowns recover faster."*
- Will Power (`TEXTKEY_STATUS_WillPower_DESC`): *"Higher values improve Max Energy, Magic Attack Power, Cooldown Reduction, ..."*
- No English text key mentions a global cooldown. Players cannot tell from the text that Cooldown Reduction leaves it alone. Your own code computes and logs an "Adjusted Global Cooldown", so the intent seems to be that it should apply.

## Technical cause

**Skill cooldown: works.**
- `CooldownCondition.<OnSuccessCondition>d__2.MoveNext` (0x8A4FE4), state 0: `cooldown = SkillTableData.Cooldown / (1 + StatusReader.GetValue(CooldownReduction = 28) * 0.01)`. Skills with `IsDelayAdjustedByAttackSpeed` divide by AttackSpeed (24) instead. The same formula is in `SkillCooldownCalculator.GetAdjustedCooldown` (0x8A5408).
- It builds `CooldownApplyData(adjusted)`. `TickEventApplier.Apply(CooldownApplyData)` (0xAC7200) passes `data.Amount` to `SkillComponent.AddCooldown(key, value)` (0xDDB0EC). The adjusted value is used.

**Global cooldown: computed, then dropped.**
- Same `MoveNext`, state 1: `SkillCooldownCalculator.GetAdjustedGlobalCooldown(statusReader)` (0x8A54B8) reads `GAMESYSTEM_Combat_GlobalCoolDown` (FloatValues [5.0]) and returns `5 / (1 + CDR * 0.01)`. That result is logged and stored in `GlobalCooldownApplyData.Amount`.
- `TickEventApplier.Apply(GlobalCooldownApplyData)` (0xAC7154):
  ```
  if (data.Amount > 0) skill.SetGlobalCooldown();            // tail call to 0xDDBA18, Amount discarded
  else if (data.Amount < 0) skill.SubtractGlobalCooldown(-data.Amount);
  ```
  At 0xAC71B0 the code loads `data.Amount` into `s0` only for the `> 0` test. It then branches to 0xDDBA18.
- `SkillComponent.SetGlobalCooldown()` (0xDDBA18) is four instructions: load `_globalCooldownValue` (+0x68), and if it is not smaller than `_globalCooldown` (+0x60), store it into `_globalCooldown` and `_globalCooldownDuration` (+0x60/+0x64). `SkillComponent.Init` sets `_globalCooldownValue` from `SystemVar.Combat.GlobalCoolDown` (5 s). It reads no stat.
- `SkillComponent.SetGlobalCooldown(float duration)` (0xDDBA30) does the same with the value you pass in. A scan of every direct `b`/`bl` in the binary found no caller. The only caller of the parameterless version is 0xAC71CC (the applier above).
- Equip-change cooldown (`SetChangeEquipCooldown`, `_changeEquipCooldownValue` +0x6C, `GAMESYSTEM_Combat_ChangeEquipCoolDown`) is a separate path. This report does not cover it.

## Suggested fix

In `TickEventApplier.Apply(GlobalCooldownApplyData data)`, pass the value that is already computed:

```csharp
if (data.Amount > 0f)
    skillComponent.SetGlobalCooldown(data.Amount);   // make the float overload public (or internal)
else if (data.Amount < 0f)
    skillComponent.SubtractGlobalCooldown(-data.Amount);
```

There is no new formula. It is the same divide that skill cooldowns already use. The fix writes the shorter value into the same fields (+0x60/+0x64). The skill slot UI (`SkillSlotUI.SetGlobalCooldown(cooldown, duration)`) probably reads those fields, so it should show the shorter value too. *(I did not trace the UI path.)*

Check one more thing: is the parameterless `SetGlobalCooldown()` still needed anywhere, for example by multiplayer sync or by a save load? In this build nothing else calls it.
