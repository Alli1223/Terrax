#include "building.h"
#include "voxel_model.h"
#include <algorithm>
#include <cstdint>
#include <random>

// --- Shared helpers ----------------------------------------------------------

MaterialPalette materialPalette(int material) {
    auto paint = [](int i) { return (BlockType)((int)BlockType::PaintFirst + i); };
    MaterialPalette p;
    p.accent = paint(7);                              // brick red — chimneys, gable trim
    switch (material) {
        case 1: p.wall = paint(0);  p.roof = paint(7);  break;  // Cottage:   white / brick red
        case 2: p.wall = paint(3);  p.roof = paint(4);  break;  // Stone:     slate / charcoal
        case 3: p.wall = paint(2);  p.roof = paint(20); break;  // Manor:     light grey / navy
        case 4: p.wall = paint(12); p.roof = paint(4);  break;  // Cabin:     chestnut / charcoal
        case 5: p.wall = paint(13); p.roof = paint(6);  break;  // Sandstone: sand / terracotta
        case 6: p.wall = paint(15); p.roof = paint(16); break;  // Forest:    sage / forest green
        case 7: p.wall = paint(0);  p.roof = paint(21); break;  // Coastal:   white / steel blue
        case 8: p.wall = paint(6);  p.roof = paint(9);  break;  // Autumn:    terracotta / rust
        case 9: p.wall = paint(23); p.roof = paint(22); break;  // Plum:      dusty rose / plum
        default:p.wall = paint(13); p.roof = paint(12); break;  // Timber:    sand / chestnut
    }
    return p;
}

void rotateBuilding(int sx, int sy, int sz,
                    const std::vector<uint8_t>& src,
                    const std::vector<Room>& srcRooms,
                    int q,
                    std::vector<uint8_t>& dst,
                    std::vector<Room>& dstRooms,
                    int& dx, int& dz)
{
    q &= 3;
    dx = (q % 2 == 0) ? sx : sz;
    dz = (q % 2 == 0) ? sz : sx;
    dst.assign((size_t)dx * sy * dz, (uint8_t)BlockType::Air);

    for (int y = 0; y < sy; y++)
        for (int z = 0; z < sz; z++)
            for (int x = 0; x < sx; x++) {
                uint8_t bt = src[((size_t)y * sz + z) * sx + x];
                int rx, rz;
                switch (q) {
                    case 1:  rx = z;          rz = sx - 1 - x; break;
                    case 2:  rx = sx - 1 - x; rz = sz - 1 - z; break;
                    case 3:  rx = sz - 1 - z; rz = x;          break;
                    default: rx = x;          rz = z;          break;
                }
                dst[((size_t)y * dz + rz) * dx + rx] = bt;
            }

    dstRooms.clear();
    dstRooms.reserve(srcRooms.size());
    for (const Room& r : srcRooms) {
        Room rr = r;
        auto rot = [&](int x, int z, int& ox, int& oz) {
            switch (q) {
                case 1:  ox = z;          oz = sx - 1 - x; break;
                case 2:  ox = sx - 1 - x; oz = sz - 1 - z; break;
                case 3:  ox = sz - 1 - z; oz = x;          break;
                default: ox = x;          oz = z;          break;
            }
        };
        int ax, az, bx, bz;
        rot(r.x0, r.z0, ax, az);
        rot(r.x1, r.z1, bx, bz);
        rr.x0 = std::min(ax, bx); rr.x1 = std::max(ax, bx);
        rr.z0 = std::min(az, bz); rr.z1 = std::max(az, bz);
        dstRooms.push_back(rr);
    }
}

// --- HouseBuilding: multi-room residential generator -------------------------

namespace {

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
    int   wallSpanX = 18;       // exterior X span (walls inclusive)
    int   wallSpanZ = 14;       // exterior Z span
    int   floors    = 1;
    int   floorH    = 6;        // height of each storey in blocks
    int   roof      = 1;        // 0 flat, 1 gabled, 2 hipped, 3 pyramid, 4 steep-gable (Norse)
    bool  porch     = false;    // small overhanging porch over the front door
    bool  threeRow  = false;    // longhouse-style: two parallel cuts → three rooms
    bool  chimney   = true;     // emit a brick chimney at the back-right
    bool  doorOpen  = true;     // punch a 3-wide door opening on the front wall
    FloorPlan plans[4];         // up to 4 floors
};

HouseSpec pickHouseSpec(int templateType) {
    HouseSpec s;
    switch (templateType) {
        case 1: { // Two-Story: living + kitchen on the ground, bedroom + study upstairs
            s.wallSpanX = 18; s.wallSpanZ = 16; s.floors = 2; s.floorH = 6;
            s.roof = 1;
            s.plans[0].cutX = 10;
            s.plans[0].rooms[0] = RoomType::LivingRoom;
            s.plans[0].rooms[1] = RoomType::Kitchen;
            s.plans[1].cutZ = 8;
            s.plans[1].rooms[0] = RoomType::Bedroom;
            s.plans[1].rooms[2] = RoomType::Study;
            break;
        }
        case 2: { // Cottage: 4-room ground floor — living, kitchen, bedroom, study
            s.wallSpanX = 16; s.wallSpanZ = 16; s.floors = 1; s.floorH = 6;
            s.roof = 1;
            s.plans[0].cutX = 9; s.plans[0].cutZ = 8;
            s.plans[0].rooms[0] = RoomType::LivingRoom;
            s.plans[0].rooms[1] = RoomType::Kitchen;
            s.plans[0].rooms[2] = RoomType::Bedroom;
            s.plans[0].rooms[3] = RoomType::Study;
            break;
        }
        case 3: { // Tower: four storeys stacked: store, living, bedroom, study.
                  // Wider footprint than a typical narrow tower so the
                  // staircase doesn't eat the whole floor — gives a 12×12
                  // interior with comfortable room around the stairs.
            s.wallSpanX = 14; s.wallSpanZ = 14; s.floors = 4; s.floorH = 6;
            s.roof = 3;
            s.plans[0].rooms[0] = RoomType::LivingRoom;
            s.plans[1].rooms[0] = RoomType::Kitchen;
            s.plans[2].rooms[0] = RoomType::Bedroom;
            s.plans[3].rooms[0] = RoomType::Study;
            break;
        }
        case 4: { // Cabin: long, two rooms — living + bedroom
            s.wallSpanX = 18; s.wallSpanZ = 12; s.floors = 1; s.floorH = 6;
            s.roof = 1;
            s.plans[0].cutX = 11;
            s.plans[0].rooms[0] = RoomType::LivingRoom;
            s.plans[0].rooms[1] = RoomType::Bedroom;
            break;
        }
        case 5: { // Longhouse: three rooms in a row
            s.wallSpanX = 24; s.wallSpanZ = 12; s.floors = 1; s.floorH = 7;
            s.roof = 1; s.threeRow = true;
            s.plans[0].cutX = 9;
            s.plans[0].rooms[0] = RoomType::Kitchen;
            s.plans[0].rooms[1] = RoomType::LivingRoom;
            s.plans[0].rooms[2] = RoomType::Bedroom;
            break;
        }
        case 6: { // Townhouse: tall, three storeys, each split front/back
            s.wallSpanX = 12; s.wallSpanZ = 16; s.floors = 3; s.floorH = 6;
            s.roof = 1;
            s.plans[0].cutZ = 8;
            s.plans[0].rooms[0] = RoomType::Kitchen;
            s.plans[0].rooms[2] = RoomType::LivingRoom;
            s.plans[1].cutZ = 8;
            s.plans[1].rooms[0] = RoomType::Bedroom;
            s.plans[1].rooms[2] = RoomType::Study;
            s.plans[2].cutZ = 8;
            s.plans[2].rooms[0] = RoomType::Bedroom;
            s.plans[2].rooms[2] = RoomType::Bedroom;
            break;
        }
        case 7: { // Manor: big, two storeys, several rooms
            s.wallSpanX = 26; s.wallSpanZ = 18; s.floors = 2; s.floorH = 6;
            s.roof = 1; s.porch = true;
            s.plans[0].cutX = 14; s.plans[0].cutZ = 10;
            s.plans[0].rooms[0] = RoomType::LivingRoom;
            s.plans[0].rooms[1] = RoomType::DiningHall;
            s.plans[0].rooms[2] = RoomType::Study;
            s.plans[0].rooms[3] = RoomType::Kitchen;
            s.plans[1].cutX = 14; s.plans[1].cutZ = 10;
            s.plans[1].rooms[0] = RoomType::Bedroom;
            s.plans[1].rooms[1] = RoomType::Bedroom;
            s.plans[1].rooms[2] = RoomType::Bedroom;
            s.plans[1].rooms[3] = RoomType::Study;
            break;
        }
        case 8: { // Hall: a single great-hall living room with a tall ceiling
            s.wallSpanX = 18; s.wallSpanZ = 16; s.floors = 1; s.floorH = 11;
            s.roof = 1;
            s.plans[0].rooms[0] = RoomType::DiningHall;
            break;
        }
        case 9: { // Keep: square, three storeys, with a bigger living area
            s.wallSpanX = 16; s.wallSpanZ = 16; s.floors = 3; s.floorH = 6;
            s.roof = 2;
            s.plans[0].cutX = 9;
            s.plans[0].rooms[0] = RoomType::LivingRoom;
            s.plans[0].rooms[1] = RoomType::Kitchen;
            s.plans[1].cutZ = 8;
            s.plans[1].rooms[0] = RoomType::Bedroom;
            s.plans[1].rooms[2] = RoomType::Study;
            s.plans[2].rooms[0] = RoomType::Bedroom;
            break;
        }
        case 10: { // Norse Longhouse: narrow elongated building, tall steep
                   // wooden roof, three rooms in a row (hearth / hall / sleep)
            s.wallSpanX = 24; s.wallSpanZ = 12; s.floors = 1; s.floorH = 7;
            s.roof = 4; s.threeRow = true;
            s.chimney = false;          // a smoke louvre, not a brick chimney
            s.plans[0].cutX = 9;
            s.plans[0].rooms[0] = RoomType::Kitchen;
            s.plans[0].rooms[1] = RoomType::DiningHall;
            s.plans[0].rooms[2] = RoomType::Bedroom;
            break;
        }
        case 11: { // Norse Mead Hall: large rectangular hall, very tall steep
                   // roof, dining hall up front, small bedroom in the back
            s.wallSpanX = 22; s.wallSpanZ = 16; s.floors = 1; s.floorH = 10;
            s.roof = 4;
            s.porch  = true;
            s.plans[0].cutZ = 12;
            s.plans[0].rooms[0] = RoomType::DiningHall;
            s.plans[0].rooms[2] = RoomType::Bedroom;
            break;
        }
        default: { // Bungalow: three-room cottage with kitchen and bedroom
            s.wallSpanX = 18; s.wallSpanZ = 14; s.floors = 1; s.floorH = 6;
            s.roof = 1;
            s.plans[0].cutX = 10; s.plans[0].cutZ = 8;
            s.plans[0].rooms[0] = RoomType::LivingRoom;
            s.plans[0].rooms[1] = RoomType::Kitchen;
            s.plans[0].rooms[2] = RoomType::LivingRoom;
            s.plans[0].rooms[3] = RoomType::Bedroom;
            break;
        }
    }
    return s;
}

// Centres a span of `n` exterior cells inside HOUSE_VX so it fits. If the spec
// is bigger than the grid, callers should bump HOUSE_VX/VZ in voxel_model.h.
int marginFor(int span, int gridSpan) {
    int m = (gridSpan - span) / 2;
    return std::max(0, m);
}

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

// Cuts a 2-block-wide × 3-tall doorway in a vertical partition wall at `wallX`
// spanning z in [zA, zB], centred near `prefZ`. Air-only — caller adds a frame.
// 2-block-wide so a player carrying a torch / lantern can pass through without
// catching on the frame.
void cutDoorwayAlongZ(Grid& g, int wallX, int zA, int zB, int yFloor, int prefZ) {
    if (zB - zA < 4) return;
    int cz = std::min(zB - 2, std::max(zA + 1, prefZ));
    g.box(wallX, wallX, yFloor + 1, yFloor + 3, cz, cz + 1, BlockType::Air);
}

void cutDoorwayAlongX(Grid& g, int wallZ, int xA, int xB, int yFloor, int prefX) {
    if (xB - xA < 4) return;
    int cx = std::min(xB - 2, std::max(xA + 1, prefX));
    g.box(cx, cx + 1, yFloor + 1, yFloor + 3, wallZ, wallZ, BlockType::Air);
}

// Common shell-generation routine shared by HouseBuilding / PubBuilding /
// BlacksmithBuilding / MageTowerBuilding. Reads `spec` (size, floors, room
// layout per floor) and produces a tight-cropped, world-aligned block grid
// plus the room list. Doors point at -Z pre-rotation.
void emitFromSpec(const HouseSpec& specIn, int material,
                  std::vector<uint8_t>& outBlocks,
                  std::vector<Room>& outRooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ)
{
    HouseSpec spec = specIn;
    MaterialPalette pal = materialPalette(material);
    const BlockType foundationB = BlockType::Stone;
    const BlockType floorB      = paint(12);              // chestnut floorboards
    const BlockType windowB     = BlockType::Glass;
    const BlockType chimneyB    = pal.accent;
    const BlockType wallB       = pal.wall;
    const BlockType partB       = paint(2);               // light grey interior plaster
    const BlockType roofB       = pal.roof;

    // Allocate a working grid sized to the global house volume.
    std::vector<BlockType> work((size_t)HOUSE_VX * HOUSE_VY * HOUSE_VZ, BlockType::Air);
    Grid g{ work.data(), HOUSE_VX, HOUSE_VY, HOUSE_VZ };

    // Centre the footprint in the grid so rotation/tight-crop keeps it consistent.
    const int spanX = std::min(spec.wallSpanX, HOUSE_VX - 2);
    const int spanZ = std::min(spec.wallSpanZ, HOUSE_VZ - 2);
    const int marginX = marginFor(spanX, HOUSE_VX);
    const int marginZ = marginFor(spanZ, HOUSE_VZ);
    const int x0 = marginX, x1 = marginX + spanX - 1;
    const int z0 = marginZ, z1 = marginZ + spanZ - 1;
    const int wallH = spec.floors * spec.floorH;
    if (wallH + 2 >= HOUSE_VY) {
        // Roof would overflow; fall back to fewer floors. Should not happen
        // with the bundled specs but guards against future edits.
        spec.floors = std::max(1, (HOUSE_VY - 3) / spec.floorH);
    }

    // Foundation, shell, hollow interior.
    g.box(x0 - 1, x1 + 1, 0, 0, z0 - 1, z1 + 1, foundationB);
    g.box(x0, x1, 1, wallH, z0, z1, wallB);
    g.box(x0 + 1, x1 - 1, 1, wallH - 1, z0 + 1, z1 - 1, BlockType::Air);

    // Floors — ground floor sits directly on foundation.
    g.box(x0 + 1, x1 - 1, 0, 0, z0 + 1, z1 - 1, floorB);
    for (int f = 1; f < spec.floors; f++)
        g.box(x0 + 1, x1 - 1, f * spec.floorH, f * spec.floorH,
              z0 + 1, z1 - 1, floorB);

    // Interior partition walls and per-floor room emission.
    const int interiorX0 = x0 + 1, interiorX1 = x1 - 1;
    const int interiorZ0 = z0 + 1, interiorZ1 = z1 - 1;

    for (int f = 0; f < spec.floors; f++) {
        const FloorPlan& plan = spec.plans[f];
        const int yFloor   = f * spec.floorH;
        const int yCeiling = (f + 1) * spec.floorH - 1;
        // Partition walls now reach all the way to the slab above (or, on the
        // top floor, up to the eave). Previously they capped one block short
        // which left an unsightly gap below the ceiling.
        const int wallTop  = (f == spec.floors - 1) ? wallH
                                                    : (f + 1) * spec.floorH - 1;

        // Three-room (longhouse / hall-style) layout: cut two parallel walls
        // at cutX and (interiorW - cutX) to make a wide centre flanked by
        // smaller rooms.
        const bool threeRow = spec.threeRow;
        int splitXa = -1, splitXb = -1;
        if (threeRow && plan.cutX > 0) {
            splitXa = interiorX0 + plan.cutX - 1;
            splitXb = interiorX1 - plan.cutX + 1;
            if (splitXa >= splitXb) { splitXa = -1; splitXb = -1; }
        }

        const int splitX = (plan.cutX > 0) ? interiorX0 + plan.cutX - 1 : -1;
        const int splitZ = (plan.cutZ > 0) ? interiorZ0 + plan.cutZ - 1 : -1;

        // Emit partition walls (plaster). The wall climbs to wallTop inclusive
        // so the room is properly sealed from the room above / the roof eave.
        auto raisePartitionAlongZ = [&](int wx) {
            g.box(wx, wx, yFloor + 1, wallTop, interiorZ0, interiorZ1, partB);
        };
        auto raisePartitionAlongX = [&](int wz) {
            g.box(interiorX0, interiorX1, yFloor + 1, wallTop, wz, wz, partB);
        };

        if (threeRow && splitXa > 0 && splitXb > 0) {
            raisePartitionAlongZ(splitXa);
            raisePartitionAlongZ(splitXb);
            cutDoorwayAlongZ(g, splitXa, interiorZ0, interiorZ1, yFloor,
                             (interiorZ0 + interiorZ1) / 2);
            cutDoorwayAlongZ(g, splitXb, interiorZ0, interiorZ1, yFloor,
                             (interiorZ0 + interiorZ1) / 2);
        } else {
            if (splitX > 0) raisePartitionAlongZ(splitX);
            if (splitZ > 0) raisePartitionAlongX(splitZ);

            // Open doorways through partitions, near the centre of each segment.
            if (splitX > 0 && splitZ > 0) {
                cutDoorwayAlongZ(g, splitX, interiorZ0, splitZ - 1, yFloor,
                                 (interiorZ0 + splitZ - 1) / 2);
                cutDoorwayAlongZ(g, splitX, splitZ + 1, interiorZ1, yFloor,
                                 (splitZ + 1 + interiorZ1) / 2);
                cutDoorwayAlongX(g, splitZ, interiorX0, splitX - 1, yFloor,
                                 (interiorX0 + splitX - 1) / 2);
                cutDoorwayAlongX(g, splitZ, splitX + 1, interiorX1, yFloor,
                                 (splitX + 1 + interiorX1) / 2);
            } else if (splitX > 0) {
                cutDoorwayAlongZ(g, splitX, interiorZ0, interiorZ1, yFloor,
                                 (interiorZ0 + interiorZ1) / 2);
            } else if (splitZ > 0) {
                cutDoorwayAlongX(g, splitZ, interiorX0, interiorX1, yFloor,
                                 (interiorX0 + interiorX1) / 2);
            }
        }

        // Emit Room metadata for this floor.
        auto addRoom = [&](int rx0, int rz0, int rx1, int rz1, RoomType t) {
            if (rx0 > rx1 || rz0 > rz1 || t == RoomType::None) return;
            Room r;
            r.x0 = rx0; r.z0 = rz0; r.x1 = rx1; r.z1 = rz1;
            r.floorY = yFloor + 1;        // walkable level
            r.ceilingY = yCeiling;
            r.type = t;
            outRooms.push_back(r);
        };

        if (threeRow && splitXa > 0 && splitXb > 0) {
            addRoom(interiorX0,  interiorZ0, splitXa - 1, interiorZ1, plan.rooms[0]);
            addRoom(splitXa + 1, interiorZ0, splitXb - 1, interiorZ1, plan.rooms[1]);
            addRoom(splitXb + 1, interiorZ0, interiorX1, interiorZ1, plan.rooms[2]);
        } else if (splitX > 0 && splitZ > 0) {
            addRoom(interiorX0, interiorZ0, splitX - 1, splitZ - 1, plan.rooms[0]);
            addRoom(splitX + 1, interiorZ0, interiorX1, splitZ - 1, plan.rooms[1]);
            addRoom(interiorX0, splitZ + 1, splitX - 1, interiorZ1, plan.rooms[2]);
            addRoom(splitX + 1, splitZ + 1, interiorX1, interiorZ1, plan.rooms[3]);
        } else if (splitX > 0) {
            addRoom(interiorX0, interiorZ0, splitX - 1, interiorZ1, plan.rooms[0]);
            addRoom(splitX + 1, interiorZ0, interiorX1, interiorZ1, plan.rooms[1]);
        } else if (splitZ > 0) {
            addRoom(interiorX0, interiorZ0, interiorX1, splitZ - 1, plan.rooms[0]);
            addRoom(interiorX0, splitZ + 1, interiorX1, interiorZ1, plan.rooms[2]);
        } else {
            addRoom(interiorX0, interiorZ0, interiorX1, interiorZ1, plan.rooms[0]);
        }
    }

    // Staircases — one straight flight per upper storey, hugging a side wall.
    for (int f = 1; f < spec.floors; f++) {
        const int xStair = (f % 2 == 1) ? x0 + 1 : x0 + 3;
        const int yL     = (f - 1) * spec.floorH + 1;
        for (int s = 0; s < spec.floorH - 1; s++)
            g.box(xStair, xStair + 1, yL + s, yL + s,
                  z0 + 2 + s, z0 + 2 + s, floorB);
        g.box(xStair, xStair + 1, f * spec.floorH, f * spec.floorH,
              z0 + 2, z0 + spec.floorH, BlockType::Air);
    }

    // Front door — a 3-wide opening centred on the front (-Z) wall on the
    // ground floor. The actual door panel is a separate Door object stamped by
    // prop_placement.cpp. Buildings with `doorOpen=false` (mage tower) keep a
    // single narrower opening punched separately below.
    const int dcx = (x0 + x1) / 2;
    if (spec.doorOpen)
        g.box(dcx - 1, dcx + 1, 1, 4, z0, z0, BlockType::Air);
    else
        g.box(dcx, dcx, 1, 4, z0, z0, BlockType::Air);

    // A tiny porch — three columns of wall on the eaves above the doorway. The
    // porch lives outside the footprint so it doesn't eat into rooms.
    if (spec.porch) {
        g.box(dcx - 2, dcx + 2, 1, 1, z0 - 2, z0 - 1, foundationB);   // step
        g.box(dcx - 2, dcx - 2, 2, 4, z0 - 2, z0 - 2, wallB);
        g.box(dcx + 2, dcx + 2, 2, 4, z0 - 2, z0 - 2, wallB);
        g.box(dcx - 2, dcx + 2, 5, 5, z0 - 2, z0,     roofB);
    }

    // Windows — 2x2 glass panels punched through every exterior wall, one row
    // per floor. We skip the cell directly above the door to keep that opening
    // clear, and avoid the corner cells so the corners stay solid.
    auto win = [&](int cx, int cy, int cz, bool alongX) {
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) {
                int wx = alongX ? cx + a : cx;
                int wz = alongX ? cz     : cz + a;
                g.set(wx, cy + b, wz, windowB);
            }
    };
    const int xspan = x1 - x0, zspan = z1 - z0;
    for (int f = 0; f < spec.floors; f++) {
        const int wy = f * spec.floorH + 2;
        if (xspan >= 10) {
            for (int cx : {x0 + xspan / 4, x0 + 3 * xspan / 4 - 1}) {
                if (!(f == 0 && cx >= dcx - 2 && cx <= dcx + 2))
                    win(cx, wy, z0, true);
                win(cx, wy, z1, true);
            }
        } else {
            const int cx = (x0 + x1) / 2 - 1;
            if (f != 0) win(cx, wy, z0, true);
            win(cx, wy, z1, true);
        }
        if (zspan >= 10) {
            for (int cz : {z0 + zspan / 4, z0 + 3 * zspan / 4 - 1}) {
                win(x0, wy, cz, false);
                win(x1, wy, cz, false);
            }
        } else {
            const int cz = (z0 + z1) / 2 - 1;
            win(x0, wy, cz, false);
            win(x1, wy, cz, false);
        }
    }

    // Roof.
    const int rx0 = x0 - 1, rx1 = x1 + 1, rz0 = z0 - 1, rz1 = z1 + 1;
    const int ry = wallH + 1;
    const bool ridgeX = (xspan >= zspan);
    int roofTopY = ry;
    if (spec.roof == 0) {
        g.box(rx0, rx1, ry, ry, rz0, rz1, roofB);
    } else if (spec.roof == 3) {
        int ax0 = rx0, ax1 = rx1, az0 = rz0, az1 = rz1, h = 0;
        while (ax0 <= ax1 && az0 <= az1) {
            g.box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
            roofTopY = ry + h;
            ax0++; ax1--; az0++; az1--; h++;
        }
    } else if (spec.roof == 2) {
        int ax0 = rx0, ax1 = rx1, az0 = rz0, az1 = rz1, h = 0;
        if (ridgeX) {
            while (az0 <= az1) {
                g.box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
                roofTopY = ry + h;
                az0++; az1--;
                if (ax1 - ax0 > 4) { ax0++; ax1--; }
                h++;
            }
        } else {
            while (ax0 <= ax1) {
                g.box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
                roofTopY = ry + h;
                ax0++; ax1--;
                if (az1 - az0 > 4) { az0++; az1--; }
                h++;
            }
        }
    } else if (spec.roof == 4) {
        // Steep Gabled — Norse / stave-church pointy roof. Each "level" of the
        // gable shrinks by 1 in Z (or X) but climbs by 2 in Y, doubling the
        // pitch of the regular gabled roof. The result is a tall A-frame
        // that reads as Viking-style from any angle.
        int h = 0;
        if (ridgeX) {
            int az0 = rz0, az1 = rz1;
            while (az0 <= az1) {
                for (int ly = 0; ly < 2; ly++) {
                    g.box(rx0, rx1, ry + h + ly, ry + h + ly, az0, az1, roofB);
                    const int g0 = std::max(az0, z0), g1 = std::min(az1, z1);
                    g.box(x0, x0, ry + h + ly, ry + h + ly, g0, g1, wallB);
                    g.box(x1, x1, ry + h + ly, ry + h + ly, g0, g1, wallB);
                    roofTopY = ry + h + ly;
                }
                az0++; az1--; h += 2;
            }
        } else {
            int ax0 = rx0, ax1 = rx1;
            while (ax0 <= ax1) {
                for (int ly = 0; ly < 2; ly++) {
                    g.box(ax0, ax1, ry + h + ly, ry + h + ly, rz0, rz1, roofB);
                    const int g0 = std::max(ax0, x0), g1 = std::min(ax1, x1);
                    g.box(g0, g1, ry + h + ly, ry + h + ly, z0, z0, wallB);
                    g.box(g0, g1, ry + h + ly, ry + h + ly, z1, z1, wallB);
                    roofTopY = ry + h + ly;
                }
                ax0++; ax1--; h += 2;
            }
        }
    } else { // Gabled
        int h = 0;
        if (ridgeX) {
            int az0 = rz0, az1 = rz1;
            while (az0 <= az1) {
                g.box(rx0, rx1, ry + h, ry + h, az0, az1, roofB);
                const int g0 = std::max(az0, z0), g1 = std::min(az1, z1);
                g.box(x0, x0, ry + h, ry + h, g0, g1, wallB);
                g.box(x1, x1, ry + h, ry + h, g0, g1, wallB);
                roofTopY = ry + h;
                az0++; az1--; h++;
            }
        } else {
            int ax0 = rx0, ax1 = rx1;
            while (ax0 <= ax1) {
                g.box(ax0, ax1, ry + h, ry + h, rz0, rz1, roofB);
                const int g0 = std::max(ax0, x0), g1 = std::min(ax1, x1);
                g.box(g0, g1, ry + h, ry + h, z0, z0, wallB);
                g.box(g0, g1, ry + h, ry + h, z1, z1, wallB);
                roofTopY = ry + h;
                ax0++; ax1--; h++;
            }
        }
    }

    // Chimney — most residential buildings get a brick stack at the back.
    if (spec.chimney && xspan >= 7 && zspan >= 7) {
        const int cx = x1 - 3, cz = z1 - 3;
        g.box(cx, cx + 1, wallH, roofTopY + 2, cz, cz + 1, chimneyB);
    }

    // (Norse stave-style crossed-V finials were here but were removed — they
    // assumed a steep-gabled roof and clipped through any other roof type,
    // and even on the right roof shape they confused the silhouette more
    // than they helped.)

    // Tight-crop the work grid into the output and tighten room coords too.
    int mnx = HOUSE_VX, mny = HOUSE_VY, mnz = HOUSE_VZ;
    int mxx = -1, mxy = -1, mxz = -1;
    for (int z = 0; z < HOUSE_VZ; z++)
        for (int y = 0; y < HOUSE_VY; y++)
            for (int x = 0; x < HOUSE_VX; x++)
                if (g.get(x, y, z) != BlockType::Air) {
                    mnx = std::min(mnx, x); mxx = std::max(mxx, x);
                    mny = std::min(mny, y); mxy = std::max(mxy, y);
                    mnz = std::min(mnz, z); mxz = std::max(mxz, z);
                }
    if (mxx < 0) {
        dimX = dimY = dimZ = 0;
        outBlocks.clear();
        outRooms.clear();
        doorDX = 0; doorDZ = -1;
        return;
    }

    dimX = mxx - mnx + 1;
    dimY = mxy - mny + 1;
    dimZ = mxz - mnz + 1;
    outBlocks.assign((size_t)dimX * dimY * dimZ, (uint8_t)BlockType::Air);
    for (int y = 0; y < dimY; y++)
        for (int z = 0; z < dimZ; z++)
            for (int x = 0; x < dimX; x++)
                outBlocks[((size_t)y * dimZ + z) * dimX + x] =
                    (uint8_t)g.get(mnx + x, mny + y, mnz + z);

    for (Room& r : outRooms) {
        r.x0 -= mnx; r.x1 -= mnx;
        r.z0 -= mnz; r.z1 -= mnz;
        r.floorY   -= mny;
        r.ceilingY -= mny;
        r.x0 = std::max(0, r.x0); r.x1 = std::min(dimX - 1, r.x1);
        r.z0 = std::max(0, r.z0); r.z1 = std::min(dimZ - 1, r.z1);
    }

    doorDX = 0; doorDZ = -1;
}

} // namespace

// --- HouseBuilding -----------------------------------------------------------

void HouseBuilding::generate(uint32_t /*seed*/,
                             std::vector<uint8_t>& outBlocks,
                             std::vector<Room>& outRooms,
                             int& dimX, int& dimY, int& dimZ,
                             int& doorDX, int& doorDZ)
{
    HouseSpec spec = pickHouseSpec(templateType);
    if (roofType >= 0 && roofType <= 3) spec.roof = roofType;
    emitFromSpec(spec, material, outBlocks, outRooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
}

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
