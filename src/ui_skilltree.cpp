// In-game skill-tree overlay (K key). Lays the role's tree out on a small grid,
// draws prereq edges, and lets the player spend skill points to learn nodes.
// Follows the inventory overlay pattern: a boolean-toggled window, no new
// GameState. All the unlock rules live in skill_tree.cpp.
#include "ui.h"
#include "app_context.h"
#include "skill_tree.h"
#include "imgui.h"
#include <string>
#include "ui_internal.h"

void renderSkillTreeUI(AppContext& ctx) {
    if (!ctx.showSkillTree) return;

    ImVec2 vp = vpPos(), vs = vpSize();
    const float W = 720.0f, H = 624.0f;   // four rows of nodes (core + three tiers)
    ImGui::SetNextWindowPos(ImVec2(vp.x + (vs.x - W) * 0.5f, vp.y + (vs.y - H) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Skill Tree", &ctx.showSkillTree,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    ImGui::Text("%s", roleName(ctx.playerRole));
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f),
                       "    Skill Points: %d", ctx.skillPoints);
    ImGui::Separator();
    ImGui::Spacing();

    const std::vector<SkillNode>& tree = skillTreeFor(ctx.playerRole);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin  = ImGui::GetCursorScreenPos();
    const float cellW = 168.0f, cellH = 96.0f, nodeW = 150.0f, nodeH = 78.0f;

    auto nodeCenter = [&](const SkillNode& n) {
        return ImVec2(origin.x + n.gridX * cellW + nodeW * 0.5f,
                      origin.y + n.gridY * cellH + nodeH * 0.5f);
    };

    // Prereq edges first, beneath the node cards.
    for (const SkillNode& n : tree)
        for (AbilityId pre : n.prereqs)
            for (const SkillNode& m : tree)
                if (m.ability == pre)
                    dl->AddLine(nodeCenter(m), nodeCenter(n), IM_COL32(150, 150, 165, 170), 2.0f);

    for (const SkillNode& n : tree) {
        ImVec2 a(origin.x + n.gridX * cellW, origin.y + n.gridY * cellH);
        ImVec2 b(a.x + nodeW, a.y + nodeH);
        bool owned = ctx.unlockedAbilities.count(n.ability) > 0;
        bool can   = canUnlockSkill(ctx, n);

        ImU32 bg     = owned ? IM_COL32(38, 78, 50, 235)
                     : can   ? IM_COL32(58, 58, 92, 235)
                             : IM_COL32(34, 34, 42, 235);
        ImU32 border = owned ? IM_COL32(120, 230, 150, 255)
                     : can   ? IM_COL32(255, 210, 120, 255)
                             : IM_COL32(90, 90, 102, 255);
        dl->AddRectFilled(a, b, bg, 5.0f);
        dl->AddRect(a, b, border, 5.0f, 0, 2.0f);

        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(&n);
        if (ImGui::InvisibleButton("node", ImVec2(nodeW, nodeH)) && can)
            unlockSkill(ctx, n);
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        // Procedural ability glyph at the card's upper-left. createAbility() is a
        // cheap throwaway used only to read the icon — the tree is drawn only
        // while the overlay is open, so the per-node allocation is negligible.
        if (auto tmp = createAbility(n.ability))
            drawAbilityIcon(dl, ImVec2(a.x + 22, a.y + 23), 30.0f,
                            tmp->icon(), tmp->iconColor());
        dl->AddText(ImVec2(a.x + 44, a.y + 9), IM_COL32(240, 240, 245, 255), n.name);
        std::string line;
        if (owned)                                  line = "Unlocked";
        else if (n.core)                            line = "Core";
        else if (ctx.playerLevel < n.requiredLevel) line = "Requires Lv " + std::to_string(n.requiredLevel);
        else if (ctx.skillPoints < n.cost)          line = std::to_string(n.cost) + " pt (need more)";
        else                                        line = "Learn (" + std::to_string(n.cost) + " pt)";
        dl->AddText(ImVec2(a.x + 7, a.y + nodeH - 19), border, line.c_str());

        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", n.name);
            ImGui::TextWrapped("%s", n.desc);
            ImGui::EndTooltip();
        }
    }

    // Drop the cursor below the grid so the footer sits under it (four rows of
    // nodes now that each role has a capstone tier).
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + cellH * 4.0f + 12.0f));
    ImGui::Separator();
    ImGui::TextDisabled("Press K to close. Highlighted nodes can be learned now.");
    ImGui::End();
}
