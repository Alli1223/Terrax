// Asset-catalog generator (Track A1).
//
// A headless tool that walks the prop registry, bakes each prop's voxel volume
// (no GL upload — links against the GL stub like the test build), measures its
// tight bounding box and voxel count, and emits a Markdown catalog. The catalog
// is a reference for world-population work: when furnishing a dungeon or house
// you can look up which props exist, how big they are, and how they behave.
//
// Build + run:  make catalog   (writes docs/ASSET_CATALOG.md)

#include "prop_registry.h"
#include "voxel_model.h"
#include "interactable.h"
#include "items.h"            // WeaponType, ClothingTier, EquipSlot, ItemRarity
#include "weapon_builder.h"   // buildWeaponVolume()
#include "item_generator.h"   // armorSetCatalog()

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

const char* interactName(InteractAction a) {
    switch (a) {
        case InteractAction::SitChair: return "Sit";
        case InteractAction::LieBed:   return "Lie down";
        default:                       return "-";
    }
}

// Tight footprint of the non-empty (alpha > 0) voxels.
struct Footprint {
    int w = 0, d = 0, h = 0;   // X (width), Z (depth), Y (height)
    long count = 0;            // number of filled voxels
};

Footprint measure(const VoxelVolume* v) {
    Footprint f;
    int minx = v->sizeX, miny = v->sizeY, minz = v->sizeZ;
    int maxx = -1, maxy = -1, maxz = -1;
    for (int x = 0; x < v->sizeX; ++x)
        for (int y = 0; y < v->sizeY; ++y)
            for (int z = 0; z < v->sizeZ; ++z) {
                if (v->getVoxel(x, y, z).a == 0) continue;
                ++f.count;
                minx = std::min(minx, x); maxx = std::max(maxx, x);
                miny = std::min(miny, y); maxy = std::max(maxy, y);
                minz = std::min(minz, z); maxz = std::max(maxz, z);
            }
    if (f.count > 0) {
        f.w = maxx - minx + 1;
        f.h = maxy - miny + 1;
        f.d = maxz - minz + 1;
    }
    return f;
}

// Stable enum labels. Kept local (rather than linking the heavy items.cpp
// closure that defines weaponTypeName/tierName) — these enums are append-only.
const char* weaponName(WeaponType t) {
    switch (t) {
        case WeaponType::Sword:  return "Sword";
        case WeaponType::Shield: return "Shield";
        case WeaponType::Bow:    return "Bow";
        case WeaponType::Staff:  return "Staff";
        case WeaponType::Axe:    return "Axe";
        case WeaponType::Hoe:    return "Hoe (farm tool)";
        case WeaponType::Scythe: return "Scythe (farm tool)";
        default:                 return "None";
    }
}
const char* weaponHand(WeaponType t) {
    return (t == WeaponType::Shield) ? "Off-hand" : "Main-hand";
}
const char* tierLabel(ClothingTier t) {
    switch (t) {
        case ClothingTier::Cloth:   return "Cloth";
        case ClothingTier::Leather: return "Leather";
        case ClothingTier::Plate:   return "Plate";
        default:                    return "?";
    }
}

void writeProps(FILE* out) {
    const auto& reg = propRegistry();
    std::fprintf(out,
        "## Props\n\n"
        "Every entry in the prop registry, with its **footprint** (tight bounding\n"
        "box of filled voxels, in model-grid units: W = X, D = Z, H = Y), filled\n"
        "voxel count, and behaviour metadata. Use this when furnishing dungeons,\n"
        "houses and towns so you pick props that fit the space and behave right.\n\n"
        "| # | Prop | W×D×H | Voxels | Interact | Emits light | Sways |\n"
        "|--:|------|:-----:|-------:|:--------:|:-----------:|:-----:|\n");
    long totalVox = 0;
    int rows = 0;
    for (const auto& def : reg) {
        std::unique_ptr<VoxelVolume> v(def.build ? def.build() : nullptr);
        Footprint f = v ? measure(v.get()) : Footprint{};
        totalVox += f.count;
        ++rows;
        std::fprintf(out, "| %d | %s | %d×%d×%d | %ld | %s | %s | %s |\n",
                     (int)def.type, def.name, f.w, f.d, f.h, f.count,
                     interactName(def.interact.action),
                     def.light.emits ? (def.light.nightOnly ? "night" : "yes") : "-",
                     def.swaysInWind ? "yes" : "-");
    }
    std::fprintf(out, "\n**%d props · %ld filled voxels total.**\n\n", rows, totalVox);
}

void writeWeapons(FILE* out) {
    std::fprintf(out,
        "## Weapons\n\n"
        "Held-weapon meshes from `buildWeaponVolume()`, measured per rarity\n"
        "(Rare/Legendary add accents + glow that can enlarge the footprint).\n"
        "Dimensions are model-grid voxels (W = X, D = Z, H = Y).\n\n"
        "| Weapon | Slot | Rarity | W×D×H | Voxels |\n"
        "|--------|:----:|:------:|:-----:|-------:|\n");
    const WeaponType types[] = { WeaponType::Sword, WeaponType::Axe, WeaponType::Shield,
                                 WeaponType::Bow, WeaponType::Staff,
                                 WeaponType::Hoe, WeaponType::Scythe };
    const ItemRarity rarities[] = { ItemRarity::Common, ItemRarity::Rare, ItemRarity::Legendary };
    const char* rarityStr[] = { "Common", "Rare", "Legendary" };
    const Voxel primary{ 180, 180, 200, 255 };
    const Voxel accent { 120,  80,  40, 255 };
    for (WeaponType t : types) {
        for (int r = 0; r < 3; ++r) {
            std::unique_ptr<VoxelVolume> v(buildWeaponVolume(t, rarities[r], primary, accent));
            Footprint f = v ? measure(v.get()) : Footprint{};
            std::fprintf(out, "| %s | %s | %s | %d×%d×%d | %ld |\n",
                         weaponName(t), weaponHand(t), rarityStr[r],
                         f.w, f.d, f.h, f.count);
        }
    }
    std::fprintf(out, "\n");
}

void writeClothingAndSets(FILE* out) {
    std::fprintf(out,
        "## Clothing\n\n"
        "Worn armour is **painted onto the player/NPC rig** (clothing_painter.cpp),\n"
        "so it has no free-standing footprint; the world-drop pickup is a fixed\n"
        "6×4×6 \"folded garment\" coloured by the item's palette. Pieces are\n"
        "generated per **slot** × **material tier**, gated by role:\n\n"
        "- **Slots:** Helmet, Shoulders, Chest, Legs, Feet (+ Main-hand / Off-hand for weapons).\n"
        "- **Tiers:** Cloth (DPS/Healer), Leather (DPS), Plate (Tank) — and anything lighter.\n"
        "- **Rarity:** Common / Rare / Legendary — scales stats + adds runes/gems/glow.\n\n"
        "### Themed armour sets\n\n"
        "Matching named sets the generator can roll (palette shared across every\n"
        "piece). Source of truth: `KNOWN_SETS` in item_generator.cpp.\n\n"
        "| Set | Tier | Primary RGB | Accent RGB |\n"
        "|-----|:----:|:-----------:|:----------:|\n");
    for (const ArmorSetInfo& s : armorSetCatalog()) {
        std::fprintf(out, "| %s | %s | %d,%d,%d | %d,%d,%d |\n",
                     s.name, tierLabel(s.tier),
                     s.primary.r, s.primary.g, s.primary.b,
                     s.accent.r, s.accent.g, s.accent.b);
    }
    std::fprintf(out, "\n**%d themed sets.**\n\n", (int)armorSetCatalog().size());
}

}  // namespace

int main(int argc, char** argv) {
    const char* outPath = (argc > 1) ? argv[1] : "docs/ASSET_CATALOG.md";

    FILE* out = std::fopen(outPath, "w");
    if (!out) {
        std::fprintf(stderr, "asset_catalog: cannot open %s for writing\n", outPath);
        return 1;
    }

    std::fprintf(out,
        "# Terrax Asset Catalog\n\n"
        "_Generated by `make catalog` (tools/asset_catalog.cpp). Do not edit by hand._\n\n"
        "A reference for world-population work — which props, weapons and clothing\n"
        "exist, their dimensions, and their behaviour metadata.\n\n");

    writeProps(out);
    writeWeapons(out);
    writeClothingAndSets(out);

    std::fclose(out);
    std::printf("asset_catalog: wrote catalog (props + weapons + clothing) to %s\n", outPath);
    return 0;
}
