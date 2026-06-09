#include "town.h"
#include "world.h"
#include "voxel_model.h"
#include "building.h"
#include <algorithm>
#include <chrono>
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

#include "town_internal.h"

namespace townint {

// Definitions for the two externs declared in town_internal.h.
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

// (The generic A* template lives in town_internal.h — it's shared by the road
//  modules and instantiated per cost-functor.)

// --- Survey & plan -----------------------------------------------------------

TownPlan buildTownPlan() {
    auto t0 = std::chrono::steady_clock::now();
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
        int   radius = 24 + (int)(14.0f * std::sqrt((float)targetHouses));   // a touch more spread

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
        // A central paved square, much larger for bigger towns. Houses ring it
        // from just outside, so the centre stays open and the town spreads out.
        t.plazaR       = std::min(40, 11 + targetHouses / 3);
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

    // Scatter the occasional roadside structure (watchtower / house / big farm)
    // along the highways, well clear of any town.
    placeRoadsideStructures(plan, hgt);

    // Street lights for every town's paths and highway approaches.
    reportStage(4, 0.0f);                  // "Lighting streets"
    placeStreetLamps(plan);
    reportStage(4, 1.0f);

    // One graveyard per town, on flat-ish dry ground just outside the built-up
    // area, with the gate facing back toward the town. Deterministic from the
    // town centre + world seed so client and server stamp it identically.
    {
        static const int DIRS[8][2] = {
            {1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}
        };

        // The graveyard ground must match the level the chunk generator will lay
        // down — near a town that's the *flattened* height, not the raw land.
        // townFlattenedHeight()/townFlatLevelAt() are no-ops during this very
        // build (g_townReady is still false), so mirror them from the town data
        // we already have. Without this a graveyard inside a town that raised the
        // surrounding terrain ends up in a pit well below it.
        auto townGroundY = [&](int gx, int gz) -> int {
            // Hard-flat zone → exactly the town base + its gentle tilt (matches
            // townFlatLevelAt's "outside any building" branch).
            const Town* hard = nullptr; float hardD2 = 1e30f;
            for (const Town& tt : plan.towns) {
                float dx = (float)(gx - tt.center.x), dz = (float)(gz - tt.center.y);
                float fr = townEffectiveFlatR(tt, dx, dz);
                float d2 = dx * dx + dz * dz;
                if (d2 < fr * fr && d2 < hardD2) { hardD2 = d2; hard = &tt; }
            }
            if (hard) return hard->baseY + townSlopeOffset(*hard, gx, gz);
            // Blend ring → ease the raw height toward nearby town bases.
            float h = (float)sampleSurfaceSolid(gx, gz);
            for (const Town& tt : plan.towns) {
                float dx = (float)(gx - tt.center.x), dz = (float)(gz - tt.center.y);
                float fr = townEffectiveFlatR(tt, dx, dz), br = fr + 80.0f;
                float d2 = dx * dx + dz * dz;
                if (d2 >= br * br) continue;
                float dist = std::sqrt(d2);
                float u = (dist - fr) / (br - fr);
                float w = 1.0f - u * u * (3.0f - 2.0f * u);   // smoothstep ease-out
                h += ((float)tt.baseY - h) * w;
            }
            return (int)(h + 0.5f);
        };

        for (const Town& t : plan.towns) {
            std::mt19937 grng(worldSeed() ^ 0x6BADF00Du
                              ^ (uint32_t)(t.center.x * 374761393)
                              ^ (uint32_t)(t.center.y * 668265263));
            int halfX = 6 + (int)(grng() % 3u);   // 6..8 interior half-width
            int halfZ = 8 + (int)(grng() % 4u);   // 8..11 interior half-depth
            int reach = t.radius + 12 + halfZ;     // clear of the buildings
            int start = (int)(grng() % 8u);
            bool placed = false;
            Graveyard g{};
            for (int k = 0; k < 8 && !placed; k++) {
                const int* d = DIRS[(start + k) % 8];
                int gx = t.center.x + d[0] * reach;
                int gz = t.center.y + d[1] * reach;
                int gy = townGroundY(gx, gz);
                if (gy < WORLD_SEA_LEVEL + 1) continue;            // not in water
                int c0 = townGroundY(gx - halfX, gz - halfZ);
                int c1 = townGroundY(gx + halfX, gz - halfZ);
                int c2 = townGroundY(gx - halfX, gz + halfZ);
                int c3 = townGroundY(gx + halfX, gz + halfZ);
                int mn = std::min(std::min(c0, c1), std::min(c2, c3));
                int mx = std::max(std::max(c0, c1), std::max(c2, c3));
                if (mx - mn > 6) continue;                         // too steep to flatten cleanly
                g.center = { gx, gz };
                g.baseY  = gy;
                g.halfX  = halfX;
                g.halfZ  = halfZ;
                int ddx = t.center.x - gx, ddz = t.center.y - gz;  // gate faces the town
                if (std::abs(ddx) >= std::abs(ddz)) { g.gateDX = ddx >= 0 ? 1 : -1; g.gateDZ = 0; }
                else                                { g.gateDX = 0; g.gateDZ = ddz >= 0 ? 1 : -1; }
                g.seed = grng();
                placed = true;
            }
            if (placed) plan.graveyards.push_back(g);
        }
        std::cout << "[Towns] Placed " << plan.graveyards.size() << " graveyards." << std::endl;
    }

    int nc = 0, nm = 0, ng = 0;
    for (const Town& t : plan.towns)
        (t.type == TownType::Coastal ? nc : t.type == TownType::Mountain ? nm : ng)++;
    std::cout << "[Towns] Placed " << plan.towns.size() << " settlements ("
              << nc << " coastal, " << nm << " mountain, " << ng << " grassland), "
              << totalBuildings << " buildings, "
              << plan.highways.size() << " highways, "
              << plan.bridges.size() << " bridges, "
              << plan.docks.size() << " docks, "
              << plan.roadside.size() << " roadside." << std::endl;
    double planMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
    std::cout << "[Towns] Plan built in " << (int)planMs << " ms." << std::endl;
    return plan;
}

} // namespace townint

// Bring the town internals into scope for the public entry points below.
using namespace townint;

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

// Cached settlement plan, built lazily the first time any system asks for it.
// Concurrently safe (the server's chunk-generation threads all call this) via a
// double-checked lock. The cache is *seed-versioned*: it records the world seed
// it was built from and rebuilds automatically when worldSeed() changes. That's
// what lets a remote client — which boots on its own random seed, then adopts
// the server's seed from the handshake — get the server's settlements without
// any explicit invalidation call. On the server the seed never changes, so this
// is a single build. Not std::call_once, which can't be re-run.
static TownPlan              g_townPlanCache;
static std::mutex            g_townPlanMutex;
static std::atomic<uint64_t> g_townPlanSeed{~0ull};   // ~0 = never built

const TownPlan& getTownPlan() {
    const uint64_t cur = worldSeed();
    if (g_townPlanSeed.load(std::memory_order_acquire) != cur) {
        std::lock_guard<std::mutex> lock(g_townPlanMutex);
        if (g_townPlanSeed.load(std::memory_order_relaxed) != cur) {
            g_townReady.store(false, std::memory_order_release);  // oracle is a no-op during the build
            g_townPlanCache = buildTownPlan();
            g_townReady.store(true, std::memory_order_release);
            g_townPlanSeed.store(cur, std::memory_order_release);
        }
    }
    return g_townPlanCache;
}

bool findNearestGraveyard(float wx, float wz, glm::ivec2& outCenter, int& outBaseY) {
    const TownPlan& plan = getTownPlan();
    const Graveyard* best = nullptr;
    float bestD2 = 0.0f;
    for (const Graveyard& g : plan.graveyards) {
        float dx = (float)g.center.x - wx, dz = (float)g.center.y - wz;
        float d2 = dx * dx + dz * dz;
        if (!best || d2 < bestD2) { best = &g; bestD2 = d2; }
    }
    if (!best) return false;
    outCenter = best->center;
    outBaseY  = best->baseY;
    return true;
}

