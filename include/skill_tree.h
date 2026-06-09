#pragma once
#include "ability.h"
#include "role.h"
#include <vector>

struct AppContext;

// ---------------------------------------------------------------------------
// Skill tree — what each role can unlock as it levels up.
// ---------------------------------------------------------------------------
// A node grants one ability. `core` nodes are pre-unlocked at character creation
// (no cost). The rest become available at `requiredLevel`, cost `cost` skill
// points, and require all `prereqs` to be unlocked first. gridX/gridY lay the
// node out in the overlay. The per-role tables in skill_tree.cpp are append-only
// so adding a node never renumbers an existing ability id.
struct SkillNode {
    AbilityId   ability;
    int         requiredLevel;
    int         cost;
    bool        core;
    std::vector<AbilityId> prereqs;
    int         gridX, gridY;
    const char* name;
    const char* desc;
};

const std::vector<SkillNode>& skillTreeFor(PlayerRole role);

// True when `node` can be unlocked right now (not core/already owned, level +
// points met, prereqs unlocked).
bool canUnlockSkill(const AppContext& ctx, const SkillNode& node);

// Spend the points and grant the ability (adds it to the book + first free
// hotbar slot). Returns false if it wasn't unlockable.
bool unlockSkill(AppContext& ctx, const SkillNode& node);
