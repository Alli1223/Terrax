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

static int prand(int x, int y, int seed) {
    return (int)(phash(x, y, seed) & 0xFF);
}

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

// ---- Opaque tile generators ----

static void genGrassTop(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int cx = tx / 8, cy = ty / 8;
        int cellVar = prand(cx, cy, 10) / 36 - 3;
        int n = prand(ax, ay, 11) / 56 - 2;
        // Vivid saturated green
        setPixel(d, ax, ay, 62 + cellVar + n, 175 + cellVar*2 + n, 35 + cellVar + n);
    }
}

static void genGrassSide(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 20) / 40 - 3;
        if (ty >= TILE_PX - 5) {
            setPixel(d, ax, ay, 62, 175, 35);
        } else if (ty >= TILE_PX - 8) {
            float t = (ty - (TILE_PX - 8)) / 3.0f;
            setPixel(d, ax, ay, (int)(128 + t*(62-128)), (int)(86 + t*(175-86)), 50);
        } else {
            bool line = (ty % 16 == 0);
            int dark = line ? -18 : 0;
            setPixel(d, ax, ay, 128+n+dark, 86+n+dark, 50+n+dark);
        }
    }
}

static void genDirt(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 30) / 40 - 3;
        int gx = (tx + prand(ty/8, 0, 31) % 8) % 10;
        int gy = (ty + prand(tx/8, 0, 32) % 8) % 10;
        int dark = (gx == 0 && gy == 0) ? -28 : 0;
        setPixel(d, ax, ay, 128+n+dark, 86+n+dark, 50+n+dark);
    }
}

static void genStone(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 40) / 44 - 3;
        int row16 = ty / 16;
        int lx = (tx + (row16 % 2) * 8) % 16;
        bool mortar = (ty % 16 == 0) || (lx == 0);
        int dark = mortar ? -38 : 0;
        setPixel(d, ax, ay, 125+n+dark, 123+n+dark, 128+n+dark);
    }
}

static void genWoodTop(std::vector<uint8_t>& d, int col, int row) {
    float cx = TILE_PX * 0.5f, cy = TILE_PX * 0.5f;
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        float dist = sqrtf((tx-cx)*(tx-cx)+(ty-cy)*(ty-cy));
        int ring = (int)(dist * 0.55f) % 2;
        int n = prand(ax, ay, 50) / 52 - 2;
        int dark = ring == 0 ? -22 : 0;
        setPixel(d, ax, ay, 178+n+dark, 118+n+dark, 56+n+dark);
    }
}

static void genWoodSide(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 60) / 52 - 2;
        int wobble = prand(0, ty, 61) / 64 - 2;
        int stripe = ((tx + wobble) % 18);
        int dark = (stripe < 5) ? -26 : (stripe < 10) ? 0 : -14;
        setPixel(d, ax, ay, 165+n+dark, 108+n+dark, 50+n+dark);
    }
}

static void genLeaves(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int cx2 = tx / 8, cy2 = ty / 8;
        int cluster = prand(cx2, cy2, 70) % 3;
        int n = prand(ax, ay, 71) / 52 - 2;
        int r, g, b;
        if (cluster == 0)      { r=30; g=100; b=18; }   // deep shade
        else if (cluster == 1) { r=40; g=128; b=24; }   // mid
        else                   { r=55; g=158; b=32; }   // bright highlight
        setPixel(d, ax, ay, r+n, g+n, b+n);
    }
}

static void genSand(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 80) / 40 - 3;
        bool grain = (prand(ax, ay, 81) > 248);
        int dark = grain ? -22 : 0;
        setPixel(d, ax, ay, 218+n+dark, 196+n+dark, 112+n+dark);
    }
}

static void genGravel(std::vector<uint8_t>& d, int col, int row) {
    static const int CELL = 10;
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int bestDist2 = 9999, secondDist2 = 9999, bestCell = 0;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int gcx = tx/CELL+dx, gcy = ty/CELL+dy;
            int jx = prand(gcx, gcy, 90) % CELL;
            int jy = prand(gcx, gcy, 91) % CELL;
            int px2 = gcx*CELL+jx, py2 = gcy*CELL+jy;
            int d2 = (tx-px2)*(tx-px2)+(ty-py2)*(ty-py2);
            if (d2 < bestDist2) { secondDist2=bestDist2; bestDist2=d2; bestCell=gcx*100+gcy; }
            else if (d2 < secondDist2) secondDist2 = d2;
        }
        bool edge = (secondDist2 - bestDist2) < 4;
        int cv = 90 + (prand(bestCell%100, bestCell/100, 92) % 55);
        setPixel(d, ax, ay, cv+(edge?-42:0), cv+(edge?-42:0), cv+(edge?-42:0));
    }
}

static void genSnow(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 100) / 44 - 3;
        int s = (prand(ax, ay, 101) > 250) ? 15 : 0;
        setPixel(d, ax, ay, 235+n+s, 238+n+s, 250+n+s);
    }
}

static void genCactusTop(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        bool spine = (tx==TILE_PX/2 || tx==TILE_PX/2-1 || ty==TILE_PX/2 || ty==TILE_PX/2-1);
        setPixel(d, ax, ay, 30+(spine?-25:0), 105+(spine?-25:0), 22+(spine?-25:0));
    }
}

static void genCactusSide(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int ridge = (tx % 12 < 2) ? -22 : 0;
        int spine = (ty%14==6 && (tx==5||tx==TILE_PX-7)) ? -40 : 0;
        setPixel(d, ax, ay, 30+ridge+spine, 105+ridge+spine, 22+ridge+spine);
    }
}

static void genSandstone(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 130) / 52 - 2;
        bool sep = (ty%20==0 || ty%20==1);
        int br, bg, bb;
        switch ((ty/20) % 3) {
            case 0: br=205; bg=175; bb=90; break;
            case 1: br=218; bg=188; bb=100; break;
            default: br=198; bg=168; bb=84; break;
        }
        setPixel(d, ax, ay, br+n+(sep?-35:0), bg+n+(sep?-35:0), bb+n+(sep?-35:0));
    }
}

static void genIce(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        int n = prand(ax, ay, 140) / 52 - 2;
        int d1 = (tx+ty*2)%32, d2 = (tx*2-ty+64)%40;
        bool crack = (d1<2 && prand(tx/4,ty/4,141)>160) || (d2<2 && prand(tx/4,ty/4,142)>190);
        setPixel(d, ax, ay, 195+n+(crack?-55:0), 212+n+(crack?-55:0), 238+n+(crack?-55:0));
    }
}

static void genGlowstone(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        bool vein = ((tx+ty)%16<2) || ((tx*2-ty+64)%24<2);
        if (vein) setPixel(d, ax, ay, 255, 215, 70);
        else { int n=prand(ax,ay,150)/44-3; setPixel(d, ax, ay, 160+n, 100+n, 28+n); }
    }
}

static void genWater(std::vector<uint8_t>& d, int col, int row) {
    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {
        int ax = col*TILE_PX+tx, ay = row*TILE_PX+ty;
        float ripple = sinf((tx+ty*0.7f)*0.28f)*18.0f + sinf((tx*0.6f-ty)*0.22f)*10.0f;
        int n = prand(ax, ay, 160) / 52 - 2;
        setPixel(d, ax, ay,
            clamp8(20+n+(int)(ripple*0.2f)),
            clamp8(95+n+(int)(ripple*0.5f)),
            clamp8(210+n+(int)(ripple*0.3f)));
    }
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

    genGrassTop  (data, 0, 0);
    genGrassSide (data, 1, 0);
    genDirt      (data, 2, 0);
    genStone     (data, 3, 0);
    genWoodTop   (data, 0, 1);
    genWoodSide  (data, 1, 1);
    genLeaves    (data, 2, 1);
    genSand      (data, 3, 1);
    genGravel    (data, 0, 2);
    genSnow      (data, 1, 2);
    genCactusTop (data, 2, 2);
    genCactusSide(data, 3, 2);
    genSandstone (data, 0, 3);
    genIce       (data, 1, 3);
    genGlowstone (data, 2, 3);
    genWater     (data, 3, 3);
    // Foliage (row 4)
    genTallGrass   (data, 0, 4);
    genFlower      (data, 1, 4, 220, 48, 20);   // FlowerRed
    genFlower      (data, 2, 4, 240, 200, 20);  // FlowerYellow
    genFlower      (data, 3, 4, 75,  85, 230);  // FlowerBlue

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
