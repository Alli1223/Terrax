#include "ferry_routes.h"
#include "town.h"
#include "world.h"
#include <mutex>
#include <iostream>

namespace {
const int JETTY_REACH = 9;   // matches DOCK_LEN in town.cpp — the jetty length
}

const std::vector<FerryRoute>& getFerryRoutes() {
    static std::vector<FerryRoute> routes;
    static std::once_flag once;
    std::call_once(once, [] {
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
    });
    return routes;
}
