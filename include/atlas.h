#pragma once
#include "gl_loader.h"

// Texture atlas layout: 4 columns × 14 rows, each tile 64×64 px → 256×896 atlas.
static constexpr int ATLAS_COLS   = 4;
static constexpr int ATLAS_ROWS   = 14;
static constexpr int TILE_PX      = 64;
static constexpr int ATLAS_PX     = ATLAS_COLS * TILE_PX; // 256 (width)
static constexpr int ATLAS_HEIGHT = ATLAS_ROWS * TILE_PX; // 832 (height)

// 24-colour painted-block palette — a designer-style spread of muted, richer
// tones. Painted blocks let houses be any of these colours; the atlas, world
// meshing and house editor all index this table.
struct RGB8 { unsigned char r, g, b; };
static constexpr int  PAINT_COUNT = 28;
static constexpr RGB8 PAINT_PALETTE[PAINT_COUNT] = {
    {238, 240, 243},  //  0 White
    {235, 224, 193},  //  1 Cream
    {178, 184, 192},  //  2 Light Gray
    {110, 120, 132},  //  3 Slate Gray
    { 56,  60,  68},  //  4 Charcoal
    { 28,  29,  34},  //  5 Black
    {197, 109,  78},  //  6 Terracotta
    {165,  67,  55},  //  7 Brick Red
    {146,  40,  50},  //  8 Crimson
    {205, 112,  44},  //  9 Rust Orange
    {228, 168,  66},  // 10 Amber
    {198, 165,  70},  // 11 Mustard
    {118,  73,  45},  // 12 Chestnut
    {215, 189, 139},  // 13 Sand
    {124, 127,  71},  // 14 Olive
    {150, 171, 129},  // 15 Sage
    { 52, 102,  60},  // 16 Forest Green
    {159, 207, 181},  // 17 Mint
    {124, 179, 223},  // 18 Sky Blue
    { 56, 149, 149},  // 19 Teal
    { 44,  60, 106},  // 20 Navy
    { 95, 123, 157},  // 21 Steel Blue
    {116,  73, 117},  // 22 Plum
    {199, 143, 151},  // 23 Dusty Rose
    { 82,  52,  30},  // 24 Dark Brown      — wood corner posts (cabin / timber)
    {170, 142,  95},  // 25 Dark Tan        — sandstone brick coursing
    { 84,  92, 102},  // 26 Dim Gray        — pale-stone corner / brick
    {150,  80,  56},  // 27 Dark Terracotta — autumn / warm-wall corner trim
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
    PaintFirst   = 24, // 28 solid painted-colour tiles, ids 24..51 (rows 6..12)
    Lantern      = 52, // col 0, row 13 — caged warm lantern (BlockType::Lantern)
    Farmland     = 53, // col 1, row 13 — tilled crop soil (BlockType::Farmland)
};

// Returns atlas UV corners for a tile (with half-texel inset to prevent bleeding).
// u0,v0 = bottom-left; u1,v1 = top-right (GL texture space, V=0 at bottom).
void tileUV(TileID tile, float& u0, float& v0, float& u1, float& v1);

GLuint generateAtlas();
