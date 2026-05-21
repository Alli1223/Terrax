#pragma once
#include "gl_loader.h"

// Texture atlas layout: 4 columns × 10 rows, each tile 64×64 px → 256×640 atlas.
static constexpr int ATLAS_COLS   = 4;
static constexpr int ATLAS_ROWS   = 10;
static constexpr int TILE_PX      = 64;
static constexpr int ATLAS_PX     = ATLAS_COLS * TILE_PX; // 256 (width)
static constexpr int ATLAS_HEIGHT = ATLAS_ROWS * TILE_PX; // 640 (height)

// 16-colour painted-block palette. Painted blocks let houses be any of these
// colours; the atlas, world meshing and house editor all index this table.
struct RGB8 { unsigned char r, g, b; };
static constexpr int  PAINT_COUNT = 16;
static constexpr RGB8 PAINT_PALETTE[PAINT_COUNT] = {
    {235, 238, 242},  //  0 White
    {168, 174, 182},  //  1 Light Gray
    {102, 108, 118},  //  2 Gray
    { 40,  42,  48},  //  3 Black
    {196,  58,  50},  //  4 Red
    {226, 124,  40},  //  5 Orange
    {240, 198,  58},  //  6 Yellow
    {146, 198,  64},  //  7 Lime
    { 66, 142,  70},  //  8 Green
    { 54, 158, 148},  //  9 Teal
    { 98, 170, 224},  // 10 Light Blue
    { 60,  86, 182},  // 11 Blue
    {134,  76, 172},  // 12 Purple
    {226, 132, 172},  // 13 Pink
    {120,  80,  50},  // 14 Brown
    {210, 182, 134},  // 15 Tan
};
static constexpr RGB8 GLASS_TINT = {200, 225, 238};

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
    TallGrass  = 16, // col 0, row 4  — foliage cross, alpha-cutout
    FlowerRed  = 17, // col 1, row 4
    FlowerYellow = 18, // col 2, row 4
    FlowerBlue   = 19, // col 3, row 4
    LeavesOrange = 20, // col 0, row 5 — autumn orange
    LeavesRed    = 21, // col 1, row 5 — deep autumn red
    LeavesPink   = 22, // col 2, row 5 — spring blossom pink
    Glass        = 23, // col 3, row 5 — window glass
    PaintFirst   = 24, // 16 solid painted-colour tiles, ids 24..39 (rows 6..9)
};

// Returns atlas UV corners for a tile (with half-texel inset to prevent bleeding).
// u0,v0 = bottom-left; u1,v1 = top-right (GL texture space, V=0 at bottom).
void tileUV(TileID tile, float& u0, float& v0, float& u1, float& v1);

GLuint generateAtlas();
