#include "game_session.h"
#include "game_types.h"
#include "network.h"
#include "world.h"
#include "vehicle.h"
#include "ferry_routes.h"
#include "npc.h"
#include "farm_director.h"
#include "animal.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include <algorithm>

static std::thread       g_serverThread;
static std::atomic<bool> g_serverThreadRunning{false};
static std::atomic<int>  g_serverRenderDistance{DEFAULT_RENDER_DISTANCE};
static float             g_serverGameTime = 0.3f;

std::atomic<bool> g_serverDayTimeSync{false};
ServerStats       g_serverStats;

static void serverThreadMain(unsigned short port) {
    std::cout << "Starting Terrax Server on port " << port << "..." << std::endl;
    g_server = new NetworkServer(port);
    World serverWorld(true);
    serverWorld.renderDistance = g_serverRenderDistance.load();
    serverWorld.generate(0, 0);

    // Server-authoritative ferries — one per wide highway water crossing.
    std::vector<std::unique_ptr<Ferry>> ferries;
    {
        const std::vector<FerryRoute>& routes = getFerryRoutes();
        uint32_t fid = 1000000u;   // entity id space, disjoint from client ids
        for (int i = 0; i < (int)routes.size(); i++) {
            auto f = std::make_unique<Ferry>();
            f->id         = fid++;
            f->routeIndex = i;
            ferries.push_back(std::move(f));
        }
    }

    // Server-authoritative NPCs — villagers streamed in and out by proximity.
    NpcDirector npcDirector;

    // Server-authoritative wildlife — animals roaming around the players.
    AnimalDirector animalDirector;

    // Server-authoritative crop growth. Farmer NPCs (owned by npcDirector) work
    // its fields, so wire the two together.
    FarmDirector farmDirector;
    npcDirector.farmDir = &farmDirector;

    auto lastWall = std::chrono::high_resolution_clock::now();
    float accumulator = 0.0f;
    g_serverStats.running.store(true);

    while (g_serverThreadRunning.load()) {
        auto now = std::chrono::high_resolution_clock::now();
        float frameDt = std::chrono::duration<float>(now - lastWall).count();
        lastWall = now;
        frameDt = std::min(frameDt, 0.25f);

        accumulator += frameDt;
        while (accumulator >= SERVER_TICK_DT) {
            auto tickT0 = std::chrono::high_resolution_clock::now();
            auto players = g_server->getPlayerStates();
            // Keep chunks loaded around ALL players at once. Calling update() per
            // player would make each player's call evict every other player's
            // chunks, thrashing generation (heap corruption / crash on join).
            std::vector<ChunkPos> centers;
            centers.reserve(players.size());
            for (auto& p : players) {
                int pcx = (int)floorf(p.pos.x / (float)CHUNK_SIZE);
                int pcz = (int)floorf(p.pos.z / (float)CHUNK_SIZE);
                centers.push_back({ pcx, pcz });
            }
            serverWorld.updateForPlayers(centers);
            g_server->update(serverWorld);

            for (auto& f : ferries) {
                f->serverStep(SERVER_TICK_DT);
                EntityStatePacket ep{};
                ep.entityId = f->id;
                ep.kind     = (uint8_t)ObjectKind::Vehicle;
                ep.subType  = 0;
                ep.x = f->position.x; ep.y = f->position.y; ep.z = f->position.z;
                ep.yaw = f->yaw;
                ep.vx = f->velocity.x; ep.vy = f->velocity.y; ep.vz = f->velocity.z;
                g_server->broadcast(PacketType::EntityState, &ep, sizeof(ep));
            }

            {
                std::vector<DirectorPlayer> dirPlayers;
                dirPlayers.reserve(players.size());
                for (auto& p : players) dirPlayers.push_back({ p.id, p.pos });

                // Grow crops first so farmers act on fresh field state.
                farmDirector.update(SERVER_TICK_DT, dirPlayers, serverWorld);

                // Resolve melee hits clients landed on NPCs this tick.
                for (const NpcHitEvent& hit : g_server->npcHits)
                    npcDirector.playerHitNpc(hit.attackerId, hit.npcId,
                                             g_server->getPlayerPosition(hit.attackerId),
                                             hit.damageScale);
                g_server->npcHits.clear();

                // Resolve area / aggro abilities this tick. Validate the cast
                // originates near the caster so a client can't AoE across the map.
                for (const AbilityCastEvent& ac : g_server->abilityCasts) {
                    glm::vec3 cp = g_server->getPlayerPosition(ac.attackerId);
                    float ddx = ac.center.x - cp.x, ddz = ac.center.z - cp.z;
                    if (ddx * ddx + ddz * ddz > 30.0f * 30.0f) continue;
                    if (ac.effect == 1)
                        npcDirector.playerTaunt(ac.attackerId, ac.center, ac.radius, ac.scale);
                    else
                        npcDirector.playerAoe(ac.attackerId, ac.center, ac.radius, ac.scale);
                }
                g_server->abilityCasts.clear();

                npcDirector.update(SERVER_TICK_DT, dirPlayers, serverWorld, g_serverGameTime);

                // Forward NPC-dealt damage to the affected players.
                for (const PlayerDamage& d : npcDirector.pendingDamage) {
                    PlayerHealthPacket hp{ d.playerId, d.amount };
                    g_server->broadcast(PacketType::PlayerHealth, &hp, sizeof(hp));
                }
                npcDirector.pendingDamage.clear();

                for (const auto& n : npcDirector.npcs()) {
                    NPCStatePacket np{};
                    np.entityId       = n->id;
                    np.npcType        = (uint8_t)n->type;
                    np.level          = n->level;
                    np.flags          = (uint8_t)((n->walking ? 1 : 0)
                                       | (n->attackAnimTimer > 0.0f ? 2 : 0)
                                       | (n->dyingTimer > 0.0f ? 4 : 0)
                                       | (n->sitting ? 8 : 0)
                                       | (n->elite ? 16 : 0));
                    np.appearanceSeed = n->appearanceSeed;
                    np.x = n->position.x; np.y = n->position.y; np.z = n->position.z;
                    np.yaw = n->yaw;
                    np.vx = n->velocity.x; np.vy = n->velocity.y; np.vz = n->velocity.z;
                    np.health = n->health;
                    g_server->broadcast(PacketType::NPCState, &np, sizeof(np));
                }
            }

            {
                std::vector<glm::vec3> animalPlayers;
                animalPlayers.reserve(players.size());
                for (auto& p : players) animalPlayers.push_back(p.pos);
                animalDirector.update(SERVER_TICK_DT, animalPlayers, serverWorld);
                for (const auto& a : animalDirector.animals()) {
                    AnimalStatePacket ap{};
                    ap.entityId = a->id;
                    ap.species  = (uint8_t)a->species;
                    ap.flags    = (uint8_t)(a->walking ? 1 : 0);
                    ap.variant  = a->variant;
                    ap.x = a->position.x; ap.y = a->position.y; ap.z = a->position.z;
                    ap.yaw = a->yaw;
                    ap.vx = a->velocity.x; ap.vy = a->velocity.y; ap.vz = a->velocity.z;
                    g_server->broadcast(PacketType::AnimalState, &ap, sizeof(ap));
                }
            }

            // Expire stale loot drops once per tick. 90 s without a
            // pickup and the drop is broadcast as gone (newOwnerId=0).
            {
                using namespace std::chrono;
                double now = duration<double>(
                    steady_clock::now().time_since_epoch()).count();
                g_server->expireStaleLoot(now, 90.0);
            }

            g_serverGameTime = fmodf(g_serverGameTime + SERVER_TICK_DT / DAY_CYCLE_SECONDS, 1.0f);
            g_serverDayTimeSync.store(true);
            DayTimePacket dt { g_serverGameTime };
            g_server->broadcast(PacketType::DayTime, &dt, sizeof(dt));

            // Publish stats for the in-game debug overlay.
            {
                int v = 0, b = 0, gd = 0;
                for (const auto& n : npcDirector.npcs()) {
                    if      (n->type == NPCType::Villager) v++;
                    else if (n->type == NPCType::Enemy)    b++;
                    else if (n->type == NPCType::Guard)    gd++;
                }
                g_serverStats.villagers.store(v);
                g_serverStats.bandits.store(b);
                g_serverStats.guards.store(gd);
                g_serverStats.animals.store((int)animalDirector.animals().size());
                g_serverStats.ferries.store((int)ferries.size());
                g_serverStats.players.store((int)players.size());
                auto tickT1 = std::chrono::high_resolution_clock::now();
                g_serverStats.tickMs.store(
                    std::chrono::duration<float, std::milli>(tickT1 - tickT0).count());
            }
            accumulator -= SERVER_TICK_DT;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    g_serverStats.running.store(false);
    delete g_server;
    g_server = nullptr;
    std::cout << "Terrax Server stopped." << std::endl;
}

void startEmbeddedServer(unsigned short port, int renderDistance) {
    if (g_serverThreadRunning.load()) return;
    g_serverRenderDistance.store(clampRenderDistance(renderDistance));
    g_serverThreadRunning.store(true);
    g_serverThread = std::thread([port]() { serverThreadMain(port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

void stopEmbeddedServer() {
    if (!g_serverThreadRunning.load()) return;
    g_serverThreadRunning.store(false);
    if (g_serverThread.joinable()) g_serverThread.join();
}

bool isEmbeddedServerRunning() {
    return g_serverThreadRunning.load();
}

float getServerGameTime() { return g_serverGameTime; }

void runDedicatedServer(unsigned short port) {
    g_serverThreadRunning.store(true);
    serverThreadMain(port);
}
