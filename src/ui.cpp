#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui.h"
#include "app_context.h"
#include "renderer.h"
#include "game_session.h"
#include "gameplay.h"
#include "town.h"
#include "npc.h"
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

// ---------------------------------------------------------------------------
// Menu UIs
// ---------------------------------------------------------------------------

static void applyRenderDistanceSetting(AppContext& ctx) {
    ctx.settings.renderDistance = clampRenderDistance(ctx.settings.renderDistance);
    ctx.world.renderDistance    = ctx.settings.renderDistance;
}

static void syncRendererFramebuffers(GLFWwindow* window, Renderer* renderer) {
    if (!window || !renderer) return;
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW > 0 && fbH > 0)
        renderer->resizeFramebuffers(fbW, fbH);
}

static void applyGraphicsSettingsAndSync(GLFWwindow* window, AppContext& ctx, Renderer* renderer) {
    applyGraphicsSettings(window, ctx);
    syncRendererFramebuffers(window, renderer);
}

static void renderSettingsUI(AppContext& ctx, GLFWwindow* window, Renderer* renderer) {
    int winW = 0, winH = 0;
    glfwGetWindowSize(window, &winW, &winH);
    if (winW <= 0) winW = WINDOW_WIDTH;
    if (winH <= 0) winH = WINDOW_HEIGHT;

    ImGui::SetNextWindowPos(ImVec2(winW / 2 - 200, winH / 2 - 240));
    ImGui::SetNextWindowSize(ImVec2(400, 480));
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    ImGui::Text("SETTINGS");
    ImGui::Separator();

    ImGui::Text("World");
    if (ImGui::SliderInt("Chunk render distance", &ctx.settings.renderDistance,
                         MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE)) {
        applyRenderDistanceSetting(ctx);
    }
    ImGui::TextWrapped(
        "Chunk radius around you. Host / singleplayer uses this when you start a session.");

    ImGui::Spacing();
    ImGui::Text("Graphics");
    bool gfxChanged = false;

    if (ImGui::Checkbox("Fullscreen", &ctx.settings.fullscreen))
        gfxChanged = true;

    if (ImGui::Checkbox("V-Sync", &ctx.settings.vsync))
        gfxChanged = true;

    bool msaa = ctx.settings.msaa;
    if (ImGui::Checkbox("Anti-aliasing (MSAA)", &msaa)) {
        ctx.settings.msaa = msaa;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Requires restarting the game to take effect.");
    }

    ImGui::BeginDisabled(ctx.settings.fullscreen);
    int resIdx = graphicsSettingsResolutionIndex(ctx.settings);
    const char* resLabels[8];
    int resCount = graphicsSettingsResolutionCount();
    for (int i = 0; i < resCount && i < 8; i++)
        resLabels[i] = graphicsSettingsResolutionLabel(i);
    if (ImGui::Combo("Resolution", &resIdx, resLabels, resCount)) {
        graphicsSettingsApplyResolutionPreset(ctx.settings, resIdx);
        gfxChanged = true;
    }
    ImGui::EndDisabled();
    if (ctx.settings.fullscreen)
        ImGui::TextDisabled("Resolution uses your desktop display mode in fullscreen.");

    if (ImGui::SliderFloat("Field of view", &ctx.settings.fov, 50.0f, 110.0f, "%.0f"))
        ctx.camera.fov = ctx.settings.fov;

    if (gfxChanged)
        applyGraphicsSettingsAndSync(window, ctx, renderer);

    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(-1, 36)))
        ctx.state = GameState::MainMenu;

    ImGui::End();
}

void renderMenuUI(AppContext& ctx, GLFWwindow* window, Renderer* renderer) {
    if (ctx.state == GameState::SettingsMenu) {
        renderSettingsUI(ctx, window, renderer);
        return;
    }
    if (ctx.state == GameState::MainMenu) {
        ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 160, WINDOW_HEIGHT / 2 - 230));
        ImGui::SetNextWindowSize(ImVec2(320, 468));
        ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        ImGui::Text("TERRAX");
        ImGui::Separator();
        ImGui::Text("Player Name");
        ImGui::InputText("##name", ctx.playerName, MAX_PLAYER_NAME + 1);

        if (ImGui::Button("Singleplayer", ImVec2(-1, 36))) {
            disconnectFromGame(ctx);
            applyRenderDistanceSetting(ctx);
            ctx.sessionMode  = SessionMode::Singleplayer;
            ctx.connectHost  = "127.0.0.1";
            ctx.connectPort  = DEFAULT_SERVER_PORT;
            startEmbeddedServer(ctx.connectPort, ctx.settings.renderDistance);
            ctx.weOwnServer  = true;
            ctx.state        = GameState::Playing;
        }
        if (ImGui::Button("Host Game", ImVec2(-1, 36))) {
            disconnectFromGame(ctx);
            applyRenderDistanceSetting(ctx);
            ctx.sessionMode  = SessionMode::Host;
            ctx.connectHost  = "127.0.0.1";
            ctx.connectPort  = DEFAULT_SERVER_PORT;
            startEmbeddedServer(ctx.connectPort, ctx.settings.renderDistance);
            ctx.weOwnServer  = true;
            ctx.state        = GameState::Playing;
        }
        if (ImGui::Button("Join Game", ImVec2(-1, 36))) {
            ctx.state = GameState::JoinMenu;
        }
        if (ImGui::Button("Character Editor", ImVec2(-1, 36))) {
            ctx.state = GameState::CharacterEditor;
        }
        if (ImGui::Button("House Editor", ImVec2(-1, 36))) {
            ctx.state      = GameState::HouseEditor;
            ctx.editorRotX = -18.0f;
            ctx.editorRotY = 35.0f;
            ctx.camDist    = 16.0f;
        }
        if (ImGui::Button("Settings", ImVec2(-1, 36))) {
            ctx.state = GameState::SettingsMenu;
        }
        if (ImGui::Button("Exit", ImVec2(-1, 36))) {
            disconnectFromGame(ctx);
            exit(0);
        }
        ImGui::End();
    }
    else if (ctx.state == GameState::JoinMenu) {
        static char hostBuf[128] = "127.0.0.1";
        static int  port = (int)DEFAULT_SERVER_PORT;

        ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 160, WINDOW_HEIGHT / 2 - 120));
        ImGui::SetNextWindowSize(ImVec2(320, 240));
        ImGui::Begin("Join Game", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        ImGui::Text("Server Address");
        ImGui::InputText("Host", hostBuf, sizeof(hostBuf));
        ImGui::InputInt("Port", &port);
        port = std::clamp(port, 1, 65535);

        if (ImGui::Button("Connect", ImVec2(-1, 36))) {
            disconnectFromGame(ctx);
            applyRenderDistanceSetting(ctx);
            ctx.sessionMode = SessionMode::Join;
            ctx.connectHost = hostBuf;
            ctx.connectPort = (unsigned short)port;
            ctx.weOwnServer = false;
            ctx.state       = GameState::Playing;
        }
        if (ImGui::Button("Back", ImVec2(-1, 36))) {
            ctx.state = GameState::MainMenu;
        }
        ImGui::End();
    }
}

void renderPauseMenuUI(AppContext& ctx, GLFWwindow* window) {
    ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 140, WINDOW_HEIGHT / 2 - 100));
    ImGui::SetNextWindowSize(ImVec2(280, 200));
    ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    if (ImGui::Button("Resume", ImVec2(-1, 32))) {
        ctx.paused = false;
        ctx.state  = GameState::Playing;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        ctx.firstMouse = true;
    }
    if (ImGui::Button("Disconnect", ImVec2(-1, 32))) {
        disconnectFromGame(ctx);
        ctx.state = GameState::MainMenu;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (ImGui::Button("Exit Game", ImVec2(-1, 32))) {
        disconnectFromGame(ctx);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Character editor UI
// ---------------------------------------------------------------------------

void renderCharacterEditorUI(AppContext& ctx, GLFWwindow* window, Renderer& renderer) {
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);

    glm::mat4 proj  = glm::perspective(glm::radians(45.0f), fbW / (float)fbH, 0.1f, 1000.0f);
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(ctx.editorRotY), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(ctx.editorRotX), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(0.06f * (ctx.playerRig ? ctx.playerRig->heightScale : 1.0f)));
    glm::mat4 view  = glm::lookAt(glm::vec3(0, 1.5, 4), glm::vec3(0, 1.0, 0), glm::vec3(0, 1, 0));

    renderer.renderEditorCharacter(ctx, model, view, proj);

    // ImGui panel
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, (float)fbH), ImGuiCond_Always);
    ImGui::Begin("Character Editor", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back to Menu", ImVec2(-1, 0))) ctx.state = GameState::MainMenu;
    ImGui::Separator();

    if (ImGui::Combo("Character Type", &ctx.editorCharType, "Human Male\0Human Female\0")) {
        ctx.playerRig->setupDefaultHuman(ctx.editorCharType == 0);
        rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
    }

    ImGui::Separator();
    if (ImGui::SliderFloat("Height", &ctx.playerRig->heightScale, 0.5f, 1.5f)) {}
    if (ImGui::SliderFloat("Weight", &ctx.playerRig->weightScale, 0.5f, 1.5f)) {
        if (ctx.playerRig->torso) {
            ctx.playerRig->torso->scale.x = ctx.playerRig->weightScale;
            ctx.playerRig->torso->scale.z = ctx.playerRig->weightScale;
        }
    }

    if (ImGui::CollapsingHeader("Face Features", ImGuiTreeNodeFlags_DefaultOpen)) {
        float sCol[3] = { ctx.playerRig->skinColor.r / 255.0f,
                          ctx.playerRig->skinColor.g / 255.0f,
                          ctx.playerRig->skinColor.b / 255.0f };
        if (ImGui::ColorEdit3("Skin Color", sCol)) {
            ctx.playerRig->skinColor = { (uint8_t)(sCol[0]*255), (uint8_t)(sCol[1]*255),
                                         (uint8_t)(sCol[2]*255), 255 };
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        }
        if (ImGui::Combo("Hair Style", &ctx.playerRig->hairStyle,
                "Bald\0Crew Cut\0Messy Short\0Mohawk\0Spiky\0Side Swept\0"
                "Bob\0Long Straight\0Wavy Long\0Bun\0Pigtails\0Braided\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);

        float hCol[3] = { ctx.playerRig->hairColor.r / 255.0f,
                          ctx.playerRig->hairColor.g / 255.0f,
                          ctx.playerRig->hairColor.b / 255.0f };
        if (ImGui::ColorEdit3("Hair Color", hCol)) {
            ctx.playerRig->hairColor = { (uint8_t)(hCol[0]*255), (uint8_t)(hCol[1]*255),
                                         (uint8_t)(hCol[2]*255), 255 };
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        }
        if (ImGui::Combo("Eyebrow Style", &ctx.playerRig->eyebrowStyle,
                "Straight\0Arched\0Thick\0Thin\0Furrowed\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);

        float eCol[3] = { ctx.playerRig->eyeColor.r / 255.0f,
                          ctx.playerRig->eyeColor.g / 255.0f,
                          ctx.playerRig->eyeColor.b / 255.0f };
        if (ImGui::ColorEdit3("Eye Color", eCol)) {
            ctx.playerRig->eyeColor = { (uint8_t)(eCol[0]*255), (uint8_t)(eCol[1]*255),
                                        (uint8_t)(eCol[2]*255), 255 };
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        }
        if (ImGui::Combo("Eye Type", &ctx.playerRig->eyeType,
                "Classic\0Happy\0Wide\0Slanted\0Heart\0Wink\0Tired\0Star\0Tears\0Determined\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        if (ImGui::Combo("Nose Style", &ctx.playerRig->noseStyle,
                "Button\0Wide\0Narrow\0Upturned\0Broad\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        if (ImGui::Combo("Ear Type", &ctx.playerRig->earType,
                "None\0Human\0Elven\0Rounded\0Wide\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        // Armour set picker removed — gear is now managed in-game from the
        // Character Loadout screen (press C while playing).
    }

    ImGui::Separator();
    ImGui::Text("Voxel Tools");
    if (ImGui::RadioButton("Paint", ctx.editorTool == EditorTool::Paint))
        ctx.editorTool = EditorTool::Paint;
    if (ImGui::RadioButton("Add",   ctx.editorTool == EditorTool::Add))
        ctx.editorTool = EditorTool::Add;
    if (ImGui::RadioButton("Erase", ctx.editorTool == EditorTool::Erase))
        ctx.editorTool = EditorTool::Erase;

    ImGui::Separator();
    ImGui::Text("Color");
    ImGui::ColorPicker4("##picker", (float*)&ctx.editorColor);

    ImGui::Separator();
    if (ImGui::Button("Save Model", ImVec2(-1, 0))) {}

    ImGui::End();

    // Mouse: rotation or voxel editing
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (!ImGui::GetIO().WantCaptureMouse) {
            float rx = (2.0f * (float)mx) / fbW - 1.0f;
            float ry = 1.0f - (2.0f * (float)my) / fbH;
            glm::vec4 clip(rx, ry, -1.0f, 1.0f);
            glm::vec4 eye = glm::inverse(proj) * clip;
            eye.z = -1.0f; eye.w = 0.0f;
            glm::vec3 rd = glm::normalize(glm::vec3(glm::inverse(view) * eye));
            glm::vec3 ro = glm::vec3(glm::inverse(view) * glm::vec4(0, 0, 0, 1));

            if (!ctx.wasEditorClick) {
                auto hit = ctx.playerRig->raycast(ro, rd, model);
                ctx.isEditorRotating = !hit.node;
                ctx.wasEditorClick   = true;
            }

            if (ctx.isEditorRotating) {
                ctx.editorRotY += (float)(mx - ctx.lastEditorX) * 0.5f;
                ctx.editorRotX += (float)(my - ctx.lastEditorY) * 0.5f;
                ctx.editorRotX  = std::clamp(ctx.editorRotX, -80.0f, 80.0f);
            } else {
                auto hit = ctx.playerRig->raycast(ro, rd, model);
                if (hit.node && hit.node->volume) {
                    Voxel cv = { (uint8_t)(ctx.editorColor.r * 255),
                                 (uint8_t)(ctx.editorColor.g * 255),
                                 (uint8_t)(ctx.editorColor.b * 255),
                                 (uint8_t)(ctx.editorColor.a * 255) };
                    if (ctx.editorTool == EditorTool::Paint) {
                        hit.node->volume->setVoxel(hit.voxel.x, hit.voxel.y, hit.voxel.z, cv);
                    } else if (ctx.editorTool == EditorTool::Add) {
                        glm::ivec3 ap = hit.voxel + hit.normal;
                        hit.node->volume->setVoxel(ap.x, ap.y, ap.z, cv);
                    } else {
                        hit.node->volume->setVoxel(hit.voxel.x, hit.voxel.y, hit.voxel.z, {0,0,0,0});
                    }
                    hit.node->volume->updateMesh();
                }
            }
        }
        ctx.lastEditorX = mx;
        ctx.lastEditorY = my;
    } else {
        ctx.wasEditorClick = false;
    }
}

// ---------------------------------------------------------------------------
// House editor UI
// ---------------------------------------------------------------------------

// Blocks the player can build a house from (parallel to the combo labels below).
// First the structural blocks, then every painted-palette colour.
static std::vector<BlockType> makeHouseBuildBlocks() {
    std::vector<BlockType> v = {
        BlockType::Wood, BlockType::Stone, BlockType::Glass,
        BlockType::Glowstone, BlockType::Leaves,
    };
    for (int i = 0; i < PAINT_COUNT; i++)
        v.push_back((BlockType)((int)BlockType::PaintFirst + i));
    return v;
}
static const std::vector<BlockType> kHouseBuildBlocks = makeHouseBuildBlocks();
static const char* kHouseBuildBlockLabels =
    "Wood\0Stone\0Glass\0Glowstone\0Leaves\0"
    "White\0Cream\0Light Gray\0Slate Gray\0Charcoal\0Black\0"
    "Terracotta\0Brick Red\0Crimson\0Rust Orange\0Amber\0Mustard\0"
    "Chestnut\0Sand\0Olive\0Sage\0Forest Green\0Mint\0"
    "Sky Blue\0Teal\0Navy\0Steel Blue\0Plum\0Dusty Rose\0";

void renderHouseEditorUI(AppContext& ctx, GLFWwindow* window, Renderer& renderer) {
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);

    HouseModel* house = ctx.houseModel;

    glm::mat4 proj  = glm::perspective(glm::radians(45.0f), fbW / (float)fbH, 0.1f, 1000.0f);
    glm::vec3 center(HOUSE_VX * 0.5f, 12.0f, HOUSE_VZ * 0.5f);
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(ctx.editorRotY), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(ctx.editorRotX), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(0.28f));
    model = glm::translate(model, -center);
    glm::mat4 view  = glm::lookAt(glm::vec3(0, 0, ctx.camDist),
                                  glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

    renderer.renderEditorHouse(ctx, model, view, proj);

    // ImGui panel
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, (float)fbH), ImGuiCond_Always);
    ImGui::Begin("House Editor", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back to Menu", ImVec2(-1, 0))) ctx.state = GameState::MainMenu;
    ImGui::Separator();

    if (house) {
        ImGui::Text("Template");
        if (ImGui::Combo("##template", &house->templateType,
                "Bungalow\0Two-Story\0Cottage\0Tower\0Cabin\0"
                "Longhouse\0Townhouse\0Manor\0Hall\0Keep\0"))
            house->rebuild();
        if (ImGui::Combo("Roof", &house->roofType,
                "Flat\0Gabled\0Hipped\0Pyramid\0"))
            house->rebuild();
        if (ImGui::Combo("Material", &house->material,
                "Timber\0Cottage\0Stone\0Manor\0Cabin\0"
                "Sandstone\0Forest\0Coastal\0Autumn\0Plum\0"))
            house->rebuild();

        ImGui::Separator();
        if (ImGui::Button("Reset to Template", ImVec2(-1, 0)))
            house->rebuild();
    }

    ImGui::Separator();
    ImGui::Text("Edit Tools");
    if (ImGui::RadioButton("Paint", ctx.editorTool == EditorTool::Paint))
        ctx.editorTool = EditorTool::Paint;
    if (ImGui::RadioButton("Add",   ctx.editorTool == EditorTool::Add))
        ctx.editorTool = EditorTool::Add;
    if (ImGui::RadioButton("Erase", ctx.editorTool == EditorTool::Erase))
        ctx.editorTool = EditorTool::Erase;

    ImGui::Separator();
    ImGui::Text("Build Block");
    ImGui::Combo("##buildblock", &ctx.editorBlock, kHouseBuildBlockLabels);

    ImGui::Separator();
    ImGui::TextWrapped("Drag empty space to rotate, scroll to zoom. "
                       "Drag the house to paint/add/erase blocks.");
    ImGui::TextWrapped("The house is built from real world blocks, so it has full "
                       "collision once placed.");
    ImGui::Spacing();
    ImGui::TextWrapped("In game: press H to preview placement, H again to build it, "
                       "Esc to cancel.");

    ImGui::End();

    // Mouse: rotation or block editing
    if (house && house->volume &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (!ImGui::GetIO().WantCaptureMouse) {
            float rx = (2.0f * (float)mx) / fbW - 1.0f;
            float ry = 1.0f - (2.0f * (float)my) / fbH;
            glm::vec4 clip(rx, ry, -1.0f, 1.0f);
            glm::vec4 eye = glm::inverse(proj) * clip;
            eye.z = -1.0f; eye.w = 0.0f;
            glm::vec3 rd = glm::normalize(glm::vec3(glm::inverse(view) * eye));
            glm::vec3 ro = glm::vec3(glm::inverse(view) * glm::vec4(0, 0, 0, 1));

            glm::mat4 invM = glm::inverse(model);
            glm::vec3 lro  = glm::vec3(invM * glm::vec4(ro, 1.0f));
            glm::vec3 lrd  = glm::normalize(glm::vec3(invM * glm::vec4(rd, 0.0f)));
            glm::ivec3 hv, hn;

            if (!ctx.wasEditorClick) {
                ctx.isEditorRotating = !house->volume->raycast(lro, lrd, 500.0f, hv, hn);
                ctx.wasEditorClick   = true;
            }

            if (ctx.isEditorRotating) {
                ctx.editorRotY += (float)(mx - ctx.lastEditorX) * 0.5f;
                ctx.editorRotX += (float)(my - ctx.lastEditorY) * 0.5f;
                ctx.editorRotX  = std::clamp(ctx.editorRotX, -89.0f, 89.0f);
            } else if (house->volume->raycast(lro, lrd, 500.0f, hv, hn)) {
                const int blockCount = (int)kHouseBuildBlocks.size();
                BlockType placeB = kHouseBuildBlocks[std::clamp(ctx.editorBlock, 0, blockCount - 1)];
                if (ctx.editorTool == EditorTool::Paint) {
                    house->set(hv.x, hv.y, hv.z, placeB);
                } else if (ctx.editorTool == EditorTool::Add) {
                    glm::ivec3 ap = hv + hn;
                    house->set(ap.x, ap.y, ap.z, placeB);
                } else {
                    house->set(hv.x, hv.y, hv.z, BlockType::Air);
                }
                house->refreshMesh();
            }
        }
        ctx.lastEditorX = mx;
        ctx.lastEditorY = my;
    } else {
        ctx.wasEditorClick = false;
    }
}

// ---------------------------------------------------------------------------
// World map
// ---------------------------------------------------------------------------

static void renderMapUI(AppContext& ctx) {
    if (!ctx.showMap) return;

    static constexpr int   TEX  = 256;   // texture resolution
    static constexpr float DISP = 460.0f; // displayed diameter in screen pixels

    // ── Poll background build ────────────────────────────────────────────────
    if (ctx.mapBuilding && ctx.mapFuture.valid() &&
        ctx.mapFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::vector<uint8_t> px = ctx.mapFuture.get();
        ctx.mapBuilding = false;
        if (!ctx.mapTex) {
            glGenTextures(1, &ctx.mapTex);
            glBindTexture(GL_TEXTURE_2D, ctx.mapTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, ctx.mapTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ── Trigger a new build when needed ─────────────────────────────────────
    float worldRadius = 256.0f / ctx.mapZoom;
    bool posChanged   = fabsf(ctx.camera.position.x - ctx.mapBuiltCX) > 32.0f ||
                        fabsf(ctx.camera.position.z - ctx.mapBuiltCZ) > 32.0f;
    if (!ctx.mapBuilding && (ctx.mapNeedsRebuild || posChanged)) {
        ctx.mapBuiltCX     = ctx.camera.position.x;
        ctx.mapBuiltCZ     = ctx.camera.position.z;
        ctx.mapNeedsRebuild = false;
        float bcx = ctx.mapBuiltCX + ctx.mapPanX;
        float bcz = ctx.mapBuiltCZ + ctx.mapPanZ;
        float wr  = worldRadius;
        ctx.mapBuilding = true;
        ctx.mapFuture = std::async(std::launch::async,
            [&world = (const World&)ctx.world, bcx, bcz, wr]() -> std::vector<uint8_t> {
                std::vector<uint8_t> px(TEX * TEX * 4);
                world.fillMapPixels(px.data(), TEX, bcx, bcz, wr);
                return px;
            });
    }

    // ── ImGui window ─────────────────────────────────────────────────────────
    const float WIN = DISP + 80.0f;
    ImGui::SetNextWindowSize(ImVec2(WIN, WIN + 44.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH * 0.5f - WIN * 0.5f,
                                   WINDOW_HEIGHT * 0.5f - (WIN + 44.0f) * 0.5f),
                            ImGuiCond_Always);
    ImGui::Begin("World Map", &ctx.showMap, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // Canvas invisible button captures mouse events
    ImVec2 canvasTL = ImGui::GetCursorScreenPos();
    float  canvasW  = ImGui::GetContentRegionAvail().x;
    float  canvasH  = DISP + 10.0f;
    ImGui::InvisibleButton("mapcanvas", ImVec2(canvasW, canvasH));

    ImVec2 mc = { canvasTL.x + canvasW * 0.5f, canvasTL.y + (DISP + 10.0f) * 0.5f };
    float  h  = DISP * 0.5f;

    // ── Mouse controls ───────────────────────────────────────────────────────
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();
    ImVec2 md    = ImGui::GetIO().MouseDelta;

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float ang = glm::radians(ctx.mapRotDeg);
        float cr  = cosf(ang), sr = sinf(ang);
        float scale = worldRadius / h;
        // Unrotate mouse delta to world-space pan delta
        float wdx =  md.x * cr + md.y * sr;
        float wdz = -md.x * sr + md.y * cr;
        ctx.mapPanX -= wdx * scale;
        ctx.mapPanZ -= wdz * scale;
        ctx.mapNeedsRebuild = true;
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ctx.mapRotDeg += md.x * 0.4f;
    }
    if (hovered) {
        float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0.0f) {
            ctx.mapZoom = std::clamp(ctx.mapZoom * (1.0f + scroll * 0.15f), 0.1f, 20.0f);
            ctx.mapNeedsRebuild = true;
        }
    }

    // ── Drawing ──────────────────────────────────────────────────────────────
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float ang = glm::radians(ctx.mapRotDeg);
    float cr  = cosf(ang), sr = sinf(ang);

    auto rotPt = [&](float dx, float dy) -> ImVec2 {
        return { mc.x + dx * cr - dy * sr,
                 mc.y + dx * sr + dy * cr };
    };

    // Background circle
    dl->AddCircleFilled(mc, h + 6.0f, IM_COL32(15, 10, 5, 230), 64);

    // Map image (rotated quad)
    if (ctx.mapTex) {
        dl->PushClipRect(ImVec2(mc.x - h - 2, mc.y - h - 2),
                         ImVec2(mc.x + h + 2, mc.y + h + 2), true);
        dl->AddImageQuad(
            (ImTextureID)(intptr_t)ctx.mapTex,
            rotPt(-h, -h), rotPt(h, -h), rotPt(h, h), rotPt(-h, h),
            ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1));
        dl->PopClipRect();
    } else {
        // Still building — show loading indicator
        dl->AddText(ImVec2(mc.x - 30, mc.y - 7), IM_COL32(180, 140, 60, 200), "Loading...");
    }

    // Decorative border ring
    dl->AddCircle(mc, h + 6.0f, IM_COL32(180, 140, 60, 240), 64, 3.0f);
    dl->AddCircle(mc, h + 9.0f, IM_COL32(100, 75, 30, 160), 64, 1.5f);

    // ── Compass rose ─────────────────────────────────────────────────────────
    // North is world −Z; on the unrotated texture it's at top (−Y screen).
    // After rotation by ang, north appears at screen direction (sr, −cr).
    float nr = h + 22.0f;
    ImVec2 nPt  = { mc.x + sr * nr,       mc.y - cr * nr };
    ImVec2 sPt  = { mc.x - sr * (nr - 6), mc.y + cr * (nr - 6) };
    ImVec2 ePt  = { mc.x + cr * nr,       mc.y + sr * nr };
    ImVec2 wPt  = { mc.x - cr * (nr - 6), mc.y - sr * (nr - 6) };
    dl->AddText(ImVec2(nPt.x - 4, nPt.y - 8),  IM_COL32(255, 80,  80,  240), "N");
    dl->AddText(ImVec2(sPt.x - 4, sPt.y - 8),  IM_COL32(200, 200, 200, 180), "S");
    dl->AddText(ImVec2(ePt.x - 4, ePt.y - 8),  IM_COL32(200, 200, 200, 180), "E");
    dl->AddText(ImVec2(wPt.x - 4, wPt.y - 8),  IM_COL32(200, 200, 200, 180), "W");

    // ── Player markers ────────────────────────────────────────────────────────
    float texCX = ctx.mapBuiltCX + ctx.mapPanX;
    float texCZ = ctx.mapBuiltCZ + ctx.mapPanZ;

    // Right-click (a click, not a rotate-drag) teleports the player there.
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        if (dd.x * dd.x + dd.y * dd.y < 36.0f) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            float lx = mp.x - mc.x, ly = mp.y - mc.y;
            if (lx * lx + ly * ly < h * h) {          // inside the map disc
                float wdx =  lx * cr + ly * sr;       // undo the map rotation
                float wdz = -lx * sr + ly * cr;
                float wx  = texCX + wdx / h * worldRadius;
                float wz  = texCZ + wdz / h * worldRadius;
                ctx.spawnX = (int)floorf(wx);
                ctx.spawnZ = (int)floorf(wz);
                int gy = sampleSurfaceSolid(ctx.spawnX, ctx.spawnZ);
                ctx.camera.position = glm::vec3((float)ctx.spawnX + 0.5f,
                                                (float)(gy + 2), (float)ctx.spawnZ + 0.5f);
                ctx.camera.velocity = glm::vec3(0.0f);
                ctx.spawnedOnGround = false;          // re-grounds when the chunk loads
                ctx.mapPanX = 0.0f;
                ctx.mapPanZ = 0.0f;
                ctx.mapNeedsRebuild = true;
            }
        }
    }

    auto worldToMap = [&](float wx, float wz) -> ImVec2 {
        float dx = (wx - texCX) / worldRadius * h;
        float dz = (wz - texCZ) / worldRadius * h;
        return { mc.x + dx * cr - dz * sr,
                 mc.y + dx * sr + dz * cr };
    };

    // ── Towns: settlement markers + names ────────────────────────────────────
    {
        const TownPlan& plan = getTownPlan();
        bool showNames = worldRadius < 1100.0f;   // hide labels when far zoomed out
        for (const Town& t : plan.towns) {
            ImVec2 sp = worldToMap((float)t.center.x, (float)t.center.y);
            float  d2 = (sp.x - mc.x) * (sp.x - mc.x) + (sp.y - mc.y) * (sp.y - mc.y);
            if (d2 >= h * h) continue;

            ImU32 col;
            switch (t.type) {
                case TownType::Coastal:  col = IM_COL32( 90, 170, 230, 235); break;
                case TownType::Mountain: col = IM_COL32(205, 205, 210, 235); break;
                default:                 col = IM_COL32(120, 200, 110, 235); break;
            }
            dl->AddRectFilled({ sp.x - 4, sp.y - 4 }, { sp.x + 4, sp.y + 4 }, col, 1.0f);
            dl->AddRect({ sp.x - 4, sp.y - 4 }, { sp.x + 4, sp.y + 4 },
                        IM_COL32(0, 0, 0, 190), 1.0f, 0, 1.5f);

            if (showNames && !t.name.empty()) {
                ImVec2 ts = ImGui::CalcTextSize(t.name.c_str());
                ImVec2 tp = { sp.x - ts.x * 0.5f, sp.y + 6.0f };
                dl->AddText({ tp.x + 1, tp.y + 1 }, IM_COL32(0, 0, 0, 210), t.name.c_str());
                dl->AddText(tp, IM_COL32(245, 235, 200, 245), t.name.c_str());
            }
        }
    }

    // Local player: white triangle pointing in facing direction
    {
        ImVec2 sp = worldToMap(ctx.camera.position.x, ctx.camera.position.z);
        float  d2 = (sp.x - mc.x) * (sp.x - mc.x) + (sp.y - mc.y) * (sp.y - mc.y);
        if (d2 < h * h) {
            // Player yaw: 0=+Z(south), 90=+X(east)
            float yawr = glm::radians(ctx.playerYaw);
            float fdx  = sinf(yawr) * cr - cosf(yawr) * sr; // map-rotated facing dir
            float fdy  = sinf(yawr) * sr + cosf(yawr) * cr;
            float rdx  = fdy, rdy = -fdx; // right perpendicular
            dl->AddTriangleFilled(
                { sp.x + fdx * 11.f,               sp.y + fdy * 11.f               },
                { sp.x + rdx * 5.f - fdx * 5.f,   sp.y + rdy * 5.f - fdy * 5.f   },
                { sp.x - rdx * 5.f - fdx * 5.f,   sp.y - rdy * 5.f - fdy * 5.f   },
                IM_COL32(255, 255, 255, 230));
            dl->AddTriangle(
                { sp.x + fdx * 11.f,               sp.y + fdy * 11.f               },
                { sp.x + rdx * 5.f - fdx * 5.f,   sp.y + rdy * 5.f - fdy * 5.f   },
                { sp.x - rdx * 5.f - fdx * 5.f,   sp.y - rdy * 5.f - fdy * 5.f   },
                IM_COL32(0, 0, 0, 160), 1.2f);
        }
    }

    // Remote players: gold dots + name labels
    for (auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        ImVec2 sp = worldToMap(p.position.x, p.position.z);
        float  d2 = (sp.x - mc.x) * (sp.x - mc.x) + (sp.y - mc.y) * (sp.y - mc.y);
        if (d2 < h * h) {
            dl->AddCircleFilled(sp, 5.5f, IM_COL32(255, 215, 40, 230));
            dl->AddCircle(sp, 5.5f, IM_COL32(0, 0, 0, 160), 10, 1.5f);
            const std::string& nm = p.name.empty() ? std::to_string(p.id) : p.name;
            dl->AddText({ sp.x + 8.f, sp.y - 7.f }, IM_COL32(255, 215, 40, 210), nm.c_str());
        }
    }

    // ── Controls hint ─────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::TextDisabled("LMB drag: Pan   |   RMB drag: Rotate   |   RMB click: Teleport   |   Scroll: Zoom   |   M / Esc: Close");

    ImGui::End();

    // Close via 'X' button resets cursor
    if (!ctx.showMap) {
        glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        ctx.firstMouse = true;
    }
}

// ---------------------------------------------------------------------------
// Play-state UI (HUD, chat, player list, nametags)
// ---------------------------------------------------------------------------

static void drawNametag(const glm::vec3& worldPos, const std::string& name,
                        const glm::mat4& view, const glm::mat4& proj, int fbW, int fbH) {
    if (name.empty()) return;
    glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.01f) return;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) return;
    float sx = (ndc.x * 0.5f + 0.5f) * (float)fbW;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)fbH;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ts  = ImGui::CalcTextSize(name.c_str());
    ImVec2 pos(sx - ts.x * 0.5f, sy - 28.0f);
    dl->AddRectFilled(ImVec2(pos.x - 4, pos.y - 2),
                      ImVec2(pos.x + ts.x + 4, pos.y + ts.y + 2),
                      IM_COL32(20, 12, 8, 180), 4.0f);
    dl->AddText(pos, IM_COL32(235, 220, 190, 255), name.c_str());
}

// Draws a small health bar at a world position (used over damaged NPCs).
static void drawHealthBar(const glm::vec3& worldPos, float frac,
                          const glm::mat4& view, const glm::mat4& proj,
                          int fbW, int fbH) {
    glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.01f) return;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) return;
    float sx = (ndc.x * 0.5f + 0.5f) * (float)fbW;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)fbH;
    frac = std::clamp(frac, 0.0f, 1.0f);
    const float W = 46.0f, H = 6.0f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(sx - W * 0.5f - 1, sy - 1),
                      ImVec2(sx + W * 0.5f + 1, sy + H + 1), IM_COL32(15, 10, 8, 200));
    dl->AddRectFilled(ImVec2(sx - W * 0.5f, sy),
                      ImVec2(sx - W * 0.5f + W * frac, sy + H), IM_COL32(200, 45, 40, 255));
}

// F3 debug / session overlay — performance, world, rendered objects, server.
static void renderDebugOverlay(AppContext& ctx) {
    // Tally the live client-side objects by kind.
    int vill = 0, band = 0, guard = 0, anim = 0, ferry = 0;
    int door = 0, prop = 0, light = 0, remote = 0;
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead) continue;
        switch (o->kind) {
            case ObjectKind::NPC: {
                NPCType t = static_cast<NPC*>(o.get())->type;
                if      (t == NPCType::Villager) vill++;
                else if (t == NPCType::Enemy)    band++;
                else if (t == NPCType::Guard)    guard++;
                break;
            }
            case ObjectKind::Animal:  anim++;  break;
            case ObjectKind::Vehicle: ferry++; break;
            case ObjectKind::Door:    door++;  break;
            case ObjectKind::Prop: {
                prop++;
                PropType pt = static_cast<Prop*>(o.get())->type;
                if (pt == PropType::Lantern || pt == PropType::StreetLamp) light++;
                break;
            }
            case ObjectKind::Player:
                if (o->id != 0) remote++;
                break;
            default: break;
        }
    }
    int totalObj = (int)ctx.objectManager.objects().size();

    const glm::vec3& cp = ctx.camera.position;
    int cx = (int)floorf(cp.x / 16.0f), cz = (int)floorf(cp.z / 16.0f);
    SurfaceSample surf = sampleSurface((int)cp.x, (int)cp.z);
    static const char* kBiomes[] = { "Plains", "Forest", "Desert", "Mountains",
                                     "Tundra", "Savanna", "Jungle" };
    const char* biome = (surf.biome >= 0 && surf.biome < 7) ? kBiomes[surf.biome] : "?";
    float gt = ctx.gameTime;
    const char* phase = (gt < 0.23f || gt > 0.77f) ? "Night"
                      : (gt < 0.30f)               ? "Dawn"
                      : (gt > 0.70f)               ? "Dusk" : "Day";

    ImGuiIO& io = ImGui::GetIO();
    static float fpsHist[90] = {};
    static int   fpsPos = 0;
    fpsHist[fpsPos] = io.Framerate;
    fpsPos = (fpsPos + 1) % 90;

    const ImVec4 head(0.62f, 0.86f, 1.0f, 1.0f);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::Begin("Debug", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "TERRAX DEBUG   [F3]");
    ImGui::Separator();

    ImGui::TextColored(head, "Performance");
    ImGui::Text("FPS %.0f   frame %.2f ms", io.Framerate,
                io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
    ImGui::PlotLines("##fps", fpsHist, 90, fpsPos, nullptr, 0.0f, 240.0f,
                     ImVec2(238, 38));

    ImGui::TextColored(head, "World");
    ImGui::Text("Pos    %.1f, %.1f, %.1f", cp.x, cp.y, cp.z);
    ImGui::Text("Chunk  %d, %d   loaded %d", cx, cz, (int)ctx.world.chunks.size());
    ImGui::Text("Biome  %s   render dist %d", biome, ctx.world.renderDistance);
    ImGui::Text("Time   %.2f  (%s)", gt, phase);

    ImGui::TextColored(head, "Rendered objects (%d)", totalObj);
    ImGui::Text("Villagers %d   Bandits %d   Guards %d", vill, band, guard);
    ImGui::Text("Animals %d   Ferries %d", anim, ferry);
    ImGui::Text("Doors/houses %d   Props %d  (lights %d)", door, prop, light);
    ImGui::Text("Remote players %d", remote);

    ImGui::TextColored(head, "Server");
    if (g_serverStats.running.load()) {
        int sv = g_serverStats.villagers.load();
        int sb = g_serverStats.bandits.load();
        int sg = g_serverStats.guards.load();
        ImGui::Text("running   tick %.2f ms   players %d",
                    g_serverStats.tickMs.load(), g_serverStats.players.load());
        ImGui::Text("NPCs %d  (V %d  B %d  G %d)", sv + sb + sg, sv, sb, sg);
        ImGui::Text("Animals %d   Ferries %d",
                    g_serverStats.animals.load(), g_serverStats.ferries.load());
    } else {
        ImGui::TextDisabled("not hosting (remote server)");
    }

    ImGui::TextColored(head, "Camera");
    ImGui::Text("Yaw %.1f   Pitch %.1f   Noclip %s",
                ctx.playerYaw, ctx.camera.pitch, ctx.noclip ? "ON" : "off");

    ImGui::End();
}

void renderPlayUI(AppContext& ctx, GLFWwindow* window, const Renderer& renderer) {
    (void)window;

    if (ctx.showDebugOverlay) renderDebugOverlay(ctx);

    // Underwater tint
    if (ctx.headUnderwater) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT),
                          IM_COL32(15, 60, 140, 90));
    }

    // Bow draw charge bar — visible while the player is holding left
    // mouse with a bow equipped. Centred above the reticle so it shows
    // up clearly while aiming. Colour shifts from amber → bright gold
    // as the draw approaches full.
    if (ctx.bowChargingHeld && ctx.bowCharge > 0.0f) {
        float w = 220.0f;
        ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - w * 0.5f,
                                       WINDOW_HEIGHT / 2 - 80));
        ImGui::SetNextWindowSize(ImVec2(w, 22));
        ImGui::Begin("BowCharge", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
        float t = ctx.bowCharge;
        ImVec4 col(0.85f + 0.10f * t, 0.55f + 0.40f * t, 0.20f + 0.15f * t, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(t, ImVec2(-1, 16),
                           t >= 0.99f ? "MAX" : "");
        ImGui::PopStyleColor();
        ImGui::End();
    }

    // Breath bar
    if (ctx.breathTime < 29.9f) {
        ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT - 100));
        ImGui::SetNextWindowSize(ImVec2(300, 18));
        ImGui::Begin("Breath", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
        float bfrac  = ctx.breathTime / 30.0f;
        ImVec4 barCol = bfrac > 0.4f ? ImVec4(0.2f, 0.55f, 1.0f, 1.0f)
                                      : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
        ImGui::ProgressBar(bfrac, ImVec2(-1, 14), "");
        ImGui::PopStyleColor();
        ImGui::End();
    }

    // HUD — health, level + XP bar.
    ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT - 90));
    ImGui::SetNextWindowSize(ImVec2(300, 78));
    ImGui::Begin("HUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoMove);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
    ImGui::ProgressBar(ctx.playerHealth, ImVec2(-1, 14), "");
    ImGui::PopStyleColor();

    // XP bar sits just below health. ProgressBar fraction is XP toward
    // the next level — matching `xpForNextLevel` in gameplay.cpp.
    int xpNeed = 100 + 50 * (ctx.playerLevel - 1);
    float xpFrac = (xpNeed > 0) ? (ctx.playerXp / float(xpNeed)) : 0.0f;
    if (xpFrac > 1.0f) xpFrac = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.42f, 0.65f, 1.00f, 1.0f));
    ImGui::ProgressBar(xpFrac, ImVec2(-1, 8), "");
    ImGui::PopStyleColor();
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f),
                       "Lv %d   %d/%d XP   HP %d/100",
                       ctx.playerLevel, (int)ctx.playerXp, xpNeed,
                       (int)(ctx.playerHealth * 100));
    ImGui::End();

    // Nametags
    for (auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        std::string label = p.name.empty() ? ("Player" + std::to_string(p.id)) : p.name;
        drawNametag(p.position + glm::vec3(0.0f, 2.1f, 0.0f), label,
                    renderer.frameView, renderer.frameProj,
                    renderer.frameFbW, renderer.frameFbH);
    }

    // NPC interaction — nametag + talk prompt for the villager being faced,
    // and the dialogue box once a conversation has been started.
    if (!ctx.talkTargetName.empty()) {
        drawNametag(ctx.talkTargetPos + glm::vec3(0.0f, 2.1f, 0.0f),
                    ctx.talkTargetName, renderer.frameView, renderer.frameProj,
                    renderer.frameFbW, renderer.frameFbH);
        if (ctx.talkTimer <= 0.0f) {
            ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 70, WINDOW_HEIGHT / 2 + 36));
            ImGui::SetNextWindowSize(ImVec2(140, 26));
            ImGui::Begin("TalkHint", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
            ImGui::TextColored(ImVec4(0.96f, 0.90f, 0.70f, 1.0f), "[E] Talk");
            ImGui::End();
        }
    }
    if (ctx.talkTimer > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 220, WINDOW_HEIGHT - 172));
        ImGui::SetNextWindowSize(ImVec2(440, 80));
        ImGui::Begin("NpcDialogue", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", ctx.talkName.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", ctx.talkLine.c_str());
        ImGui::End();
    }

    // Loot pickup prompt — shown when the player is in range of a drop.
    // Sits just above the reticle so the player doesn't have to look down
    // to read it.
    if (!ctx.lootHintName.empty()) {
        float boxW = 320.0f;
        ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - boxW * 0.5f,
                                       WINDOW_HEIGHT / 2 + 60));
        ImGui::SetNextWindowSize(ImVec2(boxW, 32));
        ImGui::Begin("LootHint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs);
        ImVec4 c(ctx.lootHintColor.r / 255.0f,
                 ctx.lootHintColor.g / 255.0f,
                 ctx.lootHintColor.b / 255.0f, 1.0f);
        ImGui::TextColored(ImVec4(0.96f, 0.90f, 0.70f, 1.0f), "[E] Pick up");
        ImGui::SameLine();
        ImGui::TextColored(c, "%s", ctx.lootHintName.c_str());
        ImGui::End();
    }

    // NPC health bars over damaged NPCs.
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        NPC* n = static_cast<NPC*>(o.get());
        if (n->dyingFlag || n->health >= 99.5f) continue;
        drawHealthBar(n->position + glm::vec3(0.0f, 2.3f, 0.0f), n->health / 100.0f,
                      renderer.frameView, renderer.frameProj,
                      renderer.frameFbW, renderer.frameFbH);
    }

    // Chat
    if (ctx.client) {
        const float logH = 120.0f;
        ImGui::SetNextWindowPos(ImVec2(12, WINDOW_HEIGHT - logH - (ctx.chatOpen ? 90.0f : 12.0f)));
        ImGui::SetNextWindowSize(ImVec2(420, logH));
        ImGui::Begin("ChatLog", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.07f, 0.05f, 0.75f));
        if (ImGui::BeginChild("scroll", ImVec2(-1, -1), true)) {
            size_t start = ctx.client->chatLog.size() > 8 ? ctx.client->chatLog.size() - 8 : 0;
            for (size_t i = start; i < ctx.client->chatLog.size(); i++) {
                const auto& m = ctx.client->chatLog[i];
                ImGui::TextWrapped("[%s] %s", m.senderName.c_str(), m.text.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::End();

        if (ctx.chatOpen) {
            ImGui::SetNextWindowPos(ImVec2(12, WINDOW_HEIGHT - 72));
            ImGui::SetNextWindowSize(ImVec2(420, 56));
            ImGui::Begin("ChatInput", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##chat", ctx.chatInput, sizeof(ctx.chatInput),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter && ctx.chatInput[0] != '\0') {
                ctx.client->sendChat(ctx.chatInput);
                ctx.chatInput[0] = '\0';
                ctx.chatOpen     = false;
            }
            ImGui::TextDisabled("Enter to send, Esc to close");
            ImGui::End();
        }
    }

    // World map overlay
    renderMapUI(ctx);

    // Player list
    if (ctx.showPlayerList) {
        ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH - 220, 12));
        ImGui::SetNextWindowSize(ImVec2(200, 180));
        ImGui::Begin("Players", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::Text("Online");
        ImGui::Separator();
        ImGui::Text("%s (you)", ctx.playerName);
        for (auto& [id, p] : ctx.remotePlayers) {
            (void)id;
            ImGui::Text("%s", p.name.empty()
                ? ("Player" + std::to_string(p.id)).c_str()
                : p.name.c_str());
        }
        ImGui::End();
    }

    // I and C both pull up the unified equipment screen (inventory grid +
    // character loadout side by side), so drag-drop between them works.
    if (ctx.showInventory || ctx.showCharacterLoadout) {
        renderInventoryUI(ctx, window);
        renderCharacterLoadoutUI(ctx, window);
    }

    // Toast queue — XP / level-up / loot notifications stacked top-right.
    // Newest at the bottom of the stack so the eye lands on the latest
    // message. Older entries fade out as their lifeTime ticks down.
    if (!ctx.toasts.empty()) {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW <= 0) fbW = WINDOW_WIDTH;
        if (fbH <= 0) fbH = WINDOW_HEIGHT;
        float toastW = 320.0f;
        float lineH  = 22.0f;
        float winH   = (float)ctx.toasts.size() * lineH + 12.0f;
        ImGui::SetNextWindowPos(ImVec2(fbW - toastW - 16.0f, 80.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(toastW, winH), ImGuiCond_Always);
        ImGui::Begin("##toasts", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
        for (auto& t : ctx.toasts) {
            float alpha = (t.lifeTime > 1.0f) ? 1.0f : t.lifeTime;
            ImU32 bg = IM_COL32(15, 10, 8, (int)(alpha * 200));
            ImU32 br = IM_COL32(t.color.r, t.color.g, t.color.b,
                                (int)(alpha * 220));
            ImU32 tx = IM_COL32(t.color.r, t.color.g, t.color.b,
                                (int)(alpha * 255));

            // Decorate via the draw list (doesn't touch ImGui layout).
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1(p0.x + toastW - 16.0f, p0.y + lineH - 2);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, p1, bg);
            dl->AddRect(p0, p1, br);
            dl->AddText(ImVec2(p0.x + 6, p0.y + 2), tx, t.text.c_str());

            // Reserve a layout row so ImGui knows about this toast and
            // grows the window's tracked content extent.
            ImGui::Dummy(ImVec2(toastW - 16.0f, lineH));
        }
        ImGui::End();
    }
}

// renderInventoryUI / renderCharacterLoadoutUI live in src/inventory_ui.cpp.
