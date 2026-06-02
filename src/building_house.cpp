// The composite-footprint house generator — L / T / U / + / courtyard / H / Z /
// E shaped houses built as a union of rectangular wings (templates 12-19) — and
// HouseBuilding::generate, which routes simple templates through emitFromSpec
// and composite ones through emitFromWings. Shared kit: building_internal.h.
#include "building.h"
#include "building_internal.h"
#include "voxel_model.h"
#include <algorithm>
#include <cstdint>
#include <random>

using namespace buildint;

namespace {

struct Wing { int x0, z0, x1, z1; };   // a footprint rectangle (inclusive), pre-margin

// Generates an L / T / U-shaped single-storey house from 2-3 rectangular wings.
// Walls follow the union's outline (a footprint cell becomes a wall where it
// borders open ground), so adjoining wings share one open interior. Each wing
// gets its own roof in the chosen style — overlaps simply stack — and one Room.
// Output is tight-cropped like emitFromSpec; the door faces -Z on the front wing.
void emitFromWings(std::vector<Wing> wings, int material, int roof, int floorH,
                   const RoomType* wingRooms, int nWings,
                   std::vector<uint8_t>& outBlocks, std::vector<Room>& outRooms,
                   int& dimX, int& dimY, int& dimZ, int& doorDX, int& doorDZ)
{
    MaterialPalette pal = materialPalette(material);
    const BlockType wallB = pal.wall, roofB = pal.roof;
    const BlockType floorB = paint(12), foundationB = BlockType::Stone, windowB = BlockType::Glass;

    std::vector<BlockType> work((size_t)HOUSE_VX * HOUSE_VY * HOUSE_VZ, BlockType::Air);
    Grid g{ work.data(), HOUSE_VX, HOUSE_VY, HOUSE_VZ };

    int bx0 = 1 << 30, bz0 = 1 << 30, bx1 = -(1 << 30), bz1 = -(1 << 30);
    for (const Wing& w : wings) {
        bx0 = std::min(bx0, w.x0); bx1 = std::max(bx1, w.x1);
        bz0 = std::min(bz0, w.z0); bz1 = std::max(bz1, w.z1);
    }
    const int offX = marginFor(bx1 - bx0 + 1, HOUSE_VX) - bx0;
    const int offZ = marginFor(bz1 - bz0 + 1, HOUSE_VZ) - bz0;
    for (Wing& w : wings) { w.x0 += offX; w.x1 += offX; w.z0 += offZ; w.z1 += offZ; }

    const int wallH = floorH;
    auto inFoot = [&](int x, int z) {
        for (const Wing& w : wings)
            if (x >= w.x0 && x <= w.x1 && z >= w.z0 && z <= w.z1) return true;
        return false;
    };
    auto perimAt = [&](int x, int z) {
        return inFoot(x, z) && (!inFoot(x - 1, z) || !inFoot(x + 1, z) ||
                                !inFoot(x, z - 1) || !inFoot(x, z + 1));
    };

    // Foundation, floor and perimeter walls following the union's outline.
    for (int x = bx0 + offX - 1; x <= bx1 + offX + 1; x++)
        for (int z = bz0 + offZ - 1; z <= bz1 + offZ + 1; z++) {
            if (inFoot(x, z) || inFoot(x - 1, z) || inFoot(x + 1, z) ||
                inFoot(x, z - 1) || inFoot(x, z + 1))
                g.set(x, 0, z, foundationB);
            if (!inFoot(x, z)) continue;
            if (perimAt(x, z)) g.box(x, x, 1, wallH, z, z, wallB);
            else               g.set(x, 0, z, floorB);
        }

    // The frontmost wing carries a 3-wide door on its -Z wall.
    const Wing* front = &wings[0];
    for (const Wing& w : wings) if (w.z0 < front->z0) front = &w;
    const int dcx = (front->x0 + front->x1) / 2;
    g.box(dcx - 1, dcx + 1, 1, 4, front->z0, front->z0, BlockType::Air);

    for (const Wing& w : wings)                    // a roof per wing
        stampRoof(g, roof, w.x0, w.x1, w.z0, w.z1, wallH, roofB, wallB);

    // Sparse 2-tall windows along the perimeter walls (never over the door).
    for (int x = bx0 + offX; x <= bx1 + offX; x++)
        for (int z = bz0 + offZ; z <= bz1 + offZ; z++) {
            if (!perimAt(x, z)) continue;
            if (((x * 3 + z * 7) % 6) != 0) continue;
            if (z == front->z0 && std::abs(x - dcx) <= 2) continue;
            g.set(x, 2, z, windowB); g.set(x, 3, z, windowB);
        }

    // One Room per wing (interior, shrunk a block off the walls).
    for (int i = 0; i < nWings; i++) {
        Room r;
        r.x0 = wings[i].x0 + 1; r.x1 = wings[i].x1 - 1;
        r.z0 = wings[i].z0 + 1; r.z1 = wings[i].z1 - 1;
        r.floorY = 1; r.ceilingY = wallH;
        r.type = wingRooms[i];
        if (r.x0 <= r.x1 && r.z0 <= r.z1 && r.type != RoomType::None)
            outRooms.push_back(r);
    }

    // Tight-crop into the output (mirrors emitFromSpec's final pass).
    int mnx = HOUSE_VX, mny = HOUSE_VY, mnz = HOUSE_VZ, mxx = -1, mxy = -1, mxz = -1;
    for (int z = 0; z < HOUSE_VZ; z++)
        for (int y = 0; y < HOUSE_VY; y++)
            for (int x = 0; x < HOUSE_VX; x++)
                if (g.get(x, y, z) != BlockType::Air) {
                    mnx = std::min(mnx, x); mxx = std::max(mxx, x);
                    mny = std::min(mny, y); mxy = std::max(mxy, y);
                    mnz = std::min(mnz, z); mxz = std::max(mxz, z);
                }
    if (mxx < 0) { dimX = dimY = dimZ = 0; outBlocks.clear(); outRooms.clear();
                   doorDX = 0; doorDZ = -1; return; }
    dimX = mxx - mnx + 1; dimY = mxy - mny + 1; dimZ = mxz - mnz + 1;
    outBlocks.assign((size_t)dimX * dimY * dimZ, (uint8_t)BlockType::Air);
    for (int y = 0; y < dimY; y++)
        for (int z = 0; z < dimZ; z++)
            for (int x = 0; x < dimX; x++)
                outBlocks[((size_t)y * dimZ + z) * dimX + x] =
                    (uint8_t)g.get(mnx + x, mny + y, mnz + z);
    for (Room& r : outRooms) {
        r.x0 -= mnx; r.x1 -= mnx; r.z0 -= mnz; r.z1 -= mnz;
        r.floorY -= mny; r.ceilingY -= mny;
        r.x0 = std::max(0, r.x0); r.x1 = std::min(dimX - 1, r.x1);
        r.z0 = std::max(0, r.z0); r.z1 = std::min(dimZ - 1, r.z1);
    }
    doorDX = 0; doorDZ = -1;
}

// Builds the wing list (and per-wing room types) for a composite house shape,
// with seed-driven dimensions so no two L / T / U / + / courtyard houses are
// alike. A small inline LCG avoids pulling in <random>.
std::vector<Wing> makeCompositeWings(int shape, uint32_t seed,
                                     RoomType* wr, int& nWings) {
    uint32_t s = seed ? seed : 1u;
    auto R = [&](int a, int b) {
        s = s * 1664525u + 1013904223u;
        return a + (int)((s >> 16) % (uint32_t)(b - a + 1));
    };
    auto coin = [&]() { s = s * 1664525u + 1013904223u; return ((s >> 20) & 1u) != 0u; };

    std::vector<Wing> w;
    switch (shape) {
    default:
    case 12: {  // L — a front bar with one back wing on a random side
        int mw = R(15, 20), md = R(7, 9), aw = R(7, 10), ad = R(8, 12);
        w.push_back({ 0, 0, mw - 1, md - 1 });
        if (coin()) w.push_back({ mw - aw, md - 1, mw - 1, md - 1 + ad });
        else        w.push_back({ 0,       md - 1, aw - 1, md - 1 + ad });
        wr[0] = RoomType::LivingRoom; wr[1] = RoomType::Bedroom; nWings = 2;
        break;
    }
    case 13: {  // T — a front bar with a central back stem
        int mw = R(16, 20), md = R(6, 8), sw = R(6, 9), sd = R(8, 12);
        int sx = (mw - sw) / 2;
        w.push_back({ 0, 0, mw - 1, md - 1 });
        w.push_back({ sx, md - 1, sx + sw - 1, md - 1 + sd });
        wr[0] = RoomType::LivingRoom; wr[1] = RoomType::Kitchen; nWings = 2;
        break;
    }
    case 14: {  // U — a front bar with two back arms (opens to the rear)
        int mw = R(16, 20), md = R(6, 8), aw = R(5, 7), ad = R(8, 12);
        w.push_back({ 0, 0, mw - 1, md - 1 });
        w.push_back({ 0, md - 1, aw - 1, md - 1 + ad });
        w.push_back({ mw - aw, md - 1, mw - 1, md - 1 + ad });
        wr[0] = RoomType::LivingRoom; wr[1] = RoomType::Kitchen; wr[2] = RoomType::Bedroom;
        nWings = 3;
        break;
    }
    case 15: {  // + — a central block with four short arms
        int cw = R(8, 11), cd = R(8, 11), aw = R(5, 6), al = R(4, 6);
        int cx = al, cz = al;
        int ax = cx + (cw - aw) / 2, az = cz + (cd - aw) / 2;
        w.push_back({ cx, cz, cx + cw - 1, cz + cd - 1 });                // centre
        w.push_back({ ax, 0,           ax + aw - 1, cz });                // front arm
        w.push_back({ ax, cz + cd - 1, ax + aw - 1, cz + cd - 1 + al });  // back arm
        w.push_back({ 0,           az, cx,          az + aw - 1 });        // left arm
        w.push_back({ cx + cw - 1, az, cx + cw - 1 + al, az + aw - 1 });  // right arm
        wr[0] = RoomType::LivingRoom; wr[1] = RoomType::Kitchen;
        wr[2] = RoomType::Bedroom;    wr[3] = RoomType::Study;
        wr[4] = RoomType::Bedroom;    nWings = 5;
        break;
    }
    case 16: {  // Courtyard — a back bar with two front arms (opens to the front)
        int bw = R(16, 20), bd = R(6, 8), aw = R(5, 7), ad = R(9, 12);
        int bz = ad - 1;
        w.push_back({ 0, bz, bw - 1, bz + bd - 1 });        // back bar
        w.push_back({ 0, 0, aw - 1, bz });                  // left front arm
        w.push_back({ bw - aw, 0, bw - 1, bz });            // right front arm
        wr[0] = RoomType::LivingRoom; wr[1] = RoomType::Kitchen; wr[2] = RoomType::Bedroom;
        nWings = 3;
        break;
    }
    case 17: {  // H — two side bars joined by a central cross-bar
        int W = R(16, 20), H = R(13, 17), sw = R(5, 7), cd = R(5, 7);
        int cz = (H - cd) / 2;
        w.push_back({ 0, 0, sw - 1, H - 1 });               // left bar
        w.push_back({ W - sw, 0, W - 1, H - 1 });           // right bar
        w.push_back({ sw - 1, cz, W - sw, cz + cd - 1 });   // connector
        wr[0] = RoomType::LivingRoom; wr[1] = RoomType::Bedroom; wr[2] = RoomType::Kitchen;
        nWings = 3;
        break;
    }
    case 18: {  // Z — two bars staggered diagonally
        int w1 = R(12, 15), d1 = R(8, 10), w2 = R(11, 14), d2 = R(8, 10);
        w.push_back({ 0, 0, w1 - 1, d1 - 1 });                              // front-left
        w.push_back({ w1 - 5, d1 - 2, w1 - 5 + w2 - 1, d1 - 2 + d2 - 1 });  // back-right
        wr[0] = RoomType::LivingRoom; wr[1] = RoomType::Bedroom; nWings = 2;
        break;
    }
    case 19: {  // E — a back spine with three front arms
        int W = R(17, 20), aw = R(4, 6), sd = R(5, 7), ad = R(8, 11);
        int sz = ad - 1;
        w.push_back({ 0, sz, W - 1, sz + sd - 1 });                  // back spine
        w.push_back({ 0, 0, aw - 1, sz });                           // left arm
        w.push_back({ (W - aw) / 2, 0, (W - aw) / 2 + aw - 1, sz });  // centre arm
        w.push_back({ W - aw, 0, W - 1, sz });                       // right arm
        wr[0] = RoomType::LivingRoom; wr[1] = RoomType::Kitchen;
        wr[2] = RoomType::Bedroom;    wr[3] = RoomType::Study;
        nWings = 4;
        break;
    }
    }
    return w;
}

}  // namespace

// --- HouseBuilding -----------------------------------------------------------

void HouseBuilding::generate(uint32_t seed,
                             std::vector<uint8_t>& outBlocks,
                             std::vector<Room>& outRooms,
                             int& dimX, int& dimY, int& dimZ,
                             int& doorDX, int& doorDZ)
{
    // Templates 12-19 are composite footprints (L / T / U / + / courtyard / H /
    // Z / E) with seed-varied proportions, built as a union of rectangular wings.
    if (templateType >= 12 && templateType <= 19) {
        RoomType wr[5] = { RoomType::LivingRoom, RoomType::None, RoomType::None,
                           RoomType::None, RoomType::None };
        int nWings = 0;
        std::vector<Wing> wings = makeCompositeWings(templateType, seed, wr, nWings);
        int roof = (roofType >= 0 && roofType <= 4) ? roofType : 1;
        emitFromWings(wings, material, roof, 6, wr, nWings,
                      outBlocks, outRooms, dimX, dimY, dimZ, doorDX, doorDZ);
        return;
    }

    HouseSpec spec = pickHouseSpec(templateType);
    if (roofType >= 0 && roofType <= 4) spec.roof = roofType;   // 4 = steep gable
    emitFromSpec(spec, material, outBlocks, outRooms, dimX, dimY, dimZ,
                 doorDX, doorDZ);
}
