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
    ctx.playerHealth  = std::min(1.0f, ctx.playerHealth + healPerTarget / ctx.maxHpScaled);
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
        // Animate the casting player's remote model — abilities that don't send
        // a PlayerAttack (taunt, shield, heals) would otherwise look static.
        auto rpIt = ctx.remotePlayers.find(e.casterID);
        if (rpIt != ctx.remotePlayers.end()) {
            rpIt->second.isAttacking = true;
            rpIt->second.attackAnim  = 0.0f;
        }
        if (e.kind == 0) {
            HealZone z;
            z.pos           = glm::vec3(e.x, e.y, e.z);
            z.radius        = e.radius;
            z.ttl           = e.ttl;
            z.maxTtl        = e.ttl;
            z.ownerClientId = e.casterID;   // not us → cosmetic only
            ctx.healZones.push_back(z);
            spawnHealRing(ctx, z.pos, z.radius);
        } else if (e.kind == 1) {
            spawnHealMotes(ctx, glm::vec3(e.x, e.y, e.z), 14, 0.5f, 3.0f);
        } else {
            // Ability cosmetics (aoe slash / taunt ring / cast flash / aura).
            spawnAbilityFx(ctx, glm::vec3(e.x, e.y, e.z), e.radius, e.kind);
        }
    }
    ctx.client->spellEffects.clear();
}

// Coloured voxel-particle burst for an ability cast — see the header. Reuses
// the heal-zone particle batch (ctx.voxelParticles) so the renderer needs no
// changes; only the palette / shape differ per kind.
void spawnAbilityFx(AppContext& ctx, const glm::vec3& center, float radius, uint8_t kind) {
    Voxel colA, colB;
    int   count   = 24;
    float spread  = radius * 0.8f;
    float up      = 1.4f;
    float outward = 0.0f;     // radial expansion speed — rings/domes punch outward
    float lifeMin = 0.6f, lifeVar = 0.6f;
    float sizeMin = 0.05f, sizeVar = 0.04f;
    int   shape   = 0;        // 0 burst · 1 ground ring · 2 rising column · 3 dome shell
    switch (kind) {
        case 3:  // TAUNT — fierce expanding orange/red ground shockwave
            colA = {255, 185, 75, 255};  colB = {255, 80, 35, 255};
            count = 48; shape = 1; up = 1.1f; outward = 5.5f;
            lifeMin = 0.5f; lifeVar = 0.45f; sizeMin = 0.06f; sizeVar = 0.05f; break;
        case 4:  // cast flash — violet
            colA = {200, 160, 255, 255}; colB = {130, 90, 220, 255};
            count = 16; spread = 0.5f; up = 2.4f; break;
        case 5:  // buff aura — blue
            colA = {130, 200, 255, 255}; colB = {80, 140, 230, 255};
            count = 22; spread = 0.6f; up = 1.6f; break;
        case 6:  // earth slam — kicked-up brown dust ring
            colA = {170, 130, 80, 255};  colB = {120, 90, 55, 255};
            count = 30; shape = 1; up = 0.8f; outward = 2.6f; break;
        case 7:  // nature renew — gentle green motes
            colA = {150, 235, 120, 255}; colB = {90, 190, 90, 255};
            count = 20; spread = 0.7f; up = 1.0f; break;
        case 8:  // holy nova — radiant white-gold ring
            colA = {255, 245, 210, 255}; colB = {255, 215, 130, 255};
            count = 34; shape = 1; up = 1.8f; outward = 3.6f; break;
        case 9:  // frost — pale icy shards
            colA = {210, 240, 255, 255}; colB = {130, 195, 245, 255};
            count = 18; spread = 0.5f; up = 1.6f; break;
        case 10: // lightning ring — crackling yellow
            colA = {255, 240, 130, 255}; colB = {255, 200, 70, 255};
            count = 30; shape = 1; up = 1.2f; outward = 4.0f; break;
        case 11: // inferno — large roaring firestorm
            colA = {255, 140, 50, 255};  colB = {220, 50, 30, 255};
            count = 38; spread = radius * 0.9f; up = 2.2f; break;
        case 12: // shout — golden roar ring
            colA = {255, 215, 110, 255}; colB = {235, 160, 60, 255};
            count = 30; shape = 1; up = 1.0f; outward = 4.6f; break;
        case 13: // SHIELD DOME — cyan/blue protective bubble swelling outward
            colA = {160, 220, 255, 255}; colB = {90, 160, 240, 255};
            count = 54; shape = 3; up = 1.0f; outward = 3.0f;
            lifeMin = 0.55f; lifeVar = 0.5f; sizeMin = 0.05f; sizeVar = 0.045f; break;
        case 14: // ROAR EMBERS — orange embers blasting up in a tight column
            colA = {255, 195, 95, 255};  colB = {255, 110, 40, 255};
            count = 32; shape = 2; up = 4.4f;
            lifeMin = 0.5f; lifeVar = 0.6f; sizeMin = 0.045f; sizeVar = 0.05f; break;
        case 15: // MELEE IMPACT — quick bright sparks at the point of contact
            colA = {255, 250, 225, 255}; colB = {255, 205, 120, 255};
            count = 14; shape = 3; up = 1.2f; outward = 3.4f;
            lifeMin = 0.16f; lifeVar = 0.18f; sizeMin = 0.04f; sizeVar = 0.03f; break;
        case 2:  // aoe slash — fiery
        default:
            colA = {255, 150, 60, 255};  colB = {255, 80, 40, 255};
            count = 24; spread = radius * 0.8f; up = 1.4f; break;
    }
    for (int i = 0; i < count; i++) {
        VoxelDeathParticle p;
        p.color = (hrand(0.0f, 1.0f) < 0.5f) ? colA : colB;
        glm::vec3 vel;
        if (shape == 1) {              // ground ring — flies outward + a little up
            float ang = hrand(0.0f, 6.2831853f);
            glm::vec3 dir(std::cos(ang), 0.0f, std::sin(ang));
            p.pos = center + dir * radius + glm::vec3(0.0f, 0.1f, 0.0f);
            vel   = dir * outward + glm::vec3(0.0f, up + hrand(0.2f, 1.4f), 0.0f);
        } else if (shape == 2) {       // tight column — embers rocketing up
            float ang = hrand(0.0f, 6.2831853f), rr = hrand(0.0f, 0.4f);
            p.pos = center + glm::vec3(std::cos(ang) * rr, hrand(0.0f, 0.6f), std::sin(ang) * rr);
            vel   = glm::vec3(hrand(-0.6f, 0.6f), up + hrand(0.0f, 2.5f), hrand(-0.6f, 0.6f));
        } else if (shape == 3) {       // dome shell — hemisphere swelling outward + up
            float ang = hrand(0.0f, 6.2831853f), el = hrand(0.05f, 1.45f);
            glm::vec3 dir(std::cos(ang) * std::cos(el), std::sin(el), std::sin(ang) * std::cos(el));
            p.pos = center + dir * (radius * 0.5f) + glm::vec3(0.0f, 0.4f, 0.0f);
            vel   = dir * outward + glm::vec3(0.0f, up, 0.0f);
        } else {                       // burst — random spread
            p.pos = center + glm::vec3(hrand(-spread, spread),
                                       hrand(0.0f, spread * 0.5f + 0.2f),
                                       hrand(-spread, spread));
            vel   = glm::vec3(hrand(-1.2f, 1.2f), up + hrand(0.4f, 2.2f), hrand(-1.2f, 1.2f));
        }
        p.vel     = vel;
        p.life    = lifeMin + hrand(0.0f, lifeVar);
        p.maxLife = p.life;
        p.size    = sizeMin + hrand(0.0f, sizeVar);
        ctx.voxelParticles.push_back(p);
    }
}

// While a buff is active (Shield Wall, Last Stand, Barrier, Battle Shout, ...)
// trickle a soft aura of motes up around the local player so it stays obvious
// the effect is still on, not just when it was cast. Colour keys to the buff:
// defence = steel-blue, power = gold. Throttled to a gentle few per second and
// fades out over the buff's final second. Local-player only.
void updateBuffAura(AppContext& ctx) {
    for (const ActiveBuff& b : ctx.activeBuffs) {
        float chance = 0.11f * std::min(1.0f, b.ttl);   // ~6/sec, easing off at the end
        if (hrand(0.0f, 1.0f) > chance) continue;
        Voxel col = (b.kind == BuffKind::Power) ? Voxel{255, 210, 110, 255}
                                                : Voxel{140, 205, 255, 255};
        float ang = hrand(0.0f, 6.2831853f), rr = 0.45f + hrand(0.0f, 0.3f);
        VoxelDeathParticle p;
        p.color   = col;
        p.pos     = ctx.camera.position + glm::vec3(std::cos(ang) * rr, hrand(0.1f, 1.7f),
                                                    std::sin(ang) * rr);
        p.vel     = glm::vec3(std::cos(ang) * 0.4f, 2.2f + hrand(0.0f, 1.0f), std::sin(ang) * 0.4f);
        p.life    = 0.4f + hrand(0.0f, 0.35f);
        p.maxLife = p.life;
        p.size    = 0.04f + hrand(0.0f, 0.03f);
        ctx.voxelParticles.push_back(p);
    }
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
            ctx.playerHealth = std::min(1.0f, ctx.playerHealth + z.healPerPulse / ctx.maxHpScaled);
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

