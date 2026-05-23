#pragma once

struct GLFWwindow;
struct AppContext;
class  Renderer;

void initImGui(GLFWwindow* window);

// Renders main menu or join menu depending on ctx.state.
void renderMenuUI(AppContext& ctx, GLFWwindow* window, Renderer* renderer);

// Renders the character editor ImGui panel + handles mouse input for voxel editing / rotation.
// Also calls renderer.renderEditorCharacter for the 3D viewport.
void renderCharacterEditorUI(AppContext& ctx, GLFWwindow* window, Renderer& renderer);

// Renders the house generator ImGui panel + handles mouse input for voxel editing / rotation.
// Also calls renderer.renderEditorHouse for the 3D viewport.
void renderHouseEditorUI(AppContext& ctx, GLFWwindow* window, Renderer& renderer);

// Renders in-game HUD, underwater overlay, breath bar, nametags, chat, player list.
void renderPlayUI(AppContext& ctx, GLFWwindow* window, const Renderer& renderer);

// Renders the pause overlay.
void renderPauseMenuUI(AppContext& ctx, GLFWwindow* window);

// Renders the loading screen (centred progress bar + stage name) while the
// background worker spawned by beginLoading() pre-warms the world plan.
// Returns true once both the worker AND the main-thread finalisation step
// (propLibrary.buildAll) have completed — the caller should then transition
// to GameState::Playing.
bool renderLoadingUI(AppContext& ctx, GLFWwindow* window);

// Kicks off the background world-generation worker and transitions the
// context into GameState::Loading. Safe to call repeatedly; reuses the same
// thread slot. The worker calls getTownPlan(), getPropPlacements() and
// getDoorPlacements() so the first frame of Playing is no longer expensive.
void beginLoading(AppContext& ctx);
