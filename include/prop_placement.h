#pragma once
#include <vector>
#include <unordered_set>
#include <cstdint>
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

// One openable door the world wants to exist (one per house front door).
struct DoorPlacement {
    glm::vec3  hinge;     // world-space hinge point (door's x=0 edge, at the floor)
    float      closedYaw; // degrees about +Y — the panel's shut orientation
    glm::ivec2 wallCell;  // doorway centre cell (world XZ)
    glm::ivec2 wallDir;   // unit step along the wall (door spans +-1 of this)
    int        variant;   // door style/colour index
};

// Deterministic list of every house door world-wide; streamed like props.
const std::vector<DoorPlacement>& getDoorPlacements();

// A procedurally-scattered wild prop (bushes) with a stable per-cell key, so the
// streamer can spawn and retire instances incrementally as the player explores.
struct WildProp {
    uint64_t      key;
    PropPlacement p;
};

// Fills `out` with the wild bushes whose position lies within `radius` of
// `center`, skipping any whose key is already in `live` (so the expensive
// surface/biome sampling only runs for newly-revealed bushes). Fully
// deterministic from the world seed — every client scatters the same bushes.
void gatherWildProps(const glm::vec3& center, float radius,
                     const std::unordered_set<uint64_t>& live,
                     std::vector<WildProp>& out);
