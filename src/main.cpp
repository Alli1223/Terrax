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
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <iostream>
#include <string>
#include <algorithm>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--server") { runDedicatedServer(); return 0; }

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

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        ctx.deltaTime = std::min(currentFrame - ctx.lastFrame, 0.05f);
        ctx.lastFrame = currentFrame;

        glfwPollEvents();

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

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    disconnectFromGame(ctx);
    glfwTerminate();
    return 0;
}
