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
#include <algorithm>
#include <vector>
#include <iostream>
#include <cmath>
#include <cstring>
#include <mutex>
#include <random>
#include <memory>

static void cleanupRemotePlayers(AppContext& ctx) {
    for (auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        if (p.rig) delete p.rig;
    }
    ctx.remotePlayers.clear();
}

// Keeps the ObjectManager's remote-player objects in step with the
// remotePlayers map: drop objects whose player has gone, add objects for
// newly-seen players. Must run before objectManager.updateAll so a Player
// never dereferences a freed RemotePlayer record.
static void syncRemotePlayerObjects(AppContext& ctx) {
    std::vector<uint32_t> gone;
    for (auto& o : ctx.objectManager.objects())
        if (o->kind == ObjectKind::Player && o->id != 0 &&
            ctx.remotePlayers.find(o->id) == ctx.remotePlayers.end())
            gone.push_back(o->id);
    for (uint32_t id : gone) ctx.objectManager.removeById(id);

    for (auto& [id, rp] : ctx.remotePlayers)
        if (!ctx.objectManager.findById(id))
            ctx.objectManager.add(std::make_unique<Player>(&rp, id));
}

// Creates/updates client-side Ferry objects from the server's EntityState
// broadcasts. The server owns ferry motion; clients only interpolate + render.
static void syncFerryObjects(AppContext& ctx) {
    if (!ctx.client) return;
    for (const EntityStatePacket& ep : ctx.client->entityUpdates) {
        GameObject* o = ctx.objectManager.findById(ep.entityId);
        Ferry* f = nullptr;
        if (!o) {
            auto nf = std::make_unique<Ferry>();
            nf->id       = ep.entityId;
            nf->mesh     = getFerryMesh();
            nf->position = glm::vec3(ep.x, ep.y, ep.z);
            nf->yaw      = ep.yaw;
            f = nf.get();
            ctx.objectManager.add(std::move(nf));
        } else if (o->kind == ObjectKind::Vehicle) {
            f = static_cast<Ferry*>(o);
        }
        if (f) {
            f->targetPos = glm::vec3(ep.x, ep.y, ep.z);
            f->targetYaw = ep.yaw;
        }
    }
    ctx.client->entityUpdates.clear();
}

// Creates/updates client-side NPC objects from the server's NPCState
// broadcasts, and times out NPCs the server has stopped sending (their town
// streamed out of range). The server owns NPC motion/AI; clients interpolate.
static void syncNPCObjects(AppContext& ctx) {
    if (!ctx.client) return;
    double now = glfwGetTime();
    for (const NPCStatePacket& np : ctx.client->npcUpdates) {
        GameObject* o = ctx.objectManager.findById(np.entityId);
        NPC* n = nullptr;
        if (!o) {
            auto nn = std::make_unique<NPC>();
            nn->id             = np.entityId;
            nn->type           = (NPCType)np.npcType;
            nn->appearanceSeed = np.appearanceSeed;
            nn->position       = glm::vec3(np.x, np.y, np.z);
            nn->yaw            = np.yaw;
            nn->initClientVisual();
            n = nn.get();
            ctx.objectManager.add(std::move(nn));
        } else if (o->kind == ObjectKind::NPC) {
            n = static_cast<NPC*>(o);
        }
        if (n) {
            n->targetPos  = glm::vec3(np.x, np.y, np.z);
            n->targetYaw  = np.yaw;
            n->velocity   = glm::vec3(np.vx, np.vy, np.vz);
            n->health     = np.health;
            n->walking    = (np.flags & 1) != 0;
            n->attackFlag = (np.flags & 2) != 0;
            n->dyingFlag  = (np.flags & 4) != 0;
            n->lastUpdate = now;
        }
    }
    ctx.client->npcUpdates.clear();

    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        if (now - static_cast<NPC*>(o.get())->lastUpdate > 2.0)
            o->dead = true;
    }
}

// Creates/updates client-side Animal objects from the server's AnimalState
// broadcasts, and times out animals the server has stopped sending.
static void syncAnimalObjects(AppContext& ctx) {
    if (!ctx.client) return;
    double now = glfwGetTime();
    for (const AnimalStatePacket& ap : ctx.client->animalUpdates) {
        GameObject* o = ctx.objectManager.findById(ap.entityId);
        Animal* a = nullptr;
        if (!o) {
            auto na = std::make_unique<Animal>();
            na->id       = ap.entityId;
            na->species  = (AnimalSpecies)ap.species;
            na->variant  = ap.variant;
            na->position = glm::vec3(ap.x, ap.y, ap.z);
            na->yaw      = ap.yaw;
            na->initClientVisual();
            a = na.get();
            ctx.objectManager.add(std::move(na));
        } else if (o->kind == ObjectKind::Animal) {
            a = static_cast<Animal*>(o);
        }
        if (a) {
            a->targetPos  = glm::vec3(ap.x, ap.y, ap.z);
            a->targetYaw  = ap.yaw;
            a->velocity   = glm::vec3(ap.vx, ap.vy, ap.vz);
            a->walking    = (ap.flags & 1) != 0;
            a->lastUpdate = now;
        }
    }
    ctx.client->animalUpdates.clear();

    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::Animal) continue;
        if (now - static_cast<Animal*>(o.get())->lastUpdate > 2.0)
            o->dead = true;
    }
}

// Finds the villager the player is facing within talk range, drives the talk
// prompt, and opens the dialogue box when the interact key was pressed.
// Scans nearby GameObjects for the best interactable in front of the player.
// Drives `ctx.pendingInteraction` (read by the HUD for the E-hint) and, on E
// press, enters or exits a player pose. Designed to be extended: adding a new
// InteractAction value + a switch case here is enough to wire a new action.
static void updatePropInteraction(AppContext& ctx) {
    glm::vec3 eye = ctx.camera.position;
    glm::vec3 fwd = glm::vec3(ctx.camera.front.x, 0.0f, ctx.camera.front.z);
    if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);

    Interaction best{};
    float bestD2 = 2.5f * 2.5f;            // ~ arm's reach
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead) continue;
        Interaction off;
        if (!o->getInteraction(off)) continue;
        glm::vec3 to = o->position - eye;
        float dy = to.y;
        to.y = 0.0f;
        float d2 = to.x * to.x + to.z * to.z;
        if (d2 > bestD2) continue;
        if (std::abs(dy) > 1.5f) continue;     // same-floor only
        if (d2 > 0.04f && glm::dot(glm::normalize(to), fwd) < 0.2f) continue;
        bestD2 = d2;
        best   = off;
    }
    // While already in a pose we don't surface another object's hint; the HUD
    // shows the "press E to get up" prompt instead.
    ctx.pendingInteraction = (ctx.playerPose == PlayerPose::Standing) ? best
                                                                      : Interaction{};

    // Handle E press: enter a new pose, or exit the current one.
    if (ctx.interactPressed) {
        if (ctx.playerPose != PlayerPose::Standing) {
            ctx.playerPose      = PlayerPose::Standing;
            ctx.interactPressed = false;
        } else if (best.action != InteractAction::None) {
            ctx.poseAnchorPos = best.anchorPos;
            ctx.poseAnchorYaw = best.anchorYaw;
            switch (best.action) {
                case InteractAction::SitChair:
                    ctx.playerPose = PlayerPose::Sitting; break;
                case InteractAction::LieBed:
                    ctx.playerPose = PlayerPose::Lying;   break;
                default: break;
            }
            ctx.interactPressed = false;
        }
    }

    // Any movement input pops the player out of the pose.
    if (ctx.playerPose != PlayerPose::Standing) {
        if (ctx.keyFwd || ctx.keyBack || ctx.keyLeft ||
            ctx.keyRight || ctx.keyJump) {
            ctx.playerPose = PlayerPose::Standing;
        }
    }

    // Push the pose into the rig and snap the camera to the pose anchor.
    if (ctx.playerRig) ctx.playerRig->pose = ctx.playerPose;
    if (ctx.playerPose != PlayerPose::Standing) {
        const float eyeOffset =
            (ctx.playerPose == PlayerPose::Sitting) ? 0.95f : 0.55f;
        ctx.camera.position = ctx.poseAnchorPos
                            + glm::vec3(0.0f, eyeOffset, 0.0f);
        ctx.camera.velocity = glm::vec3(0.0f);
        ctx.camera.onGround = true;
    }
}

static void updateNpcInteraction(AppContext& ctx) {
    glm::vec3 eye = ctx.camera.position;
    glm::vec3 fwd = glm::vec3(ctx.camera.front.x, 0.0f, ctx.camera.front.z);
    if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);

    NPC* best = nullptr;
    float bestD2 = 4.0f * 4.0f;
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        NPC* n = static_cast<NPC*>(o.get());
        if (n->type != NPCType::Villager) continue;
        glm::vec3 to = n->position - eye; to.y = 0.0f;
        float d2 = to.x * to.x + to.z * to.z;
        if (d2 > bestD2) continue;
        if (d2 > 0.04f && glm::dot(glm::normalize(to), fwd) < 0.35f) continue;
        bestD2 = d2;
        best   = n;
    }

    if (best) {
        ctx.talkTargetName = npcName(best->appearanceSeed);
        ctx.talkTargetSeed = best->appearanceSeed;
        ctx.talkTargetPos  = best->position;
    } else {
        ctx.talkTargetName.clear();
    }

    if (ctx.interactPressed && best) {
        ctx.talkName  = npcName(best->appearanceSeed);
        ctx.talkLine  = npcFlavorLine(best->appearanceSeed, ctx.talkCount);
        ctx.talkTimer = 6.0f;
        ctx.talkCount++;
    }
    ctx.interactPressed = false;
    if (ctx.talkTimer > 0.0f) ctx.talkTimer -= ctx.deltaTime;
}

// The NPC a melee swing should land on — nearest one ahead within reach.
static NPC* findMeleeTargetNpc(AppContext& ctx) {
    glm::vec3 eye = ctx.camera.position;
    glm::vec3 fwd = glm::vec3(ctx.camera.front.x, 0.0f, ctx.camera.front.z);
    if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
    NPC* best = nullptr;
    float bestD2 = 3.8f * 3.8f;
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        NPC* n = static_cast<NPC*>(o.get());
        glm::vec3 to = n->position - eye; to.y = 0.0f;
        float d2 = to.x * to.x + to.z * to.z;
        if (d2 > bestD2) continue;
        if (d2 > 0.04f && glm::dot(glm::normalize(to), fwd) < 0.3f) continue;
        bestD2 = d2;
        best   = n;
    }
    return best;
}

// Applies incoming damage to the player, regenerates health out of combat,
// and respawns at the spawn town on death.
static void updatePlayerVitals(AppContext& ctx) {
    if (ctx.client && ctx.client->pendingSelfDamage > 0.0f) {
        ctx.playerHealth -= ctx.client->pendingSelfDamage / 100.0f;
        ctx.client->pendingSelfDamage = 0.0f;
        ctx.regenDelay = 5.0f;
    }
    if (ctx.regenDelay > 0.0f) {
        ctx.regenDelay -= ctx.deltaTime;
    } else if (ctx.playerHealth < 1.0f) {
        ctx.playerHealth = std::min(1.0f, ctx.playerHealth + 0.045f * ctx.deltaTime);
    }
    if (ctx.playerHealth <= 0.0f) {
        // Death — respawn back at the spawn town.
        ctx.camera.position = glm::vec3((float)ctx.spawnX + 0.5f, 140.0f,
                                        (float)ctx.spawnZ + 0.5f);
        ctx.camera.velocity = glm::vec3(0.0f);
        ctx.playerHealth    = 1.0f;
        ctx.regenDelay      = 0.0f;
        ctx.spawnedOnGround = false;
        ctx.noclip          = true;
    }
}

// The ferry the local player is currently standing on (deck test), or null.
static Ferry* findSupportFerry(AppContext& ctx) {
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::Vehicle) continue;
        Ferry* f = static_cast<Ferry*>(o.get());
        if (f->onDeck(ctx.camera.position)) return f;
    }
    return nullptr;
}

void disconnectFromGame(AppContext& ctx) {
    if (ctx.client) {
        ctx.client->disconnect();
        delete ctx.client;
        ctx.client = nullptr;
    }
    ctx.objectManager.clear();
    cleanupRemotePlayers(ctx);
    if (ctx.weOwnServer) {
        stopEmbeddedServer();
        ctx.weOwnServer = false;
    }
    ctx.sessionMode       = SessionMode::None;
    ctx.clientInitialized = false;
    ctx.joinNameSent      = false;
    ctx.paused            = false;
    ctx.chatOpen          = false;
    ctx.showPlayerList    = false;
    ctx.spawnedOnGround   = false;
    ctx.keyFwd = ctx.keyBack = ctx.keyLeft = ctx.keyRight = ctx.keyJump = 0;
    ctx.housePreviewActive = false;
}

static void updateLeafParticles(AppContext& ctx) {
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
static void updateWeather(AppContext& ctx) {
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
static void updateWeatherParticles(AppContext& ctx) {
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
static void updateAmbientParticles(AppContext& ctx) {
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
static void updateHousePreview(AppContext& ctx) {
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

void updateGameplay(AppContext& ctx, GLFWwindow* window) {
    if (!ctx.clientInitialized) {
        std::cout << "Connecting to " << ctx.connectHost << ":" << ctx.connectPort << "...\n";
        ctx.client = new NetworkClient();
        if (!ctx.client->connect(ctx.connectHost, ctx.connectPort)) {
            std::cerr << "Failed to connect to server\n";
            disconnectFromGame(ctx);
            ctx.state = GameState::MainMenu;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            return;
        }
        ctx.world.onRequestChunk = [&ctx](int x, int z) {
            ChunkRequestPacket p { x, z };
            ctx.client->send(PacketType::ChunkRequest, &p, sizeof(p));
        };
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        ctx.clientInitialized = true;
        ctx.joinNameSent      = false;
        ctx.noclip            = true;
        ctx.firstMouse        = true;
        ctx.propLibrary.buildAll();   // shared furniture/decoration meshes

        // Spawn in the town nearest the world origin. Only host / singleplayer
        // run in-process with the server, so only there is the town plan (which
        // is seed-derived) guaranteed to match the server's world.
        ctx.spawnX = 8;
        ctx.spawnZ = 8;
        if (ctx.weOwnServer) {
            const TownPlan& plan = getTownPlan();
            const Town* best = nullptr;
            long long bestD = -1;
            for (const Town& t : plan.towns) {
                long long d = (long long)t.center.x * t.center.x
                            + (long long)t.center.y * t.center.y;
                if (bestD < 0 || d < bestD) { bestD = d; best = &t; }
            }
            if (best) {
                ctx.spawnX = best->center.x + 12;   // beside the town centre
                ctx.spawnZ = best->center.y;
                ctx.camera.position = glm::vec3((float)ctx.spawnX + 0.5f,
                                                (float)best->baseY + 50.0f,
                                                (float)ctx.spawnZ + 0.5f);
                ctx.camera.yaw = 180.0f;            // face back toward the centre
                ctx.camera.updateVectors();
                std::cout << "[Spawn] Nearest town to origin at ("
                          << best->center.x << ", " << best->center.y << ")\n";
            }
        }
    }

    if (ctx.client && !ctx.client->connected) {
        disconnectFromGame(ctx);
        ctx.state = GameState::MainMenu;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        return;
    }

    if (!ctx.client || !ctx.client->connected) return;

    if (ctx.client->clientID != 0 && !ctx.joinNameSent) {
        ctx.client->sendPlayerJoin(ctx.playerName);
        PlayerModelHeader mh{};
        mh.clientID     = ctx.client->clientID;
        mh.hairStyle    = ctx.playerRig->hairStyle;
        mh.hairColor    = ctx.playerRig->hairColor;
        mh.eyeColor     = ctx.playerRig->eyeColor;
        mh.eyeType      = ctx.playerRig->eyeType;
        mh.noseStyle    = ctx.playerRig->noseStyle;
        mh.eyebrowStyle = ctx.playerRig->eyebrowStyle;
        mh.earType      = ctx.playerRig->earType;
        mh.armorType    = ctx.playerRig->armorType;
        ctx.client->send(PacketType::PlayerModel, &mh, sizeof(mh));
        ctx.joinNameSent = true;
    }

    if (ctx.weOwnServer && g_serverDayTimeSync.load()) {
        ctx.gameTime = getServerGameTime();
    } else if (ctx.client && ctx.client->hasServerGameTime) {
        if (ctx.client->dayTimeUpdated) {
            ctx.gameTime = ctx.client->serverGameTime;
            ctx.client->dayTimeUpdated = false;
        } else {
            ctx.gameTime = fmodf(ctx.gameTime + ctx.deltaTime / DAY_CYCLE_SECONDS, 1.0f);
        }
    }

    if (!ctx.spawnedOnGround) {
        auto chunkCoord = [](int w) {
            return (w < 0 && w % CHUNK_SIZE != 0) ? w / CHUNK_SIZE - 1 : w / CHUNK_SIZE;
        };
        int scx = chunkCoord(ctx.spawnX), scz = chunkCoord(ctx.spawnZ);
        int lx  = ctx.spawnX - scx * CHUNK_SIZE, lz = ctx.spawnZ - scz * CHUNK_SIZE;
        std::lock_guard<std::mutex> lock(ctx.world.chunksMutex);
        auto it = ctx.world.chunks.find({scx, scz});
        if (it != ctx.world.chunks.end() && it->second->state != ChunkState::Empty) {
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                if (it->second->get(lx, y, lz) != BlockType::Air) {
                    ctx.camera.position = glm::vec3((float)ctx.spawnX + 0.5f,
                                                    (float)y + 1.0f,
                                                    (float)ctx.spawnZ + 0.5f);
                    ctx.noclip          = false;
                    ctx.spawnedOnGround = true;
                    std::cout << "[Client] Spawned at (" << ctx.spawnX << ", "
                              << (y + 1) << ", " << ctx.spawnZ << ")\n";
                    break;
                }
            }
        }
    }

    const bool gameplayActive = (ctx.state == GameState::Playing && !ctx.chatOpen);

    if (gameplayActive && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (!ctx.playerRig->isAttacking) {
            ctx.playerRig->isAttacking = true;
            ctx.playerRig->attackAnim  = 0.0f;
            NPC* target = findMeleeTargetNpc(ctx);
            PlayerAttackPacket ap { ctx.client->clientID, target ? target->id : 0u };
            ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));
        }
    }

    ctx.client->update(ctx.world, ctx.remotePlayers);
    updatePlayerVitals(ctx);

    {
        double now = glfwGetTime();
        std::vector<uint32_t> stale;
        for (auto& [id, p] : ctx.remotePlayers)
            if (p.lastUpdate > 0 && now - p.lastUpdate > 8.0) stale.push_back(id);
        for (uint32_t id : stale) {
            if (ctx.remotePlayers[id].rig) delete ctx.remotePlayers[id].rig;
            ctx.remotePlayers.erase(id);
        }
    }

    if (ctx.client->clientID != 0 && gameplayActive) {
        ctx.posSendTimer += ctx.deltaTime;
        if (ctx.posSendTimer >= 0.05f) {
            PlayerPosPacket p {};
            p.id          = ctx.client->clientID;
            p.x           = ctx.camera.position.x;
            p.y           = ctx.camera.position.y;
            p.z           = ctx.camera.position.z;
            p.pitch       = ctx.camera.pitch;
            p.yaw         = ctx.playerYaw;
            p.lanternHeld = ctx.lanternHeld ? 1 : 0;
            ctx.client->sendUDP(&p, sizeof(p));
            ctx.posSendTimer = 0.0f;
        }
    }

    glm::vec3 camForward = glm::normalize(glm::vec3(ctx.camera.front.x, 0.0f, ctx.camera.front.z));
    glm::vec3 camRight   = glm::normalize(glm::vec3(ctx.camera.right.x, 0.0f, ctx.camera.right.z));
    glm::vec3 moveDir(0.0f);
    if (ctx.keyFwd)   moveDir += camForward;
    if (ctx.keyBack)  moveDir -= camForward;
    if (ctx.keyRight) moveDir += camRight;
    if (ctx.keyLeft)  moveDir -= camRight;

    if (gameplayActive && glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
        float targetYaw = glm::degrees(atan2f(moveDir.x, moveDir.z));
        float diff = targetYaw - ctx.playerYaw;
        while (diff >  180.0f) diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;
        ctx.playerYaw += diff * std::min(1.0f, ctx.deltaTime * 10.0f);
    }

    if (gameplayActive) {
        // While seated / lying down the player is locked to the pose anchor —
        // skip all movement physics (it would fight the snap-to-anchor inside
        // updatePropInteraction below).
        const bool inPose = (ctx.playerPose != PlayerPose::Standing);

        // Riding a ferry: carry the player with the deck before block physics.
        Ferry* supportFerry = inPose ? nullptr : findSupportFerry(ctx);
        if (supportFerry)
            ctx.camera.position += supportFerry->velocity * ctx.deltaTime;

        int wfx = (int)floorf(ctx.camera.position.x);
        int wfz = (int)floorf(ctx.camera.position.z);
        int wfy = (int)floorf(ctx.camera.position.y);
        bool inWater = (ctx.world.getBlock(wfx, wfy, wfz)     == BlockType::Water ||
                        ctx.world.getBlock(wfx, wfy + 1, wfz) == BlockType::Water);
        ctx.headUnderwater = (ctx.world.getBlock(wfx,
            (int)floorf(ctx.camera.position.y + 1.6f), wfz) == BlockType::Water);
        if (ctx.playerRig) ctx.playerRig->isSwimming = inWater;

        if (ctx.headUnderwater)
            ctx.breathTime = std::max(0.0f, ctx.breathTime - ctx.deltaTime);
        else
            ctx.breathTime = std::min(30.0f, ctx.breathTime + ctx.deltaTime * 3.0f);

        float hw = PLAYER_WIDTH / 2.0f;
        float ph = PLAYER_HEIGHT * (ctx.playerRig ? ctx.playerRig->heightScale : 1.0f);

        if (inPose) {
            // Position is owned by updatePropInteraction; no physics applies.
            ctx.camera.velocity = glm::vec3(0.0f);
            ctx.camera.onGround = true;
        } else if (ctx.noclip) {
            if (ctx.keyJump) moveDir.y += 1.0f;
            float noclipSpeed = ctx.keySprint ? 45.0f : 15.0f;
            ctx.camera.position += moveDir * noclipSpeed * ctx.deltaTime;
            ctx.camera.velocity  = glm::vec3(0.0f);
        } else if (inWater) {
            ctx.camera.velocity.x = moveDir.x * 5.0f;
            ctx.camera.velocity.z = moveDir.z * 5.0f;
            float diff = (WATER_LEVEL_Y - 0.9f) - ctx.camera.position.y;
            ctx.camera.velocity.y += diff * 4.0f * ctx.deltaTime;
            ctx.camera.velocity.y *= powf(0.88f, ctx.deltaTime * 60.0f);
            ctx.camera.velocity.y  = std::clamp(ctx.camera.velocity.y, -6.0f, 6.0f);
            if (ctx.keyJump)
                ctx.camera.velocity.y = std::max(ctx.camera.velocity.y + 8.0f * ctx.deltaTime, 4.0f);
            ctx.camera.position += ctx.camera.velocity * ctx.deltaTime;
            ctx.camera.position  = resolveCollision(ctx.camera.position, ctx.camera, hw, ph, ctx.world, ctx.deltaTime);
            ctx.camera.onGround  = false;
        } else {
            float walkSpeed = ctx.keySprint ? 20.0f : 10.0f;
            ctx.camera.velocity.x = moveDir.x * walkSpeed;
            ctx.camera.velocity.z = moveDir.z * walkSpeed;
            if (ctx.keyJump && ctx.camera.onGround) ctx.camera.velocity.y = 8.0f;
            ctx.camera.applyGravity(ctx.deltaTime);
            ctx.camera.position = resolveCollision(ctx.camera.position, ctx.camera, hw, ph, ctx.world, ctx.deltaTime);
        }

        // The ferry deck is an object, not blocks, so settle the player onto
        // it after the normal block physics has run.
        if (supportFerry && !ctx.noclip) {
            float deckY = supportFerry->deckTopY();
            if (ctx.camera.position.y <= deckY + 0.05f &&
                ctx.camera.position.y >  deckY - 2.5f &&
                ctx.camera.velocity.y <= 0.1f) {
                ctx.camera.position.y = deckY;
                ctx.camera.velocity.y = 0.0f;
                ctx.camera.onGround   = true;
            }
        }
    }

    int pcx = (int)floorf(ctx.camera.position.x / (float)CHUNK_SIZE);
    int pcz = (int)floorf(ctx.camera.position.z / (float)CHUNK_SIZE);
    ctx.world.update(pcx, pcz);

    // --- Object system: sync remote players, advance every managed object,
    // then drive the local-player wrapper (after physics, so the rig sees
    // this frame's velocity). ---
    syncRemotePlayerObjects(ctx);
    syncFerryObjects(ctx);
    syncNPCObjects(ctx);
    syncAnimalObjects(ctx);
    ctx.objectManager.updateAll(ctx.deltaTime, ctx.world);
    ctx.objectManager.streamProps(ctx.camera.position, 260.0f,
                                  getPropPlacements(), ctx.propLibrary);
    ctx.objectManager.streamDoors(ctx.camera.position, 180.0f,
                                  getDoorPlacements(), ctx.propLibrary,
                                  &ctx.camera.position);
    if (ctx.localPlayer) {
        ctx.localPlayer->position    = ctx.camera.position;
        ctx.localPlayer->yaw         = ctx.playerYaw;
        ctx.localPlayer->lanternHeld = ctx.lanternHeld;
        ctx.localPlayer->update(ctx.deltaTime, ctx.world);
    }

    updatePropInteraction(ctx);
    updateNpcInteraction(ctx);

    if (ctx.state == GameState::Playing && !ctx.paused) {
        updateLeafParticles(ctx);
        updateWeather(ctx);
        updateWeatherParticles(ctx);
        updateAmbientParticles(ctx);
    }

    updateHousePreview(ctx);
}

void sendHousePlacement(AppContext& ctx) {
    if (!ctx.houseModel || !ctx.client) return;
    HouseModel* h = ctx.houseModel;
    glm::ivec3 mn = h->boundMin, mx = h->boundMax;
    int sx = mx.x - mn.x + 1, sy = mx.y - mn.y + 1, sz = mx.z - mn.z + 1;
    if (sx <= 0 || sy <= 0 || sz <= 0) return;

    // Rotate the design by the snapped yaw quadrant (matches glm rotateY).
    int q = ((int)lroundf(ctx.housePreviewYaw / 90.0f)) & 3;
    int dimX = (q % 2 == 0) ? sx : sz;
    int dimZ = (q % 2 == 0) ? sz : sx;
    int dimY = sy;

    std::vector<uint8_t> blocks((size_t)dimX * dimY * dimZ, (uint8_t)BlockType::Air);
    auto outIdx = [&](int x, int y, int z) {
        return ((size_t)y * dimZ + z) * dimX + x;
    };
    for (int y = 0; y < sy; y++)
        for (int z = 0; z < sz; z++)
            for (int x = 0; x < sx; x++) {
                BlockType bt = h->get(mn.x + x, mn.y + y, mn.z + z);
                int rx, rz;
                switch (q) {
                    case 1:  rx = z;          rz = sx - 1 - x; break;
                    case 2:  rx = sx - 1 - x; rz = sz - 1 - z; break;
                    case 3:  rx = sz - 1 - z; rz = x;          break;
                    default: rx = x;          rz = z;          break;
                }
                blocks[outIdx(rx, y, rz)] = (uint8_t)bt;
            }

    HousePlaceHeader hdr;
    hdr.worldX = (int)floorf(ctx.housePreviewPos.x) - dimX / 2;
    hdr.baseY  = (int)floorf(ctx.housePreviewPos.y);
    hdr.worldZ = (int)floorf(ctx.housePreviewPos.z) - dimZ / 2;
    hdr.dimX = dimX; hdr.dimY = dimY; hdr.dimZ = dimZ;

    std::vector<uint8_t> packet(sizeof(hdr) + blocks.size());
    memcpy(packet.data(), &hdr, sizeof(hdr));
    memcpy(packet.data() + sizeof(hdr), blocks.data(), blocks.size());
    ctx.client->send(PacketType::HousePlace, packet.data(), packet.size());
}
