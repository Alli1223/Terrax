#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <mutex>
#include <glm/glm.hpp>
#include "building.h"

class Chunk;

// --- Town survey progress reporting -----------------------------------------
// `buildTownPlan()` updates these atomics as it runs so a loading screen can
// show what stage the survey is at. They're safe to read from any thread.
//   `gTownBuildStage`   indexes into `kTownBuildStageNames[0..count-1]`.
//   `gTownBuildFraction` is the fraction (0..1) through the current stage.
extern std::atomic<int>   gTownBuildStage;
extern std::atomic<float> gTownBuildFraction;
extern const char* const  kTownBuildStageNames[];
extern const int          kTownBuildStageCount;

// --- Procedural towns & villages ---------------------------------------------
// A deterministic plan of settlements is surveyed once from the world seed.
// During chunk generation each chunk stamps whatever town features fall inside
// it, so towns integrate with the normal lazy chunk-streaming pipeline.

enum class TownType : unsigned char { Grassland, Coastal, Mountain };
enum class TownSize : unsigned char { Village, Town };

// What stands at a town's centre — chosen by town type and seed.
enum class TownCenter : unsigned char { Well, Market, Campfire, Statue };

// One stamped structure — a pre-rotated block grid placed at a fixed world
// position. `blocks` holds BlockType values, index ((y*dimZ)+z)*dimX+x.
// `kind` matches the BuildingKind enum (cast to int for legacy comparisons).
// `rooms` is empty for non-residential kinds and used by the furniture placer
// for kinds that have an interior (House, Pub, Blacksmith, MageTower).
struct TownBuilding {
    int wx = 0, wz = 0;          // world XZ of the grid's (0,0) corner
    int baseY = 0;               // world Y of the grid's y=0 (the plot floor)
    int dimX = 0, dimY = 0, dimZ = 0;
    int kind = 1;                // BuildingKind cast to int (Centerpiece=0, House=1, ...)
    int doorDX = 0, doorDZ = 0;   // outward facing of the front door (houses only)
    int doorX = 0, doorZ = 0;     // local grid cell of the door cut (rotated to match blocks)
    std::vector<uint8_t> blocks;
    std::vector<Room>    rooms;   // semantic interior partitions (rotated to match `blocks`)
};

// A gravel road or path — a polyline of world-XZ waypoints laid on the terrain.
struct TownRoad {
    std::vector<glm::ivec2> pts;
};

// A wooden jetty where a road meets open water; a future ferry links two docks.
struct TownDock {
    glm::ivec2 root;             // shore block the jetty starts from
    int dx = 1, dz = 0;          // cardinal direction it extends over the water
};

// A raised plank bridge carrying a highway over a gully or river. The deck is
// flat at `deckY`; the road meets it at each end where the terrain is level.
struct TownBridge {
    std::vector<glm::ivec2> pts; // world-XZ centreline of the span
    int deckY = 0;               // world Y of the deck surface
};

// A wide water crossing (> 200 blocks) where a highway is carried by a ferry
// instead of a road. The two values are the roots of the docks on each shore.
struct TownFerryLink {
    glm::ivec2 dockA, dockB;
};

struct Town {
    glm::ivec2 center;   // world XZ of the town centre
    int        baseY;    // ground height at the centre
    std::string name;    // procedurally generated, deterministic per location
    TownType   type;
    TownSize   size;     // coarse Village/Town flag (guard count, template bias)
    int        targetHouses = 0;   // desired house count (~5..100); drives radius & layout
    TownCenter centerpiece = TownCenter::Well;   // what stands at the town centre
    int        radius;   // town footprint radius in blocks
    int        plazaR = 0;                  // central paved-square radius; houses ring it from outside
    int        wallRadius = 0;              // perimeter wall ring radius; 0 = unwalled (small towns)
    int        wallStyle  = 0;              // wall-style index (palisade / stone / rampart / sandstone)
    std::vector<float> gateAngles;          // wall gate directions (radians), one per outbound highway
    glm::ivec2 bbMin, bbMax;               // world-XZ bounding box of all buildings
    std::vector<TownBuilding> buildings;   // well first, then houses & farms
    std::vector<TownRoad>     paths;       // gravel paths from each house to the well
    std::vector<glm::ivec2>   lampPosts;   // street-light positions (world XZ)
};

// Mutable construction form of the plan. Towns are held by value so the survey,
// layout and road-routing passes build and mutate them in place. Converted into
// a TownPlan (towns shared by pointer) once construction finishes — see
// publishPlan() in town.cpp.
struct TownPlanBuild {
    std::vector<Town>          towns;
    std::vector<TownRoad>      highways;
    std::vector<TownDock>      docks;
    std::vector<TownBridge>    bridges;
    std::vector<TownFerryLink> ferryLinks;
    std::vector<TownBuilding>  roadside;
};

// Published, immutable settlement plan. `towns` holds shared_ptr<const Town> so a
// growing plan (built ring-by-ring in a later phase) can publish a new snapshot
// that shares the existing towns by pointer instead of deep-copying their baked
// voxel data. The other vectors are light polylines/structs, copied by value.
struct TownPlan {
    std::vector<std::shared_ptr<const Town>> towns;
    std::vector<TownRoad>      highways;       // terrain-following roads between settlements
    std::vector<TownDock>      docks;          // jetties where highways meet the sea
    std::vector<TownBridge>    bridges;        // raised spans over gullies and rivers
    std::vector<TownFerryLink> ferryLinks;     // wide crossings served by a ferry
    std::vector<TownBuilding>  roadside;       // standalone structures scattered along highways
};

// Lazily builds (once, thread-safe) the global settlement plan. getTownPlan()
// returns the current published snapshot by reference; getTownPlanSnapshot()
// returns a shared_ptr the caller can hold across a future plan swap (the plan
// grows on a background thread in a later phase, so a held reference could
// otherwise dangle).
const TownPlan& getTownPlan();
std::shared_ptr<const TownPlan> getTownPlanSnapshot();

// Bumped each time a new (larger) plan is published — props/doors/ferries rebuild
// when it changes. The spawn-region plan is version 1; the full world is version 2.
int townPlanVersion();

// World streaming coverage. The plan is built spawn-outward in the background;
// a chunk must not be stamped until the settlements that can reach it exist.
//   townPlanCoversWorld — non-blocking test (used to defer far chunk generation).
//   ensureTownCoverage  — blocks the caller until (wx,wz)'s region is planned.
bool townPlanCoversWorld(int wx, int wz);
void ensureTownCoverage(int wx, int wz);

// Rebuilds a town-plan-derived dataset (props, doors, ferries, dungeons) whenever
// the plan grows (townPlanVersion changes). Every version is kept alive forever —
// cheap, and it means a caller holding the returned reference never dangles when a
// rebuild happens on another thread. `build(T&)` fills a fresh T from the current
// getTownPlan(). The four static slots are supplied by the caller (function-local
// statics) so each dataset gets its own. Thread-safe (double-checked under mtx).
template <class T, class BuildFn>
const T& rebuildOnPlanChange(std::atomic<int>& builtVer, std::mutex& mtx,
                             std::vector<std::shared_ptr<T>>& kept,
                             std::atomic<const T*>& cur, BuildFn build) {
    int v = townPlanVersion();
    if (builtVer.load(std::memory_order_acquire) != v) {
        std::lock_guard<std::mutex> lk(mtx);
        if (builtVer.load(std::memory_order_relaxed) != v) {
            auto p = std::make_shared<T>();
            build(*p);
            kept.push_back(p);
            cur.store(p.get(), std::memory_order_release);
            builtVer.store(v, std::memory_order_release);
        }
    }
    return *cur.load(std::memory_order_acquire);
}

// Chunk-generation pass: stamps any town features that fall inside this chunk.
void stampTownChunk(Chunk* c);

// Terrain-oracle hook: blends a raw surface height toward nearby town base
// levels so settlements sit on flat ground. Returns the raw height unchanged
// until the town plan has finished building.
float townFlattenedHeight(float wx, float wz, float rawHeight);

// Returns the baseY of any town whose strictly-flat zone covers this XZ, or
// -1 if no town claims it. The chunk generator uses this to *hard*-level the
// terrain to baseY (the density field would otherwise wobble ±a few blocks
// even with townFlattenedHeight in the bias, leaving paths and house doors
// at mismatched heights).
int townFlatLevelAt(int wx, int wz);
