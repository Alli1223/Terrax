#pragma once
#include "gl_loader.h"

// Texture atlas layout: 4 columns × 4 rows, each tile is 64×64 px → 256×256 atlas.
static constexpr int ATLAS_COLS = 4;
static constexpr int TILE_PX    = 64;
static constexpr int ATLAS_PX   = ATLAS_COLS * TILE_PX; // 256

enum class TileID : int {
    GrassTop   = 0,  // col 0, row 0
    GrassSide  = 1,  // col 1, row 0
    Dirt       = 2,  // col 2, row 0
    Stone      = 3,  // col 3, row 0
    WoodTop    = 4,  // col 0, row 1
    WoodSide   = 5,  // col 1, row 1
    Leaves     = 6,  // col 2, row 1
    Sand       = 7,  // col 3, row 1
    Gravel     = 8,  // col 0, row 2
    Snow       = 9,  // col 1, row 2
    CactusTop  = 10, // col 2, row 2
    CactusSide = 11, // col 3, row 2
    Sandstone  = 12, // col 0, row 3
    Ice        = 13, // col 1, row 3
    Glowstone  = 14, // col 2, row 3
    Water      = 15, // col 3, row 3
};

// Returns atlas UV corners for a tile (with half-texel inset to prevent bleeding).
// u0,v0 = bottom-left; u1,v1 = top-right (in GL texture space, V=0 at bottom).
void tileUV(TileID tile, float& u0, float& v0, float& u1, float& v1);

// Generates and uploads the procedural texture atlas.
// Returns the GL texture object ID. Caller owns it.
GLuint generateAtlas();
