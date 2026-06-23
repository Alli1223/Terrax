#include "quest.h"
#include "npc.h"        // NPCType, enemyLevelForTier
#include "world.h"      // worldSeed, dangerTierAt, rebuildCacheOnSeedChange
#include "town.h"       // getTownPlan
#include "dungeon.h"    // getDungeonPlan

#include <random>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cmath>

namespace {

uint32_t hashU32(uint32_t a, uint32_t b) {
    uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

// Hostile NPCType values eligible for kill quests (see npc.h).
const uint8_t HOSTILES[] = { (uint8_t)NPCType::Enemy, (uint8_t)NPCType::Skeleton,
                             (uint8_t)NPCType::Brute, (uint8_t)NPCType::Cultist,
                             (uint8_t)NPCType::Zombie, (uint8_t)NPCType::Knight,
                             (uint8_t)NPCType::Necromancer, (uint8_t)NPCType::Ghoul };

const char* COLLECTIBLES[] = {
    "Wolf Pelt", "Bandit Insignia", "Cracked Rune", "Ancient Coin",
    "Tattered Map Fragment", "Glowing Ember", "Bone Charm", "Faded Relic",
};

}  // namespace

const char* questEnemyLabel(uint8_t npcType) {
    switch ((NPCType)npcType) {
        case NPCType::Enemy:    return "Bandit";
        case NPCType::Skeleton: return "Skeleton";
        case NPCType::Brute:    return "Ogre Brute";
        case NPCType::Cultist:  return "Cultist";
        case NPCType::Zombie:      return "Zombie";
        case NPCType::Knight:      return "Fallen Knight";
        case NPCType::Necromancer: return "Necromancer";
        case NPCType::Ghoul:       return "Ghoul";
        default:                   return "Foe";
    }
}

std::vector<Quest> buildTownQuests(uint32_t seed, int townIndex,
                                   glm::ivec2 townCenter,
                                   const std::vector<QuestTarget>& targets) {
    std::vector<Quest> out;
    if (targets.empty()) return out;
    (void)townCenter;

    std::mt19937 rng(seed ^ 0x51E57A11u);
    auto pick = [&](int n) { return n > 0 ? (int)(rng() % (uint32_t)n) : 0; };

    int slots = 3 + pick(2);   // 3–4 quests on the board
    for (int s = 0; s < slots; ++s) {
        const QuestTarget& tgt = targets[pick((int)targets.size())];
        Quest q;
        q.id              = hashU32(seed ^ (uint32_t)(townIndex << 8), (uint32_t)s * 40503u + 1u);
        q.giverTownIndex  = townIndex;
        q.targetXZ        = tgt.anchor;
        q.targetName      = tgt.name;
        q.targetTier      = tgt.tier;
        q.recommendedLevel = enemyLevelForTier(tgt.tier);

        // Dungeons lean toward slaying their denizens; the wilds split kill/collect.
        bool kill = tgt.isDungeon ? (pick(4) != 0) : (((s + pick(2)) & 1) == 0);

        if (kill) {
            q.kind          = QuestKind::KillEnemies;
            q.targetNpcType = HOSTILES[pick((int)(sizeof(HOSTILES) / sizeof(HOSTILES[0])))];
            q.requiredCount = 6 + tgt.tier * 2 + pick(5);
            const char* foe = questEnemyLabel(q.targetNpcType);
            q.title = std::string("Cull the ") + foe + "s";
            q.text  = "Slay " + std::to_string(q.requiredCount) + " " + foe
                    + (q.requiredCount > 1 ? "s in " : " in ") + tgt.name + ".";
        } else {
            q.kind          = QuestKind::CollectItems;
            q.collectName   = COLLECTIBLES[pick((int)(sizeof(COLLECTIBLES) / sizeof(COLLECTIBLES[0])))];
            q.requiredCount = 4 + tgt.tier + pick(4);
            q.title = std::string("Gather ") + q.collectName + "s";
            q.text  = "Recover " + std::to_string(q.requiredCount) + " " + q.collectName
                    + (q.requiredCount > 1 ? "s from " : " from ") + tgt.name + ".";
        }

        // Rewards scale with the recommended level + objective size.
        q.rewardXp   = (15 + 5 * q.recommendedLevel) * std::max(1, q.requiredCount / 2);
        q.rewardGold = 10 * tgt.tier + q.requiredCount + pick(20);
        q.rewardItem = tgt.isDungeon || pick(100) < 45;

        out.push_back(std::move(q));
    }
    return out;
}

const std::vector<Quest>& getTownQuests(int townIndex) {
    static std::mutex mtx;
    static std::atomic<uint64_t> builtSeed{~0ull};
    static std::unordered_map<int, std::vector<Quest>> cache;
    static const std::vector<Quest> empty;

    std::lock_guard<std::mutex> lock(mtx);
    if (builtSeed.load() != worldSeed()) { cache.clear(); builtSeed.store(worldSeed()); }
    auto it = cache.find(townIndex);
    if (it != cache.end()) return it->second;

    const TownPlan& tp = getTownPlan();
    if (townIndex < 0 || townIndex >= (int)tp.towns.size()) return empty;
    const Town& town = tp.towns[townIndex];
    glm::ivec2 center = town.center;

    // Candidate targets: the nearest few dungeons + one wilderness region pushed
    // outward (away from spawn) so the quest sends the player further afield.
    std::vector<QuestTarget> cands;
    const DungeonPlan& dp = getDungeonPlan();
    std::vector<std::pair<long long, int>> byDist;
    byDist.reserve(dp.dungeons.size());
    for (size_t i = 0; i < dp.dungeons.size(); ++i) {
        glm::ivec2 a = dp.dungeons[i]->anchor;
        long long dx = a.x - center.x, dz = a.y - center.y;
        byDist.push_back({ dx * dx + dz * dz, (int)i });
    }
    std::sort(byDist.begin(), byDist.end());
    int take = (int)std::min<size_t>(3, byDist.size());
    for (int i = 0; i < take; ++i) {
        const Dungeon& d = *dp.dungeons[byDist[i].second];
        QuestTarget t;
        t.anchor    = d.anchor;
        t.name      = d.name.empty() ? "the dungeon" : d.name;
        t.tier      = dangerTierAt((float)d.anchor.x, (float)d.anchor.y);
        t.isDungeon = true;
        cands.push_back(std::move(t));
    }
    {
        glm::vec2 dir((float)center.x, (float)center.y);
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 1.0f) { dir = glm::vec2(1.0f, 0.0f); len = 1.0f; }
        glm::ivec2 wild = center + glm::ivec2((int)(dir.x / len * 1800.0f),
                                              (int)(dir.y / len * 1800.0f));
        QuestTarget t;
        t.anchor    = wild;
        t.name      = town.name.empty() ? "the surrounding wilds"
                                        : ("the wilds beyond " + town.name);
        t.tier      = dangerTierAt((float)wild.x, (float)wild.y);
        t.isDungeon = false;
        cands.push_back(std::move(t));
    }

    cache[townIndex] = buildTownQuests(worldSeed() ^ (uint32_t)townIndex,
                                       townIndex, center, cands);
    return cache[townIndex];
}
