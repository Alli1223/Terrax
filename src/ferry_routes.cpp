#include "ferry_routes.h"
#include "town.h"
#include "world.h"
#include <mutex>
#include <memory>
#include <vector>
#include <iostream>

namespace {
const int JETTY_REACH = 9;   // matches DOCK_LEN in town.cpp — the jetty length

void buildFerryRoutes(std::vector<FerryRoute>& routes) {
    const TownPlan& plan = getTownPlan();
    for (const TownFerryLink& link : plan.ferryLinks) {
        const TownDock* da = nullptr;
        const TownDock* db = nullptr;
        for (const TownDock& d : plan.docks) {
            if (d.root == link.dockA) da = &d;
            if (d.root == link.dockB) db = &d;
        }
        if (!da || !db) continue;

        // The ferry meets each jetty at its tip (root + direction * length).
        glm::vec3 a((float)(da->root.x + da->dx * JETTY_REACH) + 0.5f,
                    (float)WORLD_SEA_LEVEL,
                    (float)(da->root.y + da->dz * JETTY_REACH) + 0.5f);
        glm::vec3 b((float)(db->root.x + db->dx * JETTY_REACH) + 0.5f,
                    (float)WORLD_SEA_LEVEL,
                    (float)(db->root.y + db->dz * JETTY_REACH) + 0.5f);

        bool dup = false;
        for (const FerryRoute& r : routes)
            if ((glm::distance(r.dockA, a) < 3.0f && glm::distance(r.dockB, b) < 3.0f) ||
                (glm::distance(r.dockA, b) < 3.0f && glm::distance(r.dockB, a) < 3.0f)) {
                dup = true; break;
            }
        if (dup) continue;

        FerryRoute fr;
        fr.dockA  = a;
        fr.dockB  = b;
        fr.length = glm::distance(a, b);
        fr.speed  = 6.0f;
        if (fr.length > 8.0f) routes.push_back(fr);
    }
    std::cout << "[Ferries] " << routes.size() << " ferry route(s)." << std::endl;
}
}  // namespace

// Rebuilt when the plan grows (spawn region -> full world), so far-flung water
// crossings gain ferries once the background fill reaches them. Routes are only
// ever appended to by the world, so a higher index always means a newer route.
const std::vector<FerryRoute>& getFerryRoutes() {
    static std::atomic<int> builtVer{-1};
    static std::mutex mtx;
    static std::vector<std::shared_ptr<std::vector<FerryRoute>>> kept;
    static std::atomic<const std::vector<FerryRoute>*> cur{nullptr};
    return rebuildOnPlanChange(builtVer, mtx, kept, cur, buildFerryRoutes);
}
