#include "prop_placement.h"
#include "town.h"
#include "world.h"
#include <mutex>
#include <random>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace {

std::vector<PropPlacement> g_placements;
std::once_flag            g_once;

// Height of a table's top surface above its base, in world units (the table
// model is 13 voxels tall — see buildTable). Crockery rests here.
constexpr float TABLE_TOP_H = 13.0f * PROP_SCALE;

inline int hidx(const TownBuilding& b, int x, int y, int z) {
    return ((y * b.dimZ) + z) * b.dimX + x;
}
inline bool solidAt(const TownBuilding& b, int x, int y, int z) {
    if (x < 0 || x >= b.dimX || y < 0 || y >= b.dimY || z < 0 || z >= b.dimZ)
        return false;
    return b.blocks[hidx(b, x, y, z)] != (uint8_t)BlockType::Air;
}

// Room-aware furniture placement.
//
// Each `RoomType` has its own list of weighted "wall" and "open-floor" props
// chosen by `furnitureForRoom`. The placer walks every Room emitted by the
// building generator, scans only the cells inside that room's bounds for
// valid spots, and draws from the room-specific pools — so kitchens get sinks
// and cookers, bedrooms get beds and wardrobes, studies get bookshelves, etc.
namespace {

struct FurnitureRule {
    std::vector<PropType> wallPicks;      // pieces placed against a wall
    std::vector<PropType> openPicks;      // pieces placed in the middle of the room
    int  wallLanternEvery = 5;            // 1-in-N wall spots become a lit lantern
    int  capMin = 4, capMax = 9;          // furniture pieces per room
    bool allowTableTopper = true;         // crockery/lantern on top of placed Tables
};

FurnitureRule furnitureForRoom(RoomType t) {
    FurnitureRule r;
    switch (t) {
        case RoomType::Kitchen:
            r.wallPicks  = { PropType::Cooker, PropType::Sink, PropType::KitchenCounter,
                             PropType::KitchenCounter, PropType::Crockery };
            r.openPicks  = { PropType::Table, PropType::Chair };
            r.capMin = 4; r.capMax = 7;
            r.wallLanternEvery = 6;
            break;
        case RoomType::Bedroom:
            r.wallPicks  = { PropType::Bed, PropType::Wardrobe, PropType::SideTable,
                             PropType::SideTable, PropType::Bookshelf };
            r.openPicks  = { PropType::Chair };
            r.capMin = 4; r.capMax = 6;
            r.wallLanternEvery = 5;
            r.allowTableTopper = false;
            break;
        case RoomType::Study:
            r.wallPicks  = { PropType::Bookshelf, PropType::Bookshelf, PropType::Bookshelf,
                             PropType::Desk, PropType::Desk };
            r.openPicks  = { PropType::Chair, PropType::Chair };
            r.capMin = 4; r.capMax = 7;
            r.wallLanternEvery = 4;
            break;
        case RoomType::DiningHall:
            r.wallPicks  = { PropType::Bookshelf, PropType::Cooker, PropType::BarCounter };
            r.openPicks  = { PropType::Table, PropType::Table, PropType::Table,
                             PropType::Chair, PropType::Chair, PropType::Chair };
            r.capMin = 6; r.capMax = 10;
            r.wallLanternEvery = 3;
            break;
        case RoomType::BarArea:
            r.wallPicks  = { PropType::BarCounter, PropType::BarCounter,
                             PropType::Bookshelf, PropType::Cooker };
            r.openPicks  = { PropType::BarStool, PropType::BarStool };
            r.capMin = 4; r.capMax = 8;
            r.wallLanternEvery = 3;
            r.allowTableTopper = false;
            break;
        case RoomType::Forge:
            r.wallPicks  = { PropType::Forge, PropType::Anvil, PropType::KitchenCounter };
            r.openPicks  = { PropType::Anvil };
            r.capMin = 3; r.capMax = 5;
            r.wallLanternEvery = 3;
            r.allowTableTopper = false;
            break;
        case RoomType::Workshop:
            r.wallPicks  = { PropType::Bookshelf, PropType::KitchenCounter, PropType::Desk };
            r.openPicks  = { PropType::Table, PropType::Chair };
            r.capMin = 4; r.capMax = 7;
            r.wallLanternEvery = 4;
            break;
        case RoomType::AlchemyLab:
            r.wallPicks  = { PropType::AlchemyTable, PropType::AlchemyTable,
                             PropType::Bookshelf, PropType::Bookshelf };
            r.openPicks  = { PropType::Cauldron, PropType::Table, PropType::Chair };
            r.capMin = 4; r.capMax = 7;
            r.wallLanternEvery = 4;
            r.allowTableTopper = false;
            break;
        case RoomType::Library:
            r.wallPicks  = { PropType::Bookshelf, PropType::Bookshelf, PropType::Bookshelf,
                             PropType::Bookshelf, PropType::Desk };
            r.openPicks  = { PropType::Chair, PropType::Table };
            r.capMin = 5; r.capMax = 9;
            r.wallLanternEvery = 5;
            break;
        case RoomType::Hallway:
            r.wallPicks  = { PropType::Bookshelf };
            r.openPicks  = {};
            r.capMin = 0; r.capMax = 2;
            r.wallLanternEvery = 2;
            r.allowTableTopper = false;
            break;
        case RoomType::LivingRoom:
        default:
            r.wallPicks  = { PropType::Couch, PropType::Bookshelf, PropType::Bookshelf,
                             PropType::SideTable };
            r.openPicks  = { PropType::Table, PropType::Chair, PropType::Chair };
            r.capMin = 5; r.capMax = 9;
            r.wallLanternEvery = 5;
            break;
    }
    return r;
}

// Returns true if a piece of furniture of the given PropType should be placed
// against a wall (yaw orientation derived from the adjacent wall direction).
bool prefersWall(PropType t) {
    switch (t) {
        case PropType::Bed: case PropType::Bookshelf: case PropType::Wardrobe:
        case PropType::Cooker: case PropType::Sink: case PropType::KitchenCounter:
        case PropType::SideTable: case PropType::Couch: case PropType::Desk:
        case PropType::BarCounter: case PropType::Forge: case PropType::AlchemyTable:
            return true;
        default:
            return false;
    }
}

} // namespace

// Scatters furniture across every Room of a building, picking from a room-
// specific weighted pool so kitchens get cookers, studies get bookshelves, etc.
void placeFurniture(const TownBuilding& b) {
    if (b.dimX < 5 || b.dimZ < 5 || b.dimY < 5) return;
    if (b.rooms.empty()) return;

    std::mt19937 rng(worldSeed()
                     ^ (uint32_t)(b.wx * 73856093)
                     ^ (uint32_t)(b.wz * 19349663) ^ 0xF0A1u);

    struct Spot { int x, y, z; bool wall; float yaw; };

    for (const Room& room : b.rooms) {
        FurnitureRule rule = furnitureForRoom(room.type);
        if (rule.wallPicks.empty() && rule.openPicks.empty() &&
            rule.wallLanternEvery <= 0) continue;

        std::vector<Spot> spots;
        const int x0 = std::max(0, room.x0);
        const int x1 = std::min(b.dimX - 1, room.x1);
        const int z0 = std::max(0, room.z0);
        const int z1 = std::min(b.dimZ - 1, room.z1);
        const int y  = room.floorY;
        const int yHead = std::min(b.dimY - 1, room.ceilingY);

        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++) {
                if (y - 1 < 0 || !solidAt(b, x, y - 1, z)) continue;
                if (solidAt(b, x, y, z) || solidAt(b, x, y + 1, z)) continue;
                if (yHead > y && solidAt(b, x, yHead, z) == false) {
                    // Ceiling can be partly open at stair wells — that's fine,
                    // we still place pieces on solid floor cells with headroom.
                }
                Spot s{ x, y - 1, z, true, 0.0f };
                if      (solidAt(b, x - 1, y, z)) s.yaw = 90.0f;
                else if (solidAt(b, x + 1, y, z)) s.yaw = 270.0f;
                else if (solidAt(b, x, y, z - 1)) s.yaw = 0.0f;
                else if (solidAt(b, x, y, z + 1)) s.yaw = 180.0f;
                else { s.wall = false; s.yaw = (float)((rng() % 4) * 90); }
                spots.push_back(s);
            }
        if (spots.empty()) continue;

        for (size_t i = spots.size(); i > 1; i--)
            std::swap(spots[i - 1], spots[rng() % i]);

        int cap = rule.capMin + (int)(rng() % std::max(1, rule.capMax - rule.capMin + 1));
        int placed = 0;
        std::vector<glm::ivec3> used;
        int wallSeen = 0;
        for (const Spot& s : spots) {
            if (placed >= cap) break;

            bool tooClose = false;
            for (const glm::ivec3& u : used)
                if (u.y == s.y && std::abs(u.x - s.x) < 3 && std::abs(u.z - s.z) < 3) {
                    tooClose = true; break;
                }
            if (tooClose) continue;

            PropType t;
            bool wallLantern = false;
            if (s.wall) {
                wallSeen++;
                if (rule.wallLanternEvery > 0 &&
                    (wallSeen % rule.wallLanternEvery) == 0) {
                    t = PropType::Lantern;
                    wallLantern = true;
                } else if (!rule.wallPicks.empty()) {
                    t = rule.wallPicks[rng() % rule.wallPicks.size()];
                } else continue;
            } else {
                if (rule.openPicks.empty()) continue;
                t = rule.openPicks[rng() % rule.openPicks.size()];
            }

            // Skip pieces that need a wall when placed at a floating spot, and
            // vice versa, so big wall units don't sit awkwardly in the middle.
            if (!s.wall && prefersWall(t)) continue;

            glm::vec3 pos((float)(b.wx + s.x) + 0.5f,
                          (float)(b.baseY + s.y + 1) + (wallLantern ? 3.0f : 0.0f),
                          (float)(b.wz + s.z) + 0.5f);
            g_placements.push_back({ t, pos, s.yaw, rng() });

            if (rule.allowTableTopper && t == PropType::Table) {
                glm::vec3 cp = pos; cp.y += TABLE_TOP_H;
                PropType on = (rng() % 5 == 0) ? PropType::Crockery : PropType::Lantern;
                g_placements.push_back({ on, cp, s.yaw, rng() });
            }

            used.push_back(glm::ivec3(s.x, s.y, s.z));
            placed++;
        }
    }
}

// Lines a town's gravel paths with lamps, benches, planters and greenery.
void placeDecorations(const Town& t) {
    std::mt19937 rng(worldSeed()
                     ^ (uint32_t)(t.center.x * 19349663)
                     ^ (uint32_t)(t.center.y * 83492791) ^ 0xDEC0u);

    auto insideBuilding = [&](int wx, int wz) {
        for (const TownBuilding& b : t.buildings)
            if (wx >= b.wx - 1 && wx < b.wx + b.dimX + 1 &&
                wz >= b.wz - 1 && wz < b.wz + b.dimZ + 1)
                return true;
        return false;
    };
    // Street lamps and fences are emitted by their own passes; these are the
    // roadside dressing scattered at intervals along each path.
    static const PropType DECOS[] = { PropType::Bush, PropType::PottedPlant,
                                      PropType::Bench };
    int decoIdx = 0;
    for (const TownRoad& path : t.paths) {
        for (size_t i = 0; i + 1 < path.pts.size(); i++) {
            glm::ivec2 a = path.pts[i], c = path.pts[i + 1];
            int dx = c.x - a.x, dz = c.y - a.y;
            float segLen = std::sqrt((float)(dx * dx + dz * dz));
            if (segLen < 1.0f) continue;
            float perpX = -dz / segLen, perpZ = dx / segLen;
            for (int d = 6; d < (int)segLen; d += 11) {
                float u = (float)d / segLen;
                int side = (decoIdx & 1) ? 1 : -1;
                // Offset clear of the widest a path ever varies to (see pathHalfWidth).
                int ox = a.x + (int)(dx * u + perpX * 5.0f * side);
                int oz = a.y + (int)(dz * u + perpZ * 5.0f * side);
                decoIdx++;
                if (insideBuilding(ox, oz)) continue;
                int gy = sampleSurfaceSolid(ox, oz);
                if (gy < WORLD_SEA_LEVEL) continue;            // keep them out of water
                g_placements.push_back({ DECOS[decoIdx % 3],
                    glm::vec3((float)ox + 0.5f, (float)(gy + 1), (float)oz + 0.5f),
                    (float)((rng() % 4) * 90), rng() });
            }
        }
    }
}

// World length of one fence section (the model is 36 voxels deep — see
// buildFenceSection). Sections are spaced this far apart to form a run.
constexpr float FENCE_SECTION_LEN = 36.0f * PROP_SCALE;

bool insideAnyBuilding(const TownPlan& plan, int wx, int wz) {
    for (const Town& t : plan.towns)
        for (const TownBuilding& b : t.buildings)
            if (wx >= b.wx - 1 && wx < b.wx + b.dimX + 1 &&
                wz >= b.wz - 1 && wz < b.wz + b.dimZ + 1)
                return true;
    return false;
}

bool nearAnyTown(const TownPlan& plan, float wx, float wz, float dist) {
    float d2 = dist * dist;
    for (const Town& t : plan.towns) {
        float dx = (float)t.center.x - wx, dz = (float)t.center.y - wz;
        if (dx * dx + dz * dz < d2) return true;
    }
    return false;
}

// Emits a lantern-on-a-post Prop at each baked street-light position.
//
// We prefer townFlatLevelAt() over sampleSurfaceSolid() inside a town's flat
// zone because the chunk generator hard-snaps the terrain to that level
// (baseY + a ±1 slope offset, with a small per-building flat pad). The
// sampleSurfaceSolid prediction uses the smooth-blend townFlattenedHeight
// instead, which doesn't include the slope offset, so lamps could end up
// floating one block above or buried one block in the gravel. Outside the
// flat zone (long highway segments) we still fall back to sampleSurfaceSolid.
void placeStreetLampProps(const Town& t) {
    std::mt19937 rng(worldSeed()
                     ^ (uint32_t)(t.center.x * 73856093)
                     ^ (uint32_t)(t.center.y * 19349663) ^ 0x5A1Du);
    for (const glm::ivec2& L : t.lampPosts) {
        int gy = townFlatLevelAt(L.x, L.y);
        if (gy < 1) gy = sampleSurfaceSolid(L.x, L.y);
        g_placements.push_back({ PropType::StreetLamp,
            glm::vec3((float)L.x + 0.5f, (float)(gy + 1), (float)L.y + 0.5f),
            (float)((rng() % 4) * 90), rng() });
    }
}

// Lines one side of a road polyline with a continuous run of fence sections.
// `townGated` keeps highway fencing to the stretch nearest a settlement.
void placeFenceRun(const TownPlan& plan, const std::vector<glm::ivec2>& pts,
                   std::mt19937& rng, bool townGated) {
    int   side     = (rng() & 1u) ? 1 : -1;
    float traveled = 0.0f;
    float nextAt   = FENCE_SECTION_LEN * 0.5f;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        float ax = (float)pts[i].x, az = (float)pts[i].y;
        float dx = (float)pts[i + 1].x - ax, dz = (float)pts[i + 1].y - az;
        float segLen = std::sqrt(dx * dx + dz * dz);
        if (segLen < 0.01f) continue;
        float dirX = dx / segLen, dirZ = dz / segLen;
        float perpX = -dirZ, perpZ = dirX;
        float yaw = glm::degrees(std::atan2(dirX, dirZ));
        while (nextAt <= traveled + segLen) {
            float u  = nextAt - traveled;
            // Offset clear of the widest a path ever varies to (see pathHalfWidth).
            float px = ax + dirX * u + perpX * 4.8f * (float)side;
            float pz = az + dirZ * u + perpZ * 4.8f * (float)side;
            nextAt += FENCE_SECTION_LEN;
            int gx = (int)std::floor(px), gz = (int)std::floor(pz);
            if (insideAnyBuilding(plan, gx, gz)) continue;
            if (townGated && !nearAnyTown(plan, px, pz, 80.0f)) continue;
            int gy = sampleSurfaceSolid(gx, gz);
            if (gy < WORLD_SEA_LEVEL) continue;
            g_placements.push_back({ PropType::Fence,
                glm::vec3(px, (float)(gy + 1), pz), yaw, rng() });
        }
        traveled += segLen;
    }
}

// Fences line the countryside ends of highways nearest each settlement —
// they're no longer placed along town interior paths, which cluttered the
// centres.
void placeFences(const TownPlan& plan) {
    std::mt19937 hrng(worldSeed() ^ 0x0FE0CE5Bu);
    for (const TownRoad& h : plan.highways)
        if (hrng() % 12 == 0)
            placeFenceRun(plan, h.pts, hrng, true);
}

// Picks the right trade-sign icon for a building kind. New roles (bakery,
// apothecary, …) just need a new switch case + a new SignXxx PropType.
PropType signForKind(int kind) {
    switch ((BuildingKind)kind) {
        case BuildingKind::Pub:        return PropType::SignMug;
        case BuildingKind::Blacksmith: return PropType::SignAnvil;
        case BuildingKind::MageTower:  return PropType::SignStar;
        default:                       return PropType::Count;   // no sign
    }
}

// Local-coord cell of the centre of the front-door cut, plus the world Y of
// the door's base (the floor of the ground storey).
struct DoorCellLocal { int x; int z; int baseY; bool valid; };

// Finds the door cell using the same wall-detection scan as buildDoors() —
// scan inward until we hit a row that's at least 1/3 solid at y=2 (the real
// wall plane, not a porch overhang), then airRunCentre across that row to
// pick the doorway gap. Factored out so placeTradeSign can mount the sign at
// the same wall plane.
DoorCellLocal findFrontDoorCell(const TownBuilding& b) {
    DoorCellLocal out{0, 0, 0, false};
    if ((b.doorDX == 0 && b.doorDZ == 0) || b.rooms.empty()) return out;

    auto solid = [&](int x, int y, int z) {
        if (x < 0 || x >= b.dimX || y < 0 || y >= b.dimY ||
            z < 0 || z >= b.dimZ) return false;
        return b.blocks[((size_t)y * b.dimZ + z) * b.dimX + x]
               != (uint8_t)BlockType::Air;
    };
    auto airRun = [](int n, auto air) {
        int bestS = n / 2, bestL = 0, rs = -1, rl = 0;
        for (int i = 0; i <= n; i++) {
            bool a = (i < n) && air(i);
            if (a) { if (rs < 0) rs = i; rl++; }
            else { if (rl > bestL) { bestL = rl; bestS = rs; } rs = -1; rl = 0; }
        }
        return bestL > 0 ? bestS + bestL / 2 : n / 2;
    };

    if (b.doorDZ != 0) {
        const int thr = std::max(3, b.dimX / 3);
        auto rowSolids = [&](int z) {
            int n = 0;
            for (int x = 0; x < b.dimX; x++) if (solid(x, 2, z)) n++;
            return n;
        };
        int wz;
        if (b.doorDZ < 0) {
            wz = 0;
            while (wz < b.dimZ - 1 && rowSolids(wz) < thr) wz++;
        } else {
            wz = b.dimZ - 1;
            while (wz > 0 && rowSolids(wz) < thr) wz--;
        }
        out.x = airRun(b.dimX, [&](int x){ return !solid(x, 2, wz); });
        out.z = wz;
    } else {
        const int thr = std::max(3, b.dimZ / 3);
        auto colSolids = [&](int x) {
            int n = 0;
            for (int z = 0; z < b.dimZ; z++) if (solid(x, 2, z)) n++;
            return n;
        };
        int wx;
        if (b.doorDX < 0) {
            wx = 0;
            while (wx < b.dimX - 1 && colSolids(wx) < thr) wx++;
        } else {
            wx = b.dimX - 1;
            while (wx > 0 && colSolids(wx) < thr) wx--;
        }
        out.x = wx;
        out.z = airRun(b.dimZ, [&](int z){ return !solid(wx, 2, z); });
    }
    out.baseY = b.baseY;
    out.valid = true;
    return out;
}

// Mounts a wall plaque just above the front door. The plaque model's back
// face (model Z=0) sits flush against the wall and the icon face (model Z=1)
// faces outward in the door direction. World position is set so:
//   • prop.position.y         = baseY + 5   (one block above the door cut)
//   • XZ projection            = door-cell centre + fwd * (cell half-size +
//                                  plaque half-thickness + tiny gap)
//   • yaw                      = atan2(doorDX, doorDZ)   so model +Z aligns
//                                  with the outward wall normal.
void placeTradeSign(const TownBuilding& b) {
    PropType t = signForKind(b.kind);
    if (t == PropType::Count) return;
    DoorCellLocal dc = findFrontDoorCell(b);
    if (!dc.valid) return;

    // Cell-centre of the door wall cell in world XZ.
    const float cx = (float)(b.wx + dc.x) + 0.5f;
    const float cz = (float)(b.wz + dc.z) + 0.5f;
    const float fwdX = (float)b.doorDX;
    const float fwdZ = (float)b.doorDZ;

    // From the cell centre, step half a block to reach the wall's outer face
    // (= cx + fwd * 0.5), then a tiny bit further so the plaque's centre sits
    // just outside the wall (its back face hugs the wall plane). The 2-voxel
    // depth × PROP_SCALE gives a half-thickness of 0.06; +0.04 of clearance
    // keeps Z-fighting away.
    const float pushOut = 0.5f + 0.10f;
    const float px = cx + fwdX * pushOut;
    const float pz = cz + fwdZ * pushOut;

    // Sit one block above the door cut (cut is y=1..4; wall above starts y=5).
    const float py = (float)b.baseY + 5.0f;

    // Model +Z is the visible icon face; rotate so it aligns with the door
    // direction in world space.
    float yaw = glm::degrees(std::atan2(fwdX, fwdZ));

    g_placements.push_back({ t, glm::vec3(px, py, pz), yaw, 0 });
}

void build() {
    const TownPlan& plan = getTownPlan();
    for (const Town& t : plan.towns) {
        for (const TownBuilding& b : t.buildings) {
            if (!b.rooms.empty()) placeFurniture(b);
            placeTradeSign(b);
        }
        placeStreetLampProps(t);
        placeDecorations(t);
    }
    placeFences(plan);
}

std::vector<DoorPlacement> g_doors;
std::once_flag             g_doorsOnce;

// One door per house, in the gap of its front wall.
void buildDoors() {
    const TownPlan& plan = getTownPlan();
    // Centre index of the widest run of `air` cells over [0, n).
    auto airRunCentre = [](int n, auto air) {
        int bestS = n / 2, bestL = 0, rs = -1, rl = 0;
        for (int i = 0; i <= n; i++) {
            bool a = (i < n) && air(i);
            if (a) { if (rs < 0) rs = i; rl++; }
            else { if (rl > bestL) { bestL = rl; bestS = rs; } rs = -1; rl = 0; }
        }
        return bestL > 0 ? bestS + bestL / 2 : n / 2;
    };
    for (const Town& t : plan.towns)
        for (const TownBuilding& b : t.buildings) {
            // Every residential or special building has a door direction set
            // by its generator. Centrepieces (well/market/etc) and farms leave
            // doorDX/doorDZ at zero and are skipped here.
            if ((b.doorDX == 0 && b.doorDZ == 0) || b.rooms.empty()) continue;

            auto solid = [&](int x, int y, int z) {
                if (x < 0 || x >= b.dimX || y < 0 || y >= b.dimY ||
                    z < 0 || z >= b.dimZ) return false;
                return b.blocks[((size_t)y * b.dimZ + z) * b.dimX + x]
                       != (uint8_t)BlockType::Air;
            };
            // The footprint edge can be a roof eave OR a porch (a small step +
            // 2 posts + an overhanging roof slab in front of the actual wall).
            // The porch row is mostly air at door height, so we scan inward
            // until we find a row that's at least 1/3 solid at y=2 — that's
            // the real wall plane. Then airRunCentre on that plane finds the
            // doorway cut.
            const int wallThreshold = std::max(3, b.dimX / 3);
            const int wallThresholdZ = std::max(3, b.dimZ / 3);
            int wallX, wallZ;
            if (b.doorDZ != 0) {
                auto rowSolids = [&](int z) {
                    int n = 0;
                    for (int x = 0; x < b.dimX; x++) if (solid(x, 2, z)) n++;
                    return n;
                };
                int wz;
                if (b.doorDZ < 0) {
                    wz = 0;
                    while (wz < b.dimZ - 1 && rowSolids(wz) < wallThreshold) wz++;
                } else {
                    wz = b.dimZ - 1;
                    while (wz > 0 && rowSolids(wz) < wallThreshold) wz--;
                }
                int dx = airRunCentre(b.dimX, [&](int x){ return !solid(x, 2, wz); });
                wallX = b.wx + dx; wallZ = b.wz + wz;
            } else {
                auto colSolids = [&](int x) {
                    int n = 0;
                    for (int z = 0; z < b.dimZ; z++) if (solid(x, 2, z)) n++;
                    return n;
                };
                int wx;
                if (b.doorDX < 0) {
                    wx = 0;
                    while (wx < b.dimX - 1 && colSolids(wx) < wallThresholdZ) wx++;
                } else {
                    wx = b.dimX - 1;
                    while (wx > 0 && colSolids(wx) < wallThresholdZ) wx--;
                }
                int dz = airRunCentre(b.dimZ, [&](int z){ return !solid(wx, 2, z); });
                wallX = b.wx + wx; wallZ = b.wz + dz;
            }

            float Wx = -(float)b.doorDZ, Wz = (float)b.doorDX;   // along the wall
            float Fx =  (float)b.doorDX, Fz = (float)b.doorDZ;   // outward
            float hx = (float)wallX + 0.5f - 1.5f * Wx + 0.2f * Fx;
            float hz = (float)wallZ + 0.5f - 1.5f * Wz + 0.2f * Fz;
            float cy = glm::degrees(std::atan2(-(float)b.doorDX, -(float)b.doorDZ));
            uint32_t dh = worldSeed() ^ (uint32_t)(b.wx * 374761393)
                                      ^ (uint32_t)(b.wz * 668265263);
            int variant = (int)(dh % (uint32_t)DOOR_VARIANTS);
            g_doors.push_back({ glm::vec3(hx, (float)(b.baseY + 1), hz), cy,
                                glm::ivec2(wallX, wallZ),
                                glm::ivec2(-b.doorDZ, b.doorDX), variant });
        }
}

} // namespace

const std::vector<PropPlacement>& getPropPlacements() {
    std::call_once(g_once, [] { build(); });
    return g_placements;
}

const std::vector<DoorPlacement>& getDoorPlacements() {
    std::call_once(g_doorsOnce, [] { buildDoors(); });
    return g_doors;
}
