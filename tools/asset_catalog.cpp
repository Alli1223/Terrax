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
#include "dungeon.h"          // makeDungeon, DungeonKind, DungeonSpawn
#include "castle.h"           // CastleDungeon (overground castle layout)
#include "world.h"            // setWorldSeed, sampleSurfaceSolid
#include "npc.h"              // NPCType (boss / spawn classification)
#include "building.h"         // BuildingKind, RoomType (taxonomy reference)

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

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
        case WeaponType::Dagger: return "Dagger";
        case WeaponType::Mace:   return "Mace";
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
    const WeaponType types[] = { WeaponType::Sword, WeaponType::Axe, WeaponType::Dagger,
                                 WeaponType::Mace, WeaponType::Shield,
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

const char* dungeonName(DungeonKind k) {
    switch (k) {
        case DungeonKind::Crypt:  return "Crypt";
        case DungeonKind::Cave:   return "Cave";
        case DungeonKind::Ruins:  return "Ruins";
        case DungeonKind::Castle: return "Castle";
        default:                  return "?";
    }
}

void writeDungeons(FILE* out) {
    std::fprintf(out,
        "## Dungeons\n\n"
        "Procedural dungeons are a pure function of the world seed, so these are\n"
        "*representative* layouts (one fixed seed per kind × size tier). Footprint\n"
        "is the world-XZ bounding box (W×D blocks); size tier 0 = small … 3 = massive.\n"
        "Castles are over-ground keeps (levels × floor height); the rest are carved\n"
        "underground. Each has exactly one boss room with legendary loot.\n\n"
        "| Kind | Tier | Footprint (W×D) | Rooms | Corridors | Lights | Spawns | Bosses |\n"
        "|------|:----:|:---------------:|------:|----------:|-------:|-------:|-------:|\n");
    const DungeonKind kinds[] = { DungeonKind::Crypt, DungeonKind::Cave,
                                  DungeonKind::Ruins, DungeonKind::Castle };
    for (DungeonKind k : kinds) {
        for (int tier = 0; tier <= 3; ++tier) {
            setWorldSeed(1234u);
            auto d = makeDungeon(k);
            d->sizeTier = tier;
            glm::ivec2 anchor(5000 + tier * 800, -5000 - (int)k * 800);
            int surf = sampleSurfaceSolid(anchor.x, anchor.y);
            d->generateLayout(0xA1CE0000u + (uint32_t)k * 16 + tier, anchor, surf);
            std::vector<DungeonSpawn> spawns;
            d->fillSpawnTable(spawns, 0xA1CE0000u + (uint32_t)k * 16 + tier);
            int bosses = 0;
            for (const auto& s : spawns) if (s.boss) ++bosses;
            int fw = d->bbMax.x - d->bbMin.x + 1;
            int fd = d->bbMax.y - d->bbMin.y + 1;
            std::fprintf(out, "| %s | %d | %d×%d | %d | %d | %d | %d | %d |\n",
                         dungeonName(k), tier, fw, fd,
                         (int)d->rooms.size(), (int)d->corridors.size(),
                         (int)d->lights.size(), (int)spawns.size(), bosses);
        }
    }
    std::fprintf(out, "\nRoom purposes (DungeonRoom.purpose): "
                      "0 hall · 1 entrance · 2 boss · 3 throne · 4 library · "
                      "5 ornament · 6 vault/treasure · 7 prison.\n"
                      "Room shapes: Rect · Circle · Octagon · Cross.\n\n");
}

void writeBuildings(FILE* out) {
    // Documented taxonomy — building footprints are town-layout dependent, so the
    // useful reference is which kinds exist and what interior rooms they hold.
    // Source of truth: BuildingKind / RoomType in building.h (append-only enums).
    struct KindRow { const char* name; const char* role; };
    const KindRow kinds[] = {
        { "Centerpiece", "town focal point — well / market / statue / campfire" },
        { "House",       "residential — furnished living rooms, kitchen, bedroom(s)" },
        { "Farm",        "fenced crop plot worked by farmer NPCs" },
        { "Pub",         "tavern — bar area + dining hall + a guest bedroom" },
        { "Blacksmith",  "forge + workshop + small living quarters" },
        { "MageTower",   "multi-storey tower — alchemy lab, library, bedroom" },
        { "Stable",      "open barn — horse stalls, hay, trough, fenced paddock" },
        { "Chapel",      "tall single nave — pews facing a stone altar" },
        { "Apothecary",  "herbalist's shop — counter, shelves, living quarters" },
        { "Bakery",      "baker's shop — wood-fired oven, counters, bread shelves" },
        { "Watchtower",  "tall narrow stone guard tower with a flat lookout top" },
    };
    std::fprintf(out,
        "## Buildings\n\n"
        "Building kinds town generation can place, and the interior they furnish.\n"
        "Footprints are determined by the town layout (not fixed assets), so this\n"
        "is a taxonomy reference. Source: `BuildingKind` / `RoomType` in building.h.\n\n"
        "| # | Building | Interior / role |\n"
        "|--:|----------|-----------------|\n");
    int i = 0;
    for (const KindRow& r : kinds)
        std::fprintf(out, "| %d | %s | %s |\n", i++, r.name, r.role);
    std::fprintf(out,
        "\n**Room types** (furniture placer): LivingRoom, Kitchen, Bedroom, Study,\n"
        "DiningHall, BarArea, Forge, Workshop, AlchemyLab, Library, Hallway, Stable,\n"
        "Chapel, Apothecary, Bakery.\n\n");
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
    writeDungeons(out);
    writeBuildings(out);

    std::fclose(out);
    std::printf("asset_catalog: wrote catalog (props + weapons + clothing + dungeons + buildings) to %s\n", outPath);
    return 0;
}
