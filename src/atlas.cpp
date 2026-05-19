#include "atlas.h"
#include <vector>
#include <cmath>
#include <cstdint>

// ---- Pixel helpers ----

static uint8_t clamp8(int v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

// Sets pixel with alpha=255 (opaque)
static void setPixel(std::vector<uint8_t>& data, int ax, int ay, int r, int g, int b) {
    int idx = (ay * ATLAS_PX + ax) * 4;
    data[idx+0] = clamp8(r);
    data[idx+1] = clamp8(g);
    data[idx+2] = clamp8(b);
    data[idx+3] = 255;
}

// Sets pixel with explicit alpha (for foliage cutout tiles)
static void setPixelA(std::vector<uint8_t>& data, int ax, int ay, int r, int g, int b, int a) {
    int idx = (ay * ATLAS_PX + ax) * 4;
    data[idx+0] = clamp8(r);
    data[idx+1] = clamp8(g);
    data[idx+2] = clamp8(b);
    data[idx+3] = clamp8(a);
}

// ---- Solid colour fill helper ----

static void fillSolid(std::vector<uint8_t>& d, int col, int row, int r, int g, int b) {
    for (int ty = 0; ty < TILE_PX; ty++)
        for (int tx = 0; tx < TILE_PX; tx++)
            setPixel(d, col*TILE_PX+tx, row*TILE_PX+ty, r, g, b);
}

// ---- Foliage tiles (alpha-cutout) ----

static void genTallGrass(std::vector<uint8_t>& d, int col, int row) {
    const int centers[] = {10, 32, 54};
    const int baseW[]   = {13, 15, 11};
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        float frac = (float)ty / (TILE_PX - 1); // 0=bottom,1=top
        bool blade = false;
        for (int b = 0; b < 3; b++) {
            float hw = baseW[b] * 0.5f * (1.0f - frac * 0.92f);
            if (abs(tx - centers[b]) < (int)(hw + 0.5f)) { blade = true; break; }
        }
        if (blade) {
            int r = 50 + (int)(frac * 20);
            int g = 162 + (int)(frac * 38);
            int b = 20 + (int)(frac * 5);
            setPixelA(d, ax, ay, r, g, b, 255);
        }
        // else: stays transparent (0,0,0,0)
    }
}

static void genFlower(std::vector<uint8_t>& d, int col, int row, int hr, int hg, int hb) {
    const int cx = TILE_PX / 2;
    const int headCY = TILE_PX - 18; // center of head (near top)
    const int headR = 11;
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        float dx = (float)(tx - cx), dy = (float)(ty - headCY);
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < headR) {
            // Flower head — slight shading toward edges
            float shade = 1.0f - dist / headR * 0.35f;
            setPixelA(d, ax, ay, (int)(hr*shade), (int)(hg*shade), (int)(hb*shade), 255);
        } else if (abs(tx - cx) <= 2 && ty < headCY - headR/2 && ty > 6) {
            // Stem
            setPixelA(d, ax, ay, 42, 138, 28, 255);
        }
    }
}

// ---- Public API ----

void tileUV(TileID tile, float& u0, float& v0, float& u1, float& v1) {
    int id  = (int)tile;
    int col = id % ATLAS_COLS;
    int row = id / ATLAS_COLS;
    float us    = 1.0f / ATLAS_COLS;
    float vs    = 1.0f / ATLAS_ROWS;
    float uhalf = 0.5f / ATLAS_PX;
    float vhalf = 0.5f / ATLAS_HEIGHT;
    u0 = col * us + uhalf;
    v0 = row * vs + vhalf;
    u1 = (col + 1) * us - uhalf;
    v1 = (row + 1) * vs - vhalf;
}

GLuint generateAtlas() {
    // Init to transparent — foliage tiles rely on this for their background.
    // Opaque tiles call setPixel for every pixel, which forces alpha=255.
    std::vector<uint8_t> data(ATLAS_PX * ATLAS_HEIGHT * 4, 0);

    // Ground blocks — flat single colours, shaded by the lighting model
    fillSolid(data, 0, 0,  67, 178,  35);  // GrassTop   — bright green
    fillSolid(data, 1, 0,  67, 178,  35);  // GrassSide  — same green (face shading darkens sides)
    fillSolid(data, 2, 0, 130,  88,  50);  // Dirt       — warm brown
    fillSolid(data, 3, 0, 118, 118, 125);  // Stone      — cool grey
    fillSolid(data, 0, 1, 165, 110,  52);  // WoodTop    — amber brown
    fillSolid(data, 1, 1, 165, 110,  52);  // WoodSide   — same
    fillSolid(data, 2, 1,  38, 128,  22);  // Leaves     — dark forest green
    fillSolid(data, 3, 1, 220, 198, 115);  // Sand       — warm yellow
    fillSolid(data, 0, 2, 138, 136, 130);  // Gravel     — pebble grey
    fillSolid(data, 1, 2, 238, 242, 255);  // Snow       — near-white blue
    fillSolid(data, 2, 2,  30, 108,  22);  // CactusTop  — cactus green
    fillSolid(data, 3, 2,  30, 108,  22);  // CactusSide — same
    fillSolid(data, 0, 3, 198, 168,  88);  // Sandstone  — tan
    fillSolid(data, 1, 3, 178, 210, 240);  // Ice        — pale blue
    fillSolid(data, 2, 3, 195, 135,  38);  // Glowstone  — amber gold
    fillSolid(data, 3, 3,  22,  98, 215);  // Water      — ocean blue
    // Foliage (row 4)
    genTallGrass   (data, 0, 4);
    genFlower      (data, 1, 4, 220, 48, 20);   // FlowerRed
    genFlower      (data, 2, 4, 240, 200, 20);  // FlowerYellow
    genFlower      (data, 3, 4, 75,  85, 230);  // FlowerBlue
    // Leaf colour variants (row 5)
    fillSolid(data, 0, 5, 220, 105,  22);  // LeavesOrange — autumn orange
    fillSolid(data, 1, 5, 175,  35,  18);  // LeavesRed    — deep autumn red
    fillSolid(data, 2, 5, 255, 165, 200);  // LeavesPink   — spring blossom pink

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_PX, ATLAS_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}
