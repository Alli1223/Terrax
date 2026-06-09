#include "physics.h"
#include "world.h"
#include "camera.h"
#include <algorithm>
#include <cmath>

static constexpr float SKIN = 0.001f;

static bool isSolid(BlockType bt) {
    return bt != BlockType::Air && bt != BlockType::Water && !isWheatBlock(bt);
}

// True if any solid block sits under the player's footprint at world layer `by`.
static bool footprintSolid(float px, int by, float pz, float hw, const World& w) {
    if (by < 0 || by >= CHUNK_HEIGHT) return false;
    for (int bx = (int)floorf(px - hw); bx <= (int)floorf(px + hw - SKIN); bx++)
    for (int bz = (int)floorf(pz - hw); bz <= (int)floorf(pz + hw - SKIN); bz++)
        if (isSolid(w.getBlock(bx, by, bz))) return true;
    return false;
}

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

// True if the player's whole AABB (feet at py) is free of solid blocks.
static bool aabbClear(float px, float py, float pz, float hw, float ph, const World& w) {
    for (int bx = (int)floorf(px - hw); bx <= (int)floorf(px + hw - SKIN); bx++)
    for (int by = (int)floorf(py);      by <= (int)floorf(py + ph - SKIN); by++)
    for (int bz = (int)floorf(pz - hw); bz <= (int)floorf(pz + hw - SKIN); bz++)
        if (isSolid(w.getBlock(bx, by, bz))) return false;
    return true;
}

// Try to climb a low ledge in the direction of travel instead of stopping at
// it. Called when the body is blocked along an axis while standing on the
// ground. Raises `p` onto the obstacle — keeping the (already advanced)
// horizontal position and the tangential velocity — when a single step of
// rise clears the obstacle in the travel direction and the whole body fits
// there. A one-block kerb becomes walkable; a two-block (or taller) wall still
// blocks, because the face is still solid one step up.
static bool tryStepUp(glm::vec3& p, float hw, float ph, const World& w,
                      bool axisX, bool posDir) {
    const float STEP = 1.05f;                       // at most one block of rise
    int maxTop = (int)floorf(p.y + STEP);
    for (int top = (int)floorf(p.y) + 1; top <= maxTop; top++) {
        float standY = (float)top;
        if (standY - p.y > STEP + SKIN)              break;     // taller than a step
        if (!aabbClear(p.x, standY, p.z, hw, ph, w)) continue;  // body wouldn't fit up there
        bool blocked = axisX ? hitFaceX(p.x, standY, p.z, hw, ph, w, posDir)
                             : hitFaceZ(p.x, standY, p.z, hw, ph, w, posDir);
        if (blocked) continue;                                  // obstacle still in the way
        p.y = standY;
        return true;
    }
    return false;
}

glm::vec3 resolveCollision(const glm::vec3& pos, Camera& camera,
                            float hw, float ph, const World& w, float dt) {
    glm::vec3 p = pos;
    float startY = pos.y;
    camera.onGround = false;

    // --- Vertical ---
    if (camera.velocity.y > 0.0f) {
        // Rising — stop at a ceiling.
        int byHead = (int)floorf(p.y + ph);
        if (footprintSolid(p.x, byHead, p.z, hw, w)) {
            p.y = (float)byHead - ph - SKIN;
            camera.velocity.y = 0.0f;
        }
    } else {
        // Falling or standing — settle onto the highest surface the player can
        // actually stand on. A surface is reachable only if it is at most one
        // block above the feet (a single step-up), plus however far the player
        // fell this frame, and the player's whole body fits when resting there.
        // This lets the player walk up one cell but never climb a taller wall,
        // tree or hillside — to get any higher they must jump.
        float fallDist = (camera.velocity.y < 0.0f) ? -camera.velocity.y * dt : 0.0f;
        float reach    = fallDist + 1.0f;
        int   scanTop  = (int)floorf(p.y + fallDist) + 1;
        for (int by = scanTop; by >= 0 && by >= scanTop - 6; by--) {
            if (!footprintSolid(p.x, by, p.z, hw, w)) continue;
            float top = (float)(by + 1);
            if (top > p.y + reach + SKIN) continue;             // too high to step / land on
            if (top < p.y - SKIN) break;                        // not reached this surface yet
            if (!aabbClear(p.x, top, p.z, hw, ph, w)) continue; // no room to stand here
            p.y = top;
            camera.velocity.y = 0.0f;
            camera.onGround   = true;
            break;
        }
    }

    // --- Horizontal: stop at walls along each axis, but step up over low
    // ledges so the player walks up a one-block kerb instead of jamming. The
    // step is only attempted while grounded (so you can't climb walls in
    // mid-air) and tryStepUp's own checks reject anything taller than a step. ---
    if (camera.velocity.x != 0.0f) {
        bool posX = camera.velocity.x > 0.0f;
        if (hitFaceX(p.x, p.y, p.z, hw, ph, w, posX) &&
            !(camera.onGround && tryStepUp(p, hw, ph, w, true, posX))) {
            p.x = posX ? floorf(p.x + hw) - hw - SKIN
                       : floorf(p.x - hw - SKIN) + 1.0f + hw + SKIN;
            camera.velocity.x = 0.0f;
        }
    }
    if (camera.velocity.z != 0.0f) {
        bool posZ = camera.velocity.z > 0.0f;
        if (hitFaceZ(p.x, p.y, p.z, hw, ph, w, posZ) &&
            !(camera.onGround && tryStepUp(p, hw, ph, w, false, posZ))) {
            p.z = posZ ? floorf(p.z + hw) - hw - SKIN
                       : floorf(p.z - hw - SKIN) + 1.0f + hw + SKIN;
            camera.velocity.z = 0.0f;
        }
    }

    // Re-check footing after being pushed clear of a wall.
    if (!camera.onGround && camera.velocity.y <= 0.0f) {
        int by = (int)floorf(p.y - 0.05f);
        if (by >= 0 && p.y <= (float)(by + 1) + 0.05f &&
            footprintSolid(p.x, by, p.z, hw, w)) {
            camera.onGround = true;
        }
    }

    if (p.y < 0.0f) { p.y = 0.0f; camera.velocity.y = 0.0f; camera.onGround = true; }

    // Visual-only: when the body climbed a low ledge this frame, record the rise
    // so the renderer can ease the camera/body up rather than snapping a whole
    // block. Restricted to a grounded, single-step rise so big drops (landing
    // from a fall) and jumps are never smoothed.
    float rise = p.y - startY;
    if (camera.onGround && rise > SKIN && rise <= 1.25f)
        camera.stepSmoothOffset = std::max(-1.6f, camera.stepSmoothOffset - rise);

    return p;
}
