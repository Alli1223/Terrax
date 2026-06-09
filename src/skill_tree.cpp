#include "skill_tree.h"
#include "app_context.h"

// Per-role skill trees. Two pre-unlocked core nodes on row 0, point-bought
// nodes below. Kept as static tables (not call-site switches) so rebalancing or
// adding a node only touches this file.
const std::vector<SkillNode>& skillTreeFor(PlayerRole role) {
    static const std::vector<SkillNode> tank = {
        { AbilityId::Taunt,       1, 0, true,  {},                      0, 0, "Taunt",        "Force nearby enemies to attack you." },
        { AbilityId::ShieldUp,    1, 0, true,  {},                      1, 0, "Shield Wall",  "Sharply raise defence for a few seconds." },
        { AbilityId::ShieldBash,  3, 1, false, { AbilityId::Taunt },    0, 1, "Shield Bash",  "A heavy single-target strike." },
        { AbilityId::ThunderClap, 5, 1, false, { AbilityId::ShieldUp }, 1, 1, "Thunder Clap", "Damage every enemy around you." },
    };
    static const std::vector<SkillNode> healer = {
        { AbilityId::ChainHeal,        1, 0, true,  {},                       0, 0, "Chain Heal",        "A heal that leaps to nearby allies." },
        { AbilityId::HealingSanctuary, 1, 0, true,  {},                       1, 0, "Healing Sanctuary", "An aimed AoE heal-over-time zone." },
        { AbilityId::Smite,            3, 1, false, { AbilityId::ChainHeal },  0, 1, "Smite",             "A holy bolt that damages an enemy." },
        { AbilityId::GreaterHeal,      5, 1, false, { AbilityId::ChainHeal },  1, 1, "Greater Heal",      "A much stronger chain heal." },
    };
    static const std::vector<SkillNode> dps = {
        { AbilityId::Flurry,       1, 0, true,  {},                      0, 0, "Flurry",        "Three rapid melee strikes." },
        { AbilityId::Cleave,       1, 0, true,  {},                      1, 0, "Cleave",        "Damage the enemies in front of you." },
        { AbilityId::Fireball,     2, 1, false, {},                      2, 0, "Fireball",      "A ranged bolt that bursts on impact." },
        { AbilityId::Whirlwind,    4, 1, false, { AbilityId::Cleave },   1, 1, "Whirlwind",     "Hit everything around you." },
        { AbilityId::PiercingShot, 6, 1, false, { AbilityId::Fireball }, 2, 1, "Piercing Shot", "A bolt that pierces through enemies." },
    };
    switch (role) {
        case PlayerRole::Tank:   return tank;
        case PlayerRole::Healer: return healer;
        case PlayerRole::DPS:
        default:                 return dps;
    }
}

bool canUnlockSkill(const AppContext& ctx, const SkillNode& node) {
    if (node.core) return false;                                   // already free
    if (ctx.unlockedAbilities.count(node.ability)) return false;   // already owned
    if (ctx.playerLevel < node.requiredLevel)      return false;
    if (ctx.skillPoints < node.cost)               return false;
    for (AbilityId pre : node.prereqs)
        if (!ctx.unlockedAbilities.count(pre)) return false;       // prereq missing
    return true;
}

bool unlockSkill(AppContext& ctx, const SkillNode& node) {
    if (!canUnlockSkill(ctx, node)) return false;
    ctx.skillPoints -= node.cost;
    ctx.unlockedAbilities.insert(node.ability);
    ctx.grantAbility(node.ability);
    for (int i = 0; i < AppContext::HOTBAR_SLOTS; i++)
        if (ctx.hotbar[i] == AbilityId::None) { ctx.hotbar[i] = node.ability; break; }
    return true;
}
