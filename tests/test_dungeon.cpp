// Headless tests for the procedural dungeon LAYOUT (geometry only — no town
// survey, no GL). We drive Dungeon::generateLayout directly and assert the
// structural invariants the generator promises: many distinct (non-overlapping)
// rooms, full connectivity via corridors, the entrance/boss purposes, and that
// the room-shape system is actually exercised.
#include "terrax_test.h"
#include "dungeon.h"
#include "castle.h"    // CastleDungeon layout check
#include "world.h"     // setWorldSeed, sampleSurfaceSolid
#include "npc.h"       // NPCType (spawn-table check)

#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

// Build one underground dungeon layout for inspection. Uses generateLayout
// directly so it never triggers the (heavy) town survey in buildDungeonPlan.
static std::unique_ptr<Dungeon> makeLayout(DungeonKind kind, int tier, uint32_t seed) {
    setWorldSeed(1234u);
    auto d = makeDungeon(kind);
    d->sizeTier = tier;
    glm::ivec2 anchor(5000, -5000);
    int surf = sampleSurfaceSolid(anchor.x, anchor.y);
    d->generateLayout(seed, anchor, surf);
    return d;
}

static int roomCenterX(const DungeonRoom& r) { return (r.mn.x + r.mx.x) / 2; }
static int roomCenterZ(const DungeonRoom& r) { return (r.mn.z + r.mx.z) / 2; }

TEST_CASE(Dungeon_HasManyDistinctRooms) {
    auto d = makeLayout(DungeonKind::Crypt, 2, 0xABCDu);
    CHECK(d->rooms.size() >= 6);
    // Every pair of room bounding boxes must be disjoint — rooms are distinct
    // chambers separated by rock, connected only by corridors.
    for (size_t i = 0; i < d->rooms.size(); i++)
        for (size_t j = i + 1; j < d->rooms.size(); j++) {
            const DungeonRoom& A = d->rooms[i];
            const DungeonRoom& B = d->rooms[j];
            bool overlapX = A.mn.x <= B.mx.x && B.mn.x <= A.mx.x;
            bool overlapZ = A.mn.z <= B.mx.z && B.mn.z <= A.mx.z;
            CHECK(!(overlapX && overlapZ));
        }
}

TEST_CASE(Dungeon_AllRoomsConnected) {
    auto d = makeLayout(DungeonKind::Ruins, 1, 0x1234u);
    int n = (int)d->rooms.size();
    CHECK(n >= 2);
    CHECK(d->corridors.size() >= (size_t)(n - 1));   // at least a spanning tree

    auto key = [](int x, int z) { return ((long long)x << 32) ^ (long long)(unsigned)z; };
    std::unordered_map<long long, int> centerToIdx;
    for (int i = 0; i < n; i++)
        centerToIdx[key(roomCenterX(d->rooms[i]), roomCenterZ(d->rooms[i]))] = i;

    std::vector<int> uf(n);
    for (int i = 0; i < n; i++) uf[i] = i;
    std::function<int(int)> find = [&](int a) { while (uf[a] != a) { uf[a] = uf[uf[a]]; a = uf[a]; } return a; };

    for (const DungeonCorridor& co : d->corridors) {
        auto ia = centerToIdx.find(key(co.a.x, co.a.z));
        auto ib = centerToIdx.find(key(co.b.x, co.b.z));
        CHECK(ia != centerToIdx.end());            // corridor endpoints are real room centres
        CHECK(ib != centerToIdx.end());
        uf[find(ia->second)] = find(ib->second);
    }
    int root = find(0);
    for (int i = 1; i < n; i++) CHECK(find(i) == root);   // one connected component
}

TEST_CASE(Dungeon_HasExactlyOneEntranceAndBoss) {
    auto d = makeLayout(DungeonKind::Crypt, 2, 0x55u);
    int ent = 0, boss = 0;
    for (const DungeonRoom& r : d->rooms) {
        if (r.purpose == 1) ent++;
        if (r.purpose == 2) boss++;
    }
    CHECK_EQ(ent, 1);
    CHECK_EQ(boss, 1);
    CHECK(d->rooms.front().purpose == 1);   // the entrance room sits on the anchor
}

TEST_CASE(Dungeon_RoomShapesAreExercised) {
    auto d = makeLayout(DungeonKind::Cave, 2, 0x99u);
    CHECK(d->rooms.front().shape == RoomShape::Circle);   // caves carve blobby chambers
    for (const DungeonRoom& r : d->rooms)
        if (r.purpose == 2) CHECK(r.shape == RoomShape::Rect);   // the boss hall is a proper rectangle
}

TEST_CASE(Dungeon_BiggerTierHasMoreRooms) {
    auto small = makeLayout(DungeonKind::Crypt, 0, 0x7777u);
    auto big   = makeLayout(DungeonKind::Crypt, 3, 0x7777u);
    CHECK(big->rooms.size() > small->rooms.size());
}

TEST_CASE(Dungeon_HasDynamicLightsAndTwoTowerBeacons) {
    auto d = makeLayout(DungeonKind::Crypt, 2, 0x2468u);
    CHECK(!d->lights.empty());                       // lit by dynamic point lights, not voxels
    int beacons = 0;
    for (const DungeonLight& L : d->lights) if (L.kind == 2) beacons++;
    CHECK(beacons >= 2);                             // the two grand landmark towers
}

TEST_CASE(Castle_HasPerFloorRoomsBossAndProjectingEntrance) {
    setWorldSeed(1234u);
    auto d = makeDungeon(DungeonKind::Castle);
    d->sizeTier = 1;                                   // levels = clamp(3+1,3,6) = 4
    d->generateLayout(0xC0FFu, glm::ivec2(7000, 3000), 80);
    CHECK(d->overground);
    CHECK(d->rooms.size() >= 5);                       // 2 wings per lower floor + a boss hall
    int boss = 0;
    for (const DungeonRoom& r : d->rooms) if (r.purpose == 2) boss++;
    CHECK_EQ(boss, 1);
    CHECK(!d->lights.empty());
    CHECK(d->entrance.x > d->entranceInner.x);         // the gatehouse projects past the keep wall
    CHECK(d->bbMax.x > d->entrance.x);                 // bbox covers the descending entrance stair
}

TEST_CASE(Castle_StampHasStairHoleAndSolidFloors) {
    setWorldSeed(99u);
    auto d = makeDungeon(DungeonKind::Castle);
    d->sizeTier = 0;                                   // levels = 3, keep half = 12
    glm::ivec2 a(2000, 2000);
    int surf = sampleSurfaceSolid(a.x, a.y);
    d->generateLayout(0x1u, a, surf);

    // Read a world cell by stamping the castle into the chunk that owns it.
    auto fdiv = [](int v, int s) { return v >= 0 ? v / s : -((-v + s - 1) / s); };
    auto blockAt = [&](int wx, int wy, int wz) {
        int cx = fdiv(wx, CHUNK_SIZE), cz = fdiv(wz, CHUNK_SIZE);
        Chunk c(ChunkPos{cx, cz}, /*isServer=*/true);
        stampCastleChunk(&c, *d);
        return c.get(wx - cx * CHUNK_SIZE, wy, wz - cz * CHUNK_SIZE);
    };

    const int half = 12, KX0 = a.x - half, KX1 = a.x + half, KZ0 = a.y - half;
    const int baseY = surf, fh = d->floorH;
    const int stX0 = KX0 + 3;
    const int fs1  = baseY - 1 + fh;                   // the first upper floor's level
    // Over the stair lane, the upper floor is OPEN (you can climb up).
    CHECK(blockAt(stX0 + 1, fs1, KZ0 + 2) == BlockType::Air);
    // The room area of that floor is a solid floor you can stand on.
    CHECK(blockAt(KX0 + 6, fs1, KZ0 + 7) != BlockType::Air);

    // The entrance approach is a carved graded path (solid tread + clear above),
    // so the gate is reachable rather than buried in the ground. (GATE_DEPTH = 5.)
    const int ez = a.y, gX1 = KX1 + 5;
    int terrC1 = sampleSurfaceSolid(gX1 + 1, ez);
    int pf = baseY - 1; if (pf < terrC1) pf++; else if (pf > terrC1) pf--;
    CHECK(blockAt(gX1 + 1, pf,     ez) != BlockType::Air);   // a solid tread to walk on
    CHECK(blockAt(gX1 + 1, pf + 1, ez) == BlockType::Air);   // and headroom cut above it
}

TEST_CASE(Dungeon_SpawnTableHasOneBossAndChampions) {
    auto d = makeLayout(DungeonKind::Crypt, 2, 0x3030u);
    std::vector<DungeonSpawn> spawns;
    d->fillSpawnTable(spawns, 0x3030u);
    int bosses = 0, lords = 0;
    for (const DungeonSpawn& s : spawns) {
        if (s.boss) bosses++;
        if (s.npcType == (uint8_t)NPCType::Lich) lords++;   // the Crypt's boss type
    }
    CHECK_EQ(bosses, 1);     // exactly one true boss (legendary loot)
    CHECK(lords >= 2);       // the boss plus at least one elite "champion"
}
