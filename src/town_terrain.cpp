// Terrain-oracle hooks that flatten land under a settlement — split out of
// town.cpp. townFlattenedHeight blends a raw surface height toward nearby town
// base levels; townFlatLevelAt hard-snaps a column inside a town's footprint.
// Both are no-ops until the plan has finished building. See town_internal.h.
#include "town_internal.h"
#include <cmath>

using namespace townint;

// Per-direction effective flat-zone radius for a town. Adds a three-octave
// sinusoidal noise on the angle from the town centre so the otherwise-perfect
// circular boundary becomes a lobed blob, and the surrounding smooth blend
// inherits the same irregular shape. Much harder to spot the "bit that got
// flattened" as a clean circle from the air. Each town gets its own noise
// phase so adjacent towns don't share lobe patterns.
namespace townint {

float townEffectiveFlatR(const Town& t, float dx, float dz) {
    float baseR = (float)t.radius + 52.0f;
    if (std::abs(dx) < 0.5f && std::abs(dz) < 0.5f) return baseR;
    float angle = std::atan2(dz, dx);
    uint32_t h = (uint32_t)t.center.x * 0xA24BAED4u
               ^ (uint32_t)t.center.y * 0xCC9E2D51u
               ^ 0xB10BB10Bu;
    float phase = ((float)(h & 0xFFFF) / 65535.0f) * 6.2831853f;
    float n = 0.55f * std::sin(angle * 2.0f  + phase)
            + 0.30f * std::sin(angle * 5.0f  + phase * 1.7f)
            + 0.15f * std::sin(angle * 11.0f + phase * 2.3f);
    // Amplitude scales with town size — bigger towns get larger lobes so the
    // boundary variance reads as proportionate, not as fixed wobble.
    float amp = std::min(28.0f, (float)t.radius * 0.30f + 12.0f);
    return baseR + n * amp;
}

// Gentle low-frequency tilt across one town: the town gets a random direction
// vector (derived from its centre) and the surface rises by +1 on one side
// and falls by -1 on the other. Returns one of {-1, 0, +1}. The tilt is the
// same every frame for a given town, so neighbouring chunks agree on the
// terrace shape and a path crosses a contour exactly once.
int townSlopeOffset(const Town& t, int wx, int wz) {
    uint32_t h = (uint32_t)t.center.x * 0x9E3779B1u
               ^ (uint32_t)t.center.y * 0x85EBCA77u
               ^ 0xC0FFEEu;
    float theta = ((float)(h & 0xFFFF) / 65535.0f) * 6.2831853f;
    float dx    = (float)(wx - t.center.x);
    float dz    = (float)(wz - t.center.y);
    // Slope scale: one full step of ±1 reached around the town radius — so
    // a small village has a slightly steeper tilt than a large town, but both
    // top out at ±1 block across the settled zone.
    float r  = (float)t.radius + 20.0f;
    float tt = (dx * std::cos(theta) + dz * std::sin(theta)) / r;
    if (tt > 1.0f) tt = 1.0f; else if (tt < -1.0f) tt = -1.0f;
    return (int)std::round(tt);
}

}  // namespace townint

// Hard flat-zone level: inside any town's settled radius the chunk generator
// snaps the column to this Y so the terrain is exactly flat (paths and doors
// then agree perfectly). Two behaviours are stacked:
//
//   - Cells inside (or within a small buffer of) a building footprint return
//     that building's own baseY, giving every house a strictly flat pad even
//     when the rest of the town is on a slight slope.
//   - Cells outside all building footprints return t.baseY plus a gentle ±1
//     low-frequency tilt (see townSlopeOffset), so the town no longer looks
//     uniformly flat. Paths follow the tilt and step ±1 across the contour
//     lines, but each door still meets the path at exactly the door's level
//     because the immediate building neighbourhood is held flat.
int townFlatLevelAt(int wx, int wz) {
    if (!g_townReady.load(std::memory_order_acquire)) return -1;
    const TownPlan& plan = getTownPlan();
    const Town* bestT = nullptr;
    float bestD2 = 1e30f;
    for (const Town& t : plan.towns) {
        float dx = (float)(wx - t.center.x);
        float dz = (float)(wz - t.center.y);
        float d2 = dx * dx + dz * dz;
        // Noisy boundary so the hard-flat zone isn't a perfect circle. The
        // same noise drives the smooth-blend ring below, so the chunk-gen
        // snap and the surface oracle agree on the shape.
        float flatR = townEffectiveFlatR(t, dx, dz);
        if (d2 >= flatR * flatR) continue;
        if (d2 < bestD2) { bestD2 = d2; bestT = &t; }
    }
    if (!bestT) return -1;

    // Building flat pad — extends a couple of cells past the footprint so
    // the path landing on the door is also forced to the door's level.
    const int BUF = 2;
    for (const TownBuilding& b : bestT->buildings) {
        if (wx < b.wx - BUF) continue;
        if (wx >= b.wx + b.dimX + BUF) continue;
        if (wz < b.wz - BUF) continue;
        if (wz >= b.wz + b.dimZ + BUF) continue;
        return b.baseY;
    }

    // Outside any building: the town's gentle tilt shows through.
    return bestT->baseY + townSlopeOffset(*bestT, wx, wz);
}

// Blends a raw surface height toward the base level of any nearby town, so
// settlements sit on relatively flat ground. A no-op until the plan is ready,
// which keeps the survey working on the natural, unflattened terrain.
float townFlattenedHeight(float wx, float wz, float rawHeight) {
    if (!g_townReady.load(std::memory_order_acquire)) return rawHeight;
    const TownPlan& plan = getTownPlan();
    float h = rawHeight;
    for (const Town& t : plan.towns) {
        float dx = wx - (float)t.center.x;
        float dz = wz - (float)t.center.y;
        // Match the noisy boundary the chunk generator uses for its hard
        // snap, then ease the influence back to natural over a generous
        // 80-block ring so the perimeter never reads as a clean edge.
        float flatR  = townEffectiveFlatR(t, dx, dz);
        float blendR = flatR + 80.0f;
        float d2 = dx * dx + dz * dz;
        if (d2 >= blendR * blendR) continue;
        float dist = std::sqrt(d2);
        float w;
        if (dist <= flatR) {
            w = 1.0f;
        } else {
            float u = (dist - flatR) / (blendR - flatR);
            w = 1.0f - u * u * (3.0f - 2.0f * u);   // smoothstep ease-out
        }
        h += ((float)t.baseY - h) * w;
    }
    return h;
}

