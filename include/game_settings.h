#pragma once

static constexpr int MIN_RENDER_DISTANCE     = 4;
static constexpr int MAX_RENDER_DISTANCE     = 64;
static constexpr int DEFAULT_RENDER_DISTANCE = 30;

struct GameSettings {
    int  renderDistance = DEFAULT_RENDER_DISTANCE;
    bool fullscreen     = false;
    bool vsync          = true;
    bool msaa           = true;
    int  windowWidth    = 1280;
    int  windowHeight   = 720;
    float fov           = 70.0f;
};

inline int clampRenderDistance(int value) {
    if (value < MIN_RENDER_DISTANCE) return MIN_RENDER_DISTANCE;
    if (value > MAX_RENDER_DISTANCE) return MAX_RENDER_DISTANCE;
    return value;
}
