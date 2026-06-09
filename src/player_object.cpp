#include "player_object.h"
#include "camera.h"
#include "voxel_model.h"
#include "network.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

Player::Player(Camera* cam, BipedalRig* rig)
    : GameObject(ObjectKind::Player), camera(cam), localRig(rig) {
    isLocal = true;
    id      = 0;
}

Player::Player(RemotePlayer* rp, uint32_t netId)
    : GameObject(ObjectKind::Player), remote(rp) {
    isLocal = false;
    id      = netId;
    if (rp) { position = rp->position; yaw = rp->yaw; }
}

void Player::update(float dt, World& world) {
    (void)world;
    if (isLocal) {
        if (!localRig) return;
        localRig->lanternHeld = lanternHeld;
        float velocity = camera ? glm::length(camera->velocity) : 0.0f;
        localRig->update(dt, std::min(velocity * 0.5f, 5.0f));
        // The local lean is driven from the movement INPUT in updateGameplay
        // (gameplay.cpp) — input is screen-relative, so W+D banks the body even
        // though it auto-rotates to face the movement direction.
        return;
    }

    // --- Remote: interpolate the borrowed RemotePlayer record (this is the
    // logic that used to live inline in Renderer::renderWorld). ---
    RemotePlayer* p = remote;
    if (!p) { dead = true; return; }
    if (!p->rig || !p->rig->torso || !p->rig->torso->volume) {
        if (!p->rig) p->rig = new BipedalRig();
        p->rig->setupDefaultHuman(true);
    }
    float lerpF = 10.0f * dt;
    glm::vec3 lastPos = p->position;
    p->position = glm::mix(p->position, p->targetPosition, std::min(1.0f, lerpF));
    p->pitch    = glm::mix(p->pitch,    p->targetPitch,    std::min(1.0f, lerpF));
    p->yaw      = glm::mix(p->yaw,      p->targetYaw,      std::min(1.0f, lerpF));
    float vel = glm::length(p->position - lastPos) / (dt > 0.0f ? dt : 1.0f);
    if (p->isAttacking) {
        p->attackAnim += dt * 5.0f;
        if (p->attackAnim > 1.0f) { p->isAttacking = false; p->attackAnim = 0.0f; }
    }
    p->rig->lanternHeld = p->lanternHeld;
    p->rig->isAttacking = p->isAttacking;
    p->rig->attackAnim  = p->attackAnim;
    p->rig->isBlocking  = p->shieldRaised;   // mirror remote block state
    p->rig->update(dt, std::min(vel, 10.0f));
    {
        // Remote lean from the interpolated motion, in the remote's own frame.
        glm::vec3 dv = (p->position - lastPos) / (dt > 0.0f ? dt : 1.0f);
        float yawR = glm::radians(p->yaw);
        glm::vec3 fwd(sinf(yawR), 0.0f, cosf(yawR));
        glm::vec3 right(cosf(yawR), 0.0f, -sinf(yawR));
        p->rig->updateLean(dv.x * fwd.x + dv.z * fwd.z, dv.x * right.x + dv.z * right.z, dt);
    }

    position = p->position;
    yaw      = p->yaw;
}

void Player::draw(GLuint modelLoc) const {
    BipedalRig* r = isLocal ? localRig : (remote ? remote->rig : nullptr);
    if (!r) return;
    glm::mat4 base = baseMatrix(0.06f * r->heightScale);
    // Whole-body tilt: lean into movement, or a forward somersault while
    // rolling. Applied in world space around a pivot above the feet so the body
    // banks / tumbles naturally. Skipped while sitting / lying (the rig owns
    // that pose) or when there's nothing to apply.
    if (r->pose == PlayerPose::Standing &&
        (r->rollProgress >= 0.0f || std::fabs(r->leanPitch) > 1e-4f || std::fabs(r->leanRoll) > 1e-4f)) {
        float yawR = glm::radians(yaw);
        glm::vec3 right(cosf(yawR), 0.0f, -sinf(yawR));
        glm::vec3 fwd  (sinf(yawR), 0.0f, cosf(yawR));
        auto tilt = [&](const glm::mat4& m, float pivH, const glm::vec3& axis, float ang) {
            glm::vec3 pv = position + glm::vec3(0.0f, pivH * r->heightScale, 0.0f);
            return glm::translate(glm::mat4(1.0f), pv)
                 * glm::rotate(glm::mat4(1.0f), ang, axis)
                 * glm::translate(glm::mat4(1.0f), -pv) * m;
        };
        if (r->rollProgress >= 0.0f) {
            base = tilt(base, 0.90f, right, glm::radians(360.0f * r->rollProgress));  // somersault
        } else {
            base = tilt(base, 0.55f, right, r->leanPitch);   // lean forward
            base = tilt(base, 0.55f, fwd,   r->leanRoll);    // bank sideways
        }
    }
    r->draw(base, modelLoc);
}

void Player::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    const float hw = 0.4f, h = 1.9f;
    mn = position - glm::vec3(hw, 0.0f, hw);
    mx = position + glm::vec3(hw, h,    hw);
}
