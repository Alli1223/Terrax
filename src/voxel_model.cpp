#include "voxel_model.h"
#include <iostream>
#include <algorithm>
#include <random>

VoxelVolume::VoxelVolume(int x, int y, int z) : sizeX(x), sizeY(y), sizeZ(z) {
    voxels.resize(x * y * z, {0, 0, 0, 0});
}

VoxelVolume::~VoxelVolume() {
    if (vao) { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); }
}

void VoxelVolume::setVoxel(int x, int y, int z, Voxel v) {
    if (x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return;
    voxels[z * sizeX * sizeY + y * sizeX + x] = v;
    needsMeshUpdate = true;
}

Voxel VoxelVolume::getVoxel(int x, int y, int z) const {
    if (x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return {0,0,0,0};
    return voxels[z * sizeX * sizeY + y * sizeX + x];
}

void VoxelVolume::updateMesh() {
    if (!needsMeshUpdate) return;
    std::vector<CharacterVertex> mesh;
    auto addFace = [&](int x, int y, int z, int face, Voxel v) {
        glm::vec4 color(v.r/255.0f, v.g/255.0f, v.b/255.0f, v.a/255.0f);
        float xf = (float)x, yf = (float)y, zf = (float)z;
        static const glm::vec3 normals[] = {{0,0,1}, {0,0,-1}, {-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0}};
        static const float verts[] = {
            0,0,1, 1,0,1, 1,1,1, 1,1,1, 0,1,1, 0,0,1,
            0,0,0, 0,1,0, 1,1,0, 1,1,0, 1,0,0, 0,0,0,
            0,0,0, 0,0,1, 0,1,1, 0,1,1, 0,1,0, 0,0,0,
            1,0,0, 1,1,0, 1,1,1, 1,1,1, 1,0,1, 1,0,0,
            0,1,0, 0,1,1, 1,1,1, 1,1,1, 1,1,0, 0,1,0,
            0,0,0, 1,0,0, 1,0,1, 1,0,1, 0,0,1, 0,0,0
        };
        for (int i=0; i<6; i++) {
            mesh.push_back({glm::vec3(xf + verts[face*18 + i*3], yf + verts[face*18 + i*3 + 1], zf + verts[face*18 + i*3 + 2]), normals[face], color});
        }
    };
    for (int z=0; z<sizeZ; z++) for (int y=0; y<sizeY; y++) for (int x=0; x<sizeX; x++) {
        Voxel v = getVoxel(x, y, z);
        if (v.a == 0) continue;
        if (getVoxel(x, y, z + 1).a == 0) addFace(x, y, z, 0, v);
        if (getVoxel(x, y, z - 1).a == 0) addFace(x, y, z, 1, v);
        if (getVoxel(x - 1, y, z).a == 0) addFace(x, y, z, 2, v);
        if (getVoxel(x + 1, y, z).a == 0) addFace(x, y, z, 3, v);
        if (getVoxel(x, y + 1, z).a == 0) addFace(x, y, z, 4, v);
        if (getVoxel(x, y - 1, z).a == 0) addFace(x, y, z, 5, v);
    }
    if (!vao) glGenVertexArrays(1, &vao);
    if (!vbo) glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(CharacterVertex), mesh.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CharacterVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CharacterVertex), (void*)offsetof(CharacterVertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CharacterVertex), (void*)offsetof(CharacterVertex, color));
    glEnableVertexAttribArray(2);
    vertexCount = (int)mesh.size();
    needsMeshUpdate = false;
}

void VoxelVolume::draw() const {
    if (vertexCount == 0) return;
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

bool VoxelVolume::raycast(glm::vec3 ro, glm::vec3 rd, float maxDist, glm::ivec3& hitVoxel, glm::ivec3& hitNormal) const {
    glm::ivec3 ipos = glm::floor(ro);
    glm::vec3 rdInv = 1.0f / rd;
    glm::ivec3 step = glm::sign(rd);
    glm::vec3 tMax = (glm::vec3(ipos) + glm::vec3(std::max(step.x, 0), std::max(step.y, 0), std::max(step.z, 0)) - ro) * rdInv;
    glm::vec3 tDelta = glm::vec3(step) * rdInv;
    float dist = 0;
    while (dist < maxDist) {
        if (ipos.x >= 0 && ipos.x < sizeX && ipos.y >= 0 && ipos.y < sizeY && ipos.z >= 0 && ipos.z < sizeZ) {
            if (getVoxel(ipos.x, ipos.y, ipos.z).a > 0) { hitVoxel = ipos; return true; }
        }
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) { dist = tMax.x; tMax.x += tDelta.x; ipos.x += step.x; hitNormal = {-step.x, 0, 0}; }
            else { dist = tMax.z; tMax.z += tDelta.z; ipos.z += step.z; hitNormal = {0, 0, -step.z}; }
        } else {
            if (tMax.y < tMax.z) { dist = tMax.y; tMax.y += tDelta.y; ipos.y += step.y; hitNormal = {0, -step.y, 0}; }
            else { dist = tMax.z; tMax.z += tDelta.z; ipos.z += step.z; hitNormal = {0, 0, -step.z}; }
        }
    }
    return false;
}

// Small voxel lantern — dark metal frame around a glowing yellow
// "flame" cell, plus a tiny handle on top. Built once per rig and
// cached on `lanternMeshCache`; the lantern CharacterNode borrows the
// pointer when visible and we null it out before the destructor runs
// to avoid the node's automatic delete double-freeing our cache.
static VoxelVolume* buildLanternMesh() {
    VoxelVolume* v = new VoxelVolume(3, 5, 3);
    Voxel frame = { 55,  45,  35, 255};
    Voxel cap   = { 35,  28,  22, 255};
    Voxel flame = {255, 215, 110, 255};
    Voxel ember = {255, 165,  60, 255};

    // Bottom base — dark frame perimeter.
    for (int x = 0; x < 3; x++) for (int z = 0; z < 3; z++)
        v->setVoxel(x, 0, z, frame);
    // Two flame rows — bright centre with darker corners (so it reads
    // as a glowing cell behind a corner frame).
    for (int x = 0; x < 3; x++) for (int z = 0; z < 3; z++) {
        bool corner = (x != 1 && z != 1);
        v->setVoxel(x, 1, z, corner ? frame : flame);
        v->setVoxel(x, 2, z, corner ? frame : ember);
    }
    // Cap.
    for (int x = 0; x < 3; x++) for (int z = 0; z < 3; z++)
        v->setVoxel(x, 3, z, cap);
    // Handle — single voxel above centre.
    v->setVoxel(1, 4, 1, cap);
    v->updateMesh();
    return v;
}

BipedalRig::BipedalRig() {
    root = new CharacterNode("Root");
    torso = new CharacterNode("Torso");
    head = new CharacterNode("Head");
    lArm = new CharacterNode("LArm");
    rArm = new CharacterNode("RArm");
    lLeg = new CharacterNode("LLeg");
    rLeg = new CharacterNode("RLeg");
    sword       = new CharacterNode("MainHandWeapon");
    offHand     = new CharacterNode("OffHandWeapon");
    bowString   = new CharacterNode("BowString");
    quiver      = new CharacterNode("Quiver");
    lantern     = nullptr;   // retired — see lanternBelt + lanternHand below
    lanternBelt = new CharacterNode("LanternBelt");
    lanternHand = new CharacterNode("LanternHand");
    root->addChild(torso);
    torso->addChild(head);
    torso->addChild(lArm);
    torso->addChild(rArm);
    torso->addChild(lLeg);
    torso->addChild(rLeg);
    rArm->addChild(sword);
    lArm->addChild(offHand);
    // The bow body lives on the off-hand (see weapon_builder.cpp). The
    // string is a child of the bow so it pivots with the arm.
    offHand->addChild(bowString);
    // Quiver attached to the upper back of the torso.
    torso->addChild(quiver);
    // Two lantern anchor points — the belt version hangs from the
    // torso (left hip) and the hand version is a child of rArm so it
    // follows the right hand naturally when the arm raises. Only one
    // is ever visible at a time (volume swap each frame in update()).
    torso->addChild(lanternBelt);
    rArm->addChild(lanternHand);
    lanternMeshCache = buildLanternMesh();
    // Pivot at the bottom-centre of the lantern so localPos points at
    // the hook the lantern is hanging from.
    lanternBelt->pivot = glm::vec3(1.0f, 4.0f, 1.0f);
    lanternHand->pivot = glm::vec3(1.0f, 4.0f, 1.0f);
    // Resting positions — tweaked to read as "on the belt" / "in hand".
    // Belt: left hip, low on the torso. Hand: at the grip of the right
    // hand (which is at y=-6 from the shoulder in rArm-local space).
    lanternBelt->localPos = glm::vec3(1.0f, 2.0f, 4.0f);
    lanternHand->localPos = glm::vec3(3.0f, -7.0f, 3.0f);
}

// Custom destructor — clear BOTH lantern nodes' borrowed volume
// pointers so the inherited CharacterNode destructor doesn't try to
// double-free the cached mesh we own here. Base CharacterRig
// destructor then deletes root + children without touching the cache.
BipedalRig::~BipedalRig() {
    if (lanternBelt) lanternBelt->volume = nullptr;
    if (lanternHand) lanternHand->volume = nullptr;
    delete lanternMeshCache;
    lanternMeshCache = nullptr;
}

void BipedalRig::setupDefaultHuman(bool male) {
    skinColor = male ? Voxel{210, 160, 130, 255} : Voxel{230, 180, 150, 255};
    Voxel skin = skinColor;
    Voxel shirt = male ? Voxel{50, 100, 200, 255} : Voxel{200, 50, 100, 255};
    Voxel pants = {30, 30, 30, 255};

    // --- Torso (Short) ---
    torso->volume = new VoxelVolume(10, 8, 8);
    torso->pivot = glm::vec3(5, 0, 4);
    torso->localPos = glm::vec3(0, 7, 0); 
    for(int x=0; x<10; x++) for(int y=0; y<8; y++) for(int z=0; z<8; z++) torso->volume->setVoxel(x, y, z, shirt);
    torso->volume->updateMesh();

    // --- Head (Large) ---
    head->volume = new VoxelVolume(24, 24, 24);
    head->pivot = glm::vec3(12, 0, 12);
    head->localPos = glm::vec3(0, 8, 0); 
    for(int x=6; x<18; x++) for(int y=0; y<12; y++) for(int z=6; z<18; z++) {
        float dx=x+0.5f-12.0f, dy=y+0.5f-6.0f, dz=z+0.5f-12.0f;
        if (dx*dx + dy*dy + dz*dz <= 72.25f) head->volume->setVoxel(x, y, z, skin);
    }
    head->volume->updateMesh();

    // --- Arms ---
    for (auto arm : {lArm, rArm}) {
        arm->volume = new VoxelVolume(6, 8, 6);
        arm->pivot = glm::vec3(3, 7, 3);
        for(int x=1; x<5; x++) for(int y=0; y<7; y++) for(int z=1; z<5; z++) arm->volume->setVoxel(x, y, z, (y > 4 ? shirt : skin));
        for(int x=0; x<6; x++) for(int y=6; y<8; y++) for(int z=0; z<6; z++) arm->volume->setVoxel(x, y, z, shirt);
        arm->volume->updateMesh();
    }
    lArm->localPos = glm::vec3(-5, 7, 0); 
    rArm->localPos = glm::vec3(5, 7, 0);

    // --- Legs (Stumpy) ---
    for (auto leg : {lLeg, rLeg}) {
        leg->volume = new VoxelVolume(7, 7, 7); 
        leg->pivot = glm::vec3(3.5f, 7, 3.5f);
        for(int x=0; x<7; x++) for(int y=0; y<7; y++) for(int z=0; z<7; z++) leg->volume->setVoxel(x, y, z, pants);
        for(int x=0; x<7; x++) for(int y=0; y<2; y++) for(int z=0; z<7; z++) leg->volume->setVoxel(x, y, z, {20, 20, 20, 255});
        leg->volume->updateMesh();
    }
    lLeg->localPos = glm::vec3(-2.5, 0, 0);
    rLeg->localPos = glm::vec3(2.5, 0, 0);

    // --- Weapon ---
    sword->volume = new VoxelVolume(4, 20, 2);
    sword->pivot = glm::vec3(2, 2, 1);
    sword->localPos = glm::vec3(0, -6, 0); 
    sword->localRot = glm::vec3(90, 0, 0);
    for(int x=0; x<4; x++) for(int y=0; y<20; y++) for(int z=0; z<2; z++) {
        if (y < 4) sword->volume->setVoxel(x, y, z, {100, 60, 20, 255});
        else if (y < 6) sword->volume->setVoxel(x, y, z, {50, 50, 50, 255});
        else sword->volume->setVoxel(x, y, z, {180, 180, 190, 255});
    }
    sword->volume->updateMesh();

    applyCustomization();
}

// ---------------------------------------------------------------------------
// Animation pipeline
// ---------------------------------------------------------------------------
// `update()` composes the per-frame pose from a small number of named
// pose helpers. Each helper is responsible for ONE thing and writes to
// the body parts it owns. Later helpers in the chain override earlier
// ones for the parts they touch — that's how combat overlays beat
// walking, and clips beat both.
//
// Order matters:
//   1. Breathing          (torso + head)
//   2. Base locomotion    (arms + legs walk/idle)
//   3. Combat overlays    (attack / block / bow draw / cast)
//   4. Carry overlays     (lantern hold)
//   5. One-shot clips     (wave / cheer / etc — last word)
//   6. Misc child-node bookkeeping (bow string slide, lantern visibility)

static void approachAngle(float& cur, float target, float dt, float speed) {
    cur = glm::mix(cur, target, std::min(1.0f, dt * speed));
}

void BipedalRig::playClip(ClipKind kind, float durationSeconds) {
    AnimationClip c;
    c.kind     = kind;
    c.duration = (durationSeconds > 0.0f) ? durationSeconds : 1.0f;
    c.elapsed  = 0.0f;
    activeClips.push_back(c);
}

// ---- Pose helpers ----------------------------------------------------

namespace {

// Breath rocks the upper body gently so a standing-still character
// looks alive instead of frozen.
void applyBreathingPose(BipedalRig& r, float /*dt*/) {
    float breathe = sinf(r.animTime * 2.0f) * 1.5f;
    if (r.torso) r.torso->localRot.x = breathe;
    if (r.head)  r.head->localRot.x  = -breathe * 0.5f;
}

// Arms swing opposite the legs in a sin wave when walking; relaxes
// smoothly back to zero when standing still.
void applyBaseLocomotion(BipedalRig& r, float dt, float velocity) {
    if (!r.lArm || !r.rArm || !r.lLeg || !r.rLeg) return;
    if (velocity > 0.1f) {
        float swing = sinf(r.animTime * velocity * 0.5f * 5.0f) * 30.0f;
        r.lArm->localRot = glm::vec3(swing, 0.0f, 0.0f);
        r.rArm->localRot = glm::vec3(-swing, 0.0f, 0.0f);
        r.lLeg->localRot.x = -swing;
        r.rLeg->localRot.x =  swing;
    } else {
        approachAngle(r.lArm->localRot.x, 0.0f, dt, 5.0f);
        approachAngle(r.lArm->localRot.y, 0.0f, dt, 5.0f);
        approachAngle(r.lArm->localRot.z, 0.0f, dt, 5.0f);
        approachAngle(r.rArm->localRot.x, 0.0f, dt, 5.0f);
        approachAngle(r.rArm->localRot.y, 0.0f, dt, 5.0f);
        approachAngle(r.rArm->localRot.z, 0.0f, dt, 5.0f);
        approachAngle(r.lLeg->localRot.x, 0.0f, dt, 5.0f);
        approachAngle(r.rLeg->localRot.x, 0.0f, dt, 5.0f);
    }
}

// Right-arm sword swing — bell curve over `attackAnim` clamped to 1.0.
// Clears `isAttacking` when the swing finishes.
void applyAttackPose(BipedalRig& r, float dt) {
    if (!r.isAttacking || !r.rArm) return;
    r.attackAnim += dt * 5.0f;
    if (r.attackAnim > 1.0f) { r.isAttacking = false; r.attackAnim = 0.0f; }
    float swing = sinf(r.attackAnim * 3.14159f) * 90.0f;
    r.rArm->localRot.x = -swing;
    r.rArm->localRot.y =  swing * 0.5f;
}

// Both arms raise forward in a casting motion (used by staves).
void applyCastingPose(BipedalRig& r, float dt) {
    if (!r.isCasting) return;
    r.castAnim += dt * 4.5f;
    if (r.castAnim > 1.0f) { r.isCasting = false; r.castAnim = 0.0f; }
    float bell = std::sin(r.castAnim * 3.14159f);
    float lift = -65.0f - bell * 25.0f;
    if (r.rArm) {
        r.rArm->localRot.x = lift;
        r.rArm->localRot.y = -10.0f - bell * 15.0f;
        r.rArm->localRot.z =  10.0f * bell;
    }
    if (r.lArm) {
        r.lArm->localRot.x = lift;
        r.lArm->localRot.y =  10.0f + bell * 15.0f;
        r.lArm->localRot.z = -10.0f * bell;
    }
}

// Off-hand shield raised in front when blocking.
void applyBlockingPose(BipedalRig& r, float dt) {
    if (!r.isBlocking || !r.lArm) return;
    approachAngle(r.lArm->localRot.x, -85.0f, dt, 12.0f);
    approachAngle(r.lArm->localRot.y,  15.0f, dt, 12.0f);
}

// Right arm pulls back, left arm extends forward to hold the bow.
void applyBowDrawPose(BipedalRig& r, float dt) {
    if (r.bowDrawAmount <= 0.0f || r.isAttacking) return;
    float pullBack = r.bowDrawAmount * 95.0f;
    if (r.rArm) {
        approachAngle(r.rArm->localRot.x, -pullBack, dt, 10.0f);
        approachAngle(r.rArm->localRot.y,   25.0f,   dt, 10.0f);
    }
    if (r.lArm && !r.isBlocking) {
        approachAngle(r.lArm->localRot.x, -88.0f, dt, 10.0f);
        approachAngle(r.lArm->localRot.y, -10.0f, dt, 10.0f);
    }
}

// Right arm raised to hold the lantern out in front. Only kicks in
// when the player has the lantern AND lanternHeld is true AND no
// higher-priority animation is playing.
void applyLanternHoldPose(BipedalRig& r, float dt) {
    if (!r.hasLantern || !r.lanternHeld || !r.rArm) return;
    if (r.isAttacking || r.isCasting || r.bowDrawAmount > 0.0f) return;
    approachAngle(r.rArm->localRot.x, -75.0f, dt, 8.0f);
    approachAngle(r.rArm->localRot.y, -15.0f, dt, 8.0f);
    approachAngle(r.rArm->localRot.z,   0.0f, dt, 8.0f);
}

// Toggle lantern mesh between belt anchor (child of torso) and hand
// anchor (child of rArm). Only one is ever visible at a time so the
// cached mesh is "borrowed" by whichever node is on duty.
void applyLanternVisibility(BipedalRig& r) {
    if (!r.lanternBelt || !r.lanternHand) return;
    if (r.hasLantern && r.lanternHeld) {
        r.lanternBelt->volume = nullptr;
        r.lanternHand->volume = r.lanternMeshCache;
    } else if (r.hasLantern) {
        r.lanternBelt->volume = r.lanternMeshCache;
        r.lanternHand->volume = nullptr;
    } else {
        r.lanternBelt->volume = nullptr;
        r.lanternHand->volume = nullptr;
    }
}

// Bow string slide + counter-rotate the bow body so it stays vertical
// regardless of the left arm's pitch.
void applyBowStringPose(BipedalRig& r) {
    if (!r.bowString || !r.bowString->volume || !r.offHand || !r.lArm) return;
    r.offHand->localRot.x = -r.lArm->localRot.x;
    r.offHand->localRot.y = -r.lArm->localRot.y;
    r.bowString->localPos.z = -r.bowDrawAmount * 4.0f;
}

// Per-clip pose: each ClipKind expresses its own animation as a
// function of `t = elapsed / duration` (0..1). Wins over whatever the
// base + overlays set because clips run last.
void applyClipPose(BipedalRig& r, const AnimationClip& clip) {
    float t = (clip.duration > 0.0f) ? (clip.elapsed / clip.duration) : 1.0f;
    if (t > 1.0f) t = 1.0f;
    switch (clip.kind) {
        case ClipKind::Wave: {
            // Right arm straight up, hand sways side-to-side.
            if (r.rArm) {
                r.rArm->localRot.x = -160.0f;
                r.rArm->localRot.y = 0.0f;
                r.rArm->localRot.z = 35.0f * std::sin(t * 12.0f);
            }
            break;
        }
        case ClipKind::Cheer: {
            // Both arms up in a celebratory pump, head tilts back.
            float bob = std::sin(t * 6.0f);
            if (r.rArm) { r.rArm->localRot.x = -150.0f - 20.0f * bob;
                          r.rArm->localRot.z =   20.0f; }
            if (r.lArm) { r.lArm->localRot.x = -150.0f - 20.0f * bob;
                          r.lArm->localRot.z =  -20.0f; }
            if (r.head)  r.head->localRot.x = -8.0f;
            break;
        }
        case ClipKind::Crouch: {
            // Torso drops, knees fold forward. Time-aware so the
            // crouch eases in over the first 30% then holds.
            float drop = std::min(1.0f, t / 0.3f) * 20.0f;
            if (r.torso) r.torso->localRot.x = drop;
            if (r.lLeg)  r.lLeg->localRot.x  = -drop * 1.4f;
            if (r.rLeg)  r.rLeg->localRot.x  = -drop * 1.4f;
            break;
        }
    }
}

}  // namespace

void BipedalRig::update(float dt, float velocity) {
    animTime += dt;

    applyBreathingPose(*this, dt);
    applyBaseLocomotion(*this, dt, velocity);

    // Combat overlays — later ones win for any part they touch.
    applyAttackPose(*this, dt);
    applyCastingPose(*this, dt);
    applyBlockingPose(*this, dt);
    applyBowDrawPose(*this, dt);

    // Carry overlay — the arm raises to "hold" the lantern (only fires
    // if no higher-priority animation is currently using rArm).
    applyLanternHoldPose(*this, dt);

    // Per-frame derived state — lantern visibility, bow string offset.
    applyLanternVisibility(*this);
    applyBowStringPose(*this);

    // One-shot clips — wins over everything for the parts they touch.
    for (auto& clip : activeClips) {
        clip.elapsed += dt;
        applyClipPose(*this, clip);
    }
    activeClips.erase(
        std::remove_if(activeClips.begin(), activeClips.end(),
                       [](const AnimationClip& c){ return c.done(); }),
        activeClips.end());

    // Held lantern hangs from the handle — counter-rotate against the
    // final right-arm orientation (after every other animation pass has
    // settled it) so the lantern body always points world-down, like
    // gravity is pulling it. The pivot at the top of the lantern mesh
    // means the body swings beneath wherever the handle ends up.
    if (lanternHand && lanternHand->volume && rArm) {
        lanternHand->localRot.x = -rArm->localRot.x;
        lanternHand->localRot.y = -rArm->localRot.y;
        lanternHand->localRot.z = -rArm->localRot.z;
    }
}

void BipedalRig::applyCustomization() {
    if (!head || !head->volume || !torso || !torso->volume) return;
    Voxel skin     = skinColor;
    Voxel noseSkin = {(uint8_t)(skinColor.r * 9 / 10),
                      (uint8_t)(skinColor.g * 87 / 100),
                      (uint8_t)(skinColor.b * 85 / 100), 255};
    Voxel hair     = hairColor;
    Voxel eye      = eyeColor;
    Voxel white    = {255, 255, 255, 255};

    // --- Reset head to blank sphere ---
    for(int x=0; x<24; x++) for(int y=0; y<24; y++) for(int z=0; z<24; z++)
        head->volume->setVoxel(x, y, z, {0,0,0,0});
    for(int x=6; x<18; x++) for(int y=0; y<12; y++) for(int z=6; z<18; z++) {
        float dx=x+0.5f-12.0f, dy=y+0.5f-6.0f, dz=z+0.5f-12.0f;
        if (dx*dx + dy*dy + dz*dz <= 72.25f) head->volume->setVoxel(x, y, z, skin);
    }

    // --- Eyebrows ---
    switch (eyebrowStyle) {
        case 1: // Arched
            head->volume->setVoxel(8,8,18,hair);  head->volume->setVoxel(9,9,18,hair);  head->volume->setVoxel(10,10,18,hair);
            head->volume->setVoxel(13,10,18,hair); head->volume->setVoxel(14,9,18,hair); head->volume->setVoxel(15,8,18,hair); break;
        case 2: // Thick
            for(int ex:{8,9,10,13,14,15}) { head->volume->setVoxel(ex,9,18,hair); head->volume->setVoxel(ex,10,18,hair); } break;
        default: // Straight (0) and fallback
            for(int ex:{8,9,10,13,14,15}) head->volume->setVoxel(ex,9,18,hair);
            break;
    }

    // --- Nose ---
    switch (noseStyle) {
        case 1: // Wide
            for(int nx=10; nx<=13; nx++) head->volume->setVoxel(nx,5,18,noseSkin);
            break;
        case 2: // Narrow
            head->volume->setVoxel(12,5,18,noseSkin);
            head->volume->setVoxel(12,6,18,noseSkin);
            break;
        case 3: // Upturned
            head->volume->setVoxel(11,6,18,noseSkin); head->volume->setVoxel(12,6,18,noseSkin);
            head->volume->setVoxel(11,5,17,noseSkin); head->volume->setVoxel(12,5,17,noseSkin);
            break;
        case 4: // Broad
            for(int nx=10; nx<=13; nx++) {
                head->volume->setVoxel(nx,4,18,noseSkin);
                head->volume->setVoxel(nx,5,18,noseSkin);
            }
            head->volume->setVoxel(11,6,18,noseSkin); head->volume->setVoxel(12,6,18,noseSkin);
            break;
        default: // Button (0)
            head->volume->setVoxel(11,5,18,noseSkin);
            head->volume->setVoxel(12,5,18,noseSkin);
            break;
    }

    // --- Eyes ---
    switch (eyeType) {
        case 1: // Happy
            for(int ex:{8,9,10,13,14,15}) {
                head->volume->setVoxel(ex,7,17,eye);
                if(ex==8||ex==10||ex==13||ex==15) head->volume->setVoxel(ex,6,17,eye);
            }
            break;
        case 4: // Heart
            for(int dx:{0,6}) {
                int bx=8+dx;
                head->volume->setVoxel(bx,7,17,eye); head->volume->setVoxel(bx+2,7,17,eye);
                for(int i=0; i<3; i++) head->volume->setVoxel(bx+i,6,17,eye);
                head->volume->setVoxel(bx+1,5,17,eye);
            }
            break;
        default: // Classic (0) and fallback
            for(int ex:{8,9,14,15}) for(int ey:{6,7}) {
                head->volume->setVoxel(ex,ey,17,white);
                if(ey==7) head->volume->setVoxel(ex,ey,17,eye);
            }
            break;
    }

    // --- Ears ---
    switch (earType) {
        case 1: // Human
            for(int ey:{4,5}) for(int ez:{11,12}) {
                head->volume->setVoxel(5,ey,ez,skin);
                head->volume->setVoxel(18,ey,ez,skin);
            }
            break;
        case 2: // Elven (tall with pointed tip)
            for(int ey=3; ey<=6; ey++) for(int ez:{11,12}) {
                head->volume->setVoxel(5,ey,ez,skin);
                head->volume->setVoxel(18,ey,ez,skin);
            }
            head->volume->setVoxel(4,7,11,skin);
            head->volume->setVoxel(19,7,11,skin);
            break;
        case 3: // Rounded (wide and rounded)
            for(int ey=3; ey<=6; ey++) for(int ez=10; ez<=13; ez++) {
                head->volume->setVoxel(4,ey,ez,skin);
                head->volume->setVoxel(19,ey,ez,skin);
            }
            for(int ez:{10,11,12,13}) {
                head->volume->setVoxel(3,4,ez,skin); head->volume->setVoxel(3,5,ez,skin);
                head->volume->setVoxel(20,4,ez,skin); head->volume->setVoxel(20,5,ez,skin);
            }
            break;
        case 4: // Wide (large flared)
            for(int ey:{3,4,5,6}) for(int ez:{10,11,12,13}) {
                head->volume->setVoxel(3,ey,ez,skin);  head->volume->setVoxel(4,ey,ez,skin);
                head->volume->setVoxel(19,ey,ez,skin); head->volume->setVoxel(20,ey,ez,skin);
            }
            break;
        default: // None (0)
            break;
    }

    // --- Hair ---
    // Helper: paint sphere top surface at y=11
    auto paintTopCap = [&]() {
        for(int x=7; x<17; x++) for(int z=7; z<17; z++) {
            float dx=x+0.5f-12.0f, dy=5.5f, dz=z+0.5f-12.0f;
            if(dx*dx+dy*dy+dz*dz <= 72.25f) head->volume->setVoxel(x,11,z,hair);
        }
    };

    switch (hairStyle) {
        case 0: // Bald
            break;
        case 1: // Crew Cut
            paintTopCap();
            for(int x=8; x<16; x++) for(int z=8; z<16; z++) head->volume->setVoxel(x,12,z,hair);
            break;
        case 2: // Messy Short
            paintTopCap();
            for(int x=8; x<16; x++) for(int z=8; z<16; z++) {
                if((x+z)%2 == 0)    head->volume->setVoxel(x,12,z,hair);
                if((x*2+z)%5 == 0)  head->volume->setVoxel(x,13,z,hair);
            }
            break;
        case 3: // Mohawk
            for(int z=7; z<17; z++) { head->volume->setVoxel(11,11,z,hair); head->volume->setVoxel(12,11,z,hair); }
            for(int y=12; y<=16; y++) for(int z=7; z<17; z++) { head->volume->setVoxel(11,y,z,hair); head->volume->setVoxel(12,y,z,hair); }
            break;
        case 4: // Spiky
            paintTopCap();
            for(int sx=8; sx<17; sx+=2) for(int sz=8; sz<17; sz+=2) {
                int ht = 12 + (sx+sz)%4;
                for(int y=12; y<=ht; y++) head->volume->setVoxel(sx,y,sz,hair);
            }
            break;
        case 5: // Side Swept (swept right)
            for(int x=6; x<18; x++) for(int z=6; z<18; z++) {
                float dx=x+0.5f-12.0f, dy=5.5f, dz=z+0.5f-12.0f;
                if(dx*dx+dy*dy+dz*dz <= 72.25f) head->volume->setVoxel(x,11,z,hair);
            }
            for(int x=7; x<17; x++) for(int z=7; z<17; z++) head->volume->setVoxel(x,12,z,hair);
            for(int y=8; y<=12; y++) for(int z=7; z<17; z++) {
                head->volume->setVoxel(17,y,z,hair);
                head->volume->setVoxel(18,y,z,hair);
            }
            break;
        case 6: // Bob (chin length, all sides)
            paintTopCap();
            for(int y=3; y<=11; y++) {
                for(int z=6; z<18; z++) { head->volume->setVoxel(5,y,z,hair); head->volume->setVoxel(18,y,z,hair); }
                for(int x=5; x<19; x++) head->volume->setVoxel(x,y,5,hair);
            }
            for(int x=5; x<19; x++) for(int z=5; z<19; z++) head->volume->setVoxel(x,3,z,hair);
            break;
        case 7: // Long Straight
            paintTopCap();
            for(int y=0; y<=11; y++) {
                for(int z=6; z<18; z++) { head->volume->setVoxel(5,y,z,hair); head->volume->setVoxel(18,y,z,hair); }
                for(int x=5; x<19; x++) head->volume->setVoxel(x,y,5,hair);
            }
            break;
        case 8: // Wavy Long
            paintTopCap();
            for(int y=0; y<=11; y++) {
                int w = (y/2)%2;
                for(int z=6; z<18; z++) { head->volume->setVoxel(5+w,y,z,hair); head->volume->setVoxel(18-w,y,z,hair); }
                for(int x=5+w; x<19-w; x++) head->volume->setVoxel(x,y,5+w,hair);
            }
            break;
        case 9: // Bun
            for(int y=12; y<=15; y++) {
                int r = 3 - (y - 12);
                for(int x=12-r; x<=12+r; x++) for(int z=12-r; z<=12+r; z++) head->volume->setVoxel(x,y,z,hair);
            }
            for(int x=8; x<16; x++) for(int z=8; z<16; z++) head->volume->setVoxel(x,11,z,hair);
            for(int y=7; y<=11; y++) for(int x=7; x<17; x++) head->volume->setVoxel(x,y,5,hair);
            break;
        case 10: // Pigtails
            paintTopCap();
            for(int y=1; y<=8; y++) for(int z=9; z<=14; z++) {
                head->volume->setVoxel(5,y,z,hair);
                if(y<=6) head->volume->setVoxel(4,y,z,hair);
                head->volume->setVoxel(18,y,z,hair);
                if(y<=6) head->volume->setVoxel(19,y,z,hair);
            }
            break;
        case 11: // Braided (center back braid with side curtains)
            paintTopCap();
            for(int y=0; y<=11; y++) {
                bool cross = (y/2)%2 == 0;
                int bx0 = cross ? 10 : 11, bx1 = cross ? 13 : 14;
                for(int x=bx0; x<=bx1; x++) head->volume->setVoxel(x,y,5,hair);
                if(y%2==0) for(int x=bx0; x<=bx1; x++) head->volume->setVoxel(x,y,4,hair);
                for(int z=6; z<18; z++) { head->volume->setVoxel(5,y,z,hair); head->volume->setVoxel(18,y,z,hair); }
            }
            break;
    }

    // Old monolithic `armorType` torso overlay removed — armour is now an
    // Item layered on top of the base body via paintClothingOnto() in
    // items.cpp.

    torso->scale.x = weightScale;
    torso->scale.z = weightScale;

    // --- Update arm skin (y=0-4 is skin, y=5+ is shirt) ---
    for (auto arm : {lArm, rArm}) {
        if (!arm || !arm->volume) continue;
        for(int x=1; x<5; x++) for(int y=0; y<5; y++) for(int z=1; z<5; z++)
            arm->volume->setVoxel(x, y, z, skinColor);
        arm->volume->updateMesh();
    }

    head->volume->updateMesh();
    torso->volume->updateMesh();
}

void BipedalRig::resetBaseBody() {
    Voxel skin = skinColor;
    if (torso && torso->volume) {
        for (int x = 0; x < 10; x++) for (int y = 0; y < 8; y++) for (int z = 0; z < 8; z++)
            torso->volume->setVoxel(x, y, z, skin);
        torso->volume->updateMesh();
    }
    for (auto arm : {lArm, rArm}) {
        if (!arm || !arm->volume) continue;
        for (int x = 0; x < 6; x++) for (int y = 0; y < 8; y++) for (int z = 0; z < 6; z++)
            arm->volume->setVoxel(x, y, z, {0, 0, 0, 0});
        // Forearm core + shoulder cap → bare skin.
        for (int x = 1; x < 5; x++) for (int y = 0; y < 7; y++) for (int z = 1; z < 5; z++)
            arm->volume->setVoxel(x, y, z, skin);
        for (int x = 0; x < 6; x++) for (int y = 6; y < 8; y++) for (int z = 0; z < 6; z++)
            arm->volume->setVoxel(x, y, z, skin);
        arm->volume->updateMesh();
    }
    for (auto leg : {lLeg, rLeg}) {
        if (!leg || !leg->volume) continue;
        for (int x = 0; x < 7; x++) for (int y = 0; y < 7; y++) for (int z = 0; z < 7; z++)
            leg->volume->setVoxel(x, y, z, skin);
        leg->volume->updateMesh();
    }
}

void BipedalRig::randomizeAppearance() {
    static std::mt19937 rng(std::random_device{}());
    randomizeAppearance(rng);
}

void BipedalRig::randomizeAppearance(std::mt19937& rng) {
    auto pick = [&](int n) -> int { return std::uniform_int_distribution<int>(0, n - 1)(rng); };

    static const Voxel skinPalette[] = {
        {255, 224, 196, 255}, {240, 200, 168, 255}, {220, 175, 140, 255},
        {195, 148, 110, 255}, {165, 118,  76, 255}, {135,  90,  52, 255},
        {100,  62,  32, 255}, { 72,  42,  20, 255},
    };
    static const Voxel hairPalette[] = {
        { 15,  12,   8, 255}, { 55,  32,  12, 255}, { 90,  55,  22, 255},
        {140,  90,  40, 255}, {160,  65,  25, 255}, {185, 150,  70, 255},
        {215, 190, 115, 255}, {240, 225, 180, 255}, {185,  55,  25, 255},
        {160, 155, 150, 255},
    };
    static const Voxel eyePalette[] = {
        { 65,  38,  15, 255}, { 90,  65,  22, 255}, { 55, 110,  50, 255},
        { 42,  82, 145, 255}, { 85, 110, 130, 255}, { 25,  18,  10, 255},
    };

    skinColor    = skinPalette[pick(8)];
    hairColor    = hairPalette[pick(10)];
    eyeColor     = eyePalette[pick(6)];
    hairStyle    = pick(12);
    earType      = pick(5);
    noseStyle    = pick(5);
    eyebrowStyle = pick(3);
    eyeType      = pick(2) == 0 ? 0 : 1;
    armorType    = 0;

    applyCustomization();
}

QuadrupedRig::QuadrupedRig() {
    root = new CharacterNode("Root"); body = new CharacterNode("Body"); head = new CharacterNode("Head");
    flLeg = new CharacterNode("FLLeg"); frLeg = new CharacterNode("FRLeg"); blLeg = new CharacterNode("BLLeg");
    brLeg = new CharacterNode("BRLeg"); tail = new CharacterNode("Tail");
    root->addChild(body); body->addChild(head); body->addChild(flLeg); body->addChild(frLeg);
    body->addChild(blLeg); body->addChild(brLeg); body->addChild(tail);
}

void QuadrupedRig::update(float dt, float velocity) {
    animTime += dt;
    body->localPos.y = restY + sinf(animTime * 1.5f) * 0.2f;
    head->localRot.x = sinf(animTime * 1.5f) * 1.0f;
    if (velocity > 0.1f) {
        float swing = sinf(animTime * velocity * 0.5f * 5.0f) * 25.0f;
        flLeg->localRot.x = swing;  frLeg->localRot.x = -swing;
        blLeg->localRot.x = -swing; brLeg->localRot.x = swing;
        tail->localRot.y = sinf(animTime * 5.0f) * 20.0f;
    } else {
        float k = std::min(1.0f, dt * 8.0f);   // settle the legs when standing
        flLeg->localRot.x = glm::mix(flLeg->localRot.x, 0.0f, k);
        frLeg->localRot.x = glm::mix(frLeg->localRot.x, 0.0f, k);
        blLeg->localRot.x = glm::mix(blLeg->localRot.x, 0.0f, k);
        brLeg->localRot.x = glm::mix(brLeg->localRot.x, 0.0f, k);
    }
}

// --- House generator ---------------------------------------------------------

Voxel houseBlockColor(BlockType t) {
    switch (t) {
        case BlockType::Wood:      return {150, 103,  58, 255};
        case BlockType::Stone:     return {128, 128, 134, 255};
        case BlockType::Dirt:      return {120,  85,  55, 255};
        case BlockType::Grass:     return { 96, 158,  72, 255};
        case BlockType::Sand:      return {221, 205, 152, 255};
        case BlockType::Sandstone: return {223, 209, 162, 255};
        case BlockType::Gravel:    return {116, 110, 104, 255};
        case BlockType::Snow:      return {243, 246, 252, 255};
        case BlockType::Ice:       return {165, 208, 232, 255};
        case BlockType::Glowstone: return {255, 224, 138, 255};
        case BlockType::Leaves:    return { 66, 122,  52, 255};
        case BlockType::Cactus:    return { 84, 134,  62, 255};
        case BlockType::Glass:     return {200, 225, 238, 255};
        default: {
            int pidx = (int)t - (int)BlockType::PaintFirst;
            if (pidx >= 0 && pidx < PAINT_COUNT)
                return { PAINT_PALETTE[pidx].r, PAINT_PALETTE[pidx].g,
                         PAINT_PALETTE[pidx].b, 255 };
            return {0, 0, 0, 0};   // Air / unknown
        }
    }
}

HouseModel::HouseModel() {
    blocks.assign((size_t)HOUSE_VX * HOUSE_VY * HOUSE_VZ, BlockType::Air);
    volume = new VoxelVolume(HOUSE_VX, HOUSE_VY, HOUSE_VZ);
    rebuild();
}

HouseModel::~HouseModel() {
    delete volume;
}

BlockType HouseModel::get(int x, int y, int z) const {
    if (x < 0 || x >= HOUSE_VX || y < 0 || y >= HOUSE_VY || z < 0 || z >= HOUSE_VZ)
        return BlockType::Air;
    return blocks[((size_t)z * HOUSE_VY + y) * HOUSE_VX + x];
}

void HouseModel::set(int x, int y, int z, BlockType t) {
    if (x < 0 || x >= HOUSE_VX || y < 0 || y >= HOUSE_VY || z < 0 || z >= HOUSE_VZ)
        return;
    blocks[((size_t)z * HOUSE_VY + y) * HOUSE_VX + x] = t;
}

void generateHouseGrid(int templateType, int roofType, int material,
                       std::vector<BlockType>& blocks) {
    blocks.assign((size_t)HOUSE_VX * HOUSE_VY * HOUSE_VZ, BlockType::Air);
    auto set = [&](int x, int y, int z, BlockType t) {
        if (x < 0 || x >= HOUSE_VX || y < 0 || y >= HOUSE_VY ||
            z < 0 || z >= HOUSE_VZ) return;
        blocks[((size_t)z * HOUSE_VY + y) * HOUSE_VX + x] = t;
    };

    // Material -> painted wall / roof colours (indices into PAINT_PALETTE).
    auto paint = [](int i) { return (BlockType)((int)BlockType::PaintFirst + i); };
    BlockType wallB, roofB;
    switch (material) {
        case 1: wallB = paint(0);  roofB = paint(7);  break;  // Cottage:   white / brick red
        case 2: wallB = paint(3);  roofB = paint(4);  break;  // Stone:     slate / charcoal
        case 3: wallB = paint(2);  roofB = paint(20); break;  // Manor:     light grey / navy
        case 4: wallB = paint(12); roofB = paint(4);  break;  // Cabin:     chestnut / charcoal
        case 5: wallB = paint(13); roofB = paint(6);  break;  // Sandstone: sand / terracotta
        case 6: wallB = paint(15); roofB = paint(16); break;  // Forest:    sage / forest green
        case 7: wallB = paint(0);  roofB = paint(21); break;  // Coastal:   white / steel blue
        case 8: wallB = paint(6);  roofB = paint(9);  break;  // Autumn:    terracotta / rust
        case 9: wallB = paint(23); roofB = paint(22); break;  // Plum:      dusty rose / plum
        default:wallB = paint(13); roofB = paint(12); break;  // Timber:    sand / chestnut
    }
    const BlockType foundationB = BlockType::Stone;
    const BlockType floorB      = paint(12);          // chestnut floorboards
    const BlockType windowB     = BlockType::Glass;   // see-through glass windows
    const BlockType chimneyB    = paint(7);           // brick chimney stack

    // Template -> floor count, storey height, footprint margins (X and Z
    // separately, which gives non-square footprints).
    int floors, floorH, marginX, marginZ;
    switch (templateType) {
        case 1: floors=2; floorH= 6; marginX=4; marginZ=4; break;  // Two-Story
        case 2: floors=1; floorH= 6; marginX=7; marginZ=7; break;  // Cottage
        case 3: floors=4; floorH= 5; marginX=7; marginZ=7; break;  // Tower
        case 4: floors=1; floorH= 6; marginX=5; marginZ=8; break;  // Cabin
        case 5: floors=1; floorH= 7; marginX=2; marginZ=7; break;  // Longhouse
        case 6: floors=3; floorH= 6; marginX=8; marginZ=6; break;  // Townhouse
        case 7: floors=2; floorH= 6; marginX=2; marginZ=5; break;  // Manor
        case 8: floors=1; floorH=11; marginX=3; marginZ=3; break;  // Hall
        case 9: floors=3; floorH= 6; marginX=4; marginZ=4; break;  // Keep
        default:floors=1; floorH= 7; marginX=3; marginZ=3; break;  // Bungalow
    }
    const int x0 = marginX, x1 = HOUSE_VX - 1 - marginX;
    const int z0 = marginZ, z1 = HOUSE_VZ - 1 - marginZ;
    const int wallH = floors * floorH;            // walls span y in [1, wallH]

    auto box = [&](int ax, int bx, int ay, int by, int az, int bz, BlockType t) {
        for (int x = ax; x <= bx; x++)
            for (int y = ay; y <= by; y++)
                for (int z = az; z <= bz; z++)
                    set(x, y, z, t);
    };

    // Foundation, solid shell, hollow interior.
    box(x0 - 1, x1 + 1, 0, 0, z0 - 1, z1 + 1, foundationB);
    box(x0, x1, 1, wallH, z0, z1, wallB);
    box(x0 + 1, x1 - 1, 1, wallH - 1, z0 + 1, z1 - 1, BlockType::Air);

    // Floors — the ground-floor boards sit directly on the foundation so they
    // are level with the bottom of the doorway; each upper storey gets a slab.
    box(x0 + 1, x1 - 1, 0, 0, z0 + 1, z1 - 1, floorB);
    for (int f = 1; f < floors; f++)
        box(x0 + 1, x1 - 1, f * floorH, f * floorH, z0 + 1, z1 - 1, floorB);

    // Interior staircases — one straight, 2-wide flight per upper storey
    // against the left wall. Each step rises a single block (walkable without
    // jumping), and a matching slot is cut in the slab above to climb through.
    // Flights alternate between two adjacent lanes so the well of one does not
    // undercut the foot of the next.
    for (int f = 1; f < floors; f++) {
        const int xStair = (f % 2 == 1) ? x0 + 1 : x0 + 3;   // 2-wide lane
        const int yL     = (f - 1) * floorH + 1;   // walkable level of the floor below
        for (int s = 0; s < floorH - 1; s++)
            box(xStair, xStair + 1, yL + s, yL + s, z0 + 2 + s, z0 + 2 + s, floorB);
        box(xStair, xStair + 1, f * floorH, f * floorH, z0 + 2, z0 + floorH, BlockType::Air);
    }

    // Door — an opening centred on the front wall (z = z0), ground floor.
    const int dcx = (x0 + x1) / 2;
    box(dcx - 1, dcx + 1, 1, 4, z0, z0, BlockType::Air);

    // Windows — 2x2 panels punched through the walls, one row per floor.
    auto win = [&](int cx, int cy, int cz, bool alongX) {
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++)
                set(alongX ? cx + a : cx, cy + b, alongX ? cz : cz + a, windowB);
    };
    const int xspan = x1 - x0, zspan = z1 - z0;
    for (int f = 0; f < floors; f++) {
        const int wy = f * floorH + 2;
        if (xspan >= 10) {
            for (int cx : {x0 + xspan / 4, x0 + 3 * xspan / 4 - 1}) {
                if (!(f == 0 && cx >= dcx - 2 && cx <= dcx + 2))
                    win(cx, wy, z0, true);
                win(cx, wy, z1, true);
            }
        } else {
            const int cx = (x0 + x1) / 2 - 1;
            if (f != 0) win(cx, wy, z0, true);
            win(cx, wy, z1, true);
        }
        if (zspan >= 10) {
            for (int cz : {z0 + zspan / 4, z0 + 3 * zspan / 4 - 1}) {
                win(x0, wy, cz, false);
                win(x1, wy, cz, false);
            }
        } else {
            const int cz = (z0 + z1) / 2 - 1;
            win(x0, wy, cz, false);
            win(x1, wy, cz, false);
        }
    }

    // Roof — sits one row above the walls, overhanging the footprint by one
    // block. Gabled and hipped roofs run their ridge along the longer wall.
    const int rx0 = x0 - 1, rx1 = x1 + 1, rz0 = z0 - 1, rz1 = z1 + 1;
    const int ry = wallH + 1;
    const bool ridgeX = (xspan >= zspan);
    int roofTopY = ry;
    if (roofType == 0) {                              // Flat
        box(rx0, rx1, ry, ry, rz0, rz1, roofB);
    } else if (roofType == 3) {                       // Pyramid
        int ax0 = rx0, ax1 = rx1, az0 = rz0, az1 = rz1, h = 0;
        while (ax0 <= ax1 && az0 <= az1) {
            box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
            roofTopY = ry + h;
            ax0++; ax1--; az0++; az1--; h++;
        }
    } else if (roofType == 2) {                       // Hipped
        int ax0 = rx0, ax1 = rx1, az0 = rz0, az1 = rz1, h = 0;
        if (ridgeX) {
            while (az0 <= az1) {
                box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
                roofTopY = ry + h;
                az0++; az1--;
                if (ax1 - ax0 > 4) { ax0++; ax1--; }
                h++;
            }
        } else {
            while (ax0 <= ax1) {
                box(ax0, ax1, ry + h, ry + h, az0, az1, roofB);
                roofTopY = ry + h;
                ax0++; ax1--;
                if (az1 - az0 > 4) { az0++; az1--; }
                h++;
            }
        }
    } else {                                          // Gabled (1)
        int h = 0;
        if (ridgeX) {
            int az0 = rz0, az1 = rz1;
            while (az0 <= az1) {
                box(rx0, rx1, ry + h, ry + h, az0, az1, roofB);
                const int g0 = std::max(az0, z0), g1 = std::min(az1, z1);
                box(x0, x0, ry + h, ry + h, g0, g1, wallB);   // triangular gable ends
                box(x1, x1, ry + h, ry + h, g0, g1, wallB);
                roofTopY = ry + h;
                az0++; az1--; h++;
            }
        } else {
            int ax0 = rx0, ax1 = rx1;
            while (ax0 <= ax1) {
                box(ax0, ax1, ry + h, ry + h, rz0, rz1, roofB);
                const int g0 = std::max(ax0, x0), g1 = std::min(ax1, x1);
                box(g0, g1, ry + h, ry + h, z0, z0, wallB);   // triangular gable ends
                box(g0, g1, ry + h, ry + h, z1, z1, wallB);
                roofTopY = ry + h;
                ax0++; ax1--; h++;
            }
        }
    }

    // Chimney — a brick stack that starts at the roofline and rises out past
    // the roof peak (it does not run down into the interior).
    if (xspan >= 7 && zspan >= 7) {
        const int cx = x1 - 3, cz = z1 - 3;
        box(cx, cx + 1, wallH, roofTopY + 2, cz, cz + 1, chimneyB);
    }
}

void HouseModel::rebuild() {
    generateHouseGrid(templateType, roofType, material, blocks);
    refreshMesh();
}

void HouseModel::refreshMesh() {
    if (!volume) return;
    boundMin = glm::ivec3(HOUSE_VX, HOUSE_VY, HOUSE_VZ);
    boundMax = glm::ivec3(-1, -1, -1);
    for (int z = 0; z < HOUSE_VZ; z++)
        for (int y = 0; y < HOUSE_VY; y++)
            for (int x = 0; x < HOUSE_VX; x++) {
                BlockType t = get(x, y, z);
                volume->setVoxel(x, y, z, houseBlockColor(t));
                if (t != BlockType::Air) {
                    boundMin = glm::min(boundMin, glm::ivec3(x, y, z));
                    boundMax = glm::max(boundMax, glm::ivec3(x, y, z));
                }
            }
    if (boundMax.x < 0) { boundMin = glm::ivec3(0); boundMax = glm::ivec3(0); }
    volume->updateMesh();
}
