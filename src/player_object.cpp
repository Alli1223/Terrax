#include "player_object.h"
#include "camera.h"
#include "voxel_model.h"
#include "network.h"
#include "items.h"      // VehicleKind
#include "vehicle.h"    // getHorseMesh / getKiteMesh / getWagonMesh
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

    position    = p->position;
    yaw         = p->yaw;
    vehicleKind = static_cast<VehicleKind>(p->vehicleKind);   // render their mount

    // Trail a pulled wagon a few metres behind the remote player so other
    // clients see it follow (the exact stash position isn't networked).
    {
        float yr = glm::radians(p->yaw);
        glm::vec3 fwd(sinf(yr), 0.0f, cosf(yr));
        glm::vec3 target = p->position - fwd * 3.0f;
        if (!trailInit) { trailPos = target; trailYaw = p->yaw; trailInit = true; }
        else {
            float k = std::min(1.0f, 4.0f * dt);
            trailPos = glm::mix(trailPos, target, k);
            float dy = p->yaw - trailYaw;
            while (dy > 180.0f) dy -= 360.0f;
            while (dy < -180.0f) dy += 360.0f;
            trailYaw += dy * k;
        }
    }
}

// Draw a vehicle mesh attached to the player. `meshOffset` is the voxel in the
// mesh that should land on the player's ground position; `extraLift` raises the
// whole model. Shared by the local player (set from ctx.activeVehicle) and
// remotes (from RemotePlayer.vehicleKind). Returns how far to lift the rider.
float Player::drawVehicle(GLuint modelLoc) const {
    const float VS = 0.06f;            // vehicle voxel scale (block/voxel)
    auto blit = [&](VoxelVolume* mv, glm::vec3 meshOffset, glm::vec3 extraWorld) {
        if (!mv) return;
        glm::mat4 m = baseMatrix(VS);
        m = glm::translate(m, extraWorld);   // extra placement in the mesh frame
        m = glm::translate(m, -meshOffset);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &m[0][0]);
        mv->draw();
    };
    switch (vehicleKind) {
        case VehicleKind::Horse: {
            VoxelVolume* hv = getHorseMesh();
            if (hv) blit(hv, glm::vec3((float)hv->sizeX * 0.5f, 0.0f, 25.0f), glm::vec3(0.0f));
            return VS * 24.0f;        // lift the rider onto the saddle
        }
        case VehicleKind::Kite: {
            VoxelVolume* kv = getKiteMesh();
            // Held overhead, tilted back a little, centred on the player.
            if (kv) blit(kv, glm::vec3((float)kv->sizeX * 0.5f, 0.0f, 0.0f),
                         glm::vec3(0.0f, 34.0f, -6.0f));
            return 0.0f;
        }
        case VehicleKind::Wagon: {
            // The wagon trails behind, so it's drawn at its own world transform
            // (trailPos/trailYaw), not attached to the player's baseMatrix.
            VoxelVolume* wv = getWagonMesh();
            if (wv) {
                glm::mat4 m = glm::translate(glm::mat4(1.0f), trailPos);
                m = glm::rotate(m, glm::radians(trailYaw), glm::vec3(0, 1, 0));
                m = glm::scale(m, glm::vec3(VS));
                m = glm::translate(m, glm::vec3(-(float)wv->sizeX * 0.5f, 0.0f,
                                                -(float)wv->sizeZ * 0.5f));
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &m[0][0]);
                wv->draw();
            }
            return 0.0f;
        }
        default: return 0.0f;
    }
}

void Player::draw(GLuint modelLoc) const {
    BipedalRig* r = isLocal ? localRig : (remote ? remote->rig : nullptr);
    if (!r) return;
    float riderLift = (vehicleKind != VehicleKind::None) ? drawVehicle(modelLoc) : 0.0f;
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
    if (riderLift > 0.0f)              // seat the rider on a mount
        base = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, riderLift, 0.0f)) * base;
    r->draw(base, modelLoc);
}

void Player::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    const float hw = 0.4f, h = 1.9f;
    mn = position - glm::vec3(hw, 0.0f, hw);
    mx = position + glm::vec3(hw, h,    hw);
}
