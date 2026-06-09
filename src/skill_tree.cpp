#include "skill_tree.h"
#include "app_context.h"

// Per-role skill trees. Two pre-unlocked core nodes on row 0, point-bought
// nodes below. Kept as static tables (not call-site switches) so rebalancing or
// adding a node only touches this file.
const std::vector<SkillNode>& skillTreeFor(PlayerRole role) {
    static const std::vector<SkillNode> tank = {
        { AbilityId::Taunt,       1, 0, true,  {},                         0, 0, "Taunt",        "Force nearby enemies to attack you." },
        { AbilityId::ShieldUp,    1, 0, true,  {},                         1, 0, "Shield Wall",  "Sharply raise defence for a few seconds." },
        { AbilityId::ShieldBash,  3, 1, false, { AbilityId::Taunt },       0, 1, "Shield Bash",  "A heavy single-target strike." },
        { AbilityId::ThunderClap, 5, 1, false, { AbilityId::ShieldUp },    1, 1, "Thunder Clap", "Damage every enemy around you." },
        { AbilityId::BattleShout, 4, 1, false, { AbilityId::ShieldUp },    2, 1, "Battle Shout", "A war cry that raises your attack power." },
        { AbilityId::Slam,        6, 1, false, { AbilityId::ShieldBash },  0, 2, "Slam",         "Quake the ground in front of you." },
        { AbilityId::LastStand,   8, 2, false, { AbilityId::ThunderClap }, 1, 2, "Last Stand",   "Greatly raise defence to weather a storm." },
        { AbilityId::Earthshatter, 8, 2, false, { AbilityId::Slam },        0, 3, "Earthshatter", "A wide quake that batters everything around you." },
        { AbilityId::Bulwark,      9, 2, false, { AbilityId::LastStand },   1, 3, "Bulwark",      "Massively raise defence — an unbreakable wall." },
        { AbilityId::Avatar,      10, 2, false, { AbilityId::BattleShout }, 2, 3, "Avatar",       "Become an avatar of war — huge attack power." },
    };
    static const std::vector<SkillNode> healer = {
        { AbilityId::ChainHeal,        1, 0, true,  {},                              0, 0, "Chain Heal",        "A heal that leaps to nearby allies." },
        { AbilityId::HealingSanctuary, 1, 0, true,  {},                              1, 0, "Healing Sanctuary", "An aimed AoE heal-over-time zone." },
        { AbilityId::Smite,            3, 1, false, { AbilityId::ChainHeal },         0, 1, "Smite",             "A holy bolt that damages an enemy." },
        { AbilityId::GreaterHeal,      5, 1, false, { AbilityId::ChainHeal },         1, 1, "Greater Heal",      "A much stronger chain heal." },
        { AbilityId::Renew,            2, 1, false, {},                              2, 1, "Renew",             "A quick heal-over-time around you." },
        { AbilityId::HolyNova,         6, 1, false, { AbilityId::Smite },             0, 2, "Holy Nova",         "Burn nearby foes and mend allies." },
        { AbilityId::Barrier,          4, 1, false, { AbilityId::HealingSanctuary },  1, 2, "Barrier",           "Absorb incoming harm for a while." },
        { AbilityId::DivineStorm,      8, 2, false, { AbilityId::HolyNova },          0, 3, "Divine Storm",      "A holy storm that burns foes and heals allies." },
        { AbilityId::GuardianSpirit,   9, 2, false, { AbilityId::Barrier },           1, 3, "Guardian Spirit",   "A spirit that shields you from grievous harm." },
        { AbilityId::Tranquility,     10, 2, false, { AbilityId::GreaterHeal },       2, 3, "Tranquility",       "Bathe a wide area in powerful healing light." },
    };
    static const std::vector<SkillNode> dps = {
        { AbilityId::Flurry,       1, 0, true,  {},                      0, 0, "Flurry",        "Three rapid melee strikes." },
        { AbilityId::Cleave,       1, 0, true,  {},                      1, 0, "Cleave",        "Damage the enemies in front of you." },
        { AbilityId::Fireball,     2, 1, false, {},                      2, 0, "Fireball",      "A ranged bolt that bursts on impact." },
        { AbilityId::Frostbolt,    3, 1, false, {},                      3, 0, "Frostbolt",     "A ranged ice shard that chills." },
        { AbilityId::Rend,         3, 1, false, { AbilityId::Flurry },   0, 1, "Rend",          "A vicious strike against one foe." },
        { AbilityId::Whirlwind,    4, 1, false, { AbilityId::Cleave },   1, 1, "Whirlwind",     "Hit everything around you." },
        { AbilityId::PiercingShot, 6, 1, false, { AbilityId::Fireball }, 2, 1, "Piercing Shot", "A bolt that pierces through enemies." },
        { AbilityId::Inferno,      7, 2, false, { AbilityId::Fireball }, 3, 1, "Inferno",       "Engulf the ground ahead in flame." },
        { AbilityId::Execute,      8, 2, false, { AbilityId::Rend },     0, 2, "Execute",       "A brutal finishing blow with devastating force." },
        { AbilityId::GlacialSpike, 9, 2, false, { AbilityId::Frostbolt },2, 2, "Glacial Spike", "A massive ice spike that shatters on impact." },
        { AbilityId::Meteor,      10, 2, false, { AbilityId::Inferno },  3, 2, "Meteor",        "Call a blazing meteor onto the ground you aim at." },
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
