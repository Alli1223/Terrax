#include "terrax_test.h"
#include "world.h"
#include <memory>

// World / Chunk (src/world.cpp). The voxel store, the light-map nibble
// packing, and the seed-driven terrain oracle (sampleSurface /
// sampleSurfaceSolid) that the town planner and prop placer query without
// generating chunks. GL-touching paths (meshing/upload/draw) are exercised
// against the headless gl_stub but not asserted here.

// --- Chunk block storage ---

TEST_CASE(Chunk_FreshChunkIsAllAir) {
    Chunk c({0, 0}, false);
    CHECK(c.get(0, 0, 0)  == BlockType::Air);
    CHECK(c.get(8, 128, 8) == BlockType::Air);
}

TEST_CASE(Chunk_SetGetBlock) {
    Chunk c({0, 0}, false);
    c.set(1, 2, 3, BlockType::Stone);
    CHECK(c.get(1, 2, 3) == BlockType::Stone);
    CHECK(c.get(1, 2, 4) == BlockType::Air);   // neighbour untouched
}

TEST_CASE(Chunk_OutOfBoundsIgnored) {
    Chunk c({0, 0}, false);
    c.set(-1, 0, 0, BlockType::Stone);             // all ignored, no crash
    c.set(CHUNK_SIZE, 0, 0, BlockType::Stone);
    c.set(0, CHUNK_HEIGHT, 0, BlockType::Stone);
    c.set(0, -1, 0, BlockType::Stone);
    CHECK(c.get(-1, 0, 0)         == BlockType::Air);
    CHECK(c.get(CHUNK_SIZE, 0, 0) == BlockType::Air);
    CHECK(c.get(0, CHUNK_HEIGHT, 0) == BlockType::Air);
}

// --- Light map: sky light is the high nibble, block light the low nibble ---

TEST_CASE(Chunk_LightNibblesIndependent) {
    Chunk c({0, 0}, false);
    c.setSkyLight(2, 3, 4, 12);
    c.setBlockLight(2, 3, 4, 5);
    CHECK_EQ((int)c.getSkyLight(2, 3, 4),   12);
    CHECK_EQ((int)c.getBlockLight(2, 3, 4), 5);

    // Rewriting one channel must not disturb the other.
    c.setSkyLight(2, 3, 4, 7);
    CHECK_EQ((int)c.getSkyLight(2, 3, 4),   7);
    CHECK_EQ((int)c.getBlockLight(2, 3, 4), 5);

    c.setBlockLight(2, 3, 4, 15);
    CHECK_EQ((int)c.getSkyLight(2, 3, 4),   7);
    CHECK_EQ((int)c.getBlockLight(2, 3, 4), 15);
}

// --- Terrain oracle: seed handling ---

TEST_CASE(World_SeedRoundTrips) {
    setWorldSeed(424242u);
    CHECK_EQ((int)worldSeed(), 424242);
}

TEST_CASE(Surface_SampleDeterministic) {
    setWorldSeed(1234);
    SurfaceSample a = sampleSurface(37, -52);
    SurfaceSample b = sampleSurface(37, -52);
    CHECK_EQ(a.height, b.height);
    CHECK_EQ(a.biome,  b.biome);
}

TEST_CASE(Surface_HeightInWorldRange) {
    setWorldSeed(2024);
    for (int wx = -96; wx <= 96; wx += 16)
        for (int wz = -96; wz <= 96; wz += 16) {
            SurfaceSample s = sampleSurface(wx, wz);
            CHECK(s.height >= 0 && s.height < CHUNK_HEIGHT);
            CHECK(s.biome  >= 0);
        }
}

TEST_CASE(Surface_SolidDeterministicAndInRange) {
    setWorldSeed(2024);
    int a = sampleSurfaceSolid(20, 20);
    int b = sampleSurfaceSolid(20, 20);
    CHECK_EQ(a, b);
    CHECK(a >= 0 && a < CHUNK_HEIGHT);
}

TEST_CASE(Surface_SeedChangesTerrain) {
    // Snapshot a grid of heights under one seed, then under another; some
    // column must differ (a seed that didn't affect terrain would be a bug).
    setWorldSeed(1);
    int h[7][7];
    for (int i = 0; i < 7; i++)
        for (int j = 0; j < 7; j++)
            h[i][j] = sampleSurface(i * 24, j * 24).height;

    setWorldSeed(98765);
    bool changed = false;
    for (int i = 0; i < 7 && !changed; i++)
        for (int j = 0; j < 7 && !changed; j++)
            if (sampleSurface(i * 24, j * 24).height != h[i][j]) changed = true;
    CHECK(changed);
}

// --- World block access ---

TEST_CASE(World_UnloadedReadsAir) {
    World w(false);
    CHECK(w.getBlock(0, 70, 0)    == BlockType::Air);
    CHECK(w.getBlock(123, 5, -77) == BlockType::Air);
}

TEST_CASE(World_SetBlockNeedsLoadedChunk) {
    World w(false);
    // No chunk present -> setBlock is a silent no-op.
    w.setBlock(3, 64, 5, BlockType::Stone);
    CHECK(w.getBlock(3, 64, 5) == BlockType::Air);

    // Load a chunk, then writes stick and read back.
    auto c = std::make_unique<Chunk>(ChunkPos{0, 0}, false);
    c->state = ChunkState::Generated;
    w.chunks[ChunkPos{0, 0}] = std::move(c);

    w.setBlock(3, 64, 5, BlockType::Stone);
    CHECK(w.getBlock(3, 64, 5) == BlockType::Stone);
    CHECK(w.getBlock(3, 65, 5) == BlockType::Air);
}
