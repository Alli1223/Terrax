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
#include "audio.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <cmath>
#include <cstring>
#include <mutex>
#include <random>
#include <memory>
#include <unordered_map>
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

// World-space vent above a house's chimney — the tallest solid column of the
// baked building grid. The chimney is the highest point of the house, so this
// finds it regardless of how the template was rotated, and ignores roofs with no
// chimney stack. Computed once per building and cached by its world corner.
// Returns false for buildings with no usable chimney (sheds, flat roofs, farms).
static bool chimneyVentFor(const TownBuilding& b, glm::vec3& outVent) {
    static std::unordered_map<uint64_t, glm::vec3> cache;   // y < 0 ⇒ no chimney
    uint64_t key = ((uint64_t)(uint32_t)b.wx << 32) | (uint32_t)b.wz;
    auto it = cache.find(key);
    if (it == cache.end()) {
        int bestY = -1, bestX = 0, bestZ = 0;
        if (b.dimX > 0 && b.dimY > 0 && b.dimZ > 0 &&
            (int)b.blocks.size() >= b.dimX * b.dimY * b.dimZ) {
            for (int z = 0; z < b.dimZ; z++)
                for (int x = 0; x < b.dimX; x++)
                    for (int y = b.dimY - 1; y >= 0; y--) {
                        uint8_t v = b.blocks[((size_t)y * b.dimZ + z) * b.dimX + x];
                        if (v != (uint8_t)BlockType::Air) {
                            if (y > bestY) { bestY = y; bestX = x; bestZ = z; }
                            break;
                        }
                    }
        }
        glm::vec3 vent(-1.0f);
        if (bestY >= 5)                          // tall enough to be a real stack
            vent = glm::vec3((float)b.wx + (float)bestX + 0.5f,
                             (float)b.baseY + (float)bestY + 0.5f,
                             (float)b.wz + (float)bestZ + 0.5f);
        it = cache.emplace(key, vent).first;
    }
    if (it->second.y < 0.0f) return false;
    outVent = it->second;
    return true;
}

// Drifting atmosphere particles — pale pollen motes by day (lovely catching
// the volumetric light shafts), glowing fireflies near the ground at night,
// warm embers from nearby town campfires after dark, and thin smoke rising from
// the chimneys of occupied homes so towns read as lived-in from the outside.
void updateAmbientParticles(AppContext& ctx) {
    static std::mt19937 aRng(std::random_device{}());
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const int       MAX_AMB = 480;   // raised to make room for hearth smoke
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
        } else if (p.kind == 3) {           // smoke: rises, billows outward, slows
            p.pos.x += (sinf(t * 0.8f) * 0.25f + p.vel.x) * ctx.deltaTime;
            p.pos.z += (cosf(t * 0.7f) * 0.25f + p.vel.z) * ctx.deltaTime;
            p.pos.y += p.vel.y * ctx.deltaTime;
            p.vel.y *= 0.992f;
            p.size  += 0.20f * ctx.deltaTime;
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

    // --- Smoke + glints from streamed-in fire props (runs day and night) ----
    // Hearths smoke and spit the odd spark; lit lanterns get a faint drifting
    // glint after dark. Iterating the streamed objects keeps the cost tied to
    // what's actually around the player.
    int emitted = 0;
    for (const auto& obj : ctx.objectManager.objects()) {
        if (emitted >= 14) break;
        if (obj->dead || obj->kind != ObjectKind::Prop) continue;
        const Prop* pr = static_cast<const Prop*>(obj.get());
        const float dx = pr->position.x - cam.x, dz = pr->position.z - cam.z;
        if (pr->type == PropType::Fireplace) {
            if (dx * dx + dz * dz > 22.0f * 22.0f) continue;
            // The smoke itself now vents from the rooftop chimney (below); the
            // hearth just spits the odd spark for a cosy glow when you're indoors.
            if (u01(aRng) < 0.4f) {                       // a spark off the fire
                glm::vec3 p = pr->position + glm::vec3((u01(aRng) - 0.5f) * 0.4f, 0.7f,
                                                       (u01(aRng) - 0.5f) * 0.25f);
                spawn(p, glm::vec3(1.0f, 0.55f + u01(aRng) * 0.28f, 0.16f),
                      1.5f + u01(aRng) * 0.7f, 0.045f + u01(aRng) * 0.03f, 2,
                      glm::vec3(0.0f, 0.7f + u01(aRng) * 0.6f, 0.0f));
                emitted++;
            }
        } else if (night && pr->type == PropType::Lantern && u01(aRng) < 0.18f) {
            if (dx * dx + dz * dz > 20.0f * 20.0f) continue;
            glm::vec3 p = pr->position + glm::vec3((u01(aRng) - 0.5f) * 0.5f,
                                                   0.5f + u01(aRng) * 0.5f,
                                                   (u01(aRng) - 0.5f) * 0.5f);
            spawn(p, glm::vec3(1.0f, 0.82f, 0.45f), 2.4f + u01(aRng) * 1.4f,
                  0.05f + u01(aRng) * 0.025f, 2,
                  glm::vec3(0.0f, 0.18f + u01(aRng) * 0.16f, 0.0f));
            emitted++;
        }
    }

    // Smoke drifting up off nearby town campfires (vents to the sky, day or night).
    for (const Town& t : getTownPlan().towns) {
        if (t.centerpiece != TownCenter::Campfire) continue;
        float cdx = (float)t.center.x - cam.x, cdz = (float)t.center.y - cam.z;
        if (cdx * cdx + cdz * cdz > 40.0f * 40.0f) continue;
        if (u01(aRng) < 0.5f) {
            float g = 0.34f + u01(aRng) * 0.16f;
            glm::vec3 p((float)t.center.x + 0.5f + (u01(aRng) - 0.5f) * 0.5f,
                        (float)t.baseY + 2.2f,
                        (float)t.center.y + 0.5f + (u01(aRng) - 0.5f) * 0.5f);
            spawn(p, glm::vec3(g, g, g * 0.97f), 3.6f + u01(aRng) * 1.8f,
                  0.20f + u01(aRng) * 0.08f, 3,
                  glm::vec3((u01(aRng) - 0.5f) * 0.3f, 0.9f + u01(aRng) * 0.5f,
                            (u01(aRng) - 0.5f) * 0.3f));
        }
    }

    // --- Rooftop chimney smoke from nearby occupied homes (day & night) ------
    // A thin grey plume from each house's chimney so a town looks lived-in from
    // outside. Vents are found once per building (the tallest column of its baked
    // grid) and cached; kept within the 28-block particle horizon and capped per
    // frame so a dense town reads as a scatter of plumes, not a smokescreen.
    int chimneys = 0;
    for (const Town& t : getTownPlan().towns) {
        if (chimneys >= 6) break;
        float tdx = (float)t.center.x - cam.x, tdz = (float)t.center.y - cam.z;
        float reach = (float)t.radius + 28.0f;
        if (tdx * tdx + tdz * tdz > reach * reach) continue;   // whole town too far
        for (const TownBuilding& b : t.buildings) {
            if (chimneys >= 6) break;
            if (b.rooms.empty()) continue;                     // homes & shops only
            float bcx = (float)b.wx + b.dimX * 0.5f, bcz = (float)b.wz + b.dimZ * 0.5f;
            float dx = bcx - cam.x, dz = bcz - cam.z;
            if (dx * dx + dz * dz > 26.0f * 26.0f) continue;
            glm::vec3 vent;
            if (!chimneyVentFor(b, vent)) continue;
            if (u01(aRng) < 0.22f) {
                float g = 0.40f + u01(aRng) * 0.16f;
                glm::vec3 p = vent + glm::vec3((u01(aRng) - 0.5f) * 0.3f, 0.4f,
                                               (u01(aRng) - 0.5f) * 0.3f);
                spawn(p, glm::vec3(g, g, g * 0.96f), 4.0f + u01(aRng) * 2.2f,
                      0.13f + u01(aRng) * 0.06f, 3,
                      glm::vec3((u01(aRng) - 0.5f) * 0.18f, 0.55f + u01(aRng) * 0.35f,
                                (u01(aRng) - 0.5f) * 0.18f));
                chimneys++;
            }
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

// Per-frame audio upkeep: refresh the listener + ambient beds (wind/fire/birds)
// and emit the local player's footsteps. Sound is purely client-side, so this is
// a no-op whenever the audio engine failed to start (g_audio stays null).
void updateAudio(AppContext& ctx) {
    if (!g_audio) return;
    const glm::vec3 cam = ctx.camera.position;

    // Distance to the closest lit hearth or campfire — drives the fire ambience.
    float nearestFire = 1e9f;
    for (const auto& obj : ctx.objectManager.objects()) {
        if (obj->dead || obj->kind != ObjectKind::Prop) continue;
        const Prop* pr = static_cast<const Prop*>(obj.get());
        if (pr->type != PropType::Fireplace) continue;
        float dx = pr->position.x - cam.x, dz = pr->position.z - cam.z;
        nearestFire = std::min(nearestFire, std::sqrt(dx * dx + dz * dz));
    }
    for (const Town& t : getTownPlan().towns) {
        if (t.centerpiece != TownCenter::Campfire) continue;
        float dx = (float)t.center.x - cam.x, dz = (float)t.center.y - cam.z;
        nearestFire = std::min(nearestFire, std::sqrt(dx * dx + dz * dz));
    }

    g_audio->update(cam, ctx.playerYaw, ctx.deltaTime, ctx.gameTime,
                    ctx.weatherIntensity, nearestFire);

    // Footsteps: a soft step on a cadence that quickens with speed. Alternating
    // pitch gives a left/right-foot feel. Only while grounded and actually moving.
    static float stepTimer = 0.0f;
    static bool  altFoot   = false;
    float sx = ctx.camera.velocity.x, sz = ctx.camera.velocity.z;
    float speed = std::sqrt(sx * sx + sz * sz);
    if (ctx.camera.onGround && speed > 1.5f) {
        stepTimer -= ctx.deltaTime;
        if (stepTimer <= 0.0f) {
            stepTimer = (speed > 14.0f) ? 0.27f : 0.42f;   // faster when sprinting
            altFoot   = !altFoot;
            g_audio->play2D(SoundId::Footstep, 0.32f, altFoot ? 0.96f : 1.05f);
        }
    } else {
        stepTimer = 0.0f;
    }
}

