#pragma once
// Internal interface shared between the town_*.cpp modules. The town generator
// was split out of one large town.cpp into focused source files:
//
//   town.cpp           survey, site selection, buildTownPlan + the cached plan
//   town_buildings.cpp centrepieces (well/market/...) and the bake helpers
//   town_layout.cpp    laying houses, farms and specialist buildings in a town
//   town_roads.cpp     intra-town paths, inter-town highways, docks, street lamps
//   town_stamp.cpp     writing town features (buildings, roads, walls) into chunks
//   town_terrain.cpp   the terrain-oracle hooks that flatten land under a town
//
// Everything they share lives in `namespace townint`. Public entry points
// (getTownPlan, stampTownChunk, townFlattenedHeight, townFlatLevelAt) are
// declared in town.h and defined at global scope in these files.

#include "town.h"
#include "world.h"
#include "building.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <random>
#include <atomic>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <cstdint>

namespace townint {

// --- Survey parameters -------------------------------------------------------
constexpr int REGION      = 30000;   // half-extent of the settled belt (blocks)
constexpr int SURVEY_STEP = 64;      // coarse survey grid spacing
constexpr int GRID        = (REGION * 2) / SURVEY_STEP;   // cells per side
constexpr int BUCKET      = 2800;    // one settlement per BUCKET-sized region
constexpr int DOCK_LEN    = 9;       // jetty length out over the water (blocks)

// Biome ids — mirror the Biome enum order in world.cpp.
constexpr int BIOME_MOUNTAINS = 3, BIOME_TUNDRA = 4;

// Gravel-path half-width range (paths widen/narrow along their length).
constexpr float PATH_HW_MIN  = 2.0f;   // narrowest -> ~4 voxels across
constexpr float PATH_HW_MAX  = 4.0f;   // widest    -> ~8 voxels across
constexpr int   PATH_HW_CEIL = 4;      // ceil(PATH_HW_MAX) — chunk-clip margin

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
extern const WallStyleDef WALL_STYLES[WALL_STYLE_COUNT];

// Set true once buildTownPlan() has finished. While it is false the terrain
// oracle skips town flattening, so the survey itself works on the natural,
// unflattened land (and there is no recursion back into the plan build).
extern std::atomic<bool> g_townReady;

// --- Shared helpers (defined in town.cpp) ------------------------------------
void        reportStage(int stage, float frac);
int         cellWorld(int g);
int         worldToCell(int w);
float       frand(std::mt19937& r, float lo, float hi);
std::string makeTownName(int wx, int wz, TownType type);
int         pickWallStyle(int houses, TownType type, uint32_t roll);
int         doorQuadrant(int fx, int fz, int tx, int tz);
bool        boxesOverlap(int ax, int az, int aw, int ad, int bx, int bz, int bw, int bd);

// --- Generic A* (header-only) ------------------------------------------------
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

// --- Centrepieces & bake helpers (town_buildings.cpp) ------------------------
void bakeBuilding(TownBuilding& b, Building& gen, int q, uint32_t seed);
void bakeHouse(TownBuilding& b, int templ, int roof, int mat, int q, uint32_t seed);
void makeWell(TownBuilding& b);
void makeMarket(TownBuilding& b);
void makeCampfire(TownBuilding& b);
void makeStatue(TownBuilding& b);
void makeFarm(TownBuilding& b, std::mt19937& rng);

// --- Layout (town_layout.cpp) ------------------------------------------------
bool tryPlaceHouse(Town& t, std::mt19937& rng, int px, int pz, int faceX, int faceZ,
                   const int* templ, int nT, const int* mats, int nM,
                   const int* roofs, int nR);
bool tryPlaceFarm(Town& t, std::mt19937& rng, int px, int pz);
bool tryPlaceSpecial(Town& t, Building& gen, int px, int pz);
void layoutRings(Town& t, std::mt19937& rng, int numH, bool scattered,
                 const int* templ, int nT, const int* mats, int nM,
                 const int* roofs, int nR);
void layoutTown(Town& t);
void placeRoadsideStructures(TownPlan& plan, const std::vector<int16_t>& hgt);

// --- Roads (town_roads.cpp) --------------------------------------------------
void routeTownPaths(Town& t);
void routeHighways(TownPlan& plan, const std::vector<int16_t>& hgt);
void placeStreetLamps(TownPlan& plan);

// --- Terrain oracle (town_terrain.cpp) ---------------------------------------
float townEffectiveFlatR(const Town& t, float dx, float dz);
int   townSlopeOffset(const Town& t, int wx, int wz);

// --- Plan (town.cpp) ---------------------------------------------------------
TownPlan buildTownPlan();

}  // namespace townint
