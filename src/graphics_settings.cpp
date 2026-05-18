#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "graphics_settings.h"
#include "game_settings.h"
#include "game_types.h"
#include "app_context.h"
#include "renderer.h"
#include <algorithm>

struct ResolutionPreset {
    int w, h;
    const char* label;
};

static const ResolutionPreset kResolutions[] = {
    { 1280, 720,  "1280 x 720"  },
    { 1600, 900,  "1600 x 900"  },
    { 1920, 1080, "1920 x 1080" },
    { 2560, 1440, "2560 x 1440" },
};
static constexpr int kResolutionCount = sizeof(kResolutions) / sizeof(kResolutions[0]);

static int resolutionPresetIndex(const GameSettings& s) {
    for (int i = 0; i < kResolutionCount; i++) {
        if (kResolutions[i].w == s.windowWidth && kResolutions[i].h == s.windowHeight)
            return i;
    }
    return 0;
}

static void clampWindowSize(GameSettings& s) {
    s.windowWidth  = std::clamp(s.windowWidth, 640, 7680);
    s.windowHeight = std::clamp(s.windowHeight, 480, 4320);
    s.fov = std::clamp(s.fov, 50.0f, 110.0f);
}

GLFWwindow* createGameWindow(GameSettings& settings) {
    clampWindowSize(settings);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, settings.msaa ? 4 : 0);

    GLFWwindow* window = glfwCreateWindow(settings.windowWidth, settings.windowHeight,
                                            "Terrax", nullptr, nullptr);
    if (!window) return nullptr;

    glfwSetWindowPos(window, 100, 100);
    if (settings.fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor && mode)
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(settings.vsync ? 1 : 0);
    return window;
}

void applyGraphicsSettings(GLFWwindow* window, AppContext& ctx) {
    if (!window) return;

    clampWindowSize(ctx.settings);
    ctx.camera.fov = ctx.settings.fov;

    glfwSwapInterval(ctx.settings.vsync ? 1 : 0);

    if (ctx.settings.msaa)
        glEnable(GL_MULTISAMPLE);
    else
        glDisable(GL_MULTISAMPLE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;

    if (ctx.settings.fullscreen && monitor && mode) {
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(window, nullptr, 100, 100,
                           ctx.settings.windowWidth, ctx.settings.windowHeight, 0);
    }

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW > 0 && fbH > 0)
        glViewport(0, 0, fbW, fbH);
}

int graphicsSettingsResolutionCount() { return kResolutionCount; }

const char* graphicsSettingsResolutionLabel(int index) {
    if (index < 0 || index >= kResolutionCount) return "";
    return kResolutions[index].label;
}

void graphicsSettingsApplyResolutionPreset(GameSettings& settings, int index) {
    if (index < 0 || index >= kResolutionCount) return;
    settings.windowWidth  = kResolutions[index].w;
    settings.windowHeight = kResolutions[index].h;
}

int graphicsSettingsResolutionIndex(const GameSettings& settings) {
    return resolutionPresetIndex(settings);
}
