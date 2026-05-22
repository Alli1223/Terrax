#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// --- Procedural vegetation ---------------------------------------------------
// Detailed ground-cover plants — ferns, grasses, reeds, flowers, logs and more.
// Each type is baked into per-chunk geometry (vertex-coloured 3D triangles, no
// atlas) so a whole chunk of vegetation is still a single draw call. Generation
// is deterministic from world position, so chunk rebuilds are stable.

// One vertex of baked vegetation geometry.
struct VegVertex {
    float x, y, z;        // world position
    float nx, ny, nz;     // normal (lighting is two-sided in the shader)
    float r, g, b;        // vertex colour
    float sway;           // 0 at the root .. 1 at the tip — wind / bend weight
    float skyLight;       // baked sky light  [0,1]
    float blockLight;     // baked block light [0,1]
};

enum class VegetationType : uint8_t {
    None = 0,
    GrassTuft,        // a clump of short voxel grass blades
    TallGrass,        // taller long grass
    FrostGrass,       // pale cold-weather grass
    Fern,             // arching voxel fronds
    FernLarge,        // big jungle / forest fern
    Reeds,            // tall straight reeds
    Cattail,          // reeds with a brown seed head
    Flower,           // stem, leaf and a blocky petalled head
    FlowerCluster,    // a few flowers together
    TallFlower,       // a tall flowering stalk
    Clover,           // tiny ground cover
    Sprout,           // small fresh shoots
    MushroomCluster,  // a few small capped mushrooms
    Toadstool,        // one big spotted mushroom
    Bush,             // a leafy voxel shrub
    BerryBush,        // a shrub dotted with berries
    DeadBush,         // bare twiggy scrub
    DryShrub,         // sparse desert / mountain scrub
    FallenLog,        // a mossy log resting on the ground
    TreeStump,        // a cut tree stump
    Sapling,          // a young voxel tree
    Pebbles,          // a scatter of small rocks
};

// Catalogue + geometry generators for the vegetation system.
class Vegetation {
public:
    // Choose a vegetation type for a surface column in `biome` (terrain-oracle
    // biome id). Returns None for bare ground. Deterministic per (wx, wz).
    static VegetationType pick(int biome, int wx, int wz);

    // Append the geometry of one `type` instance to `out`. The plant is rooted
    // at world (wx, baseY, wz) — baseY is the open block it grows from. `seed`
    // varies the instance; sky/blk are the baked light at the column.
    static void emit(std::vector<VegVertex>& out, VegetationType type,
                     float wx, float baseY, float wz,
                     float skyLight, float blockLight, uint32_t seed);
};
