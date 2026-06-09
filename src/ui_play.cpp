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

// F3 debug / session overlay — performance, world, rendered objects, server.
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
        ctx.paused || ctx.chatOpen) return false;
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
        std::string tag(ab->name());
        if (tag.size() > 5) tag = tag.substr(0, 5);
        ImVec2 ts = ImGui::CalcTextSize(tag.c_str());
        dl->AddText(ImVec2(x + (slot - ts.x) * 0.5f, y0 + slot * 0.5f - 4),
                    IM_COL32(235, 235, 245, 255), tag.c_str());
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

void renderPlayUI(AppContext& ctx, GLFWwindow* window, const Renderer& renderer) {
    (void)window;

    if (ctx.showDebugOverlay) renderDebugOverlay(ctx);

    if (shouldShowCrosshair(ctx)) drawCrosshair();
    if (!ctx.showMap) drawHotbar(ctx);

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
        ImGui::SetNextWindowPos(ImVec2(vp.x + vs.x * 0.5f - 220.0f, vp.y + vs.y - 172.0f));
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

    // NPC health bars over damaged NPCs.
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        NPC* n = static_cast<NPC*>(o.get());
        if (n->dyingFlag) continue;
        float maxHp = defaultNpcHealth(n->type);          // per-type max (Skeleton 60, Brute 220, ...)
        float frac  = (maxHp > 0.0f) ? (n->health / maxHp) : 1.0f;
        if (frac >= 0.995f) continue;                     // hide the bar at full health
        drawHealthBar(n->position + glm::vec3(0.0f, 2.3f, 0.0f), frac,
                      renderer.frameView, renderer.frameProj,
                      renderer.frameFbW, renderer.frameFbH);
    }

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
