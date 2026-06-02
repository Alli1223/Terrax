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

TEST_CASE(Stable_GeneratesValidGridWithStalls) {
    StableBuilding s(6);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(s, 58u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)s.kind(), (int)BuildingKind::Stable);
    CHECK(rooms.size() > 0);
    // The stall détail overlays wood-plank dividers — none come from the shell.
    int wood = 0;
    for (uint8_t v : blocks) if (v == (uint8_t)BlockType::Wood) wood++;
    CHECK(wood > 0);
}

TEST_CASE(Chapel_GeneratesValidGridWithAltar) {
    ChapelBuilding c(2);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(c, 59u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)c.kind(), (int)BuildingKind::Chapel);
    CHECK(rooms.size() > 0);
    CHECK(dz > dx);                       // a long single nave
    // Altar candle + wall sconces emit glowstone; the shell emits none.
    int glow = 0;
    for (uint8_t v : blocks) if (v == (uint8_t)BlockType::Glowstone) glow++;
    CHECK(glow > 0);
}

TEST_CASE(Apothecary_GeneratesShopAndBackRoom) {
    ApothecaryBuilding a(8);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(a, 60u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)a.kind(), (int)BuildingKind::Apothecary);
    CHECK(rooms.size() >= 2);             // shop + living quarters
}

// --- L / T / U composite houses + biome roofs ---

TEST_CASE(House_CompositeShapesAreValid) {
    // 12 L, 13 T, 14 U, 15 +, 16 courtyard, 17 H, 18 Z, 19 E — across several
    // seeds each, since the proportions are randomised.
    for (int t = 12; t <= 19; t++)
        for (uint32_t s = 0; s < 6; s++) {
            HouseBuilding h(t, 1, 0);
            std::vector<uint8_t> blocks; std::vector<Room> rooms;
            int dx, dy, dz;
            genAndCheck(h, 200u + (uint32_t)t * 17u + s, blocks, rooms, dx, dy, dz);
            CHECK(rooms.size() >= 2);     // a wing per room → at least two rooms
        }
}

TEST_CASE(House_CompositeVariesWithSeed) {
    // Composite footprints are seed-driven, so different seeds yield different
    // houses — four seeds must produce at least two distinct grids.
    HouseBuilding h(12, 1, 0);
    std::vector<std::vector<uint8_t>> grids;
    for (uint32_t s : { 111u, 222u, 333u, 444u }) {
        std::vector<uint8_t> bl; std::vector<Room> rm;
        int dx, dy, dz, ddx, ddz;
        h.generate(s, bl, rm, dx, dy, dz, ddx, ddz);
        bool isNew = true;
        for (const auto& g : grids) if (g == bl) isNew = false;
        if (isNew) grids.push_back(bl);
    }
    CHECK(grids.size() >= 2);
}

TEST_CASE(House_SteepRoofIsTallerThanFlat) {
    // The biome rule drives roof style via roofType; flat (0) must be shorter
    // than steep gable (4) for the same template, confirming both apply.
    HouseBuilding flat(0, 0, 0), steep(0, 4, 0);
    std::vector<uint8_t> bf, bs; std::vector<Room> rf, rs;
    int fx, fy, fz, sx, sy, sz;
    genAndCheck(flat,  9u, bf, rf, fx, fy, fz);
    genAndCheck(steep, 9u, bs, rs, sx, sy, sz);
    CHECK(sy > fy);
}

TEST_CASE(Bakery_GeneratesShopWithRooms) {
    BakeryBuilding b(1);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(b, 61u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)b.kind(), (int)BuildingKind::Bakery);
    CHECK(rooms.size() >= 2);             // shop + back room
}

TEST_CASE(Watchtower_IsTallAndNarrow) {
    WatchtowerBuilding w(2, 4);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(w, 62u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)w.kind(), (int)BuildingKind::Watchtower);
    CHECK(dy > dx && dy > dz);            // taller than it is wide
}

TEST_CASE(Watchtower_MaxFloorsDoNotOverflow) {
    // A "big" town builds a 5-storey watchtower (WatchtowerBuilding(mat, 5)).
    // HouseSpec::plans[] must hold every storey — writing a 5th FloorPlan into a
    // 4-element array is the stack-buffer overrun that aborted the server. This
    // exercises that previously-crashing configuration.
    WatchtowerBuilding w(2, 5);
    std::vector<uint8_t> blocks; std::vector<Room> rooms;
    int dx, dy, dz;
    genAndCheck(w, 63u, blocks, rooms, dx, dy, dz);
    CHECK_EQ((int)w.kind(), (int)BuildingKind::Watchtower);
    CHECK(dy > dx && dy > dz);
    CHECK(rooms.size() > 0);
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
