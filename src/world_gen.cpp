// Procedural world generation, split out of world.cpp: the Perlin noise field,
// the biome system + computeColumn terrain sampler, sampleSurface, the
// vegetation/rock decorator helpers and generateChunk (the per-chunk height /
// density / cave / decoration pipeline). world.cpp keeps live chunk meshing,
// lighting, rendering and the World container. Shared decls: world_internal.h.
#include "world.h"
#include "world_internal.h"
#include "town.h"
#include "noise.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>
#include <chrono>

static PerlinNoise gNoise(12345);
static PerlinNoise gTempNoise(54321);
static PerlinNoise gHumidNoise(98765);
static PerlinNoise gRiverNoise(11111);
static PerlinNoise gContinentalNoise(77777);
static unsigned int g_worldSeed = 12345;

void setWorldSeed(unsigned int seed) {
    g_worldSeed        = seed;
    gNoise             = PerlinNoise(seed);
    gTempNoise         = PerlinNoise(seed + 11111);
    gHumidNoise        = PerlinNoise(seed + 22222);
    gRiverNoise        = PerlinNoise(seed + 33333);
    gContinentalNoise  = PerlinNoise(seed + 44444);
}

unsigned int worldSeed() { return g_worldSeed; }

// ---- Biome system ----


// Biome-selection noise frequency. Lower = larger biomes, so the player travels
// further between them. The temperature/humidity climate fields are sampled at
// this scale; it is deliberately close to (but independent of) the macro
// elevation frequency so climate and terrain don't move in lock-step.
static constexpr float BIOME_FREQ = 0.00035f;
// Gain applied to the climate fields before biome weighting. >1 pushes values
// toward the extremes so the hot/cold bands are actually reached and the corner
// biomes — desert (hot/dry), jungle (hot/wet), tundra (cold/dry) — win real
// territory. Biome selection is nearest-ideal (a Voronoi partition of the
// temp/humid square), so the ideals below are placed to tile the reachable
// climate range and this gain controls how much of that range is visited.
static constexpr float BIOME_SPREAD = 1.60f;

// Ocean shaping. Height below sea level is multiplied by this so basins drop
// away steeply and the floor sinks far out of sight; coastlines (barely below
// sea level) stay shallow, so only genuine ocean deepens. The floor is clamped
// so there is always rock — and room for caves — beneath the deepest sea.
static constexpr float OCEAN_DEPTH_SCALE = 2.6f;
static constexpr int   OCEAN_FLOOR_MIN_Y = 20;

// Surface dressing applied in the top-down surface pass:
//  - a sandy beach band just above sea level wherever grassy land meets water;
//  - an altitude snow line so the highest peaks go white in any biome.
static constexpr int BEACH_TOP_Y = SEA_LEVEL + 1;   // a thin sandy strip at the waterline
static constexpr int SNOW_LINE_Y = 120;             // upper mountains; ~top few % of land

static constexpr int NUM_BIOMES = 7;

struct BiomeDef {
    float idealTemp, idealHumid; // centre in [0,1]×[0,1] temperature-humidity space
    float freq;                  // 2-D heightmap noise frequency
    float amplitude;             // height-noise multiplier (world Y units)
    int   octaves;
    float persistence;
    BlockType surfaceBlock;
    BlockType subSurfaceBlock;
};

// Amplitudes here are LOCAL DETAIL added on top of the macro elevation base.
// The macro noise (~±160 blocks) already provides the large mountain ranges.
// Ideal temp/humid are placed to tile the reachable climate square into three
// temperature bands — cold (~0.24), temperate (~0.50), hot (~0.76) — so every
// biome, including the hot desert/jungle and the cold tundra, gets a sizeable
// region the player travels between. (Selection is nearest-ideal, so these are
// effectively Voronoi seeds.)
static const BiomeDef BIOMES[NUM_BIOMES] = {
  // temp   humid  freq     amp    oct pers   surf                   sub
    {0.50f, 0.30f, 0.006f,  3.0f,  3, 0.35f, BlockType::Grass,      BlockType::Dirt      }, // Plains    — temperate, drier
    {0.50f, 0.75f, 0.009f, 12.0f,  5, 0.55f, BlockType::Grass,      BlockType::Dirt      }, // Forest    — temperate, wet
    {0.78f, 0.22f, 0.007f,  6.0f,  3, 0.40f, BlockType::Sand,       BlockType::Sandstone }, // Desert    — hot, dry
    {0.25f, 0.62f, 0.014f, 25.0f,  8, 0.68f, BlockType::Snow,       BlockType::Stone     }, // Mountains — cold, wet
    {0.22f, 0.25f, 0.012f, 22.0f,  7, 0.65f, BlockType::Snow,       BlockType::Stone     }, // Tundra    — cold, dry
    {0.74f, 0.50f, 0.006f,  5.0f,  3, 0.40f, BlockType::Grass,      BlockType::Dirt      }, // Savanna   — hot, mid
    {0.78f, 0.80f, 0.010f, 16.0f,  6, 0.60f, BlockType::Grass,      BlockType::Dirt      }, // Jungle    — hot, wet
};

// ---- Column height/biome helper (used by both Pass 0 and the cross-chunk decorator pass) ----


ColumnInfo computeColumn(float wx, float wz) {
    float macroRaw = gContinentalNoise.octave(wx * 0.00035f, wz * 0.00035f, 5, 0.55f, 2.0f);
    float macroH   = (float)SEA_LEVEL + macroRaw * 160.0f;

    float temp  = gTempNoise .octave(wx * BIOME_FREQ,          wz * BIOME_FREQ,          2, 0.5f, 2.0f) * (0.5f * BIOME_SPREAD) + 0.5f;
    float humid = gHumidNoise.octave(wx * BIOME_FREQ + 100.0f, wz * BIOME_FREQ + 100.0f, 2, 0.5f, 2.0f) * (0.5f * BIOME_SPREAD) + 0.5f;

    float weights[NUM_BIOMES], wTotal = 0.0f;
    int   domIdx = 0;
    for (int i = 0; i < NUM_BIOMES; i++) {
        float dt = temp - BIOMES[i].idealTemp, dh = humid - BIOMES[i].idealHumid;
        weights[i] = expf(-10.0f * (dt*dt + dh*dh));
        wTotal += weights[i];
        if (weights[i] > weights[domIdx]) domIdx = i;
    }
    float elevNorm = std::clamp((macroH - (float)SEA_LEVEL) / 140.0f, -1.0f, 1.0f);
    if (elevNorm > 0.25f) {
        float snowBias = std::min((elevNorm - 0.25f) / 0.75f, 1.0f);
        float snowMult = 1.0f + snowBias * 40.0f;
        weights[(int)Biome::Mountains] *= snowMult;
        weights[(int)Biome::Tundra]    *= snowMult;
        float suppress = std::max(0.0f, 1.0f - snowBias * 3.0f);
        for (int i = 0; i < NUM_BIOMES; i++)
            if (i != (int)Biome::Mountains && i != (int)Biome::Tundra) weights[i] *= suppress;
        wTotal = 0.0f; domIdx = 0;
        for (int i = 0; i < NUM_BIOMES; i++) { wTotal += weights[i]; if (weights[i] > weights[domIdx]) domIdx = i; }
    }
    float blendH = macroH;
    for (int i = 0; i < NUM_BIOMES; i++) {
        float w = weights[i] / wTotal;
        if (w < 0.005f) continue;
        float h = gNoise.octave(wx * BIOMES[i].freq, wz * BIOMES[i].freq, BIOMES[i].octaves, BIOMES[i].persistence, 2.0f);
        blendH += w * h * BIOMES[i].amplitude;
    }
    // Deepen the oceans. Everything below sea level is pushed further down so
    // basins become too deep to see the bottom, while the shore stays shallow.
    // Done here — before the river/ravine carving below, which only acts at or
    // above sea level — so rivers remain shallow streams rather than chasms.
    if (blendH < (float)SEA_LEVEL) {
        float below = (float)SEA_LEVEL - blendH;
        blendH = std::max((float)SEA_LEVEL - below * OCEAN_DEPTH_SCALE,
                          (float)OCEAN_FLOOR_MIN_Y);
    }
    // Ravine rivers — kept infrequent, with smooth (not cliff-like) valley
    // walls: a lower noise frequency widens each valley, a narrower threshold
    // makes them rarer, and a smoothstep profile gives gentle rims and floors.
    float ridgeN = std::abs(gRiverNoise.octave(wx * 0.0045f + 777.0f, wz * 0.0045f + 777.0f, 3, 0.5f, 2.0f));
    if (ridgeN < 0.065f && blendH > (float)(SEA_LEVEL + 1)) {
        float t     = (0.065f - ridgeN) / 0.065f;        // 0 at the rim, 1 at the centre
        float carve = t * t * (3.0f - 2.0f * t);         // smoothstep — gentle rim and floor
        blendH -= carve * 13.0f;
        blendH = std::max(blendH, (float)(SEA_LEVEL - 3));
    }
    float riverN = gRiverNoise.octave(wx * 0.005f, wz * 0.005f, 2, 0.5f, 2.0f);
    if (std::abs(riverN) < 0.035f && blendH > SEA_LEVEL - 6 && blendH < SEA_LEVEL + 50) {
        float riverDepth = (0.035f - std::abs(riverN)) / 0.035f;
        blendH = std::min(blendH, (float)(SEA_LEVEL - 1) - riverDepth * 4.0f);
    }
    // Level the land under settlements (no-op until the town plan is built).
    blendH = townFlattenedHeight(wx, wz, blendH);
    return { blendH, (Biome)domIdx };
}

// Terrain oracle exposed for the town planner — surface height + biome at any
// world XZ, with no chunk generation.
SurfaceSample sampleSurface(int wx, int wz) {
    ColumnInfo ci = computeColumn((float)wx, (float)wz);
    return { (int)ci.surfH, (int)ci.biome };
}

// The actual top-solid block Y at a column. computeColumn() yields only the
// blended target height; Pass 1's 3D density field shifts the real surface
// several blocks off it. Replaying that crossing lets props rest on the
// ground instead of on the predicted height.
int sampleSurfaceSolid(int wx, int wz) {
    float surfH = computeColumn((float)wx, (float)wz).surfH;
    int hi = std::min(CHUNK_HEIGHT - 1, (int)surfH + 24);
    int lo = std::max(1, (int)surfH - 24);
    for (int y = hi; y >= lo; y--) {
        float d3   = gNoise.octave((float)wx * 0.012f * 2.0f, (float)y * 0.05f,
                                   (float)wz * 0.012f * 2.0f, 4, 0.5f, 2.0f);
        float bias = (surfH - (float)y) * 0.10f;
        if (d3 + bias > 0.0f) return y;
    }
    return (int)surfH;
}

// ---- Decorator helpers ----
// All functions take WORLD coordinates (wx, wz) for the anchor position.
// c->set() silently ignores coordinates outside the chunk, so structures that
// straddle a chunk seam are written correctly when the neighbour also processes
// the same anchor.

static void tryPlaceTree(Chunk* c, int wx, int wz, int top,
                         float n, float n2, float thresh, BlockType leafType) {
    if (n < thresh) return;
    float t = std::clamp((n  - thresh) / (1.0f - thresh), 0.0f, 1.0f);
    float s = std::clamp(n2 * 0.5f + 0.5f, 0.0f, 1.0f);

    auto rng = [wx, wz](int salt) -> uint32_t {
        uint32_t v = (uint32_t)(wx * 1619 + wz * 31337 + salt * 6271);
        v ^= (v >> 16); v *= 0x45d9f3bu; return v ^ (v >> 16);
    };

    // Huge size range: tiny saplings up to towering giants
    int trunkH = 8 + (int)(t * 55.0f + s * 27.0f);  // 8–90
    trunkH = std::min(trunkH, CHUNK_HEIGHT - top - 5);
    if (trunkH < 5) return;

    int trunkR = (trunkH >= 60) ? 2 : (trunkH >= 25) ? 1 : 0;
    int flareR = trunkR + 1;

    // Chunk-local base (may be outside [0, CHUNK_SIZE) — c->set() handles that)
    int lx = wx - c->pos.x * CHUNK_SIZE;
    int lz = wz - c->pos.z * CHUNK_SIZE;
    int trunkTop = top + trunkH;

    // Root flare
    for (int ty = top + 1; ty <= top + 2 && ty < CHUNK_HEIGHT; ty++) {
        int r = std::max(flareR - (ty - top - 1), trunkR);
        for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (std::abs(dx) == r && std::abs(dz) == r && r > 0) continue;
            c->set(lx + dx, ty, lz + dz, BlockType::Wood);
        }
    }
    // Main trunk
    for (int ty = top + 3; ty <= trunkTop && ty < CHUNK_HEIGHT; ty++)
    for (int dx = -trunkR; dx <= trunkR; dx++)
    for (int dz = -trunkR; dz <= trunkR; dz++)
        c->set(lx + dx, ty, lz + dz, BlockType::Wood);

    // Branches from upper 35% — primary visual feature
    static const int8_t DIRS8[8][2] = {
        {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}
    };
    const int branchStart  = top + (trunkH * 65) / 100;
    const int branchEnd    = top + (trunkH * 95) / 100;
    const int numBranches  = 10 + (int)(t * 8.0f + s * 6.0f);      // 10–24
    const int maxBlen      = 5 + (int)(trunkH * 0.15f);             // scales with height

    for (int bi = 0; bi < numBranches; bi++) {
        int ty   = branchStart + (int)((rng(bi) & 0xFF) / 255.0f * (branchEnd - branchStart));
        int dir  = (rng(bi + 100) >> 8) & 7;
        int blen = std::max(4, 3 + (int)((rng(bi + 200) & 0xF) / 15.0f * maxBlen));
        int rise = 1 + (int)((rng(bi + 300) & 0x7) / 7.0f * 5.0f);
        int ddx  = DIRS8[dir][0], ddz = DIRS8[dir][1];

        for (int i = 1; i <= blen; i++) {
            int bx = lx + ddx*i, by = ty + (rise*i + blen/2)/blen, bz = lz + ddz*i;
            if (by >= CHUNK_HEIGHT) break;
            c->set(bx, by, bz, BlockType::Wood);
        }
        int tipX = lx + ddx*blen, tipY = ty + rise, tipZ = lz + ddz*blen;
        int leafR = 3 + (int)((rng(bi + 400) & 3) / 3.0f * 2.0f);  // 3–5
        int leafH = 2 + (int)((rng(bi + 500) & 1));                   // 2–3
        for (int llx = -leafR; llx <= leafR; llx++)
        for (int llz = -leafR; llz <= leafR; llz++)
        for (int ly = -leafH; ly <= leafH + 1; ly++) {
            float ex = (float)(llx*llx + llz*llz) / (float)(leafR * leafR);
            float ey = (float)(ly * ly) / (float)((leafH + 1) * (leafH + 1));
            if (ex + ey > 1.0f) continue;
            int bx = tipX + llx, by = tipY + ly, bz = tipZ + llz;
            if (by > 0 && by < CHUNK_HEIGHT) {
                BlockType eb = c->get(bx, by, bz);
                if (eb == BlockType::Air || isAnyLeaves(eb))
                    c->set(bx, by, bz, leafType);
            }
        }
    }

    // Small tuft at the very top
    for (int llx = -2; llx <= 2; llx++)
    for (int llz = -2; llz <= 2; llz++)
    for (int ly = 0; ly <= 2; ly++) {
        if (std::abs(llx) == 2 && std::abs(llz) == 2) continue;
        int bx = lx + llx, by = trunkTop + ly, bz = lz + llz;
        if (by > 0 && by < CHUNK_HEIGHT) {
            BlockType eb = c->get(bx, by, bz);
            if (eb == BlockType::Air || isAnyLeaves(eb))
                c->set(bx, by, bz, leafType);
        }
    }
}

// Tall conifer — world coords
static void tryPlacePineTree(Chunk* c, int wx, int wz, int top,
                              float n, float n2, float thresh) {
    if (n < thresh) return;
    float t = std::clamp((n - thresh) / (1.0f - thresh), 0.0f, 1.0f);
    float s = std::clamp(n2 * 0.5f + 0.5f, 0.0f, 1.0f);

    auto rng = [wx, wz](int salt) -> uint32_t {
        uint32_t v = (uint32_t)(wx * 1619 + wz * 31337 + salt * 6271);
        v ^= (v >> 16); v *= 0x45d9f3bu; return v ^ (v >> 16);
    };

    int trunkH = 16 + (int)(t * 40.0f + s * 20.0f);  // 16–76
    trunkH = std::min(trunkH, CHUNK_HEIGHT - top - 5);
    if (trunkH < 8) return;

    int lx = wx - c->pos.x * CHUNK_SIZE;
    int lz = wz - c->pos.z * CHUNK_SIZE;

    for (int ty = top + 1; ty <= top + trunkH && ty < CHUNK_HEIGHT; ty++)
        c->set(lx, ty, lz, BlockType::Wood);

    int maxR = 5 + (int)(t * 3.0f + s);
    int coneApex = top + trunkH + 1;
    int coneBase = top + trunkH / 4;
    int coneH    = coneApex - coneBase;

    for (int ty = coneBase; ty <= coneApex && ty < CHUNK_HEIGHT; ty++) {
        float progress = (float)(coneApex - ty) / (float)coneH;
        int r = (int)(progress * maxR);
        for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (std::abs(dx) == r && std::abs(dz) == r && r > 1) continue;
            if (c->get(lx + dx, ty, lz + dz) == BlockType::Air)
                c->set(lx + dx, ty, lz + dz, BlockType::Leaves);
        }
    }

    static const int8_t CARD[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    int tierStep = 3 + (int)s;
    for (int ty = coneBase + 1; ty < coneApex - 1; ty += tierStep) {
        float progress = (float)(coneApex - ty) / (float)coneH;
        int rHere = (int)(progress * maxR);
        if (rHere < 2) continue;
        int blen = std::min(rHere - 1, 2 + (int)(t * 2.0f));
        int d0 = (int)(rng(ty) & 3), d1 = (d0 + 2) & 3;
        for (int d : {d0, d1}) {
            for (int i = 1; i <= blen; i++) {
                int bx = lx + CARD[d][0]*i, bz = lz + CARD[d][1]*i;
                int by = ty - (i*2 >= blen ? 1 : 0);
                if (by < 0 || by >= CHUNK_HEIGHT) break;
                c->set(bx, by, bz, BlockType::Wood);
            }
        }
    }
}

static void tryPlaceBush(Chunk* c, int wx, int wz, int top, float n, float thresh,
                         BlockType leafType) {
    if (n < thresh) return;
    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;
    int height = (n > thresh + 0.06f) ? 2 : 1;
    for (int h = 1; h <= height; h++) {
        int by = top + h; if (by >= CHUNK_HEIGHT) break;
        int rad = (h == 1) ? 1 : 0;
        for (int dx = -rad; dx <= rad; dx++) for (int dz = -rad; dz <= rad; dz++) {
            BlockType eb = c->get(lx+dx, by, lz+dz);
            if (eb == BlockType::Air || isAnyLeaves(eb))
                c->set(lx+dx, by, lz+dz, leafType);
        }
    }
}

static void tryPlaceCactus(Chunk* c, int wx, int wz, int top, float n, float thresh) {
    if (n < thresh) return;
    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;
    int height = std::clamp(1 + (int)((n - thresh) * 12.0f), 1, 3);
    for (int ty = top+1; ty <= top+height && ty < CHUNK_HEIGHT; ty++)
        c->set(lx, ty, lz, BlockType::Cactus);
}

static void tryPlaceRockFormation(Chunk* c, int wx, int wz, int top, float n, float thresh) {
    if (n < thresh) return;
    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;
    int height = std::clamp(2 + (int)((n - thresh) * 22.0f), 2, 5);
    for (int ty = top+1; ty <= top+height && ty < CHUNK_HEIGHT; ty++)
        c->set(lx, ty, lz, BlockType::Stone);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        if (dx==0 && dz==0) continue;
        if (top+1 < CHUNK_HEIGHT && c->get(lx+dx, top+1, lz+dz) == BlockType::Air)
            c->set(lx+dx, top+1, lz+dz, BlockType::Stone);
    }
}

static void tryPlaceStoneSpire(Chunk* c, int wx, int wz, int top, float n, float thresh) {
    if (n < thresh || top < SEA_LEVEL + 80) return;
    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;
    int height = std::clamp(3 + (int)((n - thresh) * 35.0f), 3, 9);
    for (int ty = top+1; ty <= top+height && ty < CHUNK_HEIGHT; ty++)
        c->set(lx, ty, lz, BlockType::Stone);
}

static void tryPlaceBoulder(Chunk* c, int wx, int wz, int top, float n, float thresh) {
    if (n < thresh) return;
    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;
    int rad = (n > thresh + 0.04f) ? 1 : 0, height = (n > thresh + 0.07f) ? 2 : 1;
    for (int h = 1; h <= height; h++) {
        int by = top + h; if (by >= CHUNK_HEIGHT) break;
        int r = (h == 1) ? rad : 0;
        for (int dx = -r; dx <= r; dx++) for (int dz = -r; dz <= r; dz++)
            if (c->get(lx+dx, by, lz+dz) == BlockType::Air)
                c->set(lx+dx, by, lz+dz, BlockType::Stone);
    }
}

// Savanna acacia — world coords
static void tryPlaceAcaciaTree(Chunk* c, int wx, int wz, int top,
                                float n, float n2, float thresh) {
    if (n < thresh) return;
    float t = std::clamp((n - thresh) / (1.0f - thresh), 0.0f, 1.0f);
    float s = std::clamp(n2 * 0.5f + 0.5f, 0.0f, 1.0f);
    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;

    int trunkH = 6 + (int)(t * 8.0f + s * 4.0f);
    trunkH = std::min(trunkH, CHUNK_HEIGHT - top - 5);
    for (int ty = top + 1; ty <= top + trunkH && ty < CHUNK_HEIGHT; ty++)
        c->set(lx, ty, lz, BlockType::Wood);

    int canopyR = 3 + (int)(t * 2.0f + s);
    for (int pass = 0; pass < 3; pass++) {
        int ly = top + trunkH + 1 + pass;
        int r  = (pass == 0) ? canopyR : (pass == 1) ? canopyR - 1 : canopyR / 2;
        if (ly >= CHUNK_HEIGHT || r <= 0) break;
        for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (dx*dx + dz*dz > r*r) continue;
            if (c->get(lx+dx, ly, lz+dz) == BlockType::Air)
                c->set(lx+dx, ly, lz+dz, BlockType::Leaves);
        }
    }
}

// Jungle tree — world coords, very tall
static void tryPlaceJungleTree(Chunk* c, int wx, int wz, int top,
                                float n, float n2, float thresh) {
    if (n < thresh) return;
    float t = std::clamp((n - thresh) / (1.0f - thresh), 0.0f, 1.0f);
    float s = std::clamp(n2 * 0.5f + 0.5f, 0.0f, 1.0f);

    auto rng = [wx, wz](int salt) -> uint32_t {
        uint32_t v = (uint32_t)(wx * 1619 + wz * 31337 + salt * 6271);
        v ^= (v >> 16); v *= 0x45d9f3bu; return v ^ (v >> 16);
    };

    int trunkR = (t > 0.3f) ? 1 : 0;
    int trunkH = 18 + (int)(t * 50.0f + s * 25.0f);  // 18–93
    trunkH = std::min(trunkH, CHUNK_HEIGHT - top - 5);
    if (trunkH < 10) return;

    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;

    for (int ty = top + 1; ty <= top + trunkH && ty < CHUNK_HEIGHT; ty++)
    for (int dx = -trunkR; dx <= trunkR; dx++)
    for (int dz = -trunkR; dz <= trunkR; dz++)
        c->set(lx+dx, ty, lz+dz, BlockType::Wood);

    static const int8_t DIRS8[8][2] = {
        {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}
    };
    const int branchStart = top + trunkH / 2, branchEnd = top + (trunkH * 9) / 10;
    const int numBranches = 3 + (int)(t * 4.0f);
    const int maxBlen = 4 + (int)(trunkH * 0.12f);

    for (int bi = 0; bi < numBranches; bi++) {
        int ty   = branchStart + (int)((rng(bi) & 0xFF) / 255.0f * (branchEnd - branchStart));
        int dir  = (rng(bi+100) >> 8) & 7;
        int blen = std::max(4, 3 + (int)((rng(bi+200) & 7) / 7.0f * maxBlen));
        int rise = 1 + (int)((rng(bi+300) & 3) / 3.0f * 3.0f);
        int ddx  = DIRS8[dir][0], ddz = DIRS8[dir][1];

        for (int i = 1; i <= blen; i++) {
            int bx = lx + ddx*i, by = ty + (rise*i)/blen, bz = lz + ddz*i;
            if (by >= CHUNK_HEIGHT) break;
            c->set(bx, by, bz, BlockType::Wood);
        }
        int tipX = lx + ddx*blen, tipY = ty + rise, tipZ = lz + ddz*blen;
        for (int llx = -3; llx <= 3; llx++)
        for (int llz = -3; llz <= 3; llz++)
        for (int ly = -1; ly <= 3; ly++) {
            if ((float)(llx*llx + llz*llz)/9.0f + (float)(ly*ly)/4.0f > 1.0f) continue;
            int bx = tipX+llx, by = tipY+ly, bz = tipZ+llz;
            if (by > 0 && by < CHUNK_HEIGHT && c->get(bx, by, bz) == BlockType::Air)
                c->set(bx, by, bz, BlockType::Leaves);
        }
    }

    const int cR = 5 + (int)(t * 3.0f + s), cH = cR - 1, cCtr = top + trunkH;
    for (int llx = -cR; llx <= cR; llx++)
    for (int llz = -cR; llz <= cR; llz++)
    for (int ly = -(cH+1); ly <= cH; ly++) {
        if ((float)(llx*llx + llz*llz)/(float)(cR*cR) + (float)(ly*ly)/(float)(cH*cH) > 1.0f) continue;
        int bx = lx+llx, by = cCtr+ly, bz = lz+llz;
        if (by > 0 && by < CHUNK_HEIGHT && c->get(bx, by, bz) == BlockType::Air)
            c->set(bx, by, bz, BlockType::Leaves);
    }
}

static void tryPlaceJungleBush(Chunk* c, int wx, int wz, int top, float n, float thresh) {
    if (n < thresh) return;
    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) for (int ly = 1; ly <= 2; ly++) {
        if (ly == 2 && (std::abs(dx) == 1 || std::abs(dz) == 1)) continue;
        int by = top + ly;
        if (by < CHUNK_HEIGHT && c->get(lx+dx, by, lz+dz) == BlockType::Air)
            c->set(lx+dx, by, lz+dz, BlockType::Leaves);
    }
}

static void tryPlaceIceSpike(Chunk* c, int wx, int wz, int top, float n, float thresh) {
    if (n < thresh) return;
    int lx = wx - c->pos.x * CHUNK_SIZE, lz = wz - c->pos.z * CHUNK_SIZE;
    int height = std::clamp(3 + (int)((n - thresh) * 32.0f), 3, 9);
    for (int ty = top+1; ty <= top+height && ty < CHUNK_HEIGHT; ty++)
        c->set(lx, ty, lz, BlockType::Ice);
}

// ---- Generation ----

void generateChunk(Chunk* c) {
    const int ox = c->pos.x * CHUNK_SIZE;
    const int oz = c->pos.z * CHUNK_SIZE;

    // Build the town plan before Pass 0 so the terrain oracle flattens the land
    // under settlements while the chunk's heightmap is computed.
    getTownPlan();

    // Pass 0: per-column biome weights → blended surface height + dominant biome
    float surfH_f[CHUNK_SIZE][CHUNK_SIZE];
    Biome dominant[CHUNK_SIZE][CHUNK_SIZE];

    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            auto info = computeColumn((float)(ox + x), (float)(oz + z));
            surfH_f[x][z]  = info.surfH;
            dominant[x][z] = info.biome;
        }

    // Pass 1: 3D density field → Stone/Gravel/Air, with cave carving
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            float wx     = (ox + x) * 0.012f;
            float wz     = (oz + z) * 0.012f;
            float surfH  = surfH_f[x][z];
            int   surfHi = std::clamp((int)surfH, 4, CHUNK_HEIGHT - 12);

            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                float d3   = gNoise.octave(wx * 2.0f, y * 0.05f, wz * 2.0f, 4, 0.5f, 2.0f);
                float bias = (surfH - y) * 0.10f;
                if (d3 + bias <= 0.0f) continue;

                if (y > 3 && y < surfHi - 2) {
                    float cave1 = gNoise.octave(wx * 3.5f + 50.0f, y * 0.12f, wz * 3.5f + 50.0f, 3, 0.5f);
                    if (cave1 > 0.36f) continue;
                    float cave2 = gNoise.octave(wx * 3.0f - 80.0f, y * 0.15f + 30.0f, wz * 3.0f - 80.0f, 2, 0.6f);
                    if (cave2 > 0.40f) continue;
                }

                BlockType bt = BlockType::Stone;
                if (y < 30) {
                    float grv = gNoise.octave(wx * 5.0f + 200.0f, (float)y * 0.2f, wz * 5.0f + 200.0f, 2);
                    if (grv > 0.30f) bt = BlockType::Gravel;
                }
                // Rare glowstone veins deep underground
                if (y > 4 && y < 50) {
                    float gn = gNoise.noise(wx * 0.11f + 333.0f, y * 0.11f + 333.0f, wz * 0.11f + 333.0f);
                    if (gn > 0.44f) bt = BlockType::Glowstone;
                }
                c->set(x, y, z, bt);
            }
        }
    }

    // Pass 1.5: hard town-level — within a town's flat zone (settlement
    // footprint + a buffer), force the terrain top to be exactly the town's
    // baseY. The density pass only *biases* toward townFlattenedHeight, so
    // the actual surface still wobbles a few blocks; without this snap, the
    // gravel path traced over the surface ends up at a slightly different
    // height to the house foundation, and the path can blockade the front
    // door. We fill stone up to baseY and clear any blocks above so the
    // surface pass that follows lays grass / snow on a perfectly flat top.
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int flatY = townFlatLevelAt(ox + x, oz + z);
            if (flatY < 1) continue;
            for (int y = 1; y <= flatY && y < CHUNK_HEIGHT; y++) {
                BlockType cur = c->get(x, y, z);
                if (cur == BlockType::Air || cur == BlockType::Water)
                    c->set(x, y, z, BlockType::Stone);
            }
            for (int y = flatY + 1; y < CHUNK_HEIGHT; y++) {
                BlockType cur = c->get(x, y, z);
                if (cur == BlockType::Air) continue;
                if (isAnyLeaves(cur)) continue;
                if (cur == BlockType::Wood || cur == BlockType::Cactus) continue;
                c->set(x, y, z, BlockType::Air);
            }
        }
    }

    // Pass 2: top-down surface scan — biome-specific surface/subsurface blocks
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            const BiomeDef& bd = BIOMES[(int)dominant[x][z]];
            int depthFromAir = 0;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                BlockType bt = c->get(x, y, z);
                if (bt == BlockType::Air || isAnyLeaves(bt)) {
                    depthFromAir = 0;
                    continue;
                }
                if (bt != BlockType::Stone && bt != BlockType::Gravel) {
                    depthFromAir++;
                    continue;
                }
                depthFromAir++;
                bool aboveSea = (y >= SEA_LEVEL);
                // A grassy column qualifies for a sandy beach where it sits in the
                // narrow band just above sea level — i.e. right at the shore.
                bool beach = aboveSea && y <= BEACH_TOP_Y &&
                             bd.surfaceBlock == BlockType::Grass;
                bool snowcap = aboveSea && y >= SNOW_LINE_Y;   // alpine snow on any peak
                if (depthFromAir == 1) {
                    BlockType surf = !aboveSea ? BlockType::Sand
                                   : snowcap   ? BlockType::Snow
                                   : beach     ? BlockType::Sand
                                               : bd.surfaceBlock;
                    c->set(x, y, z, surf);
                } else if (depthFromAir <= 5) {
                    BlockType sub = (!aboveSea || beach) ? BlockType::Sand
                                  : snowcap              ? BlockType::Stone
                                                         : bd.subSurfaceBlock;
                    c->set(x, y, z, sub);
                } else {
                    break;
                }
            }
        }
    }

    // Build top-solid map for decorator pass
    int topSolid[CHUNK_SIZE][CHUNK_SIZE];
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            topSolid[x][z] = -1;
            for (int y = CHUNK_HEIGHT-1; y >= 0; y--)
                if (c->get(x,y,z) != BlockType::Air) { topSolid[x][z] = y; break; }
        }

    // Pass 3: decorators over a padded region.
    // Large trees can extend 20+ blocks from their anchor in XZ, so each chunk
    // must also process anchor positions from neighbouring chunks to fill in the
    // parts of those trees that land inside this chunk.  c->set() ignores writes
    // that are out of this chunk's bounds, so only the correct voxels are written.
    static constexpr int DECO_PAD = 24; // worst-case XZ reach of any structure
    for (int rx = -DECO_PAD; rx < CHUNK_SIZE + DECO_PAD; rx++) {
        for (int rz = -DECO_PAD; rz < CHUNK_SIZE + DECO_PAD; rz++) {
            int wwx = ox + rx, wwz = oz + rz;

            int        top;
            Biome      biome;
            BlockType  topBlock;

            bool interior = (rx >= 0 && rx < CHUNK_SIZE && rz >= 0 && rz < CHUNK_SIZE);
            if (interior) {
                top = topSolid[rx][rz];
                if (top < 0 || top < SEA_LEVEL) continue;
                biome    = dominant[rx][rz];
                topBlock = c->get(rx, top, rz);
            } else {
                // Cheap early-out: skip if tree noise is below the lowest threshold.
                // This avoids calling computeColumn for the majority of exterior positions.
                float earlyN = gNoise.noise(wwx * 0.090f, wwz * 0.090f);
                if (earlyN < 0.48f) continue; // lowest tree threshold is 0.50

                auto info = computeColumn((float)wwx, (float)wwz);
                top = (int)info.surfH;
                if (top < SEA_LEVEL) continue;
                biome    = info.biome;
                topBlock = (top >= SEA_LEVEL) ? BIOMES[(int)biome].surfaceBlock
                                              : BlockType::Sand;
            }

            float n1 = gNoise.noise(wwx * 0.090f,           wwz * 0.090f);
            float n2 = gNoise.noise(wwx * 0.110f + 500.0f,  wwz * 0.110f + 500.0f);
            float n3 = gNoise.noise(wwx * 0.070f + 1000.0f, wwz * 0.070f + 1000.0f);

            switch (biome) {
                case Biome::Plains:
                    if (topBlock == BlockType::Grass) {
                        tryPlaceTree(c, wwx, wwz, top, n1, n2, 0.75f, BlockType::Leaves);
                        tryPlaceBush(c, wwx, wwz, top, n3, 0.72f, BlockType::Leaves);
                    }
                    break;
                case Biome::Forest: {
                    if (topBlock == BlockType::Grass) {
                        // Smooth noise zones → each region of forest is one colour
                        float ln = gNoise.noise(wwx * 0.020f + 777.7f, wwz * 0.020f + 777.7f);
                        BlockType ltype = (ln >  0.50f) ? BlockType::LeavesPink   :
                                          (ln >  0.08f) ? BlockType::LeavesOrange :
                                          (ln > -0.35f) ? BlockType::Leaves       :
                                                          BlockType::LeavesRed;
                        tryPlaceTree(c, wwx, wwz, top, n1, n2, 0.50f, ltype);
                        tryPlaceBush(c, wwx, wwz, top, n3, 0.65f, ltype);
                    }
                    break;
                }
                case Biome::Desert:
                    if (interior && topBlock == BlockType::Sand) {
                        tryPlaceCactus       (c, wwx, wwz, top, n1, 0.80f);
                        tryPlaceRockFormation(c, wwx, wwz, top, n3, 0.88f);
                    }
                    break;
                case Biome::Mountains:
                    if (topBlock == BlockType::Snow || topBlock == BlockType::Stone) {
                        tryPlacePineTree  (c, wwx, wwz, top, n1, n2, 0.75f);
                        if (interior) tryPlaceStoneSpire(c, wwx, wwz, top, n3, 0.87f);
                    }
                    break;
                case Biome::Tundra:
                    if (topBlock == BlockType::Snow || topBlock == BlockType::Stone) {
                        tryPlacePineTree(c, wwx, wwz, top, n1, n2, 0.80f);
                        if (interior) {
                            tryPlaceBoulder (c, wwx, wwz, top, n3, 0.76f);
                            tryPlaceIceSpike(c, wwx, wwz, top, n3, 0.91f);
                        }
                    }
                    break;
                case Biome::Savanna:
                    if (topBlock == BlockType::Grass) {
                        tryPlaceAcaciaTree   (c, wwx, wwz, top, n1, n2, 0.82f);
                        if (interior) tryPlaceRockFormation(c, wwx, wwz, top, n3, 0.86f);
                    }
                    break;
                case Biome::Jungle:
                    if (topBlock == BlockType::Grass) {
                        tryPlaceJungleTree(c, wwx, wwz, top, n1, n2, 0.58f);
                        tryPlaceJungleBush(c, wwx, wwz, top, n3, 0.50f);
                    }
                    break;
            }
        }
    }

    // Pass 4: water fill — columns with topSolid below sea level get filled with water
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int top = topSolid[x][z];
            if (top >= SEA_LEVEL) continue;
            for (int y = top + 1; y <= SEA_LEVEL; y++) {
                if (y >= CHUNK_HEIGHT) break;
                if (c->get(x, y, z) == BlockType::Air)
                    c->set(x, y, z, BlockType::Water);
            }
        }
    }

    // Cache surface for world map (reads topSolid which is still valid post water-fill)
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int idx = z * CHUNK_SIZE + x;
            int ts = topSolid[x][z];
            if (ts >= 0 && ts < SEA_LEVEL) ts = SEA_LEVEL; // water column surface = sea level
            c->surfaceY[idx]  = (int16_t)ts;
            c->surfaceBT[idx] = (ts >= 0) ? (uint8_t)c->get(x, ts, z) : (uint8_t)BlockType::Air;
        }
    }
    c->surfaceReady = true;

    // Pass 5: stamp procedural town / village features that fall in this chunk.
    stampTownChunk(c);

    c->computeLight();
    c->state = ChunkState::Generated;
}

