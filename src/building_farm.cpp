#include "building_farm.h"
#include <algorithm>

// Builds a tight-cropped farm grid (index ((y*dimZ)+z)*dimX+x, the Building
// convention). y=0 is the tilled plot floor; crops at y=FARM_CROP_Y; the fence
// rises to y=2 with corner posts at y=3 (and a scarecrow to y=4 when standalone).
void FarmBuilding::generate(uint32_t seed,
                            std::vector<uint8_t>& blocks,
                            std::vector<Room>& rooms,
                            int& dimX, int& dimY, int& dimZ,
                            int& doorDX, int& doorDZ) {
    int w = std::max(12, fieldW);
    int d = std::max(12, fieldD);
    int H = standalone ? 5 : 4;            // scarecrow needs one extra row

    dimX = w; dimY = H; dimZ = d;
    blocks.assign((size_t)w * H * d, (uint8_t)BlockType::Air);
    auto set = [&](int x, int y, int z, BlockType t) {
        if (x < 0 || x >= w || y < 0 || y >= H || z < 0 || z >= d) return;
        blocks[((size_t)y * d + z) * w + x] = (uint8_t)t;
    };

    const BlockType soil = BlockType::Farmland;
    const BlockType crop = BlockType::WheatTall;   // mid stage; FarmDirector varies it live

    // A deterministic ~6% of crop cells are left fallow so fields don't look
    // perfectly uniform. Keyed on the bake seed + cell so it's stable per farm.
    auto fallow = [&](int x, int z) {
        uint32_t hsh = (uint32_t)(x * 73856093) ^ (uint32_t)(z * 19349663) ^ seed;
        hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
        return (hsh % 17u) == 0u;
    };

    // Tilled soil across the whole plot.
    for (int z = 0; z < d; z++)
        for (int x = 0; x < w; x++)
            set(x, 0, z, soil);

    // Wheat rows along X, every FARM_ROW_STEP cells, leaving an Air furrow
    // between rows for the farmers to walk.
    for (int z = FARM_BORDER; z <= d - 1 - FARM_BORDER; z += FARM_ROW_STEP)
        for (int x = FARM_BORDER; x <= w - 1 - FARM_BORDER; x++)
            if (!fallow(x, z)) set(x, FARM_CROP_Y, z, crop);

    // The perimeter fence is placed as modelled Fence props (placeFarmFence in
    // prop_placement.cpp), with a gate gap on the front (-Z) side — no longer a
    // stamped wall of wood blocks.

    // Standalone farms get a scarecrow at the field centre. Its straw head is
    // LeavesOrange so the FarmDirector's crop scan ignores it; the post cell is
    // cleared of crop so the scan skips it too.
    if (standalone) {
        const BlockType post = BlockType::Wood;
        int cx = w / 2, cz = d / 2;
        set(cx, FARM_CROP_Y, cz, BlockType::Air);
        set(cx, 1, cz, post); set(cx, 2, cz, post); set(cx, 3, cz, post);  // post
        set(cx - 1, 3, cz, post); set(cx + 1, 3, cz, post);                // arms
        set(cx, 4, cz, BlockType::LeavesOrange);                           // straw head
    }

    rooms.clear();                 // no interior → populateTown leaves farms alone
    doorDX = 0; doorDZ = -1;       // gate faces -Z (front); bakeBuilding rotates it
    doorCellX = w / 2; doorCellZ = 0;
}
