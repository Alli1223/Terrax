#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gameplay.h"
#include "app_context.h"
#include "physics.h"
#include "network.h"
#include "game_session.h"
#include "character_save.h"
#include "town.h"
#include "dungeon.h"
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
    // Persist the active roster character's progression (level, skills, hotbar,
    // appearance) before tearing the session down. Only Join sessions have an
    // active roster slot; Host/Singleplayer (activeCharacter == -1) are skipped.
    if (ctx.sessionMode == SessionMode::Join && ctx.activeCharacter >= 0) {
        std::vector<CharacterSave> roster = loadRoster();
        if (ctx.activeCharacter < (int)roster.size()) {
            roster[ctx.activeCharacter] = captureCharacterFromContext(ctx);
            saveRoster(roster);
        }
    }
    ctx.activeCharacter = -1;

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
    ctx.worldSeedApplied  = false;
    ctx.paused            = false;
    ctx.chatOpen          = false;
    ctx.showPlayerList    = false;
    ctx.showTrainer       = false;
    ctx.showQuestGiver    = false;
    ctx.showVendor        = false;
    ctx.showStable        = false;
    ctx.showQuestLog      = false;
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
                                 && !ctx.showMap && !ctx.showTrainer && !ctx.showQuestGiver
                                 && !ctx.showVendor && !ctx.showStable && !ctx.showStash
                                 && !ctx.showQuestLog);

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
                // A bright spark at the point of contact sells the melee hit.
                if (target && !w->usesCastAnimation())
                    spawnAbilityFx(ctx, target->position + glm::vec3(0.0f, 1.0f, 0.0f), 0.6f, 15);
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
                if (target)
                    spawnAbilityFx(ctx, target->position + glm::vec3(0.0f, 1.0f, 0.0f), 0.6f, 15);
            }
        }
    }

    // --- Healing staff cooldowns + secondary (right mouse) ----------------
    // Tick the ability cooldowns every frame regardless of what's held.
    if (ctx.healCdPrimary   > 0.0f) ctx.healCdPrimary   -= ctx.deltaTime;
    if (ctx.healCdSecondary > 0.0f) ctx.healCdSecondary -= ctx.deltaTime;

    // --- Ability hotbar: cooldowns, resource regen, buff decay, activation --
    for (int i = 0; i < AppContext::HOTBAR_SLOTS; i++)
        if (ctx.hotbarCooldown[i] > 0.0f) ctx.hotbarCooldown[i] -= ctx.deltaTime;
    ctx.resource = std::min(ctx.resourceMax, ctx.resource + ctx.resourceRegenPerSec * ctx.deltaTime);
    for (auto& b : ctx.activeBuffs) b.ttl -= ctx.deltaTime;
    ctx.activeBuffs.erase(
        std::remove_if(ctx.activeBuffs.begin(), ctx.activeBuffs.end(),
                       [](const ActiveBuff& b){ return b.ttl <= 0.0f; }),
        ctx.activeBuffs.end());
    updateBuffAura(ctx);   // ongoing aura around the player while a buff is up
    // Advance an in-progress channelled cast; the ability fires on completion.
    if (ctx.castTimer > 0.0f) {
        ctx.castTimer -= ctx.deltaTime;
        if (ctx.playerRig) ctx.playerRig->isCasting = true;   // hold the cast pose
        if (ctx.castTimer <= 0.0f) {
            ctx.castTimer = 0.0f;
            AbilityId fired = ctx.castingAbility;
            ctx.castingAbility = AbilityId::None;
            if (Ability* ab = ctx.findAbility(fired)) ab->activate(ctx);
        }
    }
    if (gameplayActive && ctx.pendingHotbarSlot >= 0)
        tryActivateHotbar(ctx, ctx.pendingHotbarSlot);
    ctx.pendingHotbarSlot = -1;

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

    // The world is server-authoritative. A joining client streams terrain blocks
    // from the server, but it still builds a lot of content locally from the
    // world seed (the town plan, props, vegetation, NPC placement, the map). That
    // seed MUST be the server's, or the local content diverges from the streamed
    // terrain — which is exactly the "client generates a different world" desync.
    // Adopt the server's seed the moment the handshake delivers it. Every
    // seed-derived cache (town plan, props, dungeon plan, ferry routes) is
    // seed-versioned, so it rebuilds itself on the next access once the seed
    // changes — no explicit invalidation needed. A host shares the server's seed
    // in-process already, and re-seeding there would race the running server's
    // chunk-generation threads, so it's skipped.
    if (!ctx.weOwnServer && !ctx.worldSeedApplied && ctx.client->hasWorldSeed) {
        setWorldSeed(ctx.client->serverWorldSeed);
        ctx.worldSeedApplied = true;
        std::cout << "[Client] Adopted server world seed "
                  << ctx.client->serverWorldSeed << "\n";
    }
    // Until the seed is in hand, don't request chunks or place props — they'd be
    // built from the wrong seed. The network keeps draining each frame (above),
    // so the handshake still arrives and normal play resumes next frame.
    if (!ctx.weOwnServer && !ctx.worldSeedApplied) return;

    drainSpellEvents(ctx);    // apply/show heals + spawn cosmetic spell fx
    syncEnemyProjectiles(ctx);  // spawn visible bolts for enemy ranged attacks
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
            p.vehicleKind  = (uint8_t)ctx.activeVehicle;
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

    // Lean the body into its movement. The character always turns to face the
    // direction it's moving (above), so it's ALWAYS running "forwards" — pressing
    // S spins it around to face the camera, it doesn't reverse it. So the forward
    // lean is driven by movement *magnitude* (always lean forward when moving),
    // never the signed W/S input — otherwise walking toward the camera leans the
    // body backwards. A/D still bank the character into the turn.
    if (gameplayActive && ctx.playerRig) {
        float spd     = ctx.keySprint ? 20.0f : 10.0f;
        float fwdIn   = (float)(ctx.keyFwd   - ctx.keyBack);
        float rightIn = (float)(ctx.keyRight - ctx.keyLeft);
        float moveMag = sqrtf(fwdIn * fwdIn + rightIn * rightIn);   // 0 when still, ~1.4 diagonal
        ctx.playerRig->updateLean(moveMag * spd, rightIn * spd, ctx.deltaTime);
    }

    if (gameplayActive) {
        // While seated / lying down the player is locked to the pose anchor —
        // skip all movement physics (it would fight the snap-to-anchor inside
        // updatePropInteraction below).
        const bool inPose = (ctx.playerPose != PlayerPose::Standing);

        // --- Dodge roll (V): a quick directional dash with brief i-frames ----
        if (ctx.rollCooldown > 0.0f) ctx.rollCooldown -= ctx.deltaTime;
        if (ctx.rollPressed && !inPose && ctx.camera.onGround &&
            ctx.rollCooldown <= 0.0f && ctx.rollTimer <= 0.0f) {
            glm::vec3 dir = (glm::length(moveDir) > 0.001f) ? moveDir : camForward;
            ctx.rollDir      = glm::normalize(glm::vec3(dir.x, 0.0f, dir.z));
            ctx.rollTimer    = 0.5f;     // dash + damage-immunity window
            ctx.rollCooldown = 1.0f;     // 1 s before the next roll
            ctx.playerYaw    = glm::degrees(atan2f(ctx.rollDir.x, ctx.rollDir.z));
            if (ctx.playerRig) ctx.playerRig->rollProgress = 0.0f;
            if (g_audio) g_audio->play2D(SoundId::Swing, 0.45f);
        }
        ctx.rollPressed = false;
        if (ctx.rollTimer > 0.0f) {
            ctx.rollTimer -= ctx.deltaTime;
            // Steer the roll toward the current input so it curves with you — if
            // you start pressing left mid-roll, the dodge veers left, not straight.
            if (glm::length(moveDir) > 0.001f) {
                glm::vec3 want = glm::normalize(glm::vec3(moveDir.x, 0.0f, moveDir.z));
                ctx.rollDir   = glm::normalize(glm::mix(ctx.rollDir, want, std::min(1.0f, ctx.deltaTime * 12.0f)));
                ctx.playerYaw = glm::degrees(atan2f(ctx.rollDir.x, ctx.rollDir.z));
            }
            if (ctx.playerRig)
                ctx.playerRig->rollProgress = (ctx.rollTimer > 0.0f)
                    ? std::clamp(1.0f - ctx.rollTimer / 0.5f, 0.0f, 1.0f) : -1.0f;
        }

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
            if (ctx.activeVehicle == VehicleKind::Horse)        // mounted — gallop
                walkSpeed = ctx.keySprint ? 38.0f : 22.0f;
            if (ctx.castTimer > 0.0f) walkSpeed *= 0.35f;       // slowed while channelling a cast
            // Kite glide: while airborne with a kite, sail forward in the facing
            // direction (movement keys steer by turning playerYaw) and clamp the
            // descent to a slow, steady sink — so jumping off a ledge soars.
            bool kiteGliding = (ctx.activeVehicle == VehicleKind::Kite &&
                                !ctx.camera.onGround && ctx.rollTimer <= 0.0f);
            if (ctx.rollTimer > 0.0f) {                          // dodge roll: dash in the locked dir
                ctx.camera.velocity.x = ctx.rollDir.x * 13.0f;
                ctx.camera.velocity.z = ctx.rollDir.z * 13.0f;
            } else if (kiteGliding) {
                float glideSpeed = ctx.keySprint ? 20.0f : 15.0f;
                glm::vec3 look(sinf(glm::radians(ctx.playerYaw)), 0.0f,
                               cosf(glm::radians(ctx.playerYaw)));
                ctx.camera.velocity.x = look.x * glideSpeed;
                ctx.camera.velocity.z = look.z * glideSpeed;
            } else {
                ctx.camera.velocity.x = moveDir.x * walkSpeed;
                ctx.camera.velocity.z = moveDir.z * walkSpeed;
            }
            if (ctx.keyJump && ctx.camera.onGround && ctx.rollTimer <= 0.0f) ctx.camera.velocity.y = 8.0f;
            ctx.camera.applyGravity(ctx.deltaTime);
            if (kiteGliding && ctx.camera.velocity.y < -4.0f)   // slow the fall to a glide
                ctx.camera.velocity.y = -4.0f;
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

    // Tick the shared consumable cooldown down toward ready.
    if (ctx.potionCooldown > 0.0f) ctx.potionCooldown -= ctx.deltaTime;

    // Age cleared-dungeon timers; when one lapses the dungeon has re-populated, so
    // drop the "Cleared" marker (the map reverts to "available") and announce it.
    for (auto it = ctx.clearedDungeons.begin(); it != ctx.clearedDungeons.end(); ) {
        it->second -= ctx.deltaTime;
        if (it->second <= 0.0f) {
            const DungeonPlan& dp = getDungeonPlan();
            if (it->first >= 0 && it->first < (int)dp.dungeons.size())
                ctx.toasts.push_back({ dp.dungeons[it->first]->name + " has repopulated",
                                       Voxel{200, 220, 160, 255}, 4.0f });
            it = ctx.clearedDungeons.erase(it);
        } else ++it;
    }

    // Age + prune floating combat-text numbers (spawned in syncNPCObjects).
    for (auto& f : ctx.floatingTexts) f.age += ctx.deltaTime;
    ctx.floatingTexts.erase(
        std::remove_if(ctx.floatingTexts.begin(), ctx.floatingTexts.end(),
                       [](const AppContext::FloatingText& f){ return f.age >= f.life; }),
        ctx.floatingTexts.end());

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
    // Ease the step-up offset back to zero so a one-block climb glides instead
    // of snapping. The same offset is applied to the third-person camera in the
    // renderer, so the body and camera rise together. (Offset is always <= 0.)
    ctx.camera.stepSmoothOffset -= ctx.camera.stepSmoothOffset
                                 * std::min(1.0f, ctx.deltaTime * 12.0f);
    if (ctx.camera.stepSmoothOffset > -0.001f) ctx.camera.stepSmoothOffset = 0.0f;

    if (ctx.localPlayer) {
        ctx.localPlayer->position    = ctx.camera.position
                                     + glm::vec3(0.0f, ctx.camera.stepSmoothOffset, 0.0f);
        ctx.localPlayer->yaw         = ctx.playerYaw;
        ctx.localPlayer->lanternHeld = ctx.lanternHeld;
        ctx.localPlayer->vehicleKind = ctx.activeVehicle;
        // Pulled wagon: lerp the cart a few metres behind the player so it
        // follows smoothly instead of snapping when you turn.
        if (ctx.activeVehicle == VehicleKind::Wagon) {
            float yr = glm::radians(ctx.playerYaw);
            glm::vec3 fwd(sinf(yr), 0.0f, cosf(yr));
            glm::vec3 target = ctx.localPlayer->position - fwd * 3.0f;
            if (!ctx.wagonPosInit) {
                ctx.wagonPos = target; ctx.wagonYaw = ctx.playerYaw; ctx.wagonPosInit = true;
            } else {
                float k = std::min(1.0f, 4.0f * ctx.deltaTime);
                ctx.wagonPos = glm::mix(ctx.wagonPos, target, k);
                float dy = ctx.playerYaw - ctx.wagonYaw;
                while (dy > 180.0f) dy -= 360.0f;
                while (dy < -180.0f) dy += 360.0f;
                ctx.wagonYaw += dy * k;
            }
            ctx.localPlayer->trailPos  = ctx.wagonPos;
            ctx.localPlayer->trailYaw  = ctx.wagonYaw;
            ctx.localPlayer->trailInit = true;
        } else {
            ctx.wagonPosInit = false;
        }
        ctx.localPlayer->update(ctx.deltaTime, ctx.world);
    }

    updateLootPickup(ctx);
    updatePropInteraction(ctx);
    updateNpcInteraction(ctx);
    updateTargeting(ctx);

    // Region danger warning: when the local foes far outlevel the player, warn
    // them periodically — reinforcing "level up before venturing further out".
    {
        static float dangerWarnTimer = 0.0f;
        dangerWarnTimer -= ctx.deltaTime;
        int foeLvl = enemyLevelForTier(dangerTierAt(ctx.camera.position.x,
                                                    ctx.camera.position.z));
        if (foeLvl - ctx.playerLevel >= 8 && dangerWarnTimer <= 0.0f) {
            AppContext::HudToast t{ "WARNING: foes here are around level "
                + std::to_string(foeLvl) + " - far above you",
                Voxel{255, 90, 70, 255}, 4.0f };
            ctx.toasts.push_back(std::move(t));
            dangerWarnTimer = 18.0f;   // don't nag more than ~every 18s
        }
    }

    // Dungeon discovery: announce the first time the player crosses into a
    // dungeon's footprint — a little "zone discovered" moment.
    {
        const DungeonPlan& dp = getDungeonPlan();
        int px = (int)ctx.camera.position.x, pz = (int)ctx.camera.position.z;
        for (size_t i = 0; i < dp.dungeons.size(); ++i) {
            if (ctx.discoveredDungeons.count((int)i)) continue;
            const Dungeon& d = *dp.dungeons[i];
            if (px < d.bbMin.x || px > d.bbMax.x || pz < d.bbMin.y || pz > d.bbMax.y) continue;
            ctx.discoveredDungeons.insert((int)i);
            AppContext::HudToast t{ std::string("Discovered: ") + d.name,
                                    Voxel{255, 225, 130, 255}, 5.0f };
            ctx.toasts.push_back(std::move(t));
            break;   // at most one announcement per frame
        }
    }

    // Explore / Deliver quests: complete the moment the player reaches the target
    // region/dungeon (Explore) or destination town (Deliver).
    {
        float px = ctx.camera.position.x, pz = ctx.camera.position.z;
        for (Quest& q : ctx.activeQuests) {
            if (!questExploreReached(q, px, pz) && !questDeliverReached(q, px, pz)) continue;
            q.progress = q.requiredCount;
            q.status   = QuestStatus::Complete;
            ctx.toasts.push_back({ std::string("Quest complete: ") + q.title
                                       + " - return to a quest giver",
                                   Voxel{120, 230, 140, 255}, 5.0f });
        }
    }

    // The Class Trainer window opens by pressing E near a trainer (above), not
    // via a key toggle, so reconcile the cursor with its state here: free it when
    // the window opens, recapture it when it closes (Close button / X / Esc).
    {
        static bool prevShowTrainer = false;
        if (ctx.showTrainer != prevShowTrainer) {
            glfwSetInputMode(window, GLFW_CURSOR,
                             ctx.showTrainer ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (ctx.showTrainer) ctx.keyFwd = ctx.keyBack = ctx.keyLeft = ctx.keyRight = ctx.keyJump = 0;
            else                 ctx.firstMouse = true;
            prevShowTrainer = ctx.showTrainer;
        }
        static bool prevShowQuestGiver = false;
        if (ctx.showQuestGiver != prevShowQuestGiver) {
            glfwSetInputMode(window, GLFW_CURSOR,
                             ctx.showQuestGiver ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (ctx.showQuestGiver) ctx.keyFwd = ctx.keyBack = ctx.keyLeft = ctx.keyRight = ctx.keyJump = 0;
            else                    ctx.firstMouse = true;
            prevShowQuestGiver = ctx.showQuestGiver;
        }
        static bool prevShowVendor = false;
        if (ctx.showVendor != prevShowVendor) {
            glfwSetInputMode(window, GLFW_CURSOR,
                             ctx.showVendor ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (ctx.showVendor) ctx.keyFwd = ctx.keyBack = ctx.keyLeft = ctx.keyRight = ctx.keyJump = 0;
            else                ctx.firstMouse = true;
            prevShowVendor = ctx.showVendor;
        }
        static bool prevShowStable = false;
        if (ctx.showStable != prevShowStable) {
            glfwSetInputMode(window, GLFW_CURSOR,
                             ctx.showStable ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (ctx.showStable) ctx.keyFwd = ctx.keyBack = ctx.keyLeft = ctx.keyRight = ctx.keyJump = 0;
            else                ctx.firstMouse = true;
            prevShowStable = ctx.showStable;
        }
        static bool prevShowStash = false;
        if (ctx.showStash != prevShowStash) {
            glfwSetInputMode(window, GLFW_CURSOR,
                             ctx.showStash ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (ctx.showStash) ctx.keyFwd = ctx.keyBack = ctx.keyLeft = ctx.keyRight = ctx.keyJump = 0;
            else               ctx.firstMouse = true;
            prevShowStash = ctx.showStash;
        }
    }

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
    mh.armorType    = (int)ctx.playerRole;   // legacy field repurposed to carry role
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
