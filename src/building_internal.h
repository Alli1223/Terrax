#pragma once
// Shared internals for the split building*.cpp translation units: the block-Grid
// view, the painted-block helper, the HouseSpec/FloorPlan data model, the
// primitive shell-construction routines (margins, doorways, roofs) and the
// emitFromSpec shell generator that every Building subclass builds on. The
// public surface stays in building.h; this header is private to building*.cpp.
#include "building.h"     // Building, Room, RoomType, MaterialPalette, BlockType
#include <cstdint>
#include <vector>

namespace buildint {

// Painted block colour for palette index i (BlockType::PaintFirst + i).
constexpr BlockType paint(int i) {
    return (BlockType)((int)BlockType::PaintFirst + i);
}

// Specification for one residential house template. Phase 2 expands the
// original ten templates with bigger footprints, taller storeys and per-floor
// room layouts. Roof/material are picked elsewhere; the rest is data only here.
struct FloorPlan {
    // Up to two partition cuts per floor define 2..4 rooms. `cutX` is in
    // interior coordinates (relative to interior x0). -1 means no cut.
    int  cutX = -1;
    int  cutZ = -1;
    // Room assignment, indexed (frontLeft, frontRight, backLeft, backRight).
    // Indices that don't exist (because the corresponding cut is missing) are
    // ignored. "front" is the -Z side (the side with the front door).
    RoomType rooms[4] = { RoomType::LivingRoom, RoomType::None,
                          RoomType::None,       RoomType::None };
};

struct HouseSpec {
    // Hard ceiling on storeys any generator may request. The watchtower goes up
    // to 5; the extra headroom guarantees plans[] is never indexed out of bounds
    // (writing one past the end is a stack-buffer overrun that /GS aborts on).
    // emitFromSpec clamps spec.floors to this before touching plans[].
    static constexpr int MAX_FLOORS = 8;
    int   wallSpanX = 18;       // exterior X span (walls inclusive)
    int   wallSpanZ = 14;       // exterior Z span
    int   floors    = 1;
    int   floorH    = 6;        // height of each storey in blocks
    int   roof      = 1;        // 0 flat, 1 gabled, 2 hipped, 3 pyramid, 4 steep-gable (Norse)
    bool  porch     = false;    // small overhanging porch over the front door
    bool  threeRow  = false;    // longhouse-style: two parallel cuts → three rooms
    bool  chimney   = true;     // emit a brick chimney at the back-right
    bool  doorOpen  = true;     // punch a 3-wide door opening on the front wall
    FloorPlan plans[MAX_FLOORS]; // one per storey, indices [0, MAX_FLOORS)
};

struct Grid {
    BlockType*  data;
    int gx, gy, gz;
    void set(int x, int y, int z, BlockType t) {
        if (x < 0 || x >= gx || y < 0 || y >= gy || z < 0 || z >= gz) return;
        data[((size_t)z * gy + y) * gx + x] = t;
    }
    BlockType get(int x, int y, int z) const {
        if (x < 0 || x >= gx || y < 0 || y >= gy || z < 0 || z >= gz)
            return BlockType::Air;
        return data[((size_t)z * gy + y) * gx + x];
    }
    void box(int ax, int bx, int ay, int by, int az, int bz, BlockType t) {
        for (int x = ax; x <= bx; x++)
            for (int y = ay; y <= by; y++)
                for (int z = az; z <= bz; z++)
                    set(x, y, z, t);
    }
};

// --- Shared shell-construction primitives (defined in building.cpp) ---------
int  marginFor(int span, int gridSpan);
void cutDoorwayAlongZ(Grid& g, int wallX, int zA, int zB, int yFloor, int prefZ);
void cutDoorwayAlongX(Grid& g, int wallZ, int xA, int xB, int yFloor, int prefX);
int  stampRoof(Grid& g, int roof, int x0, int x1, int z0, int z1,
               int wallH, BlockType roofB, BlockType wallB);
HouseSpec pickHouseSpec(int templateType);

// The common shell generator every Building subclass builds on: reads a
// HouseSpec (size, floors, per-floor room layout) and produces a tight-cropped,
// world-aligned block grid + room list. Doors point at -Z pre-rotation.
void emitFromSpec(const HouseSpec& specIn, int material,
                  std::vector<uint8_t>& outBlocks,
                  std::vector<Room>& outRooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ,
                  int& doorX, int& doorZ);   // local cell of the door cut (post-crop)

}  // namespace buildint
