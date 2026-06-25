// In-game play-state UI: HUD, crosshair, hotbar, chat, player list, nametags,
// health bars and the F3 debug overlay. Split out of ui.cpp. Shared viewport
// helpers live in ui_internal.h.
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

// Draws a small health bar at a world position (used over damaged NPCs).
static void drawHealthBar(const glm::vec3& worldPos, float frac,
                          const glm::mat4& view, const glm::mat4& proj,
                          int fbW, int fbH) {
    glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.01f) return;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) return;
    float sx = (ndc.x * 0.5f + 0.5f) * (float)fbW;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)fbH;
    frac = std::clamp(frac, 0.0f, 1.0f);
    const float W = 46.0f, H = 6.0f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(sx - W * 0.5f - 1, sy - 1),
                      ImVec2(sx + W * 0.5f + 1, sy + H + 1), IM_COL32(15, 10, 8, 200));
    dl->AddRectFilled(ImVec2(sx - W * 0.5f, sy),
                      ImVec2(sx - W * 0.5f + W * frac, sy + H), IM_COL32(200, 45, 40, 255));
}

// WoW-style "con" colour for an enemy `level` relative to the player: red/orange
// (above), yellow (even), green/grey (below — trivial). Drives the level tag.
static ImU32 conColor(int enemyLevel, int playerLevel) {
    int d = enemyLevel - playerLevel;
    if (d >= 5)  return IM_COL32(210,  60,  50, 255);   // much higher — deadly
    if (d >= 3)  return IM_COL32(230, 140,  40, 255);   // higher — tough
    if (d >= -2) return IM_COL32(228, 214,  70, 255);   // even — fair fight
    if (d >= -7) return IM_COL32( 95, 200,  85, 255);   // lower — easy
    return IM_COL32(165, 165, 165, 255);                // trivial — grey
}

// Floating "Lv N" tag centred at a world position (above an enemy's head).
static void drawLevelTag(const glm::vec3& worldPos, int level, ImU32 col,
                         const glm::mat4& view, const glm::mat4& proj,
                         int fbW, int fbH, bool elite = false) {
    glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.01f) return;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) return;
    float sx = (ndc.x * 0.5f + 0.5f) * (float)fbW;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)fbH;
    char buf[24];
    // Elites get a starred, gold tag so they stand out from the trash mob.
    if (elite) std::snprintf(buf, sizeof(buf), "* Lv %d *", level);
    else       std::snprintf(buf, sizeof(buf), "Lv %d", level);
    ImU32 useCol = elite ? IM_COL32(255, 210, 120, 255) : col;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ts = ImGui::CalcTextSize(buf);
    ImVec2 p(sx - ts.x * 0.5f, sy - ts.y * 0.5f);
    dl->AddText(ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 200), buf);   // shadow
    dl->AddText(p, useCol, buf);
}

// F3 debug / session overlay — performance, world, rendered objects, server.
// Floating combat-text numbers: project each to screen, rise + fade over its
// life. Drawn on the foreground draw list so they sit above the world.
static void drawFloatingCombatText(AppContext& ctx, const Renderer& renderer) {
    if (ctx.floatingTexts.empty()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (const auto& f : ctx.floatingTexts) {
        float t  = (f.life > 0.0f) ? f.age / f.life : 1.0f;          // 0..1
        glm::vec3 wp = f.worldPos + glm::vec3(0.0f, t * 1.4f, 0.0f);  // rise as it ages
        glm::vec4 clip = renderer.frameProj * renderer.frameView * glm::vec4(wp, 1.0f);
        if (clip.w <= 0.01f) continue;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) continue;
        float sx = (ndc.x * 0.5f + 0.5f) * (float)renderer.frameFbW;
        float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)renderer.frameFbH;
        int a = (int)(255.0f * (1.0f - t * t));                      // fade out, slow then fast
        if (a < 0) a = 0;
        ImU32 col = IM_COL32(f.color.r, f.color.g, f.color.b, a);
        ImU32 sh  = IM_COL32(0, 0, 0, a);
        ImVec2 ts = ImGui::CalcTextSize(f.text.c_str());
        ImVec2 p(sx - ts.x * 0.5f, sy - ts.y * 0.5f);
        dl->AddText(ImVec2(p.x + 1, p.y + 1), sh, f.text.c_str());
        dl->AddText(p, col, f.text.c_str());
    }
}

static void renderDebugOverlay(AppContext& ctx) {
    // Tally the live client-side objects by kind.
    int vill = 0, band = 0, guard = 0, anim = 0, ferry = 0;
    int door = 0, prop = 0, light = 0, remote = 0;
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead) continue;
        switch (o->kind) {
            case ObjectKind::NPC: {
                NPCType t = static_cast<NPC*>(o.get())->type;
                if      (t == NPCType::Villager) vill++;
                else if (t == NPCType::Enemy)    band++;
                else if (t == NPCType::Guard)    guard++;
                break;
            }
            case ObjectKind::Animal:  anim++;  break;
            case ObjectKind::Vehicle: ferry++; break;
            case ObjectKind::Door:    door++;  break;
            case ObjectKind::Prop: {
                prop++;
                PropType pt = static_cast<Prop*>(o.get())->type;
                if (pt == PropType::Lantern || pt == PropType::StreetLamp) light++;
                break;
            }
            case ObjectKind::Player:
                if (o->id != 0) remote++;
                break;
            default: break;
        }
    }
    int totalObj = (int)ctx.objectManager.objects().size();

    const glm::vec3& cp = ctx.camera.position;
    int cx = (int)floorf(cp.x / 16.0f), cz = (int)floorf(cp.z / 16.0f);
    SurfaceSample surf = sampleSurface((int)cp.x, (int)cp.z);
    static const char* kBiomes[] = { "Plains", "Forest", "Desert", "Mountains",
                                     "Tundra", "Savanna", "Jungle" };
    const char* biome = (surf.biome >= 0 && surf.biome < 7) ? kBiomes[surf.biome] : "?";
    float gt = ctx.gameTime;
    const char* phase = (gt < 0.23f || gt > 0.77f) ? "Night"
                      : (gt < 0.30f)               ? "Dawn"
                      : (gt > 0.70f)               ? "Dusk" : "Day";

    ImGuiIO& io = ImGui::GetIO();
    static float fpsHist[90] = {};
    static int   fpsPos = 0;
    fpsHist[fpsPos] = io.Framerate;
    fpsPos = (fpsPos + 1) % 90;

    const ImVec4 head(0.62f, 0.86f, 1.0f, 1.0f);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::Begin("Debug", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "TERRAX DEBUG   [F3]");
    ImGui::Separator();

    ImGui::TextColored(head, "Performance");
    ImGui::Text("FPS %.0f   frame %.2f ms", io.Framerate,
                io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
    ImGui::PlotLines("##fps", fpsHist, 90, fpsPos, nullptr, 0.0f, 240.0f,
                     ImVec2(238, 38));

    ImGui::TextColored(head, "World");
    ImGui::Text("Pos    %.1f, %.1f, %.1f", cp.x, cp.y, cp.z);
    ImGui::Text("Chunk  %d, %d   loaded %d", cx, cz, (int)ctx.world.chunks.size());
    ImGui::Text("Biome  %s   render dist %d", biome, ctx.world.renderDistance);
    int dangerTier = dangerTierAt(cp.x, cp.z);
    float spawnDist = distanceFromSpawn(cp.x, cp.z);
    // Warm the colour as the tier climbs: green (safe) → amber → red (deadly).
    float dt01 = (float)(dangerTier - 1) / (float)(DANGER_MAX_TIER - 1);
    ImVec4 tierCol(0.45f + 0.55f * dt01, 0.95f - 0.65f * dt01, 0.35f, 1.0f);
    ImGui::TextColored(tierCol, "Danger tier %d / %d   (%.0fm from spawn)",
                       dangerTier, DANGER_MAX_TIER, spawnDist);
    ImGui::Text("Time   %.2f  (%s)", gt, phase);

    ImGui::TextColored(head, "Rendered objects (%d)", totalObj);
    ImGui::Text("Villagers %d   Bandits %d   Guards %d", vill, band, guard);
    ImGui::Text("Animals %d   Ferries %d", anim, ferry);
    ImGui::Text("Doors/houses %d   Props %d  (lights %d)", door, prop, light);
    ImGui::Text("Remote players %d", remote);

    ImGui::TextColored(head, "Server");
    if (g_serverStats.running.load()) {
        int sv = g_serverStats.villagers.load();
        int sb = g_serverStats.bandits.load();
        int sg = g_serverStats.guards.load();
        ImGui::Text("running   tick %.2f ms   players %d",
                    g_serverStats.tickMs.load(), g_serverStats.players.load());
        ImGui::Text("NPCs %d  (V %d  B %d  G %d)", sv + sb + sg, sv, sb, sg);
        ImGui::Text("Animals %d   Ferries %d",
                    g_serverStats.animals.load(), g_serverStats.ferries.load());
    } else {
        ImGui::TextDisabled("not hosting (remote server)");
    }

    ImGui::TextColored(head, "Camera");
    ImGui::Text("Yaw %.1f   Pitch %.1f   Noclip %s",
                ctx.playerYaw, ctx.camera.pitch, ctx.noclip ? "ON" : "off");

    ImGui::End();
}

// Whether to show the aiming reticle. Only while the player is actually firing
// a RANGED main-hand weapon — drawing a bow, or mid-cast with a staff/wand. A
// melee weapon never shows one, and even a ranged weapon only shows it during
// the attack, not while idle. Hidden during any menu/overlay or while sitting.
static bool shouldShowCrosshair(const AppContext& ctx) {
    if (ctx.showInventory || ctx.showCharacterLoadout || ctx.showMap ||
        ctx.paused || ctx.chatOpen || ctx.showTrainer || ctx.showQuestGiver ||
        ctx.showVendor || ctx.showQuestLog) return false;
    if (ctx.playerPose != PlayerPose::Standing) return false;
    Item* mh = ctx.inventory.equipped(EquipSlot::MainHand);
    if (!mh || mh->getKind() != ItemKind::Weapon) return false;
    WeaponItem* w = static_cast<WeaponItem*>(mh);
    bool ranged = (w->getType() == WeaponType::Bow) || w->isInstantRanged();
    if (!ranged) return false;
    // Active attack only: drawing the bow, or a swing/cast in progress.
    return ctx.bowChargingHeld ||
           (ctx.playerRig && (ctx.playerRig->isCasting || ctx.playerRig->isAttacking));
}

// Bottom-centre ability hotbar (slots 1..6) plus the role resource bar.
// Draw an ability's glyph procedurally (no texture assets) centred at `c`,
// fitting a box of side `size`, tinted by the ability's IconColor. Shapes are
// built from ImGui draw-list primitives in a normalised [-1,1] space (P(x,y)),
// so the same glyph scales cleanly on the hotbar and in the skill tree. This is
// a pure rendering map — the `switch` picks a shape, it doesn't dispatch
// behaviour (that stays virtual on Ability).
void drawAbilityIcon(ImDrawList* dl, ImVec2 c, float size,
                     AbilityIcon glyph, IconColor col) {
    const float    PI = 3.14159265f;
    const float    r  = size * 0.5f;
    const float    w  = std::max(2.0f, size * 0.11f);
    const ImU32    fg = IM_COL32(col.r, col.g, col.b, 255);
    const ImU32    hi = IM_COL32(std::min(255, col.r + 60), std::min(255, col.g + 60),
                                 std::min(255, col.b + 60), 255);
    const ImU32    dk = IM_COL32((int)(col.r * 0.45f), (int)(col.g * 0.45f),
                                 (int)(col.b * 0.45f), 255);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
    auto ray = [&](float ang, float r0, float r1, ImU32 cc, float th) {
        dl->AddLine(ImVec2(c.x + std::cos(ang) * r0 * r, c.y + std::sin(ang) * r0 * r),
                    ImVec2(c.x + std::cos(ang) * r1 * r, c.y + std::sin(ang) * r1 * r), cc, th);
    };

    switch (glyph) {
        case AbilityIcon::Sword:
            dl->AddLine(P(-0.55f, 0.6f), P(0.42f, -0.5f), fg, w);
            dl->AddTriangleFilled(P(0.32f, -0.38f), P(0.64f, -0.7f), P(0.52f, -0.22f), hi);
            dl->AddLine(P(-0.55f, 0.18f), P(0.0f, 0.5f), dk, w * 0.8f);     // crossguard
            dl->AddCircleFilled(P(-0.62f, 0.68f), w * 0.6f, dk, 10);       // pommel
            break;
        case AbilityIcon::Swords:
            dl->AddLine(P(-0.62f, 0.62f), P(0.62f, -0.62f), fg, w);
            dl->AddLine(P(0.62f, 0.62f), P(-0.62f, -0.62f), hi, w);
            dl->AddCircleFilled(P(-0.62f, 0.62f), w * 0.55f, dk, 10);
            dl->AddCircleFilled(P(0.62f, 0.62f), w * 0.55f, dk, 10);
            break;
        case AbilityIcon::Slash:
            dl->PathArcTo(P(0.15f, 0.15f), r * 0.95f, -PI * 0.92f, PI * 0.12f, 22);
            dl->PathStroke(fg, 0, w * 1.25f);
            dl->AddLine(P(0.5f, -0.55f), P(0.78f, -0.8f), hi, w * 0.6f);
            dl->AddLine(P(0.62f, -0.3f), P(0.9f, -0.5f), hi, w * 0.5f);
            break;
        case AbilityIcon::Whirl:
            dl->PathArcTo(P(-0.18f, -0.18f), r * 0.5f, -PI * 0.5f, PI * 0.85f, 18);
            dl->PathStroke(fg, 0, w);
            dl->PathArcTo(P(0.18f, 0.18f), r * 0.5f, PI * 0.5f, PI * 1.85f, 18);
            dl->PathStroke(hi, 0, w);
            break;
        case AbilityIcon::Hammer:
            dl->AddLine(P(0.0f, 0.78f), P(0.0f, -0.15f), dk, w * 0.95f);
            dl->AddRectFilled(P(-0.55f, -0.62f), P(0.55f, -0.12f), fg, 3.0f);
            dl->AddRect(P(-0.55f, -0.62f), P(0.55f, -0.12f), hi, 3.0f, 0, 1.5f);
            break;
        case AbilityIcon::Shield: {
            ImVec2 sp[5] = { P(-0.55f, -0.55f), P(0.55f, -0.55f), P(0.5f, 0.28f),
                             P(0.0f, 0.76f), P(-0.5f, 0.28f) };
            dl->AddConvexPolyFilled(sp, 5, fg);
            dl->AddPolyline(sp, 5, dk, ImDrawFlags_Closed, 1.5f);
            dl->AddLine(P(0.0f, -0.5f), P(0.0f, 0.6f), dk, w * 0.5f);
            break;
        }
        case AbilityIcon::ShieldBash: {
            ImVec2 sp[5] = { P(-0.5f, -0.4f), P(0.45f, -0.4f), P(0.4f, 0.32f),
                             P(0.0f, 0.72f), P(-0.45f, 0.32f) };
            dl->AddConvexPolyFilled(sp, 5, fg);
            dl->AddPolyline(sp, 5, dk, ImDrawFlags_Closed, 1.5f);
            dl->AddLine(P(0.42f, -0.5f), P(0.82f, -0.85f), hi, w * 0.6f);
            dl->AddLine(P(0.55f, -0.28f), P(0.92f, -0.42f), hi, w * 0.5f);
            dl->AddLine(P(0.22f, -0.6f), P(0.4f, -0.95f), hi, w * 0.5f);
            break;
        }
        case AbilityIcon::Chevrons:
            for (int i = 0; i < 3; i++) {
                float y = 0.45f - i * 0.45f;
                dl->AddLine(P(-0.55f, y + 0.25f), P(0.0f, y - 0.22f), fg, w);
                dl->AddLine(P(0.0f, y - 0.22f), P(0.55f, y + 0.25f), fg, w);
            }
            break;
        case AbilityIcon::Shockwave:
            dl->AddCircle(c, r * 0.35f, fg, 18, w * 0.7f);
            dl->AddCircle(c, r * 0.65f, fg, 24, w * 0.6f);
            dl->AddCircle(c, r * 0.95f, hi, 28, w * 0.5f);
            break;
        case AbilityIcon::Flame:
            dl->AddTriangleFilled(P(-0.45f, 0.35f), P(0.45f, 0.35f), P(0.0f, -0.82f), fg);
            dl->AddCircleFilled(P(0.0f, 0.32f), r * 0.45f, fg, 16);
            dl->AddTriangleFilled(P(-0.22f, 0.35f), P(0.22f, 0.35f), P(0.0f, -0.32f), hi);
            dl->AddCircleFilled(P(0.0f, 0.33f), r * 0.24f, hi, 12);
            break;
        case AbilityIcon::Frost:
            for (int i = 0; i < 6; i++) {
                float a = i * PI / 3.0f;
                ray(a, 0.0f, 0.88f, fg, w * 0.7f);
                ImVec2 mid = P(std::cos(a) * 0.5f, std::sin(a) * 0.5f);
                dl->AddLine(mid, ImVec2(mid.x + std::cos(a + 0.5f) * r * 0.26f,
                                        mid.y + std::sin(a + 0.5f) * r * 0.26f), fg, w * 0.5f);
                dl->AddLine(mid, ImVec2(mid.x + std::cos(a - 0.5f) * r * 0.26f,
                                        mid.y + std::sin(a - 0.5f) * r * 0.26f), fg, w * 0.5f);
            }
            dl->AddCircleFilled(c, w * 0.5f, hi, 10);
            break;
        case AbilityIcon::Holy:
            dl->AddCircleFilled(c, r * 0.4f, fg, 18);
            for (int i = 0; i < 8; i++) ray(i * PI / 4.0f, 0.55f, 0.95f, hi, w * 0.7f);
            break;
        case AbilityIcon::Nova:
            dl->AddCircleFilled(c, r * 0.3f, hi, 16);
            for (int i = 0; i < 8; i++) {
                float a = i * PI / 4.0f;
                ImVec2 outer = P(std::cos(a) * 0.95f, std::sin(a) * 0.95f);
                ImVec2 in1   = P(std::cos(a - 0.32f) * 0.42f, std::sin(a - 0.32f) * 0.42f);
                ImVec2 in2   = P(std::cos(a + 0.32f) * 0.42f, std::sin(a + 0.32f) * 0.42f);
                dl->AddTriangleFilled(in1, outer, in2, fg);
            }
            break;
        case AbilityIcon::Cross:
            dl->AddRectFilled(P(-0.24f, -0.8f), P(0.24f, 0.8f), fg, 2.0f);
            dl->AddRectFilled(P(-0.8f, -0.24f), P(0.8f, 0.24f), fg, 2.0f);
            dl->AddRectFilled(P(-0.1f, -0.72f), P(0.1f, 0.72f), hi, 1.0f);
            break;
        case AbilityIcon::Sanctuary:
            dl->AddCircle(c, r * 0.92f, fg, 28, w * 0.6f);
            dl->AddRectFilled(P(-0.14f, -0.5f), P(0.14f, 0.5f), fg, 1.5f);
            dl->AddRectFilled(P(-0.5f, -0.14f), P(0.5f, 0.14f), fg, 1.5f);
            break;
        case AbilityIcon::Leaf:
            dl->PathClear();
            dl->PathLineTo(P(0.0f, -0.75f));
            dl->PathBezierQuadraticCurveTo(P(0.72f, -0.1f), P(0.0f, 0.78f), 16);
            dl->PathBezierQuadraticCurveTo(P(-0.72f, -0.1f), P(0.0f, -0.75f), 16);
            dl->PathFillConvex(fg);
            dl->AddLine(P(0.0f, -0.7f), P(0.0f, 0.72f), dk, w * 0.45f);
            break;
        case AbilityIcon::Arrow:
            dl->AddLine(P(0.0f, 0.78f), P(0.0f, -0.45f), fg, w);
            dl->AddTriangleFilled(P(-0.4f, -0.28f), P(0.4f, -0.28f), P(0.0f, -0.86f), fg);
            dl->AddLine(P(0.0f, 0.78f), P(-0.28f, 0.5f), hi, w * 0.7f);
            dl->AddLine(P(0.0f, 0.78f), P(0.28f, 0.5f), hi, w * 0.7f);
            break;
        case AbilityIcon::Claw:
            for (int i = 0; i < 3; i++) {
                float x = -0.45f + i * 0.45f;
                dl->AddLine(P(x - 0.12f, 0.72f), P(x + 0.18f, -0.72f), i == 1 ? hi : fg, w * 0.85f);
            }
            break;
        default:
            dl->AddCircleFilled(c, r * 0.5f, fg, 16);
            break;
    }
}

static void drawHotbar(AppContext& ctx) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 o = vpPos(), s = vpSize();
    const int   N    = AppContext::HOTBAR_SLOTS;
    const float slot = 46.0f, gap = 6.0f;
    float totalW = N * slot + (N - 1) * gap;
    float x0 = o.x + (s.x - totalW) * 0.5f;
    float y0 = o.y + s.y - 104.0f - slot;   // sit above the health / XP bar

    for (int i = 0; i < N; i++) {
        float x = x0 + i * (slot + gap);
        ImVec2 a(x, y0), b(x + slot, y0 + slot);
        AbilityId aid = ctx.hotbar[i];
        Ability*  ab  = (aid != AbilityId::None) ? ctx.findAbility(aid) : nullptr;
        bool sel = (i == ctx.selectedHotbar) && ab;
        dl->AddRectFilled(a, b, IM_COL32(18, 18, 26, 205), 4.0f);
        dl->AddRect(a, b, sel ? IM_COL32(255, 210, 120, 255) : IM_COL32(120, 120, 140, 220),
                    4.0f, 0, sel ? 2.5f : 1.5f);
        dl->AddText(ImVec2(x + 3, y0 + 1), IM_COL32(210, 210, 225, 255),
                    std::to_string(i + 1).c_str());
        if (!ab) continue;
        drawAbilityIcon(dl, ImVec2(x + slot * 0.5f, y0 + slot * 0.5f + 3.0f),
                        slot * 0.62f, ab->icon(), ab->iconColor());
        float cd = ctx.hotbarCooldown[i], cdMax = ab->cooldown();
        if (cd > 0.0f && cdMax > 0.0f) {
            float frac = std::min(1.0f, cd / cdMax);
            dl->AddRectFilled(ImVec2(x, b.y - slot * frac), b, IM_COL32(0, 0, 0, 150), 4.0f);
            std::string cds = std::to_string((int)ceilf(cd));
            ImVec2 cs = ImGui::CalcTextSize(cds.c_str());
            dl->AddText(ImVec2(x + (slot - cs.x) * 0.5f, y0 + slot * 0.5f - 4),
                        IM_COL32(255, 235, 190, 255), cds.c_str());
        }
    }

    float ry = y0 + slot + 5.0f, rbH = 9.0f;
    ImVec2 ra(x0, ry), rb(x0 + totalW, ry + rbH);
    dl->AddRectFilled(ra, rb, IM_COL32(18, 18, 26, 205), 2.0f);
    float frac = (ctx.resourceMax > 0.0f) ? (ctx.resource / ctx.resourceMax) : 0.0f;
    ImU32 rcol = ctx.resourceType == ResourceType::Mana ? IM_COL32(70, 120, 230, 255)
               : ctx.resourceType == ResourceType::Rage ? IM_COL32(200, 60, 50, 255)
               :                                          IM_COL32(220, 200, 70, 255);
    dl->AddRectFilled(ra, ImVec2(x0 + totalW * frac, ry + rbH), rcol, 2.0f);
    std::string rtxt = std::string(resourceName(ctx.resourceType)) + "  " +
                       std::to_string((int)ctx.resource) + "/" + std::to_string((int)ctx.resourceMax);
    ImVec2 rs = ImGui::CalcTextSize(rtxt.c_str());
    dl->AddText(ImVec2(x0 + (totalW - rs.x) * 0.5f, ry - 2), IM_COL32(235, 235, 240, 255), rtxt.c_str());
}

// A small white reticle at screen centre, drawn with a 1px dark offset so it
// reads over both bright sky and dark terrain.
static void drawCrosshair() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 o = vpPos(), s = vpSize();
    ImVec2 c(o.x + s.x * 0.5f, o.y + s.y * 0.5f);
    const ImU32 col    = IM_COL32(255, 255, 255, 205);
    const ImU32 shadow = IM_COL32(0, 0, 0, 130);
    const float gap = 5.0f, len = 9.0f, th = 1.6f;
    auto tick = [&](float ax, float ay, float bx, float by) {
        dl->AddLine(ImVec2(c.x + ax + 1, c.y + ay + 1), ImVec2(c.x + bx + 1, c.y + by + 1), shadow, th);
        dl->AddLine(ImVec2(c.x + ax,     c.y + ay),     ImVec2(c.x + bx,     c.y + by),     col,    th);
    };
    tick(-gap - len, 0, -gap, 0);    // left
    tick( gap, 0,  gap + len, 0);    // right
    tick(0, -gap - len, 0, -gap);    // up
    tick(0,  gap, 0,  gap + len);    // down
    dl->AddCircleFilled(c, 1.3f, col);
}

// Active-buff tray: a centred row of badges near the top of the screen, one per
// timed buff (Shield Wall, Battle Shout, Last Stand, Barrier, ...), each showing
// the ability's glyph, a depleting time bar and the seconds left — so it's clear
// the buff is on and how long remains.
static void drawActiveBuffs(AppContext& ctx) {
    if (ctx.activeBuffs.empty()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 o = vpPos(), s = vpSize();
    const int   n   = (int)ctx.activeBuffs.size();
    const float box = 42.0f, gap = 8.0f;
    float totalW = n * box + (n - 1) * gap;
    float x0 = o.x + (s.x - totalW) * 0.5f;
    float y0 = o.y + 52.0f;
    for (int i = 0; i < n; i++) {
        const ActiveBuff& b = ctx.activeBuffs[i];
        Ability*  ab  = ctx.findAbility(b.id);
        IconColor col = ab ? ab->iconColor() : IconColor{150, 200, 255};
        ImU32     border = IM_COL32(col.r, col.g, col.b, 255);
        float x = x0 + i * (box + gap);
        ImVec2 a(x, y0), bb(x + box, y0 + box);
        // Pulse the border in the buff's final second so an expiry reads clearly.
        float a8 = (b.ttl < 1.0f) ? (0.45f + 0.55f * b.ttl) : 1.0f;
        dl->AddRectFilled(a, bb, IM_COL32(18, 20, 28, 215), 5.0f);
        dl->AddRect(a, bb, IM_COL32(col.r, col.g, col.b, (int)(a8 * 255)), 5.0f, 0, 2.0f);
        if (ab) drawAbilityIcon(dl, ImVec2(x + box * 0.5f, y0 + box * 0.5f - 2.0f),
                                box * 0.6f, ab->icon(), col);
        // Depleting time bar along the bottom edge.
        float frac = (b.total > 0.0f) ? std::max(0.0f, std::min(1.0f, b.ttl / b.total)) : 0.0f;
        ImVec2 ba(x + 3, bb.y - 6), be(x + box - 3, bb.y - 3);
        dl->AddRectFilled(ba, be, IM_COL32(0, 0, 0, 150), 1.5f);
        dl->AddRectFilled(ba, ImVec2(ba.x + (be.x - ba.x) * frac, be.y), border, 1.5f);
        // Seconds remaining, top-right.
        std::string secs = std::to_string((int)ceilf(b.ttl));
        dl->AddText(ImVec2(x + box - 6.0f - secs.size() * 7.0f, y0 + 2.0f),
                    IM_COL32(240, 240, 245, 255), secs.c_str());
    }
}

// The Class Trainer window — opened by pressing E at a town trainer NPC. Lets
// the player change role mid-game: keeps their character level but reseeds the
// new role's core abilities and refunds skill points to re-spend in the new
// tree ("keep level, fresh tree"). Purely client-side; the role rides the next
// player-model packet so other clients see the new body size.
static void drawTrainerWindow(AppContext& ctx) {
    if (!ctx.showTrainer) return;
    ImVec2 vp = vpPos(), vs = vpSize();
    const float W = 440.0f, H = 270.0f;
    ImGui::SetNextWindowPos(ImVec2(vp.x + (vs.x - W) * 0.5f, vp.y + (vs.y - H) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Class Trainer", &ctx.showTrainer,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "Choose your calling");
    ImGui::TextWrapped("Change your class. You keep your level, but your skills reset to "
                       "the new class's tree (skill points are refunded to re-spend).");
    ImGui::Separator();
    ImGui::Text("Current: %s  (Level %d)", roleName(ctx.playerRole), ctx.playerLevel);
    ImGui::Spacing();

    struct RoleOpt { PlayerRole role; const char* name; const char* blurb; };
    static const RoleOpt opts[3] = {
        { PlayerRole::Tank,   "Tank",   "Broad and tough — taunts, shields, holds the line." },
        { PlayerRole::DPS,    "DPS",    "Lean and deadly — melee flurries and ranged bolts." },
        { PlayerRole::Healer, "Healer", "Mends allies and smites foes with holy light." },
    };
    for (const RoleOpt& o : opts) {
        ImGui::PushID((int)o.role);
        if (o.role == ctx.playerRole) {
            ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.6f, 1.0f), "%-7s (current)", o.name);
        } else if (ImGui::Button(o.name, ImVec2(96, 0))) {
            // Swap class: keep level, reseed the role, refund all earned points.
            ctx.playerRole = o.role;
            if (ctx.playerRig) {
                ctx.playerRig->heightScale = roleHeightScale(o.role);
                ctx.playerRig->weightScale = roleWeightScale(o.role);
                if (ctx.playerRig->torso) {
                    ctx.playerRig->torso->scale.x = ctx.playerRig->weightScale;
                    ctx.playerRig->torso->scale.z = ctx.playerRig->weightScale;
                }
            }
            ctx.setupRoleLoadout();                                        // new core abilities + stats
            ctx.skillPoints = (ctx.playerLevel > 1) ? ctx.playerLevel - 1 : 0;  // refund (1 pt/level earned)
            if (ctx.playerRig) rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
            sendPlayerModelUpdate(ctx);                                    // other clients see the new size
            AppContext::HudToast t{ std::string("Now a ") + roleName(o.role) + "!",
                                    Voxel{255, 220, 120, 255}, 2.0f };
            ctx.toasts.push_back(std::move(t));
            ctx.showTrainer = false;
        }
        ImGui::SameLine(120.0f);
        ImGui::TextDisabled("%s", o.blurb);
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(-1, 0))) ctx.showTrainer = false;
    ImGui::End();
}

// The Quest Giver window — opened by pressing E at a town quest-giver NPC. Lists
// the town's deterministic quest board (getTownQuests); Accept adds a quest to
// the player's active list. Progress tracking + turn-in arrive in later phases.
static void drawQuestGiverWindow(AppContext& ctx) {
    if (!ctx.showQuestGiver) return;
    ImVec2 vp = vpPos(), vs = vpSize();
    const float W = 540.0f, H = 440.0f;
    ImGui::SetNextWindowPos(ImVec2(vp.x + (vs.x - W) * 0.5f, vp.y + (vs.y - H) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Quest Giver", &ctx.showQuestGiver,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "Tasks for an able adventurer");
    ImGui::SameLine(ImGui::GetWindowWidth() - 130.0f);
    ImGui::TextColored(ImVec4(0.93f, 0.82f, 0.35f, 1.0f), "Gold: %d", ctx.playerGold);
    ImGui::TextWrapped("There's work to be done out in the wilds. Take what suits you.");
    ImGui::Separator();

    // Completed quests ready to hand in (grant rewards on turn-in).
    int turnIn = -1;
    bool anyComplete = false;
    for (size_t i = 0; i < ctx.activeQuests.size(); ++i) {
        const Quest& aq = ctx.activeQuests[i];
        if (aq.status != QuestStatus::Complete) continue;
        anyComplete = true;
        ImGui::PushID(1000 + (int)i);
        ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.6f, 1.0f), "[Done] %s", aq.title.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Turn in", ImVec2(90, 0))) turnIn = (int)i;
        ImGui::PopID();
    }
    if (anyComplete) ImGui::Separator();
    if (turnIn >= 0) { turnInQuest(ctx, turnIn); ImGui::End(); return; }  // list mutated; redraw next frame

    const std::vector<Quest>& board = getTownQuests(ctx.questGiverTown);
    if (board.empty()) ImGui::TextDisabled("No work available right now.");

    auto isActive = [&](uint32_t id) {
        for (const Quest& q : ctx.activeQuests) if (q.id == id) return true;
        return false;
    };

    ImGui::BeginChild("questlist", ImVec2(0, H - 120.0f), false);
    for (const Quest& q : board) {
        ImGui::PushID((int)q.id);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.92f, 1.0f, 1.0f), "%s", q.title.c_str());
        ImGui::TextWrapped("%s", q.text.c_str());
        ImGui::TextDisabled("Recommended level %d  -  Reward: %d XP, %d gold%s",
                            q.recommendedLevel, q.rewardXp, q.rewardGold,
                            q.rewardItem ? ", + an item" : "");
        if (isActive(q.id)) {
            ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.6f, 1.0f), "Accepted");
        } else if (ImGui::Button("Accept", ImVec2(110, 0))) {
            Quest accepted = q;
            accepted.status   = QuestStatus::Active;
            accepted.progress = 0;
            ctx.activeQuests.push_back(std::move(accepted));
            AppContext::HudToast t{ std::string("Quest accepted: ") + q.title,
                                    Voxel{255, 220, 120, 255}, 3.0f };
            ctx.toasts.push_back(std::move(t));
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Text("Active quests: %d", (int)ctx.activeQuests.size());
    if (ImGui::Button("Close", ImVec2(-1, 0))) ctx.showQuestGiver = false;
    ImGui::End();
}

// Quest journal (J) — a full panel listing every active/complete quest with its
// objective progress, target region, recommended level and rewards.
static void drawQuestLog(AppContext& ctx) {
    if (!ctx.showQuestLog) return;
    ImVec2 vp = vpPos(), vs = vpSize();
    const float W = 520.0f, H = 460.0f;
    ImGui::SetNextWindowPos(ImVec2(vp.x + (vs.x - W) * 0.5f, vp.y + (vs.y - H) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Quest Journal", &ctx.showQuestLog,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    int active = 0;
    for (const Quest& q : ctx.activeQuests)
        if (q.status != QuestStatus::TurnedIn) ++active;
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "Active quests (%d)", active);
    ImGui::Separator();
    if (ctx.activeQuests.empty())
        ImGui::TextDisabled("No active quests. Find a Quest Giver in a town.");

    ImGui::BeginChild("questlogscroll", ImVec2(0, H - 90.0f), false);
    for (const Quest& q : ctx.activeQuests) {
        if (q.status == QuestStatus::TurnedIn) continue;
        ImGui::PushID((int)q.id);
        bool done = (q.status == QuestStatus::Complete);
        ImGui::TextColored(done ? ImVec4(0.55f, 0.95f, 0.6f, 1.0f) : ImVec4(0.88f, 0.92f, 1.0f, 1.0f),
                           "%s%s", q.title.c_str(), done ? "  [COMPLETE]" : "");
        ImGui::TextWrapped("%s", q.text.c_str());
        const char* what = (q.kind == QuestKind::KillEnemies) ? questEnemyLabel(q.targetNpcType)
                         : (q.kind == QuestKind::SlayBoss)     ? "Boss"
                         : q.collectName.c_str();
        ImGui::Text("   Progress: %s %d / %d", what, q.progress, q.requiredCount);
        ImGui::TextDisabled("   Region: %s (tier %d, rec. level %d)",
                            q.targetName.c_str(), q.targetTier, q.recommendedLevel);
        ImGui::TextDisabled("   Reward: %d XP, %d gold%s", q.rewardXp, q.rewardGold,
                            q.rewardItem ? ", + an item" : "");
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::Button("Close", ImVec2(-1, 0))) ctx.showQuestLog = false;
    ImGui::End();
}

// Target frame (top-centre) + an in-world selection marker over the locked
// target. Shows the foe's name, level (con-coloured) and health.
static void drawTargetFrame(AppContext& ctx, const Renderer& renderer) {
    NPC* t = currentTargetNpc(ctx);
    if (!t) return;
    int   lvl   = (int)t->level;
    float maxHp = defaultNpcHealth(t->type) * npcHpScaleForLevel(t->level);
    float frac  = std::clamp(maxHp > 0.0f ? t->health / maxHp : 1.0f, 0.0f, 1.0f);
    const char* name = isHostileNpc(t->type) ? questEnemyLabel((uint8_t)t->type) : "Target";
    ImU32 con  = conColor(lvl, ctx.playerLevel);
    ImVec4 conV = ImGui::ColorConvertU32ToFloat4(con);

    ImVec2 vp = vpPos(), vs = vpSize();
    const float W = 248.0f;
    ImGui::SetNextWindowPos(ImVec2(vp.x + (vs.x - W) * 0.5f, vp.y + 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##targetframe", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(conV, "%s", name);
    ImGui::SameLine();
    ImGui::TextColored(conV, "  Lv %d", lvl);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float bw = W - 16.0f;
    dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + 12.0f), IM_COL32(20, 15, 12, 220));
    dl->AddRectFilled(p, ImVec2(p.x + bw * frac, p.y + 12.0f), IM_COL32(200, 45, 40, 255));
    ImGui::Dummy(ImVec2(bw, 14.0f));
    ImGui::Text("%.0f / %.0f", t->health, maxHp);
    ImGui::End();

    // In-world selection marker: a con-coloured downward chevron above the head.
    glm::vec4 clip = renderer.frameProj * renderer.frameView *
                     glm::vec4(t->position + glm::vec3(0.0f, 3.0f, 0.0f), 1.0f);
    if (clip.w > 0.01f) {
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z >= -1.0f && ndc.z <= 1.0f) {
            float sx = (ndc.x * 0.5f + 0.5f) * (float)renderer.frameFbW;
            float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)renderer.frameFbH;
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            fg->AddTriangleFilled(ImVec2(sx - 8, sy - 10), ImVec2(sx + 8, sy - 10),
                                  ImVec2(sx, sy), con);
            fg->AddTriangle(ImVec2(sx - 8, sy - 10), ImVec2(sx + 8, sy - 10),
                            ImVec2(sx, sy), IM_COL32(0, 0, 0, 200), 1.5f);
        }
    }
}

// The Vendor window — opened by pressing E at a town merchant. Buy generated,
// tier-appropriate gear for gold; sell items from your bags. Client-side.
static void drawVendorWindow(AppContext& ctx) {
    if (!ctx.showVendor) return;
    ImVec2 vp = vpPos(), vs = vpSize();
    const float W = 560.0f, H = 460.0f;
    ImGui::SetNextWindowPos(ImVec2(vp.x + (vs.x - W) * 0.5f, vp.y + (vs.y - H) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Merchant", &ctx.showVendor,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "Wares & trade");
    ImGui::SameLine(ImGui::GetWindowWidth() - 130.0f);
    ImGui::TextColored(ImVec4(0.93f, 0.82f, 0.35f, 1.0f), "Gold: %d", ctx.playerGold);
    ImGui::Separator();

    ImGui::Columns(2, "vendorcols", true);

    // --- For sale ---------------------------------------------------------
    ImGui::TextColored(ImVec4(0.85f, 0.92f, 1.0f, 1.0f), "For sale");
    ImGui::BeginChild("buy", ImVec2(0, H - 120.0f), false);
    int n = vendorStockCount(ctx.vendorTown);
    for (int i = 0; i < n; ++i) {
        const Item* it = vendorStockItem(ctx.vendorTown, i);
        if (!it) continue;
        int price = vendorStockPrice(ctx.vendorTown, i);
        ImGui::PushID(i);
        Voxel rc = rarityUiColor(it->rarity);
        ImGui::TextColored(ImVec4(rc.r / 255.f, rc.g / 255.f, rc.b / 255.f, 1.f),
                           "%s", it->getName().c_str());
        ImGui::TextDisabled("iLvl %d   %dg", it->level, price);
        bool afford = ctx.playerGold >= price;
        if (!afford) ImGui::BeginDisabled();
        if (ImGui::Button("Buy", ImVec2(70, 0))) vendorBuy(ctx, ctx.vendorTown, i);
        if (!afford) ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    // --- Your bags (sell) -------------------------------------------------
    ImGui::TextColored(ImVec4(0.85f, 0.92f, 1.0f, 1.0f), "Your bags");
    ImGui::BeginChild("sell", ImVec2(0, H - 120.0f), false);
    const auto& bag = ctx.inventory.items();
    int sellIdx = -1;
    for (int i = 0; i < (int)bag.size(); ++i) {
        Item* it = bag[(size_t)i].get();
        if (!it) continue;
        ImGui::PushID(10000 + i);
        Voxel rc = rarityUiColor(it->rarity);
        ImGui::TextColored(ImVec4(rc.r / 255.f, rc.g / 255.f, rc.b / 255.f, 1.f),
                           "%s", it->getName().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Sell")) sellIdx = i;
        ImGui::SameLine();
        ImGui::TextDisabled("%dg", itemSellPrice(*it));
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (sellIdx >= 0) vendorSell(ctx, sellIdx);

    ImGui::Columns(1);
    if (ImGui::Button("Close", ImVec2(-1, 0))) ctx.showVendor = false;
    ImGui::End();
}

// A small always-on gold readout (bottom-left of the viewport).
static void drawGoldChip(AppContext& ctx) {
    if (ctx.paused) return;
    ImVec2 vp = vpPos(), vs = vpSize();
    ImGui::SetNextWindowPos(ImVec2(vp.x + 14.0f, vp.y + vs.y - 40.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.40f);
    ImGui::Begin("##goldchip", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(ImVec4(0.95f, 0.84f, 0.38f, 1.0f), "Gold: %d", ctx.playerGold);
    ImGui::End();
}

// On-screen quest tracker (top-right) — lists active quests + live progress.
static void drawQuestTracker(AppContext& ctx) {
    if (ctx.activeQuests.empty()) return;
    if (ctx.paused || ctx.showMap || ctx.showInventory || ctx.showCharacterLoadout ||
        ctx.showQuestGiver || ctx.showTrainer) return;
    ImVec2 vp = vpPos(), vs = vpSize();
    const float W = 268.0f;
    ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x - W - 14.0f, vp.y + 70.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.42f);
    ImGui::Begin("##questtracker", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Quests");
    ImGui::Separator();
    int shown = 0;
    for (const Quest& q : ctx.activeQuests) {
        if (q.status == QuestStatus::TurnedIn) continue;
        if (++shown > 6) break;
        bool done = (q.status == QuestStatus::Complete);
        ImGui::TextColored(done ? ImVec4(0.55f, 0.95f, 0.6f, 1.0f)
                                : ImVec4(0.90f, 0.92f, 1.0f, 1.0f),
                           "%s", q.title.c_str());
        if (done) {
            ImGui::TextDisabled("   Complete - return to a giver");
        } else {
            const char* what = (q.kind == QuestKind::KillEnemies) ? questEnemyLabel(q.targetNpcType)
                             : (q.kind == QuestKind::SlayBoss)     ? "Boss"
                             : q.collectName.c_str();
            ImGui::TextDisabled("   %s  %d/%d", what, q.progress, q.requiredCount);
        }
    }
    ImGui::End();
}

void renderPlayUI(AppContext& ctx, GLFWwindow* window, const Renderer& renderer) {
    (void)window;

    if (ctx.showDebugOverlay) renderDebugOverlay(ctx);

    if (shouldShowCrosshair(ctx)) drawCrosshair();
    if (!ctx.showMap) { drawHotbar(ctx); drawActiveBuffs(ctx); }

    // Cast bar — shown while channelling an ability with a cast time.
    if (ctx.castTimer > 0.0f && ctx.castTotal > 0.0f) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 o = vpPos(), s = vpSize();
        const float w = 240.0f, barH = 16.0f;
        float x0 = o.x + (s.x - w) * 0.5f;
        float y0 = o.y + s.y * 0.5f + 60.0f;     // below the reticle, above the hotbar
        float frac = std::min(1.0f, std::max(0.0f, 1.0f - ctx.castTimer / ctx.castTotal));
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + barH), IM_COL32(18, 18, 26, 210), 3.0f);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w * frac, y0 + barH), IM_COL32(150, 200, 255, 235), 3.0f);
        dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + barH), IM_COL32(120, 120, 140, 220), 3.0f, 0, 1.5f);
        Ability* ab = ctx.findAbility(ctx.castingAbility);
        const char* nm = ab ? ab->name() : "Casting";
        ImVec2 ts = ImGui::CalcTextSize(nm);
        dl->AddText(ImVec2(x0 + (w - ts.x) * 0.5f, y0 - 16.0f), IM_COL32(220, 235, 255, 245), nm);
    }

    // Underwater tint
    if (ctx.headUnderwater) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImVec2 o = vpPos(), s = vpSize();
        dl->AddRectFilled(o, ImVec2(o.x + s.x, o.y + s.y), IM_COL32(15, 60, 140, 90));
    }

    // Bow draw charge bar — visible while the player is holding left
    // mouse with a bow equipped. Centred above the reticle so it shows
    // up clearly while aiming. Colour shifts from amber → bright gold
    // as the draw approaches full.
    if (ctx.bowChargingHeld && ctx.bowCharge > 0.0f) {
        float w = 220.0f;
        ImVec2 vp = vpPos(), vs = vpSize();
        ImGui::SetNextWindowPos(ImVec2(vp.x + (vs.x - w) * 0.5f, vp.y + vs.y * 0.5f - 80.0f));
        ImGui::SetNextWindowSize(ImVec2(w, 22));
        ImGui::Begin("BowCharge", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
        float t = ctx.bowCharge;
        ImVec4 col(0.85f + 0.10f * t, 0.55f + 0.40f * t, 0.20f + 0.15f * t, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(t, ImVec2(-1, 16),
                           t >= 0.99f ? "MAX" : "");
        ImGui::PopStyleColor();
        ImGui::End();
    }

    // Breath bar
    if (ctx.breathTime < 29.9f) {
        ImVec2 vp = vpPos(), vs = vpSize();
        ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x * 0.5f - 150.0f, vp.y + vs.y - 100.0f));
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

    // HUD — health, level + XP bar.
    ImVec2 hudVp = vpPos(), hudVs = vpSize();
    ImGui::SetNextWindowPos(ImVec2(hudVp.x + hudVs.x * 0.5f - 150.0f, hudVp.y + hudVs.y - 90.0f));
    ImGui::SetNextWindowSize(ImVec2(300, 78));
    ImGui::Begin("HUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoMove);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
    ImGui::ProgressBar(ctx.playerHealth, ImVec2(-1, 14), "");
    ImGui::PopStyleColor();

    // XP bar sits just below health. ProgressBar fraction is XP toward
    // the next level — matching `xpForNextLevel` in gameplay.cpp.
    int xpNeed = 100 + 50 * (ctx.playerLevel - 1);
    float xpFrac = (xpNeed > 0) ? (ctx.playerXp / float(xpNeed)) : 0.0f;
    if (xpFrac > 1.0f) xpFrac = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.42f, 0.65f, 1.00f, 1.0f));
    ImGui::ProgressBar(xpFrac, ImVec2(-1, 8), "");
    ImGui::PopStyleColor();
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f),
                       "%s   Lv %d   %d/%d XP   HP %d/%d",
                       roleName(ctx.playerRole), ctx.playerLevel,
                       (int)ctx.playerXp, xpNeed,
                       (int)(ctx.playerHealth * ctx.maxHpScaled),
                       (int)ctx.maxHpScaled);
    ImGui::End();

    // Nametags
    for (auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        std::string label = p.name.empty() ? ("Player" + std::to_string(p.id)) : p.name;
        drawNametag(p.position + glm::vec3(0.0f, 2.1f, 0.0f), label,
                    renderer.frameView, renderer.frameProj,
                    renderer.frameFbW, renderer.frameFbH);
    }

    // Interactable prop in front of the player (chair, bed, etc.) — show the
    // E-hint just below the crosshair. The hint comes from the interactable
    // system so adding new actions only takes a new InteractAction case.
    if (ctx.pendingInteraction.action != InteractAction::None &&
        ctx.playerPose == PlayerPose::Standing) {
        ImVec2 vp = vpPos(), vs = vpSize();
        ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x * 0.5f - 110.0f, vp.y + vs.y * 0.5f + 36.0f));
        ImGui::SetNextWindowSize(ImVec2(220, 26));
        ImGui::Begin("InteractHint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(0.96f, 0.90f, 0.70f, 1.0f), "%s",
                           ctx.pendingInteraction.hint
                               ? ctx.pendingInteraction.hint : "[E] Interact");
        ImGui::End();
    }
    // While seated / lying, surface a clear "press E to stand" prompt.
    if (ctx.playerPose != PlayerPose::Standing) {
        ImVec2 vp = vpPos(), vs = vpSize();
        ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x * 0.5f - 90.0f, vp.y + vs.y * 0.5f + 36.0f));
        ImGui::SetNextWindowSize(ImVec2(180, 26));
        ImGui::Begin("PoseExitHint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(0.96f, 0.90f, 0.70f, 1.0f), "[E] Get up");
        ImGui::End();
    }

    // NPC interaction — nametag + talk prompt for the villager being faced,
    // and the dialogue box once a conversation has been started.
    if (!ctx.talkTargetName.empty()) {
        drawNametag(ctx.talkTargetPos + glm::vec3(0.0f, 2.1f, 0.0f),
                    ctx.talkTargetName, renderer.frameView, renderer.frameProj,
                    renderer.frameFbW, renderer.frameFbH);
        if (ctx.talkTimer <= 0.0f) {
            ImVec2 vp = vpPos(), vs = vpSize();
            ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x * 0.5f - 70.0f, vp.y + vs.y * 0.5f + 36.0f));
            ImGui::SetNextWindowSize(ImVec2(140, 26));
            ImGui::Begin("TalkHint", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
            ImGui::TextColored(ImVec4(0.96f, 0.90f, 0.70f, 1.0f), "[E] Talk");
            ImGui::End();
        }
    }
    if (ctx.talkTimer > 0.0f) {
        ImVec2 vp = vpPos(), vs = vpSize();
        // Sit well clear of the ability hotbar (which draws on the foreground
        // draw list and would otherwise cover this regular window).
        ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x * 0.5f - 220.0f, vp.y + vs.y - 268.0f));
        ImGui::SetNextWindowSize(ImVec2(440, 80));
        ImGui::Begin("NpcDialogue", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", ctx.talkName.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", ctx.talkLine.c_str());
        ImGui::End();
    }

    // Loot pickup prompt — shown when the player is in range of a drop.
    // Sits just above the reticle so the player doesn't have to look down
    // to read it.
    if (!ctx.lootHintName.empty()) {
        float boxW = 320.0f;
        ImVec2 vp = vpPos(), vs = vpSize();
        ImGui::SetNextWindowPos(ImVec2(vp.x + (vs.x - boxW) * 0.5f, vp.y + vs.y * 0.5f + 60.0f));
        ImGui::SetNextWindowSize(ImVec2(boxW, 32));
        ImGui::Begin("LootHint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs);
        ImVec4 c(ctx.lootHintColor.r / 255.0f,
                 ctx.lootHintColor.g / 255.0f,
                 ctx.lootHintColor.b / 255.0f, 1.0f);
        ImGui::TextColored(ImVec4(0.96f, 0.90f, 0.70f, 1.0f), "[E] Pick up");
        ImGui::SameLine();
        ImGui::TextColored(c, "%s", ctx.lootHintName.c_str());
        ImGui::End();
    }

    // NPC health bars over damaged NPCs, and a level tag over nearby hostiles
    // (shown even at full health so the player can size up a fight before
    // engaging — coloured by level relative to the player).
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        NPC* n = static_cast<NPC*>(o.get());
        if (n->dyingFlag) continue;
        // Max HP must mirror the server's level-scaled spawn HP, else a leveled
        // foe's bar would read past full.
        float maxHp = defaultNpcHealth(n->type) * npcHpScaleForLevel(n->level);
        float frac  = (maxHp > 0.0f) ? (n->health / maxHp) : 1.0f;
        bool hostile = isHostileNpc(n->type);
        float dist   = glm::distance(n->position, ctx.camera.position);
        if (hostile && dist < 45.0f) {
            drawLevelTag(n->position + glm::vec3(0.0f, 2.65f, 0.0f),
                         (int)n->level, conColor((int)n->level, ctx.playerLevel),
                         renderer.frameView, renderer.frameProj,
                         renderer.frameFbW, renderer.frameFbH, n->elite);
        }
        if (frac >= 0.995f) continue;                     // hide the bar at full health
        drawHealthBar(n->position + glm::vec3(0.0f, 2.3f, 0.0f), frac,
                      renderer.frameView, renderer.frameProj,
                      renderer.frameFbW, renderer.frameFbH);
    }

    // Floating combat-text damage numbers rising off struck enemies.
    drawFloatingCombatText(ctx, renderer);

    // Chat
    if (ctx.client) {
        const float logH = 120.0f;
        ImVec2 vp = vpPos(), vs = vpSize();
        ImGui::SetNextWindowPos(ImVec2(vp.x + 12.0f,
                                       vp.y + vs.y - logH - (ctx.chatOpen ? 90.0f : 12.0f)));
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
            ImGui::SetNextWindowPos(ImVec2(vp.x + 12.0f, vp.y + vs.y - 72.0f));
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

    // World map overlay
    renderMapUI(ctx);

    // Player list
    if (ctx.showPlayerList) {
        ImVec2 vp = vpPos(), vs = vpSize();
        ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x - 220.0f, vp.y + 12.0f));
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

    // I and C both pull up the unified equipment screen (inventory grid +
    // character loadout side by side), so drag-drop between them works.
    if (ctx.showInventory || ctx.showCharacterLoadout) {
        renderInventoryUI(ctx, window);
        renderCharacterLoadoutUI(ctx, window);
    }

    // Skill tree overlay (K) — spend skill points to unlock abilities.
    if (ctx.showSkillTree) renderSkillTreeUI(ctx);

    // Class Trainer window (E at a town trainer) — change role mid-game.
    drawTrainerWindow(ctx);
    // Quest Giver window (E at a town quest-giver) — accept town quests.
    drawQuestGiverWindow(ctx);
    // Vendor shop window (E at a town merchant) — buy/sell gear for gold.
    drawVendorWindow(ctx);
    // Always-on gold readout.
    drawGoldChip(ctx);
    // Active-quest tracker (top-right HUD).
    drawQuestTracker(ctx);
    // Target frame + in-world selection marker (top-centre).
    drawTargetFrame(ctx, renderer);
    // Quest journal (J).
    drawQuestLog(ctx);

    // Toast queue — XP / level-up / loot notifications stacked top-right.
    // Newest at the bottom of the stack so the eye lands on the latest
    // message. Older entries fade out as their lifeTime ticks down.
    if (!ctx.toasts.empty()) {
        ImVec2 vp = vpPos(), vs = vpSize();
        float toastW = 320.0f;
        float lineH  = 22.0f;
        float winH   = (float)ctx.toasts.size() * lineH + 12.0f;
        ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x - toastW - 16.0f, vp.y + 80.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(toastW, winH), ImGuiCond_Always);
        ImGui::Begin("##toasts", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
        for (auto& t : ctx.toasts) {
            float alpha = (t.lifeTime > 1.0f) ? 1.0f : t.lifeTime;
            ImU32 bg = IM_COL32(15, 10, 8, (int)(alpha * 200));
            ImU32 br = IM_COL32(t.color.r, t.color.g, t.color.b,
                                (int)(alpha * 220));
            ImU32 tx = IM_COL32(t.color.r, t.color.g, t.color.b,
                                (int)(alpha * 255));

            // Decorate via the draw list (doesn't touch ImGui layout).
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1(p0.x + toastW - 16.0f, p0.y + lineH - 2);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, p1, bg);
            dl->AddRect(p0, p1, br);
            dl->AddText(ImVec2(p0.x + 6, p0.y + 2), tx, t.text.c_str());

            // Reserve a layout row so ImGui knows about this toast and
            // grows the window's tracked content extent.
            ImGui::Dummy(ImVec2(toastW - 16.0f, lineH));
        }
        ImGui::End();
    }
}

// renderInventoryUI / renderCharacterLoadoutUI live in src/inventory_ui.cpp.
