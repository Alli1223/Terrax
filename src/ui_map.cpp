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
#include "dungeon.h"
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

// Marker colour for each settlement type (shared by the map and its legend).
static ImU32 townTypeColor(TownType t) {
    switch (t) {
        case TownType::Coastal:  return IM_COL32( 90, 170, 230, 235);
        case TownType::Mountain: return IM_COL32(205, 205, 210, 235);
        default:                 return IM_COL32(120, 200, 110, 235);
    }
}

// Draw a settlement marker whose SHAPE encodes the town type (square =
// Grassland, upward triangle = Mountain, diamond = Coastal) and whose radius
// `r` encodes its importance (Village small, Town large). Shared by the live
// markers and the legend swatches so the two always agree.
static void drawTownMarker(ImDrawList* dl, ImVec2 p, TownType type, float r, ImU32 col) {
    const ImU32 edge = IM_COL32(0, 0, 0, 200);
    switch (type) {
        case TownType::Mountain: {                 // a peak
            ImVec2 a{ p.x,     p.y - r          };
            ImVec2 b{ p.x - r, p.y + r * 0.85f  };
            ImVec2 c{ p.x + r, p.y + r * 0.85f  };
            dl->AddTriangleFilled(a, b, c, col);
            dl->AddTriangle(a, b, c, edge, 1.5f);
            break;
        }
        case TownType::Coastal: {                  // a harbour buoy
            ImVec2 a{ p.x,     p.y - r };
            ImVec2 b{ p.x + r, p.y     };
            ImVec2 c{ p.x,     p.y + r };
            ImVec2 d{ p.x - r, p.y     };
            dl->AddQuadFilled(a, b, c, d, col);
            dl->AddQuad(a, b, c, d, edge, 1.5f);
            break;
        }
        default: {                                 // Grassland: a square
            dl->AddRectFilled({ p.x - r, p.y - r }, { p.x + r, p.y + r }, col, 1.0f);
            dl->AddRect({ p.x - r, p.y - r }, { p.x + r, p.y + r }, edge, 1.0f, 0, 1.5f);
            break;
        }
    }
}

// A dungeon marker — a crimson X on a dark disc, distinct from town markers.
static void drawDungeonMarker(ImDrawList* dl, ImVec2 p, float r) {
    dl->AddCircleFilled(p, r + 1.0f, IM_COL32(20, 8, 8, 220), 12);
    const ImU32 crim = IM_COL32(200, 45, 45, 245);
    float a = r * 0.7f;
    dl->AddLine({ p.x - a, p.y - a }, { p.x + a, p.y + a }, crim, 2.2f);
    dl->AddLine({ p.x - a, p.y + a }, { p.x + a, p.y - a }, crim, 2.2f);
    dl->AddCircle(p, r + 1.0f, IM_COL32(0, 0, 0, 200), 12, 1.2f);
}

// A castle (overground dungeon) marker — a steel-blue battlemented keep, so it
// reads differently from the crimson underground-dungeon X.
static void drawCastleMarker(ImDrawList* dl, ImVec2 p, float r) {
    dl->AddCircleFilled(p, r + 1.5f, IM_COL32(10, 14, 22, 220), 12);
    const ImU32 steel = IM_COL32(120, 165, 215, 250);
    const ImU32 edge  = IM_COL32(18, 28, 44, 230);
    float w = r * 0.9f;
    ImVec2 a = { p.x - w, p.y - r * 0.15f };   // keep body
    ImVec2 b = { p.x + w, p.y + r * 0.95f };
    dl->AddRectFilled(a, b, steel);
    dl->AddRect(a, b, edge, 0.0f, 0, 1.0f);
    float mw = (b.x - a.x) / 5.0f;             // three merlons (battlements) on top
    for (int i = 0; i < 3; i++) {
        float mx = a.x + (2 * i) * mw;
        dl->AddRectFilled({ mx, a.y - mw }, { mx + mw, a.y }, steel);
    }
}

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
        // The uploaded texture now covers the area the build was launched for.
        ctx.mapTexCX = ctx.mapPendingCX;
        ctx.mapTexCZ = ctx.mapPendingCZ;
        ctx.mapTexR  = ctx.mapPendingR;
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
        ctx.mapPendingCX = bcx; ctx.mapPendingCZ = bcz; ctx.mapPendingR = wr;
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

    // Live view centre (player + pan) and the shared world->screen transform.
    // The map texture, roads, towns and players ALL use this, so they pan and
    // zoom together — even while a freshly panned texture is still building (the
    // stale texture just slides to its true world position via worldToMap).
    float texCX = ctx.mapBuiltCX + ctx.mapPanX;
    float texCZ = ctx.mapBuiltCZ + ctx.mapPanZ;
    auto worldToMap = [&](float wx, float wz) -> ImVec2 {
        float dx = (wx - texCX) / worldRadius * h;
        float dz = (wz - texCZ) / worldRadius * h;
        return { mc.x + dx * cr - dz * sr,
                 mc.y + dx * sr + dz * cr };
    };

    // Background circle
    dl->AddCircleFilled(mc, h + 6.0f, IM_COL32(15, 10, 5, 230), 64);

    // Map image — drawn at the texture's OWN built world-area through the live
    // transform, so it tracks the overlays exactly while panning/zooming.
    if (ctx.mapTex) {
        float tr = ctx.mapTexR;
        ImVec2 q00 = worldToMap(ctx.mapTexCX - tr, ctx.mapTexCZ - tr);
        ImVec2 q10 = worldToMap(ctx.mapTexCX + tr, ctx.mapTexCZ - tr);
        ImVec2 q11 = worldToMap(ctx.mapTexCX + tr, ctx.mapTexCZ + tr);
        ImVec2 q01 = worldToMap(ctx.mapTexCX - tr, ctx.mapTexCZ + tr);
        dl->PushClipRect(ImVec2(mc.x - h - 2, mc.y - h - 2),
                         ImVec2(mc.x + h + 2, mc.y + h + 2), true);
        dl->AddImageQuad((ImTextureID)(intptr_t)ctx.mapTex,
                         q00, q10, q11, q01,
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

    // ── Roads: highways between towns (+ town paths when zoomed in) ──────────
    {
        const TownPlan& plan = getTownPlan();
        auto drawRoad = [&](const std::vector<glm::ivec2>& pts, ImU32 col, float thick) {
            for (size_t i = 1; i < pts.size(); i++) {
                ImVec2 a = worldToMap((float)pts[i - 1].x, (float)pts[i - 1].y);
                ImVec2 b = worldToMap((float)pts[i].x,     (float)pts[i].y);
                float da = (a.x - mc.x) * (a.x - mc.x) + (a.y - mc.y) * (a.y - mc.y);
                float db = (b.x - mc.x) * (b.x - mc.x) + (b.y - mc.y) * (b.y - mc.y);
                if (da > h * h && db > h * h) continue;   // segment wholly outside the disc
                dl->AddLine(a, b, col, thick);
            }
        };
        dl->PushClipRect(ImVec2(mc.x - h, mc.y - h), ImVec2(mc.x + h, mc.y + h), true);
        for (const TownRoad& road : plan.highways)
            drawRoad(road.pts, IM_COL32(170, 135, 75, 205), 2.5f);   // tan highways
        if (worldRadius < 500.0f)                                    // town paths only up close
            for (const auto& tp : plan.towns) {
                const Town& t = *tp;
                for (const TownRoad& p : t.paths)
                    drawRoad(p.pts, IM_COL32(150, 140, 120, 170), 1.5f);
            }
        dl->PopClipRect();
    }

    // ── Towns: settlement markers + names ────────────────────────────────────
    {
        const TownPlan& plan = getTownPlan();
        bool showNames = worldRadius < 1100.0f;   // hide labels when far zoomed out
        for (const auto& tp : plan.towns) {
            const Town& t = *tp;
            ImVec2 sp = worldToMap((float)t.center.x, (float)t.center.y);
            float  d2 = (sp.x - mc.x) * (sp.x - mc.x) + (sp.y - mc.y) * (sp.y - mc.y);
            if (d2 >= h * h) continue;

            float r = (t.size == TownSize::Town) ? 6.5f : 4.0f;
            drawTownMarker(dl, sp, t.type, r, townTypeColor(t.type));

            if (showNames && !t.name.empty()) {
                ImVec2 ts = ImGui::CalcTextSize(t.name.c_str());
                ImVec2 tp = { sp.x - ts.x * 0.5f, sp.y + 6.0f };
                dl->AddText({ tp.x + 1, tp.y + 1 }, IM_COL32(0, 0, 0, 210), t.name.c_str());
                dl->AddText(tp, IM_COL32(245, 235, 200, 245), t.name.c_str());
            }
        }
    }

    // ── Dungeons: crimson markers + names ────────────────────────────────────
    {
        const DungeonPlan& dp = getDungeonPlan();
        bool showNames = worldRadius < 700.0f;   // names only when fairly zoomed in
        for (const auto& dptr : dp.dungeons) {
            const Dungeon& dg = *dptr;
            ImVec2 sp = worldToMap((float)dg.entrance.x, (float)dg.entrance.z);
            float  d2 = (sp.x - mc.x) * (sp.x - mc.x) + (sp.y - mc.y) * (sp.y - mc.y);
            if (d2 >= h * h) continue;
            if (dg.overground) drawCastleMarker(dl, sp, 5.5f);
            else               drawDungeonMarker(dl, sp, 5.0f);
            if (showNames && !dg.name.empty()) {
                ImU32 nameCol = dg.overground ? IM_COL32(185, 210, 245, 245)
                                              : IM_COL32(240, 175, 175, 245);
                ImVec2 ts = ImGui::CalcTextSize(dg.name.c_str());
                ImVec2 tp = { sp.x - ts.x * 0.5f, sp.y + 6.0f };
                dl->AddText({ tp.x + 1, tp.y + 1 }, IM_COL32(0, 0, 0, 210), dg.name.c_str());
                dl->AddText(tp, nameCol, dg.name.c_str());
            }
        }
    }

    // ── Legend (top-left corner; shapes match the markers above) ─────────────
    {
        float lx = canvasTL.x + 6.0f, ly = canvasTL.y + 6.0f;
        const float lineH = 18.0f;
        dl->AddRectFilled({ lx - 4, ly - 4 }, { lx + 124, ly + lineH * 7 + 4 },
                          IM_COL32(15, 10, 5, 180), 4.0f);
        dl->AddRect({ lx - 4, ly - 4 }, { lx + 124, ly + lineH * 7 + 4 },
                    IM_COL32(180, 140, 60, 160), 4.0f, 0, 1.0f);
        const TownType types[3]   = { TownType::Grassland, TownType::Mountain, TownType::Coastal };
        const char*    labels[3]  = { "Grassland", "Mountain", "Coastal" };
        for (int i = 0; i < 3; i++) {
            float cy = ly + lineH * i + lineH * 0.5f;
            drawTownMarker(dl, { lx + 9, cy }, types[i], 5.0f, townTypeColor(types[i]));
            dl->AddText({ lx + 24, cy - 7 }, IM_COL32(235, 225, 200, 235), labels[i]);
        }
        float cy = ly + lineH * 3 + lineH * 0.5f;   // size key: small = Village, large = Town
        drawTownMarker(dl, { lx + 6,  cy }, TownType::Grassland, 3.0f, IM_COL32(190, 190, 190, 235));
        drawTownMarker(dl, { lx + 16, cy }, TownType::Grassland, 6.0f, IM_COL32(190, 190, 190, 235));
        dl->AddText({ lx + 28, cy - 7 }, IM_COL32(235, 225, 200, 235), "Village / Town");
        float ry = ly + lineH * 4 + lineH * 0.5f;   // road swatch
        dl->AddLine({ lx + 2, ry }, { lx + 20, ry }, IM_COL32(170, 135, 75, 235), 2.5f);
        dl->AddText({ lx + 28, ry - 7 }, IM_COL32(235, 225, 200, 235), "Road");
        float dyv = ly + lineH * 5 + lineH * 0.5f;   // dungeon swatch
        drawDungeonMarker(dl, { lx + 9, dyv }, 5.0f);
        dl->AddText({ lx + 24, dyv - 7 }, IM_COL32(235, 225, 200, 235), "Dungeon");
        float cyv = ly + lineH * 6 + lineH * 0.5f;   // castle swatch
        drawCastleMarker(dl, { lx + 9, cyv }, 5.0f);
        dl->AddText({ lx + 24, cyv - 7 }, IM_COL32(235, 225, 200, 235), "Castle");
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

