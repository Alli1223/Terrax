#include "vegetation.h"
#include <cmath>

// Every plant is built from little axis-aligned cubes (voxels), to match the
// game's blocky art. Cube faces carry a fixed top/side/bottom shade for the
// classic voxel read; the shader adds dynamic light on top.

namespace {

constexpr float TAU = 6.28318530718f;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// --- Deterministic per-instance RNG (xorshift32) ----------------------------
struct VRng {
    uint32_t s;
    explicit VRng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (float)(u() & 0xFFFFFFu) / (float)0x1000000; }
    float range(float a, float b) { return a + (b - a) * f(); }
    int   irange(int a, int b)    { return a + (int)(f() * (float)(b - a + 1)); }
};

uint32_t hashXZ(int wx, int wz) {
    uint32_t h = (uint32_t)wx * 374761393u + (uint32_t)wz * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

glm::vec3 jitterCol(glm::vec3 c, VRng& r, float amt) {
    return glm::vec3(c.r + r.range(-amt, amt),
                     c.g + r.range(-amt, amt),
                     c.b + r.range(-amt, amt));
}

float smoothstepf(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// Smooth 2-D value noise in [0,1] — drives where vegetation clumps and how
// long the grass grows, so neither is an even, featureless scatter.
float vegNoise(float x, float z) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float fx = x - (float)ix, fz = z - (float)iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    auto lat = [](int gx, int gz) {
        uint32_t hh = hashXZ(gx, gz);
        return (float)(hh & 0xFFFFFFu) / (float)0x1000000;
    };
    float a = lat(ix, iz),     b = lat(ix + 1, iz);
    float c = lat(ix, iz + 1), d = lat(ix + 1, iz + 1);
    return (a + (b - a) * fx) * (1.0f - fz) + (c + (d - c) * fx) * fz;
}

// --- Voxel primitives -------------------------------------------------------

// One quad (two triangles) with a face normal from its winding.
void addQuad(std::vector<VegVertex>& o,
             glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
             glm::vec3 col, float sa, float sb, float sc, float sd,
             float sky, float blk) {
    glm::vec3 n = glm::cross(b - a, c - a);
    float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len < 1e-8f) return;
    n /= len;
    auto v = [&](glm::vec3 p, float s) {
        o.push_back({p.x, p.y, p.z, n.x, n.y, n.z, col.r, col.g, col.b, s, sky, blk});
    };
    v(a, sa); v(b, sb); v(c, sc);
    v(a, sa); v(c, sc); v(d, sd);
}

// A full cube centred at `c`, half-extents `h`. Faces are shaded top-bright,
// sides-mid, bottom-dark. swB/swT weight wind on the lower/upper vertices.
void addCube(std::vector<VegVertex>& o, glm::vec3 c, glm::vec3 h, glm::vec3 col,
             float swB, float swT, float sky, float blk) {
    float x0 = c.x - h.x, x1 = c.x + h.x;
    float y0 = c.y - h.y, y1 = c.y + h.y;
    float z0 = c.z - h.z, z1 = c.z + h.z;
    glm::vec3 c000(x0,y0,z0), c100(x1,y0,z0), c010(x0,y1,z0), c110(x1,y1,z0);
    glm::vec3 c001(x0,y0,z1), c101(x1,y0,z1), c011(x0,y1,z1), c111(x1,y1,z1);
    glm::vec3 top = col * 1.00f, sid = col * 0.82f, bot = col * 0.55f;
    addQuad(o, c010, c011, c111, c110, top, swT,swT,swT,swT, sky, blk);   // +Y
    addQuad(o, c000, c100, c101, c001, bot, swB,swB,swB,swB, sky, blk);   // -Y
    addQuad(o, c000, c001, c011, c010, sid, swB,swB,swT,swT, sky, blk);   // -X
    addQuad(o, c100, c110, c111, c101, sid, swB,swT,swT,swB, sky, blk);   // +X
    addQuad(o, c000, c010, c110, c100, sid, swB,swT,swT,swB, sky, blk);   // -Z
    addQuad(o, c001, c101, c111, c011, sid, swB,swB,swT,swT, sky, blk);   // +Z
}

// A vertical column of `count` cubes — grass blades, stems, reeds. Internal
// (hidden) horizontal faces are skipped; each cube is flat-shaded, so the
// colour steps between cubes read as stacked voxels. Sway ramps 0 -> 1 up it.
void addVoxelStack(std::vector<VegVertex>& o, glm::vec3 base, float halfW,
                   float cubeH, int count, glm::vec3 rootCol, glm::vec3 tipCol,
                   float sky, float blk) {
    if (count < 1) return;
    float x0 = base.x - halfW, x1 = base.x + halfW;
    float z0 = base.z - halfW, z1 = base.z + halfW;
    for (int k = 0; k < count; k++) {
        float y0 = base.y + (float)k * cubeH;
        float y1 = y0 + cubeH;
        float t  = (count > 1) ? (float)k / (float)(count - 1) : 0.0f;
        glm::vec3 col = glm::mix(rootCol, tipCol, t);
        glm::vec3 sid = col * 0.82f;
        float swB = (float)k / (float)count;
        float swT = (float)(k + 1) / (float)count;
        glm::vec3 c000(x0,y0,z0), c100(x1,y0,z0), c010(x0,y1,z0), c110(x1,y1,z0);
        glm::vec3 c001(x0,y0,z1), c101(x1,y0,z1), c011(x0,y1,z1), c111(x1,y1,z1);
        addQuad(o, c000, c001, c011, c010, sid, swB,swB,swT,swT, sky, blk);  // -X
        addQuad(o, c100, c110, c111, c101, sid, swB,swT,swT,swB, sky, blk);  // +X
        addQuad(o, c000, c010, c110, c100, sid, swB,swT,swT,swB, sky, blk);  // -Z
        addQuad(o, c001, c101, c111, c011, sid, swB,swB,swT,swT, sky, blk);  // +Z
        if (k == 0)
            addQuad(o, c000, c100, c101, c001, col * 0.55f,
                    swB,swB,swB,swB, sky, blk);                             // -Y
        if (k == count - 1)
            addQuad(o, c010, c011, c111, c110, col * 1.00f,
                    swT,swT,swT,swT, sky, blk);                             // +Y
    }
}

// --- Type generators --------------------------------------------------------

// A clump of upright voxel blades — grass and scrub. `regionH` (0..1) sets the
// patch's overall length; per-tuft and per-blade randomness layer on top, so
// grass length and tuft tightness vary widely from spot to spot.
void genGrass(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk,
              float regionH, glm::vec3 rootCol, glm::vec3 tipCol) {
    int   baseLen = 2 + (int)(regionH * 5.99f);     // 2..7 cubes, set regionally
    int   blades  = r.irange(3, 8);                 // tuft density varies
    float cubeH   = r.range(0.090f, 0.130f);
    float spread  = r.range(0.10f, 0.28f);
    for (int i = 0; i < blades; i++) {
        float a   = r.range(0.0f, TAU);
        float rad = r.range(0.0f, spread);
        glm::vec3 b = base + glm::vec3(cosf(a) * rad, 0.0f, sinf(a) * rad);
        int cubes = baseLen + r.irange(-1, 3);      // per-blade length variation
        if (cubes < 1) cubes = 1;
        addVoxelStack(o, b, r.range(0.042f, 0.075f), cubeH, cubes,
                      jitterCol(rootCol, r, 0.045f), jitterCol(tipCol, r, 0.05f),
                      sky, blk);
    }
}

// A fern — arching fronds of cubes radiating from a small upright crown.
void genFern(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk,
             float scale) {
    glm::vec3 rc = jitterCol(glm::vec3(0.12f, 0.30f, 0.15f), r, 0.03f);
    glm::vec3 tc = jitterCol(glm::vec3(0.32f, 0.54f, 0.24f), r, 0.05f);
    int fronds = r.irange(5, 8);
    float a0 = r.range(0.0f, TAU);
    for (int f = 0; f < fronds; f++) {
        float a = a0 + (float)f * (TAU / (float)fronds) + r.range(-0.25f, 0.25f);
        glm::vec2 dir(cosf(a), sinf(a));
        float reach = r.range(0.34f, 0.50f) * scale;
        float archH = r.range(0.30f, 0.46f) * scale;
        const int M = 5;
        for (int i = 1; i <= M; i++) {
            float t = (float)i / (float)M;
            glm::vec3 p = base
                + glm::vec3(dir.x, 0.0f, dir.y) * (reach * t)
                + glm::vec3(0.0f, archH * sinf(t * 2.0f), 0.0f);
            float taper = (0.062f - 0.030f * t) * scale;
            addCube(o, p, glm::vec3(taper), glm::mix(rc, tc, t), t, t, sky, blk);
        }
    }
    int crown = r.irange(3, 5);
    for (int i = 0; i < crown; i++) {
        float a = r.range(0.0f, TAU);
        glm::vec3 b = base + glm::vec3(cosf(a) * 0.04f, 0.0f, sinf(a) * 0.04f);
        addVoxelStack(o, b, 0.05f * scale, 0.075f * scale, r.irange(2, 3),
                      rc, tc, sky, blk);
    }
}

// Tall reeds; `cattail` adds a brown seed head to a couple of stalks.
void genReeds(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk,
              bool cattail) {
    glm::vec3 rc = jitterCol(glm::vec3(0.30f, 0.42f, 0.16f), r, 0.04f);
    glm::vec3 tc = jitterCol(glm::vec3(0.62f, 0.66f, 0.30f), r, 0.05f);
    int n = r.irange(4, 7);
    for (int i = 0; i < n; i++) {
        float a = r.range(0.0f, TAU);
        float rad = r.range(0.0f, 0.14f);
        glm::vec3 b = base + glm::vec3(cosf(a) * rad, 0.0f, sinf(a) * rad);
        int cubes = r.irange(6, 11);
        float ch = 0.13f;
        addVoxelStack(o, b, r.range(0.035f, 0.05f), ch, cubes, rc, tc, sky, blk);
        if (cattail && i < 2) {
            float hy = b.y + (float)(cubes - 2) * ch;
            for (int k = 0; k < 3; k++)
                addCube(o, glm::vec3(b.x, hy + (float)k * 0.09f, b.z),
                        glm::vec3(0.075f, 0.05f, 0.075f),
                        glm::vec3(0.36f, 0.21f, 0.10f), 0.9f, 0.97f, sky, blk);
        }
    }
}

// Crop wheat — a tuft of upright stalks. `stage` drives height + colour:
// 0 young (short, green), 1 tall (yellow-green), 2 ripe (golden, with grain
// heads). Sway ramps up each stalk so the field ripples in the wind.
void genWheat(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk, int stage) {
    glm::vec3 rootC, tipC;
    int baseLen;
    if (stage <= 0)      { rootC = glm::vec3(0.32f,0.46f,0.16f); tipC = glm::vec3(0.56f,0.74f,0.28f); baseLen = 2; }
    else if (stage == 1) { rootC = glm::vec3(0.46f,0.52f,0.18f); tipC = glm::vec3(0.80f,0.78f,0.34f); baseLen = 4; }
    else                 { rootC = glm::vec3(0.60f,0.50f,0.20f); tipC = glm::vec3(0.93f,0.82f,0.38f); baseLen = 6; }
    int   stalks = r.irange(4, 7);
    float ch = 0.13f;
    for (int i = 0; i < stalks; i++) {
        float a = r.range(0.0f, TAU), rad = r.range(0.0f, 0.17f);
        glm::vec3 b = base + glm::vec3(cosf(a) * rad, 0.0f, sinf(a) * rad);
        int cubes = baseLen + r.irange(-1, 1);
        if (cubes < 1) cubes = 1;
        addVoxelStack(o, b, r.range(0.035f, 0.055f), ch, cubes,
                      jitterCol(rootC, r, 0.04f), jitterCol(tipC, r, 0.05f), sky, blk);
        if (stage >= 2) {                                          // a fat golden grain head
            float hy = b.y + (float)cubes * ch;
            glm::vec3 grain = jitterCol(glm::vec3(0.95f, 0.80f, 0.34f), r, 0.04f);
            for (int k = 0; k < 2; k++)
                addCube(o, glm::vec3(b.x, hy + (float)k * 0.08f, b.z),
                        glm::vec3(0.058f, 0.05f, 0.058f), grain, 0.9f, 1.0f, sky, blk);
        }
    }
}

// A flower — a slim stem, a leaf, and a blocky petalled head.
void genFlower(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk,
               float heightScale) {
    glm::vec3 stem = jitterCol(glm::vec3(0.22f, 0.42f, 0.16f), r, 0.03f);
    int sc = r.irange(3, 5) + (heightScale > 1.2f ? 3 : 0);
    float ch = 0.10f;
    addVoxelStack(o, base, 0.038f, ch, sc, stem, stem, sky, blk);

    float la = r.range(0.0f, TAU);
    float leafY = ch * (float)sc * 0.4f;
    addCube(o, base + glm::vec3(cosf(la) * 0.08f, leafY, sinf(la) * 0.08f),
            glm::vec3(0.07f, 0.028f, 0.07f), glm::vec3(0.30f, 0.50f, 0.20f),
            0.4f, 0.5f, sky, blk);

    glm::vec3 head = base + glm::vec3(0.0f, ch * (float)sc, 0.0f);
    static const glm::vec3 PAL[7] = {
        glm::vec3(0.90f,0.24f,0.22f), glm::vec3(0.96f,0.82f,0.26f),
        glm::vec3(0.42f,0.48f,0.92f), glm::vec3(0.96f,0.96f,0.98f),
        glm::vec3(0.92f,0.48f,0.78f), glm::vec3(0.68f,0.40f,0.86f),
        glm::vec3(0.96f,0.56f,0.18f) };
    glm::vec3 petal = jitterCol(PAL[r.irange(0, 6)], r, 0.04f);
    glm::vec3 core(0.97f, 0.84f, 0.30f);
    float pw = r.range(0.070f, 0.088f);
    addCube(o, head, glm::vec3(pw), core, 1.0f, 1.0f, sky, blk);
    static const float D[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (int i = 0; i < 4; i++)
        addCube(o, head + glm::vec3(D[i][0] * pw * 2.0f, 0.0f, D[i][1] * pw * 2.0f),
                glm::vec3(pw), petal, 1.0f, 1.0f, sky, blk);
    addCube(o, head + glm::vec3(0.0f, pw * 1.7f, 0.0f), glm::vec3(pw * 0.7f),
            petal * 1.08f, 1.0f, 1.0f, sky, blk);
}

// Tiny ground cover — a scatter of small flat leaf cubes.
void genGround(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk,
               glm::vec3 col) {
    int n = r.irange(4, 8);
    for (int i = 0; i < n; i++) {
        float a = r.range(0.0f, TAU);
        float rad = r.range(0.0f, 0.26f);
        glm::vec3 p = base + glm::vec3(cosf(a) * rad, r.range(0.03f, 0.11f),
                                       sinf(a) * rad);
        float s = r.range(0.05f, 0.09f);
        addCube(o, p, glm::vec3(s, s * 0.55f, s), jitterCol(col, r, 0.05f),
                0.3f, 0.5f, sky, blk);
    }
}

// Capped mushrooms — a small cluster, or one big spotted toadstool.
void genMushroom(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk,
                 bool big) {
    static const glm::vec3 CAPS[4] = {
        glm::vec3(0.78f,0.16f,0.13f), glm::vec3(0.60f,0.34f,0.20f),
        glm::vec3(0.84f,0.66f,0.40f), glm::vec3(0.86f,0.78f,0.52f) };
    glm::vec3 stemC(0.90f, 0.86f, 0.74f);
    int n = big ? 1 : r.irange(2, 4);
    for (int m = 0; m < n; m++) {
        glm::vec3 b = base + (big ? glm::vec3(0.0f)
            : glm::vec3(r.range(-0.22f, 0.22f), 0.0f, r.range(-0.22f, 0.22f)));
        glm::vec3 capC = jitterCol(CAPS[r.irange(0, 3)], r, 0.03f);
        float ch = big ? 0.14f : 0.09f;
        int   sc = big ? r.irange(3, 4) : r.irange(1, 2);
        addVoxelStack(o, b, big ? 0.09f : 0.055f, ch, sc, stemC, stemC, sky, blk);
        float capY = b.y + ch * (float)sc;
        float cr = big ? r.range(0.16f, 0.21f) : r.range(0.09f, 0.12f);
        addCube(o, glm::vec3(b.x, capY, b.z), glm::vec3(cr, cr * 0.5f, cr),
                capC, 0.8f, 0.92f, sky, blk);
        if (big) {
            static const float D[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (int i = 0; i < 4; i++)
                addCube(o, glm::vec3(b.x + D[i][0] * cr * 1.05f, capY - cr * 0.18f,
                                     b.z + D[i][1] * cr * 1.05f),
                        glm::vec3(cr * 0.65f, cr * 0.4f, cr * 0.65f),
                        capC, 0.8f, 0.88f, sky, blk);
            for (int i = 0; i < 3; i++)
                addCube(o, glm::vec3(b.x + r.range(-cr*0.5f, cr*0.5f), capY + cr*0.5f,
                                     b.z + r.range(-cr*0.5f, cr*0.5f)),
                        glm::vec3(cr * 0.17f), glm::vec3(0.96f), 0.95f, 0.95f, sky, blk);
        }
    }
}

// A shrub — a dome of leaf cubes (leafy/berry) or bare twigs (dead).
void genBush(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk,
             int kind) {                              // 0 leafy, 1 berry, 2 dead
    if (kind == 2) {
        glm::vec3 tw = jitterCol(glm::vec3(0.36f, 0.30f, 0.20f), r, 0.04f);
        int n = r.irange(5, 8);
        for (int i = 0; i < n; i++) {
            float a = r.range(0.0f, TAU);
            glm::vec3 b = base + glm::vec3(cosf(a) * r.range(0.0f, 0.16f), 0.0f,
                                           sinf(a) * r.range(0.0f, 0.16f));
            addVoxelStack(o, b, r.range(0.03f, 0.045f), 0.11f, r.irange(2, 4),
                          tw, tw * 1.1f, sky, blk);
        }
        return;
    }
    glm::vec3 leaf  = jitterCol(glm::vec3(0.20f, 0.40f, 0.16f), r, 0.04f);
    glm::vec3 leafT = leaf * 1.4f;
    int n = r.irange(10, 16);
    float rad = r.range(0.26f, 0.36f);
    float topY = rad * 1.5f;
    for (int i = 0; i < n; i++) {
        float a = r.range(0.0f, TAU);
        float rr = r.range(0.0f, rad);
        float hh = r.range(0.06f, topY);
        glm::vec3 p = base + glm::vec3(cosf(a) * rr, 0.12f + hh, sinf(a) * rr);
        float sway = clamp01(hh / topY) * 0.7f;
        addCube(o, p, glm::vec3(r.range(0.09f, 0.15f)),
                jitterCol(glm::mix(leaf, leafT, hh / topY), r, 0.04f),
                sway * 0.6f, sway, sky, blk);
    }
    if (kind == 1) {
        glm::vec3 berry(0.74f, 0.10f, 0.12f);
        int b = r.irange(4, 7);
        for (int i = 0; i < b; i++) {
            float a = r.range(0.0f, TAU);
            float rr = r.range(rad * 0.4f, rad);
            glm::vec3 p = base + glm::vec3(cosf(a) * rr, r.range(0.15f, rad * 1.3f),
                                           sinf(a) * rr);
            addCube(o, p, glm::vec3(0.045f), jitterCol(berry, r, 0.04f),
                    0.6f, 0.66f, sky, blk);
        }
    }
}

// A fallen log — a row of bark cubes along one axis, with moss on top.
void genLog(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk) {
    glm::vec3 bark = glm::vec3(0.34f, 0.22f, 0.13f);
    bool alongX = (r.f() < 0.5f);
    int seg = r.irange(4, 7);
    float cs = 0.25f, rad = 0.24f;
    for (int i = 0; i < seg; i++) {
        float off = ((float)i - (float)(seg - 1) * 0.5f) * (cs * 1.92f);
        glm::vec3 c = base + glm::vec3(alongX ? off : 0.0f, rad,
                                       alongX ? 0.0f : off);
        addCube(o, c, glm::vec3(cs, rad, cs), jitterCol(bark, r, 0.025f),
                0.0f, 0.0f, sky, blk);
    }
    glm::vec3 moss(0.26f, 0.42f, 0.18f);
    int tufts = r.irange(2, 4);
    for (int i = 0; i < tufts; i++) {
        float off = r.range(-1.0f, 1.0f) * (float)(seg - 1) * 0.5f * (cs * 1.92f);
        glm::vec3 b = base + glm::vec3(alongX ? off : r.range(-0.1f, 0.1f),
                                       rad * 2.0f,
                                       alongX ? r.range(-0.1f, 0.1f) : off);
        addVoxelStack(o, b, 0.05f, 0.08f, r.irange(2, 3), moss, moss * 1.3f,
                      sky, blk);
    }
}

// A cut tree stump — four bark cubes capped with a pale cut-wood top.
void genStump(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk) {
    glm::vec3 bark = jitterCol(glm::vec3(0.32f, 0.21f, 0.13f), r, 0.03f);
    glm::vec3 wood(0.66f, 0.50f, 0.32f);
    float s = r.range(0.20f, 0.28f);
    float hh = r.range(0.18f, 0.34f);
    for (int dx = -1; dx <= 1; dx += 2)
        for (int dz = -1; dz <= 1; dz += 2)
            addCube(o, base + glm::vec3((float)dx * s * 0.5f, hh, (float)dz * s * 0.5f),
                    glm::vec3(s * 0.55f, hh, s * 0.55f), jitterCol(bark, r, 0.02f),
                    0.0f, 0.0f, sky, blk);
    addCube(o, base + glm::vec3(0.0f, hh * 2.0f - 0.03f, 0.0f),
            glm::vec3(s * 1.05f, 0.045f, s * 1.05f), wood, 0.0f, 0.0f, sky, blk);
}

// A young tree — a short trunk topped with a small voxel leaf canopy.
void genSapling(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk) {
    glm::vec3 trunk = jitterCol(glm::vec3(0.36f, 0.25f, 0.14f), r, 0.03f);
    int tc = r.irange(3, 5);
    float ch = 0.12f;
    addVoxelStack(o, base, 0.05f, ch, tc, trunk, trunk, sky, blk);
    glm::vec3 leaf = jitterCol(glm::vec3(0.20f, 0.44f, 0.18f), r, 0.04f);
    glm::vec3 top = base + glm::vec3(0.0f, ch * (float)tc, 0.0f);
    int blob = r.irange(5, 8);
    for (int i = 0; i < blob; i++) {
        glm::vec3 p = top + glm::vec3(r.range(-0.16f, 0.16f), r.range(-0.05f, 0.22f),
                                      r.range(-0.16f, 0.16f));
        addCube(o, p, glm::vec3(r.range(0.08f, 0.13f)), jitterCol(leaf, r, 0.05f),
                0.7f, 0.88f, sky, blk);
    }
}

// A scatter of small static rocks.
void genPebbles(std::vector<VegVertex>& o, glm::vec3 base, VRng& r, float sky, float blk) {
    glm::vec3 rock(0.46f, 0.46f, 0.49f);
    int n = r.irange(3, 6);
    for (int i = 0; i < n; i++) {
        float a = r.range(0.0f, TAU);
        float rad = r.range(0.0f, 0.28f);
        float s = r.range(0.06f, 0.13f);
        glm::vec3 p = base + glm::vec3(cosf(a) * rad, s * 0.7f, sinf(a) * rad);
        addCube(o, p, glm::vec3(s, s * 0.7f, s * r.range(0.8f, 1.1f)),
                jitterCol(rock, r, 0.06f), 0.0f, 0.0f, sky, blk);
    }
}

} // namespace

// --- Catalogue --------------------------------------------------------------

VegetationType Vegetation::pick(int biome, int wx, int wz) {
    uint32_t h = hashXZ(wx, wz);
    float cov = (float)(h & 0xFFFFu) / 65536.0f;
    float ty  = (float)((h >> 16) & 0xFFFFu) / 65536.0f;

    // Clumpy coverage — vegetation grows in drifts driven by a low-frequency
    // noise field, leaving barer ground between, instead of an even scatter.
    float density = vegNoise(wx * 0.05f + 19.7f, wz * 0.05f + 6.3f);
    float clump   = smoothstepf(0.36f, 0.78f, density);
    float maxCov;
    switch (biome) {
        case 2:  maxCov = 0.14f; break;   // Desert
        case 3:  maxCov = 0.30f; break;   // Mountains
        case 4:  maxCov = 0.26f; break;   // Tundra
        case 5:  maxCov = 0.50f; break;   // Savanna
        case 1:  maxCov = 0.62f; break;   // Forest
        case 6:  maxCov = 0.66f; break;   // Jungle
        default: maxCov = 0.62f; break;   // Plains
    }
    if (cov > clump * maxCov) return VegetationType::None;

    // Mostly grass; flowers a light sprinkle; everything else genuinely rare.
    using V = VegetationType;
    switch (biome) {
    case 1: // Forest — grassy floor with ferns
        if (ty < 0.72f)  return V::GrassTuft;
        if (ty < 0.86f)  return V::Fern;
        if (ty < 0.93f)  return V::Flower;
        if (ty < 0.962f) return V::MushroomCluster;
        if (ty < 0.978f) return V::Bush;
        if (ty < 0.988f) return V::FallenLog;
        if (ty < 0.995f) return V::Sapling;
        return V::Toadstool;
    case 2: // Desert
        if (ty < 0.82f) return V::DryShrub;
        if (ty < 0.94f) return V::DeadBush;
        return V::Pebbles;
    case 3: // Mountains
        if (ty < 0.80f)  return V::GrassTuft;
        if (ty < 0.88f)  return V::FrostGrass;
        if (ty < 0.95f)  return V::Pebbles;
        if (ty < 0.985f) return V::DryShrub;
        return V::DeadBush;
    case 4: // Tundra (snow is bare; only grass patches reach here)
        if (ty < 0.90f) return V::FrostGrass;
        if (ty < 0.97f) return V::Pebbles;
        return V::DeadBush;
    case 5: // Savanna
        if (ty < 0.84f)  return V::GrassTuft;
        if (ty < 0.92f)  return V::DryShrub;
        if (ty < 0.965f) return V::Flower;
        if (ty < 0.990f) return V::Reeds;
        return V::DeadBush;
    case 6: // Jungle — lush grass and ferns
        if (ty < 0.66f)  return V::GrassTuft;
        if (ty < 0.80f)  return V::Fern;
        if (ty < 0.87f)  return V::FernLarge;
        if (ty < 0.93f)  return V::Flower;
        if (ty < 0.96f)  return V::MushroomCluster;
        if (ty < 0.978f) return V::Bush;
        if (ty < 0.990f) return V::FallenLog;
        return V::Toadstool;
    case 0: default: // Plains
        if (ty < 0.86f)  return V::GrassTuft;
        if (ty < 0.93f)  return V::Flower;
        if (ty < 0.955f) return V::FlowerCluster;
        if (ty < 0.972f) return V::Clover;
        if (ty < 0.984f) return V::Sprout;
        if (ty < 0.992f) return V::BerryBush;
        if (ty < 0.997f) return V::TallFlower;
        return V::Cattail;
    }
}

void Vegetation::emit(std::vector<VegVertex>& o, VegetationType type,
                      float wx, float baseY, float wz,
                      float sky, float blk, uint32_t seed) {
    VRng r(seed);
    glm::vec3 base(wx + r.range(0.3f, 0.7f), baseY, wz + r.range(0.3f, 0.7f));
    using V = VegetationType;
    switch (type) {
    case V::GrassTuft: {
        float rh = vegNoise(wx * 0.07f + 31.1f, wz * 0.07f + 8.9f);
        genGrass(o, base, r, sky, blk, rh,
                 glm::vec3(0.20f,0.36f,0.13f), glm::vec3(0.48f,0.67f,0.23f));
        break;
    }
    case V::TallGrass: {
        float rh = 0.65f + 0.35f * vegNoise(wx * 0.07f + 5.0f, wz * 0.07f + 5.0f);
        genGrass(o, base, r, sky, blk, rh,
                 glm::vec3(0.24f,0.40f,0.14f), glm::vec3(0.60f,0.70f,0.26f));
        break;
    }
    case V::FrostGrass: {
        float rh = 0.45f * vegNoise(wx * 0.07f + 14.2f, wz * 0.07f + 51.6f);
        genGrass(o, base, r, sky, blk, rh,
                 glm::vec3(0.44f,0.55f,0.50f), glm::vec3(0.82f,0.90f,0.93f));
        break;
    }
    case V::DryShrub: {
        float rh = 0.40f * vegNoise(wx * 0.07f + 7.7f, wz * 0.07f + 22.1f);
        genGrass(o, base, r, sky, blk, rh,
                 glm::vec3(0.42f,0.38f,0.18f), glm::vec3(0.60f,0.55f,0.30f));
        break;
    }
    case V::Fern:       genFern(o, base, r, sky, blk, 1.0f); break;
    case V::FernLarge:  genFern(o, base, r, sky, blk, 1.6f); break;
    case V::Reeds:      genReeds(o, base, r, sky, blk, false); break;
    case V::Cattail:    genReeds(o, base, r, sky, blk, true);  break;
    case V::Flower:     genFlower(o, base, r, sky, blk, 1.0f); break;
    case V::TallFlower: genFlower(o, base, r, sky, blk, 1.5f); break;
    case V::FlowerCluster: {
        int n = r.irange(3, 4);
        for (int i = 0; i < n; i++) {
            glm::vec3 b = base + glm::vec3(r.range(-0.25f, 0.25f), 0.0f,
                                           r.range(-0.25f, 0.25f));
            genFlower(o, b, r, sky, blk, r.range(0.8f, 1.1f));
        }
        break;
    }
    case V::Clover:     genGround(o, base, r, sky, blk, glm::vec3(0.28f,0.54f,0.20f)); break;
    case V::Sprout:     genGround(o, base, r, sky, blk, glm::vec3(0.40f,0.58f,0.24f)); break;
    case V::MushroomCluster: genMushroom(o, base, r, sky, blk, false); break;
    case V::Toadstool:  genMushroom(o, base, r, sky, blk, true); break;
    case V::Bush:       genBush(o, base, r, sky, blk, 0); break;
    case V::BerryBush:  genBush(o, base, r, sky, blk, 1); break;
    case V::DeadBush:   genBush(o, base, r, sky, blk, 2); break;
    case V::FallenLog:  genLog(o, base, r, sky, blk); break;
    case V::TreeStump:  genStump(o, base, r, sky, blk); break;
    case V::Sapling:    genSapling(o, base, r, sky, blk); break;
    case V::Pebbles:    genPebbles(o, base, r, sky, blk); break;
    default: break;
    }
}

void Vegetation::emitWheat(std::vector<VegVertex>& o, int stage,
                           float wx, float baseY, float wz,
                           float sky, float blk, uint32_t seed) {
    VRng r(seed);
    glm::vec3 base(wx + r.range(0.35f, 0.65f), baseY, wz + r.range(0.35f, 0.65f));
    genWheat(o, base, r, sky, blk, stage);
}
