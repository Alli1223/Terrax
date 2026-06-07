#pragma once
#include <cstdint>
#include <vector>
#include <random>
#include <glm/glm.hpp>
#include "npc.h"   // DirectorPlayer (+ a forward declaration of World)

// One farm's live crop state. Each crop tile cycles through growth stages,
// advanced by time and reset/started as farmers scythe and hoe it.
struct FarmField {
    uint64_t   id = 0;
    glm::ivec2 center{0};
    int        baseY  = 64;
    int        cropY  = 65;          // world Y of the crop blocks (baseY + FARM_CROP_Y)
    int        radius = 12;          // footprint half-extent (streaming + AI bounds)
    std::vector<glm::ivec2> tile;    // world XZ of each crop cell
    std::vector<uint8_t>    stage;   // 0 bare .. 3 ripe, parallel to `tile`
    std::vector<float>      timer;   // seconds until this tile advances a stage
    bool active = false;
};

// Server-side crop simulation. Owns every farm field in the world, advances
// wheat growth over time and pushes the resulting block changes to clients via
// the BlockUpdate packet. Farmer NPCs (owned by NpcDirector) drive the work
// API to hoe bare soil and scythe ripe wheat. Server-thread only; never GL.
class FarmDirector {
public:
    void update(float dt, const std::vector<DirectorPlayer>& players, World& world);

    // Read-only farm list, used by the NpcDirector to stream farmers in/out.
    const std::vector<FarmField>& farms() const { return fields; }

    // Nearest workable tile to `from` within farm `fi`: prefers a ripe tile to
    // scythe, otherwise a bare tile to hoe. Returns false if there is no work.
    bool nearestWorkTile(int fi, glm::vec2 from, glm::ivec2& outXZ,
                         int& outTileIdx, bool& outRipe) const;
    void harvestTile(int fi, int tileIdx, World& world);   // ripe wheat -> bare soil
    void hoeTile(int fi, int tileIdx, World& world);       // bare soil -> fresh sprout

private:
    void ensureRegistry();
    void setStage(FarmField& f, int idx, uint8_t stage, World& world);
    std::vector<FarmField> fields;
    bool built = false;
    std::mt19937 rng{0x4661726Du};
};
