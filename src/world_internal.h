#pragma once
// Shared internals between world.cpp (live chunk lighting, meshing, rendering
// and the World container) and world_gen.cpp (procedural terrain + decoration).
// world.h stays the public surface; this header is private to the world* TUs.
#include "world.h"   // Chunk, BlockType
#include <cstdint>

// The waterline Y. Shared by terrain generation and the world-map renderer.
constexpr int SEA_LEVEL = 64;

// The dominant biome at a column. Internal to the world* TUs — world.h's public
// SurfaceSample reports the biome as a plain int; this enum is what generation
// and the world-map colouring reason about.
enum class Biome : uint8_t { Plains=0, Forest=1, Desert=2, Mountains=3, Tundra=4, Savanna=5, Jungle=6 };

// True for any tinted leaf block — a canopy test shared by the world* TUs (the
// tree/decoration placers in world_gen.cpp consult it when stacking foliage).
inline bool isAnyLeaves(BlockType b) {
    return b == BlockType::Leaves || b == BlockType::LeavesOrange ||
           b == BlockType::LeavesRed || b == BlockType::LeavesPink;
}

// Per-column blended surface height + dominant biome — the output of the shared
// terrain sampler computeColumn(). generateChunk() runs the full per-chunk
// generation pipeline (height → density → decoration). Both are defined in
// world_gen.cpp; world.cpp's worker thread and map renderer call across to them.
struct ColumnInfo { float surfH; Biome biome; };
ColumnInfo computeColumn(float wx, float wz);
void generateChunk(Chunk* c);
