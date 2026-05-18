#include "game_session.h"
#include "game_types.h"
#include "network.h"
#include "world.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

static std::thread       g_serverThread;
static std::atomic<bool> g_serverThreadRunning{false};
static std::atomic<int>  g_serverRenderDistance{DEFAULT_RENDER_DISTANCE};
static float             g_serverGameTime = 0.3f;

std::atomic<bool> g_serverDayTimeSync{false};

static void serverThreadMain(unsigned short port) {
    std::cout << "Starting Terrax Server on port " << port << "..." << std::endl;
    g_server = new NetworkServer(port);
    World serverWorld(true);
    serverWorld.renderDistance = g_serverRenderDistance.load();
    serverWorld.generate(0, 0);

    auto lastWall = std::chrono::high_resolution_clock::now();
    float accumulator = 0.0f;

    while (g_serverThreadRunning.load()) {
        auto now = std::chrono::high_resolution_clock::now();
        float frameDt = std::chrono::duration<float>(now - lastWall).count();
        lastWall = now;
        frameDt = std::min(frameDt, 0.25f);

        accumulator += frameDt;
        while (accumulator >= SERVER_TICK_DT) {
            auto players = g_server->getPlayerStates();
            for (auto& p : players) {
                int pcx = (int)floorf(p.pos.x / (float)CHUNK_SIZE);
                int pcz = (int)floorf(p.pos.z / (float)CHUNK_SIZE);
                serverWorld.update(pcx, pcz);
            }
            g_server->update(serverWorld);
            g_serverGameTime = fmodf(g_serverGameTime + SERVER_TICK_DT / DAY_CYCLE_SECONDS, 1.0f);
            g_serverDayTimeSync.store(true);
            DayTimePacket dt { g_serverGameTime };
            g_server->broadcast(PacketType::DayTime, &dt, sizeof(dt));
            accumulator -= SERVER_TICK_DT;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

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
