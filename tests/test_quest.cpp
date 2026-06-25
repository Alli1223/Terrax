// Headless tests for the quest DATA MODEL (pure generator — no town survey, no
// GL). Drives buildTownQuests directly with synthetic targets and asserts the
// invariants the generator promises: a non-empty board, determinism, valid
// objectives anchored on real targets, level/reward scaling, and a kill/collect
// mix across the candidate set.
#include "terrax_test.h"
#include "quest.h"
#include "npc.h"     // enemyLevelForTier

#include <vector>

static std::vector<QuestTarget> sampleTargets() {
    std::vector<QuestTarget> t;
    t.push_back({ glm::ivec2(1200, -400),  "Gloomreach Crypt", 2, true  });
    t.push_back({ glm::ivec2(-5000, 8000), "Frostpine Caverns", 6, true  });
    t.push_back({ glm::ivec2(800, 800),    "the surrounding wilds", 1, false });
    return t;
}

TEST_CASE(Quest_BoardIsNonEmptyAndAnchored) {
    auto targets = sampleTargets();
    auto board = buildTownQuests(0xC0FFEEu, 3, glm::ivec2(500, 500), targets);
    CHECK(board.size() >= 3);
    for (const Quest& q : board) {
        CHECK(q.requiredCount > 0);
        CHECK(q.rewardXp > 0);
        CHECK(q.giverTownIndex == 3);
        CHECK(q.recommendedLevel >= 1 && q.recommendedLevel <= 60);
        // The recommended level follows the target tier exactly.
        CHECK_EQ(q.recommendedLevel, enemyLevelForTier(q.targetTier));
        // Every quest is anchored on one of the supplied candidate targets.
        bool anchored = false;
        for (const QuestTarget& t : targets)
            if (t.anchor == q.targetXZ && t.tier == q.targetTier) anchored = true;
        CHECK(anchored);
        // Objective fields match the kind.
        if (q.kind == QuestKind::KillEnemies)        CHECK(q.targetNpcType != 0);
        else if (q.kind == QuestKind::CollectItems)  CHECK(!q.collectName.empty());
        else if (q.kind == QuestKind::SlayBoss)      CHECK_EQ(q.requiredCount, 1);
        else if (q.kind == QuestKind::Explore)       CHECK_EQ(q.requiredCount, 1);
        CHECK(!q.title.empty());
        CHECK(!q.text.empty());
    }
}

TEST_CASE(Quest_GenerationIsDeterministic) {
    auto targets = sampleTargets();
    auto a = buildTownQuests(12345u, 7, glm::ivec2(0, 0), targets);
    auto b = buildTownQuests(12345u, 7, glm::ivec2(0, 0), targets);
    CHECK_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        CHECK_EQ(a[i].id, b[i].id);
        CHECK_EQ((int)a[i].kind, (int)b[i].kind);
        CHECK_EQ(a[i].requiredCount, b[i].requiredCount);
        CHECK_EQ(a[i].rewardXp, b[i].rewardXp);
        CHECK(a[i].title == b[i].title);
    }
    // A different seed should generally produce a different board.
    auto c = buildTownQuests(999u, 7, glm::ivec2(0, 0), targets);
    bool differs = (c.size() != a.size());
    for (size_t i = 0; i < a.size() && i < c.size() && !differs; ++i)
        if (c[i].id != a[i].id) differs = true;
    CHECK(differs);
}

TEST_CASE(Quest_HigherTierTargetGivesHigherRecommendedLevel) {
    // A board built only against a high-tier target out-levels a low-tier one.
    std::vector<QuestTarget> low  = { { glm::ivec2(0,0), "Low Caves",  1, true } };
    std::vector<QuestTarget> high = { { glm::ivec2(0,0), "High Keep", 10, true } };
    auto lq = buildTownQuests(42u, 0, glm::ivec2(0, 0), low);
    auto hq = buildTownQuests(42u, 0, glm::ivec2(0, 0), high);
    CHECK(!lq.empty());
    CHECK(!hq.empty());
    CHECK(hq.front().recommendedLevel > lq.front().recommendedLevel);
}

TEST_CASE(Quest_EmptyTargetsYieldsNoQuests) {
    auto board = buildTownQuests(1u, 0, glm::ivec2(0, 0), {});
    CHECK(board.empty());
}

TEST_CASE(Quest_KillCountingRule) {
    Quest q;
    q.kind = QuestKind::KillEnemies;
    q.targetNpcType = 4;     // e.g. Skeleton
    q.targetTier = 5;
    q.status = QuestStatus::Active;

    // Right foe, in-region (within one tier band) → counts.
    CHECK(questKillCounts(q, 4, 5));
    CHECK(questKillCounts(q, 4, 4));
    CHECK(questKillCounts(q, 4, 6));
    // Wrong foe type → no.
    CHECK(!questKillCounts(q, 1, 5));
    // Out of region (tier too far) → no.
    CHECK(!questKillCounts(q, 4, 2));
    CHECK(!questKillCounts(q, 4, 8));
    // Not active (still available / already complete) → no.
    q.status = QuestStatus::Available; CHECK(!questKillCounts(q, 4, 5));
    q.status = QuestStatus::Complete;  CHECK(!questKillCounts(q, 4, 5));
    // Collection quests never count kills.
    q.status = QuestStatus::Active; q.kind = QuestKind::CollectItems;
    CHECK(!questKillCounts(q, 4, 5));
}

TEST_CASE(Quest_CollectCountingRule) {
    Quest q;
    q.kind = QuestKind::CollectItems;
    q.targetTier = 3;
    q.status = QuestStatus::Active;
    // In-region kills can yield the collectible (foe type irrelevant for collect).
    CHECK(questCollectCounts(q, 3));
    CHECK(questCollectCounts(q, 2));
    CHECK(questCollectCounts(q, 4));
    CHECK(!questCollectCounts(q, 6));     // out of region
    q.status = QuestStatus::Complete; CHECK(!questCollectCounts(q, 3));
    // Kill quests are never collect-credited.
    q.status = QuestStatus::Active; q.kind = QuestKind::KillEnemies;
    CHECK(!questCollectCounts(q, 3));
}

TEST_CASE(Quest_BossKillCountingRule) {
    Quest q;
    q.kind = QuestKind::SlayBoss;
    q.targetTier = 4;
    q.status = QuestStatus::Active;
    CHECK(questBossKillCounts(q, 4));
    CHECK(questBossKillCounts(q, 5));
    CHECK(!questBossKillCounts(q, 1));      // out of region
    q.status = QuestStatus::Complete; CHECK(!questBossKillCounts(q, 4));
    // Non-boss quest kinds are never boss-credited.
    q.status = QuestStatus::Active; q.kind = QuestKind::KillEnemies;
    CHECK(!questBossKillCounts(q, 4));
}

TEST_CASE(Quest_ExploreReachedRule) {
    Quest q;
    q.kind = QuestKind::Explore;
    q.targetXZ = glm::ivec2(1000, -500);
    q.status = QuestStatus::Active;
    CHECK(questExploreReached(q, 1000.0f, -500.0f));        // dead on the target
    CHECK(questExploreReached(q, 1030.0f, -480.0f));        // within the scout radius
    CHECK(!questExploreReached(q, 1300.0f, -500.0f));       // too far away
    q.status = QuestStatus::Complete;
    CHECK(!questExploreReached(q, 1000.0f, -500.0f));       // already done
    // Other kinds are never explore-credited.
    q.status = QuestStatus::Active; q.kind = QuestKind::KillEnemies;
    CHECK(!questExploreReached(q, 1000.0f, -500.0f));
}
