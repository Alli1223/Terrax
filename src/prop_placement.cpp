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

// Scatters furniture across the interior floors of one baked house.
void placeFurniture(const TownBuilding& b) {
    if (b.dimX < 7 || b.dimZ < 7 || b.dimY < 5) return;
    std::mt19937 rng(worldSeed()
                     ^ (uint32_t)(b.wx * 73856093)
                     ^ (uint32_t)(b.wz * 19349663) ^ 0xF0A1u);

    struct Spot { int x, y, z; bool wall; float yaw; };
    std::vector<Spot> spots;
    for (int x = 2; x <= b.dimX - 3; x++)
        for (int z = 2; z <= b.dimZ - 3; z++) {
            int colTop = -1;
            for (int y = b.dimY - 1; y >= 0; y--)
                if (solidAt(b, x, y, z)) { colTop = y; break; }
            if (colTop < 4) continue;
            for (int y = 0; y + 2 < colTop; y++) {
                if (!solidAt(b, x, y, z)) continue;            // need a floor
                if (solidAt(b, x, y + 1, z) || solidAt(b, x, y + 2, z))
                    continue;                                  // need headroom
                Spot s{ x, y, z, true, 0.0f };
                if      (solidAt(b, x - 1, y + 1, z)) s.yaw = 90.0f;
                else if (solidAt(b, x + 1, y + 1, z)) s.yaw = 270.0f;
                else if (solidAt(b, x, y + 1, z - 1)) s.yaw = 0.0f;
                else if (solidAt(b, x, y + 1, z + 1)) s.yaw = 180.0f;
                else { s.wall = false; s.yaw = (float)((rng() % 4) * 90); }
                spots.push_back(s);
            }
        }
    if (spots.empty()) return;

    for (size_t i = spots.size(); i > 1; i--)                  // deterministic shuffle
        std::swap(spots[i - 1], spots[rng() % i]);

    int cap = 6 + (int)(rng() % 7);                            // 6..12 pieces
    int placed = 0;
    std::vector<glm::ivec3> used;
    for (const Spot& s : spots) {
        if (placed >= cap) break;
        bool tooClose = false;
        for (const glm::ivec3& u : used)
            if (u.y == s.y && std::abs(u.x - s.x) < 3 && std::abs(u.z - s.z) < 3) {
                tooClose = true; break;
            }
        if (tooClose) continue;

        PropType t;
        if (s.wall) {
            static const PropType WALL[] = { PropType::Bookshelf, PropType::Bookshelf,
                                             PropType::Bed, PropType::Cooker };
            t = WALL[rng() % 4];
        } else {
            static const PropType OPEN[] = { PropType::Table, PropType::Chair,
                                             PropType::Lantern };
            t = OPEN[rng() % 3];
        }
        glm::vec3 pos((float)(b.wx + s.x) + 0.5f,
                      (float)(b.baseY + s.y + 1),
                      (float)(b.wz + s.z) + 0.5f);
        g_placements.push_back({ t, pos, s.yaw, rng() });
        if (t == PropType::Table) {                            // a place setting on top
            glm::vec3 cp = pos; cp.y += TABLE_TOP_H;
            g_placements.push_back({ PropType::Crockery, cp, s.yaw, rng() });
        }
        used.push_back(glm::ivec3(s.x, s.y, s.z));
        placed++;
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
    // Street lights are baked structures (see town.cpp) so they emit real
    // light; the decoration props here are the non-lighting roadside dressing.
    static const PropType DECOS[] = { PropType::Bush, PropType::PottedPlant,
                                      PropType::Bench, PropType::Fence };
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
                int ox = a.x + (int)(dx * u + perpX * 3.0f * side);
                int oz = a.y + (int)(dz * u + perpZ * 3.0f * side);
                decoIdx++;
                if (insideBuilding(ox, oz)) continue;
                int gy = sampleSurface(ox, oz).height;
                if (gy < WORLD_SEA_LEVEL) continue;            // keep them out of water
                g_placements.push_back({ DECOS[decoIdx % 4],
                    glm::vec3((float)ox + 0.5f, (float)(gy + 1), (float)oz + 0.5f),
                    (float)((rng() % 4) * 90), rng() });
            }
        }
    }
}

void build() {
    const TownPlan& plan = getTownPlan();
    for (const Town& t : plan.towns) {
        for (const TownBuilding& b : t.buildings)
            if (b.kind == 1) placeFurniture(b);
        placeDecorations(t);
    }
}

} // namespace

const std::vector<PropPlacement>& getPropPlacements() {
    std::call_once(g_once, [] { build(); });
    return g_placements;
}
