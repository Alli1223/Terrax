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

// Spawn a cosmetic caster bolt of the given Projectile subclass, aimed down the
// camera. Every RangedAbility uses this, so adding an elemental bolt is one line
// (launchBolt<IceBoltProjectile>) rather than a copied six-line block.
template <class BoltT>
void launchBolt(AppContext& ctx, float speed, float damageScale) {
    auto bolt = std::make_unique<BoltT>();
    bolt->position      = ctx.camera.position + glm::vec3(0.0f, 1.5f, 0.0f) + ctx.camera.front * 0.6f;
    bolt->velocity      = ctx.camera.front * speed;
    bolt->restingDir    = glm::normalize(ctx.camera.front);
    bolt->ownerClientId = ctx.client ? ctx.client->clientID : 0u;
    bolt->damageScale   = damageScale;
    ctx.objectManager.add(std::move(bolt));
}

}  // namespace

// === Reusable cores ========================================================

void MeleeAbility::swing(AppContext& ctx, float damageScale) {
    if (!ctx.client) return;
    NPC* target = findTargetNpc(ctx, range_, facing_);
    PlayerAttackPacket ap{};
    ap.clientID    = ctx.client->clientID;
    ap.targetNpcId = target ? target->id : 0u;
    ap.damageScale = damageScale * ctx.buffedAbilityPower();
    ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));
}

void MeleeAbility::activate(AppContext& ctx) {
    if (ctx.playerRig) { ctx.playerRig->isAttacking = true; ctx.playerRig->attackAnim = 0.0f; }
    swing(ctx, damage_);
}

void RangedAbility::spawnProjectile(AppContext& ctx, float damageScale) {
    launchBolt<MagicBoltProjectile>(ctx, speed_, damageScale);
}

void RangedAbility::activate(AppContext& ctx) {
    if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
    float scale = damage_ * ctx.buffedAbilityPower();
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
    float     scale = damage_ * ctx.buffedAbilityPower();

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
        if (b.id == id_) { b.ttl = duration_; b.total = duration_; b.magnitude = magnitude_; refreshed = true; break; }
    if (!refreshed) {
        ActiveBuff b;
        b.id = id_; b.kind = buffKind_; b.magnitude = magnitude_; b.ttl = duration_; b.total = duration_;
        ctx.activeBuffs.push_back(b);
    }
    // A clear, ability-appropriate gesture instead of the staff weave: a defiant
    // shout for power buffs, a planted shield-brace for defensive ones.
    if (ctx.playerRig)
        ctx.playerRig->playClip(buffKind_ == BuffKind::Power ? ClipKind::Roar : ClipKind::Brace, 0.7f);
    glm::vec3 c = ctx.camera.position;
    spawnAbilityFx(ctx, c, fxRadius_, fxKind_);
    sendAbilitySpellEffect(ctx, c, fxRadius_, fxKind_);

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
        icon_ = AbilityIcon::Chevrons; iconColor_ = {255, 150, 60};
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
        // A bellowing roar, not a sword swing — plus a layered blast: an
        // expanding ground shockwave AND a column of embers erupting off the
        // Tank, so it's unmistakable who just pulled aggro.
        if (ctx.playerRig) ctx.playerRig->playClip(ClipKind::Roar, 0.8f);
        spawnAbilityFx(ctx, c, radius, 3);          // expanding shockwave ring
        spawnAbilityFx(ctx, c, 1.4f, 14);           // ember column off the caster
        sendAbilitySpellEffect(ctx, c, radius, 3);  // spectators see the shockwave
        AppContext::HudToast t{ "Taunt!", Voxel{255, 160, 60, 255}, 1.4f };
        ctx.toasts.push_back(std::move(t));
    }
};

class ShieldUpAbility : public BuffAbility {
public:
    ShieldUpAbility() {
        id_ = AbilityId::ShieldUp; name_ = "Shield Wall"; role_ = PlayerRole::Tank;
        desc_ = "Brace behind your shield, sharply raising defence for a few seconds.";
        kind_ = AbilityKind::Buff; cooldown_ = 12.0f; cost_ = 20.0f; castTime_ = 0.8f;
        buffKind_ = BuffKind::Defense; magnitude_ = 0.8f; duration_ = 6.0f;
        fxKind_ = 13; fxRadius_ = 2.4f;   // cyan shield dome
        icon_ = AbilityIcon::Shield; iconColor_ = {120, 180, 235};
    }
};

// --- Healer (reuse the existing casters) ---
class ChainHealAbility : public Ability {
public:
    ChainHealAbility() {
        id_ = AbilityId::ChainHeal; name_ = "Chain Heal"; role_ = PlayerRole::Healer;
        desc_ = "A wave of restoration that leaps from you to nearby allies.";
        kind_ = AbilityKind::Heal; cooldown_ = 4.0f; cost_ = 25.0f;
        icon_ = AbilityIcon::Cross; iconColor_ = {120, 230, 150};
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
        castChainHeal(ctx, 24.0f * ctx.buffedAbilityPower());
    }
};

class HealingSanctuaryAbility : public Ability {
public:
    HealingSanctuaryAbility() {
        id_ = AbilityId::HealingSanctuary; name_ = "Healing Sanctuary"; role_ = PlayerRole::Healer;
        desc_ = "Consecrate the ground you aim at, healing allies who stand within it.";
        kind_ = AbilityKind::Heal; cooldown_ = 12.0f; cost_ = 40.0f; castTime_ = 1.5f;
        icon_ = AbilityIcon::Sanctuary; iconColor_ = {120, 230, 150};
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
        castHealZone(ctx, 12.0f * ctx.buffedAbilityPower(), 4.5f, 6.0f);
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
        icon_ = AbilityIcon::Swords; iconColor_ = {235, 90, 70};
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
        icon_ = AbilityIcon::Slash; iconColor_ = {245, 120, 60};
    }
};

class FireballAbility : public RangedAbility {
public:
    FireballAbility() {
        id_ = AbilityId::Fireball; name_ = "Fireball"; role_ = PlayerRole::DPS;
        desc_ = "Hurl a bolt of fire that bursts on impact.";
        kind_ = AbilityKind::Ranged; cooldown_ = 3.0f; cost_ = 25.0f;
        range_ = 26.0f; facing_ = 0.93f; damage_ = 1.6f; speed_ = 28.0f;
        icon_ = AbilityIcon::Flame; iconColor_ = {255, 140, 50};
    }
    void spawnProjectile(AppContext& ctx, float damageScale) override {
        launchBolt<FireBoltProjectile>(ctx, speed_, damageScale);
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
        icon_ = AbilityIcon::ShieldBash; iconColor_ = {175, 185, 205};
    }
};

class ThunderClapAbility : public AoeAbility {
public:
    ThunderClapAbility() {
        id_ = AbilityId::ThunderClap; name_ = "Thunder Clap"; role_ = PlayerRole::Tank;
        desc_ = "Smash the ground, damaging every enemy around you.";
        kind_ = AbilityKind::Aoe; cooldown_ = 9.0f; cost_ = 30.0f;
        radius_ = 4.5f; damage_ = 0.8f; fxKind_ = 10;   // lightning-yellow ring
        icon_ = AbilityIcon::Shockwave; iconColor_ = {240, 205, 90};
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
        icon_ = AbilityIcon::Holy; iconColor_ = {255, 225, 130};
    }
};

class GreaterHealAbility : public Ability {
public:
    GreaterHealAbility() {
        id_ = AbilityId::GreaterHeal; name_ = "Greater Heal"; role_ = PlayerRole::Healer;
        desc_ = "A powerful wave of restoration to you and your allies.";
        kind_ = AbilityKind::Heal; cooldown_ = 8.0f; cost_ = 45.0f; castTime_ = 1.2f;
        icon_ = AbilityIcon::Cross; iconColor_ = {150, 245, 160};
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
        castChainHeal(ctx, 45.0f * ctx.buffedAbilityPower());
    }
};

class WhirlwindAbility : public AoeAbility {
public:
    WhirlwindAbility() {
        id_ = AbilityId::Whirlwind; name_ = "Whirlwind"; role_ = PlayerRole::DPS;
        desc_ = "Spin with blades extended, hitting everything around you.";
        kind_ = AbilityKind::Aoe; cooldown_ = 8.0f; cost_ = 40.0f;
        radius_ = 4.0f; damage_ = 1.1f; fxKind_ = 2;
        icon_ = AbilityIcon::Whirl; iconColor_ = {235, 110, 80};
    }
    glm::vec3 aoeCenter(AppContext& ctx) const override { return ctx.camera.position; }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) ctx.playerRig->playClip(ClipKind::Spin, 0.7f);
        AoeAbility::activate(ctx);
    }
};

class PiercingShotAbility : public RangedAbility {
public:
    PiercingShotAbility() {
        id_ = AbilityId::PiercingShot; name_ = "Piercing Shot"; role_ = PlayerRole::DPS;
        desc_ = "A bolt that pierces through enemies in a line.";
        kind_ = AbilityKind::Ranged; cooldown_ = 5.0f; cost_ = 30.0f;
        range_ = 28.0f; facing_ = 0.95f; damage_ = 1.4f; speed_ = 48.0f;
        icon_ = AbilityIcon::Arrow; iconColor_ = {190, 150, 255};
    }
    void spawnProjectile(AppContext& ctx, float damageScale) override {
        launchBolt<ArcaneBoltProjectile>(ctx, speed_, damageScale);  // pierces on NPC hit
    }
};

// === Second-wave unlocks ===================================================
// Each is, by design, only a few lines over an existing core — picking a target
// shape, an element colour (fxKind), a glyph, and occasionally a signature
// one-shot animation clip.

// --- Tank ---
class SlamAbility : public AoeAbility {
public:
    SlamAbility() {
        id_ = AbilityId::Slam; name_ = "Slam"; role_ = PlayerRole::Tank;
        desc_ = "Drive your weapon into the earth, quaking the ground in front of you.";
        kind_ = AbilityKind::Aoe; cooldown_ = 7.0f; cost_ = 25.0f;
        radius_ = 3.8f; damage_ = 1.6f; fxKind_ = 6;     // earth burst
        icon_ = AbilityIcon::Hammer; iconColor_ = {200, 150, 95};
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) ctx.playerRig->playClip(ClipKind::Slam, 0.55f);
        AoeAbility::activate(ctx);
    }
};

class BattleShoutAbility : public BuffAbility {
public:
    BattleShoutAbility() {
        id_ = AbilityId::BattleShout; name_ = "Battle Shout"; role_ = PlayerRole::Tank;
        desc_ = "A rallying war cry that sharply raises your attack power for a while.";
        kind_ = AbilityKind::Buff; cooldown_ = 16.0f; cost_ = 25.0f;
        buffKind_ = BuffKind::Power; magnitude_ = 0.5f; duration_ = 10.0f;
        fxKind_ = 12; fxRadius_ = 7.0f;                  // gold roar ring
        icon_ = AbilityIcon::Chevrons; iconColor_ = {255, 205, 90};
    }
};

class LastStandAbility : public BuffAbility {
public:
    LastStandAbility() {
        id_ = AbilityId::LastStand; name_ = "Last Stand"; role_ = PlayerRole::Tank;
        desc_ = "Plant your feet and weather the storm, greatly raising defence.";
        kind_ = AbilityKind::Buff; cooldown_ = 24.0f; cost_ = 35.0f; castTime_ = 0.6f;
        buffKind_ = BuffKind::Defense; magnitude_ = 1.4f; duration_ = 8.0f;
        fxKind_ = 13; fxRadius_ = 2.7f;                  // big steel shield dome
        icon_ = AbilityIcon::Shield; iconColor_ = {90, 210, 210};
    }
};

// --- Healer ---
class RenewAbility : public Ability {
public:
    RenewAbility() {
        id_ = AbilityId::Renew; name_ = "Renew"; role_ = PlayerRole::Healer;
        desc_ = "Wreathe the ground beneath you in renewing light that heals over time.";
        kind_ = AbilityKind::Heal; cooldown_ = 6.0f; cost_ = 20.0f;
        icon_ = AbilityIcon::Leaf; iconColor_ = {140, 225, 110};
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
        castHealZone(ctx, 8.0f * ctx.buffedAbilityPower(), 3.0f, 5.0f);   // small, quick
        spawnAbilityFx(ctx, ctx.camera.position, 2.0f, 7);               // nature motes
    }
};

class HolyNovaAbility : public AoeAbility {
public:
    HolyNovaAbility() {
        id_ = AbilityId::HolyNova; name_ = "Holy Nova"; role_ = PlayerRole::Healer;
        desc_ = "Erupt with holy light, searing nearby foes and mending nearby allies.";
        kind_ = AbilityKind::Aoe; cooldown_ = 12.0f; cost_ = 40.0f;
        radius_ = 5.0f; damage_ = 0.9f; fxKind_ = 8;     // holy burst
        icon_ = AbilityIcon::Nova; iconColor_ = {255, 240, 190};
    }
    glm::vec3 aoeCenter(AppContext& ctx) const override { return ctx.camera.position; }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) ctx.playerRig->playClip(ClipKind::Spin, 0.7f);
        AoeAbility::activate(ctx);                            // damage foes + holy fx
        castChainHeal(ctx, 18.0f * ctx.buffedAbilityPower()); // mend allies
    }
};

class BarrierAbility : public BuffAbility {
public:
    BarrierAbility() {
        id_ = AbilityId::Barrier; name_ = "Barrier"; role_ = PlayerRole::Healer;
        desc_ = "Conjure a shimmering barrier of light that absorbs incoming harm.";
        kind_ = AbilityKind::Buff; cooldown_ = 14.0f; cost_ = 30.0f;
        buffKind_ = BuffKind::Defense; magnitude_ = 0.7f; duration_ = 8.0f;
        fxKind_ = 13; fxRadius_ = 2.1f;   // shimmering shield dome
        icon_ = AbilityIcon::Shield; iconColor_ = {245, 225, 150};
    }
};

// --- DPS ---
class FrostboltAbility : public RangedAbility {
public:
    FrostboltAbility() {
        id_ = AbilityId::Frostbolt; name_ = "Frostbolt"; role_ = PlayerRole::DPS;
        desc_ = "Hurl a shard of ice that chills whatever it strikes.";
        kind_ = AbilityKind::Ranged; cooldown_ = 3.0f; cost_ = 20.0f;
        range_ = 26.0f; facing_ = 0.93f; damage_ = 1.3f; speed_ = 40.0f;
        icon_ = AbilityIcon::Frost; iconColor_ = {150, 210, 245};
    }
    void spawnProjectile(AppContext& ctx, float damageScale) override {
        launchBolt<IceBoltProjectile>(ctx, speed_, damageScale);
    }
};

class InfernoAbility : public AoeAbility {
public:
    InfernoAbility() {
        id_ = AbilityId::Inferno; name_ = "Inferno"; role_ = PlayerRole::DPS;
        desc_ = "Engulf the ground you aim at in a roaring firestorm.";
        kind_ = AbilityKind::Aoe; cooldown_ = 10.0f; cost_ = 45.0f;
        radius_ = 4.5f; damage_ = 1.4f; fxKind_ = 11;    // inferno burst
        icon_ = AbilityIcon::Flame; iconColor_ = {255, 90, 40};
    }
};

class RendAbility : public MeleeAbility {
public:
    RendAbility() {
        id_ = AbilityId::Rend; name_ = "Rend"; role_ = PlayerRole::DPS;
        desc_ = "A vicious strike that tears deep into a single foe.";
        kind_ = AbilityKind::Melee; cooldown_ = 5.0f; cost_ = 25.0f;
        range_ = 3.6f; facing_ = 0.32f; damage_ = 2.0f;
        icon_ = AbilityIcon::Claw; iconColor_ = {210, 60, 55};
    }
};

// === Tier-3 capstones ======================================================
// Bigger, costlier versions of the second-wave abilities, bought at the top of
// each role's tree. Each is still only a few lines over an existing core.

// --- Tank ---
class EarthshatterAbility : public AoeAbility {
public:
    EarthshatterAbility() {
        id_ = AbilityId::Earthshatter; name_ = "Earthshatter"; role_ = PlayerRole::Tank;
        desc_ = "Rend the earth in a wide quake, battering everything around you.";
        kind_ = AbilityKind::Aoe; cooldown_ = 14.0f; cost_ = 45.0f;
        radius_ = 5.0f; damage_ = 1.8f; fxKind_ = 10;
        icon_ = AbilityIcon::Hammer; iconColor_ = {185, 140, 90};
    }
    glm::vec3 aoeCenter(AppContext& ctx) const override { return ctx.camera.position; }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) ctx.playerRig->playClip(ClipKind::Slam, 0.6f);
        AoeAbility::activate(ctx);
    }
};

class AvatarAbility : public BuffAbility {
public:
    AvatarAbility() {
        id_ = AbilityId::Avatar; name_ = "Avatar"; role_ = PlayerRole::Tank;
        desc_ = "Become an avatar of war — greatly raises your attack power.";
        kind_ = AbilityKind::Buff; cooldown_ = 30.0f; cost_ = 40.0f;
        buffKind_ = BuffKind::Power; magnitude_ = 0.7f; duration_ = 12.0f;
        fxKind_ = 14; fxRadius_ = 7.0f;
        icon_ = AbilityIcon::Nova; iconColor_ = {255, 180, 80};
    }
};

class BulwarkAbility : public BuffAbility {
public:
    BulwarkAbility() {
        id_ = AbilityId::Bulwark; name_ = "Bulwark"; role_ = PlayerRole::Tank;
        desc_ = "An unbreakable wall — massively raises defence for a time.";
        kind_ = AbilityKind::Buff; cooldown_ = 28.0f; cost_ = 45.0f; castTime_ = 0.5f;
        buffKind_ = BuffKind::Defense; magnitude_ = 1.8f; duration_ = 10.0f;
        fxKind_ = 13; fxRadius_ = 3.0f;
        icon_ = AbilityIcon::Shield; iconColor_ = {120, 220, 230};
    }
};

// --- Healer ---
class DivineStormAbility : public AoeAbility {
public:
    DivineStormAbility() {
        id_ = AbilityId::DivineStorm; name_ = "Divine Storm"; role_ = PlayerRole::Healer;
        desc_ = "A storm of holy light that sears your foes and mends your allies.";
        kind_ = AbilityKind::Aoe; cooldown_ = 16.0f; cost_ = 55.0f;
        radius_ = 6.0f; damage_ = 1.1f; fxKind_ = 8;
        icon_ = AbilityIcon::Holy; iconColor_ = {255, 245, 200};
    }
    glm::vec3 aoeCenter(AppContext& ctx) const override { return ctx.camera.position; }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) ctx.playerRig->playClip(ClipKind::Spin, 0.7f);
        AoeAbility::activate(ctx);                            // damage foes + holy fx
        castChainHeal(ctx, 28.0f * ctx.buffedAbilityPower()); // mend allies
    }
};

class TranquilityAbility : public Ability {
public:
    TranquilityAbility() {
        id_ = AbilityId::Tranquility; name_ = "Tranquility"; role_ = PlayerRole::Healer;
        desc_ = "Bathe a wide area in serene light, healing all who linger within.";
        kind_ = AbilityKind::Heal; cooldown_ = 20.0f; cost_ = 60.0f; castTime_ = 1.0f;
        icon_ = AbilityIcon::Sanctuary; iconColor_ = {150, 235, 140};
    }
    void activate(AppContext& ctx) override {
        if (ctx.playerRig) { ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f; }
        castHealZone(ctx, 20.0f * ctx.buffedAbilityPower(), 5.0f, 8.0f);
        spawnAbilityFx(ctx, ctx.camera.position, 3.0f, 7);
    }
};

class GuardianSpiritAbility : public BuffAbility {
public:
    GuardianSpiritAbility() {
        id_ = AbilityId::GuardianSpirit; name_ = "Guardian Spirit"; role_ = PlayerRole::Healer;
        desc_ = "Call a watchful spirit that shields you from grievous harm.";
        kind_ = AbilityKind::Buff; cooldown_ = 26.0f; cost_ = 45.0f;
        buffKind_ = BuffKind::Defense; magnitude_ = 1.0f; duration_ = 10.0f;
        fxKind_ = 13; fxRadius_ = 2.4f;
        icon_ = AbilityIcon::Cross; iconColor_ = {245, 235, 180};
    }
};

// --- DPS ---
class MeteorAbility : public AoeAbility {
public:
    MeteorAbility() {
        id_ = AbilityId::Meteor; name_ = "Meteor"; role_ = PlayerRole::DPS;
        desc_ = "Call a blazing meteor down on the ground you aim at.";
        kind_ = AbilityKind::Aoe; cooldown_ = 14.0f; cost_ = 60.0f;
        radius_ = 5.5f; damage_ = 1.8f; fxKind_ = 11;
        icon_ = AbilityIcon::Flame; iconColor_ = {255, 80, 30};
    }
};

class GlacialSpikeAbility : public RangedAbility {
public:
    GlacialSpikeAbility() {
        id_ = AbilityId::GlacialSpike; name_ = "Glacial Spike"; role_ = PlayerRole::DPS;
        desc_ = "Launch a massive spike of ice that shatters on impact.";
        kind_ = AbilityKind::Ranged; cooldown_ = 6.0f; cost_ = 35.0f;
        range_ = 28.0f; facing_ = 0.94f; damage_ = 2.0f; speed_ = 44.0f;
        icon_ = AbilityIcon::Frost; iconColor_ = {170, 225, 250};
    }
    void spawnProjectile(AppContext& ctx, float damageScale) override {
        launchBolt<IceBoltProjectile>(ctx, speed_, damageScale);
    }
};

class ExecuteAbility : public MeleeAbility {
public:
    ExecuteAbility() {
        id_ = AbilityId::Execute; name_ = "Execute"; role_ = PlayerRole::DPS;
        desc_ = "A brutal finishing blow that lands with devastating force.";
        kind_ = AbilityKind::Melee; cooldown_ = 8.0f; cost_ = 40.0f;
        range_ = 3.6f; facing_ = 0.30f; damage_ = 2.8f;
        icon_ = AbilityIcon::Claw; iconColor_ = {220, 50, 50};
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
        case AbilityId::Slam:             return std::make_unique<SlamAbility>();
        case AbilityId::BattleShout:      return std::make_unique<BattleShoutAbility>();
        case AbilityId::LastStand:        return std::make_unique<LastStandAbility>();
        case AbilityId::Renew:            return std::make_unique<RenewAbility>();
        case AbilityId::HolyNova:         return std::make_unique<HolyNovaAbility>();
        case AbilityId::Barrier:          return std::make_unique<BarrierAbility>();
        case AbilityId::Frostbolt:        return std::make_unique<FrostboltAbility>();
        case AbilityId::Inferno:          return std::make_unique<InfernoAbility>();
        case AbilityId::Rend:             return std::make_unique<RendAbility>();
        case AbilityId::Earthshatter:     return std::make_unique<EarthshatterAbility>();
        case AbilityId::Avatar:           return std::make_unique<AvatarAbility>();
        case AbilityId::Bulwark:          return std::make_unique<BulwarkAbility>();
        case AbilityId::DivineStorm:      return std::make_unique<DivineStormAbility>();
        case AbilityId::Tranquility:      return std::make_unique<TranquilityAbility>();
        case AbilityId::GuardianSpirit:   return std::make_unique<GuardianSpiritAbility>();
        case AbilityId::Meteor:           return std::make_unique<MeteorAbility>();
        case AbilityId::GlacialSpike:     return std::make_unique<GlacialSpikeAbility>();
        case AbilityId::Execute:          return std::make_unique<ExecuteAbility>();
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
