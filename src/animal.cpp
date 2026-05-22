#include "animal.h"
#include "voxel_model.h"
#include "world.h"
#include <algorithm>
#include <cmath>
#include <random>

// --- voxel helpers ---------------------------------------------------------

static void box(VoxelVolume* v, int x0, int y0, int z0,
                int x1, int y1, int z1, Voxel c) {
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++)
                if (x >= 0 && x < v->sizeX && y >= 0 && y < v->sizeY &&
                    z >= 0 && z < v->sizeZ)
                    v->setVoxel(x, y, z, c);
}

// Uniform lightness jitter — keeps the hue, varies the shade for a furry look.
static Voxel jitter(Voxel c, std::mt19937& rng, int amt) {
    int j = std::uniform_int_distribution<int>(-amt, amt)(rng);
    auto cl = [](int v) { return (uint8_t)std::max(0, std::min(255, v)); };
    return { cl((int)c.r + j), cl((int)c.g + j), cl((int)c.b + j), 255 };
}

static float frand01(std::mt19937& r) {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(r);
}

// A tapered, three-section leg that hangs straight down from its node origin
// (the hip): a full-width thigh embeds into the body, a slimmer shank gives the
// leg a knee, and a distinct foot uses the darker `boot` colour (hoof / paw).
// `bootH` is the foot height.
static void makeLeg(CharacterNode* leg, int w, int h, int d, Voxel col, Voxel boot,
                    int bootH = 2) {
    VoxelVolume* v = new VoxelVolume(w, h, d);
    int knee = std::max(bootH + 1, (h * 3) / 5);                  // thigh base
    box(v, 0, knee, 0, w - 1, h - 1, d - 1, col);                 // thigh
    int ix = (w >= 4) ? 1 : 0, iz = (d >= 4) ? 1 : 0;
    box(v, ix, bootH, iz, w - 1 - ix, knee - 1, d - 1 - iz, col); // slim shank
    box(v, 0, 0, 0, w - 1, bootH - 1, d - 1, boot);               // foot / hoof
    v->updateMesh();
    leg->volume = v;
    leg->pivot  = glm::vec3(w * 0.5f, (float)h, d * 0.5f);
}

// Fills v with a rounded, jittered blob; the lower third uses `belly`.
static void roundBlob(VoxelVolume* v, Voxel main, Voxel belly,
                      std::mt19937& rng, int jit, float r2) {
    float cx = v->sizeX * 0.5f, cy = v->sizeY * 0.5f, cz = v->sizeZ * 0.5f;
    for (int x = 0; x < v->sizeX; x++)
        for (int y = 0; y < v->sizeY; y++)
            for (int z = 0; z < v->sizeZ; z++) {
                float nx = (x + 0.5f - cx) / cx;
                float ny = (y + 0.5f - cy) / cy;
                float nz = (z + 0.5f - cz) / cz;
                if (nx * nx + ny * ny + nz * nz > r2) continue;
                v->setVoxel(x, y, z, jitter(y < v->sizeY / 3 ? belly : main, rng, jit));
            }
}

// Fills v with a chamfered box: every one of the six faces stays a solid
// rectangle (so legs and the head attach cleanly), while the twelve edges and
// corners are beveled for a rounded silhouette. The lower third uses `belly`.
static void buildBody(VoxelVolume* v, Voxel main, Voxel belly,
                      std::mt19937& rng, int jit, int chamfer) {
    const int W = v->sizeX, H = v->sizeY, D = v->sizeZ;
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            for (int z = 0; z < D; z++) {
                int dx = std::min(x, W - 1 - x);
                int dy = std::min(y, H - 1 - y);
                int dz = std::min(z, D - 1 - z);
                int lo  = std::min({dx, dy, dz});
                int hi  = std::max({dx, dy, dz});
                int mid = dx + dy + dz - lo - hi;
                if (lo + mid < chamfer) continue;          // bevel edges/corners
                v->setVoxel(x, y, z, jitter(y < H / 3 ? belly : main, rng, jit));
            }
}

// --- Animal ----------------------------------------------------------------

Animal::Animal() : GameObject(ObjectKind::Animal) {}

Animal::~Animal() {
    delete rig;
}

void Animal::initClientVisual() {
    if (rig) return;
    rig = new QuadrupedRig();
    scale = buildAnimalRig(*rig, species, variant);
}

void Animal::update(float dt, World& world) {
    (void)world;
    float k = std::min(1.0f, 10.0f * dt);
    position = glm::mix(position, targetPos, k);
    float dyaw = targetYaw - yaw;
    while (dyaw >  180.0f) dyaw -= 360.0f;
    while (dyaw < -180.0f) dyaw += 360.0f;
    yaw += dyaw * k;
    if (rig) rig->update(dt, glm::length(velocity));
}

void Animal::draw(GLuint modelLoc) const {
    if (!rig) return;
    rig->draw(baseMatrix(scale), modelLoc);
}

void Animal::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    mn = position - glm::vec3(1.2f, 0.0f, 1.2f);
    mx = position + glm::vec3(1.2f, 1.8f, 1.2f);
}

// --- species voxel models --------------------------------------------------

static float buildSheep(QuadrupedRig& rig, std::mt19937& rng) {
    const Voxel wool   {236, 233, 226, 255};
    const Voxel face   { 58,  53,  50, 255};
    const Voxel muzzle { 96,  88,  82, 255};
    const Voxel legc   { 54,  49,  47, 255};
    const Voxel hoof   { 28,  26,  24, 255};
    const Voxel eyeW   {232, 232, 232, 255};
    const Voxel eyeP   { 16,  14,  12, 255};

    const int BW = 18, BH = 16, BL = 28;   // woolly body
    const int LH = 12;                      // leg height

    // Body — a chamfered woolly box: solid faces for clean leg/head joins.
    VoxelVolume* bv = new VoxelVolume(BW, BH, BL);
    float cx = BW * 0.5f, cy = BH * 0.5f, cz = BL * 0.5f;
    buildBody(bv, wool, wool, rng, 13, 3);
    bv->updateMesh();
    rig.body->volume   = bv;
    rig.body->pivot    = glm::vec3(cx, cy, cz);
    float restY        = LH + BH * 0.5f - 3.0f;
    rig.restY          = restY;
    rig.body->localPos = glm::vec3(0.0f, restY, 0.0f);

    // Legs — embedded 3 voxels up into the body so they read as attached.
    makeLeg(rig.flLeg, 5, LH, 5, legc, hoof);
    makeLeg(rig.frLeg, 5, LH, 5, legc, hoof);
    makeLeg(rig.blLeg, 5, LH, 5, legc, hoof);
    makeLeg(rig.brLeg, 5, LH, 5, legc, hoof);
    float legX = BW * 0.5f - 3.0f, legZ = BL * 0.5f - 4.0f, hipY = -BH * 0.5f + 3.0f;
    rig.flLeg->localPos = glm::vec3(-legX, hipY,  legZ);
    rig.frLeg->localPos = glm::vec3( legX, hipY,  legZ);
    rig.blLeg->localPos = glm::vec3(-legX, hipY, -legZ);
    rig.brLeg->localPos = glm::vec3( legX, hipY, -legZ);

    // Head — dark face, lighter muzzle, ear nubs and eyes.
    const int HW = 11, HH = 12, HD = 12;
    VoxelVolume* hv = new VoxelVolume(HW, HH, HD);
    float hcx = HW * 0.5f, hcy = HH * 0.5f, hcz = HD * 0.5f;
    for (int x = 0; x < HW; x++)
        for (int y = 0; y < HH; y++)
            for (int z = 0; z < HD; z++) {
                float nx = (x + 0.5f - hcx) / hcx;
                float ny = (y + 0.5f - hcy) / hcy;
                float nz = (z + 0.5f - hcz) / hcz;
                if (nx * nx + ny * ny + nz * nz > 1.08f) continue;
                hv->setVoxel(x, y, z, jitter(face, rng, 8));
            }
    box(hv, 3, 1, HD - 3, HW - 4, 4, HD - 1, muzzle);      // muzzle
    box(hv, 0, HH - 5, 4, 1,      HH - 3, 7, face);         // left ear nub
    box(hv, HW - 2, HH - 5, 4, HW - 1, HH - 3, 7, face);    // right ear nub
    hv->setVoxel(2, 7, HD - 1, eyeW);   hv->setVoxel(2, 8, HD - 1, eyeP);
    hv->setVoxel(HW - 3, 7, HD - 1, eyeW); hv->setVoxel(HW - 3, 8, HD - 1, eyeP);
    hv->updateMesh();
    rig.head->volume   = hv;
    rig.head->pivot    = glm::vec3(hcx, hcy, 1.0f);
    rig.head->localPos = glm::vec3(0.0f, BH * 0.5f - 1.0f, BL * 0.5f - 1.0f);

    // Tail — small woolly nub.
    const int TW = 5, TH = 6, TD = 5;
    VoxelVolume* tv = new VoxelVolume(TW, TH, TD);
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++)
            for (int z = 0; z < TD; z++)
                tv->setVoxel(x, y, z, jitter(wool, rng, 13));
    tv->updateMesh();
    rig.tail->volume   = tv;
    rig.tail->pivot    = glm::vec3(TW * 0.5f, TH * 0.5f, (float)TD);
    rig.tail->localPos = glm::vec3(0.0f, BH * 0.5f + 1.0f, -BL * 0.5f + 1.0f);

    return 0.038f;
}

static float buildRabbit(QuadrupedRig& rig, std::mt19937& rng) {
    bool  grey = (rng() & 1u) != 0u;
    Voxel furM = grey ? Voxel{142, 142, 140, 255} : Voxel{152, 121,  92, 255};
    Voxel furB = grey ? Voxel{208, 208, 206, 255} : Voxel{216, 202, 184, 255};
    Voxel legc = furM;
    const Voxel tailc {240, 240, 238, 255};
    const Voxel earIn {226, 170, 170, 255};
    const Voxel eyeP  { 18,  14,  12, 255};
    const Voxel nose  {210, 120, 120, 255};

    const int BW = 9, BH = 9, BL = 14;
    const int LH = 6;

    // Body — a chamfered box with a lighter belly.
    VoxelVolume* bv = new VoxelVolume(BW, BH, BL);
    float cx = BW * 0.5f, cy = BH * 0.5f, cz = BL * 0.5f;
    buildBody(bv, furM, furB, rng, 9, 2);
    bv->updateMesh();
    rig.body->volume   = bv;
    rig.body->pivot    = glm::vec3(cx, cy, cz);
    float restY        = LH + BH * 0.5f - 2.0f;
    rig.restY          = restY;
    rig.body->localPos = glm::vec3(0.0f, restY, 0.0f);

    // Legs — slim front, bulky hind haunches, all embedded into the body.
    makeLeg(rig.flLeg, 3, LH, 3, legc, legc);
    makeLeg(rig.frLeg, 3, LH, 3, legc, legc);
    makeLeg(rig.blLeg, 5, LH, 7, legc, legc);
    makeLeg(rig.brLeg, 5, LH, 7, legc, legc);
    float hipY = -BH * 0.5f + 2.0f;
    rig.flLeg->localPos = glm::vec3(-(BW * 0.5f - 1.8f), hipY,  BL * 0.5f - 3.0f);
    rig.frLeg->localPos = glm::vec3( (BW * 0.5f - 1.8f), hipY,  BL * 0.5f - 3.0f);
    rig.blLeg->localPos = glm::vec3(-(BW * 0.5f - 2.5f), hipY, -(BL * 0.5f - 4.0f));
    rig.brLeg->localPos = glm::vec3( (BW * 0.5f - 2.5f), hipY, -(BL * 0.5f - 4.0f));

    // Head — a small skull with two tall upright ears.
    const int HW = 8, HH = 18, HD = 8;
    VoxelVolume* hv = new VoxelVolume(HW, HH, HD);
    for (int x = 0; x < HW; x++)
        for (int y = 0; y < 8; y++)
            for (int z = 0; z < HD; z++) {
                float nx = (x + 0.5f - HW * 0.5f) / (HW * 0.5f);
                float ny = (y + 0.5f - 4.0f) / 4.0f;
                float nz = (z + 0.5f - HD * 0.5f) / (HD * 0.5f);
                if (nx * nx + ny * ny + nz * nz > 1.10f) continue;
                hv->setVoxel(x, y, z, jitter(furM, rng, 9));
            }
    for (int y = 7; y < HH; y++) {                  // two upright ears
        box(hv, 1, y, 3, 2, y, 5, furM);
        box(hv, HW - 3, y, 3, HW - 2, y, 5, furM);
        if (y < HH - 2) { hv->setVoxel(1, y, 4, earIn); hv->setVoxel(HW - 2, y, 4, earIn); }
    }
    hv->setVoxel(1, 5, HD - 1, eyeP);  hv->setVoxel(HW - 2, 5, HD - 1, eyeP);
    box(hv, HW / 2 - 1, 2, HD - 1, HW / 2, 3, HD - 1, nose);
    hv->updateMesh();
    rig.head->volume   = hv;
    rig.head->pivot    = glm::vec3(HW * 0.5f, 4.0f, 1.0f);
    rig.head->localPos = glm::vec3(0.0f, BH * 0.5f, BL * 0.5f - 1.0f);

    // Tail — a round white puff.
    const int TS = 5;
    VoxelVolume* tv = new VoxelVolume(TS, TS, TS);
    float tc = TS * 0.5f;
    for (int x = 0; x < TS; x++)
        for (int y = 0; y < TS; y++)
            for (int z = 0; z < TS; z++) {
                float nx = (x + 0.5f - tc) / tc;
                float ny = (y + 0.5f - tc) / tc;
                float nz = (z + 0.5f - tc) / tc;
                if (nx * nx + ny * ny + nz * nz <= 1.05f)
                    tv->setVoxel(x, y, z, jitter(tailc, rng, 8));
            }
    tv->updateMesh();
    rig.tail->volume   = tv;
    rig.tail->pivot    = glm::vec3(tc, tc, (float)TS);
    rig.tail->localPos = glm::vec3(0.0f, BH * 0.5f + 1.0f, -BL * 0.5f + 1.0f);

    return 0.027f;
}

static float buildCow(QuadrupedRig& rig, std::mt19937& rng) {
    const Voxel hide  {126,  86,  58, 255};
    const Voxel patch {236, 233, 226, 255};
    const Voxel legc  {104,  74,  54, 255};
    const Voxel hoof  { 32,  28,  26, 255};
    const Voxel muzz  {214, 150, 150, 255};
    const Voxel horn  {224, 212, 184, 255};
    const Voxel eyeP  { 16,  14,  12, 255};
    const Voxel udder {228, 156, 154, 255};
    const Voxel tuft  { 40,  34,  30, 255};
    const int BW = 20, BH = 18, BL = 34, LH = 13;

    VoxelVolume* bv = new VoxelVolume(BW, BH, BL);
    buildBody(bv, hide, hide, rng, 10, 3);
    for (int p = 0; p < 4; p++) {                       // white patches
        int px = 3 + (int)(frand01(rng) * (BW - 6));
        int py = BH / 3 + (int)(frand01(rng) * (BH * 0.55f));
        int pz = 4 + (int)(frand01(rng) * (BL - 8));
        int pr = 3 + (int)(frand01(rng) * 3.0f);
        for (int x = px - pr; x <= px + pr; x++)
            for (int y = py - pr; y <= py + pr; y++)
                for (int z = pz - pr; z <= pz + pr; z++) {
                    if (x < 0 || x >= BW || y < 0 || y >= BH || z < 0 || z >= BL)
                        continue;
                    float dx = (float)(x - px), dy = (float)(y - py), dz = (float)(z - pz);
                    if (dx*dx + dy*dy + dz*dz <= (float)(pr*pr) &&
                        bv->getVoxel(x, y, z).a > 0)
                        bv->setVoxel(x, y, z, jitter(patch, rng, 10));
                }
    }
    box(bv, BW/2 - 3, 0, 5, BW/2 + 2, 2, 11, udder);    // udder
    bv->updateMesh();
    rig.body->volume   = bv;
    rig.body->pivot    = glm::vec3(BW*0.5f, BH*0.5f, BL*0.5f);
    rig.restY          = LH + BH*0.5f - 3.0f;
    rig.body->localPos = glm::vec3(0.0f, rig.restY, 0.0f);

    makeLeg(rig.flLeg, 6, LH, 6, legc, hoof, 3);
    makeLeg(rig.frLeg, 6, LH, 6, legc, hoof, 3);
    makeLeg(rig.blLeg, 6, LH, 6, legc, hoof, 3);
    makeLeg(rig.brLeg, 6, LH, 6, legc, hoof, 3);
    float lx = BW*0.5f - 3.5f, lz = BL*0.5f - 5.0f, hy = -BH*0.5f + 3.0f;
    rig.flLeg->localPos = glm::vec3(-lx, hy,  lz);
    rig.frLeg->localPos = glm::vec3( lx, hy,  lz);
    rig.blLeg->localPos = glm::vec3(-lx, hy, -lz);
    rig.brLeg->localPos = glm::vec3( lx, hy, -lz);

    const int HW = 13, HH = 12, HD = 14;
    VoxelVolume* hv = new VoxelVolume(HW, HH, HD);
    roundBlob(hv, hide, hide, rng, 8, 1.10f);
    box(hv, 3, 1, HD - 4, HW - 4, 5, HD - 1, muzz);             // muzzle
    box(hv, 1, HH - 4, 3, 2, HH - 1, 5, horn);                 // horns
    box(hv, HW - 3, HH - 4, 3, HW - 2, HH - 1, 5, horn);
    hv->setVoxel(2, 7, HD - 1, eyeP);  hv->setVoxel(HW - 3, 7, HD - 1, eyeP);
    hv->updateMesh();
    rig.head->volume   = hv;
    rig.head->pivot    = glm::vec3(HW*0.5f, HH*0.5f, 1.0f);
    rig.head->localPos = glm::vec3(0.0f, BH*0.5f - 1.0f, BL*0.5f - 2.0f);

    const int TW = 3, TH = 16, TD = 3;
    VoxelVolume* tv = new VoxelVolume(TW, TH, TD);
    box(tv, 0, 4, 0, TW - 1, TH - 1, TD - 1, jitter(hide, rng, 6));
    box(tv, 0, 0, 0, TW - 1, 3, TD - 1, tuft);
    tv->updateMesh();
    rig.tail->volume   = tv;
    rig.tail->pivot    = glm::vec3(TW*0.5f, (float)TH, TD*0.5f);
    rig.tail->localPos = glm::vec3(0.0f, BH*0.5f - 1.0f, -BL*0.5f + 1.0f);

    return 0.044f;
}

static float buildDeer(QuadrupedRig& rig, std::mt19937& rng) {
    const Voxel coat  {138, 100,  64, 255};
    const Voxel belly {206, 184, 150, 255};
    const Voxel legc  {110,  80,  54, 255};
    const Voxel hoof  { 36,  30,  26, 255};
    const Voxel bone  {212, 198, 168, 255};
    const Voxel muzz  { 60,  48,  40, 255};
    const Voxel eyeP  { 14,  12,  10, 255};
    const int BW = 13, BH = 14, BL = 26, LH = 16;

    VoxelVolume* bv = new VoxelVolume(BW, BH, BL);
    buildBody(bv, coat, belly, rng, 8, 3);
    bv->updateMesh();
    rig.body->volume   = bv;
    rig.body->pivot    = glm::vec3(BW*0.5f, BH*0.5f, BL*0.5f);
    rig.restY          = LH + BH*0.5f - 3.0f;
    rig.body->localPos = glm::vec3(0.0f, rig.restY, 0.0f);

    makeLeg(rig.flLeg, 4, LH, 4, legc, hoof, 2);
    makeLeg(rig.frLeg, 4, LH, 4, legc, hoof, 2);
    makeLeg(rig.blLeg, 4, LH, 4, legc, hoof, 2);
    makeLeg(rig.brLeg, 4, LH, 4, legc, hoof, 2);
    float lx = BW*0.5f - 2.5f, lz = BL*0.5f - 4.0f, hy = -BH*0.5f + 3.0f;
    rig.flLeg->localPos = glm::vec3(-lx, hy,  lz);
    rig.frLeg->localPos = glm::vec3( lx, hy,  lz);
    rig.blLeg->localPos = glm::vec3(-lx, hy, -lz);
    rig.brLeg->localPos = glm::vec3( lx, hy, -lz);

    const int HW = 9, HH = 21, HD = 13;
    VoxelVolume* hv = new VoxelVolume(HW, HH, HD);
    for (int x = 0; x < HW; x++)                          // slender skull
        for (int y = 0; y < 9; y++)
            for (int z = 0; z < HD; z++) {
                float nx = (x + 0.5f - HW*0.5f) / (HW*0.5f);
                float ny = (y + 0.5f - 4.5f) / 4.5f;
                float nz = (z + 0.5f - HD*0.5f) / (HD*0.5f);
                if (nx*nx + ny*ny + nz*nz > 1.12f) continue;
                hv->setVoxel(x, y, z, jitter(coat, rng, 8));
            }
    box(hv, HW/2 - 1, 1, HD - 3, HW/2 + 1, 4, HD - 1, muzz);
    hv->setVoxel(2, 6, HD - 1, eyeP);  hv->setVoxel(HW - 3, 6, HD - 1, eyeP);
    for (int side = 0; side < 2; side++) {               // branching antlers
        int ax = side ? HW - 3 : 2;
        for (int y = 8; y < HH; y++) box(hv, ax, y, 5, ax + 1, y, 6, bone);
        box(hv, ax, 12,     7, ax + 1, 12,     9, bone);
        box(hv, ax, HH - 3, 7, ax + 1, HH - 3, 9, bone);
    }
    hv->updateMesh();
    rig.head->volume   = hv;
    rig.head->pivot    = glm::vec3(HW*0.5f, 4.5f, 1.0f);
    rig.head->localPos = glm::vec3(0.0f, BH*0.5f + 2.0f, BL*0.5f - 2.0f);

    const int TS = 4;
    VoxelVolume* tv = new VoxelVolume(TS, TS + 2, TS);
    box(tv, 0, 0, 0, TS - 1, TS + 1, TS - 1, coat);
    box(tv, 0, 0, 0, TS - 1, 1, TS - 1, belly);
    tv->updateMesh();
    rig.tail->volume   = tv;
    rig.tail->pivot    = glm::vec3(TS*0.5f, (float)(TS + 2), TS*0.5f);
    rig.tail->localPos = glm::vec3(0.0f, BH*0.5f, -BL*0.5f + 1.0f);

    return 0.040f;
}

static float buildPig(QuadrupedRig& rig, std::mt19937& rng) {
    bool  boar   = (rng() & 1u) != 0u;
    Voxel skin   = boar ? Voxel{ 94,  74,  58, 255} : Voxel{232, 170, 166, 255};
    Voxel snoutc = boar ? Voxel{ 70,  54,  44, 255} : Voxel{244, 188, 186, 255};
    Voxel legc   = skin;
    const Voxel hoof  { 40, 34, 30, 255};
    const Voxel eyeP  { 16, 14, 12, 255};
    const Voxel nostr { 30, 24, 22, 255};
    const Voxel tusk  {236, 232, 214, 255};
    const int BW = 15, BH = 14, BL = 24, LH = 8;

    VoxelVolume* bv = new VoxelVolume(BW, BH, BL);
    buildBody(bv, skin, skin, rng, 8, 3);                // chunky, very round
    bv->updateMesh();
    rig.body->volume   = bv;
    rig.body->pivot    = glm::vec3(BW*0.5f, BH*0.5f, BL*0.5f);
    rig.restY          = LH + BH*0.5f - 3.0f;
    rig.body->localPos = glm::vec3(0.0f, rig.restY, 0.0f);

    makeLeg(rig.flLeg, 5, LH, 5, legc, hoof, 2);
    makeLeg(rig.frLeg, 5, LH, 5, legc, hoof, 2);
    makeLeg(rig.blLeg, 5, LH, 5, legc, hoof, 2);
    makeLeg(rig.brLeg, 5, LH, 5, legc, hoof, 2);
    float lx = BW*0.5f - 3.0f, lz = BL*0.5f - 4.0f, hy = -BH*0.5f + 3.0f;
    rig.flLeg->localPos = glm::vec3(-lx, hy,  lz);
    rig.frLeg->localPos = glm::vec3( lx, hy,  lz);
    rig.blLeg->localPos = glm::vec3(-lx, hy, -lz);
    rig.brLeg->localPos = glm::vec3( lx, hy, -lz);

    const int HW = 11, HH = 11, HD = 11;
    VoxelVolume* hv = new VoxelVolume(HW, HH, HD);
    roundBlob(hv, skin, skin, rng, 7, 1.05f);
    box(hv, 3, 3, HD - 1, HW - 4, 6, HD - 1, snoutc);          // flat snout
    hv->setVoxel(4, 5, HD - 1, nostr);  hv->setVoxel(HW - 5, 5, HD - 1, nostr);
    hv->setVoxel(2, 8, HD - 1, eyeP);   hv->setVoxel(HW - 3, 8, HD - 1, eyeP);
    box(hv, 1, HH - 3, 4, 3, HH - 1, 6, skin);                 // ears
    box(hv, HW - 4, HH - 3, 4, HW - 2, HH - 1, 6, skin);
    if (boar) {
        hv->setVoxel(3, 2, HD - 1, tusk);     hv->setVoxel(3, 3, HD - 1, tusk);
        hv->setVoxel(HW - 4, 2, HD - 1, tusk); hv->setVoxel(HW - 4, 3, HD - 1, tusk);
    }
    hv->updateMesh();
    rig.head->volume   = hv;
    rig.head->pivot    = glm::vec3(HW*0.5f, HH*0.5f, 1.0f);
    rig.head->localPos = glm::vec3(0.0f, BH*0.5f - 3.0f, BL*0.5f - 2.0f);

    const int TS = 3;
    VoxelVolume* tv = new VoxelVolume(TS, TS, TS);
    box(tv, 0, 0, 0, TS - 1, TS - 1, TS - 1, skin);
    tv->updateMesh();
    rig.tail->volume   = tv;
    rig.tail->pivot    = glm::vec3(TS*0.5f, TS*0.5f, (float)TS);
    rig.tail->localPos = glm::vec3(0.0f, BH*0.5f, -BL*0.5f + 1.0f);

    return 0.040f;
}

static float buildSquirrel(QuadrupedRig& rig, std::mt19937& rng) {
    bool  grey  = (rng() & 1u) != 0u;
    Voxel fur   = grey ? Voxel{142, 140, 136, 255} : Voxel{156,  98,  54, 255};
    Voxel belly = grey ? Voxel{220, 218, 214, 255} : Voxel{226, 206, 178, 255};
    Voxel legc  = fur;
    const Voxel eyeP { 14, 12, 10, 255};
    const Voxel nose { 30, 24, 22, 255};
    const int BW = 7, BH = 8, BL = 11, LH = 4;

    VoxelVolume* bv = new VoxelVolume(BW, BH, BL);
    buildBody(bv, fur, belly, rng, 9, 2);
    bv->updateMesh();
    rig.body->volume   = bv;
    rig.body->pivot    = glm::vec3(BW*0.5f, BH*0.5f, BL*0.5f);
    rig.restY          = LH + BH*0.5f - 2.0f;
    rig.body->localPos = glm::vec3(0.0f, rig.restY, 0.0f);

    makeLeg(rig.flLeg, 2, LH, 2, legc, legc, 1);
    makeLeg(rig.frLeg, 2, LH, 2, legc, legc, 1);
    makeLeg(rig.blLeg, 3, LH, 3, legc, legc, 1);
    makeLeg(rig.brLeg, 3, LH, 3, legc, legc, 1);
    float hy = -BH*0.5f + 2.0f;
    rig.flLeg->localPos = glm::vec3(-(BW*0.5f - 1.3f), hy,  BL*0.5f - 2.5f);
    rig.frLeg->localPos = glm::vec3( (BW*0.5f - 1.3f), hy,  BL*0.5f - 2.5f);
    rig.blLeg->localPos = glm::vec3(-(BW*0.5f - 1.8f), hy, -(BL*0.5f - 3.0f));
    rig.brLeg->localPos = glm::vec3( (BW*0.5f - 1.8f), hy, -(BL*0.5f - 3.0f));

    const int HW = 7, HH = 11, HD = 7;
    VoxelVolume* hv = new VoxelVolume(HW, HH, HD);
    for (int x = 0; x < HW; x++)
        for (int y = 0; y < 7; y++)
            for (int z = 0; z < HD; z++) {
                float nx = (x + 0.5f - HW*0.5f) / (HW*0.5f);
                float ny = (y + 0.5f - 3.5f) / 3.5f;
                float nz = (z + 0.5f - HD*0.5f) / (HD*0.5f);
                if (nx*nx + ny*ny + nz*nz > 1.10f) continue;
                hv->setVoxel(x, y, z, jitter(fur, rng, 8));
            }
    box(hv, 1, 6, 2, 2, HH - 1, 3, fur);                       // ear tufts
    box(hv, HW - 3, 6, 2, HW - 2, HH - 1, 3, fur);
    hv->setVoxel(1, 4, HD - 1, eyeP);  hv->setVoxel(1, 5, HD - 1, eyeP);
    hv->setVoxel(HW - 2, 4, HD - 1, eyeP);  hv->setVoxel(HW - 2, 5, HD - 1, eyeP);
    hv->setVoxel(HW / 2, 2, HD - 1, nose);
    hv->updateMesh();
    rig.head->volume   = hv;
    rig.head->pivot    = glm::vec3(HW*0.5f, 3.5f, 1.0f);
    rig.head->localPos = glm::vec3(0.0f, BH*0.5f, BL*0.5f - 1.0f);

    // Huge bushy tail, curled up over the back.
    const int TW = 8, TH = 20, TD = 7;
    VoxelVolume* tv = new VoxelVolume(TW, TH, TD);
    float tcx = TW*0.5f, tcz = TD*0.5f;
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++)
            for (int z = 0; z < TD; z++) {
                float nx = (x + 0.5f - tcx) / tcx;
                float nz = (z + 0.5f - tcz) / tcz;
                if (nx*nx + nz*nz > 1.0f) continue;
                tv->setVoxel(x, y, z, jitter(fur, rng, 16));    // heavy = bushy
            }
    tv->updateMesh();
    rig.tail->volume   = tv;
    rig.tail->pivot    = glm::vec3(tcx, 1.0f, (float)TD);
    rig.tail->localRot = glm::vec3(-58.0f, 0.0f, 0.0f);         // curl up
    rig.tail->localPos = glm::vec3(0.0f, BH*0.5f, -BL*0.5f + 1.0f);

    return 0.023f;
}

static float buildFox(QuadrupedRig& rig, std::mt19937& rng) {
    const Voxel rust  {202, 112,  46, 255};
    const Voxel white {238, 236, 230, 255};
    const Voxel black { 32,  28,  26, 255};
    const Voxel eyeP  { 16,  14,  12, 255};
    const int BW = 9, BH = 9, BL = 20, LH = 7;

    VoxelVolume* bv = new VoxelVolume(BW, BH, BL);
    buildBody(bv, rust, white, rng, 8, 2);
    bv->updateMesh();
    rig.body->volume   = bv;
    rig.body->pivot    = glm::vec3(BW*0.5f, BH*0.5f, BL*0.5f);
    rig.restY          = LH + BH*0.5f - 2.0f;
    rig.body->localPos = glm::vec3(0.0f, rig.restY, 0.0f);

    makeLeg(rig.flLeg, 3, LH, 3, rust, black, 3);
    makeLeg(rig.frLeg, 3, LH, 3, rust, black, 3);
    makeLeg(rig.blLeg, 3, LH, 3, rust, black, 3);
    makeLeg(rig.brLeg, 3, LH, 3, rust, black, 3);
    float lx = BW*0.5f - 2.0f, lz = BL*0.5f - 3.5f, hy = -BH*0.5f + 2.0f;
    rig.flLeg->localPos = glm::vec3(-lx, hy,  lz);
    rig.frLeg->localPos = glm::vec3( lx, hy,  lz);
    rig.blLeg->localPos = glm::vec3(-lx, hy, -lz);
    rig.brLeg->localPos = glm::vec3( lx, hy, -lz);

    const int HW = 9, HH = 11, HD = 11;
    VoxelVolume* hv = new VoxelVolume(HW, HH, HD);
    for (int x = 0; x < HW; x++)
        for (int y = 0; y < 7; y++)
            for (int z = 0; z < HD; z++) {
                float nx = (x + 0.5f - HW*0.5f) / (HW*0.5f);
                float ny = (y + 0.5f - 3.5f) / 3.5f;
                float nz = (z + 0.5f - HD*0.5f) / (HD*0.5f);
                if (nx*nx + ny*ny + nz*nz > 1.10f) continue;
                hv->setVoxel(x, y, z, jitter(rust, rng, 8));
            }
    box(hv, 3, 0, HD - 4, HW - 4, 3, HD - 1, white);           // white muzzle
    for (int e = 0; e < 2; e++) {                              // pointed ears
        int ex = e ? HW - 3 : 1;
        box(hv, ex, 6, 2, ex + 1, 8, 4, rust);
        box(hv, ex, 9, 3, ex + 1, HH - 1, 3, black);
    }
    hv->setVoxel(2, 4, HD - 1, eyeP);  hv->setVoxel(HW - 3, 4, HD - 1, eyeP);
    hv->updateMesh();
    rig.head->volume   = hv;
    rig.head->pivot    = glm::vec3(HW*0.5f, 3.5f, 1.0f);
    rig.head->localPos = glm::vec3(0.0f, BH*0.5f, BL*0.5f - 1.0f);

    // Long bushy tail with a white tip.
    const int TW = 7, TH = 7, TD = 17;
    VoxelVolume* tv = new VoxelVolume(TW, TH, TD);
    float tcx = TW*0.5f, tcy = TH*0.5f;
    for (int x = 0; x < TW; x++)
        for (int y = 0; y < TH; y++)
            for (int z = 0; z < TD; z++) {
                float nx = (x + 0.5f - tcx) / tcx;
                float ny = (y + 0.5f - tcy) / tcy;
                if (nx*nx + ny*ny > 1.0f) continue;
                tv->setVoxel(x, y, z, jitter(z > TD - 5 ? white : rust, rng, 14));
            }
    tv->updateMesh();
    rig.tail->volume   = tv;
    rig.tail->pivot    = glm::vec3(tcx, tcy, 0.0f);
    rig.tail->localPos = glm::vec3(0.0f, BH*0.5f - 1.0f, -BL*0.5f);

    return 0.030f;
}

static float buildChicken(QuadrupedRig& rig, std::mt19937& rng) {
    bool  brown = (rng() & 1u) != 0u;
    Voxel feath = brown ? Voxel{172, 122,  80, 255} : Voxel{242, 242, 238, 255};
    const Voxel comb {214, 46, 44, 255};
    const Voxel beak {236, 168, 46, 255};
    const Voxel legc {224, 176, 60, 255};
    const Voxel eyeP { 14, 12, 10, 255};
    const int BW = 8, BH = 9, BL = 10, LH = 6;

    VoxelVolume* bv = new VoxelVolume(BW, BH, BL);
    buildBody(bv, feath, feath, rng, 7, 2);
    bv->updateMesh();
    rig.body->volume   = bv;
    rig.body->pivot    = glm::vec3(BW*0.5f, BH*0.5f, BL*0.5f);
    rig.restY          = LH + BH*0.5f - 2.0f;
    rig.body->localPos = glm::vec3(0.0f, rig.restY, 0.0f);

    // Two legs only — the front leg pair is left empty (no volume = not drawn).
    makeLeg(rig.blLeg, 2, LH, 2, legc, legc, 1);
    makeLeg(rig.brLeg, 2, LH, 2, legc, legc, 1);
    float hy = -BH*0.5f + 2.0f;
    rig.blLeg->localPos = glm::vec3(-2.0f, hy, 0.0f);
    rig.brLeg->localPos = glm::vec3( 2.0f, hy, 0.0f);

    const int HW = 6, HH = 7, HD = 7;
    VoxelVolume* hv = new VoxelVolume(HW, HH, HD);
    roundBlob(hv, feath, feath, rng, 6, 1.12f);
    box(hv, 2, HH - 1, 1, 3, HH - 1, 4, comb);                 // comb ridge
    box(hv, HW/2 - 1, 0, HD - 2, HW/2, 1, HD - 2, comb);       // wattle
    box(hv, HW/2 - 1, 2, HD - 1, HW/2, 3, HD - 1, beak);       // beak
    hv->setVoxel(1, 4, HD - 1, eyeP);  hv->setVoxel(HW - 2, 4, HD - 1, eyeP);
    hv->updateMesh();
    rig.head->volume   = hv;
    rig.head->pivot    = glm::vec3(HW*0.5f, HH*0.5f, 1.0f);
    rig.head->localPos = glm::vec3(0.0f, BH*0.5f + 2.0f, BL*0.5f - 1.0f);

    const int TW = 6, TH = 8, TD = 4;
    VoxelVolume* tv = new VoxelVolume(TW, TH, TD);
    box(tv, 1, 0, 0, TW - 2, TH - 1, TD - 1, jitter(feath, rng, 8));
    tv->updateMesh();
    rig.tail->volume   = tv;
    rig.tail->pivot    = glm::vec3(TW*0.5f, 1.0f, (float)TD);
    rig.tail->localRot = glm::vec3(-48.0f, 0.0f, 0.0f);        // feathers fan up
    rig.tail->localPos = glm::vec3(0.0f, BH*0.5f, -BL*0.5f + 1.0f);

    return 0.026f;
}

float buildAnimalRig(QuadrupedRig& rig, AnimalSpecies species, uint32_t variant) {
    std::mt19937 rng(variant ? variant : 1u);
    float baseScale;
    switch (species) {
        case AnimalSpecies::Cow:      baseScale = buildCow(rig, rng);      break;
        case AnimalSpecies::Rabbit:   baseScale = buildRabbit(rig, rng);   break;
        case AnimalSpecies::Squirrel: baseScale = buildSquirrel(rig, rng); break;
        case AnimalSpecies::Deer:     baseScale = buildDeer(rig, rng);     break;
        case AnimalSpecies::Fox:      baseScale = buildFox(rig, rng);      break;
        case AnimalSpecies::Pig:      baseScale = buildPig(rig, rng);      break;
        case AnimalSpecies::Chicken:  baseScale = buildChicken(rig, rng);  break;
        case AnimalSpecies::Sheep:
        default:                      baseScale = buildSheep(rig, rng);   break;
    }
    return baseScale * (0.92f + frand01(rng) * 0.16f);   // per-individual size
}

float animalSpeed(AnimalSpecies species) {
    switch (species) {
        case AnimalSpecies::Rabbit:   return 3.2f;
        case AnimalSpecies::Squirrel: return 3.4f;
        case AnimalSpecies::Fox:      return 3.4f;
        case AnimalSpecies::Deer:     return 3.0f;
        case AnimalSpecies::Chicken:  return 1.8f;
        case AnimalSpecies::Cow:      return 1.5f;
        case AnimalSpecies::Pig:      return 1.8f;
        case AnimalSpecies::Sheep:
        default:                      return 1.6f;
    }
}

// Prey species bolt from the player; livestock just amble.
static bool isSkittish(AnimalSpecies s) {
    return s == AnimalSpecies::Rabbit || s == AnimalSpecies::Squirrel ||
           s == AnimalSpecies::Deer   || s == AnimalSpecies::Fox      ||
           s == AnimalSpecies::Chicken;
}

// Picks a species suited to the biome at a spawn point. Biome ids:
// Plains 0, Forest 1, Desert 2, Mountains 3, Tundra 4, Savanna 5, Jungle 6.
static AnimalSpecies pickSpecies(int biome, std::mt19937& rng) {
    static const AnimalSpecies grassland[] = {
        AnimalSpecies::Sheep,  AnimalSpecies::Cow,     AnimalSpecies::Pig,
        AnimalSpecies::Rabbit, AnimalSpecies::Chicken, AnimalSpecies::Deer };
    static const AnimalSpecies woodland[] = {
        AnimalSpecies::Squirrel, AnimalSpecies::Fox, AnimalSpecies::Deer,
        AnimalSpecies::Rabbit,   AnimalSpecies::Pig };
    static const AnimalSpecies sparse[] = {
        AnimalSpecies::Rabbit, AnimalSpecies::Fox, AnimalSpecies::Deer };
    const AnimalSpecies* set;
    uint32_t n;
    switch (biome) {
        case 1: case 6: set = woodland;  n = 5; break;   // Forest, Jungle
        case 0: case 5: set = grassland; n = 6; break;   // Plains, Savanna
        default:        set = sparse;    n = 3; break;   // Desert / Mtn / Tundra
    }
    return set[rng() % n];
}

// --- AnimalDirector --------------------------------------------------------

void AnimalDirector::stepAnimal(Animal& a, float dt, World& world,
                                const std::vector<glm::vec3>& players) {
    // Plant the animal on the surface under it.
    {
        int wx = (int)floorf(a.position.x), wz = (int)floorf(a.position.z);
        int yTop = (int)floorf(a.position.y);
        int yBot = std::max(1, yTop - 7);
        for (int y = yTop; y >= yBot; y--) {
            BlockType b = world.getBlock(wx, y, wz);
            if (b != BlockType::Air && b != BlockType::Water) {
                a.groundY = (float)(y + 1);
                break;
            }
        }
        a.position.y = a.groundY;
    }

    // A skittish species bolts from a nearby player.
    if (isSkittish(a.species)) {
        float best2 = 1e18f;
        glm::vec2 threat(0.0f);
        for (const glm::vec3& p : players) {
            float dx = p.x - a.position.x, dz = p.z - a.position.z;
            float d2 = dx * dx + dz * dz;
            if (d2 < best2) { best2 = d2; threat = glm::vec2(p.x, p.z); }
        }
        if (best2 < 10.0f * 10.0f) { a.fleeTimer = 2.5f; a.fleeFrom = threat; }
    }
    if (a.fleeTimer > 0.0f) {
        a.fleeTimer -= dt;
        glm::vec2 cur(a.position.x, a.position.z);
        glm::vec2 away = cur - a.fleeFrom;
        if (glm::length(away) < 0.01f) away = glm::vec2(1.0f, 0.0f);
        away = glm::normalize(away);
        float fs = animalSpeed(a.species) * 1.8f;
        a.position.x += away.x * fs * dt;
        a.position.z += away.y * fs * dt;
        a.yaw = glm::degrees(atan2f(away.x, away.y));
        a.velocity  = glm::vec3(away.x * fs, 0.0f, away.y * fs);
        a.walking   = true;
        a.hasTarget = false;
        return;
    }

    float speed = animalSpeed(a.species);

    if (!a.hasTarget) {
        a.walking  = false;
        a.velocity = glm::vec3(0.0f);
        a.idleTimer -= dt;
        if (a.idleTimer > 0.0f) return;
        for (int tries = 0; tries < 8; tries++) {       // pick a spot on land
            float ang = frand01(rng) * 6.2831853f;
            float r   = 5.0f + frand01(rng) * 13.0f;
            int tx = (int)(a.position.x + cosf(ang) * r);
            int tz = (int)(a.position.z + sinf(ang) * r);
            if (sampleSurface(tx, tz).height < WORLD_SEA_LEVEL + 1) continue;
            a.wanderTarget = glm::vec2((float)tx + 0.5f, (float)tz + 0.5f);
            a.hasTarget    = true;
            break;
        }
        if (!a.hasTarget) a.idleTimer = 1.0f;
        return;
    }

    glm::vec2 cur(a.position.x, a.position.z);
    glm::vec2 d = a.wanderTarget - cur;
    float dist = glm::length(d);
    if (dist < 0.6f) {
        a.hasTarget = false;
        a.walking   = false;
        a.velocity  = glm::vec3(0.0f);
        a.idleTimer = 1.5f + frand01(rng) * 4.0f;
        return;
    }
    glm::vec2 dir = d / dist;
    float stepLen = std::min(speed * dt, dist);
    a.position.x += dir.x * stepLen;
    a.position.z += dir.y * stepLen;
    a.yaw = glm::degrees(atan2f(dir.x, dir.y));
    a.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
    a.walking = true;
}

void AnimalDirector::update(float dt, const std::vector<glm::vec3>& players,
                            World& world) {
    // Despawn animals that drifted away from every player.
    active.erase(std::remove_if(active.begin(), active.end(),
                     [&](const std::unique_ptr<Animal>& a) {
                         float best2 = 1e18f;
                         for (const glm::vec3& p : players) {
                             float dx = p.x - a->position.x;
                             float dz = p.z - a->position.z;
                             best2 = std::min(best2, dx * dx + dz * dz);
                         }
                         return best2 > 200.0f * 200.0f;
                     }),
                 active.end());

    // Keep a population of wildlife around each player.
    for (const glm::vec3& p : players) {
        int nearby = 0;
        for (auto& a : active) {
            float dx = p.x - a->position.x, dz = p.z - a->position.z;
            if (dx * dx + dz * dz < 150.0f * 150.0f) nearby++;
        }
        for (int tries = 0; tries < 4 && nearby < 12 && (int)active.size() < 60;
             tries++) {
            float ang = frand01(rng) * 6.2831853f;
            float r   = 70.0f + frand01(rng) * 40.0f;
            int wx = (int)(p.x + cosf(ang) * r);
            int wz = (int)(p.z + sinf(ang) * r);
            SurfaceSample s = sampleSurface(wx, wz);
            if (s.height < WORLD_SEA_LEVEL + 2) continue;    // not in water
            auto a = std::make_unique<Animal>();
            a->id      = nextId++;
            a->species = pickSpecies(s.biome, rng);          // biome-appropriate
            a->variant = (uint32_t)rng();
            a->groundY = (float)(s.height + 1);
            a->position = glm::vec3((float)wx + 0.5f, a->groundY, (float)wz + 0.5f);
            a->yaw = (float)(rng() % 360u);
            active.push_back(std::move(a));
            nearby++;
        }
    }

    for (auto& a : active) stepAnimal(*a, dt, world, players);
}
