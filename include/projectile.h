#pragma once
#include "game_object.h"
#include "voxel_model.h"
#include <cstdint>

// Generic flying-object base class. Owns its mesh, simulates gravity,
// collides with terrain, and exposes virtual hooks so weapon-specific
// subclasses can react (apply damage, splash, leave a particle trail).
//
// Design intent: anything thrown / fired / launched should subclass
// Projectile. Arrows are first, but the structure leaves room for
// thrown axes (rotate the mesh, deeper damage), staff bolts (no
// gravity, particle trail), bombs (timed detonation), etc.
class Projectile : public GameObject {
public:
    Projectile();
    ~Projectile() override;

    void update(float dt, World& world) override;
    void draw(GLuint modelLoc) const override;
    void getAABB(glm::vec3& mn, glm::vec3& mx) const override;

    // Physics
    glm::vec3 velocity = glm::vec3(0.0f);
    float     gravity  = 12.0f;     // m/s^2 downward; subclasses can zero this
    float     drag     = 0.02f;     // simple per-second drag coefficient
    float     lifeTime = 5.0f;
    float     age      = 0.0f;
    bool      grounded = false;

    // Visual
    VoxelVolume* mesh        = nullptr;
    bool         ownsMesh    = true;
    float        meshScale   = 0.05f;
    // Orientation locked in once the projectile grounds — needed because
    // a stationary projectile has no velocity vector to align to.
    glm::vec3    restingDir  = glm::vec3(0.0f, 0.0f, 1.0f);

    // Attribution + damage info passed through onHitNpc().
    uint32_t ownerClientId = 0;
    float    damageScale   = 1.0f;

    // Subclass hooks — default impls do nothing. ArrowProjectile uses
    // these to add a brief "stuck" lifetime and to mark the projectile
    // dead the moment it touches an NPC. Future projectile types might
    // explode, bounce, or apply DoT.
    virtual void onHitGround()              {}
    virtual void onHitNpc(class NPC& /*n*/) {}

protected:
    // Build the world matrix that orients the mesh's +Y axis along the
    // current flight direction (or `restingDir` once grounded).
    glm::mat4 orientedMatrix() const;
};

// A bow's arrow. Pure-cosmetic projectile — damage is already applied to
// the target NPC via PlayerAttackPacket the moment the player releases
// the bowstring, so the arrow's job is just to look right flying through
// the air. When it brushes an NPC we mark it dead so it visibly stops in
// the body, which sells the hit.
class ArrowProjectile : public Projectile {
public:
    ArrowProjectile();

    void onHitGround() override;
    void onHitNpc(class NPC& n) override;

    // Built-in arrow mesh — shared static voxel volume isn't safe
    // because each projectile owns/disposes its own. Each ArrowProjectile
    // builds a private 1x12x3 voxel arrow (shaft + fletching + head) in
    // its constructor.
};

// A staff's magic bolt. Polymorphic-pair to the arrow but with a
// completely different feel: flies in a straight line (no gravity),
// faster than an arrow, leaves a brief glowing voxel trail behind. The
// mesh is a small bright orb tinted by the staff's own colours so a
// fire-staff fires red bolts and an ice-staff fires blue ones.
//
// This is the generic base for caster projectiles. Elemental subclasses
// (FireBolt / IceBolt / ArcaneBolt) tweak speed, size, colours and
// per-element behaviour (e.g. AOE burst on fire impact).
class MagicBoltProjectile : public Projectile {
public:
    MagicBoltProjectile();

    void update(float dt, class World& world) override;
    void onHitGround() override;
    void onHitNpc(class NPC& n) override;

    // Tint the orb. Builds a fresh small mesh — call once at spawn
    // time, not every frame.
    void setTint(Voxel primary, Voxel accent);

    // The element decides which trail colours and impact effect the
    // collision pass applies. None = treat as a generic glowing bolt.
    enum class Element { None, Fire, Ice, Arcane };
    Element boltElement = Element::None;

    // Trail colour samples. Filled by the constructor for each
    // element subclass; the gameplay-side collision pass reads these
    // and pushes voxel particles behind the bolt every frame.
    Voxel trailColorA = {255, 255, 255, 255};
    Voxel trailColorB = {200, 200, 200, 255};
    // Accumulator owned by the trail-spawn pass. Public so the free
    // function `updateProjectileCollisions` can drive it without
    // needing friend declarations.
    float trailTimer = 0.0f;

protected:
    Voxel boltCore = {220, 230, 255, 255};
    Voxel boltEdge = { 90, 160, 255, 255};
};

// Fire bolt: slower, larger, hotter colour. On impact subclass-specific
// hook spawns a small voxel burst (handled in gameplay.cpp).
class FireBoltProjectile : public MagicBoltProjectile {
public:
    FireBoltProjectile();
    void onHitGround() override;
    void onHitNpc(class NPC& n) override;
};

// Ice bolt: faster, smaller, frosty trail.
class IceBoltProjectile : public MagicBoltProjectile {
public:
    IceBoltProjectile();
};

// Arcane bolt: fastest, thinnest, pierces NPCs (doesn't despawn on
// first contact — keeps going through and can hit multiple targets).
class ArcaneBoltProjectile : public MagicBoltProjectile {
public:
    ArcaneBoltProjectile();
    void onHitNpc(class NPC& n) override;
};

// Per-frame pass: walk the ObjectManager and check every Projectile
// against nearby NPCs. Hit detection lives outside the projectile's own
// update because GameObject::update() doesn't get the ObjectManager —
// this keeps the projectile class self-contained while still allowing
// it to react via the virtual onHitNpc() hook.
class AppContext;
void updateProjectileCollisions(AppContext& ctx);
