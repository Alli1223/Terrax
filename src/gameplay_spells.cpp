// The healing staff: chain-heal and AoE heal-zone spells plus their cosmetic
// voxel-particle flourishes (motes, beams, rings) and the per-frame heal-zone
// pulse / inbound-heal drain. Split out of gameplay.cpp. The public casters are
// declared in gameplay.h; the per-frame hooks in gameplay_internal.h.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gameplay.h"
#include "app_context.h"
#include "physics.h"
#include "network.h"
#include "game_session.h"
#include "town.h"
#include "prop_placement.h"
#include "vehicle.h"
#include "npc.h"
#include "animal.h"
#include "item_generator.h"
#include "loot_drop.h"
#include "projectile.h"
#include "voxel_model.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <cmath>
#include <cstring>
#include <mutex>
#include <random>
#include <memory>
#include "gameplay_internal.h"

// ---------------------------------------------------------------------------
// Healing staff — chain heal + AOE heal zone
// ---------------------------------------------------------------------------
// Health is client-owned (every client applies its own damage/heal), so the
// caster heals itself directly and relays heals to other players: it sends a
// PlayerHealPacket per target, the server rebroadcasts to the other clients,
// and each targeted client tops up its own health. The cosmetic chain beams
// and zone fountain reuse the death-explosion voxel-particle batch (just
// tinted green/gold), so the renderer needs no changes.

namespace {

std::mt19937& healRng() {
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}
float hrand(float lo, float hi) {
    std::uniform_real_distribution<float> d(lo, hi);
    return d(healRng());
}

// Push a clutch of green/gold "restoration" motes that fountain upward and
// fade. Reuses ctx.voxelParticles so they render in the existing batch.
void spawnHealMotes(AppContext& ctx, glm::vec3 center, int count,
                    float spread, float upBias) {
    for (int i = 0; i < count; i++) {
        VoxelDeathParticle p;
        if (hrand(0.0f, 1.0f) < 0.75f)
            p.color = Voxel{ (uint8_t)hrand(60, 120), (uint8_t)hrand(200, 255),
                             (uint8_t)hrand(120, 170), 255 };      // green
        else
            p.color = Voxel{ (uint8_t)hrand(225, 255), (uint8_t)hrand(225, 255),
                             (uint8_t)hrand(140, 185), 255 };      // gold spark
        p.pos = center + glm::vec3(hrand(-spread, spread),
                                   hrand(0.0f, spread * 0.5f),
                                   hrand(-spread, spread));
        p.vel = glm::vec3(hrand(-0.8f, 0.8f),
                          upBias + hrand(0.6f, 2.6f),
                          hrand(-0.8f, 0.8f));
        p.life    = 0.9f + hrand(0.0f, 0.8f);
        p.maxLife = p.life;
        p.size    = 0.05f + hrand(0.0f, 0.03f);
        ctx.voxelParticles.push_back(p);
    }
}

// Trail of motes connecting two heal targets — sells the "chain" leap.
void spawnHealBeam(AppContext& ctx, glm::vec3 a, glm::vec3 b) {
    const int steps = 16;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        spawnHealMotes(ctx, a + (b - a) * t, 2, 0.12f, 0.5f);
    }
}

// Ring of motes around the rim of a heal zone.
void spawnHealRing(AppContext& ctx, glm::vec3 center, float radius) {
    const int n = 22;
    for (int i = 0; i < n; i++) {
        float ang = (float)i / n * 6.2831853f;
        spawnHealMotes(ctx, center + glm::vec3(std::cos(ang) * radius, 0.05f,
                                               std::sin(ang) * radius),
                       1, 0.1f, 2.2f);
    }
}

// Tell the rest of the session a player was healed. The server rebroadcasts
// to the other clients (skipping us); the targeted client tops up its health.
void sendHeal(AppContext& ctx, uint32_t targetID, float amount, glm::vec3 pos) {
    if (!ctx.client) return;
    PlayerHealPacket hp{};
    hp.targetID = targetID;
    hp.amount   = amount;
    hp.x = pos.x; hp.y = pos.y; hp.z = pos.z;
    ctx.client->send(PacketType::PlayerHeal, &hp, sizeof(hp));
}

}  // namespace

void castChainHeal(AppContext& ctx, float healPerTarget) {
    if (!ctx.client) return;

    const int   maxJumps  = 4;        // up to 4 allies chained after the caster
    const float jumpRange = 12.0f;
    const float jumpR2    = jumpRange * jumpRange;

    // The caster is always the first link — mend self immediately.
    glm::vec3 fromPos = ctx.camera.position + glm::vec3(0.0f, 1.2f, 0.0f);
    ctx.playerHealth  = std::min(1.0f, ctx.playerHealth + healPerTarget / 100.0f);
    spawnHealMotes(ctx, fromPos, 18, 0.5f, 3.2f);
    sendHeal(ctx, ctx.client->clientID, healPerTarget, fromPos);   // so spectators see it

    // Greedily jump to the nearest not-yet-healed player from the last link.
    std::unordered_set<uint32_t> healed;
    for (int jump = 0; jump < maxJumps; jump++) {
        RemotePlayer* best = nullptr;
        float bestD2 = jumpR2;
        for (auto& [id, rp] : ctx.remotePlayers) {
            if (healed.count(id)) continue;
            glm::vec3 d = rp.position - fromPos;
            float d2 = glm::dot(d, d);
            if (d2 < bestD2) { bestD2 = d2; best = &rp; }
        }
        if (!best) break;

        glm::vec3 tgtPos = best->position + glm::vec3(0.0f, 1.2f, 0.0f);
        spawnHealBeam(ctx, fromPos, tgtPos);
        spawnHealMotes(ctx, tgtPos, 16, 0.5f, 3.2f);
        best->health = std::min(100.0f, best->health + healPerTarget);   // cosmetic mirror
        sendHeal(ctx, best->id, healPerTarget, tgtPos);
        healed.insert(best->id);
        fromPos = tgtPos;
    }

    AppContext::HudToast t;
    t.text     = healed.empty() ? "Chain Heal"
                                : "Chain Heal x" + std::to_string((int)healed.size() + 1);
    t.color    = Voxel{120, 230, 150, 255};
    t.lifeTime = 1.4f;
    ctx.toasts.push_back(std::move(t));
}

void castHealZone(AppContext& ctx, float healPerPulse, float radius, float duration) {
    if (!ctx.client) return;

    // Aim point: the surface the player is looking at, else at their feet.
    glm::vec3 origin = ctx.camera.position + glm::vec3(0.0f, 1.6f, 0.0f);
    glm::ivec3 hb, hn;
    glm::vec3 pos;
    if (ctx.world.raycast(origin, ctx.camera.front, 18.0f, hb, hn))
        pos = glm::vec3(hb.x + 0.5f, (float)(hb.y + 1), hb.z + 0.5f);
    else
        pos = ctx.camera.position + glm::vec3(0.0f, 0.1f, 0.0f);

    HealZone z;
    z.pos           = pos;
    z.radius        = radius;
    z.ttl           = duration;
    z.maxTtl        = duration;
    z.healPerPulse  = healPerPulse;
    z.ownerClientId = ctx.client->clientID;
    ctx.healZones.push_back(z);

    spawnHealRing(ctx, pos, radius);   // opening flourish

    // Let spectators render a cosmetic copy of the zone.
    SpellEffectPacket se{};
    se.casterID = ctx.client->clientID;
    se.kind     = 0;
    se.x = pos.x; se.y = pos.y; se.z = pos.z;
    se.radius   = radius;
    se.ttl      = duration;
    ctx.client->send(PacketType::SpellEffect, &se, sizeof(se));

    AppContext::HudToast t;
    t.text     = "Healing Sanctuary";
    t.color    = Voxel{120, 230, 150, 255};
    t.lifeTime = 1.6f;
    ctx.toasts.push_back(std::move(t));
}

// Drain the heal + spell-effect packets received this frame: apply heals aimed
// at us, sparkle every healed player, and spawn the cosmetic zones/bursts that
// other players cast.
void drainSpellEvents(AppContext& ctx) {
    if (!ctx.client) return;

    for (const PlayerHealPacket& h : ctx.client->healEvents) {
        if (h.targetID == ctx.client->clientID)
            ctx.client->pendingSelfHeal += h.amount;        // applied in updatePlayerVitals
        auto it = ctx.remotePlayers.find(h.targetID);
        if (it != ctx.remotePlayers.end())
            it->second.health = std::min(100.0f, it->second.health + h.amount);
        spawnHealMotes(ctx, glm::vec3(h.x, h.y, h.z), 14, 0.5f, 3.0f);
    }
    ctx.client->healEvents.clear();

    for (const SpellEffectPacket& e : ctx.client->spellEffects) {
        if (e.kind == 0) {
            HealZone z;
            z.pos           = glm::vec3(e.x, e.y, e.z);
            z.radius        = e.radius;
            z.ttl           = e.ttl;
            z.maxTtl        = e.ttl;
            z.ownerClientId = e.casterID;   // not us → cosmetic only
            ctx.healZones.push_back(z);
            spawnHealRing(ctx, z.pos, z.radius);
        } else {
            spawnHealMotes(ctx, glm::vec3(e.x, e.y, e.z), 14, 0.5f, 3.0f);
        }
    }
    ctx.client->spellEffects.clear();
}

// Advance active heal zones: emit the particle fountain for everyone, and on
// the owner's client pulse health to any player standing inside the ring.
void updateHealZones(AppContext& ctx) {
    const float dt = ctx.deltaTime;
    const float pulseInterval = 1.0f;
    const bool  haveClient = ctx.client != nullptr;
    const uint32_t myId = haveClient ? ctx.client->clientID : 0u;

    for (auto& z : ctx.healZones) {
        z.ttl -= dt;

        // Particle fountain (everyone) — rate-limited so we don't flood.
        z.emitTimer -= dt;
        if (z.emitTimer <= 0.0f) {
            z.emitTimer = 0.1f;
            spawnHealRing(ctx, z.pos, z.radius);
            spawnHealMotes(ctx, z.pos + glm::vec3(0.0f, 0.1f, 0.0f), 4,
                           z.radius * 0.7f, 2.6f);
        }

        // Healing only runs on the caster's client.
        if (!haveClient || z.ownerClientId != myId) continue;
        z.pulseTimer -= dt;
        if (z.pulseTimer > 0.0f) continue;
        z.pulseTimer = pulseInterval;
        const float r2 = z.radius * z.radius;

        // Self.
        float sdx = ctx.camera.position.x - z.pos.x;
        float sdz = ctx.camera.position.z - z.pos.z;
        if (sdx * sdx + sdz * sdz <= r2 && ctx.playerHealth < 1.0f) {
            ctx.playerHealth = std::min(1.0f, ctx.playerHealth + z.healPerPulse / 100.0f);
            glm::vec3 sp = ctx.camera.position + glm::vec3(0.0f, 1.2f, 0.0f);
            spawnHealMotes(ctx, sp, 10, 0.4f, 3.0f);
            sendHeal(ctx, myId, z.healPerPulse, sp);
        }
        // Remote players.
        for (auto& [id, rp] : ctx.remotePlayers) {
            float rdx = rp.position.x - z.pos.x;
            float rdz = rp.position.z - z.pos.z;
            if (rdx * rdx + rdz * rdz > r2) continue;
            rp.health = std::min(100.0f, rp.health + z.healPerPulse);
            glm::vec3 tp = rp.position + glm::vec3(0.0f, 1.2f, 0.0f);
            spawnHealMotes(ctx, tp, 8, 0.4f, 3.0f);
            sendHeal(ctx, id, z.healPerPulse, tp);
        }
    }

    ctx.healZones.erase(
        std::remove_if(ctx.healZones.begin(), ctx.healZones.end(),
            [](const HealZone& z){ return z.ttl <= 0.0f; }),
        ctx.healZones.end());
}

