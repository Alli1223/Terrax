#include "vehicle.h"
#include "voxel_model.h"
#include "prop_builders.h"
#include "ferry_routes.h"
#include "world.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
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
