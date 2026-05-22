#pragma once
#include "game_settings.h"
#include <atomic>
#include <thread>
#include <string>

static constexpr unsigned short DEFAULT_SERVER_PORT = 12345;
static constexpr float SERVER_TICK_RATE = 20.0f;
static constexpr float SERVER_TICK_DT = 1.0f / SERVER_TICK_RATE;

// Starts embedded server thread (host / singleplayer). No-op if already running.
void startEmbeddedServer(unsigned short port = DEFAULT_SERVER_PORT,
                         int renderDistance = DEFAULT_RENDER_DISTANCE);
void stopEmbeddedServer();
bool isEmbeddedServerRunning();
void runDedicatedServer(unsigned short port = DEFAULT_SERVER_PORT);

extern std::atomic<bool> g_serverDayTimeSync;
float getServerGameTime();

// Live server-thread statistics, published for the in-game debug overlay.
struct ServerStats {
    std::atomic<bool>  running{false};
    std::atomic<int>   players{0};
    std::atomic<int>   villagers{0};
    std::atomic<int>   bandits{0};
    std::atomic<int>   guards{0};
    std::atomic<int>   animals{0};
    std::atomic<int>   ferries{0};
    std::atomic<float> tickMs{0.0f};
};
extern ServerStats g_serverStats;
