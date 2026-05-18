#pragma once

struct GLFWwindow;
struct AppContext;
struct GameSettings;

// Creates the GLFW window using graphics fields in settings (MSAA hint, size).
GLFWwindow* createGameWindow(GameSettings& settings);

// Applies runtime graphics options (fullscreen, vsync, window size, FOV).
void applyGraphicsSettings(GLFWwindow* window, AppContext& ctx);

int  graphicsSettingsResolutionCount();
const char* graphicsSettingsResolutionLabel(int index);
int  graphicsSettingsResolutionIndex(const GameSettings& settings);
void graphicsSettingsApplyResolutionPreset(GameSettings& settings, int index);
