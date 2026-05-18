#pragma once
#include <atomic>
#include <thread>
#include <string>

static constexpr unsigned short DEFAULT_SERVER_PORT = 12345;
static constexpr float SERVER_TICK_RATE = 20.0f;
static constexpr float SERVER_TICK_DT = 1.0f / SERVER_TICK_RATE;

// Starts embedded server thread (host / singleplayer). No-op if already running.
void startEmbeddedServer(unsigned short port = DEFAULT_SERVER_PORT);
void stopEmbeddedServer();
bool isEmbeddedServerRunning();
void runDedicatedServer(unsigned short port = DEFAULT_SERVER_PORT);

extern std::atomic<bool> g_serverDayTimeSync;
float getServerGameTime();
