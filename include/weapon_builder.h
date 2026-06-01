#pragma once
#include "items.h"

// Build a fresh voxel mesh for a weapon. Caller owns the returned volume.
// Rare adds a small accent (blade tip, shield boss, etc.); Legendary adds
// glowing seams, gem-like cluster, or rune highlights derived from the
// accent colour.
VoxelVolume* buildWeaponVolume(WeaponType type, ItemRarity rarity,
                               Voxel primary, Voxel accent);

// Swap the rig's main-hand and off-hand weapon meshes. Pass nullptr for an
// empty hand. Builds fresh VoxelVolume copies and transfers ownership to
// the rig's CharacterNodes.
void applyWeaponsToRig(class BipedalRig& rig,
                       const WeaponItem* mainHand,
                       const WeaponItem* offHand);

// Raw-parameter variant of applyWeaponsToRig — pass WeaponType::None to
// leave the hand empty. Used by the network sync path.
void applyWeaponsToRig(class BipedalRig& rig,
                       WeaponType mainType, ItemRarity mainRarity,
                       Voxel mainPri, Voxel mainAcc,
                       WeaponType offType,  ItemRarity offRarity,
                       Voxel offPri,  Voxel offAcc);
