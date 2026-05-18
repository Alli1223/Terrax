#include "physics.h"
#include "world.h"
#include "camera.h"
#include <cmath>

static constexpr float SKIN        = 0.001f;
static constexpr float STEP_HEIGHT = 1.0f;

static bool isSolid(BlockType bt) { return bt != BlockType::Air && bt != BlockType::Water; }

static bool hitFaceX(float px, float py, float pz, float hw, float ph, const World& w, bool posDir) {
    int bx = posDir ? (int)floorf(px + hw) : (int)floorf(px - hw - SKIN);
    for (int by = (int)floorf(py);       by <= (int)floorf(py + ph - SKIN); by++)
    for (int bz = (int)floorf(pz - hw); bz <= (int)floorf(pz + hw - SKIN); bz++)
        if (isSolid(w.getBlock(bx, by, bz))) return true;
    return false;
}

static bool hitFaceZ(float px, float py, float pz, float hw, float ph, const World& w, bool posDir) {
    int bz = posDir ? (int)floorf(pz + hw) : (int)floorf(pz - hw - SKIN);
    for (int by = (int)floorf(py);       by <= (int)floorf(py + ph - SKIN); by++)
    for (int bx = (int)floorf(px - hw); bx <= (int)floorf(px + hw - SKIN); bx++)
        if (isSolid(w.getBlock(bx, by, bz))) return true;
    return false;
}

static bool aabbClear(float px, float py, float pz, float hw, float ph, const World& w) {
    for (int bx = (int)floorf(px - hw); bx <= (int)floorf(px + hw - SKIN); bx++)
    for (int by = (int)floorf(py);      by <= (int)floorf(py + ph - SKIN); by++)
    for (int bz = (int)floorf(pz - hw); bz <= (int)floorf(pz + hw - SKIN); bz++)
        if (isSolid(w.getBlock(bx, by, bz))) return false;
    return true;
}

glm::vec3 resolveCollision(const glm::vec3& pos, Camera& camera,
                            float hw, float ph, const World& w) {
    glm::vec3 p = pos;
    camera.onGround = false;

    if (camera.velocity.y <= 0.0f) {
        int yFeet = (int)floorf(p.y);
        for (int by = yFeet + 3; by >= yFeet; by--) {
            if (by < 0) {
                p.y = SKIN; camera.velocity.y = 0.0f; camera.onGround = true; break;
            }
            bool hit = false;
            for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !hit; bx++)
            for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !hit; bz++)
                if (isSolid(w.getBlock(bx, by, bz))) hit = true;
            if (hit) {
                float top = (float)(by + 1);
                if (p.y < top) {
                    p.y = top + SKIN;
                    camera.velocity.y = 0.0f;
                    camera.onGround   = true;
                }
                break;
            }
        }
    } else {
        int byHead = (int)floorf(p.y + ph);
        bool hit = false;
        for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !hit; bx++)
        for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !hit; bz++)
            if (isSolid(w.getBlock(bx, byHead, bz))) hit = true;
        if (hit) {
            p.y = (float)byHead - ph - SKIN;
            camera.velocity.y = 0.0f;
        }
    }

    if (!camera.onGround) {
        int byBelow = (int)floorf(p.y - SKIN);
        if (byBelow >= 0) {
            bool found = false;
            for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !found; bx++)
            for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !found; bz++)
                if (isSolid(w.getBlock(bx, byBelow, bz))) found = true;
            camera.onGround = found;
        }
    }

    float savedVx = camera.velocity.x;
    float savedVz = camera.velocity.z;
    bool blockedX = false, blockedZ = false;

    if (camera.velocity.x != 0.0f) {
        bool posX = camera.velocity.x > 0.0f;
        if (hitFaceX(p.x, p.y, p.z, hw, ph, w, posX)) {
            blockedX = true;
            p.x = posX ? floorf(p.x + hw) - hw - SKIN
                       : floorf(p.x - hw - SKIN) + 1.0f + hw + SKIN;
            camera.velocity.x = 0.0f;
        }
    }

    if (camera.velocity.z != 0.0f) {
        bool posZ = camera.velocity.z > 0.0f;
        if (hitFaceZ(p.x, p.y, p.z, hw, ph, w, posZ)) {
            blockedZ = true;
            p.z = posZ ? floorf(p.z + hw) - hw - SKIN
                       : floorf(p.z - hw - SKIN) + 1.0f + hw + SKIN;
            camera.velocity.z = 0.0f;
        }
    }

    if ((blockedX || blockedZ) && camera.onGround) {
        float tryX = blockedX ? pos.x : p.x;
        float tryZ = blockedZ ? pos.z : p.z;
        float tryY = p.y + STEP_HEIGHT;
        if (aabbClear(tryX, tryY, tryZ, hw, ph, w)) {
            p = { tryX, tryY, tryZ };
            camera.velocity.x = savedVx;
            camera.velocity.z = savedVz;
        }
    }

    return p;
}
