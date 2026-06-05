#pragma once
// Shared internals for the split ui_*.cpp translation units (ui.cpp, ui_menus,
// ui_editors, ui_map, ui_play). Holds the viewport-relative placement helpers
// every ImGui panel uses, plus the one cross-file UI entry point. ui.h remains
// the public surface; this header is private to the ui_* sources.
#include "imgui.h"

struct AppContext;

// --- Viewport-relative placement -------------------------------------------
// Every ImGui window must be positioned in the main viewport's coordinate
// space (logical display units). That space updates every frame, so panels
// follow live resolution changes and HiDPI scaling. The WINDOW_WIDTH /
// WINDOW_HEIGHT constants only describe the *initial* window — positioning
// from them leaves the whole HUD stranded the moment the resolution changes.
inline ImVec2 vpPos()  { return ImGui::GetMainViewport()->Pos; }
inline ImVec2 vpSize() { return ImGui::GetMainViewport()->Size; }
// Top-left for a window of size (w,h) centred in the viewport.
inline ImVec2 vpCentered(float w, float h) {
    ImVec2 p = vpPos(), s = vpSize();
    return ImVec2(p.x + (s.x - w) * 0.5f, p.y + (s.y - h) * 0.5f);
}

// The world-map overlay lives in ui_map.cpp; renderPlayUI (ui_play.cpp) calls
// it each frame, so it needs external linkage and a shared declaration.
void renderMapUI(AppContext& ctx);
