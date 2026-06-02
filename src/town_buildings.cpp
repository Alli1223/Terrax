// Centrepieces (well, market, campfire, statue), the fenced farm plot, and the
// bake helpers that turn a Building generator into a placed, rotated grid —
// split out of town.cpp. See town_internal.h.
#include "town_internal.h"
#include "voxel_model.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace townint {

// --- Building grid construction ---------------------------------------------

// Quadrant 0..3 that makes a house's door face from (fx,fz) toward (tx,tz).
int doorQuadrant(int fx, int fz, int tx, int tz) {
    int dx = tx - fx, dz = tz - fz;
    if (std::abs(dx) > std::abs(dz)) return (dx < 0) ? 1 : 3;
    return (dz < 0) ? 0 : 2;
}

// Builds a Building through the polymorphic generator, tight-crops the result
// and rotates it by quadrant `q` into `b`. Used for every building kind that
// has rooms (House, Pub, Blacksmith, MageTower); each is identified by the
// kind() method on the Building subclass.
void bakeBuilding(TownBuilding& b, Building& gen, int q, uint32_t seed) {
    b.kind = (int)gen.kind();
    std::vector<uint8_t> raw;
    std::vector<Room>    rawRooms;
    int sx = 0, sy = 0, sz = 0, dx = 0, dz = -1;
    gen.generate(seed, raw, rawRooms, sx, sy, sz, dx, dz);
    if (sx <= 0 || sy <= 0 || sz <= 0) { b.dimX = b.dimY = b.dimZ = 0; return; }

    b.dimY = sy;
    rotateBuilding(sx, sy, sz, raw, rawRooms, q, b.blocks, b.rooms,
                   b.dimX, b.dimZ);

    // Pre-rotation the front door faces -Z; rotate that normal alongside the
    // grid so the rotated outward normal stays correct. The matrix below is
    // the same rotation rotateBuilding() applies to positions, but applied
    // to a direction vector (no translation). q=1 and q=3 were inverted in
    // the previous version, which sent the door scanner — and the trade-sign
    // placer that reads doorDX/Z — to the wall opposite the actual cut.
    //
    //   q=1: (x,z) → (z, sx-1-x)        gives  (dx,dz) → ( dz, -dx)
    //   q=2: (x,z) → (sx-1-x, sz-1-z)   gives  (dx,dz) → (-dx, -dz)
    //   q=3: (x,z) → (sz-1-z, x)        gives  (dx,dz) → (-dz,  dx)
    int rdx, rdz;
    switch (q & 3) {
        case 1:  rdx =  dz; rdz = -dx; break;
        case 2:  rdx = -dx; rdz = -dz; break;
        case 3:  rdx = -dz; rdz =  dx; break;
        default: rdx =  dx; rdz =  dz; break;
    }
    b.doorDX = rdx;
    b.doorDZ = rdz;
}

// Backwards-compatible wrapper for the existing house-placement code paths.
void bakeHouse(TownBuilding& b, int templ, int roof, int mat, int q, uint32_t seed) {
    HouseBuilding gen(templ, roof, mat);
    bakeBuilding(b, gen, q, seed);
}

// A small village well — stone rim, water pool, four posts and a pyramid roof.
void makeWell(TownBuilding& b) {
    b.kind = 0;
    b.dimX = 5; b.dimY = 8; b.dimZ = 5;
    b.blocks.assign(5 * 8 * 5, (uint8_t)BlockType::Air);
    auto set = [&](int x, int y, int z, BlockType t) {
        b.blocks[((size_t)y * 5 + z) * 5 + x] = (uint8_t)t;
    };
    const BlockType stone = BlockType::Stone, wood = BlockType::Wood;
    const BlockType water = BlockType::Water;
    const BlockType roof  = (BlockType)((int)BlockType::PaintFirst + 7);  // brick red

    for (int x = 0; x < 5; x++)
        for (int z = 0; z < 5; z++) {
            set(x, 0, z, stone);                            // base slab
            if (x == 0 || x == 4 || z == 0 || z == 4) {     // rim
                set(x, 1, z, stone);
                set(x, 2, z, stone);
            }
            set(x, 5, z, roof);                             // roof tier 0
        }
    for (int x = 1; x < 4; x++)
        for (int z = 1; z < 4; z++) {
            set(x, 1, z, water);                            // water pool
            set(x, 6, z, roof);                             // roof tier 1
        }
    set(2, 7, 2, roof);                                     // roof apex
    for (int cx = 0; cx <= 4; cx += 4)
        for (int cz = 0; cz <= 4; cz += 4)
            for (int y = 1; y <= 4; y++) set(cx, y, cz, wood);  // posts
}

// A market square — a paved plaza ringed by four awning-roofed stalls with
// goods on their counters, and a flag pole at the centre. kind = 0.
void makeMarket(TownBuilding& b) {
    b.kind = 0;
    b.dimX = 13; b.dimY = 8; b.dimZ = 13;
    b.blocks.assign((size_t)13 * 8 * 13, (uint8_t)BlockType::Air);
    auto set = [&](int x, int y, int z, BlockType t) {
        b.blocks[((size_t)y * 13 + z) * 13 + x] = (uint8_t)t;
    };
    const BlockType wood = BlockType::Wood, stone = BlockType::Stone;
    const BlockType red    = (BlockType)((int)BlockType::PaintFirst + 7);   // awning red
    const BlockType amber  = (BlockType)((int)BlockType::PaintFirst + 10);  // awning amber
    const BlockType greens = BlockType::Leaves;

    for (int x = 0; x < 13; x++)                          // paved plaza
        for (int z = 0; z < 13; z++)
            set(x, 0, z, stone);

    // One 3x3 stall: a wood counter, four posts, a cloth awning, goods on top.
    auto stall = [&](int x0, int z0, BlockType awning) {
        for (int x = x0; x < x0 + 3; x++)
            for (int z = z0; z < z0 + 3; z++) {
                set(x, 1, z, wood);                       // counter
                set(x, 4, z, awning);                     // awning roof
            }
        for (int y = 1; y <= 3; y++) {                    // corner posts
            set(x0,     y, z0,     wood); set(x0 + 2, y, z0,     wood);
            set(x0,     y, z0 + 2, wood); set(x0 + 2, y, z0 + 2, wood);
        }
        set(x0 + 1, 2, z0,     red);                      // goods on the counter
        set(x0,     2, z0 + 1, amber);
        set(x0 + 2, 2, z0 + 1, greens);
        set(x0 + 1, 2, z0 + 2, amber);
    };
    stall(1, 1, red);   stall(9, 1, amber);
    stall(1, 9, amber); stall(9, 9, red);

    for (int y = 1; y <= 6; y++) set(6, y, 6, wood);      // central flag pole
    set(7, 4, 6, red); set(7, 5, 6, red); set(7, 6, 6, red);
}

// A communal campfire — a stone hearth with crossed logs, an amber flame and
// log-stump seats. The flame is lit by a dynamic point light. kind = 0.
void makeCampfire(TownBuilding& b) {
    b.kind = 0;
    b.dimX = 9; b.dimY = 5; b.dimZ = 9;
    b.blocks.assign((size_t)9 * 5 * 9, (uint8_t)BlockType::Air);
    auto set = [&](int x, int y, int z, BlockType t) {
        b.blocks[((size_t)y * 9 + z) * 9 + x] = (uint8_t)t;
    };
    const BlockType stone = BlockType::Stone, wood = BlockType::Wood;
    const BlockType fire  = (BlockType)((int)BlockType::PaintFirst + 10);  // amber glow

    for (int x = 2; x <= 6; x++)                          // stone hearth + raised rim
        for (int z = 2; z <= 6; z++) {
            set(x, 0, z, stone);
            if (x == 2 || x == 6 || z == 2 || z == 6) set(x, 1, z, stone);
        }
    for (int x = 3; x <= 5; x++) set(x, 1, 4, wood);      // crossed logs
    for (int z = 3; z <= 5; z++) set(4, 1, z, wood);
    set(4, 2, 4, fire); set(3, 2, 4, fire); set(5, 2, 4, fire);   // flame
    set(4, 2, 3, fire); set(4, 2, 5, fire);
    set(4, 3, 4, fire);
    set(0, 0, 4, wood); set(8, 0, 4, wood);              // log-stump seats
    set(4, 0, 0, wood); set(4, 0, 8, wood);
}

// A carved figure on a tiered stone pedestal, snow-dusted — the centrepiece of
// mountain towns. kind = 0.
void makeStatue(TownBuilding& b) {
    b.kind = 0;
    b.dimX = 7; b.dimY = 14; b.dimZ = 7;
    b.blocks.assign((size_t)7 * 14 * 7, (uint8_t)BlockType::Air);
    auto set = [&](int x, int y, int z, BlockType t) {
        b.blocks[((size_t)y * 7 + z) * 7 + x] = (uint8_t)t;
    };
    const BlockType stone = BlockType::Stone, snow = BlockType::Snow;

    for (int x = 1; x <= 5; x++)                          // tiered pedestal
        for (int z = 1; z <= 5; z++) {
            set(x, 0, z, stone);
            set(x, 1, z, stone);
        }
    for (int x = 2; x <= 4; x++)
        for (int z = 2; z <= 4; z++)
            set(x, 2, z, stone);

    for (int y = 3; y <= 6; y++) {                        // legs
        set(2, y, 3, stone); set(4, y, 3, stone);
    }
    for (int y = 7; y <= 10; y++)                         // torso
        for (int x = 2; x <= 4; x++)
            set(x, y, 3, stone);
    for (int y = 8; y <= 10; y++) {                       // arms
        set(1, y, 3, stone); set(5, y, 3, stone);
    }
    set(3, 11, 3, stone); set(3, 12, 3, stone);           // head
    set(3, 13, 3, snow);                                  // snow cap
    set(2, 11, 3, snow); set(4, 11, 3, snow);             // snow on the shoulders
}

// A fenced crop field — tilled soil, rows of crops, a 2-tall wood fence with a
// gate gap on each side and taller corner posts. kind = 2.
void makeFarm(TownBuilding& b, std::mt19937& rng) {
    int w = 11 + (int)(rng() % 5), dpth = 11 + (int)(rng() % 5);
    b.kind = 2;
    b.dimX = w; b.dimY = 4; b.dimZ = dpth;
    b.blocks.assign((size_t)w * 4 * dpth, (uint8_t)BlockType::Air);
    auto set = [&](int x, int y, int z, BlockType t) {
        b.blocks[((size_t)y * dpth + z) * w + x] = (uint8_t)t;
    };
    const BlockType soil = BlockType::Dirt, fence = BlockType::Wood;
    const BlockType crop = BlockType::Leaves;

    for (int z = 0; z < dpth; z++)                          // tilled plot
        for (int x = 0; x < w; x++)
            set(x, 0, z, soil);

    for (int z = 2; z < dpth - 2; z += 2)                   // crop rows
        for (int x = 2; x < w - 2; x++)
            set(x, 1, z, crop);

    int gx = w / 2, gz = dpth / 2;                          // 2-wide gate gap per side
    for (int y = 1; y <= 2; y++) {
        for (int x = 0; x < w; x++)
            if (x != gx && x != gx - 1) {
                set(x, y, 0, fence);
                set(x, y, dpth - 1, fence);
            }
        for (int z = 0; z < dpth; z++)
            if (z != gz && z != gz - 1) {
                set(0, y, z, fence);
                set(w - 1, y, z, fence);
            }
    }
    set(0, 3, 0, fence);          set(w - 1, 3, 0, fence);          // taller corner posts
    set(0, 3, dpth - 1, fence);   set(w - 1, 3, dpth - 1, fence);
}


}  // namespace townint
