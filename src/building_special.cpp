// The specialist town buildings — pub, blacksmith, mage tower, stable, chapel,
// apothecary, bakery, watchtower. Each builds its shell via emitFromSpec; the
// stable and chapel then overlay their stall/pew interiors. Shared kit and the
// emitFromSpec declaration: building_internal.h.
#include "building.h"
#include "building_internal.h"
#include "voxel_model.h"
#include <algorithm>
#include <cstdint>
#include <random>

using namespace buildint;

namespace {

// Overlays a stable's interior onto the emitted grid: wood-plank stall dividers
// against the rear wall, a hay pile in each stall, a stone water trough along
// the aisle and a glowstone lantern overhead. Stalls run along the room's long
// (X) axis. Coordinates are cropped-grid space (same as outBlocks / Room).
void stampStableInterior(std::vector<uint8_t>& blk, int dimX, int dimY, int dimZ,
                         const Room& r) {
    auto set = [&](int x, int y, int z, BlockType t) {
        if (x < 0 || x >= dimX || y < 0 || y >= dimY || z < 0 || z >= dimZ) return;
        blk[((size_t)y * dimZ + z) * dimX + x] = (uint8_t)t;
    };
    const int fy   = r.floorY;
    const int midZ = (r.z0 + r.z1) / 2;
    for (int sx = r.x0; sx <= r.x1; sx += 4) {                 // stall dividers
        for (int z = midZ; z <= r.z1; z++)
            for (int y = fy; y <= fy + 2; y++) set(sx, y, z, BlockType::Wood);
        if (sx + 2 <= r.x1) {                                  // hay in the stall
            set(sx + 2, fy, r.z1,     paint(11));
            set(sx + 2, fy, r.z1 - 1, paint(11));
        }
    }
    for (int x = r.x0; x <= r.x1; x++)                         // low front rail w/ gates
        if ((x & 1) == 0) set(x, fy, midZ, BlockType::Wood);
    for (int x = r.x0 + 1; x <= r.x1 - 1; x++)                 // water trough
        set(x, fy, r.z0 + 1, BlockType::Stone);
    set((r.x0 + r.x1) / 2,     fy, r.z0 + 1, BlockType::Water);
    set((r.x0 + r.x1) / 2 + 1, fy, r.z0 + 1, BlockType::Water);
    for (int gx = r.x0 + 4; gx <= r.x1 - 1; gx += 9)            // hanging lanterns
        set(gx, fy + 3, midZ, BlockType::Glowstone);
}

// Overlays a chapel's interior: a raised stone altar with a glowstone candle at
// the rear of the nave, and two banks of wood pews flanking a central aisle
// running the long (Z) axis toward the altar.
void stampChapelInterior(std::vector<uint8_t>& blk, int dimX, int dimY, int dimZ,
                         const Room& r) {
    auto set = [&](int x, int y, int z, BlockType t) {
        if (x < 0 || x >= dimX || y < 0 || y >= dimY || z < 0 || z >= dimZ) return;
        blk[((size_t)y * dimZ + z) * dimX + x] = (uint8_t)t;
    };
    const int fy = r.floorY;
    const int cx = (r.x0 + r.x1) / 2;        // central aisle
    const int altarZ = r.z1 - 1;             // rear of the nave
    for (int x = cx - 1; x <= cx + 1; x++) {                   // altar dais + cloth
        set(x, fy,     altarZ, BlockType::Stone);
        set(x, fy + 1, altarZ, paint(0));
    }
    set(cx, fy + 2, altarZ, BlockType::Glowstone);             // candle
    for (int z = r.z0 + 2; z <= altarZ - 2; z += 2) {          // pews, central aisle clear
        for (int x = r.x0 + 1; x <= cx - 2; x++) set(x, fy, z, BlockType::Wood);
        for (int x = cx + 2; x <= r.x1 - 1; x++) set(x, fy, z, BlockType::Wood);
    }
    for (int z = r.z0 + 4; z <= altarZ - 2; z += 7) {          // wall-sconce candles
        set(r.x0, fy + 4, z, BlockType::Glowstone);
        set(r.x1, fy + 4, z, BlockType::Glowstone);
    }
}

}  // namespace

// --- PubBuilding -------------------------------------------------------------
// A tall single-storey common room with a bar counter along the back wall, a
// front dining hall and a small bedroom in the back-right for travellers.

void PubBuilding::generate(uint32_t /*seed*/,
                           std::vector<uint8_t>& outBlocks,
                           std::vector<Room>& rooms,
                           int& dimX, int& dimY, int& dimZ,
                           int& doorDX, int& doorDZ)
{
    HouseSpec spec;
    spec.wallSpanX = 22; spec.wallSpanZ = 16; spec.floors = 1;
    spec.floorH    = 8;          // tall ceiling so a hall doesn't feel cramped
    spec.roof      = roofType;
    spec.chimney   = true;
    spec.porch     = true;
    // Two cuts: split off a back-right bedroom plus an enclosed bar area.
    // splitX:    bar runs along the back behind a partition (between Z half and z1)
    // splitZ:    separates the front dining hall from the back two rooms
    spec.plans[0].cutX = 14;
    spec.plans[0].cutZ = 11;
    spec.plans[0].rooms[0] = RoomType::DiningHall;    // front-left, the open hall
    spec.plans[0].rooms[1] = RoomType::DiningHall;    // front-right, also dining
    spec.plans[0].rooms[2] = RoomType::BarArea;       // back-left
    spec.plans[0].rooms[3] = RoomType::Bedroom;       // back-right guest room
    emitFromSpec(spec, material, outBlocks, rooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
}

// --- BlacksmithBuilding ------------------------------------------------------
// Forge bay in the front-right + workshop occupying the rest. The forge bay
// has a wide cooker prop placed against the back wall (the Forge prop) and an
// anvil in the open area; the workshop hosts the smith's living quarters.

void BlacksmithBuilding::generate(uint32_t /*seed*/,
                                  std::vector<uint8_t>& outBlocks,
                                  std::vector<Room>& rooms,
                                  int& dimX, int& dimY, int& dimZ,
                                  int& doorDX, int& doorDZ)
{
    HouseSpec spec;
    spec.wallSpanX = 18; spec.wallSpanZ = 12; spec.floors = 1;
    spec.floorH    = 6;
    spec.roof      = roofType;
    spec.chimney   = true;
    spec.porch     = false;
    spec.plans[0].cutX = 9;
    spec.plans[0].cutZ = -1;
    spec.plans[0].rooms[0] = RoomType::Workshop;      // left: workshop / smith's room
    spec.plans[0].rooms[1] = RoomType::Forge;         // right: forge bay
    emitFromSpec(spec, material, outBlocks, rooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
}

// --- MageTowerBuilding -------------------------------------------------------
// A small square tower with a steep pyramid roof. Each storey is a single
// themed room: alchemy lab, library, bedroom, study (top-floor observatory).

void MageTowerBuilding::generate(uint32_t /*seed*/,
                                 std::vector<uint8_t>& outBlocks,
                                 std::vector<Room>& rooms,
                                 int& dimX, int& dimY, int& dimZ,
                                 int& doorDX, int& doorDZ)
{
    HouseSpec spec;
    // Wider than a stereotypical narrow wizard tower so the staircase fits
    // without choking each storey — a 12×12 interior is much easier to
    // navigate up than a 8×8 one and still reads as a tower thanks to the
    // pyramid roof and the storey stack.
    spec.wallSpanX = 14; spec.wallSpanZ = 14;
    spec.floors    = std::max(2, std::min(4, floors));
    spec.floorH    = 6;
    spec.roof      = 3;          // a tower deserves a pyramid roof
    spec.chimney   = false;      // mages don't burn ordinary wood
    spec.porch     = false;
    static const RoomType STACK[4] = {
        RoomType::AlchemyLab,
        RoomType::Library,
        RoomType::Bedroom,
        RoomType::Study,
    };
    for (int f = 0; f < spec.floors; f++)
        spec.plans[f].rooms[0] = STACK[std::min(3, f)];
    emitFromSpec(spec, material, outBlocks, rooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
}

// --- StableBuilding ----------------------------------------------------------
// A long, low single hall (no chimney) whose interior is filled with horse
// stalls. The shell/roof come from emitFromSpec; the stalls, hay and trough are
// overlaid afterwards onto the single Stable room.

void StableBuilding::generate(uint32_t /*seed*/,
                              std::vector<uint8_t>& outBlocks,
                              std::vector<Room>& rooms,
                              int& dimX, int& dimY, int& dimZ,
                              int& doorDX, int& doorDZ)
{
    HouseSpec spec;
    spec.wallSpanX = 22; spec.wallSpanZ = 12; spec.floors = 1;
    spec.floorH    = 7;          // tall doorway for horses
    spec.roof      = roofType;
    spec.chimney   = false;
    spec.porch     = false;
    spec.plans[0].rooms[0] = RoomType::Stable;
    emitFromSpec(spec, material, outBlocks, rooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
    for (const Room& r : rooms)
        if (r.type == RoomType::Stable)
            stampStableInterior(outBlocks, dimX, dimY, dimZ, r);
}

// --- ChapelBuilding ----------------------------------------------------------
// A tall single-nave hall with a porch. The pews and altar are overlaid onto
// the single Chapel room after the shell/roof are emitted.

void ChapelBuilding::generate(uint32_t /*seed*/,
                              std::vector<uint8_t>& outBlocks,
                              std::vector<Room>& rooms,
                              int& dimX, int& dimY, int& dimZ,
                              int& doorDX, int& doorDZ)
{
    HouseSpec spec;
    spec.wallSpanX = 12; spec.wallSpanZ = 22; spec.floors = 1;
    spec.floorH    = 10;         // a lofty nave
    spec.roof      = roofType;
    spec.chimney   = false;
    spec.porch     = true;
    spec.plans[0].rooms[0] = RoomType::Chapel;
    emitFromSpec(spec, material, outBlocks, rooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
    for (const Room& r : rooms)
        if (r.type == RoomType::Chapel)
            stampChapelInterior(outBlocks, dimX, dimY, dimZ, r);
}

// --- ApothecaryBuilding ------------------------------------------------------
// A two-room shop: a front Apothecary room (counter, shelves, cauldron — all
// from the existing prop set) and a back Bedroom for the herbalist. No block
// détail; the furniture placer dresses both rooms.

void ApothecaryBuilding::generate(uint32_t /*seed*/,
                                  std::vector<uint8_t>& outBlocks,
                                  std::vector<Room>& rooms,
                                  int& dimX, int& dimY, int& dimZ,
                                  int& doorDX, int& doorDZ)
{
    HouseSpec spec;
    spec.wallSpanX = 16; spec.wallSpanZ = 14; spec.floors = 1;
    spec.floorH    = 6;
    spec.roof      = roofType;
    spec.chimney   = true;
    spec.porch     = false;
    spec.plans[0].cutZ     = 9;                       // front shop / back room
    spec.plans[0].rooms[0] = RoomType::Apothecary;    // front (door side)
    spec.plans[0].rooms[2] = RoomType::Bedroom;       // back living quarters
    emitFromSpec(spec, material, outBlocks, rooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
}

// --- BakeryBuilding ----------------------------------------------------------
// A front shop with the oven (a Forge prop), counter and bread shelves, and a
// small back room. Keeps its chimney for the oven flue.

void BakeryBuilding::generate(uint32_t /*seed*/,
                              std::vector<uint8_t>& outBlocks,
                              std::vector<Room>& rooms,
                              int& dimX, int& dimY, int& dimZ,
                              int& doorDX, int& doorDZ)
{
    HouseSpec spec;
    spec.wallSpanX = 16; spec.wallSpanZ = 14; spec.floors = 1;
    spec.floorH    = 6;
    spec.roof      = roofType;
    spec.chimney   = true;
    spec.porch     = false;
    spec.plans[0].cutZ     = 9;                       // front shop / back room
    spec.plans[0].rooms[0] = RoomType::Bakery;        // front (door side)
    spec.plans[0].rooms[2] = RoomType::Bedroom;       // back living quarters
    emitFromSpec(spec, material, outBlocks, rooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
}

// --- WatchtowerBuilding ------------------------------------------------------
// A tall, narrow stone tower with a flat lookout top: plain lower floors and a
// study (the watch room) at the summit. A straight stair links each storey.

void WatchtowerBuilding::generate(uint32_t /*seed*/,
                                  std::vector<uint8_t>& outBlocks,
                                  std::vector<Room>& rooms,
                                  int& dimX, int& dimY, int& dimZ,
                                  int& doorDX, int& doorDZ)
{
    HouseSpec spec;
    spec.wallSpanX = 9; spec.wallSpanZ = 9;
    spec.floors    = std::max(3, std::min(5, floors));
    spec.floorH    = 5;
    spec.roof      = 0;          // flat battlement lookout
    spec.chimney   = false;
    spec.porch     = false;
    for (int f = 0; f < spec.floors; f++)
        spec.plans[f].rooms[0] = (f == spec.floors - 1) ? RoomType::Study
                                                        : RoomType::Hallway;
    emitFromSpec(spec, material, outBlocks, rooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
}
