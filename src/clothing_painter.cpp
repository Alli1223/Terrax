#include "clothing_painter.h"
#include "voxel_model.h"
#include <algorithm>

// Derive a small stable variation key from the item's two colours so the
// painter can pick between pattern variants without needing extra state
// over the wire. Same colours always produce the same pattern.
static int colorVariationKey(Voxel pri, Voxel acc) {
    return (pri.r * 31 + pri.g * 17 + pri.b * 13
          + acc.r * 7  + acc.g * 11 + acc.b * 3) & 0xFFFF;
}

// Brighten a voxel toward white — used for the glowing rune / gem accents
// applied to Legendary items. Factor > 1 lightens, < 1 darkens.
static Voxel brightenVoxel(Voxel c, float factor) {
    auto clip = [](int v) { return (uint8_t)std::clamp(v, 0, 255); };
    return { clip((int)(c.r * factor + 30)),
             clip((int)(c.g * factor + 30)),
             clip((int)(c.b * factor + 30)),
             255 };
}

// ---------------------------------------------------------------------------
// Clothing voxel painters
// ---------------------------------------------------------------------------
// Each painter assumes the caller has reset the rig's body to bare skin
// already (so it can blindly overwrite voxels in its target region).
// Tier ladder: Cloth → Leather → Plate, with leather/plate layered on top
// of the cloth silhouette plus tier-specific accents.

// --- Cloth painters --------------------------------------------------------

// Cap covering the top of the head sphere. Brim row in accent colour.
// Three pattern variants for visual variety: short cap, side-flap cap (with
// ear flaps reaching down to y=6), and a tall pointed cap.
static void paintClothHelmetVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    if (!rig.head || !rig.head->volume) return;
    int variant = colorVariationKey(pri, acc) % 3;
    int yMin = (variant == 1) ? 6 : 8;   // side-flap reaches further down
    int yMax = (variant == 2) ? 13 : 12; // pointed cap is taller
    for (int x = 5; x < 19; x++) for (int z = 5; z < 19; z++) {
        for (int y = yMin; y < yMax; y++) {
            float dx = x + 0.5f - 12.0f, dy = y + 0.5f - 6.0f, dz = z + 0.5f - 12.0f;
            // Slightly inflated head sphere so the cap sits on top of any hair.
            if (dx * dx + dy * dy + dz * dz <= 84.0f) {
                Voxel c = (y == yMin) ? acc : pri;
                rig.head->volume->setVoxel(x, y, z, c);
            }
        }
    }
    // Variant 2's tall cap gets a pointed tip in accent.
    if (variant == 2) {
        rig.head->volume->setVoxel(12, 13, 12, acc);
        rig.head->volume->setVoxel(11, 13, 12, acc);
        rig.head->volume->setVoxel(12, 13, 11, acc);
    }
    rig.head->volume->updateMesh();
}

// Mantle around the top of the torso plus shoulder caps on the arms.
static void paintClothShouldersVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    if (rig.torso && rig.torso->volume) {
        for (int x = 0; x < 10; x++) for (int z = 0; z < 8; z++) {
            bool edge = (x == 0 || x == 9 || z == 0 || z == 7);
            rig.torso->volume->setVoxel(x, 7, z, edge ? acc : pri);
        }
        rig.torso->volume->updateMesh();
    }
    for (auto arm : {rig.lArm, rig.rArm}) {
        if (!arm || !arm->volume) continue;
        for (int x = 0; x < 6; x++) for (int z = 0; z < 6; z++) {
            arm->volume->setVoxel(x, 6, z, pri);
            arm->volume->setVoxel(x, 7, z, pri);
        }
        arm->volume->updateMesh();
    }
}

// Tunic over the outer shell of the torso plus sleeves on the upper arms.
// Skips the top row (y=7) so it doesn't fight with a mantle. Three pattern
// variants are selected via the colour-hash so two different cloth tunics
// in the same scheme don't look identical: vertical placket, diagonal sash,
// or V-neck trim.
static void paintClothChestVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    if (rig.torso && rig.torso->volume) {
        // Paint the plain shell first.
        for (int x = 0; x < 10; x++) for (int y = 0; y < 7; y++) for (int z = 0; z < 8; z++) {
            bool shell = (x == 0 || x == 9 || z == 0 || z == 7);
            if (shell) rig.torso->volume->setVoxel(x, y, z, pri);
        }
        // Then overlay one of three accent patterns on the front (z=7).
        int variant = colorVariationKey(pri, acc) % 3;
        switch (variant) {
            case 0:   // Vertical placket
                for (int y = 0; y < 7; y++) {
                    rig.torso->volume->setVoxel(4, y, 7, acc);
                    rig.torso->volume->setVoxel(5, y, 7, acc);
                }
                break;
            case 1:   // Diagonal sash
                for (int i = 0; i < 9; i++) {
                    int x = i, y = i - 1;
                    if (x >= 0 && x < 10 && y >= 0 && y < 7)
                        rig.torso->volume->setVoxel(x, y, 7, acc);
                }
                break;
            case 2:   // V-neck trim
                for (int i = 0; i < 5; i++) {
                    int xl = 4 - i, xr = 5 + i, y = 6 - i;
                    if (y < 0) break;
                    if (xl >= 0) rig.torso->volume->setVoxel(xl, y, 7, acc);
                    if (xr < 10) rig.torso->volume->setVoxel(xr, y, 7, acc);
                }
                break;
        }
        rig.torso->volume->updateMesh();
    }
    for (auto arm : {rig.lArm, rig.rArm}) {
        if (!arm || !arm->volume) continue;
        for (int x = 1; x < 5; x++) for (int z = 1; z < 5; z++) {
            arm->volume->setVoxel(x, 5, z, pri);
            arm->volume->setVoxel(x, 6, z, pri);
        }
        arm->volume->updateMesh();
    }
}

// Trousers over the upper portion of each leg (top 5 rows). Bottom 2 rows
// stay bare so shoes can paint over them.
static void paintClothLegsVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    for (auto leg : {rig.lLeg, rig.rLeg}) {
        if (!leg || !leg->volume) continue;
        for (int x = 0; x < 7; x++) for (int y = 2; y < 7; y++) for (int z = 0; z < 7; z++) {
            bool shell = (x == 0 || x == 6 || z == 0 || z == 6 || y == 6);
            if (!shell) continue;
            // Vertical seam in accent colour for visual interest.
            Voxel c = ((y + z) % 4 == 0) ? acc : pri;
            leg->volume->setVoxel(x, y, z, c);
        }
        leg->volume->updateMesh();
    }
}

// Shoes covering the bottom 2 rows of each leg. Sole row in accent colour.
static void paintClothFeetVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    for (auto leg : {rig.lLeg, rig.rLeg}) {
        if (!leg || !leg->volume) continue;
        for (int x = 0; x < 7; x++) for (int y = 0; y < 2; y++) for (int z = 0; z < 7; z++) {
            leg->volume->setVoxel(x, y, z, y == 0 ? acc : pri);
        }
        leg->volume->updateMesh();
    }
}

// --- Leather painters ------------------------------------------------------
// Leather is the same silhouette as cloth but with rugged stitching/strap
// accents: criss-crossed laces on the chest, a thigh strap on the legs, an
// ankle band on the boots, and a brow strap on the cap.

static void paintLeatherHelmetVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    paintClothHelmetVoxels(rig, pri, acc);
    if (!rig.head || !rig.head->volume) return;
    // Brow strap — a darker band around the front of the cap.
    for (int x = 6; x < 18; x++) {
        rig.head->volume->setVoxel(x, 8, 18, acc);
    }
    rig.head->volume->updateMesh();
}

static void paintLeatherShouldersVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    paintClothShouldersVoxels(rig, pri, acc);
    // Diagonal strap across the chest (front face of torso).
    if (rig.torso && rig.torso->volume) {
        for (int i = 0; i < 6; i++) {
            int x = 2 + i, y = 2 + i;
            if (x < 9 && y < 7) rig.torso->volume->setVoxel(x, y, 7, acc);
        }
        rig.torso->volume->updateMesh();
    }
}

static void paintLeatherChestVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    paintClothChestVoxels(rig, pri, acc);
    if (!rig.torso || !rig.torso->volume) return;
    // Criss-cross laces down the front placket.
    for (int y = 0; y < 7; y += 2) {
        int x0 = 3, x1 = 6;
        if (y < 7) rig.torso->volume->setVoxel(x0, y, 7, acc);
        if (y < 7) rig.torso->volume->setVoxel(x1, y, 7, acc);
    }
    for (int y = 1; y < 7; y += 2) {
        rig.torso->volume->setVoxel(4, y, 7, acc);
        rig.torso->volume->setVoxel(5, y, 7, acc);
    }
    rig.torso->volume->updateMesh();
}

static void paintLeatherLegsVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    paintClothLegsVoxels(rig, pri, acc);
    // Horizontal thigh strap at y=4 (mid-thigh) — accent ring around the leg.
    for (auto leg : {rig.lLeg, rig.rLeg}) {
        if (!leg || !leg->volume) continue;
        for (int x = 0; x < 7; x++) for (int z = 0; z < 7; z++) {
            bool shell = (x == 0 || x == 6 || z == 0 || z == 6);
            if (shell) leg->volume->setVoxel(x, 4, z, acc);
        }
        leg->volume->updateMesh();
    }
}

static void paintLeatherFeetVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    // Boots — taller than cloth shoes (cover y=0..2) with an ankle band.
    for (auto leg : {rig.lLeg, rig.rLeg}) {
        if (!leg || !leg->volume) continue;
        for (int x = 0; x < 7; x++) for (int y = 0; y < 3; y++) for (int z = 0; z < 7; z++) {
            Voxel c = (y == 0) ? acc : pri;
            leg->volume->setVoxel(x, y, z, c);
        }
        // Ankle band at y=2.
        for (int x = 0; x < 7; x++) for (int z = 0; z < 7; z++) {
            bool shell = (x == 0 || x == 6 || z == 0 || z == 6);
            if (shell) leg->volume->setVoxel(x, 2, z, acc);
        }
        leg->volume->updateMesh();
    }
}

// --- Plate painters --------------------------------------------------------
// Plate is bulkier than cloth/leather: full-face helm, oversized pauldrons,
// rivet pattern at corners, taller sabatons.

static void paintPlateHelmetVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    if (!rig.head || !rig.head->volume) return;
    // Cover the entire head sphere (y=0..11) — a full helm hides the face.
    for (int x = 5; x < 19; x++) for (int z = 5; z < 19; z++) {
        for (int y = 0; y < 12; y++) {
            float dx = x + 0.5f - 12.0f, dy = y + 0.5f - 6.0f, dz = z + 0.5f - 12.0f;
            if (dx * dx + dy * dy + dz * dz <= 84.0f) {
                rig.head->volume->setVoxel(x, y, z, pri);
            }
        }
    }
    // Visor slit across the front at eye height.
    for (int x = 8; x < 16; x++) {
        rig.head->volume->setVoxel(x, 7, 18, acc);
        rig.head->volume->setVoxel(x, 6, 18, acc);
    }
    // Rivets at the brow corners.
    for (int x : {7, 16}) rig.head->volume->setVoxel(x, 9, 18, acc);
    rig.head->volume->updateMesh();
}

static void paintPlateShouldersVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    paintClothShouldersVoxels(rig, pri, acc);
    // Pauldrons extend a row further down the upper arms.
    for (auto arm : {rig.lArm, rig.rArm}) {
        if (!arm || !arm->volume) continue;
        for (int x = 0; x < 6; x++) for (int z = 0; z < 6; z++) {
            arm->volume->setVoxel(x, 5, z, pri);
        }
        // Rivets at the cap corners.
        for (int rx : {0, 5}) for (int rz : {0, 5})
            arm->volume->setVoxel(rx, 7, rz, acc);
        arm->volume->updateMesh();
    }
}

static void paintPlateChestVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    paintClothChestVoxels(rig, pri, acc);
    if (!rig.torso || !rig.torso->volume) return;
    // Embossed centre ridge in accent — vertical line.
    for (int y = 1; y < 6; y++) rig.torso->volume->setVoxel(4, y, 7, acc);
    for (int y = 1; y < 6; y++) rig.torso->volume->setVoxel(5, y, 7, acc);
    // Corner rivets — sprinkle the chest with metallic dots.
    int rivets[][2] = { {1, 1}, {1, 5}, {8, 1}, {8, 5}, {1, 3}, {8, 3} };
    for (auto& r : rivets) rig.torso->volume->setVoxel(r[0], r[1], 7, acc);
    rig.torso->volume->updateMesh();
}

static void paintPlateLegsVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    paintClothLegsVoxels(rig, pri, acc);
    for (auto leg : {rig.lLeg, rig.rLeg}) {
        if (!leg || !leg->volume) continue;
        // Rivets at the leg corners.
        int corners[][2] = { {0, 0}, {0, 6}, {6, 0}, {6, 6} };
        for (auto& c : corners) {
            leg->volume->setVoxel(c[0], 3, c[1], acc);
            leg->volume->setVoxel(c[0], 5, c[1], acc);
        }
        leg->volume->updateMesh();
    }
}

static void paintPlateFeetVoxels(BipedalRig& rig, Voxel pri, Voxel acc) {
    // Sabatons — taller and pointier than cloth shoes.
    for (auto leg : {rig.lLeg, rig.rLeg}) {
        if (!leg || !leg->volume) continue;
        for (int x = 0; x < 7; x++) for (int y = 0; y < 3; y++) for (int z = 0; z < 7; z++) {
            leg->volume->setVoxel(x, y, z, pri);
        }
        // Toe accent.
        for (int x = 1; x < 6; x++) leg->volume->setVoxel(x, 1, 6, acc);
        // Sole row.
        for (int x = 0; x < 7; x++) for (int z = 0; z < 7; z++)
            leg->volume->setVoxel(x, 0, z, acc);
        leg->volume->updateMesh();
    }
}

// --- Rarity embellishments -------------------------------------------------
// Applied AFTER the base + tier painter. Rare adds a few extra trim
// voxels; Legendary adds glowing runes, gem accents, and bright seams that
// derive their colour from the item's accent (brightened toward white).

static void paintRareEmbellishment(BipedalRig& rig, EquipSlot slot, Voxel acc) {
    Voxel trim = brightenVoxel(acc, 1.15f);
    switch (slot) {
        case EquipSlot::Helmet:
            // Subtle band of trim around the brim front.
            if (rig.head && rig.head->volume) {
                for (int x = 8; x <= 15; x++)
                    rig.head->volume->setVoxel(x, 7, 18, trim);
                rig.head->volume->updateMesh();
            }
            break;
        case EquipSlot::Chest:
            // Trim line across the waist (y=0 front row).
            if (rig.torso && rig.torso->volume) {
                for (int x = 1; x < 9; x++) rig.torso->volume->setVoxel(x, 0, 7, trim);
                rig.torso->volume->updateMesh();
            }
            break;
        case EquipSlot::Shoulders:
            // Tassels at the hem of the mantle (back row of torso top).
            if (rig.torso && rig.torso->volume) {
                for (int x : {1, 4, 5, 8})
                    rig.torso->volume->setVoxel(x, 6, 0, trim);
                rig.torso->volume->updateMesh();
            }
            break;
        case EquipSlot::Legs:
            // Side stripe down the outer edge of each leg.
            for (auto leg : {rig.lLeg, rig.rLeg}) {
                if (!leg || !leg->volume) continue;
                for (int y = 2; y < 6; y++) leg->volume->setVoxel(6, y, 3, trim);
                leg->volume->updateMesh();
            }
            break;
        case EquipSlot::Feet:
            // Highlight on the toe row.
            for (auto leg : {rig.lLeg, rig.rLeg}) {
                if (!leg || !leg->volume) continue;
                for (int x = 1; x < 6; x++) leg->volume->setVoxel(x, 1, 6, trim);
                leg->volume->updateMesh();
            }
            break;
        default: break;
    }
}

static void paintLegendaryEmbellishment(BipedalRig& rig, EquipSlot slot, Voxel acc) {
    Voxel glow = brightenVoxel(acc, 1.8f);
    Voxel gem  = brightenVoxel(acc, 2.2f);
    switch (slot) {
        case EquipSlot::Helmet:
            if (rig.head && rig.head->volume) {
                // Gem at the crown — a 2x2 cluster catching the light.
                for (int x = 11; x <= 12; x++) for (int z = 11; z <= 12; z++)
                    rig.head->volume->setVoxel(x, 11, z, gem);
                // Glowing trim around the brow.
                for (int x = 7; x <= 16; x++)
                    rig.head->volume->setVoxel(x, 8, 18, glow);
                rig.head->volume->updateMesh();
            }
            break;
        case EquipSlot::Chest:
            if (rig.torso && rig.torso->volume) {
                // Runic glyph centred on the chest (3x3 cross pattern).
                int cx = 4, cy = 3;
                for (int x = cx; x <= cx + 1; x++)
                    rig.torso->volume->setVoxel(x, cy, 7, glow);
                rig.torso->volume->setVoxel(cx,     cy - 1, 7, glow);
                rig.torso->volume->setVoxel(cx + 1, cy + 1, 7, glow);
                rig.torso->volume->setVoxel(cx - 1, cy,     7, glow);
                rig.torso->volume->setVoxel(cx + 2, cy,     7, glow);
                rig.torso->volume->updateMesh();
            }
            break;
        case EquipSlot::Shoulders:
            // Glowing gems on each pauldron.
            for (auto arm : {rig.lArm, rig.rArm}) {
                if (!arm || !arm->volume) continue;
                arm->volume->setVoxel(2, 7, 2, gem);
                arm->volume->setVoxel(3, 7, 2, gem);
                arm->volume->setVoxel(2, 7, 3, gem);
                arm->volume->setVoxel(3, 7, 3, gem);
                arm->volume->updateMesh();
            }
            break;
        case EquipSlot::Legs:
            // Glowing seams down the outer side of each leg.
            for (auto leg : {rig.lLeg, rig.rLeg}) {
                if (!leg || !leg->volume) continue;
                for (int y = 2; y < 6; y++) {
                    leg->volume->setVoxel(0, y, 3, glow);
                    leg->volume->setVoxel(6, y, 3, glow);
                }
                leg->volume->updateMesh();
            }
            break;
        case EquipSlot::Feet:
            // Glowing rims at the top of each boot + a glow on the toe.
            for (auto leg : {rig.lLeg, rig.rLeg}) {
                if (!leg || !leg->volume) continue;
                for (int x = 0; x < 7; x++) for (int z = 0; z < 7; z++) {
                    bool shell = (x == 0 || x == 6 || z == 0 || z == 6);
                    if (shell) leg->volume->setVoxel(x, 2, z, glow);
                }
                for (int x = 2; x < 5; x++) leg->volume->setVoxel(x, 1, 6, gem);
                leg->volume->updateMesh();
            }
            break;
        default: break;
    }
}

// --- Dispatch --------------------------------------------------------------

void paintClothingOnto(BipedalRig& rig, ClothingTier tier, ItemRarity rarity,
                       EquipSlot slot, Voxel pri, Voxel acc) {
    switch (tier) {
        case ClothingTier::Cloth:
            switch (slot) {
                case EquipSlot::Helmet:    paintClothHelmetVoxels(rig, pri, acc); break;
                case EquipSlot::Shoulders: paintClothShouldersVoxels(rig, pri, acc); break;
                case EquipSlot::Chest:     paintClothChestVoxels(rig, pri, acc); break;
                case EquipSlot::Legs:      paintClothLegsVoxels(rig, pri, acc); break;
                case EquipSlot::Feet:      paintClothFeetVoxels(rig, pri, acc); break;
                default: break;
            }
            break;
        case ClothingTier::Leather:
            switch (slot) {
                case EquipSlot::Helmet:    paintLeatherHelmetVoxels(rig, pri, acc); break;
                case EquipSlot::Shoulders: paintLeatherShouldersVoxels(rig, pri, acc); break;
                case EquipSlot::Chest:     paintLeatherChestVoxels(rig, pri, acc); break;
                case EquipSlot::Legs:      paintLeatherLegsVoxels(rig, pri, acc); break;
                case EquipSlot::Feet:      paintLeatherFeetVoxels(rig, pri, acc); break;
                default: break;
            }
            break;
        case ClothingTier::Plate:
            switch (slot) {
                case EquipSlot::Helmet:    paintPlateHelmetVoxels(rig, pri, acc); break;
                case EquipSlot::Shoulders: paintPlateShouldersVoxels(rig, pri, acc); break;
                case EquipSlot::Chest:     paintPlateChestVoxels(rig, pri, acc); break;
                case EquipSlot::Legs:      paintPlateLegsVoxels(rig, pri, acc); break;
                case EquipSlot::Feet:      paintPlateFeetVoxels(rig, pri, acc); break;
                default: break;
            }
            break;
    }
    // Rarity pass — extra accents on Rare, glow + runes + gems on Legendary.
    if (rarity == ItemRarity::Rare) {
        paintRareEmbellishment(rig, slot, acc);
    } else if (rarity == ItemRarity::Legendary) {
        paintLegendaryEmbellishment(rig, slot, acc);
    }
}

void paintClothingOnto(BipedalRig& rig, const ClothingItem& item) {
    paintClothingOnto(rig, item.getTier(), item.rarity, item.getSlot(),
                      item.primaryColor, item.accentColor);
}
