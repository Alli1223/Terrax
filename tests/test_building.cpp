#include "terrax_test.h"
#include "building.h"
#include <vector>
#include <cstdlib>

// Building generators (src/building.cpp). Each concrete Building emits a
// tight-cropped block grid + semantic rooms used by the furniture placer,
// driven deterministically by a seed. We assert the structural contract
// from building.h (grid sizing, cardinal door normal, in-bounds rooms,
// determinism) plus the material palette and the rotation helper.

// Common structural invariants every generated building must satisfy.
static void genAndCheck(Building& b, uint32_t seed,
                        std::vector<uint8_t>& blocks, std::vector<Room>& rooms,
                        int& dimX, int& dimY, int& dimZ) {
    int doorDX = 99, doorDZ = 99;
    b.generate(seed, blocks, rooms, dimX, dimY, dimZ, doorDX, doorDZ);

    CHECK(dimX > 0 && dimY > 0 && dimZ > 0);
    CHECK_EQ((int)blocks.size(), dimX * dimY * dimZ);

    // Door normal is a cardinal unit vector, or (0,0) for "no door".
    CHECK(std::abs(doorDX) <= 1 && std::abs(doorDZ) <= 1);
    CHECK(std::abs(doorDX) + std::abs(doorDZ) <= 1);

    // A building is not empty.
    bool any = false;
    for (uint8_t v : blocks) if (v != (uint8_t)BlockType::Air) { any = true; break; }
    CHECK(any);

    // Every room sits inside the (rotated) grid; spans are well-ordered.
    for (const Room& r : rooms) {
        CHECK(r.x0 >= 0 && r.x0 <= r.x1 && r.x1 < dimX);
        CHECK(r.z0 >= 0 && r.z0 <= r.z1 && r.z1 < dimZ);
        CHECK(r.floorY >= 0 && r.floorY <= r.ceilingY && r.ceilingY <= dimY);
    }
}

// --- Material palette ---

TEST_CASE(Building_MaterialPaletteValidAndDeterministic) {
    for (int m = 0; m <= 9; m++) {
        MaterialPalette p = materialPalette(m);
        CHECK(p.wall   != BlockType::Air);
        CHECK(p.roof   != BlockType::Air);
        CHECK(p.accent != BlockType::Air);
        MaterialPalette p2 = materialPalette(m);     // pure function
        CHECK(p.wall == p2.wall && p.roof == p2.roof && p.accent == p2.accent);
    }
    CHECK(materialPalette(1).wall != materialPalette(2).wall);   // materials differ
}

// --- House (all 10 templates) ---

TEST_CASE(House_AllTemplatesGenerateValidGrids) {
    for (int t = 0; t < 10; t++) {
        HouseBuilding h(t, 1, 0);
        std::vector<uint8_t> blocks; std::vector<Room> rooms;
        int dx, dy, dz;
        genAndCheck(h, 1234u + (uint32_t)t, blocks, rooms, dx, dy, dz);
    }
}

TEST_CASE(House_GenerationIsDeterministic) {
    HouseBuilding a(2, 1, 0), b(2, 1, 0);
    std::vector<uint8_t> ba, bb; std::vector<Room> ra, rb;
    int ax, ay, az, bx, by, bz;
    genAndCheck(a, 777u, ba, ra, ax, ay, az);
    genAndCheck(b, 777u, bb, rb, bx, by, bz);
    CHECK_EQ(ax, bx); CHECK_EQ(ay, by); CHECK_EQ(az, bz);
    CHECK(ba == bb);
}

// --- Other building kinds ---

TEST_CASE(Pub_GeneratesValidGridWithRooms) {
    PubBuilding p(1);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(p, 55u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)p.kind(), (int)BuildingKind::Pub);
    CHECK(rooms.size() > 0);              // a pub has an interior
}

TEST_CASE(Blacksmith_GeneratesValidGridWithRooms) {
    BlacksmithBuilding b(2);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(b, 56u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)b.kind(), (int)BuildingKind::Blacksmith);
    CHECK(rooms.size() > 0);
}

TEST_CASE(MageTower_GeneratesValidGridWithRooms) {
    MageTowerBuilding m(3, 3);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(m, 57u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)m.kind(), (int)BuildingKind::MageTower);
    CHECK(rooms.size() > 0);
    CHECK(dy > dx);                       // a tower is taller than it is wide
}

// --- rotateBuilding ---

TEST_CASE(RotateBuilding_Q0IsIdentity) {
    int sx = 2, sy = 1, sz = 3;
    std::vector<uint8_t> src(sx * sy * sz);
    for (size_t i = 0; i < src.size(); i++) src[i] = (uint8_t)(i + 1);
    std::vector<Room> srcRooms = { Room{0, 0, 1, 2, 0, 0, RoomType::LivingRoom} };

    std::vector<uint8_t> dst; std::vector<Room> dstRooms; int dx = 0, dz = 0;
    rotateBuilding(sx, sy, sz, src, srcRooms, 0, dst, dstRooms, dx, dz);

    CHECK_EQ(dx, sx); CHECK_EQ(dz, sz);
    CHECK(dst == src);
    CHECK_EQ((int)dstRooms.size(), 1);
}

TEST_CASE(RotateBuilding_Q1SwapsDimensions) {
    int sx = 2, sy = 1, sz = 3;
    std::vector<uint8_t> src(sx * sy * sz, (uint8_t)7);
    std::vector<Room> srcRooms;
    std::vector<uint8_t> dst; std::vector<Room> dstRooms; int dx = 0, dz = 0;
    rotateBuilding(sx, sy, sz, src, srcRooms, 1, dst, dstRooms, dx, dz);

    CHECK_EQ(dx, sz);                       // odd quadrant swaps X/Z
    CHECK_EQ(dz, sx);
    CHECK_EQ((int)dst.size(), dx * sy * dz);
}

TEST_CASE(RotateBuilding_FourQuarterTurnsRoundTrip) {
    int sx = 3, sy = 2, sz = 4;
    std::vector<uint8_t> src(sx * sy * sz);
    for (size_t i = 0; i < src.size(); i++) src[i] = (uint8_t)(i % 251);
    std::vector<Room> noRooms;

    std::vector<uint8_t> cur = src;
    int cx = sx, cz = sz;
    for (int k = 0; k < 4; k++) {
        std::vector<uint8_t> nxt; std::vector<Room> nxtRooms; int nx = 0, nz = 0;
        rotateBuilding(cx, sy, cz, cur, noRooms, 1, nxt, nxtRooms, nx, nz);
        cur.swap(nxt); cx = nx; cz = nz;
    }
    CHECK_EQ(cx, sx); CHECK_EQ(cz, sz);
    CHECK(cur == src);                      // 4×90° == identity
}
