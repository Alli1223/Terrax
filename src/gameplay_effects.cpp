// Environmental effects: falling leaves, weather state machine, rain/snow
// particles, ambient motes and the house-placement preview ghost. Split out of
// gameplay.cpp; updateGameplay ticks these each frame. See gameplay_internal.h.
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

void updateLeafParticles(AppContext& ctx) {
    static constexpr float LEAF_LIFE  = 5.5f;
    static constexpr int   MAX_LEAF   = 200;
    static std::mt19937 sRng(std::random_device{}());

    // Update existing particles
    for (int i = (int)ctx.leafParticles.size() - 1; i >= 0; i--) {
        LeafParticle& p = ctx.leafParticles[i];
        p.life -= ctx.deltaTime;
        if (p.life <= 0.0f) {
            ctx.leafParticles[i] = ctx.leafParticles.back();
            ctx.leafParticles.pop_back();
            continue;
        }
        p.vel.y -= 2.2f * ctx.deltaTime;
        p.vel.y  = std::max(p.vel.y, -2.5f);
        float t  = p.maxLife - p.life;
        p.vel.x  = sinf(t * 2.1f + p.pos.x * 0.4f) * 0.5f;
        p.vel.z  = cosf(t * 1.7f + p.pos.z * 0.4f) * 0.5f;
        p.pos   += p.vel * ctx.deltaTime;
    }

    // Spawn new particles
    ctx.leafSpawnTimer -= ctx.deltaTime;
    if (ctx.leafSpawnTimer > 0.0f || (int)ctx.leafParticles.size() >= MAX_LEAF) return;

    std::uniform_real_distribution<float> randF(0.0f, 1.0f);
    ctx.leafSpawnTimer = 0.08f + randF(sRng) * 0.12f;

    std::uniform_int_distribution<int> rdx(-22, 22), rdz(-22, 22);
    int spawnCount = 1 + (int)(randF(sRng) * 2.0f);
    for (int s = 0; s < spawnCount && (int)ctx.leafParticles.size() < MAX_LEAF; s++) {
        int wx = (int)ctx.camera.position.x + rdx(sRng);
        int wz = (int)ctx.camera.position.z + rdz(sRng);
        int startY = std::min((int)ctx.camera.position.y + 45, CHUNK_HEIGHT - 2);
        for (int wy = startY; wy >= (int)ctx.camera.position.y - 5; wy--) {
            BlockType bt = ctx.world.getBlock(wx, wy, wz);
            if (bt == BlockType::Leaves || bt == BlockType::LeavesOrange ||
                bt == BlockType::LeavesRed || bt == BlockType::LeavesPink) {
                if (ctx.world.getBlock(wx, wy + 1, wz) == BlockType::Air) {
                    LeafParticle lp;
                    lp.pos    = glm::vec3((float)wx + randF(sRng), (float)(wy + 1), (float)wz + randF(sRng));
                    lp.vel    = glm::vec3(0.0f, -0.05f, 0.0f);
                    lp.maxLife = LEAF_LIFE * (0.6f + 0.4f * randF(sRng));
                    lp.life   = lp.maxLife;
                    lp.leafBT = (uint8_t)bt;
                    ctx.leafParticles.push_back(lp);
                    break;
                }
            }
        }
    }
}

// Advances the client-side weather state: a dynamic cycle of clear spells,
// rain and the occasional storm. weatherIntensity eases toward each phase's
// target so weather rolls in and clears gradually rather than snapping.
void updateWeather(AppContext& ctx) {
    static std::mt19937 wRng(std::random_device{}());
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // Precipitation kind tracks the biome under the player — mountains and
    // tundra get snow, everywhere else gets rain.
    SurfaceSample s = sampleSurface((int)floorf(ctx.camera.position.x),
                                    (int)floorf(ctx.camera.position.z));
    ctx.weatherKind = (s.biome == 3 || s.biome == 4) ? 1 : 0;

    // When a phase ends, roll the next one: mostly fair weather, sometimes
    // rain, occasionally a full storm.
    ctx.weatherTimer -= ctx.deltaTime;
    if (ctx.weatherTimer <= 0.0f) {
        float r = u01(wRng);
        if (r < 0.50f) {                                  // clear spell
            ctx.weatherTarget = 0.05f * u01(wRng);
            ctx.weatherTimer  = 70.0f + 80.0f * u01(wRng);
        } else if (r < 0.84f) {                           // rain / light weather
            ctx.weatherTarget = 0.30f + 0.30f * u01(wRng);
            ctx.weatherTimer  = 55.0f + 55.0f * u01(wRng);
        } else {                                          // storm
            ctx.weatherTarget = 0.82f + 0.18f * u01(wRng);
            ctx.weatherTimer  = 35.0f + 35.0f * u01(wRng);
        }
    }
    // Ease toward the target so weather rolls in and clears gradually.
    ctx.weatherIntensity += (ctx.weatherTarget - ctx.weatherIntensity)
                          * std::min(1.0f, ctx.deltaTime * 0.18f);
}

// Spawns and advances rain / snow particles in a column around the camera.
// Density tracks the storm intensity; rain falls fast and straight, snow
// drifts gently. Particles die on any solid block, so precipitation naturally
// stops under a roof (leaves are pass-through so rain reaches the forest floor).
void updateWeatherParticles(AppContext& ctx) {
    static std::mt19937 pRng(std::random_device{}());
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const int       MAX_WP = 900;
    const bool      snow   = (ctx.weatherKind == 1);
    const glm::vec3 cam    = ctx.camera.position;

    // Advance and recycle existing particles.
    for (int i = (int)ctx.weatherParticles.size() - 1; i >= 0; i--) {
        WeatherParticle& p = ctx.weatherParticles[i];
        p.life -= ctx.deltaTime;
        if (snow) {
            p.pos.x += sinf(p.pos.y * 0.6f + p.seed) * 0.5f * ctx.deltaTime;
            p.pos.z += cosf(p.pos.y * 0.5f + p.seed * 1.3f) * 0.5f * ctx.deltaTime;
            p.pos.y += p.vel.y * ctx.deltaTime;
        } else {
            p.pos += p.vel * ctx.deltaTime;
        }
        BlockType hb = ctx.world.getBlock((int)floorf(p.pos.x),
                                          (int)floorf(p.pos.y),
                                          (int)floorf(p.pos.z));
        bool hitSolid = hb != BlockType::Air        && hb != BlockType::Leaves &&
                        hb != BlockType::LeavesOrange && hb != BlockType::LeavesRed &&
                        hb != BlockType::LeavesPink;
        if (p.life <= 0.0f || p.pos.y < cam.y - 12.0f || hitSolid) {
            ctx.weatherParticles[i] = ctx.weatherParticles.back();
            ctx.weatherParticles.pop_back();
        }
    }

    // Below a whisper of weather, stop spawning and let the stragglers fall.
    if (ctx.weatherIntensity < 0.04f) return;

    int target  = std::min(MAX_WP,
                  (int)((snow ? 470.0f : 820.0f) * ctx.weatherIntensity));
    int deficit = target - (int)ctx.weatherParticles.size();
    int spawn   = std::min(deficit, snow ? 9 : 26);
    for (int s = 0; s < spawn; s++) {
        WeatherParticle p;
        float ang = u01(pRng) * 6.2831853f;
        float rad = sqrtf(u01(pRng)) * 24.0f;
        float hi  = snow ? (8.0f + u01(pRng) * 14.0f) : (14.0f + u01(pRng) * 12.0f);
        p.pos  = glm::vec3(cam.x + cosf(ang) * rad, cam.y + hi,
                           cam.z + sinf(ang) * rad);
        p.seed = u01(pRng) * 6.2831853f;
        if (snow) {
            p.vel  = glm::vec3(0.0f, -1.3f - u01(pRng) * 0.9f, 0.0f);
            p.life = 24.0f;
        } else {
            p.vel  = glm::vec3(1.4f + u01(pRng) * 1.3f,
                               -22.0f - u01(pRng) * 7.0f, 0.7f);
            p.life = 5.0f;
        }
        ctx.weatherParticles.push_back(p);
    }
}

// Drifting atmosphere particles — pale pollen motes by day (lovely catching
// the volumetric light shafts), glowing fireflies near the ground at night,
// and warm embers rising from nearby town campfires after dark.
void updateAmbientParticles(AppContext& ctx) {
    static std::mt19937 aRng(std::random_device{}());
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const int       MAX_AMB = 300;
    const glm::vec3 cam     = ctx.camera.position;

    // Day vs night from the sun's elevation (gameTime is 0..1).
    float sunY = sinf((ctx.gameTime - 0.25f) * 6.2831853f);
    bool  day   = sunY >  0.05f;
    bool  night = sunY < -0.05f;

    // Advance and recycle existing particles.
    for (int i = (int)ctx.ambientParticles.size() - 1; i >= 0; i--) {
        AmbientParticle& p = ctx.ambientParticles[i];
        p.life -= ctx.deltaTime;
        float t = (p.maxLife - p.life) + p.seed;
        if (p.kind == 1) {                  // firefly: gentle bob and wander
            p.pos.x += sinf(t * 1.7f) * 0.45f * ctx.deltaTime;
            p.pos.y += sinf(t * 2.3f) * 0.30f * ctx.deltaTime;
            p.pos.z += cosf(t * 1.5f + p.seed) * 0.45f * ctx.deltaTime;
        } else if (p.kind == 2) {           // ember: rises with horizontal wobble
            p.pos.x += sinf(t * 3.1f) * 0.30f * ctx.deltaTime;
            p.pos.z += cosf(t * 2.7f) * 0.30f * ctx.deltaTime;
            p.pos.y += p.vel.y * ctx.deltaTime;
            p.vel.y *= 0.985f;
        } else {                             // pollen: slow drift in still air
            p.pos.x += sinf(t * 0.7f) * 0.20f * ctx.deltaTime;
            p.pos.z += cosf(t * 0.5f + p.seed) * 0.20f * ctx.deltaTime;
            p.pos.y += (-0.06f + sinf(t * 0.9f) * 0.10f) * ctx.deltaTime;
        }
        float dx = p.pos.x - cam.x, dz = p.pos.z - cam.z;
        if (p.life <= 0.0f || (dx * dx + dz * dz) > 28.0f * 28.0f) {
            ctx.ambientParticles[i] = ctx.ambientParticles.back();
            ctx.ambientParticles.pop_back();
        }
    }

    if ((int)ctx.ambientParticles.size() >= MAX_AMB) return;
    ctx.ambientSpawnTimer -= ctx.deltaTime;
    if (ctx.ambientSpawnTimer > 0.0f) return;
    ctx.ambientSpawnTimer = 0.13f + u01(aRng) * 0.10f;

    auto spawn = [&](glm::vec3 p, glm::vec3 col, float maxLife, float size,
                     uint8_t kind, glm::vec3 vel) {
        if ((int)ctx.ambientParticles.size() >= MAX_AMB) return;
        AmbientParticle a;
        a.pos = p; a.vel = vel; a.color = col;
        a.maxLife = maxLife; a.life = maxLife;
        a.seed = u01(aRng) * 6.2831853f;
        a.size = size; a.kind = kind;
        ctx.ambientParticles.push_back(a);
    };

    if (day) {
        // A pair of pollen motes drifting somewhere in a column around the camera.
        for (int s = 0; s < 2; s++) {
            float a = u01(aRng) * 6.2831853f;
            float r = sqrtf(u01(aRng)) * 22.0f;
            glm::vec3 p(cam.x + cosf(a) * r,
                        cam.y - 1.0f + u01(aRng) * 7.5f,
                        cam.z + sinf(a) * r);
            glm::vec3 col = glm::vec3(1.00f, 0.93f, 0.72f)
                          * (0.50f + u01(aRng) * 0.35f);
            spawn(p, col, 7.0f + u01(aRng) * 5.0f,
                  0.06f + u01(aRng) * 0.04f, 0, glm::vec3(0.0f));
        }
    } else if (night) {
        // One firefly low around the camera.
        {
            float a = u01(aRng) * 6.2831853f;
            float r = sqrtf(u01(aRng)) * 18.0f;
            glm::vec3 p(cam.x + cosf(a) * r,
                        cam.y - 1.4f + u01(aRng) * 2.6f,
                        cam.z + sinf(a) * r);
            spawn(p, glm::vec3(0.95f, 0.95f, 0.30f),
                  9.0f + u01(aRng) * 6.0f,
                  0.075f + u01(aRng) * 0.03f, 1, glm::vec3(0.0f));
        }
        // Embers from any nearby campfire-centerpiece town.
        const TownPlan& plan = getTownPlan();
        for (const Town& t : plan.towns) {
            if (t.centerpiece != TownCenter::Campfire) continue;
            float dx = (float)t.center.x - cam.x;
            float dz = (float)t.center.y - cam.z;
            if (dx * dx + dz * dz > 48.0f * 48.0f) continue;
            glm::vec3 p((float)t.center.x + 0.5f + (u01(aRng) - 0.5f) * 0.4f,
                        (float)t.baseY + 1.6f,
                        (float)t.center.y + 0.5f + (u01(aRng) - 0.5f) * 0.4f);
            glm::vec3 col(1.0f, 0.55f + u01(aRng) * 0.25f, 0.14f);
            spawn(p, col, 2.2f + u01(aRng) * 0.8f,
                  0.05f + u01(aRng) * 0.03f, 2,
                  glm::vec3(0.0f, 0.7f + u01(aRng) * 0.6f, 0.0f));
        }
    }
}

// Keeps the house placement ghost in front of the player, snapped to the
// ground surface, while a placement is being previewed.
void updateHousePreview(AppContext& ctx) {
    if (!ctx.housePreviewActive) return;
    glm::vec3 fwd(sinf(glm::radians(ctx.playerYaw)), 0.0f,
                  cosf(glm::radians(ctx.playerYaw)));
    glm::vec3 target = ctx.camera.position + fwd * 10.0f;
    int gx = (int)floorf(target.x), gz = (int)floorf(target.z);
    int gy = std::min((int)ctx.camera.position.y + 8, CHUNK_HEIGHT - 2);
    while (gy > 1 && ctx.world.getBlock(gx, gy - 1, gz) == BlockType::Air)
        gy--;
    ctx.housePreviewPos = glm::vec3(target.x, (float)gy, target.z);
    ctx.housePreviewYaw = roundf(ctx.playerYaw / 90.0f) * 90.0f;
}

