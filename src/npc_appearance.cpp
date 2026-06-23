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

// Farm workwear — denim/canvas overalls in muted blues, browns and hay tans.
const Palette FARMER_PALETTES[] = {
    { { 70,  90, 130, 255}, {120, 100,  70, 255} },   // blue denim overalls
    { {120, 100,  70, 255}, { 80,  65,  45, 255} },   // brown workwear
    { {150, 140, 100, 255}, {100,  85,  55, 255} },   // hay tan
    { { 90, 110,  90, 255}, { 70,  60,  45, 255} },   // faded green
    { {130, 110,  90, 255}, { 90,  70,  50, 255} },   // earth
};

// Bleached bone — skeletons (worn as pale cloth over the whole body).
const Palette SKELETON_PALETTES[] = {
    { {225, 222, 210, 255}, {150, 148, 138, 255} },   // bone white
    { {205, 205, 195, 255}, {130, 130, 120, 255} },   // grey bone
};

// Heavy dark hide — brutes / ogres.
const Palette BRUTE_PALETTES[] = {
    { { 72,  60,  55, 255}, {120,  40,  40, 255} },   // dark hide + blood
    { { 80,  72,  52, 255}, { 60,  90,  50, 255} },   // muddy green-brown
};

// Dusk-violet and crimson robes — cultists.
const Palette CULTIST_PALETTES[] = {
    { { 48,  38,  62, 255}, {135,  75, 155, 255} },   // dusk violet
    { { 36,  34,  44, 255}, {120,  45,  45, 255} },   // black + crimson
};
const Palette ZOMBIE_PALETTES[] = {
    { { 96, 120,  72, 255}, { 70,  88,  52, 255} },   // rotting moss-green
    { { 82, 102,  78, 255}, { 58,  74,  56, 255} },   // sickly grey-green
    { {110, 116,  80, 255}, { 78,  84,  56, 255} },   // jaundiced ochre
};
const Palette KNIGHT_PALETTES[] = {
    { { 70,  74,  84, 255}, {150,  40,  40, 255} },   // dark iron + crimson sash
    { { 58,  60,  70, 255}, {120, 120, 140, 255} },   // blackened steel
    { { 80,  82,  92, 255}, {200, 180,  90, 255} },   // tarnished gilt
};
const Palette VENDOR_PALETTES[] = {
    { {120,  70,  45, 255}, {210, 175,  80, 255} },   // merchant brown + gold trim
    { { 70,  95,  80, 255}, {200, 170, 110, 255} },   // travelling-trader green
    { {110,  60,  80, 255}, {225, 200, 150, 255} },   // wine-red bolt of cloth
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
    if (type == NPCType::Enemy || type == NPCType::Brute || type == NPCType::Zombie)
        tier = ClothingTier::Leather;
    else if (type == NPCType::Guard || type == NPCType::Knight)
        tier = ClothingTier::Plate;

    // Single palette picked once — used for every slot so the outfit
    // reads as one coherent set instead of five mismatched pieces.
    const Palette* palettes = VILLAGER_PALETTES;
    int            palCount = arrLen(VILLAGER_PALETTES);
    switch (type) {
        case NPCType::Enemy:
            palettes = BANDIT_PALETTES; palCount = arrLen(BANDIT_PALETTES); break;
        case NPCType::Guard:
            palettes = GUARD_PALETTES;  palCount = arrLen(GUARD_PALETTES);  break;
        case NPCType::Farmer:
            palettes = FARMER_PALETTES;   palCount = arrLen(FARMER_PALETTES);   break;
        case NPCType::Skeleton:
            palettes = SKELETON_PALETTES; palCount = arrLen(SKELETON_PALETTES); break;
        case NPCType::Brute:
            palettes = BRUTE_PALETTES;    palCount = arrLen(BRUTE_PALETTES);    break;
        case NPCType::Cultist:
            palettes = CULTIST_PALETTES;  palCount = arrLen(CULTIST_PALETTES);  break;
        case NPCType::Trainer:
            palettes = CULTIST_PALETTES;  palCount = arrLen(CULTIST_PALETTES);  break;  // robed mentor
        case NPCType::Zombie:
            palettes = ZOMBIE_PALETTES;   palCount = arrLen(ZOMBIE_PALETTES);   break;
        case NPCType::Knight:
            palettes = KNIGHT_PALETTES;   palCount = arrLen(KNIGHT_PALETTES);   break;
        case NPCType::Vendor:
            palettes = VENDOR_PALETTES;   palCount = arrLen(VENDOR_PALETTES);   break;
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
    } else if (type == NPCType::Farmer) {
        wear[0] = true;             // straw hat always
        wear[1] = false;            // no shoulders
        wear[2] = true;             // overalls / shirt
        wear[3] = true;             // trousers
        wear[4] = true;             // work boots
    } else if (type == NPCType::Trainer || type == NPCType::Vendor) {
        wear[0] = false;            // no helm — face visible
        wear[1] = true;             // hooded mantle / shopkeeper's shawl
        wear[2] = true;             // robe / apron
        wear[3] = true;             // robe skirt
        wear[4] = true;             // boots
    } else {
        wear[0] = (pick(2) == 0);   // ~50% have a helmet
        wear[1] = true;
        wear[2] = true;
        wear[3] = true;
        wear[4] = true;
    }

    // A hood / helm is part of the silhouette for cultists, skeletons, knights.
    if (type == NPCType::Cultist || type == NPCType::Skeleton ||
        type == NPCType::Knight) wear[0] = true;

    // Wipe the rig back to bare skin then layer the chosen clothing
    // pieces in slot order so accents stack correctly.
    rig.resetBaseBody();
    rig.applyCustomization();
    for (int i = 0; i < 5; i++) {
        if (!wear[i]) continue;
        Voxel pri = pal.primary, acc = pal.accent;
        if (type == NPCType::Farmer) {
            // A straw hat and brown boots regardless of the overalls colour.
            if (SLOTS[i] == EquipSlot::Helmet) { pri = {210, 190, 110, 255}; acc = {165, 140,  80, 255}; }
            else if (SLOTS[i] == EquipSlot::Feet) { pri = { 95,  70,  45, 255}; acc = { 60,  45,  30, 255}; }
        }
        paintClothingOnto(rig, tier, ItemRarity::Common, SLOTS[i], pri, acc);
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
    } else if (type == NPCType::Farmer) {
        mainW   = (pick(2) == 0) ? WeaponType::Hoe : WeaponType::Scythe;
        mainPri = {150, 150, 160, 255};   // steel head
        mainAcc = {110,  80,  50, 255};   // wooden haft
    } else if (type == NPCType::Skeleton) {
        mainW   = (pick(3) == 0) ? WeaponType::Bow : WeaponType::Sword;
        mainPri = {150, 150, 150, 255};
        mainAcc = { 90,  80,  60, 255};
    } else if (type == NPCType::Brute) {
        mainW   = WeaponType::Axe;        // a great cleaver
        mainPri = {130, 130, 135, 255};
        mainAcc = { 70,  50,  35, 255};
    } else if (type == NPCType::Cultist) {
        mainW   = WeaponType::Staff;      // hurls bolts; uses the cast pose
        mainPri = {120,  90, 150, 255};
        mainAcc = {200, 160, 220, 255};
    } else if (type == NPCType::Trainer) {
        mainW   = WeaponType::Staff;      // a mentor's staff of office
        mainPri = {120,  95,  60, 255};   // carved wood
        mainAcc = {210, 185, 110, 255};   // gilded tip
    } else if (type == NPCType::Zombie) {
        mainW   = (pick(2) == 0) ? WeaponType::Sword : WeaponType::None;  // a rusty blade, or bare claws
        mainPri = {120, 118,  96, 255};   // corroded iron
        mainAcc = { 70,  62,  44, 255};
    } else if (type == NPCType::Knight) {
        mainW   = (pick(3) == 0) ? WeaponType::Axe : WeaponType::Sword;
        offW    = WeaponType::Shield;     // sword/axe + shield, shield in livery
        mainPri = {200, 205, 215, 255};   // polished steel
        mainAcc = { 80,  55,  30, 255};
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

    // A brute towers over everyone else (height scale is applied at draw time).
    if (type == NPCType::Brute) rig.heightScale = 1.4f;
    else if (type == NPCType::Zombie) rig.heightScale = 0.95f;   // a slight shamble-hunch
    else if (type == NPCType::Knight) rig.heightScale = 1.08f;   // an imposing, armoured frame
}
