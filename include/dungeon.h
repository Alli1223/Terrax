#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "world.h"   // BlockType

class Chunk;

// The dungeon archetypes. A new type is one subclass + one line in
// makeDungeon() — the same polymorphic rule the Building/Projectile
// hierarchies follow (see CLAUDE.md). Append new values only.
enum class DungeonKind : uint8_t { Crypt = 0, Cave = 1, Ruins = 2, Castle = 3, Count };

// The carved silhouette of a chamber within its mn..mx bounding box. Non-rect
// shapes leave the surrounding rock as natural curved/angled walls.
enum class RoomShape : uint8_t { Rect = 0, Circle = 1, Octagon = 2, Cross = 3 };

// A carved chamber: an inclusive axis-aligned bounding box in world coords whose
// interior is hollowed to the `shape` silhouette, with a solid floor one block
// below mn.y. `purpose` tags it for decoration + enemy spawning:
//   0 hall/normal · 1 entrance · 2 boss · 3 throne · 4 library
//   5 ornament (central monument) · 6 vault/treasure · 7 prison/cells
struct DungeonRoom {
    glm::ivec3 mn{0};
    glm::ivec3 mx{0};
    uint8_t    purpose = 0;
    RoomShape  shape   = RoomShape::Rect;
};

// A walkable tunnel between two room centres (same level), carved as an L-shape
// (along X then Z) at the shared floor level. Width/height are set when stamped.
struct DungeonCorridor {
    glm::ivec3 a{0}, b{0};
};

// A small dynamic point light. Dungeons place NO glowing voxels — instead each
// fixture records a light here, and the renderer streams the ones near the
// player into the engine's wall-occluded point-light system. The stamp places a
// tiny non-glowing wood fixture (bracket / post) at the same spot.
//   kind 0 = wall bracket · 1 = floor post · 2 = beacon (tower top / hall centre)
struct DungeonLight {
    glm::vec3 pos{0.0f};
    uint8_t   kind = 0;
};

// One enemy to spawn, produced by fillSpawnTable() and consumed by the
// server-side director. `npcType` is an NPCType value (kept as a raw byte so
// this header doesn't depend on npc.h).
struct DungeonSpawn {
    glm::ivec3 pos{0};
    uint8_t    npcType = 1;
    bool       boss    = false;   // the dungeon's main boss (legendary loot)
};

// Abstract procedural dungeon. Geometry is a pure function of the seed, so the
// server and every client generate byte-identical dungeons with no networking
// (exactly like the town plan). Concrete subclasses pick the material palette,
// the layout flavour and the enemy roster.
class Dungeon {
public:
    virtual ~Dungeon() = default;
    virtual DungeonKind kind() const = 0;

    // Build rooms + corridors + the surface stair from `seed`, anchored at
    // `anchorXZ` with the natural surface at `surfaceY`. Fills the public state.
    virtual void generateLayout(uint32_t seed, glm::ivec2 anchorXZ, int surfaceY) = 0;

    // Material palette.
    virtual BlockType wallBlock()   const = 0;
    virtual BlockType floorBlock()  const = 0;
    virtual BlockType accentBlock() const { return BlockType::Glowstone; }

    // Enemy roster: one entry per enemy, in the rooms and around the mouth.
    virtual void fillSpawnTable(std::vector<DungeonSpawn>& out, uint32_t seed) const = 0;

    glm::ivec2 anchor{0};
    std::string name;                 // procedurally generated, deterministic per seed
    uint8_t    entranceStyle = 0;     // above-ground entrance variant (keep/circle/cave/shrine)
    int        sizeTier      = 1;     // 0 small .. 3 massive — scales footprint + room count
    bool       overground    = false; // true = an above-ground castle (built, not carved)
    int        levels        = 1;     // castle storeys (boss lives on the top one)
    int        floorH        = 6;     // castle storey height
    int        surfaceY      = 64;
    int        floorY        = 0;     // walking level of the single underground floor
    glm::ivec3 entrance{0};           // surface mouth (top of the stair)
    glm::ivec3 entranceInner{0};      // bottom of the stair, at a room edge
    std::vector<DungeonRoom>     rooms;
    std::vector<DungeonCorridor> corridors;
    std::vector<DungeonLight>    lights;   // dynamic point-light sources (no glowing voxels)
    glm::ivec2 bbMin{0}, bbMax{0};    // world-XZ bounds (inclusive) for chunk culling

protected:
    // Shared layout builder. `wander` lays a winding chain of small overlapping
    // chambers (caves); otherwise it spreads larger rooms across the footprint
    // (crypts/ruins). Connects rooms with corridors and carves a stair down
    // from the surface to the first room.
    void buildLayout(uint32_t seed, glm::ivec2 anchorXZ, int surfaceY,
                     int roomCount, int roomMin, int roomMax, bool wander);
    // Roster helper: `perRoom` minions in each room, a boss in the boss room,
    // and a champion in some chambers. Minions are drawn from `minionType` plus
    // the optional `minionType2` (255 = none) so a dungeon is populated by a
    // mix of two species instead of one.
    void rosterFill(std::vector<DungeonSpawn>& out, uint32_t seed,
                    uint8_t minionType, uint8_t bossType, int perRoom,
                    uint8_t minionType2 = 255, uint8_t minionType3 = 255,
                    uint8_t minionType4 = 255) const;
};

// Factory — the only place that maps a DungeonKind to its concrete class.
std::unique_ptr<Dungeon> makeDungeon(DungeonKind kind);

// True if world cell (x,z) lies inside a room's carved silhouette (shape-aware).
// Shared by the chunk stamp and the prop-furnishing pass.
bool dungeonRoomContains(const DungeonRoom& rm, int x, int z);

// The world's dungeon plan: surveyed once, deterministically, from the world
// seed, kept well clear of every town. Lazy + thread-safe like getTownPlan().
struct DungeonPlan {
    std::vector<std::unique_ptr<Dungeon>> dungeons;
};
const DungeonPlan& getDungeonPlan();

// Chunk-generation pass: carve any dungeon volume that falls in this chunk.
// Runs after stampTownChunk so a dungeon never cuts through a settlement
// (they are surveyed apart anyway).
void stampDungeonChunk(Chunk* c);
