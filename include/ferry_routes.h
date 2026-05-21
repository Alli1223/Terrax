#pragma once
#include <vector>
#include <glm/glm.hpp>

// A ferry crossing: two jetty-tip endpoints separated by wide open water.
// Derived deterministically from the town plan's recorded ferry links.
struct FerryRoute {
    glm::vec3 dockA, dockB;   // world-space jetty tips
    float     length;        // |dockB - dockA|
    float     speed;         // blocks per second
};

// Lazily built (once) from getTownPlan(). Used by the server to simulate the
// ferries; clients never need it (they render ferries from network packets).
const std::vector<FerryRoute>& getFerryRoutes();
