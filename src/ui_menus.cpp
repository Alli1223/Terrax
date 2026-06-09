// Out-of-game and overlay panels — main/join menu, settings, the loading
// screen and the pause overlay. Split out of ui.cpp; shared viewport helpers
// live in ui_internal.h.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui.h"
#include "app_context.h"
#include "renderer.h"
#include "game_session.h"
#include "gameplay.h"
#include "character_save.h"
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
    ImGui::SetNextWindowPos(vpCentered(400.0f, 480.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(400, 480), ImGuiCond_Always);
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
        ImGui::SetNextWindowPos(vpCentered(320.0f, 468.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320, 468), ImGuiCond_Always);
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
            ctx.weOwnServer  = true;
            beginLoading(ctx);                  // spawns the worker + state=Loading
        }
        if (ImGui::Button("Host Game", ImVec2(-1, 36))) {
            disconnectFromGame(ctx);
            applyRenderDistanceSetting(ctx);
            ctx.sessionMode  = SessionMode::Host;
            ctx.connectHost  = "127.0.0.1";
            ctx.connectPort  = DEFAULT_SERVER_PORT;
            ctx.weOwnServer  = true;
            beginLoading(ctx);
        }
        if (ImGui::Button("Join Game", ImVec2(-1, 36))) {
            ctx.state = GameState::JoinMenu;
        }
        if (ImGui::Button("Character Editor", ImVec2(-1, 36))) {
            ctx.characterCreationMode = false;   // standalone tool, not roster creation
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

        ImGui::SetNextWindowPos(vpCentered(320.0f, 240.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_Always);
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
            // Pick (or create) the character to play as before loading the world.
            ctx.state = GameState::CharacterSelect;
        }
        if (ImGui::Button("Back", ImVec2(-1, 36))) {
            ctx.state = GameState::MainMenu;
        }
        ImGui::End();
    }
}

// ---------------------------------------------------------------------------
// Character Select (shown after the Join menu, before loading the world)
// ---------------------------------------------------------------------------
void renderCharacterSelectUI(AppContext& ctx, GLFWwindow* window) {
    (void)window;
    // The on-disk roster is the source of truth — reload it every frame so
    // creates (saved in the editor) and deletes show up immediately.
    std::vector<CharacterSave> roster = loadRoster();
    const char* roleNames[3] = { "Tank", "DPS", "Healer" };

    const float W = 460.0f, H = 430.0f;
    ImGui::SetNextWindowPos(vpCentered(W, H), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Select Character", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "Choose your character");
    ImGui::TextDisabled("Connecting to %s:%d", ctx.connectHost.c_str(), (int)ctx.connectPort);
    ImGui::Separator();

    ImGui::BeginChild("roster", ImVec2(0, H - 150.0f), true);
    if (roster.empty())
        ImGui::TextDisabled("No saved characters yet — create one below.");
    for (int i = 0; i < (int)roster.size(); i++) {
        const CharacterSave& c = roster[i];
        const char* rn = (c.role < 3) ? roleNames[c.role] : "?";
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", c.name[0] ? c.name : "Unnamed");
        ImGui::SameLine(190.0f); ImGui::TextDisabled("%s  Lv%d", rn, (int)c.level);
        ImGui::SameLine(300.0f);
        if (ImGui::SmallButton("Play")) {
            ctx.activeCharacter = i;
            applyCharacterToContext(ctx, c);
            beginLoading(ctx);
            ImGui::PopID(); ImGui::EndChild(); ImGui::End();
            return;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            roster.erase(roster.begin() + i);
            saveRoster(roster);
            ImGui::PopID();
            i--; continue;              // next frame reloads from disk anyway
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Create New", ImVec2(-1, 32))) {
        // Seed a fresh level-1 character (keeping the current name) and open the
        // editor in creation mode.
        ctx.activeCharacter       = -1;
        ctx.characterCreationMode = true;
        ctx.editorCharType        = 0;
        ctx.playerRole            = PlayerRole::DPS;
        ctx.playerLevel           = 1;
        ctx.playerXp              = 0.0f;
        ctx.skillPoints           = 0;
        if (ctx.playerRig) {
            ctx.playerRig->setupDefaultHuman(true);
            ctx.playerRig->randomizeAppearance();
            ctx.playerRig->heightScale = roleHeightScale(ctx.playerRole);
            ctx.playerRig->weightScale = roleWeightScale(ctx.playerRole);
            if (ctx.playerRig->torso) {
                ctx.playerRig->torso->scale.x = ctx.playerRig->weightScale;
                ctx.playerRig->torso->scale.z = ctx.playerRig->weightScale;
            }
        }
        ctx.setupRoleLoadout();
        if (ctx.playerRig) rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        ctx.state = GameState::CharacterEditor;
    }
    if (ImGui::Button("Back", ImVec2(-1, 28)))
        ctx.state = GameState::JoinMenu;

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Loading screen (background world generation)
// ---------------------------------------------------------------------------

// The high-level loading stages. Indexes into ctx.loadingHighStage.
// 0..(N-2) cover the town survey + finalisation; the last name "Ready" is
// briefly shown right before the transition to Playing.
namespace {
const char* const kLoadingStageNames[] = {
    "Starting server",
    "Generating world",   // covers all the town survey sub-stages
    "Placing furniture",
    "Building meshes",
    "Connecting",
    "Ready",
};
constexpr int kLoadingStageCount = (int)(sizeof(kLoadingStageNames)
                                      / sizeof(kLoadingStageNames[0]));

void setHighStage(AppContext& ctx, int stage, float frac) {
    ctx.loadingHighStage.store(stage, std::memory_order_release);
    ctx.loadingHighFraction.store(frac, std::memory_order_release);
}
}  // namespace

void beginLoading(AppContext& ctx) {
    // If a previous loading run is still hanging around, join it before
    // starting a new one — clicking Host twice in a row is otherwise UB.
    if (ctx.loadingThread.joinable()) ctx.loadingThread.join();

    ctx.loadingWorkerDone.store(false, std::memory_order_release);
    ctx.loadingFinalised = false;
    setHighStage(ctx, 0, 0.0f);
    ctx.state = GameState::Loading;

    // The worker runs every step that doesn't need the GL context. propLibrary
    // mesh upload happens on the main thread in renderLoadingUI() once this
    // worker reports done.
    ctx.loadingThread = std::thread([&ctx]() {
        try {
            // Stage 0 — boot the embedded server (no-op for Join mode).
            if (ctx.weOwnServer) {
                setHighStage(ctx, 0, 0.2f);
                startEmbeddedServer(ctx.connectPort, ctx.settings.renderDistance);
                setHighStage(ctx, 0, 1.0f);

                // Stage 1 — survey, town layout, road routing. This is the
                // expensive one; the town atomics report fine-grained progress.
                setHighStage(ctx, 1, 0.0f);
                (void)getTownPlan();
                setHighStage(ctx, 1, 1.0f);

                // Stage 2 — derive furniture and door placements (depend on
                // the town plan, no GL needed).
                setHighStage(ctx, 2, 0.0f);
                (void)getPropPlacements();
                (void)getDoorPlacements();
                setHighStage(ctx, 2, 1.0f);
            } else {
                // Join mode skips straight to the connect stage.
                setHighStage(ctx, 4, 0.5f);
            }
        } catch (const std::exception& e) {
            std::cerr << "[Loading] worker exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[Loading] worker exception: unknown" << std::endl;
        }
        ctx.loadingWorkerDone.store(true, std::memory_order_release);
    });
}

bool renderLoadingUI(AppContext& ctx, GLFWwindow* /*window*/) {
    // Compute the displayed fraction. While stage 1 (town survey) is running,
    // sub-fractions come from gTownBuildFraction so the bar moves continuously
    // during the long terrain sweep. Other stages use loadingHighFraction.
    const int highStage = ctx.loadingHighStage.load(std::memory_order_acquire);
    float overall = 0.0f;
    const char* sub = nullptr;
    if (highStage == 1 && !ctx.loadingWorkerDone.load(std::memory_order_acquire)) {
        const int   ts = gTownBuildStage.load(std::memory_order_acquire);
        const float tf = gTownBuildFraction.load(std::memory_order_acquire);
        // Stage 1 spans [0.10, 0.85] of the total bar, divided across the
        // town sub-stages. Anything past the survey lives in [0.85, 1.0].
        const float low = 0.10f, high = 0.85f;
        const float perSub = (high - low) / (float)kTownBuildStageCount;
        overall = low + perSub * ((float)ts + std::clamp(tf, 0.0f, 1.0f));
        if (ts >= 0 && ts < kTownBuildStageCount) sub = kTownBuildStageNames[ts];
    } else {
        // Highstage 0 → [0.00, 0.10], 1 done → 0.85, 2 → [0.85, 0.92],
        // 3 (meshes, set on main thread) → [0.92, 0.97], 4 (connect) → 0.99.
        static const float bounds[][2] = {
            {0.00f, 0.10f}, {0.10f, 0.85f}, {0.85f, 0.92f},
            {0.92f, 0.97f}, {0.97f, 0.99f}, {1.00f, 1.00f},
        };
        const int s = std::clamp(highStage, 0, kLoadingStageCount - 1);
        const float f = std::clamp(
            ctx.loadingHighFraction.load(std::memory_order_acquire), 0.0f, 1.0f);
        overall = bounds[s][0] + (bounds[s][1] - bounds[s][0]) * f;
        sub = kLoadingStageNames[s];
    }

    // Centred translucent loading window.
    const float W = 460.0f, H = 160.0f;
    ImGui::SetNextWindowPos(vpCentered(W, H), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Loading", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextUnformatted("Loading world");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    // Animated label so the user sees movement even when an individual stage
    // hasn't moved its fraction in a moment (e.g. routing highways).
    static const char DOTS[] = {' ', '.', ':', '*'};
    int dot = (int)(ImGui::GetTime() * 3.0f) & 3;
    char label[128];
    snprintf(label, sizeof(label), "%s%c", sub ? sub : "Working", DOTS[dot]);
    ImGui::TextUnformatted(label);
    ImGui::Dummy(ImVec2(0, 4));

    ImGui::ProgressBar(std::clamp(overall, 0.0f, 1.0f),
                       ImVec2(-1, 18), nullptr);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("This only happens the first time you enter a world.");

    ImGui::End();

    // Main-thread finalisation: once the worker reports done, build the prop
    // library on the main thread (it issues GL calls and must run here).
    if (ctx.loadingWorkerDone.load(std::memory_order_acquire) &&
        !ctx.loadingFinalised)
    {
        setHighStage(ctx, 3, 0.0f);
        ctx.propLibrary.buildAll();
        setHighStage(ctx, 3, 1.0f);
        // The actual network connect happens on the first gameplay frame.
        // Mark the high-level stage so the bar shows "Connecting" briefly.
        setHighStage(ctx, 4, 0.5f);
        ctx.loadingFinalised = true;
    }

    if (ctx.loadingFinalised) {
        if (ctx.loadingThread.joinable()) ctx.loadingThread.join();
        setHighStage(ctx, 5, 1.0f);
        return true;
    }
    return false;
}

void renderPauseMenuUI(AppContext& ctx, GLFWwindow* window) {
    ImGui::SetNextWindowPos(vpCentered(280.0f, 200.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_Always);
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

