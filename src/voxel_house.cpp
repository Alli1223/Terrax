// The editor house generator: generateHouseGrid bakes a templated house into a
// HOUSE_VX*HOUSE_VY*HOUSE_VZ BlockType grid, and HouseModel wraps that grid as a
// drawable VoxelVolume for the in-game house editor. Split out of
// voxel_model.cpp. See voxel_model.h for the public surface.
#include "voxel_model.h"
#include "building.h"
#include <iostream>
#include <algorithm>
#include <memory>
#include <random>

// --- House generator ---------------------------------------------------------

Voxel houseBlockColor(BlockType t) {
    switch (t) {
        case BlockType::Wood:      return {150, 103,  58, 255};
        case BlockType::Stone:     return {128, 128, 134, 255};
        case BlockType::Dirt:      return {120,  85,  55, 255};
        case BlockType::Grass:     return { 96, 158,  72, 255};
        case BlockType::Sand:      return {221, 205, 152, 255};
        case BlockType::Sandstone: return {223, 209, 162, 255};
        case BlockType::Gravel:    return {116, 110, 104, 255};
        case BlockType::Snow:      return {243, 246, 252, 255};
        case BlockType::Ice:       return {165, 208, 232, 255};
        case BlockType::Glowstone: return {255, 224, 138, 255};
        case BlockType::Lantern:   return {255, 206, 120, 255};
        case BlockType::Leaves:    return { 66, 122,  52, 255};
        case BlockType::Cactus:    return { 84, 134,  62, 255};
        case BlockType::Glass:     return {200, 225, 238, 255};
        default: {
            int pidx = (int)t - (int)BlockType::PaintFirst;
            if (pidx >= 0 && pidx < PAINT_COUNT)
                return { PAINT_PALETTE[pidx].r, PAINT_PALETTE[pidx].g,
                         PAINT_PALETTE[pidx].b, 255 };
            return {0, 0, 0, 0};   // Air / unknown
        }
    }
}

HouseModel::HouseModel() {
    blocks.assign((size_t)HOUSE_VX * HOUSE_VY * HOUSE_VZ, BlockType::Air);
    volume = new VoxelVolume(HOUSE_VX, HOUSE_VY, HOUSE_VZ);
    rebuild();
}

HouseModel::~HouseModel() {
    delete volume;
}

BlockType HouseModel::get(int x, int y, int z) const {
    if (x < 0 || x >= HOUSE_VX || y < 0 || y >= HOUSE_VY || z < 0 || z >= HOUSE_VZ)
        return BlockType::Air;
    return blocks[((size_t)z * HOUSE_VY + y) * HOUSE_VX + x];
}

void HouseModel::set(int x, int y, int z, BlockType t) {
    if (x < 0 || x >= HOUSE_VX || y < 0 || y >= HOUSE_VY || z < 0 || z >= HOUSE_VZ)
        return;
    blocks[((size_t)z * HOUSE_VY + y) * HOUSE_VX + x] = t;
}

// Delegates to the polymorphic Building generator family and stamps the
// tight-cropped result into the fixed-size HOUSE_VX*VY*VZ grid the editor
// uses for display. Centred in X/Z so rotations look sensible.
//
// Template id mapping:
//   0..9  → HouseBuilding (Bungalow, Two-Story, Cottage, Tower, Cabin,
//           Longhouse, Townhouse, Manor, Hall, Keep)
//   10    → HouseBuilding template 10 = Norse Longhouse
//   11    → HouseBuilding template 11 = Norse Mead Hall
//   12    → PubBuilding
//   13    → BlacksmithBuilding
//   14    → MageTowerBuilding
void generateHouseGrid(int templateType, int roofType, int material,
                       std::vector<BlockType>& blocks) {
    blocks.assign((size_t)HOUSE_VX * HOUSE_VY * HOUSE_VZ, BlockType::Air);

    std::vector<uint8_t> raw;
    std::vector<Room>    rooms;
    int sx = 0, sy = 0, sz = 0, dx = 0, dz = -1;

    // Pick the right Building subclass for the requested template id. Each
    // subclass already encapsulates its size + room layout so the editor and
    // the town stamper share a single source of truth.
    std::unique_ptr<Building> gen;
    if (templateType == 12) {
        gen = std::make_unique<PubBuilding>(material, roofType);
    } else if (templateType == 13) {
        gen = std::make_unique<BlacksmithBuilding>(material, roofType);
    } else if (templateType == 14) {
        gen = std::make_unique<MageTowerBuilding>(material, /*floors=*/3);
    } else {
        gen = std::make_unique<HouseBuilding>(templateType, roofType, material);
    }
    gen->generate(0, raw, rooms, sx, sy, sz, dx, dz);
    if (sx <= 0 || sy <= 0 || sz <= 0) return;

    const int ox = std::max(0, (HOUSE_VX - sx) / 2);
    const int oz = std::max(0, (HOUSE_VZ - sz) / 2);
    const int copyX = std::min(sx, HOUSE_VX - ox);
    const int copyY = std::min(sy, HOUSE_VY);
    const int copyZ = std::min(sz, HOUSE_VZ - oz);
    for (int y = 0; y < copyY; y++)
        for (int z = 0; z < copyZ; z++)
            for (int x = 0; x < copyX; x++) {
                uint8_t b = raw[((size_t)y * sz + z) * sx + x];
                blocks[((size_t)(oz + z) * HOUSE_VY + y) * HOUSE_VX + (ox + x)]
                    = (BlockType)b;
            }
}

// Legacy in-place generator (kept available but no longer used; preserved so
// future direct callers can opt into it without going through HouseBuilding).
[[maybe_unused]] void generateHouseGridLegacy(int templateType, int roofType, int material,
                       std::vector<BlockType>& blocks) {
    blocks.assign((size_t)HOUSE_VX * HOUSE_VY * HOUSE_VZ, BlockType::Air);
    auto set = [&](int x, int y, int z, BlockType t) {
        if (x < 0 || x >= HOUSE_VX || y < 0 || y >= HOUSE_VY ||
            z < 0 || z >= HOUSE_VZ) return;
        blocks[((size_t)z * HOUSE_VY + y) * HOUSE_VX + x] = t;
    };

    // Material -> painted wall / roof colours (indices into PAINT_PALETTE).
    auto paint = [](int i) { return (BlockType)((int)BlockType::PaintFirst + i); };
    BlockType wallB, roofB;
    switch (material) {
        case 1: wallB = paint(0);  roofB = paint(7);  break;  // Cottage:   white / brick red
        case 2: wallB = paint(3);  roofB = paint(4);  break;  // Stone:     slate / charcoal
        case 3: wallB = paint(2);  roofB = paint(20); break;  // Manor:     light grey / navy
        case 4: wallB = paint(12); roofB = paint(4);  break;  // Cabin:     chestnut / charcoal
        case 5: wallB = paint(13); roofB = paint(6);  break;  // Sandstone: sand / terracotta
        case 6: wallB = paint(15); roofB = paint(16); break;  // Forest:    sage / forest green
        case 7: wallB = paint(0);  roofB = paint(21); break;  // Coastal:   white / steel blue
        case 8: wallB = paint(6);  roofB = paint(9);  break;  // Autumn:    terracotta / rust
        case 9: wallB = paint(23); roofB = paint(22); break;  // Plum:      dusty rose / plum
        default:wallB = paint(13); roofB = paint(12); break;  // Timber:    sand / chestnut
    }
    const BlockType foundationB = BlockType::Stone;
    const BlockType floorB      = paint(12);          // chestnut floorboards
    const BlockType windowB     = BlockType::Glass;   // see-through glass windows
    const BlockType chimneyB    = paint(7);           // brick chimney stack

    // Template -> floor count, storey height, footprint margins (X and Z
    // separately, which gives non-square footprints).
    int floors, floorH, marginX, marginZ;
    switch (templateType) {
        case 1: floors=2; floorH= 6; marginX=4; marginZ=4; break;  // Two-Story
        case 2: floors=1; floorH= 6; marginX=7; marginZ=7; break;  // Cottage
        case 3: floors=4; floorH= 5; marginX=7; marginZ=7; break;  // Tower
        case 4: floors=1; floorH= 6; marginX=5; marginZ=8; break;  // Cabin
        case 5: floors=1; floorH= 7; marginX=2; marginZ=7; break;  // Longhouse
        case 6: floors=3; floorH= 6; marginX=8; marginZ=6; break;  // Townhouse
        case 7: floors=2; floorH= 6; marginX=2; marginZ=5; break;  // Manor
        case 8: floors=1; floorH=11; marginX=3; marginZ=3; break;  // Hall
        case 9: floors=3; floorH= 6; marginX=4; marginZ=4; break;  // Keep
        default:floors=1; floorH= 7; marginX=3; marginZ=3; break;  // Bungalow
    }
    const int x0 = marginX, x1 = HOUSE_VX - 1 - marginX;
    const int z0 = marginZ, z1 = HOUSE_VZ - 1 - marginZ;
    const int wallH = floors * floorH;            // walls span y in [1, wallH]

    auto box = [&](int ax, int bx, int ay, int by, int az, int bz, BlockType t) {
        for (int x = ax; x <= bx; x++)
            for (int y = ay; y <= by; y++)
                for (int z = az; z <= bz; z++)
                    set(x, y, z, t);
    };

    // Foundation, solid shell, hollow interior.
    box(x0 - 1, x1 + 1, 0, 0, z0 - 1, z1 + 1, foundationB);
    box(x0, x1, 1, wallH, z0, z1, wallB);
    box(x0 + 1, x1 - 1, 1, wallH - 1, z0 + 1, z1 - 1, BlockType::Air);

    // Floors — the ground-floor boards sit directly on the foundation so they
    // are level with the bottom of the doorway; each upper storey gets a slab.
    box(x0 + 1, x1 - 1, 0, 0, z0 + 1, z1 - 1, floorB);
    for (int f = 1; f < floors; f++)
        box(x0 + 1, x1 - 1, f * floorH, f * floorH, z0 + 1, z1 - 1, floorB);

    // Interior staircases — one straight, 2-wide flight per upper storey
    // against the left wall. Each step rises a single block (walkable without
    // jumping), and a matching slot is cut in the slab above to climb through.
    // Flights alternate between two adjacent lanes so the well of one does not
    // undercut the foot of the next.
    for (int f = 1; f < floors; f++) {
        const int xStair = (f % 2 == 1) ? x0 + 1 : x0 + 3;   // 2-wide lane
        const int yL     = (f - 1) * floorH + 1;   // walkable level of the floor below
        for (int s = 0; s < floorH - 1; s++)
            box(xStair, xStair + 1, yL + s, yL + s, z0 + 2 + s, z0 + 2 + s, floorB);
        box(xStair, xStair + 1, f * floorH, f * floorH, z0 + 2, z0 + floorH, BlockType::Air);
    }

    // Door — an opening centred on the front wall (z = z0), ground floor.
    const int dcx = (x0 + x1) / 2;
    box(dcx - 1, dcx + 1, 1, 4, z0, z0, BlockType::Air);

    // Windows — 2x2 panels punched through the walls, one row per floor.
    auto win = [&](int cx, int cy, int cz, bool alongX) {
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++)
                set(alongX ? cx + a : cx, cy + b, alongX ? cz : cz + a, windowB);
    };
    const int xspan = x1 - x0, zspan = z1 - z0;
    for (int f = 0; f < floors; f++) {
        const int wy = f * floorH + 2;
        if (xspan >= 10) {
            for (int cx : {x0 + xspan / 4, x0 + 3 * xspan / 4 - 1}) {
                if (!(f == 0 && cx >= dcx - 2 && cx <= dcx + 2))
                    win(cx, wy, z0, true);
                win(cx, wy, z1, true);
            }
        } else {
            const int cx = (x0 + x1) / 2 - 1;
            if (f != 0) win(cx, wy, z0, true);
            win(cx, wy, z1, true);
        }
        if (zspan >= 10) {
            for (int cz : {z0 + zspan / 4, z0 + 3 * zspan / 4 - 1}) {
                win(x0, wy, cz, false);
                win(x1, wy, cz, false);
            }
        } else {
            const int cz = (z0 + z1) / 2 - 1;
            win(x0, wy, cz, false);
            win(x1, wy, cz, false);
        }
    }

    // Roof — sits one row above the walls, overhanging the footprint by one
    // block. Gabled and hipped roofs run their ridge along the longer wall.
    const int rx0 = x0 - 1, rx1 = x1 + 1, rz0 = z0 - 1, rz1 = z1 + 1;
    const int ry = wallH + 1;
    const bool ridgeX = (xspan >= zspan);
    int roofTopY = ry;
    if (roofType == 0) {                              // Flat
        box(rx0, rx1, ry, ry, rz0, rz1, roofB);
    } else if (roofType == 3) {                       // Pyramid
        int ax0 = rx0, ax1 = rx1, az0 = rz0, az1 = rz1, h = 0;
        while (ax0 <= ax1 && az0 <= az1) {
            box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
            roofTopY = ry + h;
            ax0++; ax1--; az0++; az1--; h++;
        }
    } else if (roofType == 2) {                       // Hipped
        int ax0 = rx0, ax1 = rx1, az0 = rz0, az1 = rz1, h = 0;
        if (ridgeX) {
            while (az0 <= az1) {
                box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
                roofTopY = ry + h;
                az0++; az1--;
                if (ax1 - ax0 > 4) { ax0++; ax1--; }
                h++;
            }
        } else {
            while (ax0 <= ax1) {
                box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
                roofTopY = ry + h;
                ax0++; ax1--;
                if (az1 - az0 > 4) { az0++; az1--; }
                h++;
            }
        }
    } else {                                          // Gabled (1)
        int h = 0;
        if (ridgeX) {
            int az0 = rz0, az1 = rz1;
            while (az0 <= az1) {
                box(rx0, rx1, ry + h, ry + h, az0, az1, roofB);
                const int g0 = std::max(az0, z0), g1 = std::min(az1, z1);
                box(x0, x0, ry + h, ry + h, g0, g1, wallB);   // triangular gable ends
                box(x1, x1, ry + h, ry + h, g0, g1, wallB);
                roofTopY = ry + h;
                az0++; az1--; h++;
            }
        } else {
            int ax0 = rx0, ax1 = rx1;
            while (ax0 <= ax1) {
                box(ax0, ax1, ry + h, ry + h, rz0, rz1, roofB);
                const int g0 = std::max(ax0, x0), g1 = std::min(ax1, x1);
                box(g0, g1, ry + h, ry + h, z0, z0, wallB);   // triangular gable ends
                box(g0, g1, ry + h, ry + h, z1, z1, wallB);
                roofTopY = ry + h;
                ax0++; ax1--; h++;
            }
        }
    }

    // Chimney — a brick stack that starts at the roofline and rises out past
    // the roof peak (it does not run down into the interior).
    if (xspan >= 7 && zspan >= 7) {
        const int cx = x1 - 3, cz = z1 - 3;
        box(cx, cx + 1, wallH, roofTopY + 2, cz, cz + 1, chimneyB);
    }
}

void HouseModel::rebuild() {
    generateHouseGrid(templateType, roofType, material, blocks);
    refreshMesh();
}

void HouseModel::refreshMesh() {
    if (!volume) return;
    boundMin = glm::ivec3(HOUSE_VX, HOUSE_VY, HOUSE_VZ);
    boundMax = glm::ivec3(-1, -1, -1);
    for (int z = 0; z < HOUSE_VZ; z++)
        for (int y = 0; y < HOUSE_VY; y++)
            for (int x = 0; x < HOUSE_VX; x++) {
                BlockType t = get(x, y, z);
                volume->setVoxel(x, y, z, houseBlockColor(t));
                if (t != BlockType::Air) {
                    boundMin = glm::min(boundMin, glm::ivec3(x, y, z));
                    boundMax = glm::max(boundMax, glm::ivec3(x, y, z));
                }
            }
    if (boundMax.x < 0) { boundMin = glm::ivec3(0); boundMax = glm::ivec3(0); }
    volume->updateMesh();
}
