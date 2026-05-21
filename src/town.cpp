#include "town.h"
#include "world.h"
#include "voxel_model.h"
#include <algorithm>
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

namespace {

// --- Survey parameters -------------------------------------------------------
constexpr int REGION      = 24000;   // half-extent of the settled belt (blocks)
constexpr int SURVEY_STEP = 64;      // coarse survey grid spacing
constexpr int GRID        = (REGION * 2) / SURVEY_STEP;   // cells per side
constexpr int BUCKET      = 2800;    // one settlement per BUCKET-sized region
constexpr int DOCK_LEN    = 9;       // jetty length out over the water (blocks)

// Biome ids — mirror the Biome enum order in world.cpp.
constexpr int BIOME_MOUNTAINS = 3, BIOME_TUNDRA = 4;

int cellWorld(int g) { return -REGION + g * SURVEY_STEP + SURVEY_STEP / 2; }

// Inverse of cellWorld: the survey cell that contains a world X or Z.
int worldToCell(int w) {
    int g = (w + REGION - SURVEY_STEP / 2) / SURVEY_STEP;
    return std::min(GRID - 1, std::max(0, g));
}

float frand(std::mt19937& r, float lo, float hi) {
    return lo + (float)(r() % 100000) / 100000.0f * (hi - lo);
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

// Generates a house, tight-crops it and rotates it by quadrant `q`, storing the
// world-oriented block grid in `b`.
void bakeHouse(TownBuilding& b, int templ, int roof, int mat, int q) {
    b.kind = 1;
    std::vector<BlockType> g;
    generateHouseGrid(templ, roof, mat, g);

    int mnx = HOUSE_VX, mny = HOUSE_VY, mnz = HOUSE_VZ, mxx = -1, mxy = -1, mxz = -1;
    for (int z = 0; z < HOUSE_VZ; z++)
        for (int y = 0; y < HOUSE_VY; y++)
            for (int x = 0; x < HOUSE_VX; x++)
                if (g[((size_t)z * HOUSE_VY + y) * HOUSE_VX + x] != BlockType::Air) {
                    mnx = std::min(mnx, x); mxx = std::max(mxx, x);
                    mny = std::min(mny, y); mxy = std::max(mxy, y);
                    mnz = std::min(mnz, z); mxz = std::max(mxz, z);
                }
    if (mxx < 0) { b.dimX = b.dimY = b.dimZ = 0; return; }

    int sx = mxx - mnx + 1, sy = mxy - mny + 1, sz = mxz - mnz + 1;
    q &= 3;
    b.dimX = (q % 2 == 0) ? sx : sz;
    b.dimZ = (q % 2 == 0) ? sz : sx;
    b.dimY = sy;
    b.blocks.assign((size_t)b.dimX * b.dimY * b.dimZ, (uint8_t)BlockType::Air);
    for (int y = 0; y < sy; y++)
        for (int z = 0; z < sz; z++)
            for (int x = 0; x < sx; x++) {
                BlockType bt = g[((size_t)(mnz + z) * HOUSE_VY + (mny + y)) * HOUSE_VX
                                 + (mnx + x)];
                int rx, rz;
                switch (q) {
                    case 1:  rx = z;          rz = sx - 1 - x; break;
                    case 2:  rx = sx - 1 - x; rz = sz - 1 - z; break;
                    case 3:  rx = sz - 1 - z; rz = x;          break;
                    default: rx = x;          rz = z;          break;
                }
                b.blocks[((size_t)y * b.dimZ + rz) * b.dimX + rx] = (uint8_t)bt;
            }
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
    bakeHouse(b, templ[rng() % nT], roofs[rng() % nR], mats[rng() % nM], q);
    if (b.dimX == 0) return false;
    b.wx = px - b.dimX / 2;
    b.wz = pz - b.dimZ / 2;
    for (const TownBuilding& o : t.buildings)
        if (boxesOverlap(b.wx - 3, b.wz - 3, b.dimX + 6, b.dimZ + 6,
                         o.wx, o.wz, o.dimX, o.dimZ))
            return false;
    b.baseY = sampleSurface(px, pz).height;
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
    b.baseY = sampleSurface(px, pz).height;
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
    for (int ring = 0; ring < 9 && placed < numH; ring++) {
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

    // Well at the town centre.
    {
        TownBuilding well;
        makeWell(well);
        well.wx    = t.center.x - well.dimX / 2;
        well.wz    = t.center.y - well.dimZ / 2;
        well.baseY = t.baseY;
        t.buildings.push_back(std::move(well));
    }

    const bool big = (t.size == TownSize::Town);
    static const int ROOFS[] = { 1, 2, 3 };   // gabled, hipped, pyramid

    if (t.type == TownType::Coastal) {
        static const int T[] = { 0, 1, 2, 4, 5 };   // bungalow/two-story/cottage/cabin/longhouse
        static const int M[] = { 7, 1, 5 };         // coastal / cottage / sandstone
        int numH = big ? 16 + (int)(rng() % 12) : 7 + (int)(rng() % 5);
        layoutRings(t, rng, numH, false, T, 5, M, 3, ROOFS, 2);
    } else if (t.type == TownType::Mountain) {
        static const int T[] = { 1, 2, 3, 4, 8 };   // two-story/cottage/tower/cabin/hall
        static const int M[] = { 2, 4, 0, 6 };      // stone / cabin / timber / forest
        int numH = big ? 11 + (int)(rng() % 9) : 5 + (int)(rng() % 4);
        layoutRings(t, rng, numH, true, T, 5, M, 4, ROOFS + 1, 2);  // hipped/pyramid
    } else {
        static const int T[] = { 0, 1, 2, 4, 5, 7 };  // grassland mix
        static const int M[] = { 0, 1, 4, 8 };        // timber/cottage/cabin/autumn
        int numH = big ? 16 + (int)(rng() % 13) : 7 + (int)(rng() % 5);
        layoutRings(t, rng, numH, false, T, 6, M, 4, ROOFS, 2);
    }

    // An outer ring of fenced farm plots (sparse for mountain hamlets).
    int numFarms;
    if (t.type == TownType::Mountain)  numFarms = (int)(rng() % 2);          // 0..1
    else if (big)                      numFarms = 3 + (int)(rng() % 2);      // 3..4
    else                               numFarms = 2 + (int)(rng() % 2);      // 2..3
    int farmRing = t.radius + 22;
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
        if (b.kind != 1) continue;                          // paths originate at houses only
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
            int h0  = H[i];
            int lim = std::min(n - 1, i + STEEP);
            int j   = i + 1;
            while (j <= lim && H[j] > h0 - MINDROP) j++;
            if (j > lim) { i++; continue; }
            int s = i;                                      // rim = highest point pre-dip
            for (int q = i; q <= j; q++) if (H[q] > H[s]) s = q;
            int deckY = H[s];
            int lim2  = std::min(n - 1, s + MAXSPAN);
            int k     = j + 1;
            while (k <= lim2 && H[k] < deckY - 2) k++;
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
        for (int idx = a; idx <= b; idx++) {
            if (H[idx] >= WORLD_SEA_LEVEL) {
                if (inWater) {                              // water -> land
                    cur = TownRoad();
                    cur.pts.push_back(placeDock(plan, P[idx], P[idx - 1]));
                    inWater = false;
                }
                cur.pts.push_back(P[idx]);
                lastLand = P[idx];
            } else if (!inWater) {                          // land -> water
                cur.pts.push_back(placeDock(plan, lastLand, P[idx]));
                if (cur.pts.size() >= 2) plan.highways.push_back(std::move(cur));
                cur = TownRoad();
                inWater = true;
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

        emitHighwayRoute(plan, route);
    }
}

// --- Survey & plan -----------------------------------------------------------

TownPlan buildTownPlan() {
    std::cout << "[Towns] Surveying region for settlement sites..." << std::endl;
    TownPlan plan;
    std::mt19937 rng(worldSeed() ^ 0x70776E21u);

    const int n = GRID * GRID;
    std::vector<int16_t> hgt(n);
    std::vector<uint8_t> bio(n);
    for (int gz = 0; gz < GRID; gz++)
        for (int gx = 0; gx < GRID; gx++) {
            SurfaceSample s = sampleSurface(cellWorld(gx), cellWorld(gz));
            hgt[gz * GRID + gx] = (int16_t)s.height;
            bio[gz * GRID + gx] = (uint8_t)s.biome;
        }

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
    const long long minSq = 360LL * 360LL;
    for (const Site& s : sites) {
        int wx = cellWorld(s.gx), wz = cellWorld(s.gz);
        bool ok = true;
        for (const Town& t : plan.towns) {
            long long dx = wx - t.center.x, dz = wz - t.center.y;
            if (dx * dx + dz * dz < minSq) { ok = false; break; }
        }
        if (!ok) continue;
        Town t;
        t.center = { wx, wz };
        t.baseY  = s.baseY;
        t.type   = s.type;
        t.size   = (rng() % 5 < 2) ? TownSize::Town : TownSize::Village;
        t.radius = (t.size == TownSize::Town) ? 64 : 38;
        plan.towns.push_back(std::move(t));
    }

    // Lay out every settlement (well + houses + farms) and route its paths.
    size_t totalBuildings = 0;
    for (Town& t : plan.towns) {
        layoutTown(t);
        routeTownPaths(t);
        totalBuildings += t.buildings.size();
    }

    // Inter-town highways: curvy, terrain-following routes with shoreline docks.
    routeHighways(plan, hgt);

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

            // Clear the column above the plot (terrain bumps, trees).
            for (int wy = b.baseY; wy < CHUNK_HEIGHT; wy++)
                c->set(lx, wy, lz, BlockType::Air);

            // Stamp the building (its own Air cells carve clean space).
            for (int y = 0; y < b.dimY; y++)
                c->set(lx, b.baseY + y, lz,
                       (BlockType)b.blocks[((size_t)y * b.dimZ + z) * b.dimX + x]);

            // Foundation skirt down to the terrain surface.
            int gy = sampleSurface(wx, wz).height;
            for (int wy = gy; wy < b.baseY; wy++)
                c->set(lx, wy, lz, BlockType::Stone);
        }
}

// Lays one gravel road cell: gravel on the terrain surface (a causeway over
// water), with the column above cleared so the path stays walkable.
void stampRoadCell(Chunk* c, int lx, int lz) {
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

    int roadY;
    if (gtop >= WORLD_SEA_LEVEL) {
        roadY = gtop;                                   // land — gravel on the surface
    } else {
        roadY = WORLD_SEA_LEVEL;                        // water — a stone causeway
        for (int y = gtop + 1; y < roadY; y++) c->set(lx, y, lz, BlockType::Stone);
    }
    c->set(lx, roadY, lz, BlockType::Gravel);

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
// the chunk so only the part that actually crosses it is drawn.
void stampRoad(Chunk* c, const TownRoad& r) {
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const int hw = 1;                                   // 3-wide road
    const int xmin = ox - hw - 1, xmax = ox + CHUNK_SIZE + hw;
    const int zmin = oz - hw - 1, zmax = oz + CHUNK_SIZE + hw;

    for (size_t i = 0; i + 1 < r.pts.size(); i++) {
        int ax = r.pts[i].x,     az = r.pts[i].y;
        int dx = r.pts[i+1].x - ax, dz = r.pts[i+1].y - az;

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
        if (!ok) continue;

        int steps = std::max(std::abs(dx), std::abs(dz));
        if (steps == 0) steps = 1;
        int s0 = std::max(0,     (int)std::floor(t0 * steps));
        int s1 = std::min(steps, (int)std::ceil (t1 * steps));
        for (int s = s0; s <= s1; s++) {
            int px = ax + (int)((long long)dx * s / steps);
            int pz = az + (int)((long long)dz * s / steps);
            for (int ddx = -hw; ddx <= hw; ddx++)
                for (int ddz = -hw; ddz <= hw; ddz++) {
                    int lx = px + ddx - ox, lz = pz + ddz - oz;
                    if (lx >= 0 && lx < CHUNK_SIZE && lz >= 0 && lz < CHUNK_SIZE)
                        stampRoadCell(c, lx, lz);
                }
        }
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
    const int xmin = ox - 3, xmax = ox + CHUNK_SIZE + 3;
    const int zmin = oz - 3, zmax = oz + CHUNK_SIZE + 3;

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
        if (!ok) continue;

        int steps = std::max(std::abs(dx), std::abs(dz));
        if (steps == 0) steps = 1;
        int s0 = std::max(0,     (int)std::floor(t0 * steps));
        int s1 = std::min(steps, (int)std::ceil (t1 * steps));
        bool horiz = std::abs(dx) >= std::abs(dz);
        for (int s = s0; s <= s1; s++) {
            int px = ax + (int)((long long)dx * s / steps);
            int pz = az + (int)((long long)dz * s / steps);
            for (int w = -1; w <= 1; w++) {
                int wx = horiz ? px : px + w;
                int wz = horiz ? pz + w : pz;
                int lx = wx - ox, lz = wz - oz;
                if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;
                c->set(lx, deckY, lz, BlockType::Wood);             // deck plank
                for (int y = deckY + 1; y < CHUNK_HEIGHT; y++)      // keep it walkable
                    c->set(lx, y, lz, BlockType::Air);
            }
        }
    }

    // Support posts down to the terrain, every few centreline vertices.
    for (size_t i = 0; i < br.pts.size(); i += 3) {
        int lx = br.pts[i].x - ox, lz = br.pts[i].y - oz;
        if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;
        int gy = sampleSurface(br.pts[i].x, br.pts[i].y).height;
        for (int y = std::max(0, gy); y < deckY; y++)
            c->set(lx, y, lz, BlockType::Wood);
    }
}

} // namespace

const TownPlan& getTownPlan() {
    static TownPlan      plan;
    static std::once_flag once;
    std::call_once(once, [] { plan = buildTownPlan(); });
    return plan;
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
        for (const TownRoad& p : t.paths) stampRoad(c, p);
    }
    for (const TownRoad& h : plan.highways)
        if (ptsHit(h.pts)) stampRoad(c, h);

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

    // Buildings.
    for (const Town& t : plan.towns) {
        if (t.bbMax.x <= ox || t.bbMin.x >= ox + CHUNK_SIZE) continue;
        if (t.bbMax.y <= oz || t.bbMin.y >= oz + CHUNK_SIZE) continue;
        for (const TownBuilding& b : t.buildings)
            stampBuilding(c, b);
    }
}
