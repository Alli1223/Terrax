#include "player_object.h"
#include "camera.h"
#include "voxel_model.h"
#include "network.h"
#include <algorithm>

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

    position = p->position;
    yaw      = p->yaw;
}

void Player::draw(GLuint modelLoc) const {
    BipedalRig* r = isLocal ? localRig : (remote ? remote->rig : nullptr);
    if (!r) return;
    r->draw(baseMatrix(0.06f * r->heightScale), modelLoc);
}

void Player::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    const float hw = 0.4f, h = 1.9f;
    mn = position - glm::vec3(hw, 0.0f, hw);
    mx = position + glm::vec3(hw, h,    hw);
}
