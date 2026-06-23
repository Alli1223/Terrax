#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gl_loader.h"
#include "app_context.h"
#include "renderer.h"
#include "input.h"
#include "gameplay.h"
#include "ui.h"
#include "game_session.h"
#include "graphics_settings.h"
#include "audio.h"
#include "screenshot.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <iostream>
#include <string>
#include <algorithm>

int main(int argc, char** argv) {
    bool tourMode = false;   // --screenshot-tour: auto-fly the world and dump PNGs
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--server") { runDedicatedServer(); return 0; }
        if (std::string(argv[i]) == "--screenshot-tour") tourMode = true;
    }

    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }

    GameSettings settings;
    GLFWwindow* window = createGameWindow(settings);
    if (!window) { std::cerr << "Window creation failed\n"; glfwTerminate(); return 1; }
    if (!gl_load()) { std::cerr << "Failed to load OpenGL functions\n"; return 1; }

    AppContext ctx;
    ctx.settings = settings;
    applyGraphicsSettings(window, ctx);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    setupInputCallbacks(window, ctx);
    initImGui(window);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    int initW = 0, initH = 0;
    glfwGetFramebufferSize(window, &initW, &initH);
    if (initW <= 0) initW = ctx.settings.windowWidth;
    if (initH <= 0) initH = ctx.settings.windowHeight;

    Renderer renderer;
    if (!renderer.init(initW, initH)) {
        std::cerr << "Renderer init failed\n";
        return 1;
    }

    // Audio is client-only and optional: if no device can be opened the game just
    // runs silent (g_audio stays null and every sound call becomes a no-op).
    AudioSystem audio;
    if (audio.init()) g_audio = &audio;

    // Last framebuffer size the offscreen targets were sized to. Tracked so the
    // main loop can re-fit them whenever the window is resized by any means.
    int lastFbW = initW, lastFbH = initH;

    // --screenshot-tour: kick straight into a singleplayer session, then the loop
    // below flies the camera out through the danger tiers, dumping a PNG at each.
    // Waypoints are distances (blocks) from spawn along +X — one per danger band.
    const int   tourDist[]  = { 0, 1500, 4000, 9000, 20000 };
    const int   tourCount   = (int)(sizeof(tourDist) / sizeof(tourDist[0]));
    int         tourStage   = -1;     // -1 = waiting for the world to come up
    double      tourStageT  = 0.0;    // time the current stage began
    bool        tourShot    = false;  // captured the current stage yet?
    const double TOUR_SETTLE = 4.0;   // seconds to let chunks stream in per stop
    if (tourMode) {
        ctx.sessionMode = SessionMode::Singleplayer;
        ctx.connectHost = "127.0.0.1";
        ctx.connectPort = DEFAULT_SERVER_PORT;
        ctx.weOwnServer = true;
        beginLoading(ctx);            // spawns the world worker; state → Loading
    }

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        ctx.deltaTime = std::min(currentFrame - ctx.lastFrame, 0.05f);
        ctx.lastFrame = currentFrame;

        glfwPollEvents();

        // Keep the GL viewport and the renderer's offscreen targets matched to
        // the live framebuffer size. Dragging or maximising the window, toggling
        // fullscreen, or changing the resolution preset all flow through here, so
        // the scene re-fits the screen instead of stretching the old-size
        // buffers. Reacts only when the size actually changes.
        {
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            if (fbW > 0 && fbH > 0 && (fbW != lastFbW || fbH != lastFbH)) {
                lastFbW = fbW; lastFbH = fbH;
                glViewport(0, 0, fbW, fbH);
                renderer.resizeFramebuffers(fbW, fbH);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        switch (ctx.state) {
        case GameState::MainMenu:
        case GameState::SettingsMenu:
        case GameState::JoinMenu:
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderMenuUI(ctx, window, &renderer);
            break;

        case GameState::CharacterSelect:
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCharacterSelectUI(ctx, window);
            break;

        case GameState::CharacterEditor:
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCharacterEditorUI(ctx, window, renderer);
            break;

        case GameState::HouseEditor:
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderHouseEditorUI(ctx, window, renderer);
            break;

        case GameState::Loading:
            // Background worker pre-warms the world plan and prop placements
            // so the first frame of Playing isn't a multi-second stall.
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glClearColor(0.08f, 0.05f, 0.04f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (renderLoadingUI(ctx, window))
                ctx.state = GameState::Playing;
            break;

        case GameState::Playing:
        case GameState::Paused:
            updateGameplay(ctx, window);
            if (ctx.state == GameState::Playing || ctx.state == GameState::Paused) {
                renderer.renderWorld(ctx, window, currentFrame);
                renderPlayUI(ctx, window, renderer);
                if (ctx.state == GameState::Paused)
                    renderPauseMenuUI(ctx, window);
            }
            break;
        }

        // Autonomous screenshot tour: once the world is up, settle at each
        // waypoint, grab a frame, then teleport further out and repeat.
        if (tourMode && ctx.state == GameState::Playing) {
            double now = glfwGetTime();
            if (tourStage < 0) { tourStage = 0; tourStageT = now; tourShot = false; }  // entered Playing
            double elapsed = now - tourStageT;
            if (elapsed > TOUR_SETTLE && !tourShot) {
                int tier = dangerTierAt((float)tourDist[tourStage], 0.0f);
                char tag[32]; std::snprintf(tag, sizeof(tag), "tier%d_%dm", tier, tourDist[tourStage]);
                ctx.screenshotTag     = tag;
                ctx.requestScreenshot = true;
                tourShot              = true;
            }
            if (elapsed > TOUR_SETTLE + 0.4) {
                int next = tourStage + 1;
                if (next >= tourCount) { glfwSetWindowShouldClose(window, 1); }
                else {
                    tourStage = next; tourStageT = now; tourShot = false;
                    ctx.spawnX = tourDist[tourStage]; ctx.spawnZ = 0;
                    int gy = sampleSurfaceSolid(ctx.spawnX, ctx.spawnZ);
                    ctx.camera.position = glm::vec3((float)ctx.spawnX + 0.5f,
                                                    (float)(gy + 8), (float)ctx.spawnZ + 0.5f);
                    ctx.camera.velocity = glm::vec3(0.0f);
                    ctx.spawnedOnGround = false;     // re-grounds when the chunk loads
                }
            }
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Capture the just-rendered frame (F2, or the tour) before the swap.
        if (ctx.requestScreenshot) {
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            std::string p = saveFramebufferPNG(fbW, fbH, ctx.screenshotTag);
            if (!p.empty()) std::cout << "[Screenshot] saved " << p << "\n";
            else            std::cerr << "[Screenshot] capture failed\n";
            ctx.requestScreenshot = false;
        }

        glfwSwapBuffers(window);
    }

    disconnectFromGame(ctx);
    if (g_audio) { audio.shutdown(); g_audio = nullptr; }
    glfwTerminate();
    return 0;
}
