#include "npc_appearance.h"
#include "items.h"
#include "clothing_painter.h"
#include "weapon_builder.h"
#include <random>

namespace {

struct Palette { Voxel primary; Voxel accent; };

// Drab peasant colours — earth tones, wool greys, faded dyes.
const Palette VILLAGER_PALETTES[] = {
    { {130, 105,  75, 255}, { 75,  55,  35, 255} },   // brown linen
    { {100, 100, 105, 255}, { 65,  65,  70, 255} },   // grey wool
    { { 85, 115,  70, 255}, { 55,  75,  45, 255} },   // dusty green
    { {120,  90,  85, 255}, { 75,  55,  55, 255} },   // muted brick
    { {140, 145, 130, 255}, { 95, 100,  85, 255} },   // pale sage
    { {110, 100,  85, 255}, { 70,  65,  55, 255} },   // hay tan
    { {120, 115, 100, 255}, { 80,  75,  65, 255} },   // bone
    { { 95, 110, 130, 255}, { 60,  75,  95, 255} },   // dusty blue
    { {145, 120,  85, 255}, {100,  80,  55, 255} },   // ochre
    { { 90,  85,  95, 255}, { 60,  55,  65, 255} },   // slate
};

// Dark rough leather — bandit colours.
const Palette BANDIT_PALETTES[] = {
    { { 50,  40,  35, 255}, {110, 110, 115, 255} },   // black leather + silver
    { { 65,  45,  30, 255}, { 35,  25,  18, 255} },   // dark brown
    { {100,  35,  35, 255}, { 50,  20,  20, 255} },   // blood red
    { { 50,  70,  50, 255}, { 30,  45,  30, 255} },   // forest green
    { { 60,  55,  55, 255}, { 30,  25,  25, 255} },   // dark grey
    { { 80,  60,  40, 255}, {180, 150,  60, 255} },   // tan + brass
    { { 45,  35,  55, 255}, { 90,  60, 110, 255} },   // dusk violet
};

// Town livery for guards — polished steel with civic accent colours.
const Palette GUARD_PALETTES[] = {
    { {170, 175, 185, 255}, { 70,  90, 155, 255} },   // steel + town blue
    { {200, 165,  80, 255}, {145,  50,  50, 255} },   // bronze + red
    { {130, 130, 140, 255}, {200, 175,  70, 255} },   // dark iron + gold
    { {160, 165, 175, 255}, { 30,  55, 110, 255} },   // polished steel + navy
    { {180, 185, 200, 255}, { 55, 110,  65, 255} },   // mithril + green
    { {145, 150, 160, 255}, {180,  90,  35, 255} },   // grey iron + amber
};

template <typename T, size_t N>
constexpr int arrLen(T (&)[N]) { return (int)N; }

}  // namespace

void applyNpcThemedLoadout(BipedalRig& rig, uint32_t seed, NPCType type) {
    std::mt19937 rng(seed ? seed : 1u);
    auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };

    // Clothing tier per type — villagers run around in cloth, bandits
    // in leather, guards in plate. This drives both the shape of the
    // armour (clothing_painter does the actual rendering) and how it
    // sits on the body.
    ClothingTier tier = ClothingTier::Cloth;
    if (type == NPCType::Enemy)      tier = ClothingTier::Leather;
    else if (type == NPCType::Guard) tier = ClothingTier::Plate;

    // Single palette picked once — used for every slot so the outfit
    // reads as one coherent set instead of five mismatched pieces.
    const Palette* palettes = VILLAGER_PALETTES;
    int            palCount = arrLen(VILLAGER_PALETTES);
    switch (type) {
        case NPCType::Enemy:
            palettes = BANDIT_PALETTES; palCount = arrLen(BANDIT_PALETTES); break;
        case NPCType::Guard:
            palettes = GUARD_PALETTES;  palCount = arrLen(GUARD_PALETTES);  break;
        default: break;
    }
    const Palette& pal = palettes[pick(palCount)];

    // Decide which slots to fill. Villagers are scrappy — they often
    // skip helmet/shoulders/shoes so the population looks varied:
    // some in full peasant kit, some bare-headed, some barefoot.
    // Bandits and guards always wear the full set (chest + legs + feet
    // mandatory; helmet only 50% of the time so faces are visible).
    static const EquipSlot SLOTS[5] = {
        EquipSlot::Helmet, EquipSlot::Shoulders, EquipSlot::Chest,
        EquipSlot::Legs,   EquipSlot::Feet,
    };
    bool wear[5];
    if (type == NPCType::Villager) {
        wear[0] = (pick(3) == 0);   // ~33% have a cap
        wear[1] = (pick(4) == 0);   // ~25% have a mantle/shawl
        wear[2] = true;             // shirt always
        wear[3] = true;             // trousers always
        wear[4] = (pick(3) != 0);   // ~66% have shoes
    } else {
        wear[0] = (pick(2) == 0);   // ~50% have a helmet
        wear[1] = true;
        wear[2] = true;
        wear[3] = true;
        wear[4] = true;
    }

    // Wipe the rig back to bare skin then layer the chosen clothing
    // pieces in slot order so accents stack correctly.
    rig.resetBaseBody();
    rig.applyCustomization();
    for (int i = 0; i < 5; i++) {
        if (!wear[i]) continue;
        paintClothingOnto(rig, tier, ItemRarity::Common,
                          SLOTS[i], pal.primary, pal.accent);
    }

    // Weapons — only bandits and guards carry them. Villagers are
    // unarmed (the user's design: peasants walk around with no weapon).
    WeaponType mainW = WeaponType::None;
    WeaponType offW  = WeaponType::None;
    Voxel mainPri{170, 175, 185, 255};
    Voxel mainAcc{ 90,  60,  35, 255};
    Voxel offPri = pal.primary;
    Voxel offAcc = pal.accent;

    if (type == NPCType::Enemy) {
        int roll = pick(10);
        if      (roll < 5) mainW = WeaponType::Sword;
        else if (roll < 8) mainW = WeaponType::Axe;
        else               mainW = WeaponType::Bow;
        // Bandits get dull weapons with dark grips.
        mainPri = {150, 155, 165, 255};
        mainAcc = { 60,  40,  25, 255};
    } else if (type == NPCType::Guard) {
        mainW = (pick(2) == 0) ? WeaponType::Sword : WeaponType::Axe;
        offW  = WeaponType::Shield;
        // Polished weapons; shield reuses armour palette = town livery.
        mainPri = {210, 210, 220, 255};
        mainAcc = { 90,  60,  30, 255};
    }

    applyWeaponsToRig(rig,
        mainW, ItemRarity::Common, mainPri, mainAcc,
        offW,  ItemRarity::Common, offPri,  offAcc);

    // Lanterns — some NPCs carry one at their belt to light their patch
    // of the world at night. Driven by the same per-NPC seed so the
    // population is stable across sessions and matches across clients.
    //   Villagers: ~25% (lanterns are valuable; not every peasant owns one)
    //   Guards:    ~70% (night patrols expect to be lit)
    //   Bandits:   never (they'd give themselves away in ambushes)
    int lampRoll = pick(100);
    if (type == NPCType::Villager) rig.hasLantern = (lampRoll < 25);
    else if (type == NPCType::Guard)    rig.hasLantern = (lampRoll < 70);
    else                                rig.hasLantern = false;
    rig.lanternHeld = false;   // NPCs always carry at the belt, never raised
}
