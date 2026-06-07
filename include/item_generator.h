#pragma once
#include "items.h"
#include <memory>
#include <cstdint>

// ---------------------------------------------------------------------------
// Procedural item generation
// ---------------------------------------------------------------------------
// Each generator is deterministic in the supplied `seed`: the same seed
// always produces the same item. Use a `std::random_device` value for an
// unpredictable result, or persist the seed if you want a unique-but-fixed
// item identity (e.g. for save files).

// `targetLevel` shapes the item's level requirement around the player's
// current level — see implementation for the rarity-dependent spread.
// Common items often roll a few levels under target (so they feel
// outdated quickly); Legendaries sit at or just above target. Stats also
// scale with the resulting level.

// Generate a randomised clothing piece for the given tier+slot.
std::unique_ptr<ClothingItem> generateRandomClothing(uint32_t seed,
                                                     ClothingTier tier,
                                                     EquipSlot slot,
                                                     int targetLevel = 1);

// Generate a randomised clothing piece with random tier AND slot.
std::unique_ptr<ClothingItem> generateRandomClothing(uint32_t seed,
                                                     int targetLevel = 1);

// Generate a randomised weapon of the given type.
std::unique_ptr<WeaponItem> generateRandomWeapon(uint32_t seed, WeaponType type,
                                                 int targetLevel = 1);

// Generate a randomised weapon with random type.
std::unique_ptr<WeaponItem> generateRandomWeapon(uint32_t seed,
                                                 int targetLevel = 1);

// Generate any random equippable item (clothing or weapon).
std::unique_ptr<Item> generateRandomItem(uint32_t seed, int targetLevel = 1);

// Generate a guaranteed-Legendary item (re-rolls until a legendary turns up).
// Used for dungeon-boss drops.
std::unique_ptr<Item> generateLegendaryItem(uint32_t seed, int targetLevel = 1);

// Roll a single themed-set clothing piece. The generator picks a random
// set from a fixed table (Wolfblood / Frostweave / Dragonscale / ...),
// applies its signature palette + tier across whatever slot is picked,
// stamps `setName` on the item, and biases the rarity upward (sets are
// trophies — they roll Rare or Legendary far more often than Common).
std::unique_ptr<ClothingItem> generateSetClothing(uint32_t seed,
                                                  int targetLevel = 1);
