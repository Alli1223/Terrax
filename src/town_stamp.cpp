// Writing town features into chunks — split out of town.cpp. Each helper stamps
// one kind of feature (building, steps, road, dock, bridge, wall) into the chunk
// being generated; stampTownChunk drives them all. See town_internal.h.
#include "town_internal.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace townint;

// Feature-stamping helpers — used only by the public entry points below.
namespace {

// --- Stamping ----------------------------------------------------------------

void stampBuilding(Chunk* c, const TownBuilding& b) {
    if (b.dimX == 0) return;
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    if (b.wx >= ox + CHUNK_SIZE || b.wx + b.dimX <= ox) return;
    if (b.wz >= oz + CHUNK_SIZE || b.wz + b.dimZ <= oz) return;

    int x0 = std::max(0, ox - b.wx), x1 = std::min(b.dimX, ox + CHUNK_SIZE - b.wx);
    int z0 = std::max(0, oz - b.wz), z1 = std::min(b.dimZ, oz + CHUNK_SIZE - b.wz);

    for (int x = x0; x < x1; x++)
        for (int z = z0; z < z1; z++) {
            int wx = b.wx + x, wz = b.wz + z;
            int lx = wx - ox,  lz = wz - oz;

            // Clear the column the building actually occupies (terrain bumps,
            // tree trunks/leaves that would clip through walls or the roof).
            // Anything *above* the building's top is left alone, so a town in
            // a forest keeps its canopy poking up above the rooftops instead
            // of carving a bare-sky disc out of the trees. A trunk that ran
            // through the building footprint is removed, which can leave a
            // floating crown — but that reads a lot more like "house built
            // in the woods" than the old strip-mined version did.
            for (int wy = b.baseY; wy < b.baseY + b.dimY; wy++)
                c->set(lx, wy, lz, BlockType::Air);

            // Stamp the building (its own Air cells carve clean space).
            for (int y = 0; y < b.dimY; y++)
                c->set(lx, b.baseY + y, lz,
                       (BlockType)b.blocks[((size_t)y * b.dimZ + z) * b.dimX + x]);

            // Foundation skirt: fill solid from the plot base down past the
            // real ground (at least 12 blocks) so a house never floats over a
            // dip in the terrain. Scans the chunk's own blocks — ground truth,
            // unlike sampleSurface() which is only the predicted height.
            for (int wy = b.baseY - 1; wy >= 0; wy--) {
                BlockType cur = c->get(lx, wy, lz);
                bool ground = cur != BlockType::Air   && cur != BlockType::Water  &&
                              cur != BlockType::Wood  && cur != BlockType::Leaves &&
                              cur != BlockType::LeavesOrange &&
                              cur != BlockType::LeavesRed    &&
                              cur != BlockType::LeavesPink   && cur != BlockType::Cactus;
                if (ground && b.baseY - wy > 12) break;
                c->set(lx, wy, lz, BlockType::Stone);
            }
        }
}

// Builds a descending stone staircase from a building's front door down to
// the terrain, so a building on raised ground stays reachable from the
// street. The steps run toward the town centre and are clipped to chunk `c`.
void stampHouseSteps(Chunk* c, const TownBuilding& b) {
    if (b.rooms.empty()) return;                      // any kind with a front door
    if (b.doorDX == 0 && b.doorDZ == 0) return;
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;

    int wallX, wallZ;                                 // centre of the door wall
    if      (b.doorDZ < 0) { wallX = b.wx + b.dimX / 2; wallZ = b.wz; }
    else if (b.doorDZ > 0) { wallX = b.wx + b.dimX / 2; wallZ = b.wz + b.dimZ - 1; }
    else if (b.doorDX < 0) { wallX = b.wx;              wallZ = b.wz + b.dimZ / 2; }
    else                   { wallX = b.wx + b.dimX - 1; wallZ = b.wz + b.dimZ / 2; }

    for (int k = 1; k <= 14; k++) {                   // each step drops one block
        int cx = wallX + b.doorDX * k;
        int cz = wallZ + b.doorDZ * k;
        int stepY = b.baseY - k;
        for (int w = -1; w <= 1; w++) {               // 3-wide, across the doorway
            int wx = cx + (b.doorDZ != 0 ? w : 0);
            int wz = cz + (b.doorDX != 0 ? w : 0);
            int lx = wx - ox, lz = wz - oz;
            if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

            int g = -1;                               // top solid (non-foliage) block
            for (int y = b.baseY + 4; y >= 0; y--) {
                BlockType t = c->get(lx, y, lz);
                if (t == BlockType::Air || t == BlockType::Water ||
                    t == BlockType::Wood || t == BlockType::Leaves ||
                    t == BlockType::LeavesOrange || t == BlockType::LeavesRed ||
                    t == BlockType::LeavesPink || t == BlockType::Cactus) continue;
                g = y; break;
            }
            if (stepY <= g) continue;                 // ground already at/above the step
            for (int y = stepY; y > g && y >= 0; y--) // solid step, no float
                c->set(lx, y, lz, BlockType::Stone);
            for (int y = stepY + 1; y <= stepY + 3 && y < CHUNK_HEIGHT; y++)
                c->set(lx, y, lz, BlockType::Air);    // walking headroom
        }
    }
}

// --- Path width --------------------------------------------------------------
// Gravel paths are not a fixed width: each one widens and narrows along its
// length. The half-width at a point comes from smooth 1-D value noise of the
// arc length travelled, so a given path position yields the same width no
// matter which chunk happens to stamp it. (PATH_HW_* live in town_internal.h.)

// Smooth value noise in [0,1]: lattice points are hashed and blended with a
// smoothstep, giving a continuous curve with no abrupt steps.
float pathNoise1D(float x, uint32_t seed) {
    int   i0 = (int)std::floor(x);
    float f  = x - (float)i0;
    f = f * f * (3.0f - 2.0f * f);
    auto h = [seed](int i) {
        uint32_t u = (uint32_t)i * 0x9E3779B1u ^ seed;
        u ^= u >> 16; u *= 0x85EBCA77u; u ^= u >> 13;
        return (float)(u & 0xFFFFFFu) / (float)0xFFFFFFu;
    };
    float a = h(i0), b = h(i0 + 1);
    return a + (b - a) * f;
}

// Path half-width `arc` blocks along a path. A low-frequency octave drives the
// broad widen/narrow; a smaller octave adds finer texture to the edge.
float pathHalfWidth(float arc, uint32_t seed) {
    float n = pathNoise1D(arc * (1.0f / 25.0f), seed)           * 0.75f
            + pathNoise1D(arc * (1.0f /  9.0f), seed ^ 0xA53Cu) * 0.25f;
    return PATH_HW_MIN + n * (PATH_HW_MAX - PATH_HW_MIN);
}

// Lays one road cell. Two surface mixes are produced, picked by `sink`:
//   - sink == 0 (intra-town paths): gravel + stone cobble mix.
//   - sink >  0 (cross-country highways): mostly dirt with a sprinkle of
//     gravel, so the country lanes read as worn dirt tracks rather than
//     paved roads.
// Either way the column above is cleared so the path stays walkable.
void stampRoadCell(Chunk* c, int lx, int lz, int sink) {
    int gtop = -1;
    for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
        BlockType b = c->get(lx, y, lz);
        if (b == BlockType::Air || b == BlockType::Water || b == BlockType::Wood ||
            b == BlockType::Leaves || b == BlockType::LeavesOrange ||
            b == BlockType::LeavesRed || b == BlockType::LeavesPink ||
            b == BlockType::Cactus) continue;
        gtop = y; break;
    }
    if (gtop < 0) return;
    // Idempotent guard: a wide road revisits each cell from many overlapping
    // stamps. If the surface is already any of the road materials, stop —
    // otherwise each revisit would re-engrave it one block deeper.
    BlockType already = c->get(lx, gtop, lz);
    if (already == BlockType::Gravel || already == BlockType::Stone ||
        already == BlockType::Dirt) return;

    // Recess the lane one block into the ground so it reads as a defined, sunken
    // path with a clean lip on either side. `sink` selects the surface material
    // (0 = town cobble, >0 = country dirt); the town path still drops one block,
    // and a deeper `sink` engraves further.
    const int depth = std::max(1, sink);
    int roadY;
    if (gtop >= WORLD_SEA_LEVEL) {
        roadY = gtop - depth;                           // land — engraved `depth` blocks down
    } else {
        roadY = WORLD_SEA_LEVEL;                        // water — a stone causeway
        for (int y = gtop + 1; y < roadY; y++) c->set(lx, y, lz, BlockType::Stone);
    }
    // Per-cell deterministic hash on world XZ so the same patch of road
    // looks the same every visit and across chunk boundaries.
    int wx = c->pos.x * CHUNK_SIZE + lx;
    int wz = c->pos.z * CHUNK_SIZE + lz;
    uint32_t h = (uint32_t)wx * 0x9E3779B1u
               ^ (uint32_t)wz * 0x85EBCA77u
               ^ 0xC0BB1Eu;
    h ^= h >> 16;
    int   r       = (int)(h & 0x3F);     // 0..63
    BlockType surface;
    if (sink == 0) {
        // Town path: ~60% gravel + ~40% stone, cobble look.
        // Mostly stone with a light gravel speckle — a clean stone-paved lane
        // rather than the old patchy gravel track.
        surface = (r < 54) ? BlockType::Stone : BlockType::Gravel;
    } else {
        // Country highway: a plain dirty-brown dirt track.
        surface = BlockType::Dirt;
    }
    (void)r;     // r is unused for the highway case; keep the seed step for parity
    c->set(lx, roadY, lz, surface);

    // Keep the path on the ground: a tree rooted on the road is removed whole,
    // but a canopy that merely overhangs the path is left intact.
    if (c->get(lx, roadY + 1, lz) == BlockType::Wood ||
        c->get(lx, roadY + 1, lz) == BlockType::Cactus) {
        for (int y = roadY + 1; y < CHUNK_HEIGHT; y++) {
            BlockType b = c->get(lx, y, lz);
            if (b != BlockType::Wood && b != BlockType::Leaves &&
                b != BlockType::LeavesOrange && b != BlockType::LeavesRed &&
                b != BlockType::LeavesPink && b != BlockType::Cactus)
                break;
            c->set(lx, y, lz, BlockType::Air);
        }
    } else {
        c->set(lx, roadY + 1, lz, BlockType::Air);       // a little walking headroom
        c->set(lx, roadY + 2, lz, BlockType::Air);
    }
}

// Rasterises a road polyline into this chunk; each segment is slab-clipped to
// the chunk so only the part that actually crosses it is drawn. The stamp is a
// disc whose radius varies smoothly along the path (see pathHalfWidth).
void stampRoad(Chunk* c, const TownRoad& r, int sink) {
    if (r.pts.size() < 2) return;
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const int hw = PATH_HW_CEIL;                        // widest the disc reaches
    const int xmin = ox - hw - 1, xmax = ox + CHUNK_SIZE + hw;
    const int zmin = oz - hw - 1, zmax = oz + CHUNK_SIZE + hw;

    // Per-road seed: each path's width wobble is unique but deterministic, so
    // every chunk that re-stamps the road agrees on the width along it.
    uint32_t seed = (uint32_t)worldSeed()
                  ^ (uint32_t)(r.pts[0].x * 0x9E3779B1u)
                  ^ (uint32_t)(r.pts[0].y * 0x85EBCA77u);

    float arc = 0.0f;                                   // arc length to segment start
    for (size_t i = 0; i + 1 < r.pts.size(); i++) {
        int ax = r.pts[i].x,     az = r.pts[i].y;
        int dx = r.pts[i+1].x - ax, dz = r.pts[i+1].y - az;
        float segLen = std::sqrt((float)(dx * dx + dz * dz));

        float t0 = 0.0f, t1 = 1.0f;
        bool ok = true;
        auto slab = [&](int a, int d, int lo, int hi) {
            if (d == 0) { if (a < lo || a > hi) ok = false; return; }
            float ta = (float)(lo - a) / (float)d, tb = (float)(hi - a) / (float)d;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) ok = false;
        };
        slab(ax, dx, xmin, xmax);
        if (ok) slab(az, dz, zmin, zmax);
        if (!ok) { arc += segLen; continue; }

        int steps = std::max(std::abs(dx), std::abs(dz));
        if (steps == 0) steps = 1;
        int s0 = std::max(0,     (int)std::floor(t0 * steps));
        int s1 = std::min(steps, (int)std::ceil (t1 * steps));
        for (int s = s0; s <= s1; s++) {
            int px = ax + (int)((long long)dx * s / steps);
            int pz = az + (int)((long long)dz * s / steps);

            // Disc radius for this point, from the arc length reached so far.
            float w  = pathHalfWidth(arc + segLen * ((float)s / (float)steps), seed);
            float w2 = w * w;
            int   wi = (int)std::ceil(w);
            for (int ddx = -wi; ddx <= wi; ddx++)
                for (int ddz = -wi; ddz <= wi; ddz++) {
                    if ((float)(ddx * ddx + ddz * ddz) > w2) continue;
                    int lx = px + ddx - ox, lz = pz + ddz - oz;
                    if (lx >= 0 && lx < CHUNK_SIZE && lz >= 0 && lz < CHUNK_SIZE)
                        stampRoadCell(c, lx, lz, sink);
                }
        }
        arc += segLen;
    }
}

// Stamps a wooden jetty: a 3-wide plank deck at sea level reaching out over the
// water, carried on posts driven down to the seabed.
void stampDock(Chunk* c, const TownDock& d) {
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const int deckY = WORLD_SEA_LEVEL;
    const int px = d.dz, pz = d.dx;                     // perpendicular (width) axis

    for (int i = 1; i <= DOCK_LEN; i++) {
        int cx = d.root.x + d.dx * i;
        int cz = d.root.y + d.dz * i;
        for (int w = -1; w <= 1; w++) {
            int wx = cx + px * w, wz = cz + pz * w;
            int lx = wx - ox, lz = wz - oz;
            if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

            c->set(lx, deckY, lz, BlockType::Wood);             // deck plank
            for (int y = deckY + 1; y < CHUNK_HEIGHT; y++)      // keep it walkable
                c->set(lx, y, lz, BlockType::Air);

            if (w == 0 && (i == DOCK_LEN || i % 3 == 0)) {      // support post
                int seabed = 0;
                for (int y = deckY - 1; y >= 0; y--) {
                    BlockType b = c->get(lx, y, lz);
                    if (b != BlockType::Air && b != BlockType::Water) { seabed = y; break; }
                }
                for (int y = seabed + 1; y < deckY; y++)
                    c->set(lx, y, lz, BlockType::Wood);
            }
        }
    }
}

// Stamps a raised plank bridge: a 3-wide deck at deckY with the span above it
// cleared, carried on wooden posts driven down to the terrain.
void stampBridge(Chunk* c, const TownBridge& br) {
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const int deckY = br.deckY;
    const int DECK_HALF = 3;          // 7-wide deck (~5 walkable between railings)
    const int xmin = ox - DECK_HALF - 2, xmax = ox + CHUNK_SIZE + DECK_HALF + 1;
    const int zmin = oz - DECK_HALF - 2, zmax = oz + CHUNK_SIZE + DECK_HALF + 1;

    // Helper: set one cell, clamped to this chunk.
    auto setCell = [&](int wx, int wz, int y, BlockType t) {
        int lx = wx - ox, lz = wz - oz;
        if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) return;
        if (y < 0 || y >= CHUNK_HEIGHT) return;
        c->set(lx, y, lz, t);
    };
    // Helper: clear the column above (wx, wz) starting at yStart so the deck
    // / stair stays walkable.
    auto clearAbove = [&](int wx, int wz, int yStart) {
        int lx = wx - ox, lz = wz - oz;
        if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) return;
        for (int y = std::max(0, yStart); y < CHUNK_HEIGHT; y++)
            c->set(lx, y, lz, BlockType::Air);
    };
    // Helper: place one row of deck/stair planks (5 wide perpendicular to the
    // travel direction) at world (wx, wz) with the row at `y`. The two outer
    // cells of every row get a wooden railing one block above, so the bridge
    // / stair is fenced on both sides for its full length.
    auto placeRow = [&](int wx, int wz, int y, bool horizDir) {
        for (int w = -DECK_HALF; w <= DECK_HALF; w++) {
            int cx = horizDir ? wx : wx + w;
            int cz = horizDir ? wz + w : wz;
            setCell(cx, cz, y, BlockType::Wood);
            clearAbove(cx, cz, y + 1);
            if (std::abs(w) == DECK_HALF)
                setCell(cx, cz, y + 1, BlockType::Wood);   // railing
        }
    };

    // --- Deck ---------------------------------------------------------------
    // Walk every segment of the centreline, clipping the iteration window to
    // this chunk's slab (plus the deck overhang) so we only place cells the
    // chunk owns. arc counts steps along the whole bridge so railing posts
    // sit on a global spacing rather than per-segment.
    int arc = 0;
    for (size_t i = 0; i + 1 < br.pts.size(); i++) {
        int ax = br.pts[i].x, az = br.pts[i].y;
        int dx = br.pts[i + 1].x - ax, dz = br.pts[i + 1].y - az;

        float t0 = 0.0f, t1 = 1.0f;
        bool ok = true;
        auto slab = [&](int p, int dd, int lo, int hi) {
            if (dd == 0) { if (p < lo || p > hi) ok = false; return; }
            float ta = (float)(lo - p) / (float)dd, tb = (float)(hi - p) / (float)dd;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) ok = false;
        };
        slab(ax, dx, xmin, xmax);
        if (ok) slab(az, dz, zmin, zmax);

        int steps = std::max(std::abs(dx), std::abs(dz));
        if (steps == 0) steps = 1;
        bool horizDir = std::abs(dx) >= std::abs(dz);

        if (ok) {
            int s0 = std::max(0,     (int)std::floor(t0 * steps));
            int s1 = std::min(steps, (int)std::ceil (t1 * steps));
            for (int s = s0; s <= s1; s++) {
                int px = ax + (int)((long long)dx * s / steps);
                int pz = az + (int)((long long)dz * s / steps);
                placeRow(px, pz, deckY, horizDir);
                // Taller railing posts every 4 cells along the deck for a
                // grand-bridge silhouette.
                int curArc = arc + s;
                if ((curArc & 3) == 0) {
                    for (int w : { -DECK_HALF, DECK_HALF }) {
                        int cx = horizDir ? px : px + w;
                        int cz = horizDir ? pz + w : pz;
                        setCell(cx, cz, deckY + 2, BlockType::Wood);
                    }
                }
            }
        }
        arc += steps;
    }

    // --- Stone support pillars ---------------------------------------------
    // 2x2 stone footprint under every third centreline vertex, going from the
    // terrain surface up to just under the deck. Reads as a real masonry pier
    // rather than the single-block wooden post the bridge used before.
    for (size_t i = 0; i < br.pts.size(); i += 3) {
        int cx = br.pts[i].x, cz = br.pts[i].y;
        int gy = sampleSurface(cx, cz).height;
        for (int dwx = -1; dwx <= 0; dwx++)
            for (int dwz = -1; dwz <= 0; dwz++) {
                for (int y = std::max(0, gy); y < deckY; y++)
                    setCell(cx + dwx, cz + dwz, y, BlockType::Stone);
            }
    }

    // --- End staircases -----------------------------------------------------
    // Each end of the bridge gets a stair that steps outward by 1 block per
    // step and drops by 1 block in Y per step, until the step is at or below
    // the local ground. That fills the awkward vertical gap where the deck
    // used to leave the road floating in space.
    auto buildStair = [&](glm::ivec2 deckEnd, glm::ivec2 inward) {
        int outX = deckEnd.x - inward.x;
        int outZ = deckEnd.y - inward.y;
        // Normalise to the dominant cardinal direction.
        if (std::abs(outX) >= std::abs(outZ)) {
            outX = (outX > 0) - (outX < 0); outZ = 0;
        } else {
            outZ = (outZ > 0) - (outZ < 0); outX = 0;
        }
        if (outX == 0 && outZ == 0) return;
        bool horizDir = (outX != 0);
        for (int k = 1; k <= 32; k++) {
            int px = deckEnd.x + outX * k;
            int pz = deckEnd.y + outZ * k;
            int yStep = deckY - k;
            int gy = sampleSurface(px, pz).height;
            if (yStep < gy) break;            // step would dig into the ground
            placeRow(px, pz, yStep, horizDir);
            // Fill solid wood below the step down to the terrain so the stair
            // reads as a continuous earthwork ramp rather than a floating run
            // of free-standing planks.
            for (int w = -DECK_HALF; w <= DECK_HALF; w++) {
                int cx = horizDir ? px : px + w;
                int cz = horizDir ? pz + w : pz;
                for (int y = yStep - 1; y >= std::max(0, gy); y--)
                    setCell(cx, cz, y, BlockType::Wood);
            }
            if (yStep <= gy) break;           // stepped onto the ground — done
        }
    };

    if (br.pts.size() >= 2) {
        buildStair(br.pts.front(), br.pts[1]);
        buildStair(br.pts.back(),  br.pts[br.pts.size() - 2]);
    }
}

}  // namespace

// Stamps the perimeter wall of a walled town: a ring at `wallRadius` in the
// town's chosen style (wooden palisade, stone wall, great rampart, sandstone),
// with gateway openings (a clear passage under a lintel, flanked by taller
// tower sections) wherever a highway leaves toward a neighbour. The wall is
// derived per-column from the distance and bearing to the town centre, so it
// streams chunk-by-chunk like every other town feature, sitting on the town's
// flat base level with a foundation skirt over any dip in the ground.
void stampTownWall(Chunk* c, const Town& t) {
    if (t.wallRadius <= 0) return;
    const int   ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    const float R = (float)t.wallRadius;
    const int   baseY = t.baseY;
    int styleIdx = (t.wallStyle >= 0 && t.wallStyle < WALL_STYLE_COUNT) ? t.wallStyle : 1;
    const WallStyleDef& st = WALL_STYLES[styleIdx];
    const int   WALL_H = st.height;
    const float HALF   = st.halfThick;

    // Cull: does the ring band actually cross this chunk's XZ box?
    float cxC   = std::min(std::max((float)t.center.x, (float)ox), (float)(ox + CHUNK_SIZE - 1));
    float czC   = std::min(std::max((float)t.center.y, (float)oz), (float)(oz + CHUNK_SIZE - 1));
    float nearD = std::sqrt(std::pow((float)t.center.x - cxC, 2.0f) +
                            std::pow((float)t.center.y - czC, 2.0f));
    float farDx = std::max(std::abs((float)t.center.x - ox),
                           std::abs((float)t.center.x - (ox + CHUNK_SIZE - 1)));
    float farDz = std::max(std::abs((float)t.center.y - oz),
                           std::abs((float)t.center.y - (oz + CHUNK_SIZE - 1)));
    float farD  = std::sqrt(farDx * farDx + farDz * farDz);
    if (farD < R - HALF - 0.5f || nearD > R + HALF + 0.5f) return;

    const float gateHalf = 5.0f / R;   // angular half-width of a gate opening
    const float towerArc = 2.2f / R;   // arc each flanking tower takes

    for (int lx = 0; lx < CHUNK_SIZE; lx++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int   wx = ox + lx, wz = oz + lz;
            float dx = (float)(wx - t.center.x), dz = (float)(wz - t.center.y);
            float dist = std::sqrt(dx * dx + dz * dz);
            if (std::abs(dist - R) > HALF) continue;          // wall thickness (by style)

            float ang = std::atan2(dz, dx);
            float ad  = 6.2831853f;
            for (float g : t.gateAngles) {
                float diff = ang - g;
                while (diff >  3.14159265f) diff -= 6.2831853f;
                while (diff < -3.14159265f) diff += 6.2831853f;
                ad = std::min(ad, std::abs(diff));
            }
            bool opening = (ad < gateHalf - towerArc);
            bool tower   = (!opening && ad < gateHalf);
            int  top     = baseY + WALL_H + (tower ? st.towerBonus : 0);

            if (opening) {
                // Gateway: clear the passage down to the engraved road level so
                // the highway runs straight through, with a lintel across the
                // top and a stone threshold flush with the road.
                for (int y = baseY; y <= baseY + WALL_H + 5 && y < CHUNK_HEIGHT; y++)
                    c->set(lx, y, lz, BlockType::Air);
                for (int y = baseY + WALL_H; y <= baseY + WALL_H + 1 && y < CHUNK_HEIGHT; y++)
                    c->set(lx, y, lz, st.mat);
                for (int y = baseY - 1; y >= 0; y--) {
                    BlockType cur = c->get(lx, y, lz);
                    if (cur != BlockType::Air && cur != BlockType::Water && (baseY - 1 - y) > 12) break;
                    c->set(lx, y, lz, BlockType::Stone);
                }
            } else {
                // Clear terrain / foliage out of the column, raise the wall
                // (crenellated, taller at the gate towers), and skirt it down.
                for (int y = baseY + 1; y <= baseY + WALL_H + 5 && y < CHUNK_HEIGHT; y++)
                    c->set(lx, y, lz, BlockType::Air);
                for (int y = baseY + 1; y <= top && y < CHUNK_HEIGHT; y++) {
                    // Crenellate stone battlements; palisades keep a solid top.
                    if (!tower && st.crenel && y == baseY + WALL_H && ((wx + wz) & 1)) continue;
                    c->set(lx, y, lz, st.mat);
                }
                for (int y = baseY; y >= 0; y--) {
                    BlockType cur = c->get(lx, y, lz);
                    if (cur != BlockType::Air && cur != BlockType::Water && baseY - y > 12) break;
                    c->set(lx, y, lz, BlockType::Stone);
                }
            }
        }
}

// Paves the central town square — a stone-rimmed cobbled disc with a concentric
// stone ring — around the centrepiece. Stamped after the paths and before the
// buildings, so the radial lanes and the centrepiece both sit on top of it.
void stampTownPlaza(Chunk* c, const Town& t) {
    const int R = t.plazaR;
    if (R < 5) return;
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;
    if (t.center.x + R < ox || t.center.x - R >= ox + CHUNK_SIZE) return;
    if (t.center.y + R < oz || t.center.y - R >= oz + CHUNK_SIZE) return;
    for (int lz = 0; lz < CHUNK_SIZE; lz++)
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            const int wx = ox + lx, wz = oz + lz;
            const float ddx = (float)(wx - t.center.x), ddz = (float)(wz - t.center.y);
            const float d = std::sqrt(ddx * ddx + ddz * ddz);
            if (d > (float)R) continue;
            int gtop = -1;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                BlockType b = c->get(lx, y, lz);
                if (b == BlockType::Air || b == BlockType::Water || b == BlockType::Wood ||
                    b == BlockType::Leaves || b == BlockType::LeavesOrange ||
                    b == BlockType::LeavesRed || b == BlockType::LeavesPink ||
                    b == BlockType::Cactus) continue;
                gtop = y; break;
            }
            if (gtop < WORLD_SEA_LEVEL) continue;                 // no square under water
            BlockType cur = c->get(lx, gtop, lz);
            if (cur == BlockType::Gravel || cur == BlockType::Stone) {
                // A lane crosses the square. stampRoadCell engraved it one block
                // down, so it sits recessed below the plaza and any bench, stall
                // or lamp placed on it hovers a block up. Lift the lane flush with
                // the plaza (fill the sunken block) before paving it over, so the
                // whole square is one level and props rest on the ground. The
                // empty-above check keeps this a no-op if the cell is revisited.
                if (gtop + 2 < CHUNK_HEIGHT && c->get(lx, gtop + 1, lz) == BlockType::Air) {
                    c->set(lx, gtop + 1, lz, BlockType::Stone);
                    gtop += 1;
                }
            }
            const bool rim   = d > (float)R - 2.0f;
            const bool ring  = std::abs(d - (float)R * 0.55f) < 1.2f;
            BlockType surface;
            if (rim || ring) surface = BlockType::Stone;
            else {
                uint32_t h = (uint32_t)wx * 0x9E3779B1u ^ (uint32_t)wz * 0x85EBCA77u ^ 0x9A2Eu;
                h ^= h >> 16;
                surface = ((h & 0x3F) < 54) ? BlockType::Stone : BlockType::Gravel;  // ~84% stone
            }
            c->set(lx, gtop, lz, surface);
            c->set(lx, gtop + 1, lz, BlockType::Air);
            c->set(lx, gtop + 2, lz, BlockType::Air);
        }
}

void stampTownChunk(Chunk* c) {
    const TownPlan& plan = getTownPlan();
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;

    auto ptsHit = [&](const std::vector<glm::ivec2>& pts) {
        int lox = 1 << 30, loz = 1 << 30, hix = -(1 << 30), hiz = -(1 << 30);
        for (const glm::ivec2& p : pts) {
            lox = std::min(lox, p.x); hix = std::max(hix, p.x);
            loz = std::min(loz, p.y); hiz = std::max(hiz, p.y);
        }
        return !(hix < ox - 3 || lox > ox + CHUNK_SIZE + 3 ||
                 hiz < oz - 3 || loz > oz + CHUNK_SIZE + 3);
    };

    // Roads & paths first — buildings stamp over them, so a road never shows
    // inside a house and an inter-town highway becomes a town's through-street.
    for (const Town& t : plan.towns) {
        if (t.bbMax.x <= ox - 4 || t.bbMin.x >= ox + CHUNK_SIZE + 4) continue;
        if (t.bbMax.y <= oz - 4 || t.bbMin.y >= oz + CHUNK_SIZE + 4) continue;
        for (const TownRoad& p : t.paths) stampRoad(c, p, 0);
        stampTownPlaza(c, t);
    }
    for (const TownRoad& h : plan.highways)
        if (ptsHit(h.pts)) stampRoad(c, h, 1);   // highways engraved one block down

    // Bridges (raised decks over gullies and rivers).
    for (const TownBridge& br : plan.bridges)
        if (ptsHit(br.pts)) stampBridge(c, br);

    // Docks (jetties reaching over open water).
    for (const TownDock& d : plan.docks) {
        int ex = d.root.x + d.dx * DOCK_LEN, ez = d.root.y + d.dz * DOCK_LEN;
        int lox = std::min(d.root.x, ex) - 1, hix = std::max(d.root.x, ex) + 1;
        int loz = std::min(d.root.y, ez) - 1, hiz = std::max(d.root.y, ez) + 1;
        if (hix < ox || lox >= ox + CHUNK_SIZE) continue;
        if (hiz < oz || loz >= oz + CHUNK_SIZE) continue;
        stampDock(c, d);
    }

    // Perimeter walls (large towns only) — stamped before buildings so a house
    // always takes precedence over a stray wall column. The wall ring sits well
    // outside the building bounding box, so it needs its own centre/radius cull.
    for (const Town& t : plan.towns) {
        if (t.wallRadius <= 0) continue;
        int R = t.wallRadius + 2;
        if (t.center.x + R <= ox || t.center.x - R >= ox + CHUNK_SIZE) continue;
        if (t.center.y + R <= oz || t.center.y - R >= oz + CHUNK_SIZE) continue;
        stampTownWall(c, t);
    }

    // Buildings.
    for (const Town& t : plan.towns) {
        if (t.bbMax.x <= ox || t.bbMin.x >= ox + CHUNK_SIZE) continue;
        if (t.bbMax.y <= oz || t.bbMin.y >= oz + CHUNK_SIZE) continue;
        for (const TownBuilding& b : t.buildings) {
            stampBuilding(c, b);
            stampHouseSteps(c, b);
        }
    }

    // Standalone roadside structures (towers, houses, big farms between towns).
    for (const TownBuilding& b : plan.roadside) {
        if (b.wx + b.dimX <= ox || b.wx >= ox + CHUNK_SIZE) continue;
        if (b.wz + b.dimZ <= oz || b.wz >= oz + CHUNK_SIZE) continue;
        stampBuilding(c, b);
        stampHouseSteps(c, b);
    }
}
