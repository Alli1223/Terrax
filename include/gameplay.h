#pragma once

struct GLFWwindow;
struct AppContext;

void disconnectFromGame(AppContext& ctx);
void updateGameplay(AppContext& ctx, GLFWwindow* window);

// Bakes the current house design into the world at the live preview location
// by sending a HousePlace packet to the server.
void sendHousePlacement(AppContext& ctx);
