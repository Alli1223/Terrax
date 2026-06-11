// Terrax Launcher — a small standalone GLFW + ImGui app that checks the GitHub
// releases page, installs/updates the game, and launches it. Built as its own
// MSBuild project (TerraxLauncher.vcxproj) so it does not collide with the game's
// own main(). Windows-only: it uses WinHTTP for the GitHub API and DWM for the
// frosted "liquid glass" window chrome.
//
// Phase 1: project scaffolding + the borderless acrylic window and the first cut
// of the liquid-glass theme / cover art. The GitHub update logic and the
// download/install/launch flow arrive in later phases; the AppState fields below
// are already shaped for them so the UI code does not have to change.

#define GLFW_INCLUDE_NONE
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <GLFW/glfw3.h>
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>
#include <GL/gl.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "launcher_update.h"

#include <string>
#include <cmath>
#include <cfloat>
#include <algorithm>

// ---------------------------------------------------------------------------
// DWM constants. Define them ourselves so the build does not depend on a recent
// Windows SDK dwmapi.h that already carries the Windows 11 backdrop enums.
// ---------------------------------------------------------------------------
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#define TX_DWMWCP_ROUND 2                // round the window corners
#define TX_DWMSBT_TRANSIENTWINDOW 3      // acrylic blur-behind backdrop

// The launcher's Status enum and the snapshot of shared state live in
// launcher_update.h; the Updater worker thread owns the live values and the UI
// reads an immutable Updater::Snapshot once per frame.

// Fonts (loaded in main()).
static ImFont* g_fontBody = nullptr;     // ~18px Segoe UI
static ImFont* g_fontSmall = nullptr;    // ~14px Segoe UI Semibold
static ImFont* g_fontButton = nullptr;   // ~20px Segoe UI Semibold
static ImFont* g_fontTitle = nullptr;    // ~64px Segoe UI Bold (wordmark)
static ImFont* g_fontTag = nullptr;      // ~17px Segoe UI (subtitle)

// Accent colour for the glass rim / primary button — a cool cyan with a violet
// lean, the classic "liquid glass" tint.
static const ImU32 kAccent     = IM_COL32(108, 196, 255, 255);
static const ImU32 kAccentDeep = IM_COL32(76,  140, 240, 255);

// ---------------------------------------------------------------------------
// Window chrome helpers
// ---------------------------------------------------------------------------
static void applyGlassChrome(GLFWwindow* window) {
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = TX_DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    // Acrylic blur-behind: where the GL framebuffer is transparent, the desktop
    // shows through frosted. On Windows 10 (no backdrop support) this call is a
    // harmless no-op and the window simply renders its own dark glass instead.
    int backdrop = TX_DWMSBT_TRANSIENTWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}

// Borderless windows have no OS title bar, so we move the window ourselves while
// the user drags the custom title strip. Tracking the cursor each frame (rather
// than the Win32 WM_NCLBUTTONDOWN modal move loop) keeps the render loop — and
// the animated cover art — alive during the drag.
static void handleWindowDrag(GLFWwindow* window, bool titleHovered) {
    static bool dragging = false;
    static POINT grab = {0, 0};
    if (titleHovered && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        POINT p; GetCursorPos(&p);
        int wx = 0, wy = 0; glfwGetWindowPos(window, &wx, &wy);
        grab.x = p.x - wx; grab.y = p.y - wy;
        dragging = true;
    }
    if (dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            POINT p; GetCursorPos(&p);
            glfwSetWindowPos(window, p.x - grab.x, p.y - grab.y);
        } else {
            dragging = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Liquid-glass drawing helpers
// ---------------------------------------------------------------------------

// A frosted glass panel: translucent dark fill, a brighter top "sheen" (top
// corners rounded, bottom edge straight) to fake a top-lit gradient, a 1px rim,
// and a bright hairline along the very top edge.
static void glassPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding,
                       float fillAlpha = 0.42f, ImU32 rim = IM_COL32(255, 255, 255, 38)) {
    dl->AddRectFilled(a, b, IM_COL32(14, 18, 30, (int)(fillAlpha * 255)), rounding);
    float midY = a.y + (b.y - a.y) * 0.5f;
    dl->AddRectFilled(a, ImVec2(b.x, midY), IM_COL32(255, 255, 255, 16), rounding,
                      ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, rim, rounding, ImDrawFlags_RoundCornersAll, 1.2f);
    dl->AddLine(ImVec2(a.x + rounding, a.y + 1.0f), ImVec2(b.x - rounding, a.y + 1.0f),
                IM_COL32(255, 255, 255, 60), 1.0f);
}

// Text drawn with manual letter spacing — for the wordmark. CalcTextSizeA(size,
// ...) measures each glyph's advance at the explicit size (ImGui 1.92's dynamic
// fonts have no fixed FontSize member to scale from).
static float spacedTextWidth(ImFont* font, float size, const char* text, float spacing) {
    float w = 0.0f;
    for (const char* c = text; *c; ++c) {
        char buf[2] = {*c, 0};
        w += font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf).x + spacing;
    }
    return w - spacing;
}
static void drawSpacedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                           ImU32 col, const char* text, float spacing) {
    float x = pos.x;
    for (const char* c = text; *c; ++c) {
        char buf[2] = {*c, 0};
        dl->AddText(font, size, ImVec2(x, pos.y), col, buf);
        x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf).x + spacing;
    }
}

// Custom glass button. Returns true on click. Disabled buttons render dimmed and
// do not respond. Uses an InvisibleButton for hit-testing + manual drawing so it
// matches the rest of the glass theme.
static bool glassButton(const char* id, ImVec2 pos, ImVec2 size, const char* label,
                        bool primary, bool enabled, float t = 0.0f) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size, enabled ? 0 : ImGuiButtonFlags_None);
    bool hovered = enabled && ImGui::IsItemHovered();
    bool active  = enabled && ImGui::IsItemActive();
    bool clicked = enabled && ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && hovered;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = pos, b = ImVec2(pos.x + size.x, pos.y + size.y);
    float rounding = size.y * 0.5f;

    // Inviting breathing glow on an idle primary button.
    if (primary && enabled && !active) {
        float pulse = 0.5f + 0.5f * std::sin(t * 2.3f);
        int ga = (int)(24 + 40 * pulse);
        dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3),
                    IM_COL32(108, 196, 255, ga), rounding + 3.0f, ImDrawFlags_RoundCornersAll, 3.0f);
    }

    // Rounded two-layer fill (dark base + lighter top sheen) gives a pill with a
    // vertical gradient and no square corners poking out under the rim.
    ImU32 baseCol, sheenCol, rim;
    if (primary) {
        int boost = active ? -24 : (hovered ? 26 : 0);
        sheenCol = IM_COL32(120 + boost, 204, 255, enabled ? 255 : 90);
        baseCol  = IM_COL32(64 + boost, 128, 236, enabled ? 255 : 90);
        rim      = IM_COL32(224, 244, 255, enabled ? 190 : 60);
    } else {
        int al = active ? 120 : (hovered ? 90 : 58);
        sheenCol = IM_COL32(255, 255, 255, enabled ? al : 24);
        baseCol  = IM_COL32(120, 140, 175, enabled ? al / 2 + 18 : 14);
        rim      = IM_COL32(255, 255, 255, enabled ? 96 : 30);
    }
    float midY = a.y + size.y * 0.52f;
    dl->AddRectFilled(a, b, baseCol, rounding);
    dl->AddRectFilled(a, ImVec2(b.x, midY), sheenCol, rounding, ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, rim, rounding, ImDrawFlags_RoundCornersAll, 1.4f);
    if (hovered) {
        dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1),
                    IM_COL32(150, 210, 255, 90), rounding + 1.0f, ImDrawFlags_RoundCornersAll, 2.0f);
    }

    ImGui::PushFont(g_fontButton);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImU32 txtCol = primary ? IM_COL32(8, 16, 30, enabled ? 255 : 110)
                           : IM_COL32(235, 244, 255, enabled ? 255 : 110);
    dl->AddText(ImVec2(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f), txtCol, label);
    ImGui::PopFont();
    return clicked;
}

// A small round icon button (minimize / close) for the borderless title bar.
static bool iconButton(const char* id, ImVec2 center, float radius, char glyph, bool danger) {
    ImVec2 size(radius * 2, radius * 2);
    ImVec2 pos(center.x - radius, center.y - radius);
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemDeactivated() && hovered;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) {
        ImU32 bg = danger ? IM_COL32(232, 72, 86, 220) : IM_COL32(255, 255, 255, 46);
        dl->AddCircleFilled(center, radius, bg, 24);
    }
    ImU32 ink = (hovered && danger) ? IM_COL32(255, 255, 255, 255) : IM_COL32(225, 234, 246, 220);
    float r = radius * 0.42f;
    if (glyph == 'x') {
        dl->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), ink, 1.6f);
        dl->AddLine(ImVec2(center.x - r, center.y + r), ImVec2(center.x + r, center.y - r), ink, 1.6f);
    } else { // minimize
        dl->AddLine(ImVec2(center.x - r, center.y + r * 0.4f), ImVec2(center.x + r, center.y + r * 0.4f), ink, 1.6f);
    }
    return clicked;
}

// A small isometric voxel cube (top + two side faces) — the launcher's motif.
static void drawVoxelCube(ImDrawList* dl, float cx, float cy, float e, ImU32 base, float alpha) {
    int A = (int)(255 * (alpha < 0 ? 0 : (alpha > 1 ? 1 : alpha)));
    auto shade = [&](float f) -> ImU32 {
        int r = (int)(((base >> IM_COL32_R_SHIFT) & 0xFF) * f);
        int g = (int)(((base >> IM_COL32_G_SHIFT) & 0xFF) * f);
        int bl = (int)(((base >> IM_COL32_B_SHIFT) & 0xFF) * f);
        return IM_COL32(r > 255 ? 255 : r, g > 255 ? 255 : g, bl > 255 ? 255 : bl, A);
    };
    ImVec2 T(cx, cy - e), R(cx + e, cy - e * 0.5f), C(cx, cy), L(cx - e, cy - e * 0.5f);
    ImVec2 BL(cx - e, cy + e * 0.5f), BC(cx, cy + e), BR(cx + e, cy + e * 0.5f);
    dl->AddQuadFilled(T, R, C, L, shade(1.15f));    // top   (brightest)
    dl->AddQuadFilled(L, C, BC, BL, shade(0.72f));  // left
    dl->AddQuadFilled(C, R, BR, BC, shade(0.48f));  // right (darkest)
}

// A small rotating arc shown while the launcher is busy.
static void drawSpinner(ImDrawList* dl, ImVec2 center, float radius, float t, ImU32 col) {
    const int seg = 28;
    float start = t * 4.0f, sweep = 4.2f;
    dl->PathClear();
    for (int i = 0; i <= seg; ++i) {
        float ang = start + sweep * (float)i / seg;
        dl->PathLineTo(ImVec2(center.x + std::cos(ang) * radius, center.y + std::sin(ang) * radius));
    }
    dl->PathStroke(col, 0, 2.4f);
}

// ---------------------------------------------------------------------------
// Cover art — an animated procedural voxel-landscape hero: gradient day/night
// sky, twinkling stars, a breathing orb, drifting voxel cubes, parallax mountain
// silhouettes, and the glowing TERRAX wordmark. `t` is seconds since launch.
// ---------------------------------------------------------------------------
static void drawCoverArt(ImDrawList* dl, ImVec2 origin, ImVec2 size, float t) {
    ImVec2 a = origin;
    ImVec2 b = ImVec2(origin.x + size.x, origin.y + size.y);
    float heroH = size.y * 0.66f;
    float horizon = a.y + heroH;

    // Sky: deep indigo at the top fading to a warm horizon glow.
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, horizon),
                                IM_COL32(20, 22, 44, 255), IM_COL32(26, 24, 52, 255),
                                IM_COL32(58, 46, 92, 255), IM_COL32(46, 40, 80, 255));
    dl->AddRectFilledMultiColor(ImVec2(a.x, horizon - heroH * 0.32f), ImVec2(b.x, horizon),
                                IM_COL32(120, 70, 90, 0), IM_COL32(120, 70, 90, 0),
                                IM_COL32(232, 132, 78, 120), IM_COL32(232, 132, 78, 120));

    // Twinkling stars across the upper sky (deterministic positions, animated alpha).
    for (int i = 0; i < 56; ++i) {
        float s = (float)i;
        float x = a.x + size.x * std::fmod(s * 0.61803f + 0.13f, 1.0f);
        float y = a.y + heroH * 0.66f * std::fmod(s * 0.30317f + 0.07f, 1.0f);
        float tw = 0.5f + 0.5f * std::sin(t * (1.4f + std::fmod(s, 3.0f)) + s);
        int al = (int)(36 + 96 * tw);
        float rad = 0.7f + std::fmod(s, 2.0f) * 0.5f;
        dl->AddCircleFilled(ImVec2(x, y), rad, IM_COL32(220, 228, 255, al), 6);
    }

    // Glowing orb (sun/moon) with a slow breathing halo.
    ImVec2 orb(a.x + size.x * 0.74f, a.y + heroH * 0.40f);
    float pulse = 0.5f + 0.5f * std::sin(t * 1.1f);
    for (int i = 7; i >= 1; --i) {
        float rr = 26.0f + i * (10.0f + pulse * 3.0f);
        int al = (int)((14.0f + pulse * 8.0f) * (1.0f - i / 8.0f)) + 5;
        dl->AddCircleFilled(orb, rr, IM_COL32(255, 196, 140, al), 48);
    }
    dl->AddCircleFilled(orb, 26.0f, IM_COL32(255, 228, 190, 240), 48);

    // Drifting voxel cubes rising from the ridgeline into the sky.
    for (int i = 0; i < 12; ++i) {
        float s = (float)i;
        float span = heroH * 0.95f;
        float speed = 9.0f + std::fmod(s * 13.0f, 15.0f);
        float ph = std::fmod(s * 97.0f, span);
        float rise = std::fmod(t * speed + ph, span);
        float life = rise / span;                          // 0 at horizon -> 1 at top
        float x = a.x + size.x * std::fmod(s * 0.61803f + 0.31f, 1.0f) + std::sin(t * 0.5f + s) * 14.0f;
        float y = horizon - rise;
        float e = 6.0f + std::fmod(s * 7.0f, 7.0f);
        float alpha = (life < 0.12f ? life / 0.12f : (1.0f - life)) * 0.55f;
        ImU32 pal[4] = { IM_COL32(96, 200, 255, 255), IM_COL32(120, 150, 255, 255),
                         IM_COL32(170, 130, 255, 255), IM_COL32(255, 170, 120, 255) };
        drawVoxelCube(dl, x, y, e, pal[i % 4], alpha);
    }

    // Layered mountain silhouettes (back = lighter/higher haze, front = darker),
    // each drifting at its own slow rate for a parallax feel.
    struct Layer { float base, amp, freq, phase, speed; ImU32 col; };
    Layer layers[3] = {
        { horizon - heroH * 0.05f, heroH * 0.26f, 0.010f, 0.6f, 0.05f, IM_COL32(44, 44, 82, 255) },
        { horizon + heroH * 0.04f, heroH * 0.32f, 0.014f, 2.1f, 0.09f, IM_COL32(30, 32, 60, 255) },
        { horizon + heroH * 0.14f, heroH * 0.40f, 0.020f, 4.7f, 0.15f, IM_COL32(18, 20, 40, 255) },
    };
    // Disable anti-aliased fill while tiling the ridge quads: AA on the shared
    // vertical edges leaves faint seams between adjacent columns.
    ImDrawListFlags savedFill = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (const Layer& L : layers) {
        const int N = 72;
        float prevX = a.x, prevY = 0.0f;
        for (int i = 0; i <= N; ++i) {
            float x = a.x + size.x * (float)i / N;
            float u = x * L.freq + t * L.speed;
            float h = std::sin(u + L.phase) * 0.6f + std::sin(u * 2.3f + L.phase * 1.7f) * 0.3f
                    + std::sin(u * 0.5f) * 0.1f;
            float y = L.base - h * L.amp;
            if (i > 0) {
                // Fill the column between this ridge sample and the previous one
                // down to the bottom. Per-segment quads handle the concave ridge
                // correctly (a single convex poly fill would not).
                dl->AddQuadFilled(ImVec2(prevX, prevY), ImVec2(x, y),
                                  ImVec2(x, b.y), ImVec2(prevX, b.y), L.col);
            }
            prevX = x; prevY = y;
        }
    }
    dl->Flags = savedFill;

    // Bottom scrim so the glass control bar reads clearly over the art.
    dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - size.y * 0.34f), b,
                                IM_COL32(10, 11, 20, 0), IM_COL32(10, 11, 20, 0),
                                IM_COL32(8, 9, 16, 235), IM_COL32(8, 9, 16, 235));

    // Wordmark "TERRAX" with a soft glow, plus a subtitle.
    const char* title = "TERRAX";
    float titleSize = 64.0f, spacing = 8.0f;
    float tw = spacedTextWidth(g_fontTitle, titleSize, title, spacing);
    ImVec2 tpos(a.x + 44.0f, a.y + heroH * 0.34f);
    float gp = 0.5f + 0.5f * std::sin(t * 1.4f);   // breathing glow
    for (int i = 5; i >= 1; --i) {
        float off = (float)i;
        int al = (int)((8 + (6 - i) * 5) * (0.7f + 0.5f * gp));
        drawSpacedText(dl, g_fontTitle, titleSize, ImVec2(tpos.x, tpos.y + off),
                       IM_COL32(110, 190, 255, al), title, spacing);
    }
    drawSpacedText(dl, g_fontTitle, titleSize, tpos, IM_COL32(244, 248, 255, 255), title, spacing);
    dl->AddText(g_fontTag, 17.0f, ImVec2(tpos.x + 3.0f, tpos.y + titleSize + 6.0f),
                IM_COL32(176, 196, 224, 220), "VOXEL  SANDBOX  \xE2\x80\xA2  MULTIPLAYER");
    (void)tw;
}

// ---------------------------------------------------------------------------
// Main launcher window
// ---------------------------------------------------------------------------
static void renderLauncher(GLFWwindow* window, Updater& updater) {
    Updater::Snapshot app = updater.snapshot();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 origin = vp->Pos;
    ImVec2 size   = vp->Size;

    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##terrax_launcher", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float t = (float)glfwGetTime();
    drawCoverArt(dl, origin, size, t);

    // --- Title bar: drag strip + window buttons ---------------------------
    float titleH = 46.0f;
    ImVec2 titleMin = origin;
    ImVec2 titleMax = ImVec2(origin.x + size.x - 96.0f, origin.y + titleH);
    bool titleHovered = ImGui::IsMouseHoveringRect(titleMin, titleMax, false);
    handleWindowDrag(window, titleHovered);

    float by = origin.y + titleH * 0.5f;
    if (iconButton("##min", ImVec2(origin.x + size.x - 64.0f, by), 13.0f, '-', false))
        glfwIconifyWindow(window);
    if (iconButton("##close", ImVec2(origin.x + size.x - 30.0f, by), 13.0f, 'x', true))
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    // --- Bottom control bar (glass) ---------------------------------------
    float margin = 22.0f;
    float barH = 118.0f;
    ImVec2 barA(origin.x + margin, origin.y + size.y - barH - margin);
    ImVec2 barB(origin.x + size.x - margin, origin.y + size.y - margin);
    // Soft drop shadow under the control bar for depth.
    for (int i = 5; i >= 1; --i)
        dl->AddRectFilled(ImVec2(barA.x - i, barA.y - i + 4), ImVec2(barB.x + i, barB.y + i + 4),
                          IM_COL32(0, 0, 0, 12), 18.0f + i);
    glassPanel(dl, barA, barB, 18.0f);

    // Version / status text on the left.
    float pad = 22.0f;
    ImGui::PushFont(g_fontSmall);
    std::string verLine;
    if (app.installedVersion.empty()) verLine = "Not installed";
    else verLine = "Installed " + app.installedVersion;
    if (!app.latestVersion.empty()) verLine += "      Latest " + app.latestVersion;
    dl->AddText(ImVec2(barA.x + pad, barA.y + 20.0f), IM_COL32(150, 168, 196, 255), verLine.c_str());
    ImGui::PopFont();

    bool busy = (app.status == Status::Checking || app.status == Status::Downloading ||
                 app.status == Status::Installing);
    float statusX = barA.x + pad + (busy ? 24.0f : 0.0f);
    if (busy)
        drawSpinner(dl, ImVec2(barA.x + pad + 8.0f, barA.y + 52.0f), 7.0f, t, kAccent);
    ImGui::PushFont(g_fontBody);
    dl->AddText(ImVec2(statusX, barA.y + 44.0f), IM_COL32(228, 238, 250, 255), app.statusLine.c_str());
    ImGui::PopFont();

    // Progress bar (shown while downloading/installing).
    if (app.status == Status::Downloading || app.status == Status::Installing) {
        ImVec2 pa(barA.x + pad, barB.y - 26.0f);
        ImVec2 pb(barB.x - 200.0f, barB.y - 16.0f);
        dl->AddRectFilled(pa, pb, IM_COL32(255, 255, 255, 28), 5.0f);
        float w = (pb.x - pa.x) * (app.progress < 0 ? 0 : (app.progress > 1 ? 1 : app.progress));
        dl->AddRectFilledMultiColor(pa, ImVec2(pa.x + w, pb.y), kAccent, kAccentDeep, kAccentDeep, kAccent);
        if (w > 4.0f)   // bright leading edge
            dl->AddRectFilled(ImVec2(pa.x + w - 4, pa.y), ImVec2(pa.x + w, pb.y), IM_COL32(235, 245, 255, 150), 2.0f);
        dl->AddRect(pa, pb, IM_COL32(255, 255, 255, 40), 5.0f);
    }

    // Primary action button on the right.
    const char* btnLabel = "PLAY";
    bool btnEnabled = true;
    switch (app.status) {
        case Status::NotInstalled:    btnLabel = "INSTALL"; break;
        case Status::UpdateAvailable: btnLabel = "UPDATE";  break;
        case Status::UpToDate:        btnLabel = "PLAY";    break;
        case Status::LauncherUpdate:  btnLabel = "UPDATE LAUNCHER"; break;
        case Status::Checking:        btnLabel = "CHECKING\xE2\x80\xA6"; btnEnabled = false; break;
        case Status::Downloading:     btnLabel = "DOWNLOADING\xE2\x80\xA6"; btnEnabled = false; break;
        case Status::Installing:      btnLabel = "INSTALLING\xE2\x80\xA6";  btnEnabled = false; break;
        case Status::Error:           btnLabel = "RETRY"; break;
    }
    // Size the button to fit the label (some, like "UPDATE LAUNCHER", are wide).
    ImGui::PushFont(g_fontButton);
    float btnTextW = ImGui::CalcTextSize(btnLabel).x;
    ImGui::PopFont();
    ImVec2 btnSize(std::max(180.0f, btnTextW + 52.0f), 52.0f);
    ImVec2 btnPos(barB.x - btnSize.x - 22.0f, barA.y + (barH - btnSize.y) * 0.5f);
    bool primary = (app.status != Status::UpToDate);
    if (glassButton("##primary", btnPos, btnSize, btnLabel, primary, btnEnabled, t)) {
        switch (app.status) {
            case Status::NotInstalled:
            case Status::UpdateAvailable: updater.startInstall(); break;
            case Status::LauncherUpdate:  updater.startLauncherUpdate(); break;
            case Status::UpToDate:
                if (updater.launchGame())   // close the launcher once the game starts
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                break;
            case Status::Error:           updater.startCheck();   break;
            default: break;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

// Load Segoe UI from the Windows font folder at the sizes we need; fall back to
// ImGui's built-in font if a face is missing.
// Basic Latin + the bullet (U+2022) and ellipsis (U+2026) we use in labels.
static const ImWchar kGlyphRanges[] = { 0x0020, 0x00FF, 0x2022, 0x2022, 0x2026, 0x2026, 0 };
static ImFont* loadFace(ImGuiIO& io, const char* file, float size) {
    char path[MAX_PATH];
    UINT n = GetWindowsDirectoryA(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 64) return io.Fonts->AddFontDefault();
    std::string full = std::string(path) + "\\Fonts\\" + file;
    ImFont* f = io.Fonts->AddFontFromFileTTF(full.c_str(), size, nullptr, kGlyphRanges);
    return f ? f : io.Fonts->AddFontDefault();
}

int main(int argc, char** argv) {
    // Silent install / update with no window — useful for first-run bootstrap or
    // automation. Honors the same TERRAX_REPO_* env overrides as the UI.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--install") {
            std::string tag, err;
            bool ok = runInstall(tag, err);
            launcherLog(ok ? ("silent install ok: " + tag) : ("silent install failed: " + err));
            return ok ? 0 : 1;
        }
    }

    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const int W = 960, H = 600;
    GLFWwindow* window = glfwCreateWindow(W, H, "Terrax Launcher", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Centre on the primary monitor's work area.
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
        int mx, my, mw, mh; glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
        glfwSetWindowPos(window, mx + (mw - W) / 2, my + (mh - H) / 2);
    }
    applyGlassChrome(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // no imgui.ini for the launcher
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    g_fontBody   = loadFace(io, "segoeui.ttf", 18.0f);
    g_fontSmall  = loadFace(io, "seguisb.ttf", 14.0f);
    g_fontButton = loadFace(io, "seguisb.ttf", 20.0f);
    g_fontTitle  = loadFace(io, "segoeuib.ttf", 64.0f);
    g_fontTag    = loadFace(io, "segoeui.ttf", 17.0f);
    io.FontDefault = g_fontBody;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    Updater updater;
    updater.startCheck();   // query GitHub immediately on launch

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderLauncher(window, updater);

        // If a launcher self-update finished downloading, run it (the installer's
        // manifest triggers the UAC elevation) and close the launcher so it can be
        // replaced in place.
        std::wstring setupToRun;
        if (updater.takeLauncherSetupToRun(setupToRun)) {
            ShellExecuteW(nullptr, L"open", setupToRun.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        ImGui::Render();
        int fbW, fbH; glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);   // transparent: let the acrylic show
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
