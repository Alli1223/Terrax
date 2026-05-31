#pragma once
#include "items.h"

// Paint a single clothing item onto the rig's existing voxel volumes. The
// caller is expected to have already called `rig.resetBaseBody()` +
// `rig.applyCustomization()` so the body is in a known state before items
// layer on top. Order matters — paint in slot order (helmet, shoulders,
// chest, legs, feet) for stable layering. After the base silhouette is
// painted, a rarity pass adds extra accents (small for Rare, runes/gems
// and glowing seams for Legendary).
void paintClothingOnto(class BipedalRig& rig, const ClothingItem& item);

// Raw-parameter variant of paintClothingOnto. Used by the network code,
// which has tier + rarity + colours but no full Item objects.
void paintClothingOnto(class BipedalRig& rig, ClothingTier tier,
                       ItemRarity rarity, EquipSlot slot,
                       Voxel primary, Voxel accent);
