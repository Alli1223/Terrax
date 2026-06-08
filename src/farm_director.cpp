#include "farm_director.h"
#include "town.h"            // getTownPlan, Town, TownBuilding
#include "building.h"        // BuildingKind
#include "building_farm.h"   // FARM_CROP_Y
#include "world.h"           // World, BlockType
#include "network.h"         // g_server, PacketType, BlockUpdatePacket
#include <algorithm>

// Block shown for each crop growth stage. Wheat blocks render as swaying voxel
// foliage (see Vegetation::emitWheat); stage 0 is bare (the tilled Farmland
// below shows through).
static BlockType stageBlock(uint8_t stage) {
    switch (stage) {
        case 0:  return BlockType::Air;          // bare tilled soil
        case 1:  return BlockType::WheatYoung;   // fresh green sprouts
        case 2:  return BlockType::WheatTall;    // tall, yellowing
        default: return BlockType::WheatRipe;    // ripe golden grain
    }
}

static uint64_t farmKey(int wx, int wz) {
    return ((uint64_t)(uint32_t)wx << 32) ^ (uint64_t)(uint32_t)wz;
}
static uint32_t hashTile(int x, int z) {
    uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(z * 19349663);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

void FarmDirector::ensureRegistry() {
    if (built) return;
    built = true;
    const TownPlan& plan = getTownPlan();
    auto addFarm = [&](const TownBuilding& b) {
        if (b.kind != (int)BuildingKind::Farm || b.dimX <= 0) return;
        FarmField f;
        f.center = glm::ivec2(b.wx + b.dimX / 2, b.wz + b.dimZ / 2);
        f.baseY  = b.baseY;
        f.cropY  = b.baseY + FARM_CROP_Y;
        f.radius = std::max(b.dimX, b.dimZ) / 2 + 2;
        f.id     = farmKey(b.wx, b.wz);
        // Scan the baked grid's crop row (grid y = FARM_CROP_Y) for planted cells.
        for (int z = 0; z < b.dimZ; z++)
            for (int x = 0; x < b.dimX; x++) {
                size_t idx = ((size_t)FARM_CROP_Y * b.dimZ + z) * b.dimX + x;
                if (idx >= b.blocks.size()) continue;
                if (!isWheatBlock((BlockType)b.blocks[idx])) continue;
                int wx = b.wx + x, wz = b.wz + z;
                f.tile.push_back(glm::ivec2(wx, wz));
                // Start each tile at a varied stage (1..3) so fields look
                // lived-in; the activation resync writes these to clients.
                f.stage.push_back((uint8_t)(1 + hashTile(wx, wz) % 3u));
                f.timer.push_back(8.0f + (float)(hashTile(wx + 7, wz - 3) % 24u));
            }
        if (!f.tile.empty()) fields.push_back(std::move(f));
    };
    for (const Town& t : plan.towns)
        for (const TownBuilding& b : t.buildings) addFarm(b);
    for (const TownBuilding& b : plan.roadside) addFarm(b);
}

void FarmDirector::setStage(FarmField& f, int idx, uint8_t stage, World& world) {
    f.stage[idx] = stage;
    int wx = f.tile[idx].x, wz = f.tile[idx].y;
    BlockType bt = stageBlock(stage);
    world.setBlock(wx, f.cropY, wz, bt);
    world.relightAt(wx, f.cropY, wz);
    if (g_server) {
        BlockUpdatePacket pkt{ wx, f.cropY, wz, (uint8_t)bt };
        g_server->broadcast(PacketType::BlockUpdate, &pkt, sizeof(pkt));
    }
}

void FarmDirector::update(float dt, const std::vector<DirectorPlayer>& players, World& world) {
    ensureRegistry();
    for (FarmField& f : fields) {
        float best2 = 1e18f;
        for (const DirectorPlayer& p : players) {
            float dx = p.pos.x - (float)f.center.x, dz = p.pos.z - (float)f.center.y;
            best2 = std::min(best2, dx * dx + dz * dz);
        }
        float act   = (float)f.radius + 140.0f;
        float deact = (float)f.radius + 200.0f;
        if (!f.active && best2 < act * act) {
            f.active = true;
            // Resync burst — write every tile to its current stage so a client
            // that just streamed the chunk sees the live crop, not the stale
            // Leaves the chunk generator stamped.
            for (int i = 0; i < (int)f.tile.size(); i++) setStage(f, i, f.stage[i], world);
        } else if (f.active && best2 > deact * deact) {
            f.active = false;   // freeze state so it resumes coherently
            continue;
        }
        if (!f.active) continue;

        // Grow: bare waits for a hoe, ripe waits for a scythe; the rest advance.
        for (int i = 0; i < (int)f.tile.size(); i++) {
            if (f.stage[i] == 0 || f.stage[i] >= 3) continue;
            f.timer[i] -= dt;
            if (f.timer[i] <= 0.0f) {
                uint8_t ns = (uint8_t)std::min(3, f.stage[i] + 1);
                f.timer[i] = 18.0f + (float)(hashTile(f.tile[i].x + (int)ns, f.tile[i].y) % 16u);
                setStage(f, i, ns, world);
            }
        }
    }
}

bool FarmDirector::nearestWorkTile(int fi, glm::vec2 from, glm::ivec2& outXZ,
                                   int& outTileIdx, bool& outRipe) const {
    if (fi < 0 || fi >= (int)fields.size()) return false;
    const FarmField& f = fields[fi];
    int bestRipe = -1, bestBare = -1;
    float dRipe = 1e18f, dBare = 1e18f;
    for (int i = 0; i < (int)f.tile.size(); i++) {
        if (f.stage[i] != 3 && f.stage[i] != 0) continue;
        float dx = (float)f.tile[i].x + 0.5f - from.x;
        float dz = (float)f.tile[i].y + 0.5f - from.y;
        float d2 = dx * dx + dz * dz;
        if (f.stage[i] == 3) { if (d2 < dRipe) { dRipe = d2; bestRipe = i; } }
        else                 { if (d2 < dBare) { dBare = d2; bestBare = i; } }
    }
    int pick = (bestRipe >= 0) ? bestRipe : bestBare;
    if (pick < 0) return false;
    outTileIdx = pick;
    outXZ      = f.tile[pick];
    outRipe    = (f.stage[pick] == 3);
    return true;
}

void FarmDirector::harvestTile(int fi, int tileIdx, World& world) {
    if (fi < 0 || fi >= (int)fields.size()) return;
    FarmField& f = fields[fi];
    if (tileIdx < 0 || tileIdx >= (int)f.tile.size()) return;
    if (f.stage[tileIdx] != 3) return;          // only ripe wheat is scythed
    setStage(f, tileIdx, 0, world);             // cut down to bare soil
}

void FarmDirector::hoeTile(int fi, int tileIdx, World& world) {
    if (fi < 0 || fi >= (int)fields.size()) return;
    FarmField& f = fields[fi];
    if (tileIdx < 0 || tileIdx >= (int)f.tile.size()) return;
    if (f.stage[tileIdx] != 0) return;          // only bare soil is hoed
    f.timer[tileIdx] = 12.0f + (float)(hashTile(f.tile[tileIdx].x, f.tile[tileIdx].y + 5) % 12u);
    setStage(f, tileIdx, 1, world);             // plant a fresh sprout
}
