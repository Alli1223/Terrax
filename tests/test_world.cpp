#include "terrax_test.h"
#include "world.h"
#include "town.h"
#include <memory>

// Internal town.cpp helper (not in the public header) — stamps a town's
// perimeter wall into a chunk. Declared here so the test can drive it directly
// with a synthetic town, no full town-plan survey required.
void stampTownWall(Chunk* c, const Town& t);

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

// --- Biome variety & size, ocean depth (the world-gen tuning) ---------------
// These sample the terrain oracle over wide areas to confirm the climate model
// produces all seven biomes (including the desert and jungle corners), that
// each biome forms large regions, and that ocean basins drop far below sea
// level. Stats are printed first so the measured numbers show even on failure.

TEST_CASE(Biomes_AllSevenAppearWithCoverage) {
    setWorldSeed(20240601u);
    const int N = 200, STEP = 96;        // 19200-block-wide grid, centred on origin
    int counts[7] = {0}, land = 0, total = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            SurfaceSample s = sampleSurface(i * STEP - N*STEP/2, j * STEP - N*STEP/2);
            if (s.biome >= 0 && s.biome < 7) { counts[s.biome]++; total++; }
            if (s.height >= 64) land++;
        }
    std::cout << "  [biomes] Plains="   << counts[0] << " Forest="    << counts[1]
              << " Desert="   << counts[2] << " Mountains=" << counts[3]
              << " Tundra="   << counts[4] << " Savanna="   << counts[5]
              << " Jungle="   << counts[6] << "  (land=" << land << "/" << total << ")\n";
    for (int b = 0; b < 7; b++) CHECK(counts[b] > 0);   // every biome occurs
    CHECK(counts[2] > 50);    // Desert is a real region, not a sliver
    CHECK(counts[6] > 50);    // Jungle likewise
}

TEST_CASE(Biomes_RegionsAreLarge) {
    setWorldSeed(20240601u);
    // Mean biome "run length" along horizontal scan lines: large biomes -> long
    // unbroken runs. (At the old 0.0010 climate frequency runs were far shorter.)
    const int STEP = 16, LEN = 12000, LINES = 9;
    long long totalRun = 0; int runs = 0;
    for (int li = 0; li < LINES; li++) {
        int wz = li * 1500 - 6000, prev = -1, runLen = 0;
        for (int wx = -LEN/2; wx <= LEN/2; wx += STEP) {
            int b = sampleSurface(wx, wz).biome;
            if (b == prev) runLen += STEP;
            else { if (prev >= 0) { totalRun += runLen; runs++; } prev = b; runLen = STEP; }
        }
        if (prev >= 0) { totalRun += runLen; runs++; }
    }
    double meanRun = runs ? (double)totalRun / runs : 0.0;
    std::cout << "  [biomes] mean region run = " << meanRun
              << " blocks over " << runs << " runs\n";
    CHECK(meanRun >= 120.0);   // biomes span many chunks, not a few
}

TEST_CASE(Oceans_AreDeep) {
    setWorldSeed(20240601u);
    const int N = 200, STEP = 64;        // 12800-block-wide grid
    int minH = 1000, oceanCols = 0, deepCols = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            int h = sampleSurface(i*STEP - N*STEP/2, j*STEP - N*STEP/2).height;
            if (h < 64) { oceanCols++; if (h < minH) minH = h; if (h <= 50) deepCols++; }
        }
    std::cout << "  [oceans] ocean cols=" << oceanCols << " deepest floor y=" << minH
              << " deep(<=50) cols=" << deepCols << "\n";
    CHECK(oceanCols > 0);
    CHECK(minH <= 45);        // basins drop well below sea level (64)
    CHECK(deepCols > 0);      // genuinely deep, see-no-bottom water exists
}

TEST_CASE(Terrain_SnowAndBeachBandsReachable) {
    setWorldSeed(20240601u);
    // The surface pass dresses columns above SNOW_LINE_Y (150) with snow and the
    // shore band [SEA_LEVEL, SEA_LEVEL+2] of grassy biomes with sand. This checks
    // the terrain actually reaches those bands so the dressing fires.
    const int N = 200, STEP = 96;
    int maxH = 0, snowCols = 0, beachCols = 0;
    auto grassBiome = [](int b){ return b == 0 || b == 1 || b == 5 || b == 6; };
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            SurfaceSample s = sampleSurface(i*STEP - N*STEP/2, j*STEP - N*STEP/2);
            if (s.height > maxH) maxH = s.height;
            if (s.height >= 120)                                          snowCols++;  // SNOW_LINE_Y
            if (grassBiome(s.biome) && s.height >= 64 && s.height <= 65)  beachCols++;  // BEACH_TOP_Y
        }
    std::cout << "  [terrain] max height=" << maxH << " snow(>=120) cols=" << snowCols
              << " beach-band grassy cols=" << beachCols << "\n";
    CHECK(maxH >= 120);      // peaks reach the snow line
    CHECK(snowCols > 0);     // upper mountains are snow-capped
    CHECK(beachCols > 0);    // grassy shore exists for beaches
}

TEST_CASE(TownWall_StylesStampWithGate) {
    const int baseY = 70, R = 100;

    // Great stone rampart (style 2): thick stone, height 7, gate with lintel.
    {
        Town t;
        t.center = glm::ivec2(0, 0); t.baseY = baseY; t.wallRadius = R;
        t.wallStyle = 2; t.gateAngles.push_back(0.0f);      // gate toward +X (100,0)
        Chunk c({0, 6}, false);                             // ring at (0,100), away from gate
        stampTownWall(&c, t);
        int wallCols = 0;
        for (int lx = 0; lx < CHUNK_SIZE; lx++)
            for (int lz = 0; lz < CHUNK_SIZE; lz++)
                if (c.get(lx, baseY + 3, lz) == BlockType::Stone) wallCols++;
        CHECK(wallCols > 0);
        CHECK(c.get(0, baseY + 1, 4) == BlockType::Stone);  // (0,100): stone wall body
        CHECK(c.get(0, baseY + 6, 4) == BlockType::Stone);  // ...up its height (7)

        Chunk cg({6, 0}, false);                            // ring at (100,0), the gate
        stampTownWall(&cg, t);
        CHECK(cg.get(4, baseY + 2, 0) == BlockType::Air);   // clear walk-through
        CHECK(cg.get(4, baseY + 7, 0) == BlockType::Stone); // lintel at the top
    }

    // Wooden palisade (style 0): a thinner, shorter wall built from wood.
    {
        Town t;
        t.center = glm::ivec2(0, 0); t.baseY = baseY; t.wallRadius = R;
        t.wallStyle = 0; t.gateAngles.push_back(0.0f);
        Chunk c({0, 6}, false);
        stampTownWall(&c, t);
        CHECK(c.get(0, baseY + 1, 4) == BlockType::Wood);   // palisade body is wood
        CHECK(c.get(0, baseY + 3, 4) == BlockType::Wood);   // up to its height (4)
    }
}
