#include "prop_placement.h"
#include "town.h"
#include "world.h"
#include "dungeon.h"     // furnish dungeon/castle rooms with the same Prop furniture
#include <mutex>
#include <atomic>
#include <random>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace {

std::vector<PropPlacement> g_placements;

// Height of a table's top surface above its base, in world units (the table
// model is 13 voxels tall — see buildTable). Crockery rests here.
constexpr float TABLE_TOP_H = 16.0f * PROP_SCALE;   // matches the enlarged table top

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
            r.openPicks  = { PropType::Table, PropType::Chair, PropType::FlowerVase };
            r.capMin = 5; r.capMax = 8;
            r.wallLanternEvery = 6;
            break;
        case RoomType::Bedroom:
            r.wallPicks  = { PropType::Bed, PropType::Wardrobe, PropType::SideTable,
                             PropType::SideTable, PropType::Bookshelf, PropType::WallPainting };
            r.openPicks  = { PropType::Chair, PropType::FlowerVase, PropType::PottedPlant };
            r.capMin = 5; r.capMax = 8;
            r.wallLanternEvery = 5;
            r.allowTableTopper = false;
            break;
        case RoomType::Study:
            r.wallPicks  = { PropType::Bookshelf, PropType::Bookshelf, PropType::Bookshelf,
                             PropType::Desk, PropType::Desk, PropType::WallPainting };
            r.openPicks  = { PropType::Chair, PropType::Chair, PropType::FlowerVase };
            r.capMin = 5; r.capMax = 8;
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
                             PropType::Bookshelf, PropType::Desk, PropType::WallPainting };
            r.openPicks  = { PropType::Chair, PropType::Table };
            r.capMin = 6; r.capMax = 10;
            r.wallLanternEvery = 5;
            break;
        case RoomType::Hallway:
            r.wallPicks  = { PropType::Bookshelf };
            r.openPicks  = {};
            r.capMin = 0; r.capMax = 2;
            r.wallLanternEvery = 2;
            r.allowTableTopper = false;
            break;
        case RoomType::Apothecary:
            r.wallPicks  = { PropType::BarCounter, PropType::Bookshelf,
                             PropType::AlchemyTable, PropType::KitchenCounter };
            r.openPicks  = { PropType::Cauldron, PropType::Table, PropType::Chair };
            r.capMin = 5; r.capMax = 8;
            r.wallLanternEvery = 3;
            break;
        case RoomType::Bakery:
            r.wallPicks  = { PropType::Forge, PropType::KitchenCounter,
                             PropType::BarCounter, PropType::Bookshelf };  // oven, counters, shelves
            r.openPicks  = { PropType::Table, PropType::Chair };
            r.capMin = 4; r.capMax = 7;
            r.wallLanternEvery = 3;
            break;
        case RoomType::Stable:
        case RoomType::Chapel:
            // Stalls / pews / altar are built as block détail in building.cpp;
            // keep the prop placer out of these rooms entirely.
            r.wallLanternEvery = 0;
            r.capMin = 0; r.capMax = 0;
            break;
        case RoomType::LivingRoom:
        default:
            r.wallPicks  = { PropType::Couch, PropType::Bookshelf, PropType::Bookshelf,
                             PropType::SideTable, PropType::WallPainting, PropType::WallPainting };
            r.openPicks  = { PropType::Table, PropType::Chair, PropType::Chair,
                             PropType::FlowerVase, PropType::PottedPlant };
            r.capMin = 7; r.capMax = 13;
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
        case PropType::Fireplace: case PropType::WallPainting:
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

        // Cosy centrepieces for living spaces: a hearth against a wall (so most
        // homes get a glowing fireplace) and a rug near the room centre. Placed
        // before the random fill so they're guaranteed. The fireplace reserves
        // its cell in `used`; the flat rug doesn't, so furniture may sit on it.
        if (room.type == RoomType::LivingRoom || room.type == RoomType::DiningHall) {
            for (const Spot& s : spots) {
                if (!s.wall) continue;
                g_placements.push_back({ PropType::Fireplace,
                    glm::vec3((float)(b.wx + s.x) + 0.5f, (float)(b.baseY + s.y + 1),
                              (float)(b.wz + s.z) + 0.5f), s.yaw, (uint32_t)rng() });
                used.push_back(glm::ivec3(s.x, s.y, s.z));
                break;
            }
            const int rcx = (x0 + x1) / 2, rcz = (z0 + z1) / 2;
            const Spot* rug = nullptr; int rugD = 1 << 30;
            for (const Spot& s : spots) {
                if (s.wall) continue;
                int d = std::abs(s.x - rcx) + std::abs(s.z - rcz);
                if (d < rugD) { rugD = d; rug = &s; }
            }
            if (rug)
                g_placements.push_back({ PropType::Rug,
                    glm::vec3((float)(b.wx + rug->x) + 0.5f, (float)(b.baseY + rug->y + 1),
                              (float)(b.wz + rug->z) + 0.5f),
                    (float)((rng() % 2) * 90), (uint32_t)rng() });
        }

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

            // Wall lanterns mount high; framed art hangs at head height; the
            // rest sit on the floor.
            float mountY = wallLantern ? 3.0f
                         : (t == PropType::WallPainting ? 2.4f : 0.0f);
            glm::vec3 pos((float)(b.wx + s.x) + 0.5f,
                          (float)(b.baseY + s.y + 1) + mountY,
                          (float)(b.wz + s.z) + 0.5f);
            g_placements.push_back({ t, pos, s.yaw, (uint32_t)rng() });

            if (rule.allowTableTopper && t == PropType::Table) {
                glm::vec3 cp = pos; cp.y += TABLE_TOP_H;
                int roll = (int)(rng() % 6);
                PropType on = (roll == 0) ? PropType::Crockery
                            : (roll == 1) ? PropType::FlowerVase
                                          : PropType::Lantern;
                g_placements.push_back({ on, cp, s.yaw, (uint32_t)rng() });
            }

            used.push_back(glm::ivec3(s.x, s.y, s.z));
            placed++;
        }
    }
}

// --- Dungeon room furnishing ------------------------------------------------
// Dungeon rooms are furnished with the SAME detailed Prop furniture as houses
// (not crude terrain blocks). Each DungeonRoom's `purpose` picks a furniture
// pool; the placer scans the carved silhouette for wall / open spots and fills
// them, exactly like placeFurniture() does for town rooms.
FurnitureRule dungeonFurnitureRule(uint8_t purpose) {
    FurnitureRule r;
    r.wallLanternEvery = 0;   // dungeon lighting is dynamic point lights — no lantern props
    switch (purpose) {
    case 4: // Library
        r.wallPicks = { PropType::Bookshelf, PropType::Bookshelf, PropType::Bookshelf,
                        PropType::Bookshelf, PropType::Desk, PropType::WallPainting };
        r.openPicks = { PropType::Chair, PropType::Table };
        r.capMin = 8; r.capMax = 14; break;
    case 6: // Vault / treasure
        r.wallPicks = { PropType::Crate, PropType::Barrel, PropType::Bookshelf };
        r.openPicks = { PropType::TreasurePile, PropType::TreasurePile, PropType::Crate,
                        PropType::Barrel, PropType::BonePile, PropType::ProducePile };
        r.capMin = 8; r.capMax = 14; r.allowTableTopper = false; break;
    case 7: // Prison / cells
        r.wallPicks = { PropType::Bed, PropType::Barrel };
        r.openPicks = { PropType::Crate, PropType::BonePile, PropType::BonePile };
        r.capMin = 3; r.capMax = 6; r.allowTableTopper = false; break;
    case 3: // Throne room
        r.wallPicks = { PropType::Couch, PropType::SideTable, PropType::WallPainting, PropType::Bookshelf };
        r.openPicks = { PropType::Brazier, PropType::FlowerVase, PropType::PottedPlant, PropType::Chair };
        r.capMin = 4; r.capMax = 8; break;
    case 2: // Boss hall
        r.wallPicks = { PropType::AlchemyTable, PropType::Bookshelf, PropType::Barrel };
        r.openPicks = { PropType::Brazier, PropType::Brazier, PropType::Cauldron,
                        PropType::TreasurePile, PropType::BonePile, PropType::Crate };
        r.capMin = 4; r.capMax = 7; break;
    case 5: // Ornament (central monument is block-built)
        r.wallPicks = { PropType::WallPainting, PropType::Bookshelf, PropType::Bench };
        r.openPicks = { PropType::PottedPlant, PropType::FlowerVase };
        r.capMin = 3; r.capMax = 6; break;
    case 1: // Entrance
        r.wallPicks = { PropType::SideTable, PropType::Bookshelf, PropType::Bench };
        r.openPicks = { PropType::Brazier, PropType::PottedPlant, PropType::Barrel };
        r.capMin = 3; r.capMax = 5; break;
    default: // Hall — a fully furnished living space (lots of small items, like a house)
        r.wallPicks = { PropType::Couch, PropType::Bookshelf, PropType::SideTable, PropType::Wardrobe,
                        PropType::Desk, PropType::Cooker, PropType::KitchenCounter,
                        PropType::WallPainting, PropType::BarCounter };
        r.openPicks = { PropType::Table, PropType::Chair, PropType::Chair, PropType::FlowerVase,
                        PropType::PottedPlant, PropType::Cauldron, PropType::Crate,
                        PropType::Barrel, PropType::BarStool, PropType::BonePile, PropType::Brazier };
        r.capMin = 9; r.capMax = 16; break;
    }
    return r;
}

void furnishDungeonRoom(const Dungeon& d, const DungeonRoom& rm) {
    const int rw = rm.mx.x - rm.mn.x, rd = rm.mx.z - rm.mn.z;
    if (rw < 4 || rd < 4) return;
    FurnitureRule rule = dungeonFurnitureRule(rm.purpose);

    std::mt19937 rng(worldSeed() ^ (uint32_t)(rm.mn.x * 73856093)
                                 ^ (uint32_t)(rm.mn.z * 19349663) ^ 0xD0F0u);
    const int fY = rm.mn.y;                                   // walkable floor (solid slab at fY-1)
    const int cx = (rm.mn.x + rm.mx.x) / 2, cz = (rm.mn.z + rm.mx.z) / 2;

    // Keep furniture clear of the block-built architecture (matches the stamp).
    auto blocked = [&](int x, int z) -> bool {
        if (d.overground) {   // the castle straight-staircase slot along the -Z wall
            int lx0 = d.bbMin.x + 4, lx1 = d.bbMin.x + 4 + (d.levels - 1) * d.floorH + 1;
            int lz0 = d.bbMin.y + 2, lz1 = d.bbMin.y + 4;
            if (x >= lx0 && x <= lx1 && z >= lz0 && z <= lz1) return true;
        }
        if (rm.purpose == 5 && std::abs(x - cx) <= 3 && std::abs(z - cz) <= 3) return true; // monument
        if ((rm.purpose == 2 || rm.purpose == 3) && x <= rm.mn.x + 3) return true;          // throne
        if (rm.purpose == 7 && x <= rm.mn.x + 3) return true;                               // cages
        return false;
    };

    struct Spot { int x, z; bool wall; float yaw; };
    std::vector<Spot> spots;
    for (int x = rm.mn.x + 1; x <= rm.mx.x - 1; x++)
        for (int z = rm.mn.z + 1; z <= rm.mx.z - 1; z++) {
            if (!dungeonRoomContains(rm, x, z) || blocked(x, z)) continue;
            Spot s{ x, z, false, (float)((rng() % 4) * 90) };
            if      (!dungeonRoomContains(rm, x - 1, z)) { s.wall = true; s.yaw = 90.0f; }
            else if (!dungeonRoomContains(rm, x + 1, z)) { s.wall = true; s.yaw = 270.0f; }
            else if (!dungeonRoomContains(rm, x, z - 1)) { s.wall = true; s.yaw = 0.0f; }
            else if (!dungeonRoomContains(rm, x, z + 1)) { s.wall = true; s.yaw = 180.0f; }
            spots.push_back(s);
        }
    if (spots.empty()) return;
    for (size_t i = spots.size(); i > 1; i--) std::swap(spots[i - 1], spots[rng() % i]);

    // A rug near the centre of most rooms (flat — furniture can sit on it).
    if (rule.capMax >= 4 && !blocked(cx, cz) && dungeonRoomContains(rm, cx, cz))
        g_placements.push_back({ PropType::Rug, glm::vec3((float)cx + 0.5f, (float)fY, (float)cz + 0.5f),
                                 (float)((rng() % 2) * 90), (uint32_t)rng() });

    int cap = rule.capMin + (int)(rng() % std::max(1, rule.capMax - rule.capMin + 1));
    int placed = 0;
    std::vector<glm::ivec3> used;
    for (const Spot& s : spots) {
        if (placed >= cap) break;
        bool tooClose = false;
        for (const glm::ivec3& u : used)
            if (std::abs(u.x - s.x) < 3 && std::abs(u.z - s.z) < 3) { tooClose = true; break; }
        if (tooClose) continue;

        PropType t;
        if (s.wall) { if (rule.wallPicks.empty()) continue; t = rule.wallPicks[rng() % rule.wallPicks.size()]; }
        else        { if (rule.openPicks.empty()) continue; t = rule.openPicks[rng() % rule.openPicks.size()]; }
        if (!s.wall && prefersWall(t)) continue;

        float mountY = (t == PropType::WallPainting) ? 2.4f : 0.0f;
        glm::vec3 pos((float)s.x + 0.5f, (float)fY + mountY, (float)s.z + 0.5f);
        g_placements.push_back({ t, pos, s.yaw, (uint32_t)rng() });
        if (rule.allowTableTopper && t == PropType::Table) {
            glm::vec3 cp = pos; cp.y += TABLE_TOP_H;
            g_placements.push_back({ (rng() % 2) ? PropType::Crockery : PropType::FlowerVase,
                                     cp, s.yaw, (uint32_t)rng() });
        }
        used.push_back(glm::ivec3(s.x, fY, s.z));
        placed++;
    }
}

// Ground rest-height for a prop. Inside a town's hard-flat zone we use the
// leveled baseY so the prop sits exactly on the flattened/paved surface; outside
// it (e.g. a fence run along an open highway) we fall back to the raw terrain
// surface. Resting on the raw surface inside a town floats props by the
// raw-vs-flattened wobble (the same reason the lamp/plaza passes prefer it).
int townGroundLevel(int wx, int wz) {
    int gy = townFlatLevelAt(wx, wz);
    return (gy < 1) ? sampleSurfaceSolid(wx, wz) : gy;
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
    static const PropType DECOS[] = { PropType::Bush, PropType::FlowerBed,
                                      PropType::PottedPlant, PropType::Bench };
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
                int gy = townGroundLevel(ox, oz);
                if (gy < WORLD_SEA_LEVEL) continue;            // keep them out of water
                g_placements.push_back({ DECOS[decoIdx % 4],
                    glm::vec3((float)ox + 0.5f, (float)(gy + 1), (float)oz + 0.5f),
                    (float)((rng() % 4) * 90), (uint32_t)rng() });
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
            (float)((rng() % 4) * 90), (uint32_t)rng() });
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
            int gy = townGroundLevel(gx, gz);
            if (gy < WORLD_SEA_LEVEL) continue;
            g_placements.push_back({ PropType::Fence,
                glm::vec3(px, (float)(gy + 1), pz), yaw, (uint32_t)rng() });
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
    // The building generator recorded the door's exact local cell (bakeBuilding
    // rotated it to match b.blocks). Use it directly — scanning the wall for the
    // widest air gap mis-fires on composite (L/T/U/...) footprints, where the
    // widest gap in the front row is a set-back or courtyard mouth, not the door.
    out.x = b.doorX;
    out.z = b.doorZ;
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

// A wall lantern mounted beside every front door — a warm porch light for each
// home. Lit after dark through the Lantern path in renderer.cpp.
void placeDoorLantern(const TownBuilding& b) {
    if ((b.doorDX == 0 && b.doorDZ == 0) || b.rooms.empty()) return;
    DoorCellLocal dc = findFrontDoorCell(b);
    if (!dc.valid) return;

    const float fwdX = (float)b.doorDX, fwdZ = (float)b.doorDZ;   // door outward normal
    const float alongX = -fwdZ, alongZ = fwdX;                    // along the wall
    const int   side   = ((b.wx * 7 + b.wz * 13) & 1) ? 1 : -1;   // which side, deterministically
    // Two cells to one side of the 3-wide door cut, pushed just past the wall.
    const float cx = (float)(b.wx + dc.x) + 0.5f + alongX * 2.0f * (float)side;
    const float cz = (float)(b.wz + dc.z) + 0.5f + alongZ * 2.0f * (float)side;
    const float px = cx + fwdX * 0.55f;
    const float pz = cz + fwdZ * 0.55f;
    const float py = (float)b.baseY + 2.6f;                       // head-height sconce
    const float yaw = glm::degrees(std::atan2(fwdX, fwdZ));
    g_placements.push_back({ PropType::Lantern, glm::vec3(px, py, pz), yaw, 0 });
}

// A fenced paddock behind each stable — a rectangular run of fence posts,
// skipping any that would land inside a building or below the sea, so horses
// have a yard to graze. Gives stables a recognisable "fences outside" look.
void placeStablePaddock(const TownPlan& plan, const TownBuilding& b) {
    std::mt19937 rng(worldSeed() ^ (uint32_t)(b.wx * 2654435761u)
                                 ^ (uint32_t)(b.wz * 40503u) ^ 0x5AB1Eu);
    int cx = b.wx + b.dimX / 2, cz = b.wz + b.dimZ / 2;
    int bdx = -b.doorDX, bdz = -b.doorDZ;            // out the back of the stable
    if (bdx == 0 && bdz == 0) bdz = 1;
    int ext = (bdx != 0) ? b.dimX / 2 : b.dimZ / 2;
    const int half = 7;
    int pcx = cx + bdx * (ext + 4 + half);
    int pcz = cz + bdz * (ext + 4 + half);
    auto post = [&](int x, int z, float yaw) {
        if (insideAnyBuilding(plan, x, z)) return;
        int gy = townGroundLevel(x, z);
        if (gy < WORLD_SEA_LEVEL) return;
        g_placements.push_back({ PropType::Fence,
            glm::vec3((float)x + 0.5f, (float)(gy + 1), (float)z + 0.5f), yaw, (uint32_t)rng() });
    };
    for (int x = pcx - half; x <= pcx + half; x += 2) {
        post(x, pcz - half, 90.0f);
        post(x, pcz + half, 90.0f);
    }
    for (int z = pcz - half + 2; z <= pcz + half - 2; z += 2) {
        post(pcx - half, z, 0.0f);
        post(pcx + half, z, 0.0f);
    }
}

// A modelled fence around a crop field's perimeter (replacing the old wall of
// wood blocks), with a gate gap on the front (door) side. Sections every 2
// blocks read as a continuous run. The field is flattened to baseY, so fences
// rest on baseY+1.
void placeFarmFence(const TownBuilding& b) {
    if (b.kind != (int)BuildingKind::Farm || b.dimX <= 0) return;
    std::mt19937 rng(worldSeed() ^ (uint32_t)(b.wx * 2654435761u)
                                 ^ (uint32_t)(b.wz * 40503u) ^ 0xFA2Eu);
    int x0 = b.wx, x1 = b.wx + b.dimX - 1;
    int z0 = b.wz, z1 = b.wz + b.dimZ - 1;
    int gx = b.wx + b.doorX, gz = b.wz + b.doorZ;        // world gate cell
    float fy = (float)(b.baseY + 1);
    auto post = [&](int x, int z, float yaw) {
        if (std::abs(x - gx) <= 1 && std::abs(z - gz) <= 1) return;   // leave the gate open
        g_placements.push_back({ PropType::Fence,
            glm::vec3((float)x + 0.5f, fy, (float)z + 0.5f), yaw, (uint32_t)rng() });
    };
    for (int x = x0; x <= x1; x += 2) { post(x, z0, 90.0f); post(x, z1, 90.0f); }   // front / back runs
    for (int z = z0 + 2; z <= z1 - 2; z += 2) { post(x0, z, 0.0f); post(x1, z, 0.0f); }  // side runs
}

// Flower pots flanking every building's front door, plus a cluster of barrels
// beside pubs, blacksmiths, and bakeries.
void placeEntranceDecorations(const TownBuilding& b) {
    if ((b.doorDX == 0 && b.doorDZ == 0) || b.rooms.empty()) return;
    DoorCellLocal dc = findFrontDoorCell(b);
    if (!dc.valid) return;

    const float fwdX  = (float)b.doorDX, fwdZ  = (float)b.doorDZ;
    const float sideX = -fwdZ,           sideZ  =  fwdX;

    // One flower pot on each side of the door, just clear of the wall.
    for (int side : { -1, 1 }) {
        float cx = (float)(b.wx + dc.x) + 0.5f + sideX * 2.5f * (float)side;
        float cz = (float)(b.wz + dc.z) + 0.5f + sideZ * 2.5f * (float)side;
        g_placements.push_back({ PropType::FlowerPot,
            glm::vec3(cx + fwdX * 0.6f, (float)(b.baseY + 1), cz + fwdZ * 0.6f),
            glm::degrees(std::atan2(fwdX, fwdZ)), 0 });
    }

    // Barrels beside pubs, blacksmiths, and bakeries.
    auto kind = (BuildingKind)b.kind;
    if (kind == BuildingKind::Pub || kind == BuildingKind::Blacksmith ||
        kind == BuildingKind::Bakery) {
        const int wallSide = ((b.wx * 5 + b.wz * 11) & 1) ? 1 : -1;
        for (int i = 0; i < 2; i++) {
            float cx = (float)(b.wx + dc.x) + 0.5f + sideX * (4.5f + (float)i) * (float)wallSide;
            float cz = (float)(b.wz + dc.z) + 0.5f + sideZ * (4.5f + (float)i) * (float)wallSide;
            g_placements.push_back({ PropType::Barrel,
                glm::vec3(cx + fwdX * 0.7f, (float)(b.baseY + 1), cz + fwdZ * 0.7f),
                (float)((b.wx * 3 + b.wz * 7 + i * 37) % 4) * 90.0f, 0 });
        }
    }
}

// Tiles BuntingSpan segments along chains of nearby lamp posts, creating the
// appearance of festive bunting zigzagging across the street from post to post.
//
// Town selection: Market/Statue towns always get bunting, Campfire towns never
// do, Well towns get it on a coin-flip.  This keeps bunting out of the most
// rural settlements while making market towns feel distinctly festive.
//
// Chain strategy: start from each unvisited post, then keep extending to the
// nearest unused neighbour within MAX_SPAN.  Because lamp posts alternate sides
// of the road, the chain naturally zigzags A(left)→B(right)→C(left)→D(right)…
// spanning the whole street rather than just one crossing.
void placeBuntingSpans(const Town& t) {
    // Campfire villages are too rural for bunting.
    if (t.centerpiece == TownCenter::Campfire) return;
    // Well towns get bunting in roughly half of cases.
    if (t.centerpiece == TownCenter::Well) {
        uint32_t h = worldSeed() ^ (uint32_t)(t.center.x * 2654435761u)
                                 ^ (uint32_t)(t.center.y * 40503u) ^ 0xFE57u;
        if (h & 1u) return;
    }
    // Market and Statue towns always get bunting.

    constexpr float MAX_SPAN = 18.0f;
    constexpr float SEG_LEN  = 50.0f * PROP_SCALE;   // 3.0 world units per segment

    auto groundAt = [](int wx, int wz) {
        int gy = townFlatLevelAt(wx, wz);
        return gy < 1 ? sampleSurfaceSolid(wx, wz) : gy;
    };

    // Emit one span's worth of tiled segments between posts a and b.
    auto stringSpan = [&](size_t a, size_t b) {
        int   gy1 = groundAt(t.lampPosts[a].x, t.lampPosts[a].y);
        int   gy2 = groundAt(t.lampPosts[b].x, t.lampPosts[b].y);
        float hangY = (float)std::max(gy1, gy2) + 3.5f;

        float ax = (float)t.lampPosts[a].x + 0.5f, az = (float)t.lampPosts[a].y + 0.5f;
        float bx = (float)t.lampPosts[b].x + 0.5f, bz = (float)t.lampPosts[b].y + 0.5f;
        float dx = bx - ax, dz = bz - az;
        float dist    = std::sqrt(dx * dx + dz * dz);
        float spanYaw = glm::degrees(std::atan2(dx, dz));
        for (float u = SEG_LEN * 0.5f; u < dist; u += SEG_LEN) {
            float frac = u / dist;
            g_placements.push_back({ PropType::BuntingSpan,
                glm::vec3(ax + dx * frac, hangY, az + dz * frac),
                spanYaw, 0 });
        }
    };

    const size_t n = t.lampPosts.size();
    std::vector<bool> used(n, false);

    for (size_t i = 0; i < n; i++) {
        if (used[i]) continue;
        used[i] = true;
        size_t cur = i;

        // Extend the chain greedily: always jump to the nearest unused post.
        while (true) {
            float  bestDist = MAX_SPAN + 1.0f;
            size_t bestNext = n;
            for (size_t j = 0; j < n; j++) {
                if (used[j]) continue;
                float dx = (float)(t.lampPosts[j].x - t.lampPosts[cur].x);
                float dz = (float)(t.lampPosts[j].y - t.lampPosts[cur].y);
                float d  = std::sqrt(dx * dx + dz * dz);
                if (d < bestDist) { bestDist = d; bestNext = j; }
            }
            if (bestNext == n || bestDist > MAX_SPAN) break;

            stringSpan(cur, bestNext);
            used[bestNext] = true;
            cur = bestNext;
        }
    }
}

// Adds detail to the town centre: physical goods on market stalls, produce
// and crates around the market, a fountain for civilised settlements, and a
// light scatter of flowers and benches around the central plaza.
void placePlazaDetail(const Town& t) {
    if (t.buildings.empty()) return;
    const TownBuilding& cp = t.buildings[0];   // centrepiece is always first
    if (cp.kind != 0) return;

    auto groundAt = [](int wx, int wz) {
        int gy = townFlatLevelAt(wx, wz);
        return gy < 1 ? sampleSurfaceSolid(wx, wz) : gy;
    };

    // ── Market: ProducePile on each stall counter, Barrel/Crate outside ──────
    if (t.centerpiece == TownCenter::Market) {
        // The 4 stalls sit at local corners (1,1), (9,1), (1,9), (9,9).
        // The counter top is at baseY + 2, so props placed there rest on the counter.
        struct StallInfo { int lx, lz; float yaw; PropType outside; };
        const StallInfo STALLS[4] = {
            { 2,  2,   0.0f, PropType::Barrel },
            {10,  2,  90.0f, PropType::Crate  },
            { 2, 10, 180.0f, PropType::Crate  },
            {10, 10, 270.0f, PropType::Barrel },
        };
        for (const StallInfo& s : STALLS) {
            // Produce pile on the counter.
            g_placements.push_back({ PropType::ProducePile,
                glm::vec3((float)(cp.wx + s.lx) + 0.5f, (float)(cp.baseY + 2),
                          (float)(cp.wz + s.lz) + 0.5f),
                s.yaw, 0 });
            // Barrel or crate beside the stall (1 block outside building footprint).
            int ox = cp.wx + s.lx + (s.lx < 6 ? -2 : 2);
            int oz = cp.wz + s.lz + (s.lz < 6 ? -2 : 2);
            int gy = groundAt(ox, oz);
            g_placements.push_back({ s.outside,
                glm::vec3((float)ox + 0.5f, (float)(gy + 1), (float)oz + 0.5f),
                s.yaw, 0 });
        }
    }

    // ── Fountain: placed 5 blocks east of the centrepiece for non-Well/Market towns
    if (t.centerpiece == TownCenter::Statue || t.centerpiece == TownCenter::Campfire) {
        int fx = t.center.x + cp.dimX / 2 + 4;
        int fz = t.center.y;
        int gy = groundAt(fx, fz);
        g_placements.push_back({ PropType::Fountain,
            glm::vec3((float)fx + 0.5f, (float)(gy + 1), (float)fz + 0.5f),
            0.0f, 0 });
    }

    // ── Plaza furnishings ─────────────────────────────────────────────────────
    // The paved square grew a lot when towns were spread out, so detail is laid
    // in concentric rings reaching out toward the plaza edge rather than hugging
    // the centrepiece, with seating clustered so townsfolk have places to gather.
    if (t.plazaR < 8) return;
    const int innerR = cp.dimX / 2 + 4;            // just outside the centrepiece

    auto inCentrepiece = [&](int rx, int rz) {
        return rx >= cp.wx - 1 && rx < cp.wx + cp.dimX + 1 &&
               rz >= cp.wz - 1 && rz < cp.wz + cp.dimZ + 1;
    };
    auto faceCentre = [&](int rx, int rz) {
        return glm::degrees(std::atan2((float)(t.center.x - rx),
                                       (float)(t.center.y - rz)));
    };
    auto placeFacing = [&](PropType type, int rx, int rz, float yaw) {
        if (inCentrepiece(rx, rz)) return;
        int gy = groundAt(rx, rz);
        if (gy < WORLD_SEA_LEVEL) return;
        g_placements.push_back({ type,
            glm::vec3((float)rx + 0.5f, (float)(gy + 1), (float)rz + 0.5f),
            yaw, 0 });
    };

    // A notice board near the eastern plaza edge, facing in toward the square.
    {
        int nbR = std::max(innerR + 5, t.plazaR - 4);
        int nx = t.center.x + nbR, nz = t.center.y;
        placeFacing(PropType::NoticeBoard, nx, nz, faceCentre(nx, nz));
    }

    // Market & well towns get a few covered stalls on a mid-plaza ring.
    if (t.centerpiece == TownCenter::Market || t.centerpiece == TownCenter::Well) {
        const int sR = std::max(innerR + 7, (int)(t.plazaR * 0.55f));
        const int nStalls = (t.plazaR >= 22) ? 4 : 3;
        for (int i = 0; i < nStalls; i++) {
            // Offset from 0 so a stall never lands on the east-side notice board.
            float a  = 0.6f + (6.2831853f / (float)nStalls) * (float)i;
            int   rx = t.center.x + (int)(std::cos(a) * (float)sR);
            int   rz = t.center.y + (int)(std::sin(a) * (float)sR);
            placeFacing(PropType::MarketStall, rx, rz, faceCentre(rx, rz));
        }
    }

    // Two concentric rings of seating, flowers and greenery across the square.
    // Benches face inward so seated townsfolk look onto the centre.
    static const PropType RING_A[] = {
        PropType::Bench, PropType::FlowerBed, PropType::Bench, PropType::PottedPlant
    };
    static const PropType RING_B[] = {
        PropType::FlowerBed, PropType::Bench, PropType::PottedPlant, PropType::Bench
    };
    struct Ring { int radius; const PropType* props; float phase; };
    const int outerR = std::max(innerR + 10, (int)(t.plazaR * 0.78f));
    const Ring rings[2] = {
        { innerR, RING_A, 0.0f },
        { outerR, RING_B, 0.5f },
    };
    for (const Ring& ring : rings) {
        if (ring.radius >= t.plazaR) continue;
        int nSlots = std::max(4, (int)(6.2831853f * (float)ring.radius / 7.0f));
        for (int i = 0; i < nSlots; i++) {
            float a  = (6.2831853f / (float)nSlots) * ((float)i + ring.phase);
            int   rx = t.center.x + (int)(std::cos(a) * (float)ring.radius);
            int   rz = t.center.y + (int)(std::sin(a) * (float)ring.radius);
            placeFacing(ring.props[i % 4], rx, rz, faceCentre(rx, rz));
        }
    }
}

void build() {
    const TownPlan& plan = getTownPlan();
    for (const Town& t : plan.towns) {
        for (const TownBuilding& b : t.buildings) {
            if (!b.rooms.empty()) placeFurniture(b);
            placeTradeSign(b);
            placeDoorLantern(b);
            placeEntranceDecorations(b);
            if (b.kind == (int)BuildingKind::Stable) placeStablePaddock(plan, b);
            if (b.kind == (int)BuildingKind::Farm)   placeFarmFence(b);
        }
        placeStreetLampProps(t);
        placeDecorations(t);
        placeBuntingSpans(t);
        placePlazaDetail(t);
    }
    for (const TownBuilding& b : plan.roadside)        // roadside farms get fences too
        if (b.kind == (int)BuildingKind::Farm) placeFarmFence(b);
    placeFences(plan);

    // Dungeons & castles: furnish every room with the same detailed Prop
    // furniture houses use (tables, chairs, shelves, cookers, barrels, rugs…).
    const DungeonPlan& dp = getDungeonPlan();
    for (const auto& dptr : dp.dungeons)
        for (const DungeonRoom& rm : dptr->rooms)
            furnishDungeonRoom(*dptr, rm);

    // Graveyard tombstones — rows inside each town's graveyard fence, facing the
    // gate, with per-stone variation (variant, slight lean, the odd empty plot)
    // so it reads as a weathered graveyard rather than a grid. The central
    // walkway is kept clear and a lamp post lights a front corner.
    for (const Graveyard& gy : getTownPlan().graveyards) {
        uint32_t h = gy.seed ? gy.seed : 1u;
        auto nrand = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h; };
        float faceYaw = glm::degrees(std::atan2((float)gy.gateDX, (float)gy.gateDZ));
        float fdx = (float)gy.gateDX, fdz = (float)gy.gateDZ;   // toward the gate

        for (int rz = -gy.halfZ + 2; rz <= gy.halfZ - 2; rz += 3)
            for (int rx = -gy.halfX + 2; rx <= gy.halfX - 2; rx += 3) {
                if (gy.gateDX != 0 && rz == 0) continue;   // keep the walkway clear
                if (gy.gateDZ != 0 && rx == 0) continue;
                uint32_t r = nrand();
                if ((r & 15u) == 0u) continue;             // a few empty plots
                glm::vec3 base((float)(gy.center.x + rx) + 0.5f, (float)(gy.baseY + 1),
                               (float)(gy.center.y + rz) + 0.5f);
                float jit = (float)((int)((r >> 6) % 21u) - 10);   // +-10 deg lean

                uint32_t kind = (r >> 3) % 24u;
                if (kind == 0) {                            // a stone urn
                    g_placements.push_back({ PropType::StoneUrn, base, faceYaw + jit, r });
                } else if (kind <= 3) {                     // a fresh grave: mound + marker
                    g_placements.push_back({ PropType::SoilMound, base, faceYaw + jit, r });
                    uint32_t m = (r & 0x300u);
                    if      (m == 0x100u) g_placements.push_back({ PropType::Spade,      base + glm::vec3(0.25f, 0.0f, 0.25f), faceYaw, r });
                    else if (m == 0x200u) g_placements.push_back({ PropType::GraveCross, base, faceYaw + jit, r });
                } else {                                    // a headstone
                    PropType pt = (kind % 4u == 0u) ? PropType::TombstoneCross
                                : (kind % 7u == 0u) ? PropType::GraveCross
                                :                      PropType::Tombstone;
                    g_placements.push_back({ pt, base, faceYaw + jit, r });
                }

                // A tended grave: flowers or a wreath laid just in front (toward
                // the gate) of roughly two in five markers.
                uint32_t deco = (r >> 12) % 5u;
                if (deco == 0)
                    g_placements.push_back({ PropType::GraveFlowers,
                        base + glm::vec3(fdx * 0.6f, 0.0f, fdz * 0.6f), faceYaw, r });
                else if (deco == 1)
                    g_placements.push_back({ PropType::FlowerWreath,
                        base + glm::vec3(fdx * 0.7f, 0.0f, fdz * 0.7f), faceYaw, r });
            }

        // Two bare dead trees in the back corners (away from the gate).
        for (int s = -1; s <= 1; s += 2) {
            int cxoff, czoff;
            if (gy.gateDX != 0) { cxoff = -gy.gateDX * (gy.halfX - 1); czoff = s * (gy.halfZ - 1); }
            else                { cxoff = s * (gy.halfX - 1);          czoff = -gy.gateDZ * (gy.halfZ - 1); }
            g_placements.push_back({ PropType::DeadTree,
                glm::vec3((float)(gy.center.x + cxoff) + 0.5f, (float)(gy.baseY + 1),
                          (float)(gy.center.y + czoff) + 0.5f),
                (float)((gy.seed >> (s > 0 ? 5 : 11)) % 360u), gy.seed });
        }

        // A modelled fence around the perimeter (like the farms), with a gap on
        // the gate side. Sections every 2 blocks read as a continuous run; the
        // plot is flattened to baseY so the fences rest on baseY+1.
        {
            int x0 = gy.center.x - (gy.halfX + 1), x1 = gy.center.x + (gy.halfX + 1);
            int z0 = gy.center.y - (gy.halfZ + 1), z1 = gy.center.y + (gy.halfZ + 1);
            float fy = (float)(gy.baseY + 1);
            int gxc = gy.center.x + gy.gateDX * (gy.halfX + 1);   // gate centre on the ring
            int gzc = gy.center.y + gy.gateDZ * (gy.halfZ + 1);
            auto fpost = [&](int x, int z, float yw) {
                if (std::abs(x - gxc) <= 1 && std::abs(z - gzc) <= 1) return;   // leave the gate open
                g_placements.push_back({ PropType::Fence,
                    glm::vec3((float)x + 0.5f, fy, (float)z + 0.5f), yw, nrand() });
            };
            for (int x = x0; x <= x1; x += 2) { fpost(x, z0, 90.0f); fpost(x, z1, 90.0f); }   // front / back
            for (int z = z0 + 2; z <= z1 - 2; z += 2) { fpost(x0, z, 0.0f); fpost(x1, z, 0.0f); } // sides
        }

        // A pair of lamps at the front (gate-side) interior corners for an eerie glow.
        for (int s = -1; s <= 1; s += 2) {
            int lx, lz;
            if (gy.gateDX != 0) { lx = gy.center.x + gy.gateDX * (gy.halfX - 1); lz = gy.center.y + s * (gy.halfZ - 1); }
            else                { lx = gy.center.x + s * (gy.halfX - 1);          lz = gy.center.y + gy.gateDZ * (gy.halfZ - 1); }
            g_placements.push_back({ PropType::StreetLamp,
                glm::vec3((float)lx + 0.5f, (float)(gy.baseY + 1), (float)lz + 0.5f), 0.0f, gy.seed });
        }
    }
}

std::vector<DoorPlacement> g_doors;

// One door per house, in the gap of its front wall.
void buildDoors() {
    const TownPlan& plan = getTownPlan();
    for (const Town& t : plan.towns)
        for (const TownBuilding& b : t.buildings) {
            // Every residential or special building has a door direction set
            // by its generator. Centrepieces (well/market/etc) and farms leave
            // doorDX/doorDZ at zero and are skipped here.
            if ((b.doorDX == 0 && b.doorDZ == 0) || b.rooms.empty()) continue;

            // The generator recorded the door's exact local cell (bakeBuilding
            // rotated it to match b.blocks); world-project it. Scanning the wall
            // for the widest air gap mis-fires on composite (L/T/U/...) houses,
            // where the widest front-row gap is a set-back or courtyard mouth
            // rather than the doorway.
            const int wallX = b.wx + b.doorX;
            const int wallZ = b.wz + b.doorZ;

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
    static std::mutex            mtx;
    static std::atomic<uint64_t> built{~0ull};
    rebuildCacheOnSeedChange(built, mtx, [] { g_placements.clear(); build(); });
    return g_placements;
}

const std::vector<DoorPlacement>& getDoorPlacements() {
    static std::mutex            mtx;
    static std::atomic<uint64_t> built{~0ull};
    rebuildCacheOnSeedChange(built, mtx, [] { g_doors.clear(); buildDoors(); });
    return g_doors;
}

// --- Wild bush scatter ------------------------------------------------------
namespace {
inline uint32_t wildHash(int x, int z, uint32_t seed) {
    uint32_t h = seed + 0x9E3779B9u;
    h ^= (uint32_t)x * 0x85EBCA77u; h = (h ^ (h >> 15)) * 0xC2B2AE3Du;
    h ^= (uint32_t)z * 0x27D4EB2Fu; h = (h ^ (h >> 13)) * 0x165667B1u;
    h ^= h >> 16; return h;
}
inline uint64_t wildKeyFor(int cx, int cz, int slot) {
    uint64_t k = ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
    return k ^ (0x9E3779B97F4A7C15ull * (uint64_t)(slot + 1));
}
// Bush variant biased by biome (Plains=0, Forest=1, Desert=2, Mountains=3,
// Tundra=4, Savanna=5, Jungle=6).
PropType wildBushVariant(int biome, uint32_t h) {
    uint32_t v = h % 100u;
    switch (biome) {
        case 2: return PropType::BushDry;                                    // Desert
        case 5: return (v < 68) ? PropType::BushDry : PropType::Bush;         // Savanna
        case 3:                                                              // Mountains
        case 4: return (v < 55) ? PropType::BushConifer : PropType::BushDry;  // Tundra
        case 1:                                                              // Forest
        case 6: return (v < 45) ? PropType::Bush                             // Jungle
                      : (v < 72) ? PropType::BushBerry : PropType::BushFlowering;
        default: return (v < 50) ? PropType::Bush                            // Plains, etc.
                       : (v < 76) ? PropType::BushFlowering : PropType::BushBerry;
    }
}

// World-XZ footprints of roadside crop fields, built once. Town farms already
// sit in the town flat zone (skipped by the townFlatLevelAt check); this keeps
// wild bushes out of the stand-alone roadside fields too.
bool insideRoadsideFarm(int wx, int wz) {
    static std::vector<glm::ivec4> rects;          // (x0, z0, x1, z1) with a 1-block margin
    static std::mutex            mtx;
    static std::atomic<uint64_t> built{~0ull};
    rebuildCacheOnSeedChange(built, mtx, [] {
        rects.clear();
        for (const TownBuilding& b : getTownPlan().roadside)
            if (b.kind == (int)BuildingKind::Farm && b.dimX > 0)
                rects.push_back(glm::ivec4(b.wx - 1, b.wz - 1, b.wx + b.dimX, b.wz + b.dimZ));
    });
    for (const glm::ivec4& r : rects)
        if (wx >= r.x && wx <= r.z && wz >= r.y && wz <= r.w) return true;
    return false;
}
}  // namespace

void gatherWildProps(const glm::vec3& center, float radius,
                     const std::unordered_set<uint64_t>& live,
                     std::vector<WildProp>& out) {
    out.clear();
    const int      CELL = 11;                 // world blocks per scatter cell
    const float    in2  = radius * radius;
    const uint32_t seed = worldSeed();
    const size_t   CAP  = 32;                  // cap new bushes (and surface samples) per call
    const int cx0 = (int)std::floor((center.x - radius) / CELL);
    const int cx1 = (int)std::floor((center.x + radius) / CELL);
    const int cz0 = (int)std::floor((center.z - radius) / CELL);
    const int cz1 = (int)std::floor((center.z + radius) / CELL);

    for (int cz = cz0; cz <= cz1 && out.size() < CAP; cz++)
        for (int cx = cx0; cx <= cx1 && out.size() < CAP; cx++) {
            uint32_t h = wildHash(cx, cz, seed);
            int n = 0;
            uint32_t r = h & 0xF;
            if      (r < 6) n = 1;             // ~44% of cells get one bush
            else if (r < 8) n = 2;             // ~12% get two
            for (int s = 0; s < n; s++) {
                uint32_t sh = h ^ (0x9E3779B1u * (uint32_t)(s + 1));
                float px = (float)cx * CELL + (float)(sh & 0xFF) / 255.0f * (CELL - 1);
                float pz = (float)cz * CELL + (float)((sh >> 8) & 0xFF) / 255.0f * (CELL - 1);
                float dx = px - center.x, dz = pz - center.z;
                if (dx * dx + dz * dz > in2) continue;
                uint64_t key = wildKeyFor(cx, cz, s);
                if (live.count(key)) continue;            // already spawned — skip the sampling
                int wx = (int)std::floor(px), wz = (int)std::floor(pz);
                if (townFlatLevelAt(wx, wz) >= 1) continue;       // leave town ground to town props
                if (insideRoadsideFarm(wx, wz)) continue;          // keep bushes out of crop fields
                // Rest on the ACTUAL top solid block (the 3D-density surface the
                // chunk really generates) — not sampleSurface()'s smooth blended
                // target, which sits a block off and leaves bushes hovering.
                int gy = sampleSurfaceSolid(wx, wz);
                if (gy < WORLD_SEA_LEVEL + 1) continue;           // never in water
                int biome = sampleSurface(wx, wz).biome;          // climate → variant + density
                uint32_t cull = (sh >> 16) & 0xFF;
                if ((biome == 2 || biome == 4) && cull > 96)  continue;  // ~38% keep (Desert/Tundra)
                if (biome == 3 && cull > 165) continue;                  // ~65% keep (Mountains)
                out.push_back({ key, { wildBushVariant(biome, sh),
                                       glm::vec3(px, (float)(gy + 1), pz),
                                       (float)((sh >> 24) % 360u), sh } });
                if (out.size() >= CAP) break;
            }
        }
}
