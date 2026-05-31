#include "projectile.h"
#include "app_context.h"
#include "world.h"
#include "npc.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Projectile (base)
// ---------------------------------------------------------------------------

Projectile::Projectile() : GameObject(ObjectKind::Projectile) {}

Projectile::~Projectile() {
    if (ownsMesh && mesh) delete mesh;
}

void Projectile::update(float dt, World& world) {
    age += dt;
    if (age >= lifeTime) { dead = true; return; }
    if (grounded) return;

    // Light air drag — stops arrows from drifting forever at high charge.
    if (drag > 0.0f) {
        float k = 1.0f - std::min(0.99f, drag * dt);
        velocity *= k;
    }
    velocity.y -= gravity * dt;
    glm::vec3 next = position + velocity * dt;

    // Terrain collision — treat any non-Air, non-Water block as solid.
    int bx = (int)std::floor(next.x);
    int by = (int)std::floor(next.y);
    int bz = (int)std::floor(next.z);
    BlockType b = world.getBlock(bx, by, bz);
    if (b != BlockType::Air && b != BlockType::Water) {
        // Lock orientation to direction of motion at impact so the
        // mesh appears "stuck in the wall" pointing the right way.
        if (glm::length(velocity) > 0.01f)
            restingDir = glm::normalize(velocity);
        position = next;
        velocity = glm::vec3(0.0f);
        grounded = true;
        // Once grounded, shorten remaining lifetime so we don't litter.
        lifeTime = std::min(lifeTime, age + 4.0f);
        onHitGround();
        return;
    }

    position = next;
}

void Projectile::draw(GLuint modelLoc) const {
    if (!mesh) return;
    glm::mat4 m = orientedMatrix();
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &m[0][0]);
    mesh->draw();
}

void Projectile::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    const float r = 0.15f;
    mn = position + glm::vec3(-r, -r, -r);
    mx = position + glm::vec3( r,  r,  r);
}

glm::mat4 Projectile::orientedMatrix() const {
    // Choose the direction to align the mesh's +Y to.
    glm::vec3 dir = grounded ? restingDir
                  : (glm::length(velocity) > 0.01f
                        ? glm::normalize(velocity)
                        : restingDir);

    // Build an orthonormal basis with `dir` as the Y axis. Pick an `up`
    // hint that isn't parallel to `dir` to avoid degeneracy.
    glm::vec3 upHint = (std::abs(dir.y) > 0.95f)
                         ? glm::vec3(0.0f, 0.0f, 1.0f)
                         : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right  = glm::normalize(glm::cross(upHint, dir));
    glm::vec3 up     = glm::cross(dir, right);

    glm::mat4 rot(1.0f);
    rot[0] = glm::vec4(right, 0.0f);
    rot[1] = glm::vec4(dir,   0.0f);
    rot[2] = glm::vec4(up,    0.0f);

    glm::mat4 m = glm::translate(glm::mat4(1.0f), position) * rot;
    m = glm::scale(m, glm::vec3(meshScale));
    // Centre the mesh on its long axis (Y) — atlas builds the arrow
    // at y=0..12 with the tip at the top.
    if (mesh)
        m = glm::translate(m, glm::vec3(-mesh->sizeX * 0.5f,
                                         0.0f,
                                        -mesh->sizeZ * 0.5f));
    return m;
}

// ---------------------------------------------------------------------------
// ArrowProjectile
// ---------------------------------------------------------------------------

static VoxelVolume* buildArrowMesh() {
    // 3x14x3 — narrow oak shaft, flint head, two-feather fletching at
    // the base. Built fresh per arrow so each one can be disposed by
    // the base destructor without sharing state.
    VoxelVolume* v = new VoxelVolume(3, 14, 3);
    Voxel shaft   = {130,  90,  55, 255};
    Voxel head    = {180, 180, 195, 255};
    Voxel headTip = {200, 200, 210, 255};
    Voxel feather = {220, 220, 220, 255};
    // Shaft along the middle column.
    for (int y = 2; y < 11; y++)
        v->setVoxel(1, y, 1, shaft);
    // Head — a narrow triangle at the tip.
    v->setVoxel(1, 11, 1, head);
    v->setVoxel(1, 12, 1, head);
    v->setVoxel(1, 13, 1, headTip);
    // Fletching — two feathers at the back.
    v->setVoxel(0, 1, 1, feather);
    v->setVoxel(2, 1, 1, feather);
    v->setVoxel(0, 2, 1, feather);
    v->setVoxel(2, 2, 1, feather);
    v->setVoxel(1, 1, 0, feather);
    v->setVoxel(1, 1, 2, feather);
    v->updateMesh();
    return v;
}

ArrowProjectile::ArrowProjectile() {
    mesh      = buildArrowMesh();
    ownsMesh  = true;
    meshScale = 0.08f;
    gravity   = 9.5f;
    drag      = 0.04f;
    lifeTime  = 6.0f;
}

void ArrowProjectile::onHitGround() {
    // Default base behaviour already shortens the lifetime to ~4s after
    // grounding; nothing extra needed yet, but the hook is here so it
    // can grow into "stuck arrow" pickup later.
}

void ArrowProjectile::onHitNpc(NPC& /*n*/) {
    // Damage was already applied server-side via PlayerAttackPacket;
    // here we just visibly stop the arrow on impact. Marking dead
    // removes the visual without lingering shaft inside the body.
    dead = true;
}

// ---------------------------------------------------------------------------
// MagicBoltProjectile
// ---------------------------------------------------------------------------

static VoxelVolume* buildMagicBoltMesh(Voxel core, Voxel edge) {
    // 5x5x5 orb — bright core voxel surrounded by a one-voxel
    // tinted shell. Small enough to read as "a glowing bolt", big
    // enough to spot at speed.
    VoxelVolume* v = new VoxelVolume(5, 5, 5);
    for (int x = 0; x < 5; x++)
        for (int y = 0; y < 5; y++)
            for (int z = 0; z < 5; z++) {
                float dx = x - 2.0f, dy = y - 2.0f, dz = z - 2.0f;
                float r2 = dx * dx + dy * dy + dz * dz;
                if (r2 <= 1.5f)       v->setVoxel(x, y, z, core);
                else if (r2 <= 4.5f)  v->setVoxel(x, y, z, edge);
            }
    v->updateMesh();
    return v;
}

MagicBoltProjectile::MagicBoltProjectile() {
    mesh      = buildMagicBoltMesh(boltCore, boltEdge);
    ownsMesh  = true;
    meshScale = 0.12f;
    // Magic flies straight — no gravity, very low drag.
    gravity   = 0.0f;
    drag      = 0.005f;
    lifeTime  = 3.0f;
}

void MagicBoltProjectile::setTint(Voxel primary, Voxel accent) {
    boltCore = primary;
    boltEdge = accent;
    if (ownsMesh && mesh) { delete mesh; mesh = nullptr; }
    mesh = buildMagicBoltMesh(boltCore, boltEdge);
}

void MagicBoltProjectile::update(float dt, World& world) {
    Projectile::update(dt, world);
    if (dead) return;
    // Glowing trail — spawn a small fading voxel ~30 times per second
    // along the bolt's path. We piggy-back on the existing voxel-death
    // particle system so the renderer needs no changes. We can't reach
    // ctx from here, so we just leave the particle spawn to a
    // surrounding update pass if anyone wants to wire it (not
    // implemented yet — see TODO in gameplay.cpp).
    trailTimer += dt;
    if (trailTimer > 0.033f) trailTimer = 0.033f;
}

void MagicBoltProjectile::onHitGround() {
    // Magic dissipates immediately on contact — no stuck-bolt visual.
    dead = true;
}

void MagicBoltProjectile::onHitNpc(NPC& /*n*/) {
    dead = true;
}

// ---------------------------------------------------------------------------
// FireBoltProjectile — slow, big, leaves embers, bursts on impact.
// ---------------------------------------------------------------------------

FireBoltProjectile::FireBoltProjectile() {
    boltElement  = Element::Fire;
    boltCore     = {255, 230, 120, 255};   // hot yellow centre
    boltEdge     = {220,  70,  30, 255};   // orange shell
    trailColorA  = {255, 180,  60, 255};
    trailColorB  = {200,  40,  20, 255};
    if (ownsMesh && mesh) { delete mesh; mesh = nullptr; }
    setTint(boltCore, boltEdge);
    meshScale = 0.16f;   // larger than the base bolt
    lifeTime  = 2.6f;
}

void FireBoltProjectile::onHitGround() {
    // Burst on impact — the actual particle spawn happens in
    // gameplay.cpp's collision pass, which can reach ctx. Here we just
    // flag dead and let the post-pass see the element + position.
    dead = true;
}

void FireBoltProjectile::onHitNpc(NPC& /*n*/) {
    dead = true;
}

// ---------------------------------------------------------------------------
// IceBoltProjectile — fast, small, frosty trail.
// ---------------------------------------------------------------------------

IceBoltProjectile::IceBoltProjectile() {
    boltElement  = Element::Ice;
    boltCore     = {235, 250, 255, 255};
    boltEdge     = { 90, 170, 240, 255};
    trailColorA  = {210, 240, 255, 255};
    trailColorB  = {120, 180, 230, 255};
    if (ownsMesh && mesh) { delete mesh; mesh = nullptr; }
    setTint(boltCore, boltEdge);
    meshScale = 0.10f;
    lifeTime  = 2.0f;
    drag      = 0.002f;   // very low drag, glassy
}

// ---------------------------------------------------------------------------
// ArcaneBoltProjectile — fastest, pierces NPCs.
// ---------------------------------------------------------------------------

ArcaneBoltProjectile::ArcaneBoltProjectile() {
    boltElement  = Element::Arcane;
    boltCore     = {245, 200, 255, 255};
    boltEdge     = {160,  80, 230, 255};
    trailColorA  = {220, 160, 255, 255};
    trailColorB  = {130,  60, 210, 255};
    if (ownsMesh && mesh) { delete mesh; mesh = nullptr; }
    setTint(boltCore, boltEdge);
    meshScale = 0.09f;
    lifeTime  = 2.0f;
    drag      = 0.0f;
}

void ArcaneBoltProjectile::onHitNpc(NPC& /*n*/) {
    // Arcane bolts pierce — don't mark dead, just let it pass through.
    // Damage attribution to multiple NPCs would need a separate per-NPC
    // "already-hit" set per projectile, but the visual pierce alone is
    // a meaningful feel improvement.
}

// ---------------------------------------------------------------------------
// Per-frame projectile-vs-NPC collision sweep
// ---------------------------------------------------------------------------

void updateProjectileCollisions(AppContext& ctx) {
    // Quadratic in (projectiles * npcs) but both lists are tiny in
    // practice (< a dozen each). Distance check uses squared length
    // so we avoid sqrt per pair.
    const float hitRadius   = 0.45f;
    const float hitRadiusSq = hitRadius * hitRadius;

    for (auto& a : ctx.objectManager.objects()) {
        if (a->dead || a->kind != ObjectKind::Projectile) continue;
        Projectile* proj = static_cast<Projectile*>(a.get());

        // Magic-bolt trail particles — pushed into the global voxel
        // particle queue so the bolt's path is visibly streaked with
        // its elemental colours. Without this the bolt is a tiny 0.6
        // block sprite gone in a frame; with this you can clearly see
        // a fire bolt arcing across a battlefield.
        if (auto* bolt = dynamic_cast<MagicBoltProjectile*>(proj)) {
            if (!bolt->grounded && !bolt->dead) {
                bolt->trailTimer += ctx.deltaTime;
                while (bolt->trailTimer > 0.025f) {
                    bolt->trailTimer -= 0.025f;
                    VoxelDeathParticle p;
                    p.pos = bolt->position;
                    p.vel = glm::vec3(0.0f);   // trails just float
                    p.color    = (rand() % 2 == 0)
                                  ? bolt->trailColorA
                                  : bolt->trailColorB;
                    p.life     = 0.5f;
                    p.maxLife  = 0.5f;
                    p.size     = bolt->meshScale * 0.6f;
                    p.grounded = true;          // skip gravity in updater
                    ctx.voxelParticles.push_back(p);
                }
            }
            // Fire bolts burst on impact — spawn a small ring of
            // embers around the impact point. We do it here (after
            // any contact mark dead this frame) so it fires once.
            if (bolt->dead && bolt->boltElement
                == MagicBoltProjectile::Element::Fire) {
                for (int i = 0; i < 24; i++) {
                    float ang = (float)i * 6.2831f / 24.0f;
                    VoxelDeathParticle p;
                    p.pos = bolt->position + glm::vec3(
                        std::cos(ang) * 0.05f, 0.1f, std::sin(ang) * 0.05f);
                    float speed = 2.0f + (rand() % 100) / 50.0f;
                    p.vel  = glm::vec3(std::cos(ang) * speed,
                                        3.0f + (rand() % 100) / 60.0f,
                                        std::sin(ang) * speed);
                    p.color    = (i & 1) ? bolt->trailColorA
                                          : bolt->trailColorB;
                    p.life     = 1.2f;
                    p.maxLife  = 1.2f;
                    p.size     = 0.10f;
                    p.grounded = false;
                    ctx.voxelParticles.push_back(p);
                }
            }
        }

        if (proj->grounded) continue;

        for (auto& b : ctx.objectManager.objects()) {
            if (b->dead || b->kind != ObjectKind::NPC) continue;
            NPC* n = static_cast<NPC*>(b.get());
            // Project to body-centre roughly (1.0m above the foot).
            glm::vec3 centre = n->position + glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 d      = proj->position - centre;
            if (d.x * d.x + d.y * d.y + d.z * d.z < hitRadiusSq) {
                proj->onHitNpc(*n);
                break;   // one NPC per arrow is plenty (arcane pierces)
            }
        }
    }
}
