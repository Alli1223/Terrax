#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui.h"
#include "app_context.h"
#include "renderer.h"
#include "game_session.h"
#include "gameplay.h"
#include "graphics_settings.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <algorithm>

// ---------------------------------------------------------------------------
// ImGui theme
// ---------------------------------------------------------------------------

static void setupFantasyStyle() {
    ImGuiStyle& style  = ImGui::GetStyle();
    ImVec4*     colors = style.Colors;

    ImVec4 bg_base     = ImVec4(0.15f, 0.08f, 0.05f, 1.00f);
    ImVec4 bg_mid      = ImVec4(0.22f, 0.12f, 0.08f, 1.00f);
    ImVec4 gold_bright = ImVec4(0.85f, 0.65f, 0.25f, 1.00f);
    ImVec4 gold_dim    = ImVec4(0.60f, 0.45f, 0.15f, 1.00f);
    ImVec4 parchment   = ImVec4(0.92f, 0.85f, 0.75f, 1.00f);

    colors[ImGuiCol_Text]                  = parchment;
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]              = bg_base;
    colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]               = bg_base;
    colors[ImGuiCol_Border]                = gold_dim;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = bg_mid;
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.30f, 0.18f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.35f, 0.22f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBg]               = bg_base;
    colors[ImGuiCol_TitleBgActive]         = bg_mid;
    colors[ImGuiCol_TitleBgCollapsed]      = bg_base;
    colors[ImGuiCol_MenuBarBg]             = bg_base;
    colors[ImGuiCol_ScrollbarBg]           = bg_base;
    colors[ImGuiCol_ScrollbarGrab]         = gold_dim;
    colors[ImGuiCol_ScrollbarGrabHovered]  = gold_bright;
    colors[ImGuiCol_ScrollbarGrabActive]   = gold_bright;
    colors[ImGuiCol_CheckMark]             = gold_bright;
    colors[ImGuiCol_SliderGrab]            = gold_dim;
    colors[ImGuiCol_SliderGrabActive]      = gold_bright;
    colors[ImGuiCol_Button]                = bg_mid;
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.40f, 0.25f, 0.15f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = gold_dim;
    colors[ImGuiCol_Header]                = bg_mid;
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.35f, 0.20f, 0.12f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = gold_dim;
    colors[ImGuiCol_Separator]             = gold_dim;
    colors[ImGuiCol_SeparatorHovered]      = gold_bright;
    colors[ImGuiCol_SeparatorActive]       = gold_bright;
    colors[ImGuiCol_ResizeGrip]            = gold_dim;
    colors[ImGuiCol_ResizeGripHovered]     = gold_bright;
    colors[ImGuiCol_ResizeGripActive]      = gold_bright;
    colors[ImGuiCol_Tab]                   = bg_base;
    colors[ImGuiCol_TabHovered]            = bg_mid;
    colors[ImGuiCol_TabActive]             = bg_mid;
    colors[ImGuiCol_TabUnfocused]          = bg_base;
    colors[ImGuiCol_TabUnfocusedActive]    = bg_mid;
    colors[ImGuiCol_PlotLines]             = gold_bright;
    colors[ImGuiCol_PlotLinesHovered]      = parchment;
    colors[ImGuiCol_PlotHistogram]         = gold_bright;
    colors[ImGuiCol_PlotHistogramHovered]  = parchment;
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.45f, 0.30f, 0.15f, 1.00f);
    colors[ImGuiCol_DragDropTarget]        = gold_bright;
    colors[ImGuiCol_NavHighlight]          = gold_bright;
    colors[ImGuiCol_NavWindowingHighlight] = gold_bright;
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.20f, 0.15f, 0.10f, 0.60f);

    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(8, 6);
    style.ItemSpacing       = ImVec2(10, 8);
    style.IndentSpacing     = 25.0f;
    style.ScrollbarSize     = 15.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabMinSize       = 12.0f;
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.TabRounding       = 4.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    style.WindowBorderSize  = 2.0f;
    style.FrameBorderSize   = 1.0f;
}

void initImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    setupFantasyStyle();
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
        ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 160, WINDOW_HEIGHT / 2 - 180));
        ImGui::SetNextWindowSize(ImVec2(320, 400));
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
            ctx.playerRig->applyCustomization();
        }
        if (ImGui::Combo("Hair Style", &ctx.playerRig->hairStyle,
                "Bald\0Crew Cut\0Messy Short\0Mohawk\0Spiky\0Side Swept\0"
                "Bob\0Long Straight\0Wavy Long\0Bun\0Pigtails\0Braided\0"))
            ctx.playerRig->applyCustomization();

        float hCol[3] = { ctx.playerRig->hairColor.r / 255.0f,
                          ctx.playerRig->hairColor.g / 255.0f,
                          ctx.playerRig->hairColor.b / 255.0f };
        if (ImGui::ColorEdit3("Hair Color", hCol)) {
            ctx.playerRig->hairColor = { (uint8_t)(hCol[0]*255), (uint8_t)(hCol[1]*255),
                                         (uint8_t)(hCol[2]*255), 255 };
            ctx.playerRig->applyCustomization();
        }
        if (ImGui::Combo("Eyebrow Style", &ctx.playerRig->eyebrowStyle,
                "Straight\0Arched\0Thick\0Thin\0Furrowed\0"))
            ctx.playerRig->applyCustomization();

        float eCol[3] = { ctx.playerRig->eyeColor.r / 255.0f,
                          ctx.playerRig->eyeColor.g / 255.0f,
                          ctx.playerRig->eyeColor.b / 255.0f };
        if (ImGui::ColorEdit3("Eye Color", eCol)) {
            ctx.playerRig->eyeColor = { (uint8_t)(eCol[0]*255), (uint8_t)(eCol[1]*255),
                                        (uint8_t)(eCol[2]*255), 255 };
            ctx.playerRig->applyCustomization();
        }
        if (ImGui::Combo("Eye Type", &ctx.playerRig->eyeType,
                "Classic\0Happy\0Wide\0Slanted\0Heart\0Wink\0Tired\0Star\0Tears\0Determined\0"))
            ctx.playerRig->applyCustomization();
        if (ImGui::Combo("Nose Style", &ctx.playerRig->noseStyle,
                "Button\0Wide\0Narrow\0Upturned\0Broad\0"))
            ctx.playerRig->applyCustomization();
        if (ImGui::Combo("Ear Type", &ctx.playerRig->earType,
                "None\0Human\0Elven\0Rounded\0Wide\0"))
            ctx.playerRig->applyCustomization();
        if (ImGui::Combo("Armor Set", &ctx.playerRig->armorType,
                "None\0Cloth\0Leather\0Heavy\0"))
            ctx.playerRig->applyCustomization();
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

void renderPlayUI(AppContext& ctx, GLFWwindow* window, const Renderer& renderer) {
    (void)window;

    // Underwater tint
    if (ctx.headUnderwater) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT),
                          IM_COL32(15, 60, 140, 90));
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

    // HUD
    ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT - 80));
    ImGui::SetNextWindowSize(ImVec2(300, 60));
    ImGui::Begin("HUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoMove);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
    ImGui::ProgressBar(ctx.playerHealth, ImVec2(-1, 20), "");
    ImGui::PopStyleColor();
    ImGui::Text("Health: %d / 100", (int)(ctx.playerHealth * 100));
    ImGui::End();

    // Nametags
    for (auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        std::string label = p.name.empty() ? ("Player" + std::to_string(p.id)) : p.name;
        drawNametag(p.position + glm::vec3(0.0f, 2.1f, 0.0f), label,
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
}
