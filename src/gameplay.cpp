#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gameplay.h"
#include "app_context.h"
#include "physics.h"
#include "network.h"
#include "game_session.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <cmath>
#include <mutex>

static void cleanupRemotePlayers(AppContext& ctx) {
    for (auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        if (p.rig) delete p.rig;
    }
    ctx.remotePlayers.clear();
}

void disconnectFromGame(AppContext& ctx) {
    if (ctx.client) {
        ctx.client->disconnect();
        delete ctx.client;
        ctx.client = nullptr;
    }
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
        std::lock_guard<std::mutex> lock(ctx.world.chunksMutex);
        auto it = ctx.world.chunks.find({0, 0});
        if (it != ctx.world.chunks.end() && it->second->state != ChunkState::Empty) {
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                if (it->second->get(8, y, 8) != BlockType::Air) {
                    ctx.camera.position = glm::vec3(8.5f, (float)y + 1.0f, 8.5f);
                    ctx.noclip          = false;
                    ctx.spawnedOnGround = true;
                    std::cout << "[Client] Spawned on ground at Y=" << y << "\n";
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
            PlayerAttackPacket ap { ctx.client->clientID };
            ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));
        }
    }

    ctx.client->update(ctx.world, ctx.remotePlayers);

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

        if (ctx.noclip) {
            if (ctx.keyJump) moveDir.y += 1.0f;
            ctx.camera.position += moveDir * 15.0f * ctx.deltaTime;
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
            ctx.camera.position  = resolveCollision(ctx.camera.position, ctx.camera, hw, ph, ctx.world);
            ctx.camera.onGround  = false;
        } else {
            ctx.camera.velocity.x = moveDir.x * 10.0f;
            ctx.camera.velocity.z = moveDir.z * 10.0f;
            if (ctx.keyJump && ctx.camera.onGround) ctx.camera.velocity.y = 8.0f;
            ctx.camera.applyGravity(ctx.deltaTime);
            ctx.camera.position = resolveCollision(ctx.camera.position, ctx.camera, hw, ph, ctx.world);
        }
    }

    int pcx = (int)floorf(ctx.camera.position.x / (float)CHUNK_SIZE);
    int pcz = (int)floorf(ctx.camera.position.z / (float)CHUNK_SIZE);
    ctx.world.update(pcx, pcz);
}
