#pragma once
#include "role.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

struct AppContext;

// ---------------------------------------------------------------------------
// Ability — the polymorphic skill / spell hierarchy.
// ---------------------------------------------------------------------------
// Mirrors the Item / Projectile pattern: a base class with a single virtual
// entry point (activate), intermediate bases that hold the reusable melee /
// ranged / AoE / buff "core", and concrete abilities that subclass one of them
// and override slightly. Concrete classes live in ability.cpp and are built via
// createAbility() — call sites never switch on the id.

enum class AbilityId : uint16_t {
    None = 0,
    Taunt,             // Tank   — pull NPC aggro (server-resolved)
    ShieldUp,          // Tank   — temporary defence buff
    ChainHeal,         // Healer — bouncing heal (reuses castChainHeal)
    HealingSanctuary,  // Healer — AoE heal-over-time zone (reuses castHealZone)
    Flurry,            // DPS    — rapid multi-hit melee
    Cleave,            // DPS    — melee AoE
    Fireball,          // DPS    — ranged bolt
    // --- skill-tree unlocks (Phase 3) ---
    ShieldBash,        // Tank   — heavy single-target melee
    ThunderClap,       // Tank   — melee AoE around self
    Smite,             // Healer — holy ranged bolt
    GreaterHeal,       // Healer — stronger chain heal
    Whirlwind,         // DPS    — wide melee AoE around self
    PiercingShot,      // DPS    — piercing ranged bolt
    // --- second wave of unlocks ---
    Slam,              // Tank   — heavy frontal earth slam (AoE)
    BattleShout,       // Tank   — party-style attack-power buff
    LastStand,         // Tank   — strong, longer defence buff
    Renew,             // Healer — quick self heal-over-time zone
    HolyNova,          // Healer — burst that damages foes + heals allies
    Barrier,           // Healer — defensive absorb buff
    Frostbolt,         // DPS    — ranged ice bolt
    Inferno,           // DPS    — aimed fire AoE
    Rend,              // DPS    — heavy single-target melee
    // --- tier-3 capstones ---
    Earthshatter,      // Tank   — wide ground quake (AoE)
    Avatar,            // Tank   — strong attack-power buff
    Bulwark,           // Tank   — top-tier defence buff
    DivineStorm,       // Healer — Holy Nova capstone (damage + heal)
    Tranquility,       // Healer — strong heal-over-time zone
    GuardianSpirit,    // Healer — emergency defence buff
    Meteor,            // DPS    — large fire AoE
    GlacialSpike,      // DPS    — heavy ice bolt
    Execute,           // DPS    — massive single-target melee
    // Append-only: ids are serialised into skill-tree progress (and the
    // character-save unlock bitmask — keep the ordinal count under 64).
};

enum class AbilityKind  : uint8_t { Melee, Ranged, Aoe, Buff, Heal };
enum class ResourceType : uint8_t { Mana, Energy, Rage };

// A timed buff applied to the local player by a BuffAbility. Kept light — the
// magnitude feeds the existing combat multipliers while ttl > 0.
//  Defense → added to defenceMult (less damage taken)
//  Power   → added to abilityPowerMult (more outgoing damage), via
//            AppContext::buffedAbilityPower()
enum class BuffKind : uint8_t { Defense, Power };

// A small RGB tint for an ability's hotbar / skill-tree glyph. Kept as a plain
// trio (not Voxel) so ability.h needn't pull in the GL-heavy voxel headers.
struct IconColor { uint8_t r, g, b; };

// The procedurally-drawn glyph shown for an ability on the hotbar and in the
// skill tree. Each value maps to a shape drawn by drawAbilityIcon() (ui_play.cpp)
// from ImGui draw-list primitives — no texture assets. Append-only.
enum class AbilityIcon : uint8_t {
    Sword,       // a single diagonal blade
    Swords,      // two crossed blades
    Slash,       // a sweeping crescent arc
    Whirl,       // a spiral / cyclone
    Hammer,      // a maul head on a haft
    Shield,      // a heater shield
    ShieldBash,  // shield with an impact spark
    Chevrons,    // three stacked upward chevrons (a shout)
    Shockwave,   // concentric expanding rings
    Flame,       // a teardrop flame
    Frost,       // a six-spoke snowflake
    Holy,        // a radiant sun-burst
    Nova,        // a filled multi-point star
    Cross,       // a thick plus (a heal)
    Sanctuary,   // a plus inside a ring
    Leaf,        // a leaf
    Arrow,       // an upward arrow
    Claw,        // three raking claw marks
};
struct ActiveBuff {
    AbilityId id        = AbilityId::None;
    BuffKind  kind      = BuffKind::Defense;
    float     magnitude = 0.0f;   // added to the relevant multiplier while active
    float     ttl       = 0.0f;   // seconds remaining
    float     total     = 0.0f;   // full duration, for the HUD countdown bar
};

class Ability {
public:
    virtual ~Ability() = default;

    AbilityId   id()           const { return id_; }
    const char* name()         const { return name_; }
    const char* description()  const { return desc_; }
    AbilityKind kind()         const { return kind_; }
    PlayerRole  role()         const { return role_; }
    float       cooldown()     const { return cooldown_; }
    float       resourceCost() const { return cost_; }
    // Seconds the player must channel before the effect fires (0 = instant).
    // While casting the player moves slowly; the effect happens on completion.
    float       castTime()     const { return castTime_; }
    // Procedural glyph + tint for the hotbar / skill-tree icon.
    AbilityIcon icon()         const { return icon_; }
    IconColor   iconColor()    const { return iconColor_; }

    // The single override point. The hotbar harness has already checked and
    // spent the cooldown + resource; this just performs the effect: pick a
    // target, send the authoritative packet(s), spawn cosmetics.
    virtual void activate(AppContext& ctx) = 0;

protected:
    AbilityId   id_       = AbilityId::None;
    const char* name_     = "";
    const char* desc_     = "";
    AbilityKind kind_     = AbilityKind::Melee;
    PlayerRole  role_     = PlayerRole::DPS;
    float       cooldown_ = 1.0f;
    float       cost_     = 0.0f;
    float       castTime_ = 0.0f;
    AbilityIcon icon_      = AbilityIcon::Sword;
    IconColor   iconColor_ = {220, 220, 230};
};

// --- Reusable "core" intermediate bases ------------------------------------

// Single-target melee: find the best NPC in a frontal cone and send one
// authoritative PlayerAttack scaled by ability power. Subclasses tweak the
// range / damage, or call swing() several times (Flurry).
class MeleeAbility : public Ability {
public:
    void activate(AppContext& ctx) override;
protected:
    void  swing(AppContext& ctx, float damageScale);
    float range_  = 3.8f;
    float facing_ = 0.30f;
    float damage_ = 1.0f;
};

// Single-target ranged: spawn a cosmetic projectile toward the aim point and
// send the authoritative PlayerAttack. Subclasses override spawnProjectile to
// pick the bolt type / speed.
class RangedAbility : public Ability {
public:
    void activate(AppContext& ctx) override;
protected:
    virtual void spawnProjectile(AppContext& ctx, float damageScale);
    float range_  = 26.0f;
    float facing_ = 0.93f;
    float damage_ = 1.0f;
    float speed_  = 34.0f;
};

// Area effect: resolve damage to NPCs in a radius server-side (AbilityCast) and
// spawn local + spectator cosmetics. Subclasses choose the centre + visuals.
class AoeAbility : public Ability {
public:
    void activate(AppContext& ctx) override;
protected:
    virtual glm::vec3 aoeCenter(AppContext& ctx) const;   // default: just ahead at feet
    float   radius_ = 4.0f;
    float   damage_ = 1.0f;
    uint8_t fxKind_ = 2;   // SpellEffectPacket.kind for the spectator burst
};

// Self buff: push an ActiveBuff onto the player for `duration_`. Subclasses set
// the kind / magnitude (ShieldUp → defence).
class BuffAbility : public Ability {
public:
    void activate(AppContext& ctx) override;
protected:
    BuffKind buffKind_  = BuffKind::Defense;
    float    magnitude_ = 0.5f;
    float    duration_  = 6.0f;
    uint8_t  fxKind_    = 5;      // SpellEffectPacket.kind for the aura burst
    float    fxRadius_  = 1.5f;   // burst radius (a shout uses a big ring)
};

// --- Factory + role kits ----------------------------------------------------
// The ONLY id -> concrete-class map (mirrors createWeaponItem).
std::unique_ptr<Ability> createAbility(AbilityId id);

// The abilities a freshly created character of `role` starts with. In Phase 3
// the skill tree splits these into pre-unlocked "core" plus point-bought nodes.
const std::vector<AbilityId>& roleStartingAbilities(PlayerRole role);

const char* resourceName(ResourceType r);
