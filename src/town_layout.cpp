// Laying out a town: houses in concentric rings, farms in the outer fields, and
// the specialist buildings (pub, smith, mage tower, stable, chapel, ...). Split
// out of town.cpp; layoutTown is the entry point. See town_internal.h.
#include "town_internal.h"
#include "voxel_model.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace townint {

// --- Layout ------------------------------------------------------------------

bool boxesOverlap(int ax, int az, int aw, int ad, int bx, int bz, int bw, int bd) {
    return ax < bx + bw && ax + aw > bx && az < bz + bd && az + ad > bz;
}

bool tryPlaceHouse(Town& t, std::mt19937& rng, int px, int pz, int faceX, int faceZ,
                   const int* templ, int nT, const int* mats, int nM,
                   const int* roofs, int nR) {
    int q = doorQuadrant(px, pz, faceX, faceZ);
    TownBuilding b;
    uint32_t seed = worldSeed() ^ (uint32_t)(px * 73856093) ^ (uint32_t)(pz * 19349663);
    bakeHouse(b, templ[rng() % nT], roofs[rng() % nR], mats[rng() % nM], q, seed);
    if (b.dimX == 0) return false;
    b.wx = px - b.dimX / 2;
    b.wz = pz - b.dimZ / 2;
    for (const TownBuilding& o : t.buildings)
        if (boxesOverlap(b.wx - 3, b.wz - 3, b.dimX + 6, b.dimZ + 6,
                         o.wx, o.wz, o.dimX, o.dimZ))
            return false;
    // Each building gets the slope offset at its own centre — the chunk
    // generator pins the surrounding flat pad to this same value, so the door
    // and the path leading to it always meet at the door's level even when
    // the town as a whole tilts.
    b.baseY = t.baseY + townSlopeOffset(t, px, pz);
    t.buildings.push_back(std::move(b));
    return true;
}

bool tryPlaceFarm(Town& t, std::mt19937& rng, int px, int pz) {
    TownBuilding b;
    makeFarm(b, rng);
    b.wx = px - b.dimX / 2;
    b.wz = pz - b.dimZ / 2;
    for (const TownBuilding& o : t.buildings)
        if (boxesOverlap(b.wx - 4, b.wz - 4, b.dimX + 8, b.dimZ + 8,
                         o.wx, o.wz, o.dimX, o.dimZ))
            return false;
    b.baseY = t.baseY + townSlopeOffset(t, px, pz);
    t.buildings.push_back(std::move(b));
    return true;
}

// Stamps a single specialised Building (pub / blacksmith / mage tower) facing
// the town centre. Acts like tryPlaceHouse but takes a polymorphic generator
// so the caller picks the kind/material per town and type bias.
bool tryPlaceSpecial(Town& t, Building& gen, int px, int pz) {
    int q = doorQuadrant(px, pz, t.center.x, t.center.y);
    TownBuilding b;
    uint32_t seed = worldSeed() ^ (uint32_t)(px * 73856093) ^ (uint32_t)(pz * 19349663);
    bakeBuilding(b, gen, q, seed);
    if (b.dimX == 0) return false;
    b.wx = px - b.dimX / 2;
    b.wz = pz - b.dimZ / 2;
    for (const TownBuilding& o : t.buildings)
        if (boxesOverlap(b.wx - 3, b.wz - 3, b.dimX + 6, b.dimZ + 6,
                         o.wx, o.wz, o.dimX, o.dimZ))
            return false;
    b.baseY = t.baseY + townSlopeOffset(t, px, pz);
    t.buildings.push_back(std::move(b));
    return true;
}

// Houses arranged in concentric rings facing the town centre. `scattered`
// loosens the spacing for mountain villages (which also terrace naturally,
// since each house takes its own ground height).
void layoutRings(Town& t, std::mt19937& rng, int numH, bool scattered,
                 const int* templ, int nT, const int* mats, int nM,
                 const int* roofs, int nR) {
    int placed = 0;
    for (int ring = 0; ring < 16 && placed < numH; ring++) {
        int ringR = 22 + ring * 14;
        if (ringR > t.radius + 14) break;
        int slots = std::max(4, ringR / 5);
        float a0 = frand(rng, 0.0f, 6.2832f);
        for (int s = 0; s < slots && placed < numH; s++) {
            float jit = scattered ? 0.42f : 0.16f;
            float ang = a0 + s * (6.2832f / slots) + frand(rng, -jit, jit);
            int rr = ringR + (int)frand(rng, scattered ? -10.0f : -3.0f,
                                             scattered ?  10.0f :  3.0f);
            int px = t.center.x + (int)(cosf(ang) * rr);
            int pz = t.center.y + (int)(sinf(ang) * rr);
            if (tryPlaceHouse(t, rng, px, pz, t.center.x, t.center.y,
                              templ, nT, mats, nM, roofs, nR))
                placed++;
        }
    }
}

void layoutTown(Town& t) {
    std::mt19937 rng(worldSeed()
                     ^ (uint32_t)(t.center.x * 73856093)
                     ^ (uint32_t)(t.center.y * 19349663));

    // Town centrepiece — varies by town type and seed.
    {
        if (t.type == TownType::Mountain)
            t.centerpiece = TownCenter::Statue;
        else {
            static const TownCenter OPTS[] = { TownCenter::Well, TownCenter::Market,
                                               TownCenter::Campfire };
            t.centerpiece = OPTS[rng() % 3];
        }
        TownBuilding cp;
        switch (t.centerpiece) {
            case TownCenter::Market:   makeMarket(cp);   break;
            case TownCenter::Campfire: makeCampfire(cp); break;
            case TownCenter::Statue:   makeStatue(cp);   break;
            default:                   makeWell(cp);     break;
        }
        cp.wx    = t.center.x - cp.dimX / 2;
        cp.wz    = t.center.y - cp.dimZ / 2;
        cp.baseY = t.baseY;
        t.buildings.push_back(std::move(cp));
    }

    const bool big = (t.size == TownSize::Town);

    // House roof style follows the local biome: desert towns are uniformly
    // flat-roofed, snowy (mountain / tundra) towns get steep pitched roofs to
    // shed snow, and everywhere else mixes gabled / hipped / pyramid roofs.
    // layoutRings draws each house's roof from this set.
    static const int ROOF_FLAT[]  = { 0 };
    static const int ROOF_STEEP[] = { 4, 4, 3 };       // steep gable, occasional steep pyramid
    static const int ROOF_MIX[]   = { 1, 2, 3 };       // gabled, hipped, pyramid
    int townBiome = sampleSurface(t.center.x, t.center.y).biome;
    const int* ROOFS; int nROOFS;
    if (townBiome == 2)                          { ROOFS = ROOF_FLAT;  nROOFS = 1; }  // Desert
    else if (townBiome == 3 || townBiome == 4)   { ROOFS = ROOF_STEEP; nROOFS = 3; }  // Mountains/Tundra
    else                                         { ROOFS = ROOF_MIX;   nROOFS = 3; }

    // --- Specialised buildings (pub / blacksmith / mage tower) ------------
    // One pub and one blacksmith per town (every settlement has both — this
    // is a hand-wave, but it gives every village a familiar set of services).
    // A mage tower is rarer and biased toward mountain towns.
    auto placeSpecial = [&](Building& gen, float baseAngle, int innerR) {
        for (int attempt = 0; attempt < 8; attempt++) {
            float ang = baseAngle + frand(rng, -0.2f, 0.2f);
            int   rr  = innerR + (int)frand(rng, -2.0f, 6.0f);
            int   px  = t.center.x + (int)(cosf(ang) * rr);
            int   pz  = t.center.y + (int)(sinf(ang) * rr);
            if (tryPlaceSpecial(t, gen, px, pz)) return true;
            baseAngle += 0.6f;   // try a different sector
        }
        return false;
    };

    {
        // Pub — material picked by town type so it blends in with the houses.
        int pubMat;
        switch (t.type) {
            case TownType::Coastal:  pubMat = 7;  break;   // coastal palette
            case TownType::Mountain: pubMat = 4;  break;   // cabin
            default:                 pubMat = 1;  break;   // cottage
        }
        PubBuilding pub(pubMat, /*roof=*/1);
        placeSpecial(pub, frand(rng, 0.0f, 6.2832f), 18);
    }
    {
        // Blacksmith — usually stone walls; mountain villages get hipped roof
        // to handle snow load.
        int smithMat = (t.type == TownType::Mountain) ? 2 : 4;
        int smithRoof = (t.type == TownType::Mountain) ? 2 : 1;
        BlacksmithBuilding smith(smithMat, smithRoof);
        placeSpecial(smith, frand(rng, 0.0f, 6.2832f) + 2.094f, 18);  // +120°
    }
    {
        // Mage tower — common in mountain settlements (mages like remote
        // peaks), rare elsewhere.
        const int towerChance =
            (t.type == TownType::Mountain) ? 60 :
            (t.type == TownType::Coastal)  ? 25 : 35;
        if ((int)(rng() % 100) < (big ? towerChance + 15 : towerChance)) {
            int towerMat = (t.type == TownType::Mountain) ? 2 : 3;
            MageTowerBuilding tower(towerMat, big ? 4 : 3);
            placeSpecial(tower, frand(rng, 0.0f, 6.2832f) + 4.189f, 20);  // +240°
        }
    }
    {
        // Stable — most settlements keep horses; a little rarer in cramped
        // coastal towns. Placed out among the houses rather than at the centre.
        const int chance = (t.type == TownType::Coastal) ? 35 : 60;
        if ((int)(rng() % 100) < (big ? chance + 15 : chance)) {
            int mat = (t.type == TownType::Mountain) ? 4 : 6;   // cabin / forest timber
            StableBuilding stable(mat, /*roof=*/1);
            placeSpecial(stable, frand(rng, 0.0f, 6.2832f) + 1.047f, 26);   // +60°
        }
    }
    {
        // Chapel — a place of worship, more common in larger settlements.
        if ((int)(rng() % 100) < (big ? 65 : 45)) {
            int mat = (t.type == TownType::Coastal) ? 5 : 2;    // sandstone / stone
            ChapelBuilding chapel(mat, /*roof=*/4);
            placeSpecial(chapel, frand(rng, 0.0f, 6.2832f) + 3.665f, 28);   // +210°
        }
    }
    {
        // Apothecary — a herbalist's shop, biased toward bigger towns.
        const int chance = (t.type == TownType::Mountain) ? 25 : 40;
        if ((int)(rng() % 100) < (big ? chance + 15 : chance)) {
            int mat = (t.type == TownType::Coastal) ? 9 : 8;    // plum / autumn
            ApothecaryBuilding apo(mat, /*roof=*/1);
            placeSpecial(apo, frand(rng, 0.0f, 6.2832f) + 5.236f, 24);      // +300°
        }
    }
    {
        // Bakery — a common high-street shop.
        if ((int)(rng() % 100) < (big ? 55 : 40)) {
            int mat = (t.type == TownType::Mountain) ? 4 : 1;   // cabin / cottage
            BakeryBuilding bakery(mat, /*roof=*/1);
            placeSpecial(bakery, frand(rng, 0.0f, 6.2832f) + 0.785f, 26);   // +45°
        }
    }
    {
        // Watchtower — a guard post; common in walled or large settlements.
        const int chance = (t.wallRadius > 0) ? 55 : 25;
        if ((int)(rng() % 100) < (big ? chance + 15 : chance)) {
            int mat = (t.type == TownType::Coastal) ? 5 : 2;    // sandstone / stone
            WatchtowerBuilding tower(mat, big ? 5 : 4);
            placeSpecial(tower, frand(rng, 0.0f, 6.2832f) + 2.618f, 28);    // +150°
        }
    }

    if (t.type == TownType::Coastal) {
        // Coastal villages get the Norse longhouse mixed in — feels right for
        // a seafaring settlement on a fjord.
        static const int T[] = { 0, 1, 2, 4, 5, 10, 12, 13, 14, 15, 16, 17, 18, 19 };  // + composites
        static const int M[] = { 7, 1, 5 };         // coastal / cottage / sandstone
        int numH = t.targetHouses;
        layoutRings(t, rng, numH, false, T, 14, M, 3, ROOFS, nROOFS);
    } else if (t.type == TownType::Mountain) {
        // Mountain towns favour heavier wooden structures; both Norse templates
        // appear here for the high-alpine stave-church silhouette.
        static const int T[] = { 1, 2, 3, 4, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };  // + composites
        static const int M[] = { 2, 4, 0, 6 };      // stone / cabin / timber / forest
        int numH = t.targetHouses;
        layoutRings(t, rng, numH, true, T, 15, M, 4, ROOFS, nROOFS);
    } else {
        static const int T[] = { 0, 1, 2, 4, 5, 7, 12, 13, 14, 15, 16, 17, 18, 19 };  // mix + composites
        static const int M[] = { 0, 1, 4, 8 };        // timber/cottage/cabin/autumn
        int numH = t.targetHouses;
        layoutRings(t, rng, numH, false, T, 14, M, 4, ROOFS, nROOFS);
    }

    // An outer ring of fenced farm plots (sparse for mountain hamlets).
    int numFarms;
    if (t.type == TownType::Mountain)  numFarms = (int)(rng() % 2);          // 0..1
    else if (big)                      numFarms = 3 + (int)(rng() % 2);      // 3..4
    else                               numFarms = 2 + (int)(rng() % 2);      // 2..3
    // Farmland sits in the fields beyond the wall (or just past the houses in an
    // unwalled village).
    int farmRing = (t.wallRadius > 0) ? t.wallRadius + 14 : t.radius + 22;
    for (int f = 0; f < numFarms; f++)
        for (int attempt = 0; attempt < 10; attempt++) {
            float ang = frand(rng, 0.0f, 6.2832f);
            int   rr  = farmRing + (int)frand(rng, -10.0f, 18.0f);
            int   px  = t.center.x + (int)(cosf(ang) * rr);
            int   pz  = t.center.y + (int)(sinf(ang) * rr);
            if (tryPlaceFarm(t, rng, px, pz)) break;
        }

    // Bounding box over every building, for fast chunk-stamp culling.
    t.bbMin = glm::ivec2(1 << 30, 1 << 30);
    t.bbMax = glm::ivec2(-(1 << 30), -(1 << 30));
    for (const TownBuilding& b : t.buildings) {
        t.bbMin.x = std::min(t.bbMin.x, b.wx);
        t.bbMin.y = std::min(t.bbMin.y, b.wz);
        t.bbMax.x = std::max(t.bbMax.x, b.wx + b.dimX);
        t.bbMax.y = std::max(t.bbMax.y, b.wz + b.dimZ);
    }
}


}  // namespace townint
