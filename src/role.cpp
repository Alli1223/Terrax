#include "role.h"

// Tables for the three player roles. Kept here (not as call-site switches) so
// new roles or rebalancing only touch this file. Each switch carries a default
// so adding a role can't silently fall through to garbage.

const char* roleName(PlayerRole r) {
    switch (r) {
        case PlayerRole::Tank:   return "Tank";
        case PlayerRole::Healer: return "Healer";
        case PlayerRole::DPS:
        default:                 return "DPS";
    }
}

RoleStats roleBaseStats(PlayerRole r) {
    switch (r) {
        //                      maxHpL1  hp/lvl  defMult  apMult
        case PlayerRole::Tank:   return { 180.0f, 22.0f,  1.45f,   0.85f };
        case PlayerRole::Healer: return { 120.0f, 13.0f,  0.90f,   1.25f };
        case PlayerRole::DPS:
        default:                 return { 110.0f, 12.0f,  1.00f,   1.30f };
    }
}

float roleHeightScale(PlayerRole r) {
    switch (r) {
        case PlayerRole::Tank:   return 1.22f;   // broad and tall
        case PlayerRole::Healer: return 1.00f;
        case PlayerRole::DPS:
        default:                 return 0.92f;   // lean and a touch shorter
    }
}

float roleWeightScale(PlayerRole r) {
    switch (r) {
        case PlayerRole::Tank:   return 1.25f;
        case PlayerRole::Healer: return 1.00f;
        case PlayerRole::DPS:
        default:                 return 0.90f;
    }
}

ClothingTier roleMaxTier(PlayerRole r) {
    switch (r) {
        case PlayerRole::Tank:   return ClothingTier::Plate;     // anything
        case PlayerRole::Healer: return ClothingTier::Cloth;     // cloth only
        case PlayerRole::DPS:
        default:                 return ClothingTier::Leather;   // leather + cloth
    }
}

bool canEquipRole(const Item* item, PlayerRole role) {
    if (!item) return false;
    if (item->getKind() == ItemKind::Clothing)
        return roleCanWearTier(role, static_cast<const ClothingItem*>(item)->getTier());
    return true;   // weapons are not tier-gated by role
}
