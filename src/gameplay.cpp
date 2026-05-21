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
#include <cstring>
#include <mutex>
#include <random>

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
    }

    int pcx = (int)floorf(ctx.camera.position.x / (float)CHUNK_SIZE);
    int pcz = (int)floorf(ctx.camera.position.z / (float)CHUNK_SIZE);
    ctx.world.update(pcx, pcz);

    if (ctx.state == GameState::Playing && !ctx.paused)
        updateLeafParticles(ctx);

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
