#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "prop.h"

// One furniture/decoration instance the world wants to exist. The streamer
// turns nearby placements into live Prop objects.
struct PropPlacement {
    PropType  type;
    glm::vec3 pos;     // world-space, bottom-centre resting on the floor/ground
    float     yaw;     // degrees about +Y
    uint32_t  seed;    // per-instance seed (reserved for builder variation)
};

// Deterministic, built once from worldSeed() + the town plan (same lazy-once
// pattern as getTownPlan()). Lists every placement world-wide; the Object
// manager streams the nearby ones in and out by distance.
const std::vector<PropPlacement>& getPropPlacements();
