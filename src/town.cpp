#include "town.h"
#include "world.h"
#include "voxel_model.h"
#include "building.h"
#include <algorithm>
#include <thread>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

// Forward declaration so the in-anon-namespace placement helpers further down
// can reach the file-scope definition that lives below buildTownPlan().
static int townSlopeOffset(const Town& t, int wx, int wz);

namespace {

// --- Survey parameters -------------------------------------------------------
constexpr int REGION      = 24000;   // half-extent of the settled belt (blocks)
constexpr int SURVEY_STEP = 64;      // coarse survey grid spacing
constexpr int GRID        = (REGION * 2) / SURVEY_STEP;   // cells per side
constexpr int BUCKET      = 2800;    // one settlement per BUCKET-sized region
constexpr int DOCK_LEN    = 9;       // jetty length out over the water (blocks)

// Biome ids — mirror the Biome enum order in world.cpp.
constexpr int BIOME_MOUNTAINS = 3, BIOME_TUNDRA = 4;

// --- Town wall styles --------------------------------------------------------
// Walled towns (Town::wallRadius > 0) each pick one of these, by size and type,
// so the world has humble wooden palisades, modest and great stone walls, and
// pale coastal sandstone ramparts rather than one uniform wall everywhere.
struct WallStyleDef {
    int       height;      // wall height above the base
    float     halfThick;   // radial half-thickness (≈ 2*halfThick+1 blocks wide)
    BlockType mat;         // body / tower / lintel material
    bool      crenel;      // true = stone battlements; false = solid palisade top
    int       towerBonus;  // extra height on the gate towers
};
constexpr int WALL_STYLE_COUNT = 4;
const WallStyleDef WALL_STYLES[WALL_STYLE_COUNT] = {
    { 4, 0.6f, BlockType::Wood,      false, 2 },  // 0 wooden palisade  — small/humble
    { 5, 1.0f, BlockType::Stone,     true,  3 },  // 1 modest stone wall
    { 7, 1.8f, BlockType::Stone,     true,  4 },  // 2 great stone rampart — cities
    { 6, 1.3f, BlockType::Sandstone, true,  3 },  // 3 coastal sandstone wall
};

// Picks a wall style from a town's size, type and a seed roll, so bigger towns
// trend toward grander walls and seaside towns toward sandstone, with variety.
int pickWallStyle(int houses, TownType type, uint32_t roll) {
    int r = (int)(roll % 100u);
    if (houses >= 60) return (r < 78) ? 2 : 1;              // cities: great ramparts
    if (houses >= 38) {                                      // towns
        if (type == TownType::Coastal && r < 45) return 3;
        return (r < 50) ? 1 : 2;
    }
    if (type == TownType::Coastal && r < 35) return 3;       // small walled town
    return (r < 55) ? 0 : 1;                                 // palisade or modest stone
}

// Set true once buildTownPlan() has finished. While it is false the terrain
// oracle skips town flattening, so the survey itself works on the natural,
// unflattened land (and there is no recursion back into the plan build).
std::atomic<bool> g_townReady{false};

void reportStage(int stage, float frac) {
    gTownBuildStage.store(stage,    std::memory_order_release);
    gTownBuildFraction.store(frac,  std::memory_order_release);
}

int cellWorld(int g) { return -REGION + g * SURVEY_STEP + SURVEY_STEP / 2; }

// Inverse of cellWorld: the survey cell that contains a world X or Z.
int worldToCell(int w) {
    int g = (w + REGION - SURVEY_STEP / 2) / SURVEY_STEP;
    return std::min(GRID - 1, std::max(0, g));
}

float frand(std::mt19937& r, float lo, float hi) {
    return lo + (float)(r() % 100000) / 100000.0f * (hi - lo);
}

// A deterministic settlement name: a root chosen from the town's location,
// plus a suffix themed to the town type (coastal/mountain/grassland).
std::string makeTownName(int wx, int wz, TownType type) {
    static const char* PRE[] = {
        "Ash","Black","Bram","Crow","Dun","Elder","Fern","Frost","Gold","Grey",
        "Hart","Holl","Iron","Lark","Mire","Moss","Oak","Pine","Raven","Red",
        "Stone","Thorn","West","North","Brook","Birch","Clear","Wild","Marsh","Dawn"
    };
    static const char* COAST[] = { "port","bay","wick","mouth","shore","cove","harbour","strand" };
    static const char* MTN[]   = { "peak","crag","ridge","fell","hold","spire","crest","reach" };
    static const char* GRASS[] = { "field","vale","meadow","dale","ford","hollow","ton","mere" };

    std::mt19937 rng(worldSeed()
                     ^ (uint32_t)(wx * 0x9E3779B1u)
                     ^ (uint32_t)(wz * 0x85EBCA77u) ^ 0x7A11u);
    const char* pre = PRE[rng() % (sizeof(PRE) / sizeof(PRE[0]))];
    const char* suf;
    if      (type == TownType::Coastal)  suf = COAST[rng() % (sizeof(COAST) / sizeof(COAST[0]))];
    else if (type == TownType::Mountain) suf = MTN[rng()   % (sizeof(MTN)   / sizeof(MTN[0]))];
    else                                 suf = GRASS[rng() % (sizeof(GRASS) / sizeof(GRASS[0]))];
    return std::string(pre) + suf;
}

// --- Generic A* --------------------------------------------------------------
// 8-connected A* over a rectangular grid. `cost(x,z)` returns the per-cell
// traverse cost (>= 1) or a negative value if the cell is impassable. The
// heuristic is octile distance scaled by `hweight` (> 1 trades optimality for
// speed). Returns the path inclusive of start and goal, or empty if no route
// is found within `cap` node expansions.
template <class CostFn>
std::vector<glm::ivec2> astar(int gw, int gh, glm::ivec2 s, glm::ivec2 g,
                              CostFn cost, float hweight, int cap) {
    std::vector<glm::ivec2> path;
    if (s.x < 0 || s.x >= gw || s.y < 0 || s.y >= gh) return path;
    if (g.x < 0 || g.x >= gw || g.y < 0 || g.y >= gh) return path;

    auto key = [gw](int x, int z) -> int64_t { return (int64_t)z * gw + x; };
    auto heur = [&](int x, int z) {
        int dx = std::abs(x - g.x), dz = std::abs(z - g.y);
        int hi = std::max(dx, dz), lo = std::min(dx, dz);
        return (float)(hi - lo) + 1.41421356f * (float)lo;
    };

    struct Open { float f, g; int x, z; };
    struct Cmp { bool operator()(const Open& a, const Open& b) const { return a.f > b.f; } };
    std::priority_queue<Open, std::vector<Open>, Cmp> open;
    std::unordered_map<int64_t, float>   gScore;
    std::unordered_map<int64_t, int64_t> came;

    gScore[key(s.x, s.y)] = 0.0f;
    open.push({ hweight * heur(s.x, s.y), 0.0f, s.x, s.y });

    const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    const int DZ[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    int  expanded = 0;
    bool found    = false;

    while (!open.empty()) {
        Open cur = open.top(); open.pop();
        int64_t ck = key(cur.x, cur.z);
        auto git = gScore.find(ck);
        if (git != gScore.end() && cur.g > git->second + 0.001f) continue;  // stale entry
        if (cur.x == g.x && cur.z == g.y) { found = true; break; }
        if (++expanded > cap) break;

        for (int d = 0; d < 8; d++) {
            int nx = cur.x + DX[d], nz = cur.z + DZ[d];
            if (nx < 0 || nx >= gw || nz < 0 || nz >= gh) continue;
            float cc = cost(nx, nz);
            if (cc < 0.0f) continue;                       // impassable cell
            float step = (d < 4) ? 1.0f : 1.41421356f;
            float ng   = cur.g + step * cc;
            int64_t nk = key(nx, nz);
            auto nit = gScore.find(nk);
            if (nit == gScore.end() || ng < nit->second) {
                gScore[nk] = ng;
                came[nk]   = ck;
                open.push({ ng + hweight * heur(nx, nz), ng, nx, nz });
            }
        }
    }

    if (!found) return path;
    int64_t k = key(g.x, g.y), sk = key(s.x, s.y);
    while (k != sk) {
        path.push_back(glm::ivec2((int)(k % gw), (int)(k / gw)));
        auto it = came.find(k);
        if (it == came.end()) { path.clear(); return path; }
        k = it->second;
    }
    path.push_back(s);
    std::reverse(path.begin(), path.end());
    return path;
}

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

// --- Layout ------------------------------------------------------------------

bool boxesOverlap(int ax, int az, int aw, int ad, int bx, int bz, int bw, int bd) {
    return ax < bx + bw && ax + aw > bx && az < bz + bd && az + ad > bz;
}

bool tryPlaceHouse(Town& t, std::mt19937& rng, int px, int pz, int faceX, int faceZ,
                   const int* templ, int nT, const int* mats, int nM,
                   const int* roofs, int nR) {
    int q = doorQuadrant(px, pz, faceX, faceZ);
    TownBuilding b;
    uint32_t seed = worldSeed() ^ (uint32_t)(px * 73856093) ^ (uint32_t)(pz * 19349663);
    bakeHouse(b, templ[rng() % nT], roofs[rng() % nR], mats[rng() % nM], q, seed);
    if (b.dimX == 0) return false;
    b.wx = px - b.dimX / 2;
    b.wz = pz - b.dimZ / 2;
    for (const TownBuilding& o : t.buildings)
        if (boxesOverlap(b.wx - 3, b.wz - 3, b.dimX + 6, b.dimZ + 6,
                         o.wx, o.wz, o.dimX, o.dimZ))
            return false;
    // Each building gets the slope offset at its own centre — the chunk
    // generator pins the surrounding flat pad to this same value, so the door
    // and the path leading to it always meet at the door's level even when
    // the town as a whole tilts.
    b.baseY = t.baseY + townSlopeOffset(t, px, pz);
    t.buildings.push_back(std::move(b));
    return true;
}

bool tryPlaceFarm(Town& t, std::mt19937& rng, int px, int pz) {
    TownBuilding b;
    makeFarm(b, rng);
    b.wx = px - b.dimX / 2;
    b.wz = pz - b.dimZ / 2;
    for (const TownBuilding& o : t.buildings)
        if (boxesOverlap(b.wx - 4, b.wz - 4, b.dimX + 8, b.dimZ + 8,
                         o.wx, o.wz, o.dimX, o.dimZ))
            return false;
    b.baseY = t.baseY + townSlopeOffset(t, px, pz);
    t.buildings.push_back(std::move(b));
    return true;
}

// Stamps a single specialised Building (pub / blacksmith / mage tower) facing
// the town centre. Acts like tryPlaceHouse but takes a polymorphic generator
// so the caller picks the kind/material per town and type bias.
bool tryPlaceSpecial(Town& t, Building& gen, int px, int pz) {
    int q = doorQuadrant(px, pz, t.center.x, t.center.y);
    TownBuilding b;
    uint32_t seed = worldSeed() ^ (uint32_t)(px * 73856093) ^ (uint32_t)(pz * 19349663);
    bakeBuilding(b, gen, q, seed);
    if (b.dimX == 0) return false;
    b.wx = px - b.dimX / 2;
    b.wz = pz - b.dimZ / 2;
    for (const TownBuilding& o : t.buildings)
        if (boxesOverlap(b.wx - 3, b.wz - 3, b.dimX + 6, b.dimZ + 6,
                         o.wx, o.wz, o.dimX, o.dimZ))
            return false;
    b.baseY = t.baseY + townSlopeOffset(t, px, pz);
    t.buildings.push_back(std::move(b));
    return true;
}

// Houses arranged in concentric rings facing the town centre. `scattered`
// loosens the spacing for mountain villages (which also terrace naturally,
// since each house takes its own ground height).
void layoutRings(Town& t, std::mt19937& rng, int numH, bool scattered,
                 const int* templ, int nT, const int* mats, int nM,
                 const int* roofs, int nR) {
    int placed = 0;
    for (int ring = 0; ring < 16 && placed < numH; ring++) {
        int ringR = 22 + ring * 14;
        if (ringR > t.radius + 14) break;
        int slots = std::max(4, ringR / 5);
        float a0 = frand(rng, 0.0f, 6.2832f);
        for (int s = 0; s < slots && placed < numH; s++) {
            float jit = scattered ? 0.42f : 0.16f;
            float ang = a0 + s * (6.2832f / slots) + frand(rng, -jit, jit);
            int rr = ringR + (int)frand(rng, scattered ? -10.0f : -3.0f,
                                             scattered ?  10.0f :  3.0f);
            int px = t.center.x + (int)(cosf(ang) * rr);
            int pz = t.center.y + (int)(sinf(ang) * rr);
            if (tryPlaceHouse(t, rng, px, pz, t.center.x, t.center.y,
                              templ, nT, mats, nM, roofs, nR))
                placed++;
        }
    }
}

void layoutTown(Town& t) {
    std::mt19937 rng(worldSeed()
                     ^ (uint32_t)(t.center.x * 73856093)
                     ^ (uint32_t)(t.center.y * 19349663));

    // Town centrepiece — varies by town type and seed.
    {
        if (t.type == TownType::Mountain)
            t.centerpiece = TownCenter::Statue;
        else {
            static const TownCenter OPTS[] = { TownCenter::Well, TownCenter::Market,
                                               TownCenter::Campfire };
            t.centerpiece = OPTS[rng() % 3];
        }
        TownBuilding cp;
        switch (t.centerpiece) {
            case TownCenter::Market:   makeMarket(cp);   break;
            case TownCenter::Campfire: makeCampfire(cp); break;
            case TownCenter::Statue:   makeStatue(cp);   break;
            default:                   makeWell(cp);     break;
        }
        cp.wx    = t.center.x - cp.dimX / 2;
        cp.wz    = t.center.y - cp.dimZ / 2;
        cp.baseY = t.baseY;
        t.buildings.push_back(std::move(cp));
    }

    const bool big = (t.size == TownSize::Town);

    // House roof style follows the local biome: desert towns are uniformly
    // flat-roofed, snowy (mountain / tundra) towns get steep pitched roofs to
    // shed snow, and everywhere else mixes gabled / hipped / pyramid roofs.
    // layoutRings draws each house's roof from this set.
    static const int ROOF_FLAT[]  = { 0 };
    static const int ROOF_STEEP[] = { 4, 4, 3 };       // steep gable, occasional steep pyramid
    static const int ROOF_MIX[]   = { 1, 2, 3 };       // gabled, hipped, pyramid
    int townBiome = sampleSurface(t.center.x, t.center.y).biome;
    const int* ROOFS; int nROOFS;
    if (townBiome == 2)                          { ROOFS = ROOF_FLAT;  nROOFS = 1; }  // Desert
    else if (townBiome == 3 || townBiome == 4)   { ROOFS = ROOF_STEEP; nROOFS = 3; }  // Mountains/Tundra
    else                                         { ROOFS = ROOF_MIX;   nROOFS = 3; }

    // --- Specialised buildings (pub / blacksmith / mage tower) ------------
    // One pub and one blacksmith per town (every settlement has both — this
    // is a hand-wave, but it gives every village a familiar set of services).
    // A mage tower is rarer and biased toward mountain towns.
    auto placeSpecial = [&](Building& gen, float baseAngle, int innerR) {
        for (int attempt = 0; attempt < 8; attempt++) {
            float ang = baseAngle + frand(rng, -0.2f, 0.2f);
            int   rr  = innerR + (int)frand(rng, -2.0f, 6.0f);
            int   px  = t.center.x + (int)(cosf(ang) * rr);
            int   pz  = t.center.y + (int)(sinf(ang) * rr);
            if (tryPlaceSpecial(t, gen, px, pz)) return true;
            baseAngle += 0.6f;   // try a different sector
        }
        return false;
    };

    {
        // Pub — material picked by town type so it blends in with the houses.
        int pubMat;
        switch (t.type) {
            case TownType::Coastal:  pubMat = 7;  break;   // coastal palette
            case TownType::Mountain: pubMat = 4;  break;   // cabin
            default:                 pubMat = 1;  break;   // cottage
        }
        PubBuilding pub(pubMat, /*roof=*/1);
        placeSpecial(pub, frand(rng, 0.0f, 6.2832f), 18);
    }
    {
        // Blacksmith — usually stone walls; mountain villages get hipped roof
        // to handle snow load.
        int smithMat = (t.type == TownType::Mountain) ? 2 : 4;
        int smithRoof = (t.type == TownType::Mountain) ? 2 : 1;
        BlacksmithBuilding smith(smithMat, smithRoof);
        placeSpecial(smith, frand(rng, 0.0f, 6.2832f) + 2.094f, 18);  // +120°
    }
    {
        // Mage tower — common in mountain settlements (mages like remote
        // peaks), rare elsewhere.
        const int towerChance =
            (t.type == TownType::Mountain) ? 60 :
            (t.type == TownType::Coastal)  ? 25 : 35;
        if ((int)(rng() % 100) < (big ? towerChance + 15 : towerChance)) {
            int towerMat = (t.type == TownType::Mountain) ? 2 : 3;
            MageTowerBuilding tower(towerMat, big ? 4 : 3);
            placeSpecial(tower, frand(rng, 0.0f, 6.2832f) + 4.189f, 20);  // +240°
        }
    }
    {
        // Stable — most settlements keep horses; a little rarer in cramped
        // coastal towns. Placed out among the houses rather than at the centre.
        const int chance = (t.type == TownType::Coastal) ? 35 : 60;
        if ((int)(rng() % 100) < (big ? chance + 15 : chance)) {
            int mat = (t.type == TownType::Mountain) ? 4 : 6;   // cabin / forest timber
            StableBuilding stable(mat, /*roof=*/1);
            placeSpecial(stable, frand(rng, 0.0f, 6.2832f) + 1.047f, 26);   // +60°
        }
    }
    {
        // Chapel — a place of worship, more common in larger settlements.
        if ((int)(rng() % 100) < (big ? 65 : 45)) {
            int mat = (t.type == TownType::Coastal) ? 5 : 2;    // sandstone / stone
            ChapelBuilding chapel(mat, /*roof=*/4);
            placeSpecial(chapel, frand(rng, 0.0f, 6.2832f) + 3.665f, 28);   // +210°
        }
    }
    {
        // Apothecary — a herbalist's shop, biased toward bigger towns.
        const int chance = (t.type == TownType::Mountain) ? 25 : 40;
        if ((int)(rng() % 100) < (big ? chance + 15 : chance)) {
            int mat = (t.type == TownType::Coastal) ? 9 : 8;    // plum / autumn
            ApothecaryBuilding apo(mat, /*roof=*/1);
            placeSpecial(apo, frand(rng, 0.0f, 6.2832f) + 5.236f, 24);      // +300°
        }
    }
    {
        // Bakery — a common high-street shop.
        if ((int)(rng() % 100) < (big ? 55 : 40)) {
            int mat = (t.type == TownType::Mountain) ? 4 : 1;   // cabin / cottage
            BakeryBuilding bakery(mat, /*roof=*/1);
            placeSpecial(bakery, frand(rng, 0.0f, 6.2832f) + 0.785f, 26);   // +45°
        }
    }
    {
        // Watchtower — a guard post; common in walled or large settlements.
        const int chance = (t.wallRadius > 0) ? 55 : 25;
        if ((int)(rng() % 100) < (big ? chance + 15 : chance)) {
            int mat = (t.type == TownType::Coastal) ? 5 : 2;    // sandstone / stone
            WatchtowerBuilding tower(mat, big ? 5 : 4);
            placeSpecial(tower, frand(rng, 0.0f, 6.2832f) + 2.618f, 28);    // +150°
        }
    }

    if (t.type == TownType::Coastal) {
        // Coastal villages get the Norse longhouse mixed in — feels right for
        // a seafaring settlement on a fjord.
        static const int T[] = { 0, 1, 2, 4, 5, 10, 12, 13, 14, 15, 16, 17, 18, 19 };  // + composites
        static const int M[] = { 7, 1, 5 };         // coastal / cottage / sandstone
        int numH = t.targetHouses;
        layoutRings(t, rng, numH, false, T, 14, M, 3, ROOFS, nROOFS);
    } else if (t.type == TownType::Mountain) {
        // Mountain towns favour heavier wooden structures; both Norse templates
        // appear here for the high-alpine stave-church silhouette.
        static const int T[] = { 1, 2, 3, 4, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };  // + composites
        static const int M[] = { 2, 4, 0, 6 };      // stone / cabin / timber / forest
        int numH = t.targetHouses;
        layoutRings(t, rng, numH, true, T, 15, M, 4, ROOFS, nROOFS);
    } else {
        static const int T[] = { 0, 1, 2, 4, 5, 7, 12, 13, 14, 15, 16, 17, 18, 19 };  // mix + composites
        static const int M[] = { 0, 1, 4, 8 };        // timber/cottage/cabin/autumn
        int numH = t.targetHouses;
        layoutRings(t, rng, numH, false, T, 14, M, 4, ROOFS, nROOFS);
    }

    // An outer ring of fenced farm plots (sparse for mountain hamlets).
    int numFarms;
    if (t.type == TownType::Mountain)  numFarms = (int)(rng() % 2);          // 0..1
    else if (big)                      numFarms = 3 + (int)(rng() % 2);      // 3..4
    else                               numFarms = 2 + (int)(rng() % 2);      // 2..3
    // Farmland sits in the fields beyond the wall (or just past the houses in an
    // unwalled village).
    int farmRing = (t.wallRadius > 0) ? t.wallRadius + 14 : t.radius + 22;
    for (int f = 0; f < numFarms; f++)
        for (int attempt = 0; attempt < 10; attempt++) {
            float ang = frand(rng, 0.0f, 6.2832f);
            int   rr  = farmRing + (int)frand(rng, -10.0f, 18.0f);
            int   px  = t.center.x + (int)(cosf(ang) * rr);
            int   pz  = t.center.y + (int)(sinf(ang) * rr);
            if (tryPlaceFarm(t, rng, px, pz)) break;
        }

    // Bounding box over every building, for fast chunk-stamp culling.
    t.bbMin = glm::ivec2(1 << 30, 1 << 30);
    t.bbMax = glm::ivec2(-(1 << 30), -(1 << 30));
    for (const TownBuilding& b : t.buildings) {
        t.bbMin.x = std::min(t.bbMin.x, b.wx);
        t.bbMin.y = std::min(t.bbMin.y, b.wz);
        t.bbMax.x = std::max(t.bbMax.x, b.wx + b.dimX);
        t.bbMax.y = std::max(t.bbMax.y, b.wz + b.dimZ);
    }
}

// --- Intra-town paths --------------------------------------------------------

// Carves a gravel path from every house to the village well, routed with A*
// around the other buildings so paths bend instead of cutting through walls.
void routeTownPaths(Town& t) {
    if (t.buildings.size() < 2) return;
    const int CELL = 2, MARGIN = 14;
    int minx = t.bbMin.x - MARGIN, minz = t.bbMin.y - MARGIN;
    int maxx = t.bbMax.x + MARGIN, maxz = t.bbMax.y + MARGIN;
    int gw = (maxx - minx) / CELL + 1;
    int gh = (maxz - minz) / CELL + 1;
    if (gw < 2 || gh < 2) return;

    auto w2c = [&](int wx, int wz) {
        int cx = (wx - minx) / CELL, cz = (wz - minz) / CELL;
        return glm::ivec2(std::min(gw - 1, std::max(0, cx)),
                          std::min(gh - 1, std::max(0, cz)));
    };
    auto c2w = [&](int cx, int cz) {
        return glm::ivec2(minx + cx * CELL + CELL / 2, minz + cz * CELL + CELL / 2);
    };

    // Obstacle grid: -1 free, otherwise the index of the building occupying it.
    std::vector<int> obst((size_t)gw * gh, -1);
    for (size_t bi = 0; bi < t.buildings.size(); bi++) {
        const TownBuilding& b = t.buildings[bi];
        if (b.kind == 0) continue;                          // the well stays walkable
        glm::ivec2 lo = w2c(b.wx - 1, b.wz - 1);
        glm::ivec2 hi = w2c(b.wx + b.dimX + 1, b.wz + b.dimZ + 1);
        for (int cz = lo.y; cz <= hi.y; cz++)
            for (int cx = lo.x; cx <= hi.x; cx++)
                obst[(size_t)cz * gw + cx] = (int)bi;
    }

    glm::ivec2 wellC = w2c(t.center.x, t.center.y);

    for (size_t bi = 0; bi < t.buildings.size(); bi++) {
        const TownBuilding& b = t.buildings[bi];
        // Paths originate at every building that has rooms (houses, pubs,
        // blacksmiths, mage towers) — centrepieces and farms are skipped.
        if (b.rooms.empty()) continue;
        int hcx = b.wx + b.dimX / 2, hcz = b.wz + b.dimZ / 2;
        glm::ivec2 hC = w2c(hcx, hcz);
        int self = (int)bi;
        auto cost = [&obst, gw, self](int cx, int cz) -> float {
            int o = obst[(size_t)cz * gw + cx];
            return (o < 0 || o == self) ? 1.0f : -1.0f;     // own footprint is passable
        };
        std::vector<glm::ivec2> cells = astar(gw, gh, hC, wellC, cost, 1.0f, 60000);

        TownRoad p;
        if (cells.size() >= 2) {
            for (const glm::ivec2& cc : cells)
                p.pts.push_back(c2w(cc.x, cc.y));
        } else {                                            // fallback: straight line
            p.pts.push_back(glm::ivec2(hcx, hcz));
            p.pts.push_back(t.center);
        }
        t.paths.push_back(std::move(p));
    }

    // Grow the town bounding box to cover routed paths (used for stamp culling).
    for (const TownRoad& p : t.paths)
        for (const glm::ivec2& pt : p.pts) {
            t.bbMin.x = std::min(t.bbMin.x, pt.x);
            t.bbMin.y = std::min(t.bbMin.y, pt.y);
            t.bbMax.x = std::max(t.bbMax.x, pt.x);
            t.bbMax.y = std::max(t.bbMax.y, pt.y);
        }
}

// --- Highways & docks --------------------------------------------------------

// Places (or reuses) a dock where a highway meets the water. `landW` is a route
// point on land and `waterW` the next route point out over the water. Returns
// the world-XZ block the jetty roots at.
glm::ivec2 placeDock(TownPlan& plan, glm::ivec2 landW, glm::ivec2 waterW) {
    int sx = (waterW.x > landW.x) - (waterW.x < landW.x);
    int sz = (waterW.y > landW.y) - (waterW.y < landW.y);
    if (sx != 0 && sz != 0) {                               // force a single cardinal axis
        if (std::abs(waterW.x - landW.x) >= std::abs(waterW.y - landW.y)) sz = 0;
        else                                                              sx = 0;
    }
    if (sx == 0 && sz == 0) sx = 1;

    // March from the land point toward the water; stop on the last solid block.
    int x = landW.x, z = landW.y;
    for (int step = 0; step < 256; step++) {
        if (sampleSurface(x + sx, z + sz).height < WORLD_SEA_LEVEL) break;
        x += sx; z += sz;
    }

    for (const TownDock& o : plan.docks)                    // reuse a nearby jetty
        if (std::abs(o.root.x - x) + std::abs(o.root.y - z) < 14)
            return o.root;

    // No jetties inside any settlement — coastal towns included. A dock stamped
    // among the houses clutters the waterfront, so the road still meets the
    // shore here but grows no jetty; any ferry link to this point is dropped
    // because it won't resolve to a dock (see ferry_routes.cpp).
    for (const Town& t : plan.towns) {
        long long dx = (long long)x - t.center.x;
        long long dz = (long long)z - t.center.y;
        long long r  = (long long)t.radius + 90;
        if (dx * dx + dz * dz < r * r)
            return glm::ivec2(x, z);                        // road still meets the shore
    }

    TownDock dk;
    dk.root = glm::ivec2(x, z);
    dk.dx = sx; dk.dz = sz;
    plan.docks.push_back(dk);
    return dk.root;
}

// Resamples a world-XZ polyline to roughly even `spacing`-block steps.
std::vector<glm::ivec2> densifyPath(const std::vector<glm::ivec2>& poly, int spacing) {
    std::vector<glm::ivec2> out;
    if (poly.empty()) return out;
    out.push_back(poly[0]);
    for (size_t i = 0; i + 1 < poly.size(); i++) {
        int ax = poly[i].x, az = poly[i].y;
        int dx = poly[i + 1].x - ax, dz = poly[i + 1].y - az;
        int len   = (int)std::sqrt((double)((long long)dx * dx + (long long)dz * dz));
        int steps = std::max(1, len / std::max(1, spacing));
        for (int s = 1; s <= steps; s++)
            out.push_back(glm::ivec2(ax + dx * s / steps, az + dz * s / steps));
    }
    return out;
}

// Fine A* from a town centre out through its own buildings to a portal point
// clear of the settlement, so a highway weaves past the houses before going
// direct. Returns a world-XZ polyline (centre first, portal last).
std::vector<glm::ivec2> routeTownExit(const Town& t, glm::ivec2 portalW) {
    const int CELL = 2, MARGIN = 12;
    int minx = std::min(portalW.x, t.bbMin.x) - MARGIN;
    int minz = std::min(portalW.y, t.bbMin.y) - MARGIN;
    int maxx = std::max(portalW.x, t.bbMax.x) + MARGIN;
    int maxz = std::max(portalW.y, t.bbMax.y) + MARGIN;
    int gw = (maxx - minx) / CELL + 1;
    int gh = (maxz - minz) / CELL + 1;
    if (gw < 2 || gh < 2) return { t.center, portalW };

    auto w2c = [&](int wx, int wz) {
        int cx = (wx - minx) / CELL, cz = (wz - minz) / CELL;
        return glm::ivec2(std::min(gw - 1, std::max(0, cx)),
                          std::min(gh - 1, std::max(0, cz)));
    };
    auto c2w = [&](int cx, int cz) {
        return glm::ivec2(minx + cx * CELL + CELL / 2, minz + cz * CELL + CELL / 2);
    };

    std::vector<char> blocked((size_t)gw * gh, 0);
    for (const TownBuilding& b : t.buildings) {
        if (b.kind == 0) continue;                          // the well is passable
        glm::ivec2 lo = w2c(b.wx - 1, b.wz - 1);
        glm::ivec2 hi = w2c(b.wx + b.dimX + 1, b.wz + b.dimZ + 1);
        for (int cz = lo.y; cz <= hi.y; cz++)
            for (int cx = lo.x; cx <= hi.x; cx++)
                blocked[(size_t)cz * gw + cx] = 1;
    }

    glm::ivec2 sc = w2c(t.center.x, t.center.y);
    glm::ivec2 gc = w2c(portalW.x, portalW.y);
    blocked[(size_t)sc.y * gw + sc.x] = 0;
    for (int dz = -1; dz <= 1; dz++)                        // keep the portal reachable
        for (int dx = -1; dx <= 1; dx++) {
            int cx = gc.x + dx, cz = gc.y + dz;
            if (cx >= 0 && cx < gw && cz >= 0 && cz < gh)
                blocked[(size_t)cz * gw + cx] = 0;
        }

    auto cost = [&blocked, gw](int cx, int cz) -> float {
        return blocked[(size_t)cz * gw + cx] ? -1.0f : 1.0f;
    };
    std::vector<glm::ivec2> cells = astar(gw, gh, sc, gc, cost, 1.0f, 80000);
    if (cells.size() < 2) return { t.center, portalW };

    std::vector<glm::ivec2> out;
    out.push_back(t.center);
    for (size_t ci = 1; ci + 1 < cells.size(); ci++)
        out.push_back(c2w(cells[ci].x, cells[ci].y));
    out.push_back(portalW);
    return out;
}

// Turns a full highway route into stamped features: flat bridges over gullies
// or rivers up to 200 blocks wide, gravel road on open ground, and a dock on
// each shore where the route meets water (the water span is left clear).
void emitHighwayRoute(TownPlan& plan, const std::vector<glm::ivec2>& route) {
    const int SP = 4;
    std::vector<glm::ivec2> P = densifyPath(route, SP);
    const int n = (int)P.size();
    if (n < 2) return;

    std::vector<int> H(n);
    for (int i = 0; i < n; i++) H[i] = sampleSurface(P[i].x, P[i].y).height;

    // Route points within a town — a bridge must never span into one (towns
    // are flattened, so any crossing there is just flat road).
    std::vector<char> inTown(n, 0);
    for (int i = 0; i < n; i++)
        for (const Town& t : plan.towns) {
            long long dx = (long long)P[i].x - t.center.x;
            long long dz = (long long)P[i].y - t.center.y;
            long long rr = (long long)t.radius + 90;   // clear the whole flattened footprint
            if (dx * dx + dz * dz < rr * rr) { inTown[i] = 1; break; }
        }

    const int MAXSPAN = 200 / SP;   // longest bridge span — 200 blocks
    const int STEEP   = 48  / SP;   // the drop must occur within 48 blocks
    const int MINDROP = 14;         // and fall at least 14 blocks below the rim

    // Detect bridge spans: a steep drop of at least 14 blocks that recovers to
    // a similar level within 200 blocks — a genuine ravine, not a gentle dip.
    struct Span { int s, e, deckY; };
    std::vector<Span> spans;
    {
        int i = 0;
        while (i < n - 1) {
            if (inTown[i]) { i++; continue; }               // never start a span in a town
            int h0  = H[i];
            int lim = std::min(n - 1, i + STEEP);
            int j   = i + 1;
            while (j <= lim && H[j] > h0 - MINDROP) j++;
            if (j > lim) { i++; continue; }
            int s = i;                                      // rim = highest point pre-dip
            for (int q = i; q <= j; q++) if (H[q] > H[s]) s = q;
            if (inTown[s]) { i++; continue; }
            int deckY = H[s];
            int lim2  = std::min(n - 1, s + MAXSPAN);
            int k     = j + 1;
            while (k <= lim2 && H[k] < deckY - 2 && !inTown[k]) k++;  // stop at the town edge
            if (k <= lim2 && H[k] >= deckY - 2) {
                spans.push_back({ s, k, deckY });
                i = k;
            } else {
                i++;
            }
        }
    }

    // Emits gravel road for an index range, split at water (a dock per shore).
    auto emitRoad = [&](int a, int b) {
        TownRoad   cur;
        bool       inWater  = false;
        glm::ivec2 lastLand = P[a];
        int        waterEnter = 0;
        glm::ivec2 dockEnter(0);
        for (int idx = a; idx <= b; idx++) {
            if (H[idx] >= WORLD_SEA_LEVEL) {
                if (inWater) {                              // water -> land
                    glm::ivec2 dockExit = placeDock(plan, P[idx], P[idx - 1]);
                    cur = TownRoad();
                    cur.pts.push_back(dockExit);
                    inWater = false;
                    // A crossing wider than 200 blocks is served by a ferry.
                    if ((idx - waterEnter) * SP > 200)
                        plan.ferryLinks.push_back({ dockEnter, dockExit });
                }
                cur.pts.push_back(P[idx]);
                lastLand = P[idx];
            } else if (!inWater) {                          // land -> water
                dockEnter = placeDock(plan, lastLand, P[idx]);
                cur.pts.push_back(dockEnter);
                if (cur.pts.size() >= 2) plan.highways.push_back(std::move(cur));
                cur = TownRoad();
                inWater = true;
                waterEnter = idx;
            }
        }
        if (!inWater && cur.pts.size() >= 2) plan.highways.push_back(std::move(cur));
    };

    int roadStart = 0;
    for (const Span& sp : spans) {
        emitRoad(roadStart, sp.s);
        TownBridge tb;
        tb.deckY = sp.deckY;
        for (int idx = sp.s; idx <= sp.e; idx++) tb.pts.push_back(P[idx]);
        plan.bridges.push_back(std::move(tb));
        roadStart = sp.e;
    }
    emitRoad(roadStart, n - 1);
}

// Routes a highway between every settlement and its two nearest neighbours.
// Each link weaves out of its endpoint towns with a fine A* pass, then takes a
// direct, terrain-following A* route across the coarse survey grid.
void routeHighways(TownPlan& plan, const std::vector<int16_t>& hgt) {
    const int N = (int)plan.towns.size();
    if (N < 2) return;

    // Survey-cell penalty so the trunk route bends around other settlements.
    std::vector<float> townPen((size_t)GRID * GRID, 0.0f);
    for (const Town& t : plan.towns) {
        int r = t.radius + 40;
        int c0x = worldToCell(t.center.x - r), c1x = worldToCell(t.center.x + r);
        int c0z = worldToCell(t.center.y - r), c1z = worldToCell(t.center.y + r);
        for (int gz = c0z; gz <= c1z; gz++)
            for (int gx = c0x; gx <= c1x; gx++) {
                long long dx = cellWorld(gx) - t.center.x;
                long long dz = cellWorld(gz) - t.center.y;
                if (dx * dx + dz * dz < (long long)r * r)
                    townPen[(size_t)gz * GRID + gx] = 28.0f;
            }
    }

    auto highwayCost = [&hgt, &townPen](int gx, int gz) -> float {
        size_t k = (size_t)gz * GRID + gx;
        int h = (int)hgt[k];
        if (h < WORLD_SEA_LEVEL) return 22.0f + townPen[k];  // open water
        int rough = 0;                                       // local steepness
        if (gx > 0)        rough = std::max(rough, std::abs(h - (int)hgt[k - 1]));
        if (gx < GRID - 1) rough = std::max(rough, std::abs(h - (int)hgt[k + 1]));
        if (gz > 0)        rough = std::max(rough, std::abs(h - (int)hgt[k - GRID]));
        if (gz < GRID - 1) rough = std::max(rough, std::abs(h - (int)hgt[k + GRID]));
        return 1.0f + (float)rough * 0.20f + townPen[k];
    };

    // Link every settlement to its two nearest neighbours (deduplicated).
    std::set<std::pair<int, int>> edges;
    std::vector<std::pair<long long, int>> d;
    for (int i = 0; i < N; i++) {
        d.clear();
        for (int j = 0; j < N; j++) {
            if (j == i) continue;
            long long dx = plan.towns[i].center.x - plan.towns[j].center.x;
            long long dz = plan.towns[i].center.y - plan.towns[j].center.y;
            d.push_back({ dx * dx + dz * dz, j });
        }
        int take = std::min(2, (int)d.size());
        std::partial_sort(d.begin(), d.begin() + take, d.end());
        for (int k = 0; k < take; k++)
            edges.insert({ std::min(i, d[k].second), std::max(i, d[k].second) });
    }

    // A point clear of town `t`, ~radius+50 out toward `toward`, pulled to land.
    auto portalOf = [](const Town& t, glm::ivec2 toward) {
        float dx = (float)(toward.x - t.center.x);
        float dz = (float)(toward.y - t.center.y);
        float L  = std::sqrt(dx * dx + dz * dz);
        if (L < 1.0f) { dx = 1.0f; dz = 0.0f; L = 1.0f; }
        glm::ivec2 p = t.center;
        for (int reach = t.radius + 50; reach > t.radius; reach -= 6) {
            p.x = t.center.x + (int)(dx / L * reach);
            p.y = t.center.y + (int)(dz / L * reach);
            if (sampleSurface(p.x, p.y).height >= WORLD_SEA_LEVEL) break;
        }
        return p;
    };

    // Bearing, measured from `c`, at which polyline `pts` first reaches radius
    // `R` (optionally scanning from the far end). Used to seat a wall gate where
    // the highway actually crosses the wall — the road weaves out of town, so
    // that point is offset from the straight-line bearing to the neighbour.
    auto crossingAngle = [](const std::vector<glm::ivec2>& pts, glm::ivec2 c,
                            float R, bool fromEnd) -> float {
        int n = (int)pts.size();
        glm::vec2 cf((float)c.x, (float)c.y), prev = cf;
        for (int k = 0; k < n; k++) {
            int i = fromEnd ? (n - 1 - k) : k;
            glm::vec2 p((float)pts[i].x, (float)pts[i].y);
            float d = glm::length(p - cf);
            if (d >= R) {
                float dp = glm::length(prev - cf);
                float t  = (R - dp) / std::max(0.001f, d - dp);
                glm::vec2 x = prev + (p - prev) * glm::clamp(t, 0.0f, 1.0f);
                return std::atan2(x.y - cf.y, x.x - cf.x);
            }
            prev = p;
        }
        glm::vec2 last((float)pts[fromEnd ? 0 : n - 1].x,
                       (float)pts[fromEnd ? 0 : n - 1].y);
        return std::atan2(last.y - cf.y, last.x - cf.x);
    };

    for (const auto& e : edges) {
        const Town& tA = plan.towns[e.first];
        const Town& tB = plan.towns[e.second];
        glm::ivec2 portA = portalOf(tA, tB.center);
        glm::ivec2 portB = portalOf(tB, tA.center);

        glm::ivec2 sc(worldToCell(portA.x), worldToCell(portA.y));
        glm::ivec2 gc(worldToCell(portB.x), worldToCell(portB.y));
        std::vector<glm::ivec2> trunk =
            astar(GRID, GRID, sc, gc, highwayCost, 1.3f, 220000);

        // Full route: weave out of A, direct trunk, weave into B.
        std::vector<glm::ivec2> route = routeTownExit(tA, portA);
        for (const glm::ivec2& cc : trunk)
            route.push_back(glm::ivec2(cellWorld(cc.x), cellWorld(cc.y)));
        std::vector<glm::ivec2> exitB = routeTownExit(tB, portB);
        for (size_t ci = exitB.size(); ci-- > 0; )
            route.push_back(exitB[ci]);

        // A walled town opens a gate where this highway crosses its wall, so the
        // gateway lines up with the road that runs through it.
        if (tA.wallRadius > 0)
            plan.towns[e.first].gateAngles.push_back(
                crossingAngle(route, tA.center, (float)tA.wallRadius, false));
        if (tB.wallRadius > 0)
            plan.towns[e.second].gateAngles.push_back(
                crossingAngle(route, tB.center, (float)tB.wallRadius, true));

        emitHighwayRoute(plan, route);
    }
}

// Places street lights along the gravel paths between houses and along the
// first stretch of each highway leaving town. Stored as world-XZ positions;
// the prop streamer spawns a lantern-post Prop at each (prop_placement.cpp).
void placeStreetLamps(TownPlan& plan) {
    for (Town& t : plan.towns) {
        auto tooClose = [&](int wx, int wz) {
            for (const glm::ivec2& L : t.lampPosts)
                if (std::abs(L.x - wx) + std::abs(L.y - wz) < 11) return true;
            return false;
        };
        auto inBuilding = [&](int wx, int wz) {
            for (const TownBuilding& b : t.buildings)
                if (wx >= b.wx - 1 && wx < b.wx + b.dimX + 1 &&
                    wz >= b.wz - 1 && wz < b.wz + b.dimZ + 1) return true;
            return false;
        };
        int side = 0;
        // Walks a polyline placing a lamp every `spacing` blocks of *cumulative*
        // arc length (the polylines are made of short ~2-4 block segments).
        auto walkRoad = [&](const std::vector<glm::ivec2>& pts, int spacing,
                            float maxFromCentre) {
            float nextAt   = (float)spacing * 0.5f;
            float traveled = 0.0f;
            for (size_t i = 0; i + 1 < pts.size(); i++) {
                glm::ivec2 a = pts[i], b = pts[i + 1];
                float dx = (float)(b.x - a.x), dz = (float)(b.y - a.y);
                float segLen = std::sqrt(dx * dx + dz * dz);
                if (segLen < 0.01f) continue;
                float perpX = -dz / segLen, perpZ = dx / segLen;
                while (nextAt <= traveled + segLen) {
                    float u  = (nextAt - traveled) / segLen;
                    int   px = a.x + (int)(dx * u), pz = a.y + (int)(dz * u);
                    nextAt  += (float)spacing;
                    if (maxFromCentre > 0.0f) {
                        float cdx = (float)(px - t.center.x);
                        float cdz = (float)(pz - t.center.y);
                        if (cdx * cdx + cdz * cdz > maxFromCentre * maxFromCentre)
                            continue;
                    }
                    int s  = (side++ & 1) ? 1 : -1;
                    // Offset clear of the widest a path ever varies to (see pathHalfWidth).
                    int lx = px + (int)(perpX * 5.2f * (float)s);
                    int lz = pz + (int)(perpZ * 5.2f * (float)s);
                    if (inBuilding(lx, lz) || tooClose(lx, lz)) continue;
                    t.lampPosts.push_back(glm::ivec2(lx, lz));
                }
                traveled += segLen;
            }
        };
        for (const TownRoad& p : t.paths)        walkRoad(p.pts, 15,   0.0f);
        for (const TownRoad& h : plan.highways)  walkRoad(h.pts, 20, 130.0f);
    }
}

// --- Survey & plan -----------------------------------------------------------

TownPlan buildTownPlan() {
    std::cout << "[Towns] Surveying region for settlement sites..." << std::endl;
    TownPlan plan;
    std::mt19937 rng(worldSeed() ^ 0x70776E21u);

    reportStage(0, 0.0f);                  // "Surveying terrain"
    const int n = GRID * GRID;
    std::vector<int16_t> hgt(n);
    std::vector<uint8_t> bio(n);
    // Parallel surface sampling — each row is independent and the noise
    // functions are read-only once seeded, so we can scale across cores. On
    // typical hardware this drops the longest survey pass from seconds to a
    // fraction of a second. Workers claim rows from a shared atomic counter
    // so threads with cheaper rows steal work from the slower ones.
    {
        const int nWorkers = std::max(1,
                                (int)std::thread::hardware_concurrency() - 1);
        std::atomic<int> nextRow{0};
        std::atomic<int> doneRows{0};
        std::vector<std::thread> workers;
        workers.reserve(nWorkers);
        for (int w = 0; w < nWorkers; w++) {
            workers.emplace_back([&]() {
                while (true) {
                    int gz = nextRow.fetch_add(1, std::memory_order_relaxed);
                    if (gz >= GRID) break;
                    for (int gx = 0; gx < GRID; gx++) {
                        SurfaceSample s = sampleSurface(cellWorld(gx), cellWorld(gz));
                        hgt[gz * GRID + gx] = (int16_t)s.height;
                        bio[gz * GRID + gx] = (uint8_t)s.biome;
                    }
                    int done = doneRows.fetch_add(1, std::memory_order_relaxed) + 1;
                    if ((done & 0xF) == 0)
                        reportStage(0, (float)done / (float)GRID);
                }
            });
        }
        for (auto& t : workers) t.join();
    }
    reportStage(0, 1.0f);

    auto at = [&](int gx, int gz) { return (int)hgt[gz * GRID + gx]; };

    struct Site { int gx, gz, baseY; float score; TownType type; bool ok; };
    auto evalCell = [&](int gx, int gz) -> Site {
        Site s{}; s.ok = false;
        if (gx < 3 || gx >= GRID - 3 || gz < 3 || gz >= GRID - 3) return s;
        int h = at(gx, gz);
        if (h < WORLD_SEA_LEVEL + 2) return s;

        int hi = h, lo = h;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                int hh = at(gx + dx, gz + dz);
                hi = std::max(hi, hh);
                lo = std::min(lo, hh);
            }
        int spread = hi - lo;

        bool nearWater = false;
        for (int dz = -2; dz <= 2 && !nearWater; dz++)
            for (int dx = -2; dx <= 2; dx++)
                if (at(gx + dx, gz + dz) < WORLD_SEA_LEVEL) { nearWater = true; break; }

        int b = bio[gz * GRID + gx];
        TownType type;
        if (nearWater)
            type = TownType::Coastal;
        else if (h > WORLD_SEA_LEVEL + 40 || b == BIOME_MOUNTAINS || b == BIOME_TUNDRA)
            type = TownType::Mountain;
        else
            type = TownType::Grassland;

        int maxFlat = (type == TownType::Mountain) ? 26 : 16;
        if (spread > maxFlat) return s;

        s.gx = gx; s.gz = gz; s.baseY = h;
        s.score = (float)(maxFlat - spread);
        s.type  = type;
        s.ok    = true;
        return s;
    };

    const int BCOUNT  = (REGION * 2) / BUCKET;
    const int CELLS_B = GRID / BCOUNT;
    std::uniform_real_distribution<float> jitter(0.0f, 3.0f);

    reportStage(1, 0.0f);                  // "Selecting town sites"
    std::vector<Site> sites;
    for (int bz = 0; bz < BCOUNT; bz++)
        for (int bx = 0; bx < BCOUNT; bx++) {
            Site best{}; best.ok = false; best.score = -1.0f;
            for (int cz = 0; cz < CELLS_B; cz++)
                for (int cx = 0; cx < CELLS_B; cx++) {
                    Site s = evalCell(bx * CELLS_B + cx, bz * CELLS_B + cz);
                    if (!s.ok) continue;
                    float sc = s.score + jitter(rng);
                    if (sc > best.score) { best = s; best.score = sc; }
                }
            if (best.ok) sites.push_back(best);
        }

    std::sort(sites.begin(), sites.end(),
              [](const Site& a, const Site& b) { return a.score > b.score; });
    for (const Site& s : sites) {
        int wx = cellWorld(s.gx), wz = cellWorld(s.gz);

        // Roll this settlement's scale up front so the spacing check can scale
        // with it. A cubic bias on a uniform roll yields mostly hamlets and
        // villages with the occasional large town or sprawling city — a wide
        // spread from ~5 houses up to ~100. Deterministic per site so the plan
        // is stable for a given world.
        std::mt19937 srng(worldSeed()
                          ^ (uint32_t)(wx * 374761393)
                          ^ (uint32_t)(wz * 668265263));
        float u = (float)(srng() & 0xFFFFFFu) / (float)0x1000000u;   // [0,1)
        int   targetHouses = 5 + (int)(95.0f * u * u * u + 0.5f);    // 5..100
        // Houses fill concentric rings over a disc, so capacity grows with the
        // square of the radius — hence radius ~ sqrt(houses), plus a floor so
        // even the smallest hamlet has elbow room.
        int   radius = 24 + (int)(13.0f * std::sqrt((float)targetHouses));

        // Radius-aware spacing: keep one town's hard-flattened footprint
        // (~radius + 80 at its noisy lobes) clear of the next, with the old
        // 360-block minimum as a floor so clusters of hamlets still breathe.
        bool ok = true;
        for (const Town& t : plan.towns) {
            long long dx = wx - t.center.x, dz = wz - t.center.y;
            long long minD  = (long long)(radius + t.radius) + 170;
            long long minSq = std::max(360LL * 360LL, minD * minD);
            if (dx * dx + dz * dz < minSq) { ok = false; break; }
        }
        if (!ok) continue;

        Town t;
        t.center       = { wx, wz };
        t.baseY        = s.baseY;
        t.type         = s.type;
        t.name         = makeTownName(wx, wz, t.type);
        t.targetHouses = targetHouses;
        t.radius       = radius;
        // Larger settlements (20+ houses) are ringed by a defensive wall just
        // outside the outermost houses; villages stay open. The style (wooden
        // palisade, stone wall, great rampart, sandstone) varies by size/type.
        t.wallRadius   = (targetHouses >= 20) ? radius + 30 : 0;
        t.wallStyle    = pickWallStyle(targetHouses, s.type, srng());
        // The coarse Village/Town flag still drives guard counts and building
        // template bias elsewhere; anything sizeable counts as a Town.
        t.size         = (targetHouses >= 16) ? TownSize::Town : TownSize::Village;
        plan.towns.push_back(std::move(t));
    }

    // Lay out every settlement (well + houses + farms) and route its paths.
    reportStage(2, 0.0f);                  // "Designing settlements"
    size_t totalBuildings = 0;
    for (size_t i = 0; i < plan.towns.size(); i++) {
        layoutTown(plan.towns[i]);
        routeTownPaths(plan.towns[i]);
        totalBuildings += plan.towns[i].buildings.size();
        reportStage(2, (float)(i + 1) / (float)plan.towns.size());
    }

    // Inter-town highways: curvy, terrain-following routes with shoreline docks.
    reportStage(3, 0.0f);                  // "Routing roads"
    routeHighways(plan, hgt);

    // Street lights for every town's paths and highway approaches.
    reportStage(4, 0.0f);                  // "Lighting streets"
    placeStreetLamps(plan);
    reportStage(4, 1.0f);

    int nc = 0, nm = 0, ng = 0;
    for (const Town& t : plan.towns)
        (t.type == TownType::Coastal ? nc : t.type == TownType::Mountain ? nm : ng)++;
    std::cout << "[Towns] Placed " << plan.towns.size() << " settlements ("
              << nc << " coastal, " << nm << " mountain, " << ng << " grassland), "
              << totalBuildings << " buildings, "
              << plan.highways.size() << " highways, "
              << plan.bridges.size() << " bridges, "
              << plan.docks.size() << " docks." << std::endl;
    return plan;
}

// --- Stamping ----------------------------------------------------------------

void stampBuilding(Chunk* c, const TownBuilding& b) {
    if (b.dimX == 0) return;
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    if (b.wx >= ox + CHUNK_SIZE || b.wx + b.dimX <= ox) return;
    if (b.wz >= oz + CHUNK_SIZE || b.wz + b.dimZ <= oz) return;

    int x0 = std::max(0, ox - b.wx), x1 = std::min(b.dimX, ox + CHUNK_SIZE - b.wx);
    int z0 = std::max(0, oz - b.wz), z1 = std::min(b.dimZ, oz + CHUNK_SIZE - b.wz);

    for (int x = x0; x < x1; x++)
        for (int z = z0; z < z1; z++) {
            int wx = b.wx + x, wz = b.wz + z;
            int lx = wx - ox,  lz = wz - oz;

            // Clear the column the building actually occupies (terrain bumps,
            // tree trunks/leaves that would clip through walls or the roof).
            // Anything *above* the building's top is left alone, so a town in
            // a forest keeps its canopy poking up above the rooftops instead
            // of carving a bare-sky disc out of the trees. A trunk that ran
            // through the building footprint is removed, which can leave a
            // floating crown — but that reads a lot more like "house built
            // in the woods" than the old strip-mined version did.
            for (int wy = b.baseY; wy < b.baseY + b.dimY; wy++)
                c->set(lx, wy, lz, BlockType::Air);

            // Stamp the building (its own Air cells carve clean space).
            for (int y = 0; y < b.dimY; y++)
                c->set(lx, b.baseY + y, lz,
                       (BlockType)b.blocks[((size_t)y * b.dimZ + z) * b.dimX + x]);

            // Foundation skirt: fill solid from the plot base down past the
            // real ground (at least 12 blocks) so a house never floats over a
            // dip in the terrain. Scans the chunk's own blocks — ground truth,
            // unlike sampleSurface() which is only the predicted height.
            for (int wy = b.baseY - 1; wy >= 0; wy--) {
                BlockType cur = c->get(lx, wy, lz);
                bool ground = cur != BlockType::Air   && cur != BlockType::Water  &&
                              cur != BlockType::Wood  && cur != BlockType::Leaves &&
                              cur != BlockType::LeavesOrange &&
                              cur != BlockType::LeavesRed    &&
                              cur != BlockType::LeavesPink   && cur != BlockType::Cactus;
                if (ground && b.baseY - wy > 12) break;
                c->set(lx, wy, lz, BlockType::Stone);
            }
        }
}

// Builds a descending stone staircase from a building's front door down to
// the terrain, so a building on raised ground stays reachable from the
// street. The steps run toward the town centre and are clipped to chunk `c`.
void stampHouseSteps(Chunk* c, const TownBuilding& b) {
    if (b.rooms.empty()) return;                      // any kind with a front door
    if (b.doorDX == 0 && b.doorDZ == 0) return;
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;

    int wallX, wallZ;                                 // centre of the door wall
    if      (b.doorDZ < 0) { wallX = b.wx + b.dimX / 2; wallZ = b.wz; }
    else if (b.doorDZ > 0) { wallX = b.wx + b.dimX / 2; wallZ = b.wz + b.dimZ - 1; }
    else if (b.doorDX < 0) { wallX = b.wx;              wallZ = b.wz + b.dimZ / 2; }
    else                   { wallX = b.wx + b.dimX - 1; wallZ = b.wz + b.dimZ / 2; }

    for (int k = 1; k <= 14; k++) {                   // each step drops one block
        int cx = wallX + b.doorDX * k;
        int cz = wallZ + b.doorDZ * k;
        int stepY = b.baseY - k;
        for (int w = -1; w <= 1; w++) {               // 3-wide, across the doorway
            int wx = cx + (b.doorDZ != 0 ? w : 0);
            int wz = cz + (b.doorDX != 0 ? w : 0);
            int lx = wx - ox, lz = wz - oz;
            if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

            int g = -1;                               // top solid (non-foliage) block
            for (int y = b.baseY + 4; y >= 0; y--) {
                BlockType t = c->get(lx, y, lz);
                if (t == BlockType::Air || t == BlockType::Water ||
                    t == BlockType::Wood || t == BlockType::Leaves ||
                    t == BlockType::LeavesOrange || t == BlockType::LeavesRed ||
                    t == BlockType::LeavesPink || t == BlockType::Cactus) continue;
                g = y; break;
            }
            if (stepY <= g) continue;                 // ground already at/above the step
            for (int y = stepY; y > g && y >= 0; y--) // solid step, no float
                c->set(lx, y, lz, BlockType::Stone);
            for (int y = stepY + 1; y <= stepY + 3 && y < CHUNK_HEIGHT; y++)
                c->set(lx, y, lz, BlockType::Air);    // walking headroom
        }
    }
}

// --- Path width --------------------------------------------------------------
// Gravel paths are not a fixed width: each one widens and narrows along its
// length. The half-width at a point comes from smooth 1-D value noise of the
// arc length travelled, so a given path position yields the same width no
// matter which chunk happens to stamp it.
constexpr float PATH_HW_MIN  = 2.0f;   // narrowest -> ~4 voxels across
constexpr float PATH_HW_MAX  = 4.0f;   // widest    -> ~8 voxels across
constexpr int   PATH_HW_CEIL = 4;      // ceil(PATH_HW_MAX) — chunk-clip margin

// Smooth value noise in [0,1]: lattice points are hashed and blended with a
// smoothstep, giving a continuous curve with no abrupt steps.
float pathNoise1D(float x, uint32_t seed) {
    int   i0 = (int)std::floor(x);
    float f  = x - (float)i0;
    f = f * f * (3.0f - 2.0f * f);
    auto h = [seed](int i) {
        uint32_t u = (uint32_t)i * 0x9E3779B1u ^ seed;
        u ^= u >> 16; u *= 0x85EBCA77u; u ^= u >> 13;
        return (float)(u & 0xFFFFFFu) / (float)0xFFFFFFu;
    };
    float a = h(i0), b = h(i0 + 1);
    return a + (b - a) * f;
}

// Path half-width `arc` blocks along a path. A low-frequency octave drives the
// broad widen/narrow; a smaller octave adds finer texture to the edge.
float pathHalfWidth(float arc, uint32_t seed) {
    float n = pathNoise1D(arc * (1.0f / 25.0f), seed)           * 0.75f
            + pathNoise1D(arc * (1.0f /  9.0f), seed ^ 0xA53Cu) * 0.25f;
    return PATH_HW_MIN + n * (PATH_HW_MAX - PATH_HW_MIN);
}

// Lays one road cell. Two surface mixes are produced, picked by `sink`:
//   - sink == 0 (intra-town paths): gravel + stone cobble mix.
//   - sink >  0 (cross-country highways): mostly dirt with a sprinkle of
//     gravel, so the country lanes read as worn dirt tracks rather than
//     paved roads.
// Either way the column above is cleared so the path stays walkable.
void stampRoadCell(Chunk* c, int lx, int lz, int sink) {
    int gtop = -1;
    for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
        BlockType b = c->get(lx, y, lz);
        if (b == BlockType::Air || b == BlockType::Water || b == BlockType::Wood ||
            b == BlockType::Leaves || b == BlockType::LeavesOrange ||
            b == BlockType::LeavesRed || b == BlockType::LeavesPink ||
            b == BlockType::Cactus) continue;
        gtop = y; break;
    }
    if (gtop < 0) return;
    // Idempotent guard: a wide road revisits each cell from many overlapping
    // stamps. If the surface is already any of the road materials, stop —
    // otherwise each revisit would re-engrave it one block deeper.
    BlockType already = c->get(lx, gtop, lz);
    if (already == BlockType::Gravel || already == BlockType::Stone ||
        already == BlockType::Dirt) return;

    int roadY;
    if (gtop >= WORLD_SEA_LEVEL) {
        roadY = gtop - sink;                            // land — engraved `sink` blocks down
    } else {
        roadY = WORLD_SEA_LEVEL;                        // water — a stone causeway
        for (int y = gtop + 1; y < roadY; y++) c->set(lx, y, lz, BlockType::Stone);
    }
    // Per-cell deterministic hash on world XZ so the same patch of road
    // looks the same every visit and across chunk boundaries.
    int wx = c->pos.x * CHUNK_SIZE + lx;
    int wz = c->pos.z * CHUNK_SIZE + lz;
    uint32_t h = (uint32_t)wx * 0x9E3779B1u
               ^ (uint32_t)wz * 0x85EBCA77u
               ^ 0xC0BB1Eu;
    h ^= h >> 16;
    int   r       = (int)(h & 0x3F);     // 0..63
    BlockType surface;
    if (sink == 0) {
        // Town path: ~60% gravel + ~40% stone, cobble look.
        surface = (r < 25) ? BlockType::Stone : BlockType::Gravel;
    } else {
        // Country highway: a plain dirty-brown dirt track.
        surface = BlockType::Dirt;
    }
    (void)r;     // r is unused for the highway case; keep the seed step for parity
    c->set(lx, roadY, lz, surface);

    // Keep the path on the ground: a tree rooted on the road is removed whole,
    // but a canopy that merely overhangs the path is left intact.
    if (c->get(lx, roadY + 1, lz) == BlockType::Wood ||
        c->get(lx, roadY + 1, lz) == BlockType::Cactus) {
        for (int y = roadY + 1; y < CHUNK_HEIGHT; y++) {
            BlockType b = c->get(lx, y, lz);
            if (b != BlockType::Wood && b != BlockType::Leaves &&
                b != BlockType::LeavesOrange && b != BlockType::LeavesRed &&
                b != BlockType::LeavesPink && b != BlockType::Cactus)
                break;
            c->set(lx, y, lz, BlockType::Air);
        }
    } else {
        c->set(lx, roadY + 1, lz, BlockType::Air);       // a little walking headroom
        c->set(lx, roadY + 2, lz, BlockType::Air);
    }
}

// Rasterises a road polyline into this chunk; each segment is slab-clipped to
// the chunk so only the part that actually crosses it is drawn. The stamp is a
// disc whose radius varies smoothly along the path (see pathHalfWidth).
void stampRoad(Chunk* c, const TownRoad& r, int sink) {
    if (r.pts.size() < 2) return;
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const int hw = PATH_HW_CEIL;                        // widest the disc reaches
    const int xmin = ox - hw - 1, xmax = ox + CHUNK_SIZE + hw;
    const int zmin = oz - hw - 1, zmax = oz + CHUNK_SIZE + hw;

    // Per-road seed: each path's width wobble is unique but deterministic, so
    // every chunk that re-stamps the road agrees on the width along it.
    uint32_t seed = (uint32_t)worldSeed()
                  ^ (uint32_t)(r.pts[0].x * 0x9E3779B1u)
                  ^ (uint32_t)(r.pts[0].y * 0x85EBCA77u);

    float arc = 0.0f;                                   // arc length to segment start
    for (size_t i = 0; i + 1 < r.pts.size(); i++) {
        int ax = r.pts[i].x,     az = r.pts[i].y;
        int dx = r.pts[i+1].x - ax, dz = r.pts[i+1].y - az;
        float segLen = std::sqrt((float)(dx * dx + dz * dz));

        float t0 = 0.0f, t1 = 1.0f;
        bool ok = true;
        auto slab = [&](int a, int d, int lo, int hi) {
            if (d == 0) { if (a < lo || a > hi) ok = false; return; }
            float ta = (float)(lo - a) / (float)d, tb = (float)(hi - a) / (float)d;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) ok = false;
        };
        slab(ax, dx, xmin, xmax);
        if (ok) slab(az, dz, zmin, zmax);
        if (!ok) { arc += segLen; continue; }

        int steps = std::max(std::abs(dx), std::abs(dz));
        if (steps == 0) steps = 1;
        int s0 = std::max(0,     (int)std::floor(t0 * steps));
        int s1 = std::min(steps, (int)std::ceil (t1 * steps));
        for (int s = s0; s <= s1; s++) {
            int px = ax + (int)((long long)dx * s / steps);
            int pz = az + (int)((long long)dz * s / steps);

            // Disc radius for this point, from the arc length reached so far.
            float w  = pathHalfWidth(arc + segLen * ((float)s / (float)steps), seed);
            float w2 = w * w;
            int   wi = (int)std::ceil(w);
            for (int ddx = -wi; ddx <= wi; ddx++)
                for (int ddz = -wi; ddz <= wi; ddz++) {
                    if ((float)(ddx * ddx + ddz * ddz) > w2) continue;
                    int lx = px + ddx - ox, lz = pz + ddz - oz;
                    if (lx >= 0 && lx < CHUNK_SIZE && lz >= 0 && lz < CHUNK_SIZE)
                        stampRoadCell(c, lx, lz, sink);
                }
        }
        arc += segLen;
    }
}

// Stamps a wooden jetty: a 3-wide plank deck at sea level reaching out over the
// water, carried on posts driven down to the seabed.
void stampDock(Chunk* c, const TownDock& d) {
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const int deckY = WORLD_SEA_LEVEL;
    const int px = d.dz, pz = d.dx;                     // perpendicular (width) axis

    for (int i = 1; i <= DOCK_LEN; i++) {
        int cx = d.root.x + d.dx * i;
        int cz = d.root.y + d.dz * i;
        for (int w = -1; w <= 1; w++) {
            int wx = cx + px * w, wz = cz + pz * w;
            int lx = wx - ox, lz = wz - oz;
            if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

            c->set(lx, deckY, lz, BlockType::Wood);             // deck plank
            for (int y = deckY + 1; y < CHUNK_HEIGHT; y++)      // keep it walkable
                c->set(lx, y, lz, BlockType::Air);

            if (w == 0 && (i == DOCK_LEN || i % 3 == 0)) {      // support post
                int seabed = 0;
                for (int y = deckY - 1; y >= 0; y--) {
                    BlockType b = c->get(lx, y, lz);
                    if (b != BlockType::Air && b != BlockType::Water) { seabed = y; break; }
                }
                for (int y = seabed + 1; y < deckY; y++)
                    c->set(lx, y, lz, BlockType::Wood);
            }
        }
    }
}

// Stamps a raised plank bridge: a 3-wide deck at deckY with the span above it
// cleared, carried on wooden posts driven down to the terrain.
void stampBridge(Chunk* c, const TownBridge& br) {
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const int deckY = br.deckY;
    const int DECK_HALF = 3;          // 7-wide deck (~5 walkable between railings)
    const int xmin = ox - DECK_HALF - 2, xmax = ox + CHUNK_SIZE + DECK_HALF + 1;
    const int zmin = oz - DECK_HALF - 2, zmax = oz + CHUNK_SIZE + DECK_HALF + 1;

    // Helper: set one cell, clamped to this chunk.
    auto setCell = [&](int wx, int wz, int y, BlockType t) {
        int lx = wx - ox, lz = wz - oz;
        if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) return;
        if (y < 0 || y >= CHUNK_HEIGHT) return;
        c->set(lx, y, lz, t);
    };
    // Helper: clear the column above (wx, wz) starting at yStart so the deck
    // / stair stays walkable.
    auto clearAbove = [&](int wx, int wz, int yStart) {
        int lx = wx - ox, lz = wz - oz;
        if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) return;
        for (int y = std::max(0, yStart); y < CHUNK_HEIGHT; y++)
            c->set(lx, y, lz, BlockType::Air);
    };
    // Helper: place one row of deck/stair planks (5 wide perpendicular to the
    // travel direction) at world (wx, wz) with the row at `y`. The two outer
    // cells of every row get a wooden railing one block above, so the bridge
    // / stair is fenced on both sides for its full length.
    auto placeRow = [&](int wx, int wz, int y, bool horizDir) {
        for (int w = -DECK_HALF; w <= DECK_HALF; w++) {
            int cx = horizDir ? wx : wx + w;
            int cz = horizDir ? wz + w : wz;
            setCell(cx, cz, y, BlockType::Wood);
            clearAbove(cx, cz, y + 1);
            if (std::abs(w) == DECK_HALF)
                setCell(cx, cz, y + 1, BlockType::Wood);   // railing
        }
    };

    // --- Deck ---------------------------------------------------------------
    // Walk every segment of the centreline, clipping the iteration window to
    // this chunk's slab (plus the deck overhang) so we only place cells the
    // chunk owns. arc counts steps along the whole bridge so railing posts
    // sit on a global spacing rather than per-segment.
    int arc = 0;
    for (size_t i = 0; i + 1 < br.pts.size(); i++) {
        int ax = br.pts[i].x, az = br.pts[i].y;
        int dx = br.pts[i + 1].x - ax, dz = br.pts[i + 1].y - az;

        float t0 = 0.0f, t1 = 1.0f;
        bool ok = true;
        auto slab = [&](int p, int dd, int lo, int hi) {
            if (dd == 0) { if (p < lo || p > hi) ok = false; return; }
            float ta = (float)(lo - p) / (float)dd, tb = (float)(hi - p) / (float)dd;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) ok = false;
        };
        slab(ax, dx, xmin, xmax);
        if (ok) slab(az, dz, zmin, zmax);

        int steps = std::max(std::abs(dx), std::abs(dz));
        if (steps == 0) steps = 1;
        bool horizDir = std::abs(dx) >= std::abs(dz);

        if (ok) {
            int s0 = std::max(0,     (int)std::floor(t0 * steps));
            int s1 = std::min(steps, (int)std::ceil (t1 * steps));
            for (int s = s0; s <= s1; s++) {
                int px = ax + (int)((long long)dx * s / steps);
                int pz = az + (int)((long long)dz * s / steps);
                placeRow(px, pz, deckY, horizDir);
                // Taller railing posts every 4 cells along the deck for a
                // grand-bridge silhouette.
                int curArc = arc + s;
                if ((curArc & 3) == 0) {
                    for (int w : { -DECK_HALF, DECK_HALF }) {
                        int cx = horizDir ? px : px + w;
                        int cz = horizDir ? pz + w : pz;
                        setCell(cx, cz, deckY + 2, BlockType::Wood);
                    }
                }
            }
        }
        arc += steps;
    }

    // --- Stone support pillars ---------------------------------------------
    // 2x2 stone footprint under every third centreline vertex, going from the
    // terrain surface up to just under the deck. Reads as a real masonry pier
    // rather than the single-block wooden post the bridge used before.
    for (size_t i = 0; i < br.pts.size(); i += 3) {
        int cx = br.pts[i].x, cz = br.pts[i].y;
        int gy = sampleSurface(cx, cz).height;
        for (int dwx = -1; dwx <= 0; dwx++)
            for (int dwz = -1; dwz <= 0; dwz++) {
                for (int y = std::max(0, gy); y < deckY; y++)
                    setCell(cx + dwx, cz + dwz, y, BlockType::Stone);
            }
    }

    // --- End staircases -----------------------------------------------------
    // Each end of the bridge gets a stair that steps outward by 1 block per
    // step and drops by 1 block in Y per step, until the step is at or below
    // the local ground. That fills the awkward vertical gap where the deck
    // used to leave the road floating in space.
    auto buildStair = [&](glm::ivec2 deckEnd, glm::ivec2 inward) {
        int outX = deckEnd.x - inward.x;
        int outZ = deckEnd.y - inward.y;
        // Normalise to the dominant cardinal direction.
        if (std::abs(outX) >= std::abs(outZ)) {
            outX = (outX > 0) - (outX < 0); outZ = 0;
        } else {
            outZ = (outZ > 0) - (outZ < 0); outX = 0;
        }
        if (outX == 0 && outZ == 0) return;
        bool horizDir = (outX != 0);
        for (int k = 1; k <= 32; k++) {
            int px = deckEnd.x + outX * k;
            int pz = deckEnd.y + outZ * k;
            int yStep = deckY - k;
            int gy = sampleSurface(px, pz).height;
            if (yStep < gy) break;            // step would dig into the ground
            placeRow(px, pz, yStep, horizDir);
            // Fill solid wood below the step down to the terrain so the stair
            // reads as a continuous earthwork ramp rather than a floating run
            // of free-standing planks.
            for (int w = -DECK_HALF; w <= DECK_HALF; w++) {
                int cx = horizDir ? px : px + w;
                int cz = horizDir ? pz + w : pz;
                for (int y = yStep - 1; y >= std::max(0, gy); y--)
                    setCell(cx, cz, y, BlockType::Wood);
            }
            if (yStep <= gy) break;           // stepped onto the ground — done
        }
    };

    if (br.pts.size() >= 2) {
        buildStair(br.pts.front(), br.pts[1]);
        buildStair(br.pts.back(),  br.pts[br.pts.size() - 2]);
    }
}

} // namespace

// --- Town survey progress (declared in town.h) ------------------------------

std::atomic<int>   gTownBuildStage{0};
std::atomic<float> gTownBuildFraction{0.0f};
const char* const  kTownBuildStageNames[] = {
    "Surveying terrain",
    "Selecting town sites",
    "Designing settlements",
    "Routing roads",
    "Lighting streets",
};
const int          kTownBuildStageCount =
    (int)(sizeof(kTownBuildStageNames) / sizeof(kTownBuildStageNames[0]));

const TownPlan& getTownPlan() {
    static TownPlan      plan;
    static std::once_flag once;
    std::call_once(once, [] {
        plan = buildTownPlan();
        g_townReady.store(true, std::memory_order_release);
    });
    return plan;
}

// Per-direction effective flat-zone radius for a town. Adds a three-octave
// sinusoidal noise on the angle from the town centre so the otherwise-perfect
// circular boundary becomes a lobed blob, and the surrounding smooth blend
// inherits the same irregular shape. Much harder to spot the "bit that got
// flattened" as a clean circle from the air. Each town gets its own noise
// phase so adjacent towns don't share lobe patterns.
static float townEffectiveFlatR(const Town& t, float dx, float dz) {
    float baseR = (float)t.radius + 52.0f;
    if (std::abs(dx) < 0.5f && std::abs(dz) < 0.5f) return baseR;
    float angle = std::atan2(dz, dx);
    uint32_t h = (uint32_t)t.center.x * 0xA24BAED4u
               ^ (uint32_t)t.center.y * 0xCC9E2D51u
               ^ 0xB10BB10Bu;
    float phase = ((float)(h & 0xFFFF) / 65535.0f) * 6.2831853f;
    float n = 0.55f * std::sin(angle * 2.0f  + phase)
            + 0.30f * std::sin(angle * 5.0f  + phase * 1.7f)
            + 0.15f * std::sin(angle * 11.0f + phase * 2.3f);
    // Amplitude scales with town size — bigger towns get larger lobes so the
    // boundary variance reads as proportionate, not as fixed wobble.
    float amp = std::min(28.0f, (float)t.radius * 0.30f + 12.0f);
    return baseR + n * amp;
}

// Gentle low-frequency tilt across one town: the town gets a random direction
// vector (derived from its centre) and the surface rises by +1 on one side
// and falls by -1 on the other. Returns one of {-1, 0, +1}. The tilt is the
// same every frame for a given town, so neighbouring chunks agree on the
// terrace shape and a path crosses a contour exactly once.
static int townSlopeOffset(const Town& t, int wx, int wz) {
    uint32_t h = (uint32_t)t.center.x * 0x9E3779B1u
               ^ (uint32_t)t.center.y * 0x85EBCA77u
               ^ 0xC0FFEEu;
    float theta = ((float)(h & 0xFFFF) / 65535.0f) * 6.2831853f;
    float dx    = (float)(wx - t.center.x);
    float dz    = (float)(wz - t.center.y);
    // Slope scale: one full step of ±1 reached around the town radius — so
    // a small village has a slightly steeper tilt than a large town, but both
    // top out at ±1 block across the settled zone.
    float r  = (float)t.radius + 20.0f;
    float tt = (dx * std::cos(theta) + dz * std::sin(theta)) / r;
    if (tt > 1.0f) tt = 1.0f; else if (tt < -1.0f) tt = -1.0f;
    return (int)std::round(tt);
}

// Hard flat-zone level: inside any town's settled radius the chunk generator
// snaps the column to this Y so the terrain is exactly flat (paths and doors
// then agree perfectly). Two behaviours are stacked:
//
//   - Cells inside (or within a small buffer of) a building footprint return
//     that building's own baseY, giving every house a strictly flat pad even
//     when the rest of the town is on a slight slope.
//   - Cells outside all building footprints return t.baseY plus a gentle ±1
//     low-frequency tilt (see townSlopeOffset), so the town no longer looks
//     uniformly flat. Paths follow the tilt and step ±1 across the contour
//     lines, but each door still meets the path at exactly the door's level
//     because the immediate building neighbourhood is held flat.
int townFlatLevelAt(int wx, int wz) {
    if (!g_townReady.load(std::memory_order_acquire)) return -1;
    const TownPlan& plan = getTownPlan();
    const Town* bestT = nullptr;
    float bestD2 = 1e30f;
    for (const Town& t : plan.towns) {
        float dx = (float)(wx - t.center.x);
        float dz = (float)(wz - t.center.y);
        float d2 = dx * dx + dz * dz;
        // Noisy boundary so the hard-flat zone isn't a perfect circle. The
        // same noise drives the smooth-blend ring below, so the chunk-gen
        // snap and the surface oracle agree on the shape.
        float flatR = townEffectiveFlatR(t, dx, dz);
        if (d2 >= flatR * flatR) continue;
        if (d2 < bestD2) { bestD2 = d2; bestT = &t; }
    }
    if (!bestT) return -1;

    // Building flat pad — extends a couple of cells past the footprint so
    // the path landing on the door is also forced to the door's level.
    const int BUF = 2;
    for (const TownBuilding& b : bestT->buildings) {
        if (wx < b.wx - BUF) continue;
        if (wx >= b.wx + b.dimX + BUF) continue;
        if (wz < b.wz - BUF) continue;
        if (wz >= b.wz + b.dimZ + BUF) continue;
        return b.baseY;
    }

    // Outside any building: the town's gentle tilt shows through.
    return bestT->baseY + townSlopeOffset(*bestT, wx, wz);
}

// Blends a raw surface height toward the base level of any nearby town, so
// settlements sit on relatively flat ground. A no-op until the plan is ready,
// which keeps the survey working on the natural, unflattened terrain.
float townFlattenedHeight(float wx, float wz, float rawHeight) {
    if (!g_townReady.load(std::memory_order_acquire)) return rawHeight;
    const TownPlan& plan = getTownPlan();
    float h = rawHeight;
    for (const Town& t : plan.towns) {
        float dx = wx - (float)t.center.x;
        float dz = wz - (float)t.center.y;
        // Match the noisy boundary the chunk generator uses for its hard
        // snap, then ease the influence back to natural over a generous
        // 80-block ring so the perimeter never reads as a clean edge.
        float flatR  = townEffectiveFlatR(t, dx, dz);
        float blendR = flatR + 80.0f;
        float d2 = dx * dx + dz * dz;
        if (d2 >= blendR * blendR) continue;
        float dist = std::sqrt(d2);
        float w;
        if (dist <= flatR) {
            w = 1.0f;
        } else {
            float u = (dist - flatR) / (blendR - flatR);
            w = 1.0f - u * u * (3.0f - 2.0f * u);   // smoothstep ease-out
        }
        h += ((float)t.baseY - h) * w;
    }
    return h;
}

// Stamps the perimeter wall of a walled town: a ring at `wallRadius` in the
// town's chosen style (wooden palisade, stone wall, great rampart, sandstone),
// with gateway openings (a clear passage under a lintel, flanked by taller
// tower sections) wherever a highway leaves toward a neighbour. The wall is
// derived per-column from the distance and bearing to the town centre, so it
// streams chunk-by-chunk like every other town feature, sitting on the town's
// flat base level with a foundation skirt over any dip in the ground.
void stampTownWall(Chunk* c, const Town& t) {
    if (t.wallRadius <= 0) return;
    const int   ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const float R = (float)t.wallRadius;
    const int   baseY = t.baseY;
    int styleIdx = (t.wallStyle >= 0 && t.wallStyle < WALL_STYLE_COUNT) ? t.wallStyle : 1;
    const WallStyleDef& st = WALL_STYLES[styleIdx];
    const int   WALL_H = st.height;
    const float HALF   = st.halfThick;

    // Cull: does the ring band actually cross this chunk's XZ box?
    float cxC   = std::min(std::max((float)t.center.x, (float)ox), (float)(ox + CHUNK_SIZE - 1));
    float czC   = std::min(std::max((float)t.center.y, (float)oz), (float)(oz + CHUNK_SIZE - 1));
    float nearD = std::sqrt(std::pow((float)t.center.x - cxC, 2.0f) +
                            std::pow((float)t.center.y - czC, 2.0f));
    float farDx = std::max(std::abs((float)t.center.x - ox),
                           std::abs((float)t.center.x - (ox + CHUNK_SIZE - 1)));
    float farDz = std::max(std::abs((float)t.center.y - oz),
                           std::abs((float)t.center.y - (oz + CHUNK_SIZE - 1)));
    float farD  = std::sqrt(farDx * farDx + farDz * farDz);
    if (farD < R - HALF - 0.5f || nearD > R + HALF + 0.5f) return;

    const float gateHalf = 5.0f / R;   // angular half-width of a gate opening
    const float towerArc = 2.2f / R;   // arc each flanking tower takes

    for (int lx = 0; lx < CHUNK_SIZE; lx++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int   wx = ox + lx, wz = oz + lz;
            float dx = (float)(wx - t.center.x), dz = (float)(wz - t.center.y);
            float dist = std::sqrt(dx * dx + dz * dz);
            if (std::abs(dist - R) > HALF) continue;          // wall thickness (by style)

            float ang = std::atan2(dz, dx);
            float ad  = 6.2831853f;
            for (float g : t.gateAngles) {
                float diff = ang - g;
                while (diff >  3.14159265f) diff -= 6.2831853f;
                while (diff < -3.14159265f) diff += 6.2831853f;
                ad = std::min(ad, std::abs(diff));
            }
            bool opening = (ad < gateHalf - towerArc);
            bool tower   = (!opening && ad < gateHalf);
            int  top     = baseY + WALL_H + (tower ? st.towerBonus : 0);

            if (opening) {
                // Gateway: clear the passage down to the engraved road level so
                // the highway runs straight through, with a lintel across the
                // top and a stone threshold flush with the road.
                for (int y = baseY; y <= baseY + WALL_H + 5 && y < CHUNK_HEIGHT; y++)
                    c->set(lx, y, lz, BlockType::Air);
                for (int y = baseY + WALL_H; y <= baseY + WALL_H + 1 && y < CHUNK_HEIGHT; y++)
                    c->set(lx, y, lz, st.mat);
                for (int y = baseY - 1; y >= 0; y--) {
                    BlockType cur = c->get(lx, y, lz);
                    if (cur != BlockType::Air && cur != BlockType::Water && (baseY - 1 - y) > 12) break;
                    c->set(lx, y, lz, BlockType::Stone);
                }
            } else {
                // Clear terrain / foliage out of the column, raise the wall
                // (crenellated, taller at the gate towers), and skirt it down.
                for (int y = baseY + 1; y <= baseY + WALL_H + 5 && y < CHUNK_HEIGHT; y++)
                    c->set(lx, y, lz, BlockType::Air);
                for (int y = baseY + 1; y <= top && y < CHUNK_HEIGHT; y++) {
                    // Crenellate stone battlements; palisades keep a solid top.
                    if (!tower && st.crenel && y == baseY + WALL_H && ((wx + wz) & 1)) continue;
                    c->set(lx, y, lz, st.mat);
                }
                for (int y = baseY; y >= 0; y--) {
                    BlockType cur = c->get(lx, y, lz);
                    if (cur != BlockType::Air && cur != BlockType::Water && baseY - y > 12) break;
                    c->set(lx, y, lz, BlockType::Stone);
                }
            }
        }
}

void stampTownChunk(Chunk* c) {
    const TownPlan& plan = getTownPlan();
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;

    auto ptsHit = [&](const std::vector<glm::ivec2>& pts) {
        int lox = 1 << 30, loz = 1 << 30, hix = -(1 << 30), hiz = -(1 << 30);
        for (const glm::ivec2& p : pts) {
            lox = std::min(lox, p.x); hix = std::max(hix, p.x);
            loz = std::min(loz, p.y); hiz = std::max(hiz, p.y);
        }
        return !(hix < ox - 3 || lox > ox + CHUNK_SIZE + 3 ||
                 hiz < oz - 3 || loz > oz + CHUNK_SIZE + 3);
    };

    // Roads & paths first — buildings stamp over them, so a road never shows
    // inside a house and an inter-town highway becomes a town's through-street.
    for (const Town& t : plan.towns) {
        if (t.bbMax.x <= ox - 4 || t.bbMin.x >= ox + CHUNK_SIZE + 4) continue;
        if (t.bbMax.y <= oz - 4 || t.bbMin.y >= oz + CHUNK_SIZE + 4) continue;
        for (const TownRoad& p : t.paths) stampRoad(c, p, 0);
    }
    for (const TownRoad& h : plan.highways)
        if (ptsHit(h.pts)) stampRoad(c, h, 1);   // highways engraved one block down

    // Bridges (raised decks over gullies and rivers).
    for (const TownBridge& br : plan.bridges)
        if (ptsHit(br.pts)) stampBridge(c, br);

    // Docks (jetties reaching over open water).
    for (const TownDock& d : plan.docks) {
        int ex = d.root.x + d.dx * DOCK_LEN, ez = d.root.y + d.dz * DOCK_LEN;
        int lox = std::min(d.root.x, ex) - 1, hix = std::max(d.root.x, ex) + 1;
        int loz = std::min(d.root.y, ez) - 1, hiz = std::max(d.root.y, ez) + 1;
        if (hix < ox || lox >= ox + CHUNK_SIZE) continue;
        if (hiz < oz || loz >= oz + CHUNK_SIZE) continue;
        stampDock(c, d);
    }

    // Perimeter walls (large towns only) — stamped before buildings so a house
    // always takes precedence over a stray wall column. The wall ring sits well
    // outside the building bounding box, so it needs its own centre/radius cull.
    for (const Town& t : plan.towns) {
        if (t.wallRadius <= 0) continue;
        int R = t.wallRadius + 2;
        if (t.center.x + R <= ox || t.center.x - R >= ox + CHUNK_SIZE) continue;
        if (t.center.y + R <= oz || t.center.y - R >= oz + CHUNK_SIZE) continue;
        stampTownWall(c, t);
    }

    // Buildings.
    for (const Town& t : plan.towns) {
        if (t.bbMax.x <= ox || t.bbMin.x >= ox + CHUNK_SIZE) continue;
        if (t.bbMax.y <= oz || t.bbMin.y >= oz + CHUNK_SIZE) continue;
        for (const TownBuilding& b : t.buildings) {
            stampBuilding(c, b);
            stampHouseSteps(c, b);
        }
    }
}
