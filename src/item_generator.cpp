#include "item_generator.h"
#include <algorithm>
#include <random>
#include <string>

// ---------------------------------------------------------------------------
// Procedural item generation
// ---------------------------------------------------------------------------
// Palettes, name pools and helper functions are kept in an anonymous
// namespace so they don't leak symbols out of this translation unit.
//
// Generation is rarity-driven: every generator first rolls an ItemRarity
// (Common ~65%, Rare ~28%, Legendary ~7%) and uses that to pick from
// rarity-specific palette + name pools and to scale stats. Common items
// are drab and plainly named; Legendary items use jewel-tone palettes,
// grand adjectives, and may pick up an "of <Power>" suffix.

namespace {

struct Palette { Voxel primary; Voxel accent; };

// =============================================================================
// Palette pools — three tiers (Cloth / Leather / Plate) × three rarities.
// =============================================================================

// --- Cloth ----------------------------------------------------------------

const Palette CLOTH_COMMON[] = {
    { {130, 110,  90, 255}, { 85,  70,  55, 255} },   // dusty linen
    { {110, 105,  90, 255}, { 70,  65,  55, 255} },   // sackcloth
    { { 95,  90,  85, 255}, { 60,  55,  50, 255} },   // grey wool
    { {140, 120,  90, 255}, { 95,  80,  60, 255} },   // dirty beige
    { { 95, 115,  90, 255}, { 60,  80,  60, 255} },   // muddy green
    { {120, 100,  95, 255}, { 80,  65,  60, 255} },   // faded rose
};

const Palette CLOTH_RARE[] = {
    { { 70,  95, 175, 255}, {200, 175,  80, 255} },   // royal navy + gold
    { {130,  55, 130, 255}, { 90,  35,  90, 255} },   // royal purple
    { {175,  55,  55, 255}, {110,  35,  35, 255} },   // deep crimson
    { { 50, 115,  75, 255}, {200, 175,  80, 255} },   // forest + gold
    { {200, 195, 175, 255}, {150,  55,  55, 255} },   // cream + crimson trim
    { { 60,  85, 140, 255}, {185, 180, 165, 255} },   // dusk blue + silk
    { { 95,  60, 130, 255}, {180, 130,  60, 255} },   // amethyst + amber
};

const Palette CLOTH_LEGENDARY[] = {
    { {240, 235, 215, 255}, {255, 200,  60, 255} },   // ivory silk + sun gold
    { { 25,  30,  80, 255}, { 90, 200, 240, 255} },   // void blue + starlight
    { {165,  35,  85, 255}, {255, 215, 100, 255} },   // garnet + amber gold
    { { 80, 200, 180, 255}, {240, 240, 240, 255} },   // turquoise + pearl
    { { 35, 150,  90, 255}, {255, 215, 100, 255} },   // emerald + gold
    { {220, 220, 230, 255}, { 90, 160, 255, 255} },   // alabaster + sapphire glow
    { {200,  80,  40, 255}, {255, 200,  60, 255} },   // sunfire + gold
};

// --- Leather --------------------------------------------------------------

const Palette LEATHER_COMMON[] = {
    { {115,  80,  50, 255}, { 70,  45,  25, 255} },   // tan + brown
    { { 75,  50,  30, 255}, { 40,  25,  15, 255} },   // dark brown + black
    { { 95,  70,  50, 255}, { 60,  40,  25, 255} },   // weathered tan
    { { 90,  75,  60, 255}, { 55,  45,  35, 255} },   // grey-brown
    { { 60,  45,  30, 255}, { 35,  25,  15, 255} },   // peat brown
};

const Palette LEATHER_RARE[] = {
    { { 40,  35,  30, 255}, {180, 180, 190, 255} },   // black + silver studs
    { {130,  80,  40, 255}, {200, 165,  80, 255} },   // oiled tan + bronze
    { { 95,  35,  35, 255}, { 60,  20,  20, 255} },   // bloodied red leather
    { { 55,  85,  55, 255}, {180, 165,  90, 255} },   // hunter's green + tan
    { { 65,  50,  85, 255}, {180, 160, 100, 255} },   // dark violet + bronze
};

const Palette LEATHER_LEGENDARY[] = {
    { { 25,  60,  35, 255}, { 80, 230, 130, 255} },   // dragon hide + emerald glow
    { { 25,  25,  35, 255}, {180, 180, 255, 255} },   // void leather + starlight
    { {120,  20,  30, 255}, {255, 215, 100, 255} },   // blood red + gold
    { { 95,  60,  20, 255}, {255, 180,  60, 255} },   // ember + flame
    { { 35,  35,  60, 255}, { 90, 200, 240, 255} },   // midnight + sapphire
};

// --- Plate ----------------------------------------------------------------

const Palette PLATE_COMMON[] = {
    { {120, 125, 135, 255}, { 80,  85,  95, 255} },   // dull iron
    { {110, 100,  95, 255}, { 75,  65,  60, 255} },   // rusted iron
    { {145, 145, 150, 255}, { 95,  95, 100, 255} },   // scuffed steel
    { { 90,  90,  95, 255}, { 50,  50,  55, 255} },   // blackened iron
};

const Palette PLATE_RARE[] = {
    { {180, 185, 200, 255}, {110, 115, 130, 255} },   // polished steel
    { {200, 160,  80, 255}, {130, 100,  50, 255} },   // bronze
    { { 80,  85,  95, 255}, {180, 180, 195, 255} },   // gunmetal + silver
    { {165, 175, 190, 255}, { 70,  90, 145, 255} },   // steel + blue accents
    { {175, 145,  80, 255}, { 65,  45,  25, 255} },   // brass + dark
};

const Palette PLATE_LEGENDARY[] = {
    { {220, 235, 250, 255}, {100, 170, 240, 255} },   // mithril + sapphire glow
    { {255, 215,  90, 255}, {255, 150,  60, 255} },   // sun gold + flame
    { { 30,  30,  40, 255}, {220,  50,  50, 255} },   // obsidian + crimson rune
    { {230, 220, 230, 255}, {255, 130, 180, 255} },   // dawn silver + rose
    { {180, 220, 240, 255}, { 80, 220, 255, 255} },   // glacier + cyan glow
    { { 30,  70,  60, 255}, { 80, 230, 130, 255} },   // verdant + emerald
};

// =============================================================================
// Name pools — adjective and noun lists by rarity / slot / weapon type.
// =============================================================================

const char* CLOTH_ADJ_COMMON[]    = { "Worn", "Patched", "Simple", "Threadbare",
                                       "Faded", "Plain", "Frayed", "Coarse" };
const char* CLOTH_ADJ_RARE[]      = { "Embroidered", "Tailored", "Fine",
                                       "Gilded", "Refined", "Brocade",
                                       "Court", "Velvet" };
const char* CLOTH_ADJ_LEGENDARY[] = { "Silken", "Astral", "Sovereign",
                                       "Celestial", "Twilit", "Sunwoven",
                                       "Voidwoven", "Hallowed" };

const char* LEATHER_ADJ_COMMON[]    = { "Battered", "Cracked", "Tanned",
                                         "Roughspun", "Used", "Scuffed",
                                         "Stiff" };
const char* LEATHER_ADJ_RARE[]      = { "Studded", "Reinforced", "Hunter's",
                                         "Wayfarer's", "Ranger's", "Oiled",
                                         "Riveted", "Wyrm" };
const char* LEATHER_ADJ_LEGENDARY[] = { "Dragonhide", "Shadowbound", "Wyrmscale",
                                         "Bloodforged", "Stormhide",
                                         "Voidtouched", "Phoenix" };

const char* PLATE_ADJ_COMMON[]    = { "Dented", "Rusted", "Crude", "Iron",
                                       "Pitted", "Bent", "Plain" };
const char* PLATE_ADJ_RARE[]      = { "Knight's", "Burnished", "Ornate",
                                       "Steel", "Champion's", "Heavy",
                                       "Bronze", "Captain's" };
const char* PLATE_ADJ_LEGENDARY[] = { "Mithril", "Dragonsteel", "Stormforged",
                                       "Sunblest", "Eternal", "Godforged",
                                       "Skyforged", "Dawnsteel" };

// --- Themed armour sets ---------------------------------------------------
// Each set is a fixed (name, tier, palette) — every piece a generator
// rolls under that set inherits the palette, so a Wolfblood Cap and a
// Wolfblood Cuirass match at a glance even though they were rolled
// hours apart. Set pieces also skip the rarity weighting and roll only
// Rare/Legendary (commons would feel wrong on a themed piece).
struct ArmorSet {
    const char*  name;
    const char*  adjective;
    ClothingTier tier;
    Voxel        primary;
    Voxel        accent;
};

const ArmorSet KNOWN_SETS[] = {
    { "Wolfblood",   "Wolfblood",   ClothingTier::Leather,
        {115,  35,  30, 255}, {225, 180,  60, 255} },
    { "Frostweave",  "Frostweave",  ClothingTier::Cloth,
        {210, 230, 245, 255}, { 80, 160, 220, 255} },
    { "Dragonscale", "Dragonscale", ClothingTier::Plate,
        { 45,  70,  40, 255}, {255, 180,  60, 255} },
    { "Voidwoven",   "Voidwoven",   ClothingTier::Cloth,
        { 30,  30,  55, 255}, {145,  85, 225, 255} },
    { "Sunsteel",    "Sunsteel",    ClothingTier::Plate,
        {225, 200, 100, 255}, {255, 130,  40, 255} },
    { "Shadowsilk",  "Shadowsilk",  ClothingTier::Cloth,
        { 28,  28,  34, 255}, {185,  60, 225, 255} },
    { "Ironclad",    "Ironclad",    ClothingTier::Plate,
        {105, 110, 120, 255}, {200,  60,  60, 255} },
    { "Stagstride",  "Stagstride",  ClothingTier::Leather,
        {115,  82,  52, 255}, {225, 205, 130, 255} },
    { "Tideborn",    "Tideborn",    ClothingTier::Leather,
        { 40,  75, 110, 255}, {200, 230, 220, 255} },
    { "Emberforge",  "Emberforge",  ClothingTier::Plate,
        { 70,  35,  35, 255}, {255, 110,  45, 255} },
    { "Stormcaller", "Stormcaller", ClothingTier::Cloth,
        { 60,  80, 120, 255}, {180, 230, 255, 255} },   // storm-blue + lightning white
    { "Thornweave",  "Thornweave",  ClothingTier::Leather,
        { 48,  78,  50, 255}, {150, 110,  70, 255} },   // bramble green + bark
    { "Obsidian",    "Obsidian",    ClothingTier::Plate,
        { 24,  24,  30, 255}, {200,  50,  40, 255} },   // black glass + ember red
    { "Moonveil",    "Moonveil",    ClothingTier::Cloth,
        {200, 205, 230, 255}, {120, 150, 220, 255} },   // pale silver + moonlit blue
    { "Bloodforged", "Bloodforged", ClothingTier::Plate,
        { 90,  30,  30, 255}, {200, 180, 190, 255} },   // dark blood + pale steel
    { "Verdant",     "Verdant",     ClothingTier::Leather,
        { 60, 100,  55, 255}, {220, 200, 110, 255} },   // living green + gold
    { "Ashen",       "Ashen",       ClothingTier::Cloth,
        { 90,  88,  92, 255}, {235, 120,  60, 255} },   // grey ash + smouldering ember
    { "Aurelian",    "Aurelian",    ClothingTier::Plate,
        {215, 180,  90, 255}, {255, 245, 220, 255} },   // gilded gold + ivory
    { "Ravenfeather","Ravenfeather",ClothingTier::Leather,
        { 34,  32,  44, 255}, {120,  80, 170, 255} },   // black feather + violet sheen
    { "Glacial",     "Glacial",     ClothingTier::Cloth,
        {200, 228, 240, 255}, { 70, 150, 200, 255} },   // ice white + deep glacier blue
};

// Slot noun pools. Common slots have more "plain" nouns; rare/legendary
// pools lean toward grander terms.
const char* HELMET_NOUNS_COMMON[]    = { "Cap", "Hood", "Coif" };
const char* HELMET_NOUNS_RARE[]      = { "Helm", "Coif", "Crested Helm" };
const char* HELMET_NOUNS_LEGENDARY[] = { "Crown", "Greathelm", "Diadem", "Circlet" };

const char* SHOULDERS_NOUNS_COMMON[]    = { "Mantle", "Cape", "Wrap" };
const char* SHOULDERS_NOUNS_RARE[]      = { "Pauldrons", "Spaulders", "Mantle" };
const char* SHOULDERS_NOUNS_LEGENDARY[] = { "Pauldrons", "Mantle", "Shoulderplates" };

const char* CHEST_NOUNS_COMMON[]    = { "Tunic", "Vest", "Jerkin", "Shirt" };
const char* CHEST_NOUNS_RARE[]      = { "Cuirass", "Robe", "Jerkin", "Doublet" };
const char* CHEST_NOUNS_LEGENDARY[] = { "Cuirass", "Aegis", "Vestment",
                                         "Breastplate", "Hauberk" };

const char* LEGS_NOUNS_COMMON[]    = { "Trousers", "Breeches", "Pants" };
const char* LEGS_NOUNS_RARE[]      = { "Greaves", "Leggings", "Breeches" };
const char* LEGS_NOUNS_LEGENDARY[] = { "Greaves", "Legplates", "Cuisses" };

const char* FEET_NOUNS_COMMON[]    = { "Shoes", "Slippers", "Footwraps" };
const char* FEET_NOUNS_RARE[]      = { "Boots", "Greaves", "Sandals" };
const char* FEET_NOUNS_LEGENDARY[] = { "Sabatons", "Striders", "Treads" };

// Suffix pool — only applied to legendary items, gives them an evocative
// "of <Power>" trailing phrase. Sampled with ~60% probability.
const char* LEGENDARY_SUFFIXES[] = {
    "of the Eternal Flame", "of Dawn", "of the Void", "of Storms",
    "of Kings", "of the Tempest", "of the Sun", "of Shadows",
    "of the First Light", "of the Hollow Wyrm", "of Tides",
    "of the Frozen Star", "of the Burning Sky", "of Forgotten Realms",
};

// --- Weapon adjective pools per rarity ------------------------------------

const char* SWORD_ADJ_COMMON[]    = { "Iron", "Rusty", "Plain", "Old", "Soldier's" };
const char* SWORD_ADJ_RARE[]      = { "Steel", "Honed", "Dueling", "Silvered", "Knight's" };
const char* SWORD_ADJ_LEGENDARY[] = { "Mythril", "Sunsteel", "Voidblade", "Stormcleaver", "Dawnedge" };

const char* SHIELD_ADJ_COMMON[]    = { "Wooden", "Bucklered", "Round", "Worn" };
const char* SHIELD_ADJ_RARE[]      = { "Iron-banded", "Tower", "Heater", "Knight's" };
const char* SHIELD_ADJ_LEGENDARY[] = { "Aegis of the Sun", "Bulwark", "Skyguard", "Wardstone" };

const char* BOW_ADJ_COMMON[]    = { "Hunter's", "Yew", "Shortbow", "Roughwood" };
const char* BOW_ADJ_RARE[]      = { "Recurve", "Longbow", "Hornbow", "Ranger's" };
const char* BOW_ADJ_LEGENDARY[] = { "Stormwind", "Moonshadow", "Sunpiercer", "Heartseeker" };

const char* STAFF_ADJ_COMMON[]    = { "Apprentice's", "Oaken", "Carved", "Acolyte's" };
const char* STAFF_ADJ_RARE[]      = { "Mage's", "Druid's", "Sage's", "Runed" };
const char* STAFF_ADJ_LEGENDARY[] = { "Archon's", "Voidcaller", "Sunpillar", "Stormcrown" };

const char* AXE_ADJ_COMMON[]    = { "Hand", "Hatchet", "Crude", "Woodsman's" };
const char* AXE_ADJ_RARE[]      = { "Battle", "War", "Bearded", "Reaver's" };
const char* AXE_ADJ_LEGENDARY[] = { "Skullsplitter", "Worldcleaver", "Stormaxe", "Bloodmoon" };

template <typename T, size_t N>
constexpr int arrLen(T (&)[N]) { return (int)N; }

// =============================================================================
// Helpers — RNG-driven selection
// =============================================================================

Voxel perturbVoxel(std::mt19937& rng, Voxel c, int range) {
    auto clip = [](int v) { return (uint8_t)std::clamp(v, 0, 255); };
    int dr = std::uniform_int_distribution<int>(-range, range)(rng);
    int dg = std::uniform_int_distribution<int>(-range, range)(rng);
    int db = std::uniform_int_distribution<int>(-range, range)(rng);
    return { clip(c.r + dr), clip(c.g + dg), clip(c.b + db), 255 };
}

// Weighted rarity roll: 65% / 28% / 7% by default.
ItemRarity rollRarity(std::mt19937& rng) {
    int r = std::uniform_int_distribution<int>(0, 99)(rng);
    if (r < 65) return ItemRarity::Common;
    if (r < 93) return ItemRarity::Rare;
    return ItemRarity::Legendary;
}

const Palette& pickClothingPalette(ClothingTier tier, ItemRarity rarity,
                                    std::mt19937& rng) {
    auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
    switch (tier) {
        case ClothingTier::Cloth:
            switch (rarity) {
                case ItemRarity::Common:    return CLOTH_COMMON   [pick(arrLen(CLOTH_COMMON))];
                case ItemRarity::Rare:      return CLOTH_RARE     [pick(arrLen(CLOTH_RARE))];
                case ItemRarity::Legendary: return CLOTH_LEGENDARY[pick(arrLen(CLOTH_LEGENDARY))];
            }
            break;
        case ClothingTier::Leather:
            switch (rarity) {
                case ItemRarity::Common:    return LEATHER_COMMON   [pick(arrLen(LEATHER_COMMON))];
                case ItemRarity::Rare:      return LEATHER_RARE     [pick(arrLen(LEATHER_RARE))];
                case ItemRarity::Legendary: return LEATHER_LEGENDARY[pick(arrLen(LEATHER_LEGENDARY))];
            }
            break;
        case ClothingTier::Plate:
            switch (rarity) {
                case ItemRarity::Common:    return PLATE_COMMON   [pick(arrLen(PLATE_COMMON))];
                case ItemRarity::Rare:      return PLATE_RARE     [pick(arrLen(PLATE_RARE))];
                case ItemRarity::Legendary: return PLATE_LEGENDARY[pick(arrLen(PLATE_LEGENDARY))];
            }
            break;
    }
    return CLOTH_COMMON[0];   // unreachable
}

const char* pickClothingAdj(ClothingTier tier, ItemRarity rarity,
                             std::mt19937& rng) {
    auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
    switch (tier) {
        case ClothingTier::Cloth:
            switch (rarity) {
                case ItemRarity::Common:    return CLOTH_ADJ_COMMON   [pick(arrLen(CLOTH_ADJ_COMMON))];
                case ItemRarity::Rare:      return CLOTH_ADJ_RARE     [pick(arrLen(CLOTH_ADJ_RARE))];
                case ItemRarity::Legendary: return CLOTH_ADJ_LEGENDARY[pick(arrLen(CLOTH_ADJ_LEGENDARY))];
            }
            break;
        case ClothingTier::Leather:
            switch (rarity) {
                case ItemRarity::Common:    return LEATHER_ADJ_COMMON   [pick(arrLen(LEATHER_ADJ_COMMON))];
                case ItemRarity::Rare:      return LEATHER_ADJ_RARE     [pick(arrLen(LEATHER_ADJ_RARE))];
                case ItemRarity::Legendary: return LEATHER_ADJ_LEGENDARY[pick(arrLen(LEATHER_ADJ_LEGENDARY))];
            }
            break;
        case ClothingTier::Plate:
            switch (rarity) {
                case ItemRarity::Common:    return PLATE_ADJ_COMMON   [pick(arrLen(PLATE_ADJ_COMMON))];
                case ItemRarity::Rare:      return PLATE_ADJ_RARE     [pick(arrLen(PLATE_ADJ_RARE))];
                case ItemRarity::Legendary: return PLATE_ADJ_LEGENDARY[pick(arrLen(PLATE_ADJ_LEGENDARY))];
            }
            break;
    }
    return "";
}

const char* pickSlotNoun(EquipSlot slot, ItemRarity rarity, std::mt19937& rng) {
    auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
    auto choose = [&](const char* const* lo, int loN,
                      const char* const* hi, int hiN,
                      const char* const* leg, int legN) -> const char* {
        switch (rarity) {
            case ItemRarity::Common:    return lo [pick(loN)];
            case ItemRarity::Rare:      return hi [pick(hiN)];
            case ItemRarity::Legendary: return leg[pick(legN)];
        }
        return "Garment";
    };
    switch (slot) {
        case EquipSlot::Helmet:
            return choose(HELMET_NOUNS_COMMON, arrLen(HELMET_NOUNS_COMMON),
                          HELMET_NOUNS_RARE,   arrLen(HELMET_NOUNS_RARE),
                          HELMET_NOUNS_LEGENDARY, arrLen(HELMET_NOUNS_LEGENDARY));
        case EquipSlot::Shoulders:
            return choose(SHOULDERS_NOUNS_COMMON, arrLen(SHOULDERS_NOUNS_COMMON),
                          SHOULDERS_NOUNS_RARE,   arrLen(SHOULDERS_NOUNS_RARE),
                          SHOULDERS_NOUNS_LEGENDARY, arrLen(SHOULDERS_NOUNS_LEGENDARY));
        case EquipSlot::Chest:
            return choose(CHEST_NOUNS_COMMON, arrLen(CHEST_NOUNS_COMMON),
                          CHEST_NOUNS_RARE,   arrLen(CHEST_NOUNS_RARE),
                          CHEST_NOUNS_LEGENDARY, arrLen(CHEST_NOUNS_LEGENDARY));
        case EquipSlot::Legs:
            return choose(LEGS_NOUNS_COMMON, arrLen(LEGS_NOUNS_COMMON),
                          LEGS_NOUNS_RARE,   arrLen(LEGS_NOUNS_RARE),
                          LEGS_NOUNS_LEGENDARY, arrLen(LEGS_NOUNS_LEGENDARY));
        case EquipSlot::Feet:
            return choose(FEET_NOUNS_COMMON, arrLen(FEET_NOUNS_COMMON),
                          FEET_NOUNS_RARE,   arrLen(FEET_NOUNS_RARE),
                          FEET_NOUNS_LEGENDARY, arrLen(FEET_NOUNS_LEGENDARY));
        default: return "Garment";
    }
}

const char* pickWeaponAdj(WeaponType type, ItemRarity rarity,
                           std::mt19937& rng) {
    auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
    auto choose = [&](const char* const* lo, int loN,
                      const char* const* hi, int hiN,
                      const char* const* leg, int legN) -> const char* {
        switch (rarity) {
            case ItemRarity::Common:    return lo [pick(loN)];
            case ItemRarity::Rare:      return hi [pick(hiN)];
            case ItemRarity::Legendary: return leg[pick(legN)];
        }
        return "";
    };
    switch (type) {
        case WeaponType::Sword:
            return choose(SWORD_ADJ_COMMON, arrLen(SWORD_ADJ_COMMON),
                          SWORD_ADJ_RARE,   arrLen(SWORD_ADJ_RARE),
                          SWORD_ADJ_LEGENDARY, arrLen(SWORD_ADJ_LEGENDARY));
        case WeaponType::Shield:
            return choose(SHIELD_ADJ_COMMON, arrLen(SHIELD_ADJ_COMMON),
                          SHIELD_ADJ_RARE,   arrLen(SHIELD_ADJ_RARE),
                          SHIELD_ADJ_LEGENDARY, arrLen(SHIELD_ADJ_LEGENDARY));
        case WeaponType::Bow:
            return choose(BOW_ADJ_COMMON, arrLen(BOW_ADJ_COMMON),
                          BOW_ADJ_RARE,   arrLen(BOW_ADJ_RARE),
                          BOW_ADJ_LEGENDARY, arrLen(BOW_ADJ_LEGENDARY));
        case WeaponType::Staff:
            return choose(STAFF_ADJ_COMMON, arrLen(STAFF_ADJ_COMMON),
                          STAFF_ADJ_RARE,   arrLen(STAFF_ADJ_RARE),
                          STAFF_ADJ_LEGENDARY, arrLen(STAFF_ADJ_LEGENDARY));
        case WeaponType::Axe:
        case WeaponType::Mace:   // heavy weapons share the brutal adjective pool
            return choose(AXE_ADJ_COMMON, arrLen(AXE_ADJ_COMMON),
                          AXE_ADJ_RARE,   arrLen(AXE_ADJ_RARE),
                          AXE_ADJ_LEGENDARY, arrLen(AXE_ADJ_LEGENDARY));
        case WeaponType::Dagger: // bladed weapons share the sword adjective pool
        case WeaponType::Greatsword:
        case WeaponType::Spear:
            return choose(SWORD_ADJ_COMMON, arrLen(SWORD_ADJ_COMMON),
                          SWORD_ADJ_RARE,   arrLen(SWORD_ADJ_RARE),
                          SWORD_ADJ_LEGENDARY, arrLen(SWORD_ADJ_LEGENDARY));
        default: return "";
    }
}

// Stat multiplier driven by rarity. Common items are below the design
// base; Rare around par; Legendary substantially above.
float statMultiplierForRarity(ItemRarity rarity, std::mt19937& rng) {
    auto frand = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    switch (rarity) {
        case ItemRarity::Common:    return frand(0.6f, 0.9f);
        case ItemRarity::Rare:      return frand(1.0f, 1.3f);
        case ItemRarity::Legendary: return frand(1.6f, 2.2f);
    }
    return 1.0f;
}

// Roll an item level around `target`. Commons skew low (they're meant to
// be quickly outgrown); Rares match target; Legendaries skew slightly
// high (so they feel like aspirational upgrades).
int rollItemLevel(int target, ItemRarity rarity, std::mt19937& rng) {
    auto pick = [&](int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    };
    int lvl = target;
    switch (rarity) {
        case ItemRarity::Common:    lvl = target + pick(-2, 0); break;
        case ItemRarity::Rare:      lvl = target + pick(-1, 1); break;
        case ItemRarity::Legendary: lvl = target + pick( 0, 2); break;
    }
    if (lvl < 1) lvl = 1;
    return lvl;
}

// Per-level stat scaling — each level adds 10% to the base value, so a
// level-10 piece is roughly twice as strong as a level-1 piece (before
// rarity multipliers).
float levelStatBonus(int level) {
    return 1.0f + 0.10f * float(level - 1);
}

// Colour perturbation range tightens for high rarity so legendary palettes
// stay visually distinct (a curated jewel tone isn't muddled by noise).
int colorPerturbForRarity(ItemRarity rarity) {
    switch (rarity) {
        case ItemRarity::Common:    return 25;
        case ItemRarity::Rare:      return 15;
        case ItemRarity::Legendary: return 6;
    }
    return 10;
}

}  // namespace

// Public view of the themed-set table (anon-namespace symbols are visible
// within this TU, so we can read KNOWN_SETS here). Built once on first use.
const std::vector<ArmorSetInfo>& armorSetCatalog() {
    static const std::vector<ArmorSetInfo> cat = [] {
        std::vector<ArmorSetInfo> v;
        for (int i = 0; i < arrLen(KNOWN_SETS); ++i)
            v.push_back({ KNOWN_SETS[i].name, KNOWN_SETS[i].tier,
                          KNOWN_SETS[i].primary, KNOWN_SETS[i].accent });
        return v;
    }();
    return cat;
}

// =============================================================================
// Public API
// =============================================================================

std::unique_ptr<ClothingItem> generateRandomClothing(uint32_t seed,
                                                     ClothingTier tier,
                                                     EquipSlot slot,
                                                     int targetLevel) {
    std::mt19937 rng(seed);
    ItemRarity rarity = rollRarity(rng);
    const Palette& pal = pickClothingPalette(tier, rarity, rng);

    std::string name = std::string(pickClothingAdj(tier, rarity, rng)) + " "
                     + pickSlotNoun(slot, rarity, rng);
    if (rarity == ItemRarity::Legendary
        && std::uniform_int_distribution<int>(0, 9)(rng) < 6) {
        int i = std::uniform_int_distribution<int>(0, arrLen(LEGENDARY_SUFFIXES) - 1)(rng);
        name += " ";
        name += LEGENDARY_SUFFIXES[i];
    }

    auto item = std::make_unique<ClothingItem>(name, slot, tier);
    item->rarity = rarity;
    item->level  = rollItemLevel(targetLevel, rarity, rng);
    int range = colorPerturbForRarity(rarity);
    item->primaryColor = perturbVoxel(rng, pal.primary, range);
    item->accentColor  = perturbVoxel(rng, pal.accent,  range);
    item->patternSeed  = (int)(rng() & 0xFFFF);

    item->defenseValue *= statMultiplierForRarity(rarity, rng)
                        * levelStatBonus(item->level);
    return item;
}

std::unique_ptr<ClothingItem> generateRandomClothing(uint32_t seed,
                                                     int targetLevel) {
    std::mt19937 outer(seed);
    ClothingTier tier = (ClothingTier)std::uniform_int_distribution<int>(0, 2)(outer);
    static const EquipSlot slots[] = {
        EquipSlot::Helmet, EquipSlot::Shoulders, EquipSlot::Chest,
        EquipSlot::Legs,   EquipSlot::Feet,
    };
    EquipSlot slot = slots[std::uniform_int_distribution<int>(0, 4)(outer)];
    return generateRandomClothing(outer(), tier, slot, targetLevel);
}

std::unique_ptr<ClothingItem> generateSetClothing(uint32_t seed, int targetLevel) {
    std::mt19937 rng(seed);
    auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
    auto frand = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    const ArmorSet& set = KNOWN_SETS[pick(arrLen(KNOWN_SETS))];
    static const EquipSlot slots[] = {
        EquipSlot::Helmet, EquipSlot::Shoulders, EquipSlot::Chest,
        EquipSlot::Legs,   EquipSlot::Feet,
    };
    EquipSlot slot = slots[pick(arrLen(slots))];

    // Set pieces skip Common — they're trophies, not jumble. 70/30
    // weighting between Rare and Legendary.
    ItemRarity rarity = (pick(10) < 7) ? ItemRarity::Rare : ItemRarity::Legendary;

    // Name: "<setAdjective> <slotNoun>" — e.g. "Wolfblood Pauldrons".
    std::string name = std::string(set.adjective) + " "
                     + pickSlotNoun(slot, rarity, rng);

    auto item = std::make_unique<ClothingItem>(name, slot, set.tier);
    item->rarity      = rarity;
    item->level       = rollItemLevel(targetLevel, rarity, rng);
    item->setKey      = set.name;
    // Tiny perturbation so two pieces from the same set don't look
    // pixel-perfect identical, but stay clearly related.
    item->primaryColor = perturbVoxel(rng, set.primary, 6);
    item->accentColor  = perturbVoxel(rng, set.accent,  6);
    item->patternSeed  = (int)(rng() & 0xFFFF);

    item->defenseValue *= statMultiplierForRarity(rarity, rng)
                        * levelStatBonus(item->level);
    (void)frand;
    return item;
}

std::unique_ptr<WeaponItem> generateRandomWeapon(uint32_t seed, WeaponType type,
                                                 int targetLevel) {
    std::mt19937 rng(seed);
    ItemRarity rarity = rollRarity(rng);
    auto rndByte = [&](int lo, int hi) {
        return (uint8_t)std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    if (type == WeaponType::None) return nullptr;
    std::string name = std::string(pickWeaponAdj(type, rarity, rng)) + " "
                     + std::string(weaponTypeName(type));
    if (rarity == ItemRarity::Legendary
        && std::uniform_int_distribution<int>(0, 9)(rng) < 5) {
        int i = std::uniform_int_distribution<int>(0, arrLen(LEGENDARY_SUFFIXES) - 1)(rng);
        name += " ";
        name += LEGENDARY_SUFFIXES[i];
    }
    auto item = createWeaponItem(name, type);
    if (!item) return nullptr;
    item->rarity = rarity;
    item->level  = rollItemLevel(targetLevel, rarity, rng);

    // Colour ranges tighten for legendary so the iconic metallics stay
    // recognisable; commons get a muddier spread.
    auto metallic = [&](int loBase, int hiBase) {
        int spread = (rarity == ItemRarity::Legendary) ? 25
                   : (rarity == ItemRarity::Rare)      ? 50
                   : 90;
        int lo = std::max(40, loBase - spread / 4);
        int hi = std::min(255, hiBase + spread / 4);
        return Voxel{ rndByte(lo, hi), rndByte(lo, hi), rndByte(lo, hi), 255 };
    };
    auto wood = [&]() {
        int loR = (rarity == ItemRarity::Legendary) ? 60 : 70;
        int hiR = (rarity == ItemRarity::Legendary) ? 130 : 180;
        return Voxel{ rndByte(loR, hiR), rndByte(loR - 20, hiR - 40),
                      rndByte(loR - 40, hiR - 70), 255 };
    };
    auto vivid = [&]() {
        return Voxel{ rndByte(120, 240), rndByte(120, 240), rndByte(120, 240), 255 };
    };
    auto glowAcc = [&]() {
        // Legendary accents lean bright + saturated — a colour that will
        // brighten well into a believable glow.
        return Voxel{ rndByte(120, 255), rndByte(120, 255), rndByte(160, 255), 255 };
    };

    switch (type) {
        case WeaponType::Sword:
            item->primaryColor = metallic(140, 230);
            item->accentColor  = (rarity == ItemRarity::Legendary) ? glowAcc()
                                : Voxel{ rndByte(60, 130), rndByte(30, 90), rndByte(20, 60), 255 };
            break;
        case WeaponType::Shield:
            item->primaryColor = (rarity == ItemRarity::Common) ? wood() : metallic(90, 200);
            item->accentColor  = (rarity == ItemRarity::Legendary) ? glowAcc() : vivid();
            break;
        case WeaponType::Bow:
            item->primaryColor = wood();
            item->accentColor  = (rarity == ItemRarity::Legendary) ? glowAcc() : vivid();
            break;
        case WeaponType::Staff: {
            // Staves get an elemental school + colour palette to match —
            // a red+yellow staff fires fire bolts, blue+white fires ice,
            // purple fires arcane. The StaffItem reads `element` in
            // onPrimaryAttack to pick the right Projectile subclass.
            int elementRoll = std::uniform_int_distribution<int>(0, 2)(rng);
            WeaponElement elem = (elementRoll == 0) ? WeaponElement::Fire
                                : (elementRoll == 1) ? WeaponElement::Ice
                                                      : WeaponElement::Arcane;
            item->element = elem;
            switch (elem) {
                case WeaponElement::Fire:
                    // Dark reddish-brown body with bright flame accent.
                    item->primaryColor = { rndByte( 80, 120), rndByte( 30,  50),
                                            rndByte( 20,  30), 255 };
                    item->accentColor  = { rndByte(220, 255), rndByte(120, 200),
                                            rndByte( 40,  80), 255 };
                    break;
                case WeaponElement::Ice:
                    // Cool blue body with bright frost accent.
                    item->primaryColor = { rndByte( 70, 130), rndByte( 90, 150),
                                            rndByte(110, 180), 255 };
                    item->accentColor  = { rndByte(190, 240), rndByte(220, 250),
                                            rndByte(240, 255), 255 };
                    break;
                case WeaponElement::Arcane:
                    // Dark indigo body with vivid purple accent.
                    item->primaryColor = { rndByte( 60,  90), rndByte( 40,  60),
                                            rndByte( 90, 140), 255 };
                    item->accentColor  = { rndByte(180, 230), rndByte( 90, 160),
                                            rndByte(220, 255), 255 };
                    break;
                default: break;
            }
            // Prepend the element to the name so the player can tell
            // them apart in the bag at a glance.
            const char* prefix = (elem == WeaponElement::Fire)   ? "Flame "
                               : (elem == WeaponElement::Ice)    ? "Frost "
                               : (elem == WeaponElement::Arcane) ? "Arcane "
                                                                  : "";
            item->setName(std::string(prefix) + item->getName());
            break;
        }
        case WeaponType::Axe:
        case WeaponType::Mace:
            item->primaryColor = metallic(150, 230);
            item->accentColor  = (rarity == ItemRarity::Legendary) ? glowAcc()
                                : Voxel{ rndByte(80, 150), rndByte(50, 100), rndByte(30, 70), 255 };
            break;
        case WeaponType::Dagger:
        case WeaponType::Greatsword:
            item->primaryColor = metallic(140, 230);
            item->accentColor  = (rarity == ItemRarity::Legendary) ? glowAcc()
                                : Voxel{ rndByte(60, 130), rndByte(30, 90), rndByte(20, 60), 255 };
            break;
        case WeaponType::Spear:   // steel head, wooden shaft
            item->primaryColor = metallic(150, 230);
            item->accentColor  = wood();
            break;
        default: break;
    }

    float mult = statMultiplierForRarity(rarity, rng) * levelStatBonus(item->level);
    item->attackPower  *= mult;
    item->defenseValue *= mult;
    return item;
}

std::unique_ptr<WeaponItem> generateRandomWeapon(uint32_t seed,
                                                 int targetLevel) {
    std::mt19937 outer(seed);
    static const WeaponType types[] = {
        WeaponType::Sword, WeaponType::Shield, WeaponType::Bow,
        WeaponType::Staff, WeaponType::Axe, WeaponType::Dagger, WeaponType::Mace,
        WeaponType::Spear, WeaponType::Greatsword,
    };
    WeaponType t = types[std::uniform_int_distribution<int>(
        0, (int)(sizeof(types) / sizeof(types[0])) - 1)(outer)];
    return generateRandomWeapon(outer(), t, targetLevel);
}

std::unique_ptr<Item> generateRandomItem(uint32_t seed, int targetLevel) {
    std::mt19937 outer(seed);
    int roll = std::uniform_int_distribution<int>(0, 99)(outer);
    // 18% set piece, 52% free-roll clothing, 30% weapon.
    if (roll < 18)       return generateSetClothing(outer(), targetLevel);
    if (roll < 18 + 52)  return generateRandomClothing(outer(), targetLevel);
    return generateRandomWeapon(outer(), targetLevel);
}

std::unique_ptr<Item> generateLegendaryItem(uint32_t seed, int targetLevel) {
    std::mt19937 s(seed ? seed : 1u);
    for (int i = 0; i < 250; i++) {
        auto it = generateRandomItem(s(), targetLevel);
        if (it && it->rarity == ItemRarity::Legendary) return it;   // rare ~7%, so this lands fast
    }
    return generateRandomItem(s(), targetLevel);
}
