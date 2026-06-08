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
#include <condition_variable>
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

// Set true once the first plan has been published. While it is false the terrain
// oracle skips town flattening, so the survey itself works on the natural,
// unflattened land (and there is no recursion back into the plan build).
std::atomic<bool> g_townReady{false};

// Set on a survey worker thread while it samples where settlements may go, so the
// terrain oracle returns NATURAL (un-flattened) height even after an earlier
// (spawn-region) plan has been published and g_townReady is already true. Without
// it the background full-world survey would sit on the spawn region's flattened
// land and pick different sites than a clean full build would.
thread_local bool g_surveying = false;

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

// Builds the settlement plan for the rings 0..maxRing around the spawn bucket
// (maxRing >= the world's outer ring builds the whole world). A small maxRing is
// the fast "spawn region only" build; a large one is the full background build.
TownPlanBuild buildTownPlan(int maxRing) {
    auto t0 = std::chrono::steady_clock::now();
    TownPlanBuild plan;

    reportStage(0, 0.0f);                  // "Surveying terrain"
    const int n = GRID * GRID;
    std::vector<int16_t> hgt(n, 0);
    std::vector<uint8_t> bio(n, 0);
    std::vector<uint8_t> surveyed(n, 0);   // 1 = this cell has been sampled

    // Parallel surface sampling of any not-yet-sampled cells in a clamped cell
    // rectangle. The build samples the world ring by ring outward from the spawn,
    // and the `surveyed` mask means each cell is sampled exactly once across the
    // whole build — so a regional build pays only for the region it touches, which
    // is what later phases stream in around the player. Workers claim rows from a
    // shared atomic counter, so cheaper rows steal work from slower ones.
    auto surveyCellRange = [&](int cx0, int cz0, int cx1, int cz1) {
        cx0 = std::max(0, cx0); cz0 = std::max(0, cz0);
        cx1 = std::min(GRID - 1, cx1); cz1 = std::min(GRID - 1, cz1);
        if (cx1 < cx0 || cz1 < cz0) return;
        const int nWorkers = std::max(1,
                                (int)std::thread::hardware_concurrency() - 1);
        std::atomic<int> nextRow{cz0};
        std::vector<std::thread> workers;
        workers.reserve(nWorkers);
        for (int w = 0; w < nWorkers; w++) {
            workers.emplace_back([&, cx0, cx1, cz1]() {
                g_surveying = true;   // this thread samples NATURAL terrain only
                while (true) {
                    int gz = nextRow.fetch_add(1, std::memory_order_relaxed);
                    if (gz > cz1) break;
                    for (int gx = cx0; gx <= cx1; gx++) {
                        size_t idx = (size_t)gz * GRID + gx;
                        if (surveyed[idx]) continue;
                        SurfaceSample s = sampleSurface(cellWorld(gx), cellWorld(gz));
                        hgt[idx] = (int16_t)s.height;
                        bio[idx] = (uint8_t)s.biome;
                        surveyed[idx] = 1;
                    }
                }
            });
        }
        for (auto& t : workers) t.join();
    };

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

    // Per-cell flatness jitter — a deterministic function of the cell's absolute
    // grid coordinates and the world seed, NOT a shared RNG stream consumed in
    // survey order. So the winning cell in a bucket depends only on that bucket,
    // never on how many other buckets were surveyed first — the key to a regional
    // build reproducing the same settlements as a full-world build.
    auto cellJitter = [](int gx, int gz) -> float {
        uint32_t h = worldSeed() * 0x9E3779B1u
                   ^ (uint32_t)gx  * 0x85EBCA77u
                   ^ (uint32_t)gz  * 0xC2B2AE3Du;
        h ^= h >> 15; h *= 0x27D4EB2Fu; h ^= h >> 13;
        return (float)(h & 0xFFFFFFu) / (float)0x1000000u * 3.0f;   // [0,3)
    };

    // One candidate settlement per bucket: the highest-scoring (flattest, plus the
    // per-cell jitter) eligible cell in that bucket, with scale/radius/wall rolled
    // deterministically from the centre so the spacing pass can reason about it.
    struct Cand {
        int      wx, wz, baseY;
        TownType type;
        float    prio;                       // score + jitter — the spacing priority
        int      targetHouses, radius, wallStyle;
        bool     ok = false;
    };
    auto candHash = [](const Cand& c) -> uint32_t {
        uint32_t h = (uint32_t)c.wx * 0x9E3779B1u ^ (uint32_t)c.wz * 0x85EBCA77u;
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; return h;
    };
    auto beats = [&](const Cand& a, const Cand& b) {
        if (a.prio != b.prio) return a.prio > b.prio;
        return candHash(a) > candHash(b);     // deterministic tie-break
    };

    // Lazy per-bucket candidate cache. ensureCand computes (once) a bucket's
    // candidate from already-surveyed cells; callers survey the cells first.
    std::vector<Cand>    bcand((size_t)BCOUNT * BCOUNT);
    std::vector<uint8_t> bdone((size_t)BCOUNT * BCOUNT, 0);
    auto ensureCand = [&](int bx, int bz) -> const Cand* {
        if (bx < 0 || bx >= BCOUNT || bz < 0 || bz >= BCOUNT) return nullptr;
        size_t bi = (size_t)bz * BCOUNT + bx;
        if (!bdone[bi]) {
            bdone[bi] = 1;
            Site  best{}; best.ok = false;
            float bestPrio = -1.0f;
            for (int cz = 0; cz < CELLS_B; cz++)
                for (int cx = 0; cx < CELLS_B; cx++) {
                    int gx = bx * CELLS_B + cx, gz = bz * CELLS_B + cz;
                    Site s = evalCell(gx, gz);
                    if (!s.ok) continue;
                    float p = s.score + cellJitter(gx, gz);
                    if (p > bestPrio) { best = s; bestPrio = p; }
                }
            Cand& c = bcand[bi];
            if (best.ok) {
                c.ok = true;
                c.wx = cellWorld(best.gx); c.wz = cellWorld(best.gz);
                c.baseY = best.baseY; c.type = best.type; c.prio = bestPrio;
                std::mt19937 srng(worldSeed()
                                  ^ (uint32_t)(c.wx * 374761393)
                                  ^ (uint32_t)(c.wz * 668265263));
                float u = (float)(srng() & 0xFFFFFFu) / (float)0x1000000u;   // [0,1)
                c.targetHouses = 5 + (int)(95.0f * u * u * u + 0.5f);        // 5..100
                c.radius       = 24 + (int)(14.0f * std::sqrt((float)c.targetHouses));
                c.wallStyle    = pickWallStyle(c.targetHouses, c.type, srng());
            }
        }
        return bcand[bi].ok ? &bcand[bi] : nullptr;
    };

    // A bucket's candidate is kept iff no higher-priority candidate lies within
    // their combined footprint (radius-aware, 360-block floor) — checked only
    // against the 3x3 bucket neighbourhood, the only buckets whose centres can
    // fall within spacing (BUCKET=2800 >> max spacing ~500). Two kept towns can
    // never be mutually within spacing, so this is overlap-free AND independent of
    // which region built the bucket: a regional build accepts exactly the towns a
    // full build would for any bucket clear of the surveyed region's very edge.
    auto accepted = [&](int bx, int bz) -> const Cand* {
        const Cand* c = ensureCand(bx, bz);
        if (!c) return nullptr;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dz == 0) continue;
                const Cand* d = ensureCand(bx + dx, bz + dz);
                if (!d || !beats(*d, *c)) continue;
                long long ddx = c->wx - d->wx, ddz = c->wz - d->wz;
                long long minD  = (long long)(c->radius + d->radius) + 170;
                long long minSq = std::max(360LL * 360LL, minD * minD);
                if (ddx * ddx + ddz * ddz < minSq) return nullptr;
            }
        return c;
    };

    // Spawn bucket: the bucket containing the world origin. Rings expand outward
    // from here by Chebyshev bucket distance, so spawn-area settlements are laid
    // out first and a town's index is fixed the moment its ring is appended — which
    // keeps index-based references (e.g. NPC townIndex) stable as the plan grows.
    int g0 = worldToCell(0);
    const int b0x = g0 / CELLS_B, b0z = g0 / CELLS_B;
    auto morton = [](uint32_t x, uint32_t z) -> uint32_t {
        uint32_t m = 0;
        for (int i = 0; i < 11; i++) { m |= ((x >> i) & 1u) << (2 * i);
                                       m |= ((z >> i) & 1u) << (2 * i + 1); }
        return m;
    };
    int maxR = std::max(std::max(b0x, BCOUNT - 1 - b0x),
                        std::max(b0z, BCOUNT - 1 - b0z));
    int hiR  = std::min(maxR, std::max(0, maxRing));   // outermost ring this build covers

    reportStage(1, 0.0f);                  // "Selecting town sites"
    for (int R = 0; R <= hiR; R++) {
        // Survey this ring's footprint plus one bucket of apron (so edge buckets
        // see their true neighbours for the spacing test). Incremental: the mask
        // skips everything earlier rings already sampled.
        int K = R + 1;
        surveyCellRange((b0x - K) * CELLS_B - 2, (b0z - K) * CELLS_B - 2,
                        (b0x + K + 1) * CELLS_B + 1, (b0z + K + 1) * CELLS_B + 1);

        std::vector<std::pair<uint32_t, Cand>> ring;     // (morton key, candidate)
        for (int bz = std::max(0, b0z - R); bz <= std::min(BCOUNT - 1, b0z + R); bz++)
            for (int bx = std::max(0, b0x - R); bx <= std::min(BCOUNT - 1, b0x + R); bx++) {
                if (std::max(std::abs(bx - b0x), std::abs(bz - b0z)) != R) continue;
                const Cand* c = accepted(bx, bz);
                if (c) ring.push_back({ morton((uint32_t)bx, (uint32_t)bz), *c });
            }
        std::sort(ring.begin(), ring.end(),
                  [](const std::pair<uint32_t, Cand>& a,
                     const std::pair<uint32_t, Cand>& b) { return a.first < b.first; });
        for (const auto& pr : ring) {
            const Cand& c = pr.second;
            Town t;
            t.center       = { c.wx, c.wz };
            t.baseY        = c.baseY;
            t.type         = c.type;
            t.name         = makeTownName(c.wx, c.wz, c.type);
            t.targetHouses = c.targetHouses;
            t.radius       = c.radius;
            t.wallRadius   = (c.targetHouses >= 20) ? c.radius + 30 : 0;
            t.wallStyle    = c.wallStyle;
            t.size         = (c.targetHouses >= 16) ? TownSize::Town : TownSize::Village;
            t.plazaR       = std::min(40, 11 + c.targetHouses / 3);
            plan.towns.push_back(std::move(t));
        }
        reportStage(1, (float)(R + 1) / (float)(hiR + 1));
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

    int nc = 0, nm = 0, ng = 0;
    for (const Town& t : plan.towns)
        (t.type == TownType::Coastal ? nc : t.type == TownType::Mountain ? nm : ng)++;
    const char* scope = (hiR >= maxR) ? "full world  " : "spawn region";
    std::cout << "[Towns] " << scope << " (rings 0.." << hiR << "): "
              << plan.towns.size() << " settlements ("
              << nc << " coastal, " << nm << " mountain, " << ng << " grassland), "
              << totalBuildings << " buildings, "
              << plan.highways.size() << " highways, "
              << plan.bridges.size() << " bridges, "
              << plan.docks.size() << " docks, "
              << plan.roadside.size() << " roadside." << std::endl;
    double planMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
    std::cout << "[Towns] " << scope << " built in " << (int)planMs << " ms." << std::endl;
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

// Freeze a freshly-built TownPlanBuild into an immutable, shared TownPlan: each
// Town is moved behind a shared_ptr<const Town> (so a later growing snapshot can
// re-publish while sharing these towns by pointer), and the lighter vectors are
// moved across wholesale.
static std::shared_ptr<const TownPlan> publishPlan(TownPlanBuild&& b) {
    auto plan = std::make_shared<TownPlan>();
    plan->towns.reserve(b.towns.size());
    for (Town& t : b.towns)
        plan->towns.push_back(std::make_shared<const Town>(std::move(t)));
    plan->highways   = std::move(b.highways);
    plan->docks      = std::move(b.docks);
    plan->bridges    = std::move(b.bridges);
    plan->ferryLinks = std::move(b.ferryLinks);
    plan->roadside   = std::move(b.roadside);
    return plan;
}

// ---- Staged, growing publication --------------------------------------------
// The plan is published in two stages so the player can enter the world fast:
//   1. a small spawn region (rings 0..RING0) built synchronously on first access;
//   2. the rest of the world, built on a background thread, which republishes a
//      full plan and marks the whole world covered.
// Town ordering is spawn-outward, so the full plan's spawn-area towns keep the
// SAME indices as stage 1 (index-stable, e.g. for NPC townIndex references).
//
// Every published plan is kept alive forever in g_allPlans. That is cheap — towns
// are shared by shared_ptr<const Town>, so only the light container vectors are
// duplicated — and it lets a reader holding a `const TownPlan&` from getTownPlan()
// keep using it safely even after a newer plan is swapped in.
static constexpr int RING0         = 3;          // spawn region radius, in buckets
static constexpr int TOWN_RING_ALL = 1 << 28;    // "the whole world is covered"

static std::once_flag                               g_planOnce;
static std::mutex                                   g_planMutex;
static std::vector<std::shared_ptr<const TownPlan>> g_allPlans;     // kept alive forever
static std::shared_ptr<const TownPlan>              g_planPtr;
static std::atomic<const TownPlan*>                 g_planRaw{nullptr};
static std::atomic<int>                             g_planVersion{0};

static std::mutex              g_coverMutex;
static std::condition_variable g_coverCv;
static std::atomic<int>        g_coveredRing{-1};   // highest ring published; -1 = none yet

// World XZ -> Chebyshev bucket distance from the spawn bucket (the origin bucket).
static int townBucketRing(int wx, int wz) {
    const int BCOUNT  = (REGION * 2) / BUCKET;
    const int CELLS_B = GRID / BCOUNT;
    int bx = worldToCell(wx) / CELLS_B, bz = worldToCell(wz) / CELLS_B;
    int b0 = worldToCell(0) / CELLS_B;
    return std::max(std::abs(bx - b0), std::abs(bz - b0));
}

static void publishStaged(TownPlanBuild&& b, int coveredRing) {
    std::shared_ptr<const TownPlan> p = publishPlan(std::move(b));
    {
        std::lock_guard<std::mutex> lk(g_planMutex);
        g_allPlans.push_back(p);                 // never freed -> raw refs stay valid
        g_planPtr = p;
        g_planRaw.store(p.get(), std::memory_order_release);
    }
    g_planVersion.fetch_add(1, std::memory_order_acq_rel);
    // Flip readiness only after the first plan is published, so the terrain oracle
    // (which short-circuits while this is false) sees natural land during the
    // survey and never recurses back into the build.
    g_townReady.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(g_coverMutex);
        if (coveredRing > g_coveredRing.load(std::memory_order_relaxed))
            g_coveredRing.store(coveredRing, std::memory_order_release);
    }
    g_coverCv.notify_all();
}

static void ensureTownPlan() {
    std::call_once(g_planOnce, [] {
        // Stage 1 — the spawn region only: a small survey + a handful of towns, so
        // the loading screen finishes in well under a second instead of ~12 s.
        publishStaged(buildTownPlan(RING0), RING0);
        // Stage 2 — the rest of the world, on a background thread. It republishes a
        // full, prefix-stable plan and marks the whole world covered. Detached: it
        // touches only this file's state and finishes long before a normal exit.
        std::thread([] {
            TownPlanBuild full = buildTownPlan(TOWN_RING_ALL);
            publishStaged(std::move(full), TOWN_RING_ALL);
        }).detach();
    });
}

const TownPlan& getTownPlan() {
    ensureTownPlan();
    return *g_planRaw.load(std::memory_order_acquire);
}

std::shared_ptr<const TownPlan> getTownPlanSnapshot() {
    ensureTownPlan();
    std::lock_guard<std::mutex> lk(g_planMutex);
    return g_planPtr;
}

int townPlanVersion() {
    ensureTownPlan();   // so callers never observe version 0 (then rebuild needlessly)
    return g_planVersion.load(std::memory_order_acquire);
}

bool townPlanCoversWorld(int wx, int wz) {
    return townBucketRing(wx, wz) <= g_coveredRing.load(std::memory_order_acquire);
}

// Blocks the calling (chunk-worker) thread until the published plan covers the
// settlement region around (wx,wz). The spawn region is covered synchronously, so
// this only ever waits for far chunks reached before the background fill arrives.
void ensureTownCoverage(int wx, int wz) {
    ensureTownPlan();
    int need = townBucketRing(wx, wz);
    if (g_coveredRing.load(std::memory_order_acquire) >= need) return;
    std::unique_lock<std::mutex> lk(g_coverMutex);
    g_coverCv.wait(lk, [need] {
        return g_coveredRing.load(std::memory_order_acquire) >= need;
    });
}

