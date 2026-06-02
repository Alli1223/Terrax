#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui.h"
#include "app_context.h"
#include "renderer.h"
#include "game_session.h"
#include "gameplay.h"
#include "town.h"
#include "npc.h"
#include "prop_placement.h"
#include "graphics_settings.h"
#include "gl_loader.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <iostream>
#include "ui_internal.h"


// ---------------------------------------------------------------------------
// ImGui theme
// ---------------------------------------------------------------------------

// Pixel-art-leaning style: sharp corners everywhere, chunky borders, no
// anti-aliasing on lines / fills, a warm parchment-on-leather palette.
// The visual goal is "tabletop RPG screen", not "default ImGui debug HUD".
static void setupPixelArtStyle() {
    ImGuiStyle& style  = ImGui::GetStyle();
    ImVec4*     colors = style.Colors;

    ImVec4 ink         = ImVec4(0.06f, 0.04f, 0.03f, 1.00f);
    ImVec4 leather     = ImVec4(0.13f, 0.09f, 0.06f, 0.97f);
    ImVec4 leather_mid = ImVec4(0.20f, 0.14f, 0.09f, 1.00f);
    ImVec4 leather_hi  = ImVec4(0.30f, 0.20f, 0.12f, 1.00f);
    ImVec4 gold        = ImVec4(0.95f, 0.78f, 0.32f, 1.00f);
    ImVec4 gold_dim    = ImVec4(0.65f, 0.50f, 0.18f, 1.00f);
    ImVec4 parchment   = ImVec4(0.96f, 0.90f, 0.72f, 1.00f);
    ImVec4 parchdim    = ImVec4(0.70f, 0.62f, 0.45f, 1.00f);

    colors[ImGuiCol_Text]                  = parchment;
    colors[ImGuiCol_TextDisabled]          = parchdim;
    colors[ImGuiCol_WindowBg]              = leather;
    colors[ImGuiCol_ChildBg]               = ImVec4(0.08f, 0.06f, 0.04f, 0.55f);
    colors[ImGuiCol_PopupBg]               = leather;
    colors[ImGuiCol_Border]                = gold_dim;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg]               = leather_mid;
    colors[ImGuiCol_FrameBgHovered]        = leather_hi;
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.40f, 0.28f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ink;
    colors[ImGuiCol_TitleBgActive]         = leather_mid;
    colors[ImGuiCol_TitleBgCollapsed]      = ink;
    colors[ImGuiCol_MenuBarBg]             = leather_mid;
    colors[ImGuiCol_ScrollbarBg]           = ink;
    colors[ImGuiCol_ScrollbarGrab]         = gold_dim;
    colors[ImGuiCol_ScrollbarGrabHovered]  = gold;
    colors[ImGuiCol_ScrollbarGrabActive]   = gold;
    colors[ImGuiCol_CheckMark]             = gold;
    colors[ImGuiCol_SliderGrab]            = gold_dim;
    colors[ImGuiCol_SliderGrabActive]      = gold;
    colors[ImGuiCol_Button]                = leather_mid;
    colors[ImGuiCol_ButtonHovered]         = leather_hi;
    colors[ImGuiCol_ButtonActive]          = gold_dim;
    colors[ImGuiCol_Header]                = leather_mid;
    colors[ImGuiCol_HeaderHovered]         = leather_hi;
    colors[ImGuiCol_HeaderActive]          = gold_dim;
    colors[ImGuiCol_Separator]             = gold_dim;
    colors[ImGuiCol_SeparatorHovered]      = gold;
    colors[ImGuiCol_SeparatorActive]       = gold;
    colors[ImGuiCol_ResizeGrip]            = gold_dim;
    colors[ImGuiCol_ResizeGripHovered]     = gold;
    colors[ImGuiCol_ResizeGripActive]      = gold;
    colors[ImGuiCol_Tab]                   = ink;
    colors[ImGuiCol_TabHovered]            = leather_hi;
    colors[ImGuiCol_TabActive]             = leather_mid;
    colors[ImGuiCol_TabUnfocused]          = ink;
    colors[ImGuiCol_TabUnfocusedActive]    = leather_mid;
    colors[ImGuiCol_PlotLines]             = gold;
    colors[ImGuiCol_PlotLinesHovered]      = parchment;
    colors[ImGuiCol_PlotHistogram]         = gold;
    colors[ImGuiCol_PlotHistogramHovered]  = parchment;
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.45f, 0.30f, 0.15f, 1.00f);
    colors[ImGuiCol_DragDropTarget]        = gold;
    colors[ImGuiCol_NavHighlight]          = gold;
    colors[ImGuiCol_NavWindowingHighlight] = gold;
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

    // Sharp corners — defining feature of the pixel-art feel.
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;

    // Chunky borders for that punched-out plate-armour feel.
    style.WindowBorderSize  = 2.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 2.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 1.0f;
    style.SeparatorTextBorderSize = 2.0f;

    style.WindowPadding     = ImVec2(14, 14);
    style.FramePadding      = ImVec2(8, 6);
    style.ItemSpacing       = ImVec2(10, 8);
    style.ItemInnerSpacing  = ImVec2(8, 6);
    style.IndentSpacing     = 24.0f;
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 14.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign   = ImVec2(0.5f, 0.5f);

    // Disable AA so primitives draw as crisp rectangles instead of soft
    // edges — keeps the whole HUD inside a single visual language.
    style.AntiAliasedLines       = false;
    style.AntiAliasedLinesUseTex = false;
    style.AntiAliasedFill        = false;
}

void initImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // ImGui ships ProggyClean — a bitmap-style font designed for exactly
    // 13px. We disable atlas oversampling so the glyphs rasterise at 1:1
    // (no sub-pixel smoothing) and leave FontGlobalScale at 1.0 so the
    // atlas pixels map straight to screen pixels. Upscaling ProggyClean
    // is what made the text look blurry — the ImGui GL backend binds a
    // GL_LINEAR sampler object that overrides any per-texture filter we
    // try to set, so non-integer scales blur and even integer scales
    // need a backend patch to stay crisp. Native size is bulletproof.
    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;
    io.Fonts->AddFontDefault(&cfg);

    setupPixelArtStyle();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

