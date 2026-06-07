#include "weapon_builder.h"
#include "voxel_model.h"
#include <algorithm>
#include <cmath>

// Brighten a voxel toward white — used for the glow seams on Legendary
// weapons. Mirrors the helper in clothing_painter.cpp.
static Voxel brightenVoxel(Voxel c, float factor) {
    auto clip = [](int v) { return (uint8_t)std::clamp(v, 0, 255); };
    return { clip((int)(c.r * factor + 30)),
             clip((int)(c.g * factor + 30)),
             clip((int)(c.b * factor + 30)),
             255 };
}

// Stamp rarity-dependent extras onto an already-built weapon mesh. Rare
// gets a small highlight (mostly accent), Legendary gets glow seams /
// gem clusters / runes.
static void embellishWeapon(VoxelVolume* v, WeaponType type,
                            ItemRarity rarity, Voxel acc) {
    if (!v || rarity == ItemRarity::Common) return;
    Voxel trim = brightenVoxel(acc, 1.2f);
    Voxel glow = brightenVoxel(acc, 1.8f);
    bool legendary = (rarity == ItemRarity::Legendary);
    switch (type) {
        case WeaponType::Sword:
            // Tip highlight at the very top of the blade.
            for (int x = 0; x < 4; x++) v->setVoxel(x, 19, 0, legendary ? glow : trim);
            if (legendary) {
                // Fuller line down the blade in glow.
                for (int y = 7; y < 18; y++) {
                    v->setVoxel(1, y, 0, glow);
                    v->setVoxel(2, y, 1, glow);
                }
            }
            break;
        case WeaponType::Shield:
            if (legendary) {
                // Glowing boss in the centre.
                for (int x = 3; x <= 6; x++) for (int y = 5; y <= 8; y++)
                    v->setVoxel(x, y, 1, glow);
            } else {
                // Single-voxel rivet at the centre.
                v->setVoxel(4, 7, 1, trim);
                v->setVoxel(5, 7, 1, trim);
            }
            break;
        case WeaponType::Bow:
            // Glowing tips on Legendary; brighter accent stripe on Rare.
            for (int y : {0, 1, 22, 23}) {
                v->setVoxel(0, y, 1, legendary ? glow : trim);
                v->setVoxel(1, y, 1, legendary ? glow : trim);
                v->setVoxel(2, y, 1, legendary ? glow : trim);
            }
            if (legendary) {
                // Bowstring brightened.
                for (int y = 0; y < 24; y++) v->setVoxel(1, y, 0, glow);
            }
            break;
        case WeaponType::Staff:
            if (legendary) {
                // Extra ring of glow around the orb.
                for (int y = 20; y < 22; y++) for (int x = 0; x < 3; x++) for (int z = 0; z < 3; z++)
                    if (x == 0 || x == 2 || z == 0 || z == 2) v->setVoxel(x, y, z, glow);
                // Floating shard above the orb.
                v->setVoxel(1, 25, 1, glow);
            } else {
                // Rare: just a brighter band below the orb.
                for (int x = 0; x < 3; x++) for (int z = 0; z < 3; z++)
                    v->setVoxel(x, 21, z, trim);
            }
            break;
        case WeaponType::Axe:
            if (legendary) {
                // Glowing rune across the axe head.
                for (int x = 2; x < 5; x++) v->setVoxel(x, 16, 1, glow);
                v->setVoxel(3, 15, 1, glow);
                v->setVoxel(3, 17, 1, glow);
            } else {
                // Rare: brighter edge along the cutting curve.
                for (int x = 1; x < 6; x++) v->setVoxel(x, 19, 0, trim);
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Weapon mesh construction + rig attachment
// ---------------------------------------------------------------------------

static VoxelVolume* buildBaseWeaponMesh(WeaponType type, Voxel pri, Voxel acc) {
    static const Voxel guard = {60, 60, 60, 255};
    switch (type) {
        case WeaponType::Sword: {
            // 4x20x2 — short grip in accent, guard, then a long blade.
            VoxelVolume* v = new VoxelVolume(4, 20, 2);
            for (int x = 0; x < 4; x++) for (int y = 0; y < 20; y++) for (int z = 0; z < 2; z++) {
                if (y < 4)       v->setVoxel(x, y, z, acc);
                else if (y < 6)  v->setVoxel(x, y, z, guard);
                else             v->setVoxel(x, y, z, pri);
            }
            return v;
        }
        case WeaponType::Shield: {
            // 10x14x2 heater shield — primary face, accent rim, dark grip.
            VoxelVolume* v = new VoxelVolume(10, 14, 2);
            for (int x = 0; x < 10; x++) for (int y = 0; y < 14; y++) for (int z = 0; z < 2; z++) {
                float dx = (x + 0.5f - 4.5f) / 5.0f;
                float dy = (y + 0.5f - 7.0f) / 7.0f;
                float r  = dx * dx + dy * dy * 0.9f;
                if (r > 1.05f) continue;
                Voxel c = (r > 0.85f) ? acc : pri;
                if (z == 0 && (x >= 4 && x <= 5) && (y >= 6 && y <= 8))
                    c = guard;   // grip strap on the back
                v->setVoxel(x, y, z, c);
            }
            return v;
        }
        case WeaponType::Bow: {
            // 3x24x2 — wooden limbs, string at z=2.
            VoxelVolume* v = new VoxelVolume(3, 24, 3);
            for (int y = 0; y < 24; y++) {
                int curve = (int)(2.5f - 2.5f * std::abs(y - 11.5f) / 11.5f);
                int x = 1 + curve;
                if (x < 0) x = 0;
                if (x > 2) x = 2;
                v->setVoxel(x, y, 1, pri);
                if (y < 2 || y > 21) v->setVoxel(x, y, 1, acc);   // tips
                // String along the inner side.
                v->setVoxel(1, y, 0, acc);
            }
            return v;
        }
        case WeaponType::Staff: {
            // 2x26x2 — long shaft, crystal/orb cluster at the top in accent.
            VoxelVolume* v = new VoxelVolume(3, 26, 3);
            for (int y = 0; y < 22; y++) for (int z = 0; z < 3; z++) for (int x = 0; x < 3; x++) {
                if (x == 1 && z == 1) v->setVoxel(x, y, z, pri);
                else if ((x == 1 && z != 1) || (x != 1 && z == 1)) v->setVoxel(x, y, z, pri);
            }
            // Orb at the head.
            for (int y = 22; y < 26; y++) for (int z = 0; z < 3; z++) for (int x = 0; x < 3; x++) {
                float dx = x - 1.0f, dy = y - 23.5f, dz = z - 1.0f;
                if (dx * dx + dy * dy + dz * dz <= 2.5f)
                    v->setVoxel(x, y, z, acc);
            }
            return v;
        }
        case WeaponType::Axe: {
            // 6x20x2 — handle on one side, big head at the top.
            VoxelVolume* v = new VoxelVolume(6, 20, 2);
            for (int y = 0; y < 18; y++) for (int z = 0; z < 2; z++) {
                v->setVoxel(0, y, z, acc);   // wooden haft
                v->setVoxel(1, y, z, acc);
            }
            // Axe head — sweeping curve on top.
            for (int x = 1; x < 6; x++) for (int y = 13; y < 20; y++) for (int z = 0; z < 2; z++) {
                int top = 19 - (x * 2);
                if (top < 13) top = 13;
                if (y >= top) v->setVoxel(x, y, z, pri);
            }
            return v;
        }
        case WeaponType::Hoe: {
            // 5x22x2 — a wooden haft (accent) with a short metal blade jutting
            // out at the top, perpendicular to the shaft.
            VoxelVolume* v = new VoxelVolume(5, 22, 2);
            for (int y = 0; y < 20; y++) for (int z = 0; z < 2; z++)
                v->setVoxel(1, y, z, acc);              // haft
            for (int x = 1; x < 5; x++) for (int z = 0; z < 2; z++)
                v->setVoxel(x, 19, z, pri);             // blade
            for (int x = 2; x < 5; x++) for (int z = 0; z < 2; z++)
                v->setVoxel(x, 18, z, pri);             // a little depth
            return v;
        }
        case WeaponType::Scythe: {
            // 8x24x2 — a long snath (accent) with a curved blade (primary)
            // sweeping down and out near the top.
            VoxelVolume* v = new VoxelVolume(8, 24, 2);
            for (int y = 0; y < 22; y++) for (int z = 0; z < 2; z++)
                v->setVoxel(1, y, z, acc);              // snath
            for (int x = 1; x < 8; x++) for (int z = 0; z < 2; z++) {
                int by = 21 - (x - 1);                  // diagonal sweep
                if (by >= 13) {
                    v->setVoxel(x, by, z, pri);
                    if (by - 1 >= 13) v->setVoxel(x, by - 1, z, pri);
                }
            }
            return v;
        }
        default: return nullptr;
    }
}

VoxelVolume* buildWeaponVolume(WeaponType type, ItemRarity rarity,
                               Voxel pri, Voxel acc) {
    VoxelVolume* v = buildBaseWeaponMesh(type, pri, acc);
    if (v) embellishWeapon(v, type, rarity, acc);
    return v;
}

// Pose (pivot/localPos/localRot/scale) for a weapon held in a given hand.
// Hand convention: rArm for main, lArm for off. Both arms have pivot at
// the shoulder (3,7,3); the hand is roughly at local y=-6 from the
// shoulder origin. Each weapon needs slightly different framing.
static void setWeaponPose(CharacterNode* node, WeaponType type, bool offHand) {
    if (!node) return;
    switch (type) {
        case WeaponType::Sword:
            node->pivot    = glm::vec3(2, 2, 1);
            node->localPos = glm::vec3(0, -6, 0);
            node->localRot = glm::vec3(90, 0, 0);
            break;
        case WeaponType::Shield:
            // Gripped by the strap on the back face. The arm's hand sits
            // at (0, -6, 0) below the shoulder; anchoring the shield's
            // back-centre voxel there places the grip in the hand and the
            // shield's outer face naturally points forward (block stance
            // is handled by the rig's `isBlocking` arm animation).
            node->pivot    = glm::vec3(5, 7, 0);
            node->localPos = glm::vec3(0, -6, 0);
            node->localRot = glm::vec3(0, 0, 0);
            break;
        case WeaponType::Bow:
            // Held vertically along the arm.
            node->pivot    = glm::vec3(1, 12, 1);
            node->localPos = glm::vec3(0, -6, 0);
            node->localRot = glm::vec3(0, 0, 0);
            break;
        case WeaponType::Staff:
            node->pivot    = glm::vec3(1, 4, 1);
            node->localPos = glm::vec3(0, -6, 0);
            node->localRot = glm::vec3(75, 0, 0);
            break;
        case WeaponType::Axe:
            node->pivot    = glm::vec3(1, 2, 1);
            node->localPos = glm::vec3(0, -6, 0);
            node->localRot = glm::vec3(90, 0, offHand ? -10.0f : 10.0f);
            break;
        case WeaponType::Hoe:
        case WeaponType::Scythe:
            // Held like a tool, haft in the hand, head angled forward.
            node->pivot    = glm::vec3(1, 2, 1);
            node->localPos = glm::vec3(0, -6, 0);
            node->localRot = glm::vec3(80, 0, 0);
            break;
        default: break;
    }
    node->scale = glm::vec3(1.0f);
}

static void attachWeaponToNode(CharacterNode* node, WeaponType type,
                                ItemRarity rarity, Voxel pri, Voxel acc,
                                bool isOff) {
    if (!node) return;
    if (node->volume) {
        delete node->volume;
        node->volume = nullptr;
    }
    if (type == WeaponType::None) return;
    VoxelVolume* v = buildWeaponVolume(type, rarity, pri, acc);
    if (v) v->updateMesh();
    node->volume = v;
    setWeaponPose(node, type, isOff);
}

// Build a single-column voxel string for the bow. Curves slightly inward
// when `drawAmount` > 0 so it visibly bends as the player pulls. Caller
// owns the volume.
static VoxelVolume* buildBowStringMesh(float drawAmount, Voxel acc) {
    // 3x24x4 box: same vertical extent as the bow body. The string sits
    // at z=3 (back face of the box) when at rest and the middle voxels
    // creep toward z=0 (player side) proportional to draw.
    const int H = 24;
    VoxelVolume* v = new VoxelVolume(3, H, 4);
    Voxel nock = {220, 200, 140, 255};
    for (int y = 0; y < H; y++) {
        // Bell-curve pull: tips stay anchored at the bow limbs, middle
        // pulls back the most.
        float t = 1.0f - std::abs((float)y - 11.5f) / 11.5f;
        int   z = 3 - (int)(drawAmount * 3.0f * t);
        if (z < 0) z = 0;
        v->setVoxel(1, y, z, acc);
    }
    // A small bright bead at the middle when the player is drawing —
    // suggests a nocked arrow about to fly.
    if (drawAmount > 0.05f) {
        int z = 3 - (int)(drawAmount * 3.0f);
        if (z < 0) z = 0;
        v->setVoxel(1, 11, z, nock);
        v->setVoxel(1, 12, z, nock);
    }
    return v;
}

// Build a back-slung quiver: a small open cylinder of dark leather with
// a few coloured fletchings poking out the top. The quiver is parented
// to the torso so it stays on the player's back through animations.
static VoxelVolume* buildQuiverMesh() {
    VoxelVolume* v = new VoxelVolume(5, 10, 3);
    Voxel leather  = { 95,  55,  30, 255};
    Voxel rim      = { 60,  35,  20, 255};
    Voxel feather  = {230, 220, 220, 255};
    Voxel feather2 = {200, 100, 100, 255};
    Voxel shaft    = {120,  90,  55, 255};
    // Body — 3x6x2 leather tube (open top).
    for (int x = 1; x < 4; x++)
        for (int y = 0; y < 6; y++)
            for (int z = 0; z < 2; z++) {
                bool shell = (x == 1 || x == 3 || z == 0 || z == 1);
                if (!shell) continue;
                v->setVoxel(x, y, z, leather);
            }
    // Reinforced rim at the top of the tube.
    for (int x = 1; x < 4; x++) for (int z = 0; z < 2; z++)
        v->setVoxel(x, 5, z, rim);
    // A few arrow shafts + fletching sticking out.
    v->setVoxel(1, 6, 0, shaft); v->setVoxel(1, 7, 0, shaft);
    v->setVoxel(1, 8, 0, feather);
    v->setVoxel(3, 6, 1, shaft); v->setVoxel(3, 7, 1, shaft);
    v->setVoxel(3, 8, 1, feather2);
    v->setVoxel(2, 6, 0, shaft); v->setVoxel(2, 7, 0, shaft); v->setVoxel(2, 8, 0, shaft);
    v->setVoxel(2, 9, 0, feather);
    return v;
}

static void clearNodeMesh(CharacterNode* node) {
    if (!node) return;
    if (node->volume) { delete node->volume; node->volume = nullptr; }
}

void applyWeaponsToRig(BipedalRig& rig,
                       WeaponType mainType, ItemRarity mainRarity,
                       Voxel mainPri, Voxel mainAcc,
                       WeaponType offType,  ItemRarity offRarity,
                       Voxel offPri,  Voxel offAcc) {
    // Bows are two-handed: the bow body goes on the LEFT arm (off-hand
    // slot), the right arm draws the string. So when the main hand is
    // a bow, we route its mesh to the off-hand node and the right hand
    // stays empty. Any shield equipped on the off-hand is hidden while
    // the bow is out (the visible loadout reflects what you can use
    // right now). Quiver and string nodes come along for the ride.
    bool mainIsBow = (mainType == WeaponType::Bow);

    if (mainIsBow) {
        attachWeaponToNode(rig.sword,   WeaponType::None, ItemRarity::Common,
                            {0,0,0,0}, {0,0,0,0}, false);
        attachWeaponToNode(rig.offHand, mainType, mainRarity, mainPri, mainAcc, true);
        // Drop the natural shield pose — the bow's own pose has just
        // overwritten offHand's transform, so the off-hand sub-tree is
        // now in bow space.
    } else {
        attachWeaponToNode(rig.sword,   mainType, mainRarity, mainPri, mainAcc, false);
        attachWeaponToNode(rig.offHand, offType,  offRarity,  offPri,  offAcc,  true);
    }

    // Bow string node — only carries a mesh while a bow is equipped.
    // Position is in offHand's local space (offHand is the bow node).
    clearNodeMesh(rig.bowString);
    if (mainIsBow && rig.bowString) {
        rig.bowString->volume = buildBowStringMesh(0.0f, mainAcc);
        rig.bowString->volume->updateMesh();
        // Match the bow's pivot so the string sits flush against it.
        rig.bowString->pivot    = glm::vec3(1, 12, 1);
        rig.bowString->localPos = glm::vec3(0, 0, 0);
        rig.bowString->localRot = glm::vec3(0, 0, 0);
        rig.bowString->scale    = glm::vec3(1.0f);
    }

    // Quiver — only visible while a bow is equipped (no point carrying
    // arrows otherwise). Parented to the torso, sitting between the
    // shoulder blades.
    clearNodeMesh(rig.quiver);
    if (mainIsBow && rig.quiver) {
        rig.quiver->volume = buildQuiverMesh();
        rig.quiver->volume->updateMesh();
        // Torso volume is 10x8x8; place the quiver against the back
        // face (z=0), centred between the shoulders, tilted slightly
        // so it slings naturally over one shoulder.
        rig.quiver->pivot    = glm::vec3(2, 3, 1);
        rig.quiver->localPos = glm::vec3(4, 4, -1);
        rig.quiver->localRot = glm::vec3(0, 0, -15);
        rig.quiver->scale    = glm::vec3(1.0f);
    }
}

void applyWeaponsToRig(BipedalRig& rig,
                       const WeaponItem* mainHand,
                       const WeaponItem* offHand) {
    WeaponType mt  = mainHand ? mainHand->getType()    : WeaponType::None;
    ItemRarity mr  = mainHand ? mainHand->rarity       : ItemRarity::Common;
    Voxel      mp  = mainHand ? mainHand->primaryColor : Voxel{0, 0, 0, 0};
    Voxel      ma  = mainHand ? mainHand->accentColor  : Voxel{0, 0, 0, 0};
    WeaponType ot  = offHand  ? offHand->getType()     : WeaponType::None;
    ItemRarity orr = offHand  ? offHand->rarity        : ItemRarity::Common;
    Voxel      op  = offHand  ? offHand->primaryColor  : Voxel{0, 0, 0, 0};
    Voxel      oa  = offHand  ? offHand->accentColor   : Voxel{0, 0, 0, 0};
    applyWeaponsToRig(rig, mt, mr, mp, ma, ot, orr, op, oa);
}
