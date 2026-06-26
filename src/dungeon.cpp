#include "dungeon.h"
#include "castle.h"    // CastleDungeon + stampCastleChunk (overground castles)
#include "world.h"     // Chunk, CHUNK_SIZE/HEIGHT, sampleSurfaceSolid, worldSeed, WORLD_SEA_LEVEL
#include "town.h"      // getTownPlan — dungeons must stay clear of towns
#include "npc.h"       // NPCType — enemy rosters
#include <random>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <cmath>
#include <iostream>

// --- Shared layout + roster --------------------------------------------------

// Lowest solid surface across a dungeon's footprint — used to bury the rooms
// below even the low edge of the site, and to reject sites that dip to water.
static int footprintMinSurface(glm::ivec2 a, int half) {
    int mn = 1 << 30, step = std::max(1, half / 3);
    for (int dz = -half; dz <= half; dz += step)
        for (int dx = -half; dx <= half; dx += step)
            mn = std::min(mn, sampleSurfaceSolid(a.x + dx, a.y + dz));
    return mn;
}

// Highest solid surface across a footprint — used to reject castle sites where
// the ground rises so far above the anchor that the keep (and its entrance)
// would be buried in a hillside.
static int footprintMaxSurface(glm::ivec2 a, int half) {
    int mx = -(1 << 30), step = std::max(1, half / 3);
    for (int dz = -half; dz <= half; dz += step)
        for (int dx = -half; dx <= half; dx += step)
            mx = std::max(mx, sampleSurfaceSolid(a.x + dx, a.y + dz));
    return mx;
}

// Per-dungeon size tier → footprint half-extent and room-count multiplier.
// Greatly enlarged from the first pass: even a small dungeon is a real complex,
// and a rare tier-3 is a sprawling labyrinth.
static int   dungeonHalf(int tier) { static const int   H[4] = { 48, 80, 118, 150 };        return H[std::clamp(tier, 0, 3)]; }
static float dungeonMult(int tier) { static const float M[4] = { 1.4f, 2.2f, 3.2f, 4.2f };  return M[std::clamp(tier, 0, 3)]; }

// True if world cell (x,z) lies inside a room's carved silhouette. Shared by the
// layout's light placement, the chunk stamp's carving, and the prop-furnishing
// pass (declared in dungeon.h) so they always agree.
bool dungeonRoomContains(const DungeonRoom& rm, int x, int z) {
    int cx = (rm.mn.x + rm.mx.x) / 2, cz = (rm.mn.z + rm.mx.z) / 2;
    int hx = std::max(1, (rm.mx.x - rm.mn.x) / 2), hz = std::max(1, (rm.mx.z - rm.mn.z) / 2);
    int dx = x - cx, dz = z - cz;
    if (std::abs(dx) > hx || std::abs(dz) > hz) return false;
    switch (rm.shape) {
        case RoomShape::Circle:  { float nx = (float)dx / hx, nz = (float)dz / hz; return nx*nx + nz*nz <= 1.05f; }
        case RoomShape::Octagon: { int k = (int)((hx + hz) * 0.72f); return std::abs(dx) + std::abs(dz) <= k; }
        case RoomShape::Cross:   return std::abs(dx) <= hx / 2 || std::abs(dz) <= hz / 2;
        default:                 return true;   // Rect
    }
}

// The two grand landmark towers flank the surface mouth of an underground
// dungeon (computed identically for the beacon lights and the stamp).
static void dungeonTowerCenters(const Dungeon& d, glm::ivec2 out[2]) {
    out[0] = glm::ivec2(d.entrance.x + 1, d.entrance.z - 9);
    out[1] = glm::ivec2(d.entrance.x + 1, d.entrance.z + 9);
}
static const int DUNGEON_TOWER_R = 4;    // tower radius (9 across)
static const int DUNGEON_TOWER_H = 24;   // height above the surface — a tall landmark

void Dungeon::buildLayout(uint32_t seed, glm::ivec2 a, int surf,
                          int roomCount, int roomMin, int roomMax, bool wander) {
    std::mt19937 r(seed ? seed : 1u);
    auto ri = [&](int lo, int hi) { return lo + (int)(r() % (uint32_t)std::max(1, hi - lo + 1)); };

    anchor = a; surfaceY = surf;
    const int half   = dungeonHalf(sizeTier);   // footprint half-extent (scales with size)
    const int ROOM_H = 7;                        // taller chambers (was 5)
    int rc = std::max(6, (int)(roomCount * dungeonMult(sizeTier)));   // scaled room count
    // Bury the rooms well below the LOWEST surface across the whole footprint so
    // a sloping or low edge can't expose them (and they never poke out near
    // water). The grand stair bridges the now-deliberately-large gap.
    int deepRef = footprintMinSurface(a, half);
    floorY = deepRef - (ROOM_H + 12);
    if (floorY < 6) floorY = 6;

    // --- Rejection-sample DISTINCT rooms: no overlap, with a rock gap between
    // them so each is its own walled chamber connected only by corridors. ------
    struct Placed { int x, z, w, d; };
    std::vector<Placed> placed;
    placed.push_back({ a.x, a.y, ri(roomMin, roomMax), ri(roomMin, roomMax) });   // entrance room on the anchor
    int attempts = rc * 60;
    while ((int)placed.size() < rc && attempts-- > 0) {
        int w = ri(roomMin, roomMax), d = ri(roomMin, roomMax), x, z;
        if (wander) {                    // grow off an existing chamber (a cave complex)
            const Placed& b = placed[r() % placed.size()];
            int axis = ri(0, 3), gap = ri(6, 14);
            x = b.x + (axis == 0 ? (b.w/2 + w/2 + gap) : axis == 1 ? -(b.w/2 + w/2 + gap) : ri(-6, 6));
            z = b.z + (axis == 2 ? (b.d/2 + d/2 + gap) : axis == 3 ? -(b.d/2 + d/2 + gap) : ri(-6, 6));
        } else {                         // spread across the footprint
            x = a.x + ri(-half, half);
            z = a.y + ri(-half, half);
        }
        bool ok = true;                  // keep a rock gap between rooms
        for (const Placed& p : placed)
            if (std::abs(x - p.x) < (w + p.w)/2 + 5 && std::abs(z - p.z) < (d + p.d)/2 + 5) { ok = false; break; }
        if (ok) placed.push_back({ x, z, w, d });
    }
    rc = (int)placed.size();

    std::vector<glm::ivec3> centers;
    for (int k = 0; k < rc; k++) {
        const Placed& p = placed[k];
        DungeonRoom room;
        room.mn = glm::ivec3(p.x - p.w/2, floorY,              p.z - p.d/2);
        room.mx = glm::ivec3(p.x + p.w/2, floorY + ROOM_H - 1, p.z + p.d/2);
        uint32_t hsh = (uint32_t)(p.x * 73856093) ^ (uint32_t)(p.z * 19349663) ^ (uint32_t)(k * 2654435761u);
        bool squarish = std::abs(p.w - p.d) <= 4;
        if      (wander)                          room.shape = RoomShape::Circle;   // caves are blobby
        else if (squarish && (hsh & 3u) == 0u)    room.shape = RoomShape::Circle;
        else if (squarish && (hsh & 3u) == 1u)    room.shape = RoomShape::Octagon;
        else if (!squarish && (hsh % 5u) == 0u)   room.shape = RoomShape::Cross;
        else                                      room.shape = RoomShape::Rect;
        rooms.push_back(room);
        centers.push_back(glm::ivec3(p.x, floorY, p.z));
    }

    // --- Connect with corridors: a nearest-neighbour spanning tree (Prim) so
    // every room is reachable, plus a few loop edges for circulation. ----------
    auto d2 = [&](int i, int j) {
        long long dx = centers[i].x - centers[j].x, dz = centers[i].z - centers[j].z;
        return dx*dx + dz*dz;
    };
    if (rc >= 2) {
        std::vector<char> inTree(rc, 0); inTree[0] = 1; int added = 1;
        while (added < rc) {
            int bi = -1, bj = -1; long long best = (1LL << 62);
            for (int i = 0; i < rc; i++) if (inTree[i])
                for (int j = 0; j < rc; j++) if (!inTree[j]) {
                    long long dd = d2(i, j);
                    if (dd < best) { best = dd; bi = i; bj = j; }
                }
            if (bj < 0) break;
            corridors.push_back({ centers[bi], centers[bj] });
            inTree[bj] = 1; added++;
        }
        int extra = std::max(1, rc / 5);                 // a few loops
        for (int e = 0; e < extra; e++) {
            int i = (int)(r() % (uint32_t)rc), j = (int)(r() % (uint32_t)rc);
            long long lim = (long long)(half + 30) * (half + 30);
            if (i != j && d2(i, j) < lim) corridors.push_back({ centers[i], centers[j] });
        }
    }

    // --- Purposes: entrance (room 0), boss (farthest room), and themed rooms. -
    if (!rooms.empty()) {
        rooms.front().purpose = 1;
        int bossIdx = 0; long long far = -1;
        for (int k = 1; k < rc; k++) { long long dd = d2(0, k); if (dd > far) { far = dd; bossIdx = k; } }
        rooms[bossIdx].purpose = 2;
        rooms[bossIdx].shape   = RoomShape::Rect;        // the boss hall is a proper rectangular throne hall
        for (int k = 1; k < rc; k++) {
            if (rooms[k].purpose != 0) continue;
            uint32_t hsh = (uint32_t)(centers[k].x * 374761393) ^ (uint32_t)(centers[k].z * 668265263) ^ (uint32_t)(k * 40503u);
            int roll = (int)(hsh % 100u);
            if      (roll < 12) rooms[k].purpose = 3;    // throne
            else if (roll < 30) rooms[k].purpose = 4;    // library
            else if (roll < 46) rooms[k].purpose = 5;    // ornament / monument
            else if (roll < 58) rooms[k].purpose = 6;    // vault / treasure
            else if (roll < 70) rooms[k].purpose = 7;    // prison / cells
        }
    }

    // --- Grand descent: a long, wide vaulted stair from the surface mouth down
    // to the entrance room's +X edge (carved & widened in stampDungeonChunk). --
    if (!rooms.empty()) {
        int ex = rooms.front().mx.x;
        int ez = centers.front().z;
        int depth0  = surfaceY - floorY;
        int entSurf = sampleSurfaceSolid(ex + depth0, ez);
        int depth   = entSurf - floorY;
        if (depth < 6) depth = 6;
        entranceInner = glm::ivec3(ex,         floorY,  ez);
        entrance      = glm::ivec3(ex + depth, entSurf, ez);
    }

    // --- Light sources: SMALL dynamic point lights (no glowing voxels). Lit
    // rooms get wall brackets + a central floor post; corridors and the stair
    // get wall brackets; the two landmark towers carry beacons. ~30% of plain
    // halls are left dark. The stamp drops a tiny wood fixture at each. --------
    auto addLight = [&](int x, int yy, int z, uint8_t kind) {
        lights.push_back({ glm::vec3((float)x + 0.5f, (float)yy + 0.5f, (float)z + 0.5f), kind });
    };
    for (const DungeonRoom& rm : rooms) {
        uint32_t rh = (uint32_t)(rm.mn.x * 73856093) ^ (uint32_t)(rm.mn.z * 19349663);
        bool dark = (rm.purpose == 0 || rm.purpose == 7) && (rh % 100u) < 30u;
        if (dark) continue;
        int ly = rm.mn.y + 2, rw = rm.mx.x - rm.mn.x, rd = rm.mx.z - rm.mn.z;
        for (int x = rm.mn.x + 2; x <= rm.mx.x - 2; x += 6) {
            if (dungeonRoomContains(rm, x, rm.mn.z)) addLight(x, ly, rm.mn.z, 0);
            if (dungeonRoomContains(rm, x, rm.mx.z)) addLight(x, ly, rm.mx.z, 0);
        }
        for (int z = rm.mn.z + 5; z <= rm.mx.z - 2; z += 6) {
            if (dungeonRoomContains(rm, rm.mn.x, z)) addLight(rm.mn.x, ly, z, 0);
            if (dungeonRoomContains(rm, rm.mx.x, z)) addLight(rm.mx.x, ly, z, 0);
        }
        if (rw >= 12 && rd >= 12) addLight((rm.mn.x + rm.mx.x) / 2, rm.mn.y + 2, (rm.mn.z + rm.mx.z) / 2, 1);
    }
    for (const DungeonCorridor& co : corridors) {
        int ax = co.a.x, az = co.a.z, bx = co.b.x, bz = co.b.z, ly = floorY + 2;
        for (int x = std::min(ax, bx); x <= std::max(ax, bx); x += 7) { addLight(x, ly, az - 2, 0); addLight(x, ly, az + 2, 0); }
        for (int z = std::min(az, bz); z <= std::max(az, bz); z += 7) { addLight(bx - 2, ly, z, 0); addLight(bx + 2, ly, z, 0); }
    }
    {
        int depth = entrance.x - entranceInner.x, ez = entrance.z;
        for (int i = 0; i <= depth; i += 4) {
            int sx = entrance.x - i, sy = entrance.y - i, shw = std::min(4, 2 + i / 3);
            addLight(sx, sy + 2, ez - shw, 0); addLight(sx, sy + 2, ez + shw, 0);
        }
    }
    addLight(entrance.x, entrance.y + 2, entrance.z - 2, 0);
    addLight(entrance.x, entrance.y + 2, entrance.z + 2, 0);
    glm::ivec2 tc[2]; dungeonTowerCenters(*this, tc);
    for (int i = 0; i < 2; i++) addLight(tc[i].x, sampleSurfaceSolid(tc[i].x, tc[i].y) + DUNGEON_TOWER_H, tc[i].y, 2);

    // World-XZ bounding box (inclusive) + a wide margin for the grand stair/gate
    // and the two landmark towers.
    int lox = 1 << 30, loz = 1 << 30, hix = -(1 << 30), hiz = -(1 << 30);
    auto ext = [&](int x, int z) {
        lox = std::min(lox, x); hix = std::max(hix, x);
        loz = std::min(loz, z); hiz = std::max(hiz, z);
    };
    for (const DungeonRoom& rm : rooms) { ext(rm.mn.x, rm.mn.z); ext(rm.mx.x, rm.mx.z); }
    ext(entrance.x, entrance.z); ext(entranceInner.x, entranceInner.z);
    ext(entrance.x + 13, entrance.z + 11); ext(entrance.x - 13, entrance.z - 11);  // gatehouse footprint
    for (int i = 0; i < 2; i++) { ext(tc[i].x - DUNGEON_TOWER_R - 1, tc[i].y - DUNGEON_TOWER_R - 1);
                                  ext(tc[i].x + DUNGEON_TOWER_R + 1, tc[i].y + DUNGEON_TOWER_R + 1); }
    bbMin = glm::ivec2(lox - 3, loz - 3);
    bbMax = glm::ivec2(hix + 3, hiz + 3);
}

void Dungeon::rosterFill(std::vector<DungeonSpawn>& out, uint32_t seed,
                         uint8_t minionType, uint8_t bossType, int perRoom,
                         uint8_t minionType2, uint8_t minionType3,
                         uint8_t minionType4) const {
    std::mt19937 r(seed ^ 0x00D0A6E0u);
    // Minions are drawn from the primary type (≈half) plus any secondaries, so
    // chambers read as a believable mix of species rather than a clone army.
    uint8_t pool[4]; int poolN = 0;
    pool[poolN++] = minionType;
    if (minionType2 != 255) pool[poolN++] = minionType2;
    if (minionType3 != 255) pool[poolN++] = minionType3;
    if (minionType4 != 255) pool[poolN++] = minionType4;
    auto minionPick = [&]() -> uint8_t {
        if (poolN == 1 || (r() % 100u) < 50u) return pool[0];
        return pool[1 + (int)(r() % (uint32_t)(poolN - 1))];
    };
    for (const DungeonRoom& rm : rooms) {
        int ccx = (rm.mn.x + rm.mx.x) / 2, ccz = (rm.mn.z + rm.mx.z) / 2;
        // Spawn within the central ~50% of the room so enemies land inside the
        // carved silhouette even for circular / octagonal / cross-shaped rooms.
        int hx = std::max(1, (rm.mx.x - rm.mn.x) / 4), hz = std::max(1, (rm.mx.z - rm.mn.z) / 4);
        auto spawnIn = [&](uint8_t t) {
            int sx = ccx + (int)(r() % (uint32_t)(2 * hx + 1)) - hx;
            int sz = ccz + (int)(r() % (uint32_t)(2 * hz + 1)) - hz;
            out.push_back({ glm::ivec3(sx, rm.mn.y, sz), t });
        };
        int n = perRoom;
        if (rm.purpose == 1) n = std::max(0, perRoom - 1);   // lighter guard near the entrance
        if (rm.purpose == 2) {           // boss room — a boss plus a couple of minions
            out.push_back({ glm::ivec3(ccx, rm.mn.y, ccz), bossType, true });   // the main boss
            n = std::max(1, perRoom - 1);
        }
        for (int k = 0; k < n; k++) spawnIn(minionPick());
        // A tougher "champion" (boss=false → a hard elite, not THE boss) stalks
        // some chambers: throne rooms always, other non-entrance rooms ~25%.
        if (rm.purpose != 1 && rm.purpose != 2 &&
            (rm.purpose == 3 || (r() % 100u) < 25u))
            spawnIn(bossType);
    }
}

// --- Concrete dungeon types --------------------------------------------------

// Stone crypt: spread rectangular rooms, skeletons + a brute warden.
class CryptDungeon : public Dungeon {
public:
    DungeonKind kind() const override { return DungeonKind::Crypt; }
    void generateLayout(uint32_t seed, glm::ivec2 a, int surf) override {
        buildLayout(seed, a, surf, 8 + (int)(seed % 5u), 16, 30, false);
    }
    BlockType wallBlock()  const override { return BlockType::Stone; }
    BlockType floorBlock() const override { return BlockType::Stone; }
    void fillSpawnTable(std::vector<DungeonSpawn>& out, uint32_t seed) const override {
        // Skeletons, shambling zombies, drifting wraiths + scuttling ghouls, raised
        // and ruled by a lich.
        rosterFill(out, seed, (uint8_t)NPCType::Skeleton, (uint8_t)NPCType::Lich, 2,
                   (uint8_t)NPCType::Zombie, (uint8_t)NPCType::Wraith, (uint8_t)NPCType::Ghoul);
    }
};

// Natural cave: a winding chain of overlapping chambers, bandit den + brute.
class CaveDungeon : public Dungeon {
public:
    DungeonKind kind() const override { return DungeonKind::Cave; }
    void generateLayout(uint32_t seed, glm::ivec2 a, int surf) override {
        buildLayout(seed, a, surf, 14 + (int)(seed % 8u), 11, 20, true);
    }
    BlockType wallBlock()  const override { return BlockType::Stone; }
    BlockType floorBlock() const override { return BlockType::Gravel; }
    void fillSpawnTable(std::vector<DungeonSpawn>& out, uint32_t seed) const override {
        // A bandit den of cutthroats + brigands, with ghouls and the odd skeleton
        // from the deep dark.
        rosterFill(out, seed, (uint8_t)NPCType::Enemy, (uint8_t)NPCType::Brute, 2,
                   (uint8_t)NPCType::Brigand, (uint8_t)NPCType::Ghoul, (uint8_t)NPCType::Skeleton);
    }
};

// Sunken ruins: shallow sandstone rooms, cultists + a skeletal champion.
class RuinsDungeon : public Dungeon {
public:
    DungeonKind kind() const override { return DungeonKind::Ruins; }
    void generateLayout(uint32_t seed, glm::ivec2 a, int surf) override {
        buildLayout(seed, a, surf, 8 + (int)(seed % 5u), 14, 26, false);
    }
    BlockType wallBlock()  const override { return BlockType::Sandstone; }
    BlockType floorBlock() const override { return BlockType::Sandstone; }
    void fillSpawnTable(std::vector<DungeonSpawn>& out, uint32_t seed) const override {
        // Cultists + ghouls + conjured fire elementals + drifting wraiths, ruled by
        // a necromancer.
        rosterFill(out, seed, (uint8_t)NPCType::Cultist, (uint8_t)NPCType::Necromancer, 2,
                   (uint8_t)NPCType::Ghoul, (uint8_t)NPCType::FireElemental, (uint8_t)NPCType::Wraith);
    }
};

// The overground CastleDungeon now lives in castle.cpp (it is large enough to
// warrant its own translation unit) — see include/castle.h.

std::unique_ptr<Dungeon> makeDungeon(DungeonKind kind) {
    switch (kind) {
        case DungeonKind::Crypt:  return std::make_unique<CryptDungeon>();
        case DungeonKind::Cave:   return std::make_unique<CaveDungeon>();
        case DungeonKind::Ruins:  return std::make_unique<RuinsDungeon>();
        case DungeonKind::Castle: return std::make_unique<CastleDungeon>();
        default:                  return std::make_unique<CryptDungeon>();
    }
}

// --- Deterministic plan ------------------------------------------------------

static uint32_t dungHash(uint32_t s, uint32_t gx, uint32_t gz) {
    uint32_t h = s * 0x9E3779B1u ^ (gx * 0x85EBCA77u) ^ (gz * 0xC2B2AE3Du);
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

// A deterministic fantasy name for a dungeon, themed by its kind (e.g.
// "Crypt of Morgaroth", "Velmire Caverns", "Ruins of Skarrok").
static std::string makeDungeonName(uint32_t seed, DungeonKind kind) {
    static const char* A[] = { "Mor","Vel","Thar","Kor","Dra","Gron","Bal","Nyx","Skar",
                               "Ulth","Zar","Fen","Grim","Hel","Vor","Mal","Aza","Kael" };
    static const char* B[] = { "gar","mire","loth","wen","rok","val","nar","gul","dred",
                               "mor","heim","dath","keth","run","goth","vash" };
    static const char* C[] = { "oth","ar","is","une","ek","orn","ash","iel","ux","om","","","" };
    uint32_t h = seed ? seed : 1u;
    auto nx = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h; };
    std::string nm = A[nx() % (sizeof(A) / sizeof(A[0]))];
    nm += B[nx() % (sizeof(B) / sizeof(B[0]))];
    nm += C[nx() % (sizeof(C) / sizeof(C[0]))];
    switch (kind) {
        case DungeonKind::Crypt:
            switch (nx() % 3u) { case 0: return "Crypt of " + nm; case 1: return nm + " Catacombs"; default: return "Tomb of " + nm; }
        case DungeonKind::Cave:
            switch (nx() % 3u) { case 0: return nm + " Caverns"; case 1: return "Caves of " + nm; default: return nm + " Hollow"; }
        case DungeonKind::Castle:
            switch (nx() % 3u) { case 0: return "Castle " + nm; case 1: return nm + " Keep"; default: return "Fortress of " + nm; }
        default: // Ruins
            switch (nx() % 3u) { case 0: return "Ruins of " + nm; case 1: return "Lost " + nm; default: return nm + " Ruins"; }
    }
}

static DungeonPlan buildDungeonPlan() {
    DungeonPlan plan;
    const TownPlan& towns = getTownPlan();   // built first; needed for clearance
    const int CELL  = 1024;                   // one candidate per ~1km cell
    const int REACH = 30000;                  // matches the settled belt half-extent
    const int G     = REACH / CELL;
    for (int gz = -G; gz <= G; gz++)
        for (int gx = -G; gx <= G; gx++) {
            uint32_t h = dungHash(worldSeed(), (uint32_t)gx, (uint32_t)gz);
            if ((h % 100u) >= 9u) continue;                       // ~9% of cells hold a dungeon
            int ax = gx * CELL + 64 + (int)((h >> 3)  % (uint32_t)(CELL - 128));
            int az = gz * CELL + 64 + (int)((h >> 13) % (uint32_t)(CELL - 128));
            int surf = sampleSurfaceSolid(ax, az);
            if (surf < WORLD_SEA_LEVEL + 3) continue;            // dry land only
            bool nearTown = false;
            for (const Town& t : towns.towns) {
                long long dx = ax - t.center.x, dz = az - t.center.y;
                long long md = (long long)t.radius + 120;        // well clear of any town
                if (dx * dx + dz * dz < md * md) { nearTown = true; break; }
            }
            if (nearTown) continue;
            uint32_t kr = (h >> 20) % 100u;
            DungeonKind kind = (kr < 28) ? DungeonKind::Castle    // ~28% are overground castles (easy to spot)
                                         : (DungeonKind)(kr % 3u); // else Crypt / Cave / Ruins
            uint32_t sr = (h >> 12) % 100u;
            int tier = (sr < 25) ? 0 : (sr < 70) ? 1 : (sr < 92) ? 2 : 3;   // 25/45/22/8 — massive is rare
            // Reject sites whose ground is too uneven across the footprint: an
            // underground complex would poke out of a hillside (or need an absurd
            // descent) and a castle would straddle a cliff. Sample the central
            // footprint (capped) so even big dungeons can find flat-enough land.
            int checkHalf = (kind == DungeonKind::Castle) ? (16 + tier * 6) : dungeonHalf(tier);
            if (surf - footprintMinSurface(glm::ivec2(ax, az), std::min(checkHalf, 80)) > 40) continue;
            // A castle sits ON the ground, so also reject sites where the land
            // rises far above the anchor — its keep would be deeply buried. (The
            // entrance itself is always cut accessible, so this is only to avoid
            // the worst cases, not the primary fix.)
            if (kind == DungeonKind::Castle &&
                footprintMaxSurface(glm::ivec2(ax, az), checkHalf) - surf > 16) continue;
            auto d = makeDungeon(kind);
            d->sizeTier = tier;
            d->generateLayout(h ^ 0x6E756Eu, glm::ivec2(ax, az), surf);
            d->name = makeDungeonName(h ^ 0x4E414Du, kind);
            d->entranceStyle = (uint8_t)((h >> 25) % 4u);
            plan.dungeons.push_back(std::move(d));
        }
    std::string sample = plan.dungeons.empty() ? "" : (" e.g. \"" + plan.dungeons.front()->name + "\"");
    std::cout << "[Dungeons] Placed " << plan.dungeons.size() << " dungeons." << sample << std::endl;
    return plan;
}

const DungeonPlan& getDungeonPlan() {
    static DungeonPlan           plan;
    static std::mutex            mtx;
    static std::atomic<uint64_t> built{~0ull};
    rebuildCacheOnSeedChange(built, mtx, [] { plan = buildDungeonPlan(); });
    return plan;
}

// --- Chunk stamping ----------------------------------------------------------

void stampDungeonChunk(Chunk* c) {
    const DungeonPlan& plan = getDungeonPlan();
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;

    auto setW = [&](int wx, int wy, int wz, BlockType b) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return;
        if (wx < ox || wx >= ox + CHUNK_SIZE || wz < oz || wz >= oz + CHUNK_SIZE) return;
        c->set(wx - ox, wy, wz - oz, b);
    };
    auto fillBox = [&](int x0, int x1, int y0, int y1, int z0, int z1, BlockType b) {
        int lx = std::max(x0, ox), hx = std::min(x1, ox + CHUNK_SIZE - 1);
        int lz = std::max(z0, oz), hz = std::min(z1, oz + CHUNK_SIZE - 1);
        int ly = std::max(0, y0),  hy = std::min(CHUNK_HEIGHT - 1, y1);
        for (int wx = lx; wx <= hx; wx++)
            for (int wz = lz; wz <= hz; wz++)
                for (int wy = ly; wy <= hy; wy++)
                    c->set(wx - ox, wy, wz - oz, b);
    };

    for (const auto& dptr : plan.dungeons) {
        const Dungeon& d = *dptr;
        if (d.bbMax.x < ox - 1 || d.bbMin.x > ox + CHUNK_SIZE ||
            d.bbMax.y < oz - 1 || d.bbMin.y > oz + CHUNK_SIZE) continue;

        const BlockType wall = d.wallBlock(), floor = d.floorBlock();
        const BlockType wood     = BlockType::Wood;
        const BlockType accent   = wall;             // entrance/castle caps — NON-glowing (lights are dynamic)
        const BlockType bone     = (BlockType)((int)BlockType::PaintFirst + 1);    //  1 Cream — skulls/bones
        const int y = d.floorY;

        // Overground castles are far more elaborate (graded foundations, a
        // gatehouse + arch, partitioned interiors, towers) — built in castle.cpp.
        if (d.overground) { stampCastleChunk(c, d); continue; }

        // --- lights & props. Dungeons place NO glowing voxels: each light in
        // d.lights becomes a tiny non-glowing WOOD fixture here, and the renderer
        // streams the nearby ones into the dynamic point-light system (small,
        // soft, flickering, occluded by walls). ------------------------------
        auto placeLightFixture = [&](const DungeonLight& L) {
            int lx = (int)std::floor(L.pos.x), ly = (int)std::floor(L.pos.y), lz = (int)std::floor(L.pos.z);
            if (L.kind == 1) { setW(lx, ly - 2, lz, wood); setW(lx, ly - 1, lz, wood); }   // floor post (light at top)
            else if (L.kind == 0) setW(lx, ly, lz, wood);                                   // small wall bracket
            // kind 2 (beacon): the tower / castle structure carries its own mount
        };
        // A tall round landmark tower on the surface, flanking the entrance so the
        // dungeon is easy to spot from a distance.
        auto buildTower = [&](int tcx, int tcz) {
            int base = sampleSurfaceSolid(tcx, tcz), R = DUNGEON_TOWER_R, H = DUNGEON_TOWER_H;
            fillBox(tcx - R, tcx + R, base - 6, base - 1, tcz - R, tcz + R, wall);    // footing
            for (int yy = base; yy <= base + H; yy++)
                for (int dx = -R; dx <= R; dx++) for (int dz = -R; dz <= R; dz++) {
                    int r2 = dx*dx + dz*dz;
                    if (r2 <= R*R && r2 > (R-2)*(R-2)) setW(tcx + dx, yy, tcz + dz, wall);    // 2-thick ring wall
                }
            for (int dx = -R; dx <= R; dx++) for (int dz = -R; dz <= R; dz++) {       // crenellations
                int r2 = dx*dx + dz*dz;
                if (r2 <= R*R && r2 > (R-2)*(R-2) && (((dx + dz) & 1) == 0)) setW(tcx + dx, base + H + 1, tcz + dz, wall);
            }
            setW(tcx, base + H, tcz, wood);    // beacon mount (the dynamic beacon light sits here)
        };
        // Furniture is now placed as detailed Prop objects (the same system houses
        // use — see furnishDungeonRoom in prop_placement.cpp). The stamp only lays
        // the ARCHITECTURE: thrones, the ornament monument, prison cages, banners.
        auto column = [&](int x, int z, int y0, int h, BlockType b) { for (int k = 0; k < h; k++) setW(x, y0 + k, z, b); };
        auto cage = [&](int x, int z, int fY) {            // a 3x3 barred cell holding a captive's bones
            for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++)
                if ((dx == 0) != (dz == 0)) column(x + dx, z + dz, fY, 3, wood);   // bars on the 4 sides
            setW(x, fY, z, bone);
        };
        auto paintB = [](int i) { return (BlockType)((int)BlockType::PaintFirst + i); };
        auto banner = [&](int x, int yTop, int z, int colorIdx) {   // a hanging wall cloth (3 tall)
            setW(x, yTop, z, wood);
            for (int k = 1; k <= 3; k++) setW(x, yTop - k, z, paintB(colorIdx));
        };
        auto spike = [&](int x, int z, int baseY, int h, int dir) { // stalagmite (+1) / stalactite (-1)
            for (int k = 0; k < h; k++) setW(x, baseY + dir * k, z, wall);
        };
        auto niche = [&](int wx, int wy, int wz) {                  // a bone alcove recessed into a wall
            setW(wx, wy, wz, bone); setW(wx, wy + 1, wz, BlockType::Air);
        };

        // Room shape silhouette test (shared with the layout via dungeonRoomContains).
        auto inRoom = [](const DungeonRoom& rm, int x, int z) { return dungeonRoomContains(rm, x, z); };

        // Place the small non-glowing wood fixtures for every light in this
        // dungeon (clipped to the chunk). Runs for castles and carved dungeons.
        for (const DungeonLight& L : d.lights) placeLightFixture(L);

        // Rooms: carve the shape silhouette (floor slab + hollow), light it with
        // a coverage grid of hung lanterns (some plain halls left deliberately
        // dark), then dress it by purpose.
        for (const DungeonRoom& rm : d.rooms) {
            int fY = rm.mn.y, ceilY = rm.mx.y, rhgt = ceilY - fY;
            int cx = (rm.mn.x + rm.mx.x) / 2, cz = (rm.mn.z + rm.mx.z) / 2;
            int rw = rm.mx.x - rm.mn.x, rd = rm.mx.z - rm.mn.z;

            // carve only the part of the silhouette in this chunk
            int lxx = std::max(rm.mn.x, ox), hxx = std::min(rm.mx.x, ox + CHUNK_SIZE - 1);
            int lzz = std::max(rm.mn.z, oz), hzz = std::min(rm.mx.z, oz + CHUNK_SIZE - 1);
            for (int x = lxx; x <= hxx; x++)
                for (int z = lzz; z <= hzz; z++)
                    if (inRoom(rm, x, z)) {
                        setW(x, fY - 1, z, floor);
                        for (int yy = fY; yy <= ceilY; yy++) setW(x, yy, z, BlockType::Air);
                    }

            // --- Environmental detail (deterministic per room): grand colonnades,
            // per-kind flavour, and the occasional water pool / chasm. ----------
            uint32_t rh = (uint32_t)(rm.mn.x * 73856093) ^ (uint32_t)(rm.mn.z * 19349663);
            DungeonKind kind = d.kind();

            // Rows of full-height columns frame big rooms and the boss hall (the
            // centre / boss aisle is kept open).
            if ((rw >= 12 && rd >= 12) || rm.purpose == 2) {
                for (int x = rm.mn.x + 3; x <= rm.mx.x - 3; x += 6)
                    for (int z = rm.mn.z + 3; z <= rm.mx.z - 3; z += 6) {
                        if (std::abs(x - cx) <= 1 && std::abs(z - cz) <= 1) continue;          // open centre
                        if (rm.purpose == 2 && std::abs(z - cz) <= 1) continue;                 // boss aisle
                        if (dungeonRoomContains(rm, x, z)) column(x, z, fY, rhgt + 1, wall);
                    }
            }

            if (kind == DungeonKind::Cave) {                       // stalagmites, stalactites, moss
                for (int x = rm.mn.x + 1; x <= rm.mx.x - 1; x += 3)
                    for (int z = rm.mn.z + 1; z <= rm.mx.z - 1; z += 3) {
                        if (!dungeonRoomContains(rm, x, z)) continue;
                        uint32_t h = (uint32_t)(x * 374761393) ^ (uint32_t)(z * 668265263);
                        if      ((h & 7u) == 0u) spike(x, z, fY,    1 + (int)(h % 3u), +1);
                        else if ((h & 7u) == 1u) spike(x, z, ceilY, 1 + (int)((h >> 4) % 3u), -1);
                        else if ((h & 15u) == 4u) setW(x, fY - 1, z, BlockType::Leaves);        // moss
                    }
            } else if (kind == DungeonKind::Crypt) {              // catacomb bone niches in the walls
                for (int z = rm.mn.z + 2; z <= rm.mx.z - 2; z += 3) {
                    if (dungeonRoomContains(rm, rm.mn.x, z) && !dungeonRoomContains(rm, rm.mn.x - 1, z))
                        niche(rm.mn.x - 1, fY + 1, z);
                    if (dungeonRoomContains(rm, rm.mx.x, z) && !dungeonRoomContains(rm, rm.mx.x + 1, z))
                        niche(rm.mx.x + 1, fY + 1, z);
                }
            } else if (kind == DungeonKind::Ruins) {             // toppled columns + rubble
                for (int x = rm.mn.x + 2; x <= rm.mx.x - 2; x += 4)
                    for (int z = rm.mn.z + 2; z <= rm.mx.z - 2; z += 4) {
                        if (!dungeonRoomContains(rm, x, z)) continue;
                        uint32_t h = (uint32_t)(x * 9176 + z * 4129);
                        if      ((h & 3u) == 0u) column(x, z, fY, 1 + (int)(h % (uint32_t)std::max(2, rhgt - 1)), wall);
                        else if ((h & 7u) == 1u) setW(x, fY, z, BlockType::Gravel);
                    }
            }

            // Water: a chasm + bridge in a few large halls; a shallow pool in some.
            if (rm.purpose == 0 && rw >= 10 && rd >= 10 && (rh % 100u) < 16u) {
                for (int x = rm.mn.x + 2; x <= rm.mx.x - 2; x++)            // 2-deep pit, bridge along z=cz
                    for (int z = rm.mn.z + 2; z <= rm.mx.z - 2; z++) {
                        if (!dungeonRoomContains(rm, x, z) || std::abs(z - cz) <= 1) continue;
                        setW(x, fY - 1, z, BlockType::Water);
                        setW(x, fY - 2, z, BlockType::Water);
                    }
            } else if ((rm.purpose == 0 || rm.purpose == 1 || rm.purpose == 4) && (rh % 100u) >= 82u) {
                int pr = std::min(3, std::min(rw, rd) / 3);                  // shallow ornamental pool (wade)
                for (int dx = -pr; dx <= pr; dx++) for (int dz = -pr; dz <= pr; dz++) {
                    if (dx * dx + dz * dz > pr * pr || !dungeonRoomContains(rm, cx + dx, cz + dz)) continue;
                    setW(cx + dx, fY - 1, cz + dz, BlockType::Water);
                }
            }

            // ARCHITECTURE only. All furniture (tables, chairs, shelves, barrels,
            // cookers, cauldrons, rugs, …) is placed as detailed Prop objects by
            // furnishDungeonRoom() — the same system houses use. Only structural
            // pieces stay block-built here.
            switch (rm.purpose) {
            case 2: {   // BOSS hall — a great throne at the back, skull pikes, banners
                int tbx = rm.mn.x + 2;
                column(tbx, cz, fY, 3, wall);
                setW(tbx, fY + 1, cz - 1, wall); setW(tbx, fY + 1, cz + 1, wall);
                setW(tbx + 1, fY, cz, wall);
                banner(rm.mn.x + 1, fY + 4, cz - 2, 8); banner(rm.mn.x + 1, fY + 4, cz + 2, 8);
                for (int dzp : { -3, 3 }) { setW(cx, fY, cz + dzp, wood); setW(cx, fY + 1, cz + dzp, bone); }
                break;
            }
            case 3: {   // THRONE room — a throne and a pair of banners
                int tbx = rm.mn.x + 2;
                column(tbx, cz, fY, 2, wall);
                setW(tbx, fY + 1, cz - 1, wall); setW(tbx, fY + 1, cz + 1, wall);
                banner(rm.mn.x + 1, fY + 4, cz - 2, 22); banner(rm.mn.x + 1, fY + 4, cz + 2, 22);
                break;
            }
            case 5: {   // ORNAMENT — a central stepped monument (a beacon light crowns it)
                for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) if (inRoom(rm, cx + dx, cz + dz)) setW(cx + dx, fY, cz + dz, wall);
                for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) setW(cx + dx, fY + 1, cz + dz, wall);
                column(cx, cz, fY + 1, rhgt - 1, wall);
                setW(cx, ceilY, cz, wood);   // beacon mount at the crown
                break;
            }
            case 7: {   // PRISON — barred cells along the back wall
                for (int z = rm.mn.z + 2; z <= rm.mx.z - 2; z += 4)
                    if (inRoom(rm, rm.mn.x + 2, z)) cage(rm.mn.x + 2, z, fY);
                break;
            }
            default: break;   // entrance / library / vault / hall — furnished by Props
            }
        }
        // Corridors: an L (along X, then Z), 5 wide and 5 tall, floor beneath.
        // (Lit by the dynamic wall-bracket lights placed above.)
        const int CW = 2;            // corridor half-width (5 wide)
        const int CH = 4;            // corridor headroom (5 tall: y .. y+4)
        for (const DungeonCorridor& co : d.corridors) {
            int ax = co.a.x, az = co.a.z, bx = co.b.x, bz = co.b.z;
            for (int x = std::min(ax, bx); x <= std::max(ax, bx); x++) {
                fillBox(x, x, y - 1, y - 1, az - CW, az + CW, floor);
                fillBox(x, x, y,     y + CH, az - CW, az + CW, BlockType::Air);
            }
            for (int z = std::min(az, bz); z <= std::max(az, bz); z++) {
                fillBox(bx - CW, bx + CW, y - 1, y - 1, z, z, floor);
                fillBox(bx - CW, bx + CW, y,     y + CH, z, z, BlockType::Air);
            }
        }
        // Grand descent: a long vaulted stair from the surface mouth down to the
        // entrance room. It starts as a 5-wide doorway and opens out to a 9-wide,
        // 8-tall hall as it descends, lit by wall + centre lanterns.
        const int SHW    = 2;        // surface-mouth half-width (also used by the entrance styles)
        const int STAIRW = 4;        // grand stair half-width deep down (9 wide)
        const int SHEAD  = 7;        // headroom above each tread (8 tall)
        int depth = d.entrance.x - d.entranceInner.x;
        int ez    = d.entrance.z;
        for (int i = 0; i <= depth; i++) {
            int sx = d.entrance.x - i, sy = d.entrance.y - i;
            int shw = std::min(STAIRW, SHW + i / 3);                       // widen as it goes down
            fillBox(sx, sx, sy - 1, sy - 1,     ez - shw, ez + shw, floor);
            fillBox(sx, sx, sy,     sy + SHEAD, ez - shw, ez + shw, BlockType::Air);
        }
        // Above-ground entrance — one of several styles, chosen per dungeon for
        // variety. The stair descends -X, so each style opens toward +X.
        auto pillar = [&](int px, int pz, int y0, int hgt, BlockType b) {
            for (int yy = 0; yy < hgt; yy++) setW(px, y0 + yy, pz, b);
        };
        int ex = d.entrance.x, sy = d.entrance.y;
        switch (d.entranceStyle) {
        default:
        case 0: {   // Battlemented gatehouse keep
            int gx0 = ex - 7, gx1 = ex + 5, gz0 = ez - 6, gz1 = ez + 6, wallTop = sy + 5;
            fillBox(gx0 + 1, gx1 - 1, sy, wallTop + 3, gz0 + 1, gz1 - 1, BlockType::Air);
            for (int x = gx0; x <= gx1; x++)
                for (int z = gz0; z <= gz1; z++)
                    if (!(z >= ez - SHW && z <= ez + SHW && x <= ex)) setW(x, sy - 1, z, floor);
            for (int yy = sy; yy <= wallTop; yy++) {
                for (int x = gx0; x <= gx1; x++) { setW(x, yy, gz0, wall); setW(x, yy, gz1, wall); }
                for (int z = gz0; z <= gz1; z++) {
                    setW(gx0, yy, z, wall);
                    if (!((z >= ez - SHW && z <= ez + SHW) && yy <= sy + 5)) setW(gx1, yy, z, wall);   // tall gate mouth
                }
            }
            for (int x = gx0; x <= gx1; x += 2) { setW(x, wallTop + 1, gz0, wall); setW(x, wallTop + 1, gz1, wall); }
            for (int z = gz0; z <= gz1; z += 2) { setW(gx0, wallTop + 1, z, wall); setW(gx1, wallTop + 1, z, wall); }
            int cpx[2] = { gx0, gx1 }, cpz[2] = { gz0, gz1 };
            for (int a = 0; a < 2; a++) for (int b2 = 0; b2 < 2; b2++) {
                pillar(cpx[a], cpz[b2], sy, 8, wall); setW(cpx[a], sy + 8, cpz[b2], accent);
            }
            for (int dzp : { -2, 2 }) { pillar(gx1 + 1, ez + dzp, sy, 2, BlockType::Wood); setW(gx1 + 1, sy + 2, ez + dzp, accent); }
            for (int dzp : { -3, 3 }) { pillar(gx1 + 3, ez + dzp, sy, 2, BlockType::Wood); setW(gx1 + 3, sy + 2, ez + dzp, bone); }
            setW(gx1, sy + 2, ez - SHW, accent); setW(gx1, sy + 2, ez + SHW, accent);
            break;
        }
        case 1: {   // Ruined stone circle with a lintel arch over the mouth
            for (int dx = -5; dx <= 5; dx++) for (int dz = -5; dz <= 5; dz++)
                if (dx * dx + dz * dz <= 28 && !(dz >= -SHW && dz <= SHW && dx <= 0)) setW(ex + dx, sy - 1, ez + dz, floor);
            for (int i = 0; i < 8; i++) {
                float a  = (float)i / 8.0f * 6.2831853f;
                int   px = ex + (int)std::lround(std::cos(a) * 5.0f);
                int   pz = ez + (int)std::lround(std::sin(a) * 5.0f);
                int   ph = 3 + (int)(((uint32_t)(px * 9301 ^ pz * 49297)) % 3u);
                pillar(px, pz, sy, ph, wall);
                if (i % 2 == 0) setW(px, sy + ph, pz, accent);
            }
            pillar(ex + 2, ez - SHW, sy, 6, wall); pillar(ex + 2, ez + SHW, sy, 6, wall);         // lintel posts
            for (int dzl = -SHW; dzl <= SHW; dzl++) setW(ex + 2, sy + 6, ez + dzl, wall);         // lintel beam
            setW(ex + 2, sy + 5, ez - SHW, accent); setW(ex + 2, sy + 5, ez + SHW, accent);       // lintel torches
            for (int dzp : { -4, 4 }) { pillar(ex + 4, ez + dzp, sy, 2, BlockType::Wood); setW(ex + 4, sy + 2, ez + dzp, bone); }
            break;
        }
        case 2: {   // Natural cave mouth — a lumpy rocky rim with glowing crystals
            for (int dx = -4; dx <= 3; dx++) for (int dz = -4; dz <= 4; dz++) {
                if (dz >= -SHW && dz <= SHW && dx <= 0) continue;                                // keep the pit open
                int r2 = dx * dx + dz * dz;
                if (r2 < 4 || r2 > 24) continue;
                uint32_t hh = (uint32_t)((ex + dx) * 73856093 ^ (ez + dz) * 19349663);
                int rh = 1 + (int)(hh % 3u);
                pillar(ex + dx, ez + dz, sy, rh, wall);
                if ((hh & 7u) == 0u) setW(ex + dx, sy + rh, ez + dz, accent);                    // a glowing crystal
            }
            setW(ex + 3, sy, ez - 3, accent); setW(ex + 3, sy, ez + 3, accent);
            break;
        }
        case 3: {   // Obelisk shrine — a platform, a tall obelisk and corner braziers
            for (int dx = -4; dx <= 4; dx++) for (int dz = -4; dz <= 4; dz++)
                if (!(dz >= -SHW && dz <= SHW && dx <= 0)) setW(ex + dx, sy - 1, ez + dz, floor);
            pillar(ex - 3, ez, sy, 8, wall);                                                     // central obelisk
            pillar(ex - 3, ez - 1, sy, 5, wall); pillar(ex - 3, ez + 1, sy, 5, wall);
            setW(ex - 3, sy + 8, ez, accent);                                                    // glowing capstone
            for (int cxo : { -4, 4 }) for (int czo : { -4, 4 }) { pillar(ex + cxo, ez + czo, sy, 2, wall); setW(ex + cxo, sy + 2, ez + czo, accent); }
            for (int dzp : { -2, 2 }) { pillar(ex + 4, ez + dzp, sy, 2, BlockType::Wood); setW(ex + 4, sy + 2, ez + dzp, bone); }
            break;
        }
        }

        // Grand circular landmark towers flanking the mouth — visible from afar.
        glm::ivec2 tc[2]; dungeonTowerCenters(d, tc);
        buildTower(tc[0].x, tc[0].y);
        buildTower(tc[1].x, tc[1].y);
    }
}
