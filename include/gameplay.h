#pragma once

struct GLFWwindow;
struct AppContext;

void disconnectFromGame(AppContext& ctx);
void updateGameplay(AppContext& ctx, GLFWwindow* window);
