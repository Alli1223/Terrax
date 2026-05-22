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
        bool wallLantern = false;
        if (s.wall) {
            if (rng() % 4 == 0) {                              // a wall-mounted lantern
                t = PropType::Lantern;
                wallLantern = true;
            } else {
                static const PropType WALL[] = { PropType::Bookshelf, PropType::Bookshelf,
                                                 PropType::Bed, PropType::Cooker };
                t = WALL[rng() % 4];
            }
        } else {
            static const PropType OPEN[] = { PropType::Table, PropType::Table,
                                             PropType::Chair };
            t = OPEN[rng() % 3];
        }
        glm::vec3 pos((float)(b.wx + s.x) + 0.5f,
                      (float)(b.baseY + s.y + 1) + (wallLantern ? 3.0f : 0.0f),
                      (float)(b.wz + s.z) + 0.5f);
        g_placements.push_back({ t, pos, s.yaw, rng() });
        if (t == PropType::Table) {                            // a lantern (or crockery) on top
            glm::vec3 cp = pos; cp.y += TABLE_TOP_H;
            PropType on = (rng() % 5 == 0) ? PropType::Crockery : PropType::Lantern;
            g_placements.push_back({ on, cp, s.yaw, rng() });
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
void placeStreetLampProps(const Town& t) {
    std::mt19937 rng(worldSeed()
                     ^ (uint32_t)(t.center.x * 73856093)
                     ^ (uint32_t)(t.center.y * 19349663) ^ 0x5A1Du);
    for (const glm::ivec2& L : t.lampPosts) {
        int gy = sampleSurfaceSolid(L.x, L.y);
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

void build() {
    const TownPlan& plan = getTownPlan();
    for (const Town& t : plan.towns) {
        for (const TownBuilding& b : t.buildings)
            if (b.kind == 1) placeFurniture(b);
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
            if (b.kind != 1 || (b.doorDX == 0 && b.doorDZ == 0)) continue;

            auto solid = [&](int x, int y, int z) {
                if (x < 0 || x >= b.dimX || y < 0 || y >= b.dimY ||
                    z < 0 || z >= b.dimZ) return false;
                return b.blocks[((size_t)y * b.dimZ + z) * b.dimX + x]
                       != (uint8_t)BlockType::Air;
            };
            // The footprint edge can be a roof eave — scan inward (above the
            // doorway) for the real wall plane, then find the doorway gap's
            // centre along that wall at door height.
            int wallX, wallZ;
            if (b.doorDZ != 0) {
                int mx = b.dimX / 2, wz;
                if (b.doorDZ < 0) { wz = 0;          while (wz < b.dimZ - 1 && !solid(mx, 5, wz)) wz++; }
                else              { wz = b.dimZ - 1; while (wz > 0          && !solid(mx, 5, wz)) wz--; }
                int dx = airRunCentre(b.dimX, [&](int x){ return !solid(x, 2, wz); });
                wallX = b.wx + dx; wallZ = b.wz + wz;
            } else {
                int mz = b.dimZ / 2, wx;
                if (b.doorDX < 0) { wx = 0;          while (wx < b.dimX - 1 && !solid(wx, 5, mz)) wx++; }
                else              { wx = b.dimX - 1; while (wx > 0          && !solid(wx, 5, mz)) wx--; }
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
