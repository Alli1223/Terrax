// Headless tests for the procedural dungeon LAYOUT (geometry only — no town
// survey, no GL). We drive Dungeon::generateLayout directly and assert the
// structural invariants the generator promises: many distinct (non-overlapping)
// rooms, full connectivity via corridors, the entrance/boss purposes, and that
// the room-shape system is actually exercised.
#include "terrax_test.h"
#include "dungeon.h"
#include "world.h"     // setWorldSeed, sampleSurfaceSolid

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
