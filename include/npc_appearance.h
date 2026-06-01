#pragma once
#include "voxel_model.h"
#include "npc.h"

// Paint a coherent themed clothing + weapon loadout onto an NPC's rig,
// derived deterministically from `seed` so server + every client see the
// same NPC wearing the same outfit. Style is consistent per-NPC:
//
//   - Villager: cloth tier, drab civilian palette, no weapon. Some slots
//                (helmet, shoulders, shoes) are randomly skipped so
//                villagers look like a mix of fully- and partially-clad
//                peasants instead of every one wearing the full kit.
//   - Enemy (bandit): leather tier, dark rough palette, random melee
//                or ranged weapon.
//   - Guard:    plate tier, town-livery palette, sword/axe + shield
//                (shield uses the same palette as the armour so the
//                whole outfit reads as one town's colours).
//
// All five clothing slots share a single primary/accent colour picked
// once per NPC, so the outfit always matches — no mismatched set of
// random colours that look thrown together.
void applyNpcThemedLoadout(class BipedalRig& rig, uint32_t seed, NPCType type);
