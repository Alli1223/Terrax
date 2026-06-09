#include "ability.h"
#include "app_context.h"
#include "network.h"
#include "projectile.h"
#include "npc.h"
#include "gameplay.h"            // castChainHeal / castHealZone
#include "gameplay_internal.h"  // findTargetNpc / spawnAbilityFx
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// ---------------------------------------------------------------------------
// Ability implementations.
// ---------------------------------------------------------------------------
// Combat is server-authoritative: single-target hits ride PlayerAttackPacket
// (the server already resolves it into NPC damage), area + taunt effects ride
// AbilityCastPacket (the server resolves them in NpcDirector), and every cast
// fans a cosmetic SpellEffectPacket out to spectators. Outgoing damage is
// scaled by the caster's abilityPowerMult here, on the client send side, so the
// server stays role-agnostic.

namespace {

// Cosmetic spell-effect for spectators — the caster renders its own copy via
// spawnAbilityFx(); this tells the other clients to do the same.
void sendAbilitySpellEffect(AppContext& ctx, const glm::vec3& c, float radius, uint8_t kind) {
    if (!ctx.client) return;
    SpellEffectPacket se{};
    se.casterID = ctx.client->clientID;
    se.kind     = kind;
    se.x = c.x; se.y = c.y; se.z = c.z;
    se.radius   = radius;
    se.ttl      = 0.6f;
    ctx.client->send(PacketType::SpellEffect, &se, sizeof(se));
}

}  // namespace

// === Reusable cores ========================================================

void MeleeAbility::swing(AppContext& ctx, float damageScale) {
    if (!ctx.client) return;
    NPC* target = findTargetNpc(ctx, range_, facing_);
    PlayerAttackPacket ap{};
    ap.clientID    = ctx.client->clientID;
    ap.targetNpcId = target ? target->id : 0u;
    ap.damageScale = damageScale * ctx.abilityPowerMult;
    ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));
}

void MeleeAbility::activate(AppContext& ctx) {
    if (ctx.playerRig) { ctx.playerRig->isAttacking = true; ctx.playerRig->attackAnim = 0.0f; }
    swing(ctx, damage_);
}

void RangedAbility::spawnProjectile(AppContext& ctx, float damageScale) {
    auto bolt = std::make_unique<MagicBoltProjectile>();
    bolt->position      = ctx.camera.position + glm::vec3(0.0f, 1.5f, 0.0f) + ctx.camera.front * 0.6f;
    bolt->velocity      = ctx.camera.front * speed_;
    bolt->restingDir    = glm::normalize(ctx.camera.front);
    bolt->ownerClientId = ctx.client ? ctx.client->clientID : 0u;
    bolt->damageScale   = damageScale;
    ctx.objectManager.add(std::move(bolt));
}

void RangedAbility::activate(AppContext& ctx) {
    if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
    float scale = damage_ * ctx.abilityPowerMult;
    spawnProjectile(ctx, scale);
    if (ctx.client) {
        NPC* target = findTargetNpc(ctx, range_, facing_);
        PlayerAttackPacket ap{};
        ap.clientID    = ctx.client->clientID;
        ap.targetNpcId = target ? target->id : 0u;
        ap.damageScale = scale;
        ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));
    }
}

glm::vec3 AoeAbility::aoeCenter(AppContext& ctx) const {
    glm::vec3 fwd(ctx.camera.front.x, 0.0f, ctx.camera.front.z);
    if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
    return ctx.camera.position + fwd * (radius_ * 0.5f);
}

void AoeAbility::activate(AppContext& ctx) {
    if (!ctx.client) return;
    glm::vec3 c     = aoeCenter(ctx);
    float     scale = damage_ * ctx.abilityPowerMult;

    AbilityCastPacket ac{};
    ac.casterID  = ctx.client->clientID;
    ac.abilityId = (uint16_t)id_;
    ac.effect    = 0;             // AoE damage
    ac.x = c.x; ac.y = c.y; ac.z = c.z;
    ac.radius    = radius_;
    ac.scale     = scale;
    ctx.client->send(PacketType::AbilityCast, &ac, sizeof(ac));

    if (ctx.playerRig) { ctx.playerRig->isAttacking = true; ctx.playerRig->attackAnim = 0.0f; }
    spawnAbilityFx(ctx, c, radius_, fxKind_);          // local
    sendAbilitySpellEffect(ctx, c, radius_, fxKind_);  // spectators
}

void BuffAbility::activate(AppContext& ctx) {
    bool refreshed = false;
    for (auto& b : ctx.activeBuffs)
        if (b.id == id_) { b.ttl = duration_; b.magnitude = magnitude_; refreshed = true; break; }
    if (!refreshed) {
        ActiveBuff b;
        b.id = id_; b.kind = buffKind_; b.magnitude = magnitude_; b.ttl = duration_;
        ctx.activeBuffs.push_back(b);
    }
    if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
    glm::vec3 c = ctx.camera.position;
    spawnAbilityFx(ctx, c, 1.5f, 5);
    sendAbilitySpellEffect(ctx, c, 1.5f, 5);

    AppContext::HudToast t;
    t.text     = name_;
    t.color    = Voxel{120, 200, 255, 255};
    t.lifeTime = 1.4f;
    ctx.toasts.push_back(std::move(t));
}

// === Concrete abilities ====================================================
namespace {

// --- Tank ---
class TauntAbility : public Ability {
public:
    TauntAbility() {
        id_ = AbilityId::Taunt; name_ = "Taunt"; role_ = PlayerRole::Tank;
        desc_ = "A roar that forces nearby enemies to attack you for a few seconds.";
        kind_ = AbilityKind::Aoe; cooldown_ = 8.0f; cost_ = 15.0f;
    }
    void activate(AppContext& ctx) override {
        if (!ctx.client) return;
        const float radius = 10.0f, duration = 6.0f;
        glm::vec3 c = ctx.camera.position;
        AbilityCastPacket ac{};
        ac.casterID  = ctx.client->clientID;
        ac.abilityId = (uint16_t)id_;
        ac.effect    = 1;          // taunt
        ac.x = c.x; ac.y = c.y; ac.z = c.z;
        ac.radius    = radius;
        ac.scale     = duration;   // taunt carries its duration in `scale`
        ctx.client->send(PacketType::AbilityCast, &ac, sizeof(ac));
        if (ctx.playerRig) { ctx.playerRig->isAttacking = true; ctx.playerRig->attackAnim = 0.0f; }
        spawnAbilityFx(ctx, c, radius, 3);
        sendAbilitySpellEffect(ctx, c, radius, 3);
        AppContext::HudToast t{ "Taunt!", Voxel{255, 160, 60, 255}, 1.4f };
        ctx.toasts.push_back(std::move(t));
    }
};

class ShieldUpAbility : public BuffAbility {
public:
    ShieldUpAbility() {
        id_ = AbilityId::ShieldUp; name_ = "Shield Wall"; role_ = PlayerRole::Tank;
        desc_ = "Brace behind your shield, sharply raising defence for a few seconds.";
        kind_ = AbilityKind::Buff; cooldown_ = 12.0f; cost_ = 20.0f;
        buffKind_ = BuffKind::Defense; magnitude_ = 0.8f; duration_ = 6.0f;
    }
};

// --- Healer (reuse the existing casters) ---
class ChainHealAbility : public Ability {
public:
    ChainHealAbility() {
        id_ = AbilityId::ChainHeal; name_ = "Chain Heal"; role_ = PlayerRole::Healer;
        desc_ = "A wave of restoration that leaps from you to nearby allies.";
        kind_ = AbilityKind::Heal; cooldown_ = 4.0f; cost_ = 25.0f;
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
        castChainHeal(ctx, 24.0f * ctx.abilityPowerMult);
    }
};

class HealingSanctuaryAbility : public Ability {
public:
    HealingSanctuaryAbility() {
        id_ = AbilityId::HealingSanctuary; name_ = "Healing Sanctuary"; role_ = PlayerRole::Healer;
        desc_ = "Consecrate the ground you aim at, healing allies who stand within it.";
        kind_ = AbilityKind::Heal; cooldown_ = 12.0f; cost_ = 40.0f;
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
        castHealZone(ctx, 12.0f * ctx.abilityPowerMult, 4.5f, 6.0f);
    }
};

// --- DPS ---
class FlurryAbility : public MeleeAbility {
public:
    FlurryAbility() {
        id_ = AbilityId::Flurry; name_ = "Flurry"; role_ = PlayerRole::DPS;
        desc_ = "A rapid trio of strikes against the enemy in front of you.";
        kind_ = AbilityKind::Melee; cooldown_ = 5.0f; cost_ = 30.0f;
        range_ = 3.8f; facing_ = 0.30f; damage_ = 0.6f;   // per hit; three hits
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isAttacking = true; ctx.playerRig->attackAnim = 0.0f; }
        for (int i = 0; i < 3; i++) swing(ctx, damage_);
    }
};

class CleaveAbility : public AoeAbility {
public:
    CleaveAbility() {
        id_ = AbilityId::Cleave; name_ = "Cleave"; role_ = PlayerRole::DPS;
        desc_ = "A sweeping strike that damages every enemy in front of you.";
        kind_ = AbilityKind::Aoe; cooldown_ = 6.0f; cost_ = 35.0f;
        radius_ = 3.5f; damage_ = 1.0f; fxKind_ = 2;
    }
};

class FireballAbility : public RangedAbility {
public:
    FireballAbility() {
        id_ = AbilityId::Fireball; name_ = "Fireball"; role_ = PlayerRole::DPS;
        desc_ = "Hurl a bolt of fire that bursts on impact.";
        kind_ = AbilityKind::Ranged; cooldown_ = 3.0f; cost_ = 25.0f;
        range_ = 26.0f; facing_ = 0.93f; damage_ = 1.6f; speed_ = 28.0f;
    }
    void spawnProjectile(AppContext& ctx, float damageScale) override {
        auto bolt = std::make_unique<FireBoltProjectile>();
        bolt->position      = ctx.camera.position + glm::vec3(0.0f, 1.5f, 0.0f) + ctx.camera.front * 0.6f;
        bolt->velocity      = ctx.camera.front * speed_;
        bolt->restingDir    = glm::normalize(ctx.camera.front);
        bolt->ownerClientId = ctx.client ? ctx.client->clientID : 0u;
        bolt->damageScale   = damageScale;
        ctx.objectManager.add(std::move(bolt));
    }
};

// --- skill-tree unlocks (each is a few lines over an existing core) ---
class ShieldBashAbility : public MeleeAbility {
public:
    ShieldBashAbility() {
        id_ = AbilityId::ShieldBash; name_ = "Shield Bash"; role_ = PlayerRole::Tank;
        desc_ = "Slam your shield into a foe for heavy damage.";
        kind_ = AbilityKind::Melee; cooldown_ = 6.0f; cost_ = 20.0f;
        range_ = 3.5f; facing_ = 0.35f; damage_ = 1.8f;
    }
};

class ThunderClapAbility : public AoeAbility {
public:
    ThunderClapAbility() {
        id_ = AbilityId::ThunderClap; name_ = "Thunder Clap"; role_ = PlayerRole::Tank;
        desc_ = "Smash the ground, damaging every enemy around you.";
        kind_ = AbilityKind::Aoe; cooldown_ = 9.0f; cost_ = 30.0f;
        radius_ = 4.5f; damage_ = 0.8f; fxKind_ = 2;
    }
    glm::vec3 aoeCenter(AppContext& ctx) const override { return ctx.camera.position; }
};

class SmiteAbility : public RangedAbility {
public:
    SmiteAbility() {
        id_ = AbilityId::Smite; name_ = "Smite"; role_ = PlayerRole::Healer;
        desc_ = "Hurl a bolt of holy light at an enemy.";
        kind_ = AbilityKind::Ranged; cooldown_ = 3.0f; cost_ = 20.0f;
        range_ = 24.0f; facing_ = 0.93f; damage_ = 1.2f; speed_ = 34.0f;
    }
};

class GreaterHealAbility : public Ability {
public:
    GreaterHealAbility() {
        id_ = AbilityId::GreaterHeal; name_ = "Greater Heal"; role_ = PlayerRole::Healer;
        desc_ = "A powerful wave of restoration to you and your allies.";
        kind_ = AbilityKind::Heal; cooldown_ = 8.0f; cost_ = 45.0f;
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
        castChainHeal(ctx, 45.0f * ctx.abilityPowerMult);
    }
};

class WhirlwindAbility : public AoeAbility {
public:
    WhirlwindAbility() {
        id_ = AbilityId::Whirlwind; name_ = "Whirlwind"; role_ = PlayerRole::DPS;
        desc_ = "Spin with blades extended, hitting everything around you.";
        kind_ = AbilityKind::Aoe; cooldown_ = 8.0f; cost_ = 40.0f;
        radius_ = 4.0f; damage_ = 1.1f; fxKind_ = 2;
    }
    glm::vec3 aoeCenter(AppContext& ctx) const override { return ctx.camera.position; }
};

class PiercingShotAbility : public RangedAbility {
public:
    PiercingShotAbility() {
        id_ = AbilityId::PiercingShot; name_ = "Piercing Shot"; role_ = PlayerRole::DPS;
        desc_ = "A bolt that pierces through enemies in a line.";
        kind_ = AbilityKind::Ranged; cooldown_ = 5.0f; cost_ = 30.0f;
        range_ = 28.0f; facing_ = 0.95f; damage_ = 1.4f; speed_ = 48.0f;
    }
    void spawnProjectile(AppContext& ctx, float damageScale) override {
        auto bolt = std::make_unique<ArcaneBoltProjectile>();  // pierces on NPC hit
        bolt->position      = ctx.camera.position + glm::vec3(0.0f, 1.5f, 0.0f) + ctx.camera.front * 0.6f;
        bolt->velocity      = ctx.camera.front * speed_;
        bolt->restingDir    = glm::normalize(ctx.camera.front);
        bolt->ownerClientId = ctx.client ? ctx.client->clientID : 0u;
        bolt->damageScale   = damageScale;
        ctx.objectManager.add(std::move(bolt));
    }
};

}  // namespace

// === Factory + role kits ===================================================

std::unique_ptr<Ability> createAbility(AbilityId id) {
    switch (id) {
        case AbilityId::Taunt:            return std::make_unique<TauntAbility>();
        case AbilityId::ShieldUp:         return std::make_unique<ShieldUpAbility>();
        case AbilityId::ChainHeal:        return std::make_unique<ChainHealAbility>();
        case AbilityId::HealingSanctuary: return std::make_unique<HealingSanctuaryAbility>();
        case AbilityId::Flurry:           return std::make_unique<FlurryAbility>();
        case AbilityId::Cleave:           return std::make_unique<CleaveAbility>();
        case AbilityId::Fireball:         return std::make_unique<FireballAbility>();
        case AbilityId::ShieldBash:       return std::make_unique<ShieldBashAbility>();
        case AbilityId::ThunderClap:      return std::make_unique<ThunderClapAbility>();
        case AbilityId::Smite:            return std::make_unique<SmiteAbility>();
        case AbilityId::GreaterHeal:      return std::make_unique<GreaterHealAbility>();
        case AbilityId::Whirlwind:        return std::make_unique<WhirlwindAbility>();
        case AbilityId::PiercingShot:     return std::make_unique<PiercingShotAbility>();
        case AbilityId::None:
        default:                          return nullptr;
    }
}

// The pre-unlocked "core" each role starts with. The remaining abilities are
// bought with skill points in the tree (see skill_tree.cpp).
const std::vector<AbilityId>& roleStartingAbilities(PlayerRole role) {
    static const std::vector<AbilityId> tank   = { AbilityId::Taunt, AbilityId::ShieldUp };
    static const std::vector<AbilityId> healer = { AbilityId::ChainHeal, AbilityId::HealingSanctuary };
    static const std::vector<AbilityId> dps    = { AbilityId::Flurry, AbilityId::Cleave };
    switch (role) {
        case PlayerRole::Tank:   return tank;
        case PlayerRole::Healer: return healer;
        case PlayerRole::DPS:
        default:                 return dps;
    }
}

const char* resourceName(ResourceType r) {
    switch (r) {
        case ResourceType::Mana: return "Mana";
        case ResourceType::Rage: return "Rage";
        case ResourceType::Energy:
        default:                 return "Energy";
    }
}
