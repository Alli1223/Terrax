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
#include "gameplay_internal.h"

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
        sendPlayerModelUpdate(ctx);
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

    // Active gameplay = Playing with no blocking overlay up. Excluding the
    // inventory / loadout / map here stops mouse clicks meant for those
    // screens from leaking into combat (swings, casts, heal-zone drops).
    const bool gameplayActive = (ctx.state == GameState::Playing && !ctx.chatOpen
                                 && !ctx.showInventory && !ctx.showCharacterLoadout
                                 && !ctx.showMap);

    // Combat input — branches by equipped main-hand weapon.
    //   * Bow: left mouse held charges the shot (rig.bowDrawAmount), and
    //     releasing fires once with damage scaled by the charge level.
    //     Sub-15% charges are aborted (mis-fire prevention).
    //   * Melee/none: keep the original click-to-swing behaviour.
    // Shield blocking is independent — driven by right-mouse in input.cpp.
    bool leftHeld = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    Item* mainHand = ctx.inventory.equipped(EquipSlot::MainHand);
    bool hasBow = mainHand
        && mainHand->getKind() == ItemKind::Weapon
        && static_cast<WeaponItem*>(mainHand)->getType() == WeaponType::Bow;

    if (gameplayActive && hasBow) {
        if (leftHeld) {
            if (!ctx.bowChargingHeld) ctx.bowCharge = 0.0f;
            ctx.bowChargingHeld = true;
            // ~1.4 seconds to a full draw — long enough that the player
            // sees the bar fill, short enough not to feel sluggish.
            ctx.bowCharge = std::min(1.0f, ctx.bowCharge + ctx.deltaTime * 0.7f);
        } else if (ctx.bowChargingHeld) {
            ctx.bowChargingHeld = false;
            if (ctx.bowCharge > 0.15f) {
                ctx.playerRig->isAttacking = true;
                ctx.playerRig->attackAnim  = 0.0f;
                NPC* target = findRangedTargetNpc(ctx);
                // Damage scales 0.5..2.5 across the charge range so a
                // full draw is roughly 2.5x a melee hit.
                float scale = 0.5f + ctx.bowCharge * 2.0f;
                PlayerAttackPacket ap {};
                ap.clientID    = ctx.client->clientID;
                ap.targetNpcId = target ? target->id : 0u;
                ap.damageScale = scale;
                ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));

                // Spawn the visible arrow. Damage is already applied
                // server-side via the packet above — this projectile is
                // the cosmetic shaft you see flying through the air.
                auto arrow = std::make_unique<ArrowProjectile>();
                // `ctx.camera.position` is the player's feet — spawn at
                // chest height (+1.5 blocks) and slightly forward so
                // the arrow leaves from the bow, not from the ground.
                glm::vec3 spawnPos = ctx.camera.position
                                   + glm::vec3(0.0f, 1.5f, 0.0f)
                                   + ctx.camera.front * 0.5f;
                arrow->position = spawnPos;
                // Initial speed scales with charge. ~22 m/s on a min
                // shot, ~55 m/s on a max draw — fast enough to feel
                // snappy, slow enough that the drop is visible.
                float speed = 22.0f + ctx.bowCharge * 33.0f;
                arrow->velocity      = ctx.camera.front * speed;
                arrow->restingDir    = glm::normalize(ctx.camera.front);
                arrow->ownerClientId = ctx.client->clientID;
                arrow->damageScale   = scale;
                ctx.objectManager.add(std::move(arrow));
                if (g_audio) g_audio->play2D(SoundId::BowShot, 0.55f);
            }
            ctx.bowCharge = 0.0f;
        }
        ctx.playerRig->bowDrawAmount = ctx.bowChargingHeld ? ctx.bowCharge : 0.0f;
    } else {
        // Reset bow state when not wielding one — avoids the rig getting
        // stuck in a draw pose after swapping weapons mid-charge.
        ctx.bowChargingHeld = false;
        ctx.bowCharge       = 0.0f;
        ctx.playerRig->bowDrawAmount = 0.0f;

        if (gameplayActive && leftHeld
            && !ctx.playerRig->isAttacking && !ctx.playerRig->isCasting) {
            // Polymorphic dispatch — the weapon decides what an attack
            // does. Staff overrides to fire a magic bolt; default
            // (sword/axe/no-weapon) sends a melee PlayerAttackPacket.
            WeaponItem* w = (mainHand
                              && mainHand->getKind() == ItemKind::Weapon)
                            ? static_cast<WeaponItem*>(mainHand) : nullptr;
            // Pick the animation flag based on the weapon: casters
            // raise their arms ("spell pose"), everything else does
            // the right-arm sword swing.
            if (w && w->usesCastAnimation()) {
                ctx.playerRig->isCasting = true;
                ctx.playerRig->castAnim  = 0.0f;
                if (g_audio) g_audio->play2D(SoundId::MagicCast, 0.6f);
            } else {
                ctx.playerRig->isAttacking = true;
                ctx.playerRig->attackAnim  = 0.0f;
            }
            if (w) {
                float range = w->isInstantRanged() ? w->attackRange() : 3.8f;
                float cone  = w->isInstantRanged() ? w->attackFacing() : 0.3f;
                NPC* target = findTargetNpc(ctx, range, cone);
                w->onPrimaryAttack(ctx, 1.0f, target);
                if (g_audio && !w->usesCastAnimation()) {
                    g_audio->play2D(SoundId::Swing, 0.45f);
                    if (target) g_audio->play2D(SoundId::MeleeHit, 0.5f);
                }
            } else {
                NPC* target = findMeleeTargetNpc(ctx);
                PlayerAttackPacket ap {};
                ap.clientID    = ctx.client->clientID;
                ap.targetNpcId = target ? target->id : 0u;
                ap.damageScale = 1.0f;
                ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));
                if (g_audio) {
                    g_audio->play2D(SoundId::Swing, 0.45f);
                    if (target) g_audio->play2D(SoundId::MeleeHit, 0.5f);
                }
            }
        }
    }

    // --- Healing staff cooldowns + secondary (right mouse) ----------------
    // Tick the ability cooldowns every frame regardless of what's held.
    if (ctx.healCdPrimary   > 0.0f) ctx.healCdPrimary   -= ctx.deltaTime;
    if (ctx.healCdSecondary > 0.0f) ctx.healCdSecondary -= ctx.deltaTime;

    // Right mouse triggers a weapon's secondary attack (the healing staff's
    // AOE). Edge-detected so one press drops one zone; input.cpp suppresses
    // block placement while a secondary-capable weapon is held.
    bool rightHeld = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    if (gameplayActive && rightHeld && !ctx.rmbWasDown
        && mainHand && mainHand->getKind() == ItemKind::Weapon) {
        WeaponItem* sw = static_cast<WeaponItem*>(mainHand);
        if (sw->hasSecondaryAttack() && ctx.healCdSecondary <= 0.0f
            && !ctx.playerRig->isCasting && !ctx.playerRig->isAttacking) {
            ctx.playerRig->isCasting = true;
            ctx.playerRig->castAnim  = 0.0f;
            sw->onSecondaryAttack(ctx);
        }
    }
    ctx.rmbWasDown = rightHeld;

    // Mirror the shield-block state into the rig so the animation kicks
    // in and the damage filter below sees it.
    ctx.playerRig->isBlocking = ctx.shieldRaised
        && ctx.inventory.equipped(EquipSlot::OffHand) != nullptr;

    ctx.client->update(ctx.world, ctx.remotePlayers);
    drainSpellEvents(ctx);    // apply/show heals + spawn cosmetic spell fx
    updateHealZones(ctx);     // advance heal sanctuaries (owner pulses health)
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
            p.lanternHeld  = ctx.lanternHeld ? 1 : 0;
            p.shieldRaised = ctx.shieldRaised ? 1 : 0;
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
    syncLootDrops(ctx);

    // Age HUD toasts and prune expired ones. syncNPCObjects (above) is the
    // most common source of new toasts via awardEnemyKill().
    for (auto& t : ctx.toasts) t.lifeTime -= ctx.deltaTime;
    ctx.toasts.erase(
        std::remove_if(ctx.toasts.begin(), ctx.toasts.end(),
                       [](const AppContext::HudToast& t){ return t.lifeTime <= 0.0f; }),
        ctx.toasts.end());

    // Update death-explosion voxel particles. Each falls under gravity and
    // sticks to the first solid block its centre crosses, then fades over
    // its remaining lifetime.
    {
        const float dt = ctx.deltaTime;
        for (auto& p : ctx.voxelParticles) {
            p.life -= dt;
            if (p.life <= 0.0f) continue;
            if (p.grounded) continue;

            p.vel.y -= 16.0f * dt;
            glm::vec3 next = p.pos + p.vel * dt;

            int bx = (int)std::floor(next.x);
            int by = (int)std::floor(next.y);
            int bz = (int)std::floor(next.z);
            BlockType b = ctx.world.getBlock(bx, by, bz);
            if (b != BlockType::Air && b != BlockType::Water) {
                next.y = (float)(by + 1);
                p.vel  = glm::vec3(0.0f);
                p.grounded = true;
            }
            p.pos = next;
        }
        ctx.voxelParticles.erase(
            std::remove_if(ctx.voxelParticles.begin(), ctx.voxelParticles.end(),
                [](const VoxelDeathParticle& p){ return p.life <= 0.0f; }),
            ctx.voxelParticles.end());
    }
    ctx.objectManager.updateAll(ctx.deltaTime, ctx.world);

    // Sweep flying projectiles against NPCs in a separate pass — keeps
    // the projectile class self-contained (no ObjectManager back-ref)
    // while still letting it react via virtual hooks. See projectile.cpp.
    updateProjectileCollisions(ctx);
    ctx.objectManager.streamProps(ctx.camera.position, 260.0f,
                                  getPropPlacements(), ctx.propLibrary);
    ctx.objectManager.streamWildProps(ctx.camera.position, 96.0f, ctx.propLibrary);
    ctx.objectManager.streamDoors(ctx.camera.position, 180.0f,
                                  getDoorPlacements(), ctx.propLibrary,
                                  &ctx.camera.position);
    if (ctx.localPlayer) {
        ctx.localPlayer->position    = ctx.camera.position;
        ctx.localPlayer->yaw         = ctx.playerYaw;
        ctx.localPlayer->lanternHeld = ctx.lanternHeld;
        ctx.localPlayer->update(ctx.deltaTime, ctx.world);
    }

    updateLootPickup(ctx);
    updatePropInteraction(ctx);
    updateNpcInteraction(ctx);

    if (ctx.state == GameState::Playing && !ctx.paused) {
        updateLeafParticles(ctx);
        updateWeather(ctx);
        updateWeatherParticles(ctx);
        updateAmbientParticles(ctx);
        updateAudio(ctx);
    }

    updateHousePreview(ctx);
}

void sendPlayerModelUpdate(AppContext& ctx) {
    if (!ctx.client || !ctx.client->connected || !ctx.playerRig) return;
    PlayerModelHeader mh{};
    mh.clientID     = ctx.client->clientID;
    mh.hairStyle    = ctx.playerRig->hairStyle;
    mh.hairColor    = ctx.playerRig->hairColor;
    mh.eyeColor     = ctx.playerRig->eyeColor;
    mh.eyeType      = ctx.playerRig->eyeType;
    mh.noseStyle    = ctx.playerRig->noseStyle;
    mh.eyebrowStyle = ctx.playerRig->eyebrowStyle;
    mh.earType      = ctx.playerRig->earType;
    mh.armorType    = 0;   // legacy field, replaced by `slots`
    mh.playerLevel  = ctx.playerLevel;
    fillSlotsFromInventory(mh.slots, ctx.inventory);
    ctx.client->send(PacketType::PlayerModel, &mh, sizeof(mh));
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
