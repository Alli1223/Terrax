#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// Quests (Track E) — the data model.
// ---------------------------------------------------------------------------
// Quest givers in towns hand out deterministic, seed-derived quests that send
// the player out to a dungeon or wilderness region to kill enemies or gather
// loot. Generation is a pure function of the world seed + giver town, so the
// server and every client agree on the quest board with no networking (exactly
// like the town / dungeon plans). E2+ wires givers, progress tracking, the log
// UI and rewards on top of this model.

enum class QuestKind   : uint8_t { KillEnemies = 0, CollectItems = 1, SlayBoss = 2 };
enum class QuestStatus : uint8_t { Available = 0, Active = 1, Complete = 2, TurnedIn = 3 };

// A place a quest sends the player: a named dungeon or a wilderness region.
struct QuestTarget {
    glm::ivec2  anchor{0};      // world XZ the objective is anchored to
    std::string name;           // "Gloomreach Crypt" / "the Frostpine wilds"
    int         tier      = 1;  // danger tier at the anchor (drives difficulty)
    bool        isDungeon = false;
};

// One generated quest. Plain data so it serialises cleanly to clients later.
struct Quest {
    uint32_t    id = 0;                 // stable + deterministic per (town, slot)
    QuestKind   kind = QuestKind::KillEnemies;
    std::string title;
    std::string text;                   // flavour + the objective, one line

    // Objective ------------------------------------------------------------
    uint8_t     targetNpcType = 1;      // NPCType value (KillEnemies)
    std::string collectName;            // collectible label (CollectItems)
    int         requiredCount = 1;

    // Where the player is sent --------------------------------------------
    glm::ivec2  targetXZ{0};
    std::string targetName;
    int         targetTier       = 1;
    int         recommendedLevel = 1;

    // Rewards (granted on turn-in) ----------------------------------------
    int         rewardXp   = 0;
    int         rewardGold = 0;
    bool        rewardItem = false;     // rolls an item at recommendedLevel
    int         giverTownIndex = -1;

    // Runtime progress (set once a player accepts the quest) -------------
    int         progress = 0;           // objective count so far
    QuestStatus status   = QuestStatus::Available;
};

// True when a kill of `npcType` at `killTier` (the danger tier of the kill
// location) counts toward `q` — the foe type matches and the kill happened in
// the quest's region (within one danger tier of its target). Shared by the
// gameplay kill hook so the rule lives next to the data model.
inline bool questKillCounts(const Quest& q, uint8_t npcType, int killTier) {
    return q.status == QuestStatus::Active && q.kind == QuestKind::KillEnemies
        && q.targetNpcType == npcType
        && (killTier >= q.targetTier - 1) && (killTier <= q.targetTier + 1);
}

// True when a kill in the quest's region can yield a collectible toward `q` — a
// CollectItems quest whose region (danger tier band) contains the kill. The
// caller still rolls the per-kill drop chance.
inline bool questCollectCounts(const Quest& q, int killTier) {
    return q.status == QuestStatus::Active && q.kind == QuestKind::CollectItems
        && (killTier >= q.targetTier - 1) && (killTier <= q.targetTier + 1);
}

// True when killing a *boss* in the quest's region advances a SlayBoss quest.
// The caller checks the slain NPC's boss flag.
inline bool questBossKillCounts(const Quest& q, int killTier) {
    return q.status == QuestStatus::Active && q.kind == QuestKind::SlayBoss
        && (killTier >= q.targetTier - 1) && (killTier <= q.targetTier + 1);
}

// Pure, deterministic generator: build a giver town's quest board from its
// centre + seed + a set of candidate targets. No world-plan survey, no GL — so
// it is unit-testable headlessly. Returns 0 quests only if `targets` is empty.
std::vector<Quest> buildTownQuests(uint32_t seed, int townIndex,
                                   glm::ivec2 townCenter,
                                   const std::vector<QuestTarget>& targets);

// Gather candidate targets (the nearest dungeons + a wilderness region) from the
// world plans and build the town's quests. Lazy + cached per town, rebuilt when
// the world seed changes. Needs the town / dungeon plans (deterministic).
const std::vector<Quest>& getTownQuests(int townIndex);

// Human-readable label for a hostile NPCType used in quest text.
const char* questEnemyLabel(uint8_t npcType);
