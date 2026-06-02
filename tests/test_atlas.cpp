#include "terrax_test.h"
#include "atlas.h"
#include <cmath>

// Texture atlas (src/atlas.cpp). tileUV() maps each TileID to its inset UV
// rect in the 4x12 atlas; world meshing depends on those rects being in
// range, correctly ordered, and pointing at the right grid cell.

static float colOf(float u0, float u1) { return ((u0 + u1) * 0.5f) / (1.0f / ATLAS_COLS); }
static float rowOf(float v0, float v1) { return ((v0 + v1) * 0.5f) / (1.0f / ATLAS_ROWS); }

TEST_CASE(Atlas_GeometryConstants) {
    CHECK_EQ(ATLAS_PX,     ATLAS_COLS * TILE_PX);
    CHECK_EQ(ATLAS_HEIGHT, ATLAS_ROWS * TILE_PX);
}

TEST_CASE(Atlas_TileUVInRangeAndOrdered) {
    TileID tiles[] = { TileID::GrassTop, TileID::Stone, TileID::Sand, TileID::Snow,
                       TileID::Water, TileID::Glass, TileID::LeavesRed, TileID::PaintFirst };
    for (TileID t : tiles) {
        float u0, v0, u1, v1;
        tileUV(t, u0, v0, u1, v1);
        CHECK(u0 < u1);
        CHECK(v0 < v1);
        CHECK(u0 >= 0.0f && u1 <= 1.0f);
        CHECK(v0 >= 0.0f && v1 <= 1.0f);
    }
}

TEST_CASE(Atlas_TileMapsToCorrectCell) {
    float u0, v0, u1, v1;
    // GrassTop = id 0 -> (col 0, row 0)
    tileUV(TileID::GrassTop, u0, v0, u1, v1);
    CHECK_EQ((int)colOf(u0, u1), 0);
    CHECK_EQ((int)rowOf(v0, v1), 0);
    // Stone = id 3 -> (col 3, row 0)
    tileUV(TileID::Stone, u0, v0, u1, v1);
    CHECK_EQ((int)colOf(u0, u1), 3);
    CHECK_EQ((int)rowOf(v0, v1), 0);
    // WoodTop = id 4 -> (col 0, row 1)
    tileUV(TileID::WoodTop, u0, v0, u1, v1);
    CHECK_EQ((int)colOf(u0, u1), 0);
    CHECK_EQ((int)rowOf(v0, v1), 1);
}

TEST_CASE(Atlas_TileHasHalfTexelInset) {
    float u0, v0, u1, v1;
    tileUV(TileID::Dirt, u0, v0, u1, v1);
    float w = u1 - u0;
    // one column wide, shrunk by a half-texel on each side
    CHECK(std::fabs(w - (1.0f / ATLAS_COLS - 1.0f / ATLAS_PX)) < 1e-4f);
    CHECK(w > 0.0f && w < 1.0f / ATLAS_COLS);
}

// generateAtlas runs every pixel-fill path (solids, foliage cutouts, painted
// palette) and uploads via the GL stub — a headless smoke test that the
// whole atlas build executes without tripping a bounds error.
TEST_CASE(Atlas_GenerateRunsHeadless) {
    GLuint tex = generateAtlas();
    CHECK(tex != 0);     // stub hands back a non-zero id
}
