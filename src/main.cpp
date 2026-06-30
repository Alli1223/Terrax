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
    // Nudge a target XZ to the nearest land column (surface above sea level) so
    // tour shots land on terrain, not open ocean. Searches outward rings.
    auto findLand = [](int x, int z, int& ox, int& oz) {
        ox = x; oz = z;
        // A good vista anchor is genuinely INLAND — the column AND its surroundings
        // sit a margin above sea level — so the camera looks over terrain rather
        // than a pond or a thin coastal spit (which made far shots all open water).
        auto inland = [](int cx, int cz) {
            if (sampleSurfaceSolid(cx, cz) <= WORLD_SEA_LEVEL + 6) return false;
            const int o[4][2] = { {28,0}, {-28,0}, {0,28}, {0,-28} };
            for (auto& d : o)
                if (sampleSurfaceSolid(cx + d[0], cz + d[1]) <= WORLD_SEA_LEVEL + 1) return false;
            return true;
        };
        if (inland(x, z)) return;
        const int dx[8] = { 1,-1,0,0, 1,1,-1,-1 };
        const int dz[8] = { 0,0,1,-1, 1,-1,1,-1 };
        int fbx = x, fbz = z; bool haveFb = false;        // fallback: any dry land
        for (int r = 300; r <= 9000; r += 300)
            for (int k = 0; k < 8; ++k) {
                int tx = x + dx[k] * r, tz = z + dz[k] * r;
                if (inland(tx, tz)) { ox = tx; oz = tz; return; }
                if (!haveFb && sampleSurfaceSolid(tx, tz) > WORLD_SEA_LEVEL + 1) {
                    fbx = tx; fbz = tz; haveFb = true;
                }
            }
        if (haveFb) { ox = fbx; oz = fbz; }               // no inland spot — take any land
    };
    if (tourMode) {
        ctx.sessionMode = SessionMode::Singleplayer;
        ctx.connectHost = "127.0.0.1";
        ctx.connectPort = DEFAULT_SERVER_PORT;
        ctx.weOwnServer = true;
        ctx.showDebugOverlay = false;   // clean frames for the rendering review
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
                if (!tourMode)                       // clean, HUD-free review frames
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
                    int lx, lz; findLand(tourDist[tourStage], 0, lx, lz);
                    // Sit above the TALLEST nearby column (sampled in a small ring),
                    // not just the anchor's surface, so a camera that lands beside a
                    // tall mesa / cliff clears it instead of clipping into the wall.
                    int gy = sampleSurfaceSolid(lx, lz);
                    for (int ddx = -24; ddx <= 24; ddx += 12)
                        for (int ddz = -24; ddz <= 24; ddz += 12)
                            gy = std::max(gy, sampleSurfaceSolid(lx + ddx, lz + ddz));
                    // Elevated, angled vista (noclip keeps the free camera aloft
                    // without gravity while distant chunks stream in). Sit well
                    // above the land column + look down steeply so far stages on
                    // tall/coastal terrain don't bury the camera inside a hill.
                    ctx.noclip          = true;
                    ctx.spawnedOnGround = true;       // don't snap back to ground
                    ctx.spawnX = lx; ctx.spawnZ = lz;
                    ctx.camera.position = glm::vec3((float)lx + 0.5f,
                                                    (float)(gy + 32), (float)lz + 0.5f);
                    ctx.camera.velocity = glm::vec3(0.0f);
                    ctx.camera.yaw   = 35.0f + (float)tourStage * 57.0f;
                    ctx.camera.pitch = -32.0f;
                    ctx.camera.updateVectors();
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
