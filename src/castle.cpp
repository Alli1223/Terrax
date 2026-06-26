#include "castle.h"
#include "world.h"     // Chunk, CHUNK_SIZE/HEIGHT, sampleSurfaceSolid, BlockType
#include "npc.h"       // NPCType — enemy roster
#include <algorithm>
#include <cmath>

// --- shared geometry constants (used by both the layout and the stamp) -------
namespace {
const int GATE_DEPTH = 5;    // how far the gatehouse projects from the +X wall
const int GATE_HALF  = 2;    // gatehouse half-width (5 wide)
const int TOWER_R    = 3;    // round corner-tower radius
const int STAIR_OUT  = 24;   // max length of the graded entrance cutting
}

// Keep half-extent per size tier (12 / 18 / 24 / 30 → up to 61 across).
int CastleDungeon::keepHalf(int sizeTier) { return 12 + std::clamp(sizeTier, 0, 3) * 6; }

void CastleDungeon::generateLayout(uint32_t seed, glm::ivec2 a, int surf) {
    (void)seed;
    overground = true;
    floorH = 6;
    levels = std::clamp(3 + sizeTier, 3, 6);
    int half = keepHalf(sizeTier);
    anchor = a; surfaceY = surf; floorY = surf;

    const int KX0 = a.x - half, KX1 = a.x + half, KZ0 = a.y - half, KZ1 = a.y + half;
    const int rzN = KZ0 + 4, rzX = KZ1 - 1;          // the +Z "room area" (-Z strip is the stair)
    const int rcz = (rzN + rzX) / 2;

    // Rooms used for furnishing + enemy spawns. Lower storeys are split into a
    // west + east room by an interior wall; the top storey is the open boss hall.
    for (int lv = 0; lv < levels; lv++) {
        int wy = surf + lv * floorH, wt = wy + floorH - 2;
        if (lv == levels - 1) {
            DungeonRoom rm; rm.mn = glm::ivec3(KX0 + 1, wy, rzN); rm.mx = glm::ivec3(KX1 - 1, wt, rzX);
            rm.purpose = 2; rooms.push_back(rm);                                       // boss hall
        } else {
            DungeonRoom w; w.mn = glm::ivec3(KX0 + 1, wy, rzN); w.mx = glm::ivec3(a.x - 1, wt, rzX);
            w.purpose = 0; rooms.push_back(w);                                         // west wing
            DungeonRoom e; e.mn = glm::ivec3(a.x + 1, wy, rzN); e.mx = glm::ivec3(KX1 - 1, wt, rzX);
            e.purpose = 0; rooms.push_back(e);                                         // east wing
        }
    }
    entrance      = glm::ivec3(KX1 + GATE_DEPTH, surf, a.y);   // gatehouse mouth (map marker)
    entranceInner = glm::ivec3(KX1, surf, a.y);

    // World-XZ bounds: keep + corner-tower bulges + gatehouse + descending stair.
    bbMin = glm::ivec2(KX0 - TOWER_R - 1, KZ0 - TOWER_R - 1);
    bbMax = glm::ivec2(KX1 + GATE_DEPTH + STAIR_OUT + 2, KZ1 + TOWER_R + 1);

    // Dynamic point lights (no glowing voxels): a beacon per wing + corner-tower
    // beacons + a gatehouse beacon.
    for (int lv = 0; lv < levels; lv++) {
        int wy = surf + lv * floorH, by = wy + floorH - 3;
        if (lv == levels - 1) {
            lights.push_back({ glm::vec3((float)a.x + 0.5f, (float)by + 0.5f, (float)rcz + 0.5f), 2 });
        } else {
            lights.push_back({ glm::vec3((float)((KX0 + a.x) / 2) + 0.5f, (float)by + 0.5f, (float)rcz + 0.5f), 2 });
            lights.push_back({ glm::vec3((float)((a.x + KX1) / 2) + 0.5f, (float)by + 0.5f, (float)rcz + 0.5f), 2 });
        }
        lights.push_back({ glm::vec3((float)(KX0 + 2) + 0.5f, (float)(wy + 2) + 0.5f, (float)rcz + 0.5f), 0 });
        lights.push_back({ glm::vec3((float)(KX1 - 2) + 0.5f, (float)(wy + 2) + 0.5f, (float)rcz + 0.5f), 0 });
    }
    int roofY = surf - 1 + levels * floorH;
    int cpx[2] = { KX0, KX1 }, cpz[2] = { KZ0, KZ1 };
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++)
        lights.push_back({ glm::vec3((float)cpx[i] + 0.5f, (float)(roofY + 6) + 0.5f, (float)cpz[j] + 0.5f), 2 });
    lights.push_back({ glm::vec3((float)(KX1 + GATE_DEPTH) + 0.5f, (float)(surf + 4) + 0.5f, (float)a.y + 0.5f), 2 });
}

void CastleDungeon::fillSpawnTable(std::vector<DungeonSpawn>& out, uint32_t seed) const {
    // A garrison of fallen knights + skeletal levies + animated stone guardians,
    // under a warlord.
    rosterFill(out, seed, (uint8_t)NPCType::Knight, (uint8_t)NPCType::Warlord, 3,
               (uint8_t)NPCType::Skeleton, (uint8_t)NPCType::StoneElemental);
}

// --- Chunk stamping ----------------------------------------------------------

void stampCastleChunk(Chunk* c, const Dungeon& d) {
    const int ox = c->pos.x * CHUNK_SIZE, oz = c->pos.z * CHUNK_SIZE;

    auto setW = [&](int wx, int wy, int wz, BlockType b) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return;
        if (wx < ox || wx >= ox + CHUNK_SIZE || wz < oz || wz >= oz + CHUNK_SIZE) return;
        c->set(wx - ox, wy, wz - oz, b);
    };
    auto fillBox = [&](int x0, int x1, int y0, int y1, int z0, int z1, BlockType b) {
        int lx = std::max(x0, ox), hx = std::min(x1, ox + CHUNK_SIZE - 1);
        int lz = std::max(z0, oz), hz = std::min(z1, oz + CHUNK_SIZE - 1);
        int ly = std::max(0, y0),  hy = std::min(CHUNK_HEIGHT - 1, y1);
        for (int wx = lx; wx <= hx; wx++)
            for (int wz = lz; wz <= hz; wz++)
                for (int wy = ly; wy <= hy; wy++)
                    c->set(wx - ox, wy, wz - oz, b);
    };
    // True only for a cell inside this chunk (so we sample terrain just once per
    // cell, in the chunk that owns it).
    auto here = [&](int x, int z) { return x >= ox && x < ox + CHUNK_SIZE && z >= oz && z < oz + CHUNK_SIZE; };

    const BlockType wall = d.wallBlock(), floor = d.floorBlock(), wood = BlockType::Wood;

    int half = CastleDungeon::keepHalf(d.sizeTier);
    int ax = d.anchor.x, az = d.anchor.y;
    int KX0 = ax - half, KX1 = ax + half, KZ0 = az - half, KZ1 = az + half;
    int baseY = d.surfaceY, fh = d.floorH, L = d.levels;
    int roofY = baseY - 1 + L * fh;
    int ez = az;                                       // gate centre
    int rzN = KZ0 + 4, rzX = KZ1 - 1, rcz = (rzN + rzX) / 2;

    auto column = [&](int x, int z, int y0, int h, BlockType b) { for (int k = 0; k < h; k++) setW(x, y0 + k, z, b); };

    // 1. FOUNDATION SKIRT — grade the base down to the real terrain so the castle
    //    never floats over a dip. (interior terrain ABOVE base is cleared below).
    for (int x = KX0; x <= KX1; x++) for (int z = KZ0; z <= KZ1; z++) {
        if (!here(x, z)) continue;
        int terr = sampleSurfaceSolid(x, z);
        if (terr < baseY - 1) fillBox(x, x, terr, baseY - 1, z, z, floor);
    }
    // 2. Clear the interior (hill/trees) up past the roof.
    fillBox(KX0, KX1, baseY, roofY + 8, KZ0, KZ1, BlockType::Air);

    // Grand straight staircase up the -Z wall, through an open slot in every floor.
    const int stZ0 = KZ0 + 1, stZ1 = KZ0 + 3;
    const int stX0 = KX0 + 3;
    const int rise = (L - 1) * fh;
    const int stX1 = stX0 + rise;
    auto inStair = [&](int x, int z) { return x >= stX0 && x <= stX1 + 1 && z >= stZ0 && z <= stZ1; };

    fillBox(KX0, KX1, baseY - 12, baseY - 1, KZ0, KZ1, floor);     // solid plinth core

    // 3. Per storey: floor (open over the stair), perimeter walls with windows +
    //    the ground gate, an interior partition wall + doorway, and the throne.
    for (int lv = 0; lv < L; lv++) {
        int fs = baseY - 1 + lv * fh, wy = baseY + lv * fh, wt = wy + fh - 2;
        for (int x = KX0; x <= KX1; x++) for (int z = KZ0; z <= KZ1; z++)
            if (!inStair(x, z)) setW(x, fs, z, floor);
        for (int yy = wy; yy <= wt; yy++)
            for (int x = KX0; x <= KX1; x++) for (int z = KZ0; z <= KZ1; z++) {
                if (x != KX0 && x != KX1 && z != KZ0 && z != KZ1) continue;          // perimeter only
                bool window = (((x + z) & 3) == 0) && yy == wy + 2;
                bool gate   = lv == 0 && x == KX1 && z >= ez - 1 && z <= ez + 1 && yy <= wy + 3;  // into the gatehouse
                if (!window && !gate) setW(x, yy, z, wall);
            }
        // interior partition (lower storeys): a wall down x=ax with a central
        // doorway; z=KZ0+4 is left open as a cross-passage from the stair.
        if (lv < L - 1)
            for (int z = KZ0 + 5; z <= KZ1 - 1; z++)
                for (int yy = wy; yy <= wt; yy++)
                    if (!(z >= rcz - 1 && z <= rcz + 1 && yy <= wy + 3)) setW(ax, yy, z, wall);
        if (lv == L - 1) {                                                            // throne, back wall
            setW(KX0 + 2, wy, rcz, wall); setW(KX0 + 2, wy + 1, rcz, wall);
            setW(KX0 + 2, wy + 1, rcz - 1, wall); setW(KX0 + 2, wy + 1, rcz + 1, wall);
        }
    }
    // The continuous staircase, built once.
    for (int k = 0; k <= rise; k++) {
        int sx = stX0 + k, surf = baseY + k;
        for (int z = stZ0; z <= stZ1; z++) {
            fillBox(sx, sx, baseY - 1, surf - 1, z, z, floor);
            fillBox(sx, sx, surf, surf + 2, z, z, BlockType::Air);
        }
    }
    // 4. Roof deck + battlements.
    for (int x = KX0; x <= KX1; x++) for (int z = KZ0; z <= KZ1; z++)
        if (!inStair(x, z)) setW(x, roofY, z, floor);
    for (int x = KX0; x <= KX1; x += 2) { setW(x, roofY + 1, KZ0, wall); setW(x, roofY + 1, KZ1, wall); }
    for (int z = KZ0; z <= KZ1; z += 2) { setW(KX0, roofY + 1, z, wall); setW(KX1, roofY + 1, z, wall); }

    // 5. ROUND CORNER TOWERS — taller than the keep, open toward the keep so they
    //    are walkable round alcoves on every floor; crenellated, with a beacon.
    int cpx[2] = { KX0, KX1 }, cpz[2] = { KZ0, KZ1 };
    int towerH = roofY + 6, R = TOWER_R;
    for (int A = 0; A < 2; A++) for (int B = 0; B < 2; B++) {
        int tcx = cpx[A], tcz = cpz[B];
        int sx = (A == 0) ? -1 : 1, sz = (B == 0) ? -1 : 1;     // outward direction
        // clear the tower's round interior of any intruding terrain/trees
        for (int dx = -R; dx <= R; dx++) for (int dz = -R; dz <= R; dz++)
            if (dx*dx + dz*dz <= R*R) fillBox(tcx + dx, tcx + dx, baseY, towerH + 2, tcz + dz, tcz + dz, BlockType::Air);
        // foundation skirt for the tower
        for (int dx = -R; dx <= R; dx++) for (int dz = -R; dz <= R; dz++) {
            if (dx*dx + dz*dz > R*R) continue;
            int x = tcx + dx, z = tcz + dz;
            if (!here(x, z)) continue;
            int terr = sampleSurfaceSolid(x, z);
            if (terr < baseY - 1) fillBox(x, x, terr, baseY - 1, z, z, floor);
        }
        // ring wall — skip the inner quadrant so it opens into the keep
        for (int yy = baseY; yy <= towerH; yy++)
            for (int dx = -R; dx <= R; dx++) for (int dz = -R; dz <= R; dz++) {
                int r2 = dx*dx + dz*dz;
                if (r2 > R*R || r2 <= (R-2)*(R-2)) continue;
                bool inner = (dx * sx < 0) && (dz * sz < 0);
                if (!inner) setW(tcx + dx, yy, tcz + dz, wall);
            }
        // a floor inside the tower at every storey
        for (int lv = 0; lv < L; lv++) {
            int fs = baseY - 1 + lv * fh;
            for (int dx = -R; dx <= R; dx++) for (int dz = -R; dz <= R; dz++)
                if (dx*dx + dz*dz < R*R) setW(tcx + dx, fs, tcz + dz, floor);
        }
        for (int dx = -R; dx <= R; dx++) for (int dz = -R; dz <= R; dz++) {           // crenellations
            int r2 = dx*dx + dz*dz;
            if (r2 <= R*R && r2 > (R-2)*(R-2) && (((dx + dz) & 1) == 0)) setW(tcx + dx, towerH + 1, tcz + dz, wall);
        }
        setW(tcx, towerH, tcz, BlockType::Wood);     // beacon mount
    }

    // 6. GATEHOUSE — a projecting block on the +X face with an ARCHED entrance.
    int gX0 = KX1, gX1 = KX1 + GATE_DEPTH, gZ0 = ez - GATE_HALF, gZ1 = ez + GATE_HALF, gTop = baseY + 4;
    for (int x = gX0; x <= gX1; x++) for (int z = gZ0; z <= gZ1; z++) {              // graded floor
        if (!here(x, z)) continue;
        int terr = sampleSurfaceSolid(x, z);
        if (terr < baseY - 1) fillBox(x, x, terr, baseY - 1, z, z, floor);
        setW(x, baseY - 1, z, floor);
    }
    fillBox(gX0 + 1, gX1, baseY, gTop + 2, gZ0 + 1, gZ1 - 1, BlockType::Air);        // hollow passage
    for (int yy = baseY; yy <= gTop; yy++) {
        for (int x = gX0; x <= gX1; x++) { setW(x, yy, gZ0, wall); setW(x, yy, gZ1, wall); }   // side walls
        for (int z = gZ0; z <= gZ1; z++) {                                            // front wall + arch
            bool arch = (z >= ez - 1 && z <= ez + 1 && yy <= baseY + 2) || (z == ez && yy == baseY + 3);
            if (!arch) setW(gX1, yy, z, wall);
        }
    }
    for (int x = gX0; x <= gX1; x++) for (int z = gZ0; z <= gZ1; z++) setW(x, gTop + 1, z, wall);  // roof
    for (int x = gX0; x <= gX1; x += 2) { setW(x, gTop + 2, gZ0, wall); setW(x, gTop + 2, gZ1, wall); }

    // 7. ENTRANCE APPROACH — a graded ramp cut from the arch out to the real
    //    ground. It ramps UP into rising ground (cutting an open trench through
    //    the hill) or DOWN toward a dip, so the gate is ALWAYS reachable — never
    //    buried in the terrain.
    int pf = baseY - 1;                            // path-floor block (walkable pf+1)
    for (int i = 1; i <= STAIR_OUT; i++) {
        int x = gX1 + i;
        int terrC = sampleSurfaceSolid(x, ez);
        if      (pf < terrC) pf++;                  // ramp up into a rising hill
        else if (pf > terrC) pf--;                  // ramp down toward lower ground
        for (int z = ez - GATE_HALF; z <= ez + GATE_HALF; z++) {
            if (!here(x, z)) continue;
            int terr = sampleSurfaceSolid(x, z);
            fillBox(x, x, std::min(terr, pf), pf, z, z, floor);                 // solid tread (filled to ground)
            int clearTop = std::max(pf + 5, terr + 1);                           // open the cutting to the sky
            fillBox(x, x, pf + 1, clearTop, z, z, BlockType::Air);               // remove any hill above the path
        }
        if (pf == terrC) break;                     // met the ground
    }

    // 8. Small non-glowing wood fixtures for the dynamic point lights (placed
    //    last so the structure doesn't erase them).
    for (const DungeonLight& Lt : d.lights) {
        int lx = (int)std::floor(Lt.pos.x), ly = (int)std::floor(Lt.pos.y), lz = (int)std::floor(Lt.pos.z);
        if (Lt.kind == 1) { setW(lx, ly - 2, lz, wood); setW(lx, ly - 1, lz, wood); }
        else if (Lt.kind == 0) setW(lx, ly, lz, wood);
    }
}
