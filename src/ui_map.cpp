// The full-screen world-map overlay (M key) — split out of ui.cpp. Exposed via
// ui_internal.h so renderPlayUI (ui_play.cpp) can open it. See ui_internal.h.
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
// World map
// ---------------------------------------------------------------------------

void renderMapUI(AppContext& ctx) {
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
    ImGui::SetNextWindowPos(vpCentered(WIN, WIN + 44.0f), ImGuiCond_Always);
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

