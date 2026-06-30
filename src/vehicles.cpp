#include "vehicle.h"
#include "voxel_model.h"
#include "prop_builders.h"
#include "ferry_routes.h"
#include "world.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdlib>
#include <algorithm>

// --- Ferry tuning ----------------------------------------------------------
// 1 ferry voxel = 1 world block. The hull rides low so its deck is flush with
// the dock jetties (which sit at WORLD_SEA_LEVEL).
static constexpr float FERRY_SCALE    = 1.0f;
static constexpr float FERRY_BASE_Y   = (float)WORLD_SEA_LEVEL - 4.0f; // hull bottom
static constexpr float FERRY_DECK_OFF = 5.0f;                          // deck above base
static constexpr float FERRY_HALF_LEN = 17.0f;   // Z half-extent (model is 34 long)
static constexpr float FERRY_HALF_WID = 7.0f;    // X half-extent (model is 14 wide)

VoxelVolume* buildFerryVolume() {
    const int W = 14, H = 10, D = 34;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel HULLD{ 66,  42, 24, 255};
    const Voxel HULL { 96,  62, 36, 255};
    const Voxel DECK {150, 108, 66, 255};
    const Voxel RAIL {120,  80, 46, 255};
    const Voxel CABIN{172, 142, 96, 255};
    const Voxel ROOF {150,  62, 56, 255};

    voxFill(v, 1, 0, 3, W - 2, 2, D - 4, HULLD);          // keel
    voxFill(v, 0, 2, 1, W - 1, 3, D - 2, HULL);           // hull sides
    voxFill(v, 1, 4, 1, W - 2, 4, D - 2, DECK);           // deck planks
    voxFill(v, 1, 5, 1,     W - 2, 6, 1,     RAIL);       // rails
    voxFill(v, 1, 5, D - 2, W - 2, 6, D - 2, RAIL);
    voxFill(v, 1, 5, 1,     1,     6, D - 2, RAIL);
    voxFill(v, W - 2, 5, 1, W - 2, 6, D - 2, RAIL);
    voxFill(v, 3, 5, D - 8, W - 4, 8, D - 3, CABIN);      // wheelhouse
    voxFill(v, 3, 9, D - 8, W - 4, 9, D - 3, ROOF);
    return v;
}

VoxelVolume* getFerryMesh() {
    static VoxelVolume* mesh = nullptr;   // main thread only — no race
    if (!mesh) {
        mesh = buildFerryVolume();
        mesh->updateMesh();
    }
    return mesh;
}

// --- Personal vehicles (horse / wagon / kite) ------------------------------
// Built at the player-rig voxel resolution (~0.06 block/voxel) so Player::draw
// composes them with the rig. y=0 is the ground-contact plane; the model faces
// +Z (the rig's forward) at yaw 0.

VoxelVolume* buildHorseVolume() {
    // A beefy mount: thick legs, a full barrel, a clearly forward-and-up neck +
    // head, and a tail. Faces +Z; y=0 = hooves. Saddle top ~y24 → riderLift.
    const int W = 14, H = 36, D = 48;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel COAT {126,  86,  52, 255};   // chestnut body
    const Voxel COATD{104,  68,  40, 255};   // shaded underside
    const Voxel DARK { 64,  42,  26, 255};   // mane, tail, lower legs
    const Voxel HOOF { 36,  27,  19, 255};
    const Voxel SADL { 60,  40,  26, 255};   // leather saddle
    const Voxel TRIM {165, 120,  72, 255};   // saddle trim / bridle
    const Voxel EYE  { 18,  14,  10, 255};

    auto leg = [&](int x0, int z0) {
        voxFill(v, x0, 0, z0, x0 + 3, 15, z0 + 3, COAT);   // upper leg
        voxFill(v, x0, 0, z0, x0 + 3,  4, z0 + 3, DARK);   // lower leg (sock)
        voxFill(v, x0, 0, z0, x0 + 3,  1, z0 + 3, HOOF);   // hoof
    };
    leg(2, 6);  leg(W - 6, 6);             // hind legs (low Z)
    leg(2, D - 11); leg(W - 6, D - 11);    // fore legs (high Z)

    // Barrel body — full width, long, with a shaded belly and a taller chest.
    voxFill(v, 1, 15, 6,  W - 2, 25, D - 8, COAT);
    voxFill(v, 1, 14, 8,  W - 2, 15, D - 10, COATD);       // belly shade
    voxFill(v, 1, 15, D - 18, W - 2, 27, D - 8, COAT);     // chest a bit taller
    voxFill(v, 1, 15, 6,  W - 2, 26, 14, COAT);            // hindquarters

    // Neck rising toward the front (high Z), then the head + muzzle.
    voxFill(v, 4, 24, D - 12, W - 5, 30, D - 6, COAT);     // lower neck
    voxFill(v, 4, 28, D - 8,  W - 5, 34, D - 3, COAT);     // upper neck
    voxFill(v, 4, 31, D - 5,  W - 5, 35, D - 1, COAT);     // head
    voxFill(v, 5, 29, D - 3,  W - 6, 32, D - 1, COATD);    // muzzle
    voxFill(v, 5, 27, D - 12, W - 6, 34, D - 6, DARK);     // mane along the neck
    v->setVoxel(4, 33, D - 2, EYE); v->setVoxel(W - 5, 33, D - 2, EYE);
    v->setVoxel(5, 35, D - 4, COAT); v->setVoxel(W - 6, 35, D - 4, COAT);  // ears

    // Tail — hangs at the back (low Z).
    voxFill(v, 5, 12, 2, W - 6, 25, 5, DARK);

    // Saddle + skirts on the mid-back (where the rider sits).
    voxFill(v, 1, 25, 18, W - 2, 26, 32, SADL);
    voxFill(v, 0, 22, 20, 0,     26, 30, SADL);            // left skirt
    voxFill(v, W - 1, 22, 20, W - 1, 26, 30, SADL);        // right skirt
    voxFill(v, 1, 26, 18, W - 2, 26, 18, TRIM);            // cantle (back rise)
    voxFill(v, 1, 26, 32, W - 2, 26, 32, TRIM);            // pommel (front rise)
    return v;
}

VoxelVolume* buildWagonVolume() {
    const int W = 18, H = 26, D = 36;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel WOOD {158, 116,  70, 255};
    const Voxel WOODD{ 98,  68,  40, 255};
    const Voxel IRON { 72,  68,  62, 255};
    const Voxel HUB  {120, 110,  96, 255};

    voxFill(v, 1, 9, 4, W - 2, 11, D - 2, WOOD);          // bed platform
    voxFill(v, 1, 11, 4, 2,     20, D - 2, WOODD);        // left wall
    voxFill(v, W - 3, 11, 4, W - 2, 20, D - 2, WOODD);    // right wall
    voxFill(v, 1, 11, 4,     W - 2, 20, 5,     WOODD);    // back wall (low Z)
    voxFill(v, 1, 11, D - 3, W - 2, 20, D - 2, WOODD);    // front wall (high Z)
    voxFill(v, 0, 6, 7,     W - 1, 7, 8,     IRON);       // rear axle
    voxFill(v, 0, 6, D - 9, W - 1, 7, D - 8, IRON);       // front axle

    // Wheels — a near-round disc in the YZ plane at each side (axle along X).
    auto wheel = [&](int x, int cz) {
        const int cy = 6, R = 6;
        for (int y = 0; y < 13; y++)
            for (int z = cz - R; z <= cz + R; z++) {
                int dy = y - cy, dz = z - cz, d2 = dy * dy + dz * dz;
                if (d2 <= R * R && d2 >= (R - 2) * (R - 2))
                    voxFill(v, x, y, z, x + 1, y, z, IRON);   // rim
            }
        voxFill(v, x, cy - 1, cz - 1, x + 1, cy + 1, cz + 1, HUB);  // hub
    };
    wheel(0, 8);     wheel(W - 2, 8);          // rear wheels
    wheel(0, D - 9); wheel(W - 2, D - 9);      // front wheels
    voxFill(v, W / 2 - 1, 8, D - 1, W / 2, 9, D - 1, WOODD);  // tongue stub (front)
    return v;
}

VoxelVolume* buildKiteVolume() {
    const int W = 24, H = 24, D = 3;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel SAIL {200,  70,  70, 255};   // red
    const Voxel SAIL2{230, 200,  90, 255};   // gold
    const Voxel SPAR { 90,  60,  40, 255};   // wooden cross-spars
    const int cx = W / 2, cy = H / 2, R = 11;
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++) {
            int d = std::abs(x - cx) + std::abs(y - cy);   // diamond (L1 metric)
            if (d <= R) v->setVoxel(x, y, 1, ((x + y) & 2) ? SAIL : SAIL2);
        }
    voxFill(v, cx, 0, 0, cx, H - 1, D - 1, SPAR);          // vertical spar
    voxFill(v, 0, cy, 0, W - 1, cy, D - 1, SPAR);          // horizontal spar
    return v;
}

VoxelVolume* getHorseMesh() {
    static VoxelVolume* mesh = nullptr;   // main thread only — no race
    if (!mesh) { mesh = buildHorseVolume(); mesh->updateMesh(); }
    return mesh;
}
VoxelVolume* getWagonMesh() {
    static VoxelVolume* mesh = nullptr;
    if (!mesh) { mesh = buildWagonVolume(); mesh->updateMesh(); }
    return mesh;
}
VoxelVolume* getKiteMesh() {
    static VoxelVolume* mesh = nullptr;
    if (!mesh) { mesh = buildKiteVolume(); mesh->updateMesh(); }
    return mesh;
}

// --- Ferry -----------------------------------------------------------------

Ferry::Ferry() {
    position.y = FERRY_BASE_Y;
}

float Ferry::deckTopY() const {
    return position.y + FERRY_DECK_OFF;
}

bool Ferry::onDeck(const glm::vec3& p) const {
    float dy = p.y - deckTopY();
    if (dy < -3.0f || dy > 3.0f) return false;
    // Rotate the world-space offset into the ferry's local frame.
    float t = glm::radians(yaw);
    float c = cosf(t), s = sinf(t);
    float wx = p.x - position.x, wz = p.z - position.z;
    float mx = wx * c - wz * s;
    float mz = wx * s + wz * c;
    return std::fabs(mx) < FERRY_HALF_WID && std::fabs(mz) < FERRY_HALF_LEN;
}

void Ferry::serverStep(float dt) {
    const std::vector<FerryRoute>& routes = getFerryRoutes();
    if (routeIndex < 0 || routeIndex >= (int)routes.size()) return;
    const FerryRoute& r = routes[routeIndex];

    glm::vec3 prev = position;
    if (waitTimer > 0.0f) {
        waitTimer -= dt;
    } else {
        float step = (r.length > 1.0f) ? (r.speed * dt / r.length) : 1.0f;
        routeT += step * (float)dir;
        if (routeT >= 1.0f)      { routeT = 1.0f; dir = -1; waitTimer = 6.0f; }
        else if (routeT <= 0.0f) { routeT = 0.0f; dir =  1; waitTimer = 6.0f; }
    }
    position   = glm::mix(r.dockA, r.dockB, routeT);
    position.y = FERRY_BASE_Y;
    velocity   = (dt > 0.0f) ? (position - prev) / dt : glm::vec3(0.0f);

    glm::vec3 d = r.dockB - r.dockA;
    if (std::fabs(d.x) + std::fabs(d.z) > 0.001f) {
        yaw = glm::degrees(atan2f(d.x, d.z));
        if (dir < 0) yaw += 180.0f;
    }
}

void Ferry::update(float dt, World& world) {
    (void)world;
    float k = std::min(1.0f, 10.0f * dt);
    glm::vec3 prev = position;
    position = glm::mix(position, targetPos, k);
    float dyaw = targetYaw - yaw;
    while (dyaw >  180.0f) dyaw -= 360.0f;
    while (dyaw < -180.0f) dyaw += 360.0f;
    yaw += dyaw * k;
    velocity = (dt > 0.0f) ? (position - prev) / dt : glm::vec3(0.0f);
}

void Ferry::draw(GLuint modelLoc) const {
    if (!mesh) return;
    glm::mat4 m = baseMatrix(FERRY_SCALE);
    m = glm::translate(m, glm::vec3(-mesh->sizeX * 0.5f, 0.0f, -mesh->sizeZ * 0.5f));
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &m[0][0]);
    mesh->draw();
}

void Ferry::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    mn = position - glm::vec3(FERRY_HALF_LEN, 0.0f,                FERRY_HALF_LEN);
    mx = position + glm::vec3(FERRY_HALF_LEN, FERRY_DECK_OFF + 6.0f, FERRY_HALF_LEN);
}
