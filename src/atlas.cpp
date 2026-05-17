#include "atlas.h"
#include <vector>
#include <cmath>
#include <cstdint>

// ---- Pixel helpers ----

static uint32_t phash(int x, int y, int seed) {
    uint32_t h = (uint32_t)(x * 1619 + y * 31337 + seed * 6271);
    h ^= h >> 16; h *= 0x45d9f3bU; h ^= h >> 16;
    return h;
}

// Returns a value in [0,255] deterministically from position and seed
static int prand(int x, int y, int seed) {
    return (int)(phash(x, y, seed) & 0xFF);
}

// Clamp a value to [0, 255]
static uint8_t clamp8(int v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static void setPixel(std::vector<uint8_t>& data, int ax, int ay,
                     int r, int g, int b) {
    int idx = (ay * ATLAS_PX + ax) * 4;
    data[idx+0] = clamp8(r);
    data[idx+1] = clamp8(g);
    data[idx+2] = clamp8(b);
    data[idx+3] = 255;
}

// ---- Per-tile generators ----
// Tile local coords: tx,ty in [0, TILE_PX).
// Atlas coords: ax = col*TILE_PX+tx, ay = row*TILE_PX+ty.
// In GL texture space V=0 is stored in memory row 0 (bottom of tile).

static void genGrassTop(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 10);
        int dark = prand(ax, ay, 11) > 210 ? -20 : 0; // occasional dark patch
        setPixel(d, ax, ay, 72+n/8+dark, 120+n/4+dark, 40+n/10);
    }
}

static void genGrassSide(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 20);
        // ty=0 is bottom (dirt), ty=TILE_PX-1 is top (grass cap)
        if (ty >= TILE_PX - 6) {
            // grass cap strip at top
            setPixel(d, ax, ay, 72+n/8, 120+n/4, 40);
        } else if (ty >= TILE_PX - 10) {
            // transition: blend grass green into dirt
            float t = (ty - (TILE_PX-10)) / 4.0f;
            int dr = (int)(107 + n/10 + t*(72+n/8  - 107-n/10));
            int dg = (int)( 70 + n/10 + t*(120+n/4 -  70-n/10));
            int db = (int)( 40 + n/15 + t*( 40     -  40-n/15));
            setPixel(d, ax, ay, dr, dg, db);
        } else {
            // dirt body
            setPixel(d, ax, ay, 107+n/10, 70+n/10, 40+n/15);
        }
    }
}

static void genDirt(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 30);
        int spot = prand(ax, ay, 31) > 230 ? -15 : 0;
        setPixel(d, ax, ay, 107+n/10+spot, 70+n/10+spot, 40+n/15+spot);
    }
}

static void genStone(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 40);
        // Crack-like dark lines
        int crack = (prand(ax, ay, 41) > 245) ? -40 : 0;
        int v = 115 + n/6 + crack;
        setPixel(d, ax, ay, v, v, v+2);
    }
}

static void genWoodTop(std::vector<uint8_t>& d, int col, int row) {
    float cx = TILE_PX * 0.5f, cy = TILE_PX * 0.5f;
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        float dist = sqrtf((tx-cx)*(tx-cx)+(ty-cy)*(ty-cy));
        int ring = (int)(dist * 1.2f) % 6; // concentric rings
        int n = prand(ax, ay, 50);
        int dark = ring < 2 ? -18 : 0;
        setPixel(d, ax, ay, 175+n/15+dark, 120+n/15+dark, 60+n/20+dark);
    }
}

static void genWoodSide(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 60);
        // Vertical grain stripes
        int stripe = (tx + prand(0, ty, 61)/16) % 8;
        int dark = stripe < 2 ? -25 : 0;
        setPixel(d, ax, ay, 140+n/15+dark, 90+n/15+dark, 45+n/20+dark);
    }
}

static void genLeaves(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 70);
        int bright = prand(ax, ay, 71) > 200 ? 20 : 0; // lighter patches
        setPixel(d, ax, ay, 35+n/12, 90+n/8+bright, 25+n/15);
    }
}

static void genSand(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 80);
        setPixel(d, ax, ay, 210+n/15, 190+n/15, 115+n/15);
    }
}

static void genGravel(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 90);
        int v = 100 + (n & 0x3F);
        int tint = prand(ax, ay, 91) > 150 ? 8 : -4;
        setPixel(d, ax, ay, v+tint, v, v-tint/2);
    }
}

static void genSnow(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 100);
        int sparkle = prand(ax, ay, 101) > 235 ? 12 : 0;
        int v = 218 + n/14 + sparkle;
        setPixel(d, ax, ay, v, v, v + 10); // slight blue tint
    }
}

static void genCactusTop(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 110);
        bool spine = (tx == TILE_PX/2 || ty == TILE_PX/2);
        int dark = spine ? -18 : 0;
        setPixel(d, ax, ay, 28+n/18+dark, 95+n/10+dark, 18+n/18);
    }
}

static void genCactusSide(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 120);
        int ridge = (tx % 10 < 3) ? -12 : 0;                  // vertical ridges
        int spine = (prand(ax, ay, 121) > 248) ? -25 : 0;     // occasional spine dot
        setPixel(d, ax, ay, 28+n/18+ridge, 90+n/10+ridge+spine, 18+n/18);
    }
}

static void genSandstone(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 130);
        // Horizontal layering lines
        int layer = ((ty * 3 / 4) + prand(ax, 0, 131) / 32) % 7;
        int dark = (layer == 0 || layer == 3) ? -18 : 0;
        // Warm beige-orange, similar to sand but with visible strata
        setPixel(d, ax, ay, 200+n/18+dark, 170+n/20+dark, 90+n/25+dark);
    }
}

static void genIce(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 140);
        // Crack veins
        int crack = (prand(ax, ay, 141) > 242) ? -30 : 0;
        // Sparkle highlights
        int sparkle = (prand(ax, ay, 142) > 250) ? 25 : 0;
        int v = 200 + n/14 + crack + sparkle;
        // Blue-white translucent-looking ice
        setPixel(d, ax, ay, v - 20, v - 10, v + 15);
    }
}

static void genWater(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n  = prand(ax, ay, 160);
        // Subtle diagonal ripple pattern
        float ripple = sinf((tx * 0.35f + ty * 0.28f)) * 14.0f
                     + sinf((tx * 0.20f - ty * 0.40f)) * 8.0f;
        int r = clamp8(18  + n/24 + (int)(ripple * 0.3f));
        int g = clamp8(88  + n/12 + (int)(ripple * 0.6f));
        int b = clamp8(180 + n/18 + (int)(ripple * 0.5f));
        setPixel(d, ax, ay, r, g, b);
    }
}

static void genGlowstone(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 150);
        // Cracked bright amber with glowing yellow veins
        int crack = (prand(ax, ay, 151) > 235) ? -35 : 0;
        int glow  = (prand(ax, ay, 152) > 220) ? 25 : 0;
        setPixel(d, ax, ay, 230+n/20+crack, 180+n/18+crack+glow, 60+n/25);
    }
}

// ---- Public API ----

void tileUV(TileID tile, float& u0, float& v0, float& u1, float& v1) {
    int id  = (int)tile;
    int col = id % ATLAS_COLS;
    int row = id / ATLAS_COLS;
    float ts = 1.0f / ATLAS_COLS;          // tile size in UV space (0.25)
    float half = 0.5f / ATLAS_PX;          // half-texel inset against bleeding
    u0 = col * ts + half;
    v0 = row * ts + half;
    u1 = (col + 1) * ts - half;
    v1 = (row + 1) * ts - half;
}

GLuint generateAtlas() {
    std::vector<uint8_t> data(ATLAS_PX * ATLAS_PX * 4, 255);

    genGrassTop (data, 0, 0);
    genGrassSide(data, 1, 0);
    genDirt     (data, 2, 0);
    genStone    (data, 3, 0);
    genWoodTop  (data, 0, 1);
    genWoodSide (data, 1, 1);
    genLeaves   (data, 2, 1);
    genSand     (data, 3, 1);
    genGravel    (data, 0, 2);
    genSnow      (data, 1, 2);
    genCactusTop (data, 2, 2);
    genCactusSide(data, 3, 2);
    genSandstone (data, 0, 3);
    genIce       (data, 1, 3);
    genGlowstone (data, 2, 3);
    genWater     (data, 3, 3);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_PX, ATLAS_PX, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}
