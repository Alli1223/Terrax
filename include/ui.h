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

// In-game inventory overlay (I key). Lists items in the bag, lets the
// player equip/unequip them. Called by renderPlayUI when ctx.showInventory.
void renderInventoryUI(AppContext& ctx, GLFWwindow* window);

// In-game character loadout overlay (C key). Shows the equipped slots and
// a quick switcher for each one. Called by renderPlayUI when
// ctx.showCharacterLoadout.
void renderCharacterLoadoutUI(AppContext& ctx, GLFWwindow* window);
