#pragma once
#include "building.h"

// A large fenced crop field: tilled soil, alternating rows of wheat separated
// by 1-wide walkable Air furrows (so farmer NPCs can path the field instead of
// standing on the crops — anything but Air/Water counts as solid ground to the
// NPC ground-snap), a 2-tall wood fence with a front gate and taller corner
// posts. The `standalone` variant (placed out on the roads, not inside a town)
// adds a scarecrow at the field centre.
//
// Crops are stamped as Leaves here; the server-side FarmDirector later scans
// for them and drives each tile through growth stages (bare -> sprout ->
// growing -> ripe), editing the blocks as farmers hoe and scythe.

// Layout constants shared with the FarmDirector so it edits exactly the cells
// FarmBuilding planted. Crops sit one block above the tilled floor (grid y=1).
static constexpr int FARM_CROP_Y   = 1;   // crop row height above the plot floor
static constexpr int FARM_ROW_STEP = 2;   // a crop row every 2 cells (furrow between)
static constexpr int FARM_BORDER   = 2;   // bare-soil margin inside the fence

class FarmBuilding : public Building {
public:
    int  fieldW     = 24;
    int  fieldD     = 24;
    bool standalone = false;

    FarmBuilding() = default;
    FarmBuilding(int w, int d, bool s = false) : fieldW(w), fieldD(d), standalone(s) {}

    BuildingKind kind() const override { return BuildingKind::Farm; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};
