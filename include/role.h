#pragma once
#include "items.h"      // ClothingTier, Item, ItemKind, ClothingItem
#include <cstdint>

// ---------------------------------------------------------------------------
// PlayerRole — the combat archetype chosen at character creation.
// ---------------------------------------------------------------------------
// Drives body size (Tank larger, DPS smaller), which armour tiers can be worn,
// base combat stats, and (Phase 2+) the starting abilities and resource type.
// The value is append-only: it is serialised on the wire by reusing the legacy
// PlayerModelHeader::armorType field, so existing numbers must never change.
enum class PlayerRole : uint8_t {
    Tank   = 0,
    DPS    = 1,
    Healer = 2,
};

// Base combat scalars for a role. maxHp scales up with level via roleMaxHp();
// the multipliers feed the existing damage / heal / defence math so roles feel
// distinct without a full character sheet.
struct RoleStats {
    float maxHpAtL1;          // max HP at level 1 (same 0..100-ish scale as today)
    float hpPerLevel;         // added per level above 1
    float defenseMult;        // >1 = tankier; incoming damage is divided by this
    float abilityPowerMult;   // scales outgoing ability damage and healing
};

const char*  roleName(PlayerRole r);
RoleStats    roleBaseStats(PlayerRole r);

// Body proportions per role — applied to the rig's heightScale (overall) and
// weightScale (torso width).
float roleHeightScale(PlayerRole r);
float roleWeightScale(PlayerRole r);

// Heaviest armour tier a role may wear; roles can also wear anything lighter.
ClothingTier roleMaxTier(PlayerRole r);

// "Own tier and lighter": ClothingTier is ordered Cloth < Leather < Plate.
inline bool roleCanWearTier(PlayerRole r, ClothingTier t) {
    return (int)t <= (int)roleMaxTier(r);
}

// Effective max HP at a given level for a role.
inline float roleMaxHp(PlayerRole r, int level) {
    RoleStats s = roleBaseStats(r);
    return s.maxHpAtL1 + s.hpPerLevel * (float)(level - 1);
}

// True when `item` may be equipped by `role`: clothing is gated to the role's
// tier-and-lighter; weapons are not tier-gated here (hand/skill checks apply
// elsewhere).
bool canEquipRole(const Item* item, PlayerRole role);
