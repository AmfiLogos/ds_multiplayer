// Copy of ds_custommodes units/mu_look.h at 095d27a, for the multiplayer mod's Bring Units card window (no link to Units Laboratory).
// mu_look.h - a unit's two looks, drawn by the game (shared by the Unit Builder, My Units and the Wiki Build Gallery):
//   the avatar: its portrait picture (UnitVisualDatabase portrait sprite, as on the game's unit cards);
//   the sprite: the whole in-game character, race body + hair + face + worn gear, made by the game's own compositor
//     (Refactor.UnitVisual: SetProfileData, RefreshAllEquipment, PaintAll) on a hidden copy of the race's unit prefab
//     (ResourceLoader.LoadUnitObject "UNIT_PlayerUnit_<visual type>"), with the profile row of the portrait
//     (UnitProfileTable: Portrait == the portrait key).
// Main thread only. Keys are game keys: race AFFECTER_Elf, portrait ElfStandardFemale_002, gear ITEM_ArcronMace.
#pragma once
#include "dsl_sdk.h"
#include "fe_json.h"

#define MU_LOOK_GEAR 8
typedef struct mu_look {
    char race[48];                         /* AFFECTER_<race> (used when the portrait is empty / unknown: the race's first face) */
    char portrait[64];                     /* portrait key, or "" */
    char gear[MU_LOOK_GEAR][64];           /* worn item keys (weapon, off-hand, helmet / hood, armour; others are ignored) */
    int ngear;
    int hide_head;                         /* 1 = helmet worn but not drawn (the save's HideHeadEquipment) */
} mu_look;

/* the portrait, square s (one kit panel). NULL = no sprite for it (nothing drawn). */
DSLObj mu_look_avatar(DSLObj parent, float x, float y, float s, const mu_look *u);
/* the in-game character in a w x h box, kept square (one kit panel: the 30 x 30 px square around the character on the
   game's 64 x 40 canvas). Each look is made once and kept (48 most recent). NULL = the game could not make it (logged
   once per reason). */
DSLObj mu_look_sprite(DSLObj parent, float x, float y, float w, float h, const mu_look *u);
/* a random recruit: a face the game's recruit pool rolls (by its weight) and a weapon, armour, often a helmet or hood
   and sometimes a shield the player can own (the weapon's material when the game has the piece). -1 = the game's
   tables are not there (u is then empty). */
int mu_look_random(mu_look *u);
/* fill from a wiki build JSON (race, portrait, eq, hideHead) */
void mu_look_from_build(const jv *b, mu_look *u);
