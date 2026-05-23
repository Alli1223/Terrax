#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "building.h"

class Chunk;

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
    TownSize   size;
    TownCenter centerpiece = TownCenter::Well;   // what stands at the town centre
    int        radius;   // town footprint radius in blocks
    glm::ivec2 bbMin, bbMax;               // world-XZ bounding box of all buildings
    std::vector<TownBuilding> buildings;   // well first, then houses & farms
    std::vector<TownRoad>     paths;       // gravel paths from each house to the well
    std::vector<glm::ivec2>   lampPosts;   // street-light positions (world XZ)
};

struct TownPlan {
    std::vector<Town>      towns;
    std::vector<TownRoad>  highways;       // terrain-following roads between settlements
    std::vector<TownDock>  docks;          // jetties where highways meet the sea
    std::vector<TownBridge> bridges;       // raised spans over gullies and rivers
    std::vector<TownFerryLink> ferryLinks; // wide crossings served by a ferry
};

// Lazily builds (once, thread-safe) and returns the global settlement plan.
const TownPlan& getTownPlan();

// Chunk-generation pass: stamps any town features that fall inside this chunk.
void stampTownChunk(Chunk* c);

// Terrain-oracle hook: blends a raw surface height toward nearby town base
// levels so settlements sit on flat ground. Returns the raw height unchanged
// until the town plan has finished building.
float townFlattenedHeight(float wx, float wz, float rawHeight);
