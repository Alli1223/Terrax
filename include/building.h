#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include "world.h"

// --- Building & Room model ---------------------------------------------------
// A polymorphic, extensible building hierarchy. Each concrete `Building`
// subclass produces a tight-cropped block grid plus a list of semantic `Room`s
// the furniture placer uses to decide which props go where. Town generation
// instantiates a subclass per building kind; the house editor still drives a
// `HouseBuilding` directly.

enum class BuildingKind : uint8_t {
    Centerpiece = 0,   // well, market, campfire, statue (legacy kind 0)
    House       = 1,   // residential — has rooms with furniture
    Farm        = 2,   // fenced crop plot (legacy kind 2)
    Pub         = 3,   // tavern with bar area + dining hall + a guest room
    Blacksmith  = 4,   // forge + workshop + a small living quarters
    MageTower   = 5,   // multi-storey tower: alchemy lab, library, bedroom
    Stable      = 6,   // open barn of horse stalls, hay + trough, fenced paddock
    Chapel      = 7,   // a tall single nave: rows of pews facing a stone altar
    Apothecary  = 8,   // a herbalist's shop: counter + shelves, with living quarters
    Bakery      = 9,   // a baker's shop: wood-fired oven, counter, bread shelves
    Watchtower  = 10,  // a tall narrow stone guard tower with a flat lookout top
    Count
};

// Every room a building generator emits. The placer consumes RoomType to pick
// appropriate furniture; new room types can be appended without breaking the
// existing placement rules (any RoomType the placer doesn't know about falls
// through to generic LivingRoom-style placement).
enum class RoomType : uint8_t {
    None = 0,
    LivingRoom,      // sofas, tables, chairs, rugs
    Kitchen,         // cooker, sink, kitchen counter, crockery
    Bedroom,         // bed, wardrobe, side table with lantern
    Study,           // bookshelves, desk, chair
    DiningHall,      // long tables + chairs (pub common room)
    BarArea,         // bar counter, bar stools, bottle shelves
    Forge,           // anvil, forge, water trough
    Workshop,        // workbench, tool racks, bookshelf
    AlchemyLab,      // cauldron, alchemy table, bookshelf
    Library,         // wall-to-wall bookshelves, reading desk
    Hallway,         // wall lanterns only
    Stable,          // horse stalls, hay and a water trough (built as block détail)
    Chapel,          // rows of pews facing an altar (built as block détail)
    Apothecary,      // herbalist's shop: counter, shelves, bubbling cauldron
    Bakery,          // baker's shop: oven (a Forge prop), counters and bread shelves
    Count
};

// One semantic interior region within a building's block grid (after rotation).
// All coordinates are in the building's local grid space (the same space as
// `TownBuilding::blocks`). The room spans [x0, x1] × [z0, z1] inclusively at
// floor height `floorY`, with `ceilingY` the y of the storey above (or the
// roof). The placer scans this slab for usable spots.
struct Room {
    int x0 = 0, z0 = 0;
    int x1 = 0, z1 = 0;
    int floorY = 0;
    int ceilingY = 0;
    RoomType type = RoomType::None;
};

// Abstract building generator. A concrete subclass is parameterised at
// construction (templ/material/etc.) and produces a tight, world-aligned block
// grid plus the room list when `generate()` is called. `seed` lets the
// subclass derive deterministic variation (which side has the kitchen, etc.).
//
// Implementation contract:
//   - `blocks` is sized to dimX*dimY*dimZ, index ((y*dimZ)+z)*dimX+x.
//   - The grid is tight-cropped to the non-air bounds; dimY includes the roof.
//   - doorDX/doorDZ is the unit outward normal of the front door (one of the
//     four cardinal directions). (0,0) means "no door".
//   - Rooms may be left empty for kinds that have no interior furniture (Farm,
//     Centerpiece). For multi-room kinds, every interior cell should be in
//     exactly one Room so the placer sees every floor tile.
class Building {
public:
    virtual ~Building() = default;
    virtual BuildingKind kind() const = 0;

    virtual void generate(uint32_t seed,
                          std::vector<uint8_t>& blocks,
                          std::vector<Room>& rooms,
                          int& dimX, int& dimY, int& dimZ,
                          int& doorDX, int& doorDZ) = 0;

    // Local-grid cell (pre-rotation, post-crop) of the front-door cut, recorded
    // by generate() so callers don't have to recover it by scanning the blocks.
    // That scan is unreliable for composite (L/T/U/+/H/Z/E) footprints, where the
    // widest gap in the front wall row is usually a set-back or courtyard mouth
    // rather than the doorway. bakeBuilding() rotates this into world placement.
    int doorCellX = 0, doorCellZ = 0;
};

// --- Concrete buildings ------------------------------------------------------

// A residential house. Driven by the same `templateType`/`roofType`/`material`
// triplet the original `generateHouseGrid` used, so the house editor still
// addresses it directly. Templates 0-9 are unchanged in identity:
//   0 Bungalow, 1 Two-Story, 2 Cottage, 3 Tower, 4 Cabin,
//   5 Longhouse, 6 Townhouse, 7 Manor, 8 Hall, 9 Keep
class HouseBuilding : public Building {
public:
    int templateType = 0;
    int roofType     = 1;
    int material     = 0;

    HouseBuilding() = default;
    HouseBuilding(int t, int r, int m) : templateType(t), roofType(r), material(m) {}

    BuildingKind kind() const override { return BuildingKind::House; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// A tavern: a tall single-storey common room with a bar counter along the back
// wall, a row of dining tables, and an attached small bedroom for travellers.
class PubBuilding : public Building {
public:
    int material = 1;   // cottage-like timber and red roof by default
    int roofType = 1;   // gabled
    PubBuilding() = default;
    explicit PubBuilding(int mat, int roof = 1) : material(mat), roofType(roof) {}
    BuildingKind kind() const override { return BuildingKind::Pub; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// A blacksmith: open-fronted forge bay (anvil + forge) plus an enclosed
// workshop/living room with a bedroom alcove.
class BlacksmithBuilding : public Building {
public:
    int material = 2;   // stone walls by default
    int roofType = 2;   // hipped
    BlacksmithBuilding() = default;
    explicit BlacksmithBuilding(int mat, int roof = 2) : material(mat), roofType(roof) {}
    BuildingKind kind() const override { return BuildingKind::Blacksmith; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// A wizard's tower: three or four cylindrical storeys (alchemy lab, library,
// bedroom, conical roof). Material defaults to stone-with-blue-roof.
class MageTowerBuilding : public Building {
public:
    int material = 3;   // navy roof manor palette
    int floors   = 3;   // 3 or 4 storeys
    MageTowerBuilding() = default;
    explicit MageTowerBuilding(int mat, int f = 3) : material(mat), floors(f) {}
    BuildingKind kind() const override { return BuildingKind::MageTower; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// A stable: a long open barn whose single hall is filled with horse stalls —
// wood plank dividers, hay piles and a water trough — lit by a hanging
// glowstone lantern. A fenced paddock is dropped outside by the prop system.
class StableBuilding : public Building {
public:
    int material = 6;   // forest-timber palette by default
    int roofType = 1;   // gabled
    StableBuilding() = default;
    explicit StableBuilding(int mat, int roof = 1) : material(mat), roofType(roof) {}
    BuildingKind kind() const override { return BuildingKind::Stable; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// A chapel: a tall single-nave hall with two banks of pews running down a
// central aisle toward a raised stone altar lit by a candle at the far end.
class ChapelBuilding : public Building {
public:
    int material = 2;   // stone walls by default
    int roofType = 4;   // steep gable — a fitting spire-like silhouette
    ChapelBuilding() = default;
    explicit ChapelBuilding(int mat, int roof = 4) : material(mat), roofType(roof) {}
    BuildingKind kind() const override { return BuildingKind::Chapel; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// An apothecary: a herbalist's shop — a front counter, wall shelves and a
// bubbling cauldron (all from the existing prop set) plus a small back room of
// living quarters.
class ApothecaryBuilding : public Building {
public:
    int material = 8;   // autumn-plum palette by default
    int roofType = 1;   // gabled
    ApothecaryBuilding() = default;
    explicit ApothecaryBuilding(int mat, int roof = 1) : material(mat), roofType(roof) {}
    BuildingKind kind() const override { return BuildingKind::Apothecary; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// A bakery: a shop with a wood-fired oven, a serving counter and bread shelves
// up front, and a small back room for the baker.
class BakeryBuilding : public Building {
public:
    int material = 1;   // warm cottage palette by default
    int roofType = 1;   // gabled
    BakeryBuilding() = default;
    explicit BakeryBuilding(int mat, int roof = 1) : material(mat), roofType(roof) {}
    BuildingKind kind() const override { return BuildingKind::Bakery; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// A watchtower: a tall, narrow stone tower with a flat lookout top — a guard
// post overlooking the town. Plain floors with a watch room at the summit.
class WatchtowerBuilding : public Building {
public:
    int material = 2;   // stone walls by default
    int floors   = 4;   // 3..5 storeys
    WatchtowerBuilding() = default;
    explicit WatchtowerBuilding(int mat, int f = 4) : material(mat), floors(f) {}
    BuildingKind kind() const override { return BuildingKind::Watchtower; }
    void generate(uint32_t seed,
                  std::vector<uint8_t>& blocks,
                  std::vector<Room>& rooms,
                  int& dimX, int& dimY, int& dimZ,
                  int& doorDX, int& doorDZ) override;
};

// --- Helpers shared by building generators -----------------------------------

// Painted block colour for material index (0..9). Returns {wall, roof, accent}.
// Used by every Building subclass so they share the same palette set.
struct MaterialPalette {
    BlockType wall;
    BlockType roof;
    BlockType accent;     // chimneys, gable trim, etc.
    BlockType wallDark;   // darker wall shade — used for corner posts
    bool      stone = false;   // masonry material — walls left plain (no corner texturing)
};
MaterialPalette materialPalette(int material);

// Rotates `src` (`sx`*`sy`*`sz`, index ((y*sz)+z)*sx+x) into `dst` by quadrant
// `q` (0..3), and the same rotation is applied to each Room in `srcRooms` ->
// `dstRooms`. Output dims set in `dx`/`dz`. Used by town.cpp after generation.
void rotateBuilding(int sx, int sy, int sz,
                    const std::vector<uint8_t>& src,
                    const std::vector<Room>& srcRooms,
                    int q,
                    std::vector<uint8_t>& dst,
                    std::vector<Room>& dstRooms,
                    int& dx, int& dz);
