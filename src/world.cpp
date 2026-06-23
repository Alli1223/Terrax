#include "world.h"
#include "town.h"
#include "noise.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>
#include <chrono>
#include "world_internal.h"


// ---- Chunk ----

Chunk::Chunk(ChunkPos p, bool isServer) : pos(p), isServer(isServer) {
    blocks.fill(BlockType::Air);
    lightMap.fill(0);
}

// ---- Light accessors ----

static int lightIdx(int x, int y, int z) {
    return y * CHUNK_SIZE * CHUNK_SIZE + z * CHUNK_SIZE + x;
}

uint8_t Chunk::getSkyLight(int x, int y, int z) const {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 0;
    return (lightMap[lightIdx(x,y,z)] >> 4) & 0xF;
}
uint8_t Chunk::getBlockLight(int x, int y, int z) const {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 0;
    return lightMap[lightIdx(x,y,z)] & 0xF;
}
void Chunk::setSkyLight(int x, int y, int z, uint8_t v) {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return;
    int i = lightIdx(x,y,z);
    lightMap[i] = (uint8_t)((lightMap[i] & 0x0F) | (v << 4));
}
void Chunk::setBlockLight(int x, int y, int z, uint8_t v) {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return;
    int i = lightIdx(x,y,z);
    lightMap[i] = (uint8_t)((lightMap[i] & 0xF0) | (v & 0xF));
}

// ---- Flood-fill light propagation ----

void Chunk::computeLight() {
    lightMap.fill(0);

    struct LightNode { uint8_t x, y, z, val; };
    // val encoding: bit 7 = channel (0=sky, 1=block), bits 3:0 = light level
    std::queue<LightNode> q;

    // ── Sky light: scan each column top-down through air and water ───────
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                BlockType bt = get(x, y, z);
                if (bt != BlockType::Air && bt != BlockType::Water &&
                    bt != BlockType::Glass && !isWheatBlock(bt)) break;   // wheat is transparent
                setSkyLight(x, y, z, 15);
                q.push({(uint8_t)x, (uint8_t)y, (uint8_t)z, 15u});
            }
        }
    }

    // ── Block light: seed emitters (Glowstone full, Lantern slightly softer) ──
    // Linear scan of the raw block array: the vast majority of chunks contain no
    // emitters, so an early-continue over a flat array is far cheaper than the
    // old triple-nested get() (65k bounds-checked accessor calls per chunk).
    const int CS2 = CHUNK_SIZE * CHUNK_SIZE;
    for (int i = 0; i < (int)blocks.size(); i++) {
        BlockType b = blocks[i];
        if (b != BlockType::Glowstone && b != BlockType::Lantern) continue;
        int y = i / CS2;
        int z = (i - y * CS2) / CHUNK_SIZE;
        int x = i - y * CS2 - z * CHUNK_SIZE;
        uint8_t emit = (b == BlockType::Glowstone) ? 15u : 14u;
        setBlockLight(x, y, z, emit);
        q.push({(uint8_t)x, (uint8_t)y, (uint8_t)z, (uint8_t)(0x80 | emit)}); // block channel
    }

    // ── BFS flood fill ────────────────────────────────────────────────────
    static const int8_t DD[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    while (!q.empty()) {
        auto [nx, ny, nz, nval] = q.front(); q.pop();
        bool isBlock = (nval & 0x80) != 0;
        uint8_t level = nval & 0x0F;
        if (level <= 1) continue;
        uint8_t next = level - 1;

        for (auto& d : DD) {
            int bx = (int)nx + d[0], by = (int)ny + d[1], bz = (int)nz + d[2];
            if (bx < 0 || bx >= CHUNK_SIZE || by < 0 || by >= CHUNK_HEIGHT || bz < 0 || bz >= CHUNK_SIZE) continue;
            BlockType nb = get(bx, by, bz);
            if (nb != BlockType::Air && nb != BlockType::Water &&
                nb != BlockType::Glass && !isWheatBlock(nb)) continue;   // light passes through wheat
            uint8_t cur = isBlock ? getBlockLight(bx,by,bz) : getSkyLight(bx,by,bz);
            if (next > cur) {
                isBlock ? setBlockLight(bx,by,bz,next) : setSkyLight(bx,by,bz,next);
                q.push({(uint8_t)bx, (uint8_t)by, (uint8_t)bz, (uint8_t)((isBlock ? 0x80 : 0) | next)});
            }
        }
    }
}

Chunk::~Chunk() {
    if (!isServer) {
        if (vao)        { glDeleteVertexArrays(1, &vao);        glDeleteBuffers(1, &vbo);        }
        if (waterVao)   { glDeleteVertexArrays(1, &waterVao);   glDeleteBuffers(1, &waterVbo);   }
        if (foliageVao) { glDeleteVertexArrays(1, &foliageVao); glDeleteBuffers(1, &foliageVbo); }
        if (glassVao)   { glDeleteVertexArrays(1, &glassVao);   glDeleteBuffers(1, &glassVbo);   }
    }
}

BlockType Chunk::get(int x, int y, int z) const {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE)
        return BlockType::Air;
    return blocks[y * CHUNK_SIZE * CHUNK_SIZE + z * CHUNK_SIZE + x];
}

void Chunk::set(int x, int y, int z, BlockType t) {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return;
    blocks[y * CHUNK_SIZE * CHUNK_SIZE + z * CHUNK_SIZE + x] = t;
}

static bool isOpaque(BlockType b) {
    return b != BlockType::Air && b != BlockType::Water && b != BlockType::Glass &&
           !isWheatBlock(b);   // wheat is foliage, not a solid face
}

static TileID getTile(BlockType bt, int face) {
    switch (bt) {
        case BlockType::Grass:
            if (face == 2) return TileID::GrassTop;
            if (face == 3) return TileID::Dirt;
            return TileID::GrassSide;
        case BlockType::Dirt:   return TileID::Dirt;
        case BlockType::Stone:  return TileID::Stone;
        case BlockType::Wood:
            return (face == 2 || face == 3) ? TileID::WoodTop : TileID::WoodSide;
        case BlockType::Leaves:       return TileID::Leaves;
        case BlockType::LeavesOrange: return TileID::LeavesOrange;
        case BlockType::LeavesRed:    return TileID::LeavesRed;
        case BlockType::LeavesPink:   return TileID::LeavesPink;
        case BlockType::Sand:   return TileID::Sand;
        case BlockType::Gravel: return TileID::Gravel;
        case BlockType::Snow:   return TileID::Snow;
        case BlockType::Cactus:
            return (face == 2 || face == 3) ? TileID::CactusTop : TileID::CactusSide;
        case BlockType::Sandstone: return TileID::Sandstone;
        case BlockType::Ice:       return TileID::Ice;
        case BlockType::Glowstone: return TileID::Glowstone;
        case BlockType::Lantern:   return TileID::Lantern;
        case BlockType::Farmland:  return TileID::Farmland;
        case BlockType::Water:     return TileID::Water;
        case BlockType::Glass:     return TileID::Glass;
        default: {
            int pidx = (int)bt - (int)BlockType::PaintFirst;
            if (pidx >= 0 && pidx < PAINT_COUNT)
                return (TileID)((int)TileID::PaintFirst + pidx);
            return TileID::Stone;
        }
    }
}

void Chunk::buildMesh(World* world) {
    std::vector<Vertex> verts;
    std::vector<Vertex> wverts;
    std::vector<Vertex> gverts;
    verts.reserve(4096);
    wverts.reserve(512);
    gverts.reserve(256);

    struct NeighborData {
        ChunkPos pos;
        const std::array<BlockType, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE>* blocks;
        const std::array<uint8_t,   CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE>* lights;
    };
    std::vector<NeighborData> neighbors;
    {
        std::lock_guard<std::mutex> lock(world->chunksMutex);
        const ChunkPos ncps[4] = {{pos.x-1,pos.z},{pos.x+1,pos.z},{pos.x,pos.z-1},{pos.x,pos.z+1}};
        for (const auto& ncp : ncps) {
            auto it = world->chunks.find(ncp);
            if (it != world->chunks.end() && (it->second->state != ChunkState::Empty && it->second->state != ChunkState::Generating)) {
                neighbors.push_back({ncp, &it->second->blocks, &it->second->lightMap});
            }
        }
    }

    auto chunkCoord = [](int w, int cs) -> int {
        return (w < 0 && w % cs != 0) ? w/cs - 1 : w/cs;
    };

    auto worldGet = [&](int wx, int wy, int wz) -> BlockType {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return BlockType::Air;
        int cx = chunkCoord(wx, CHUNK_SIZE);
        int cz = chunkCoord(wz, CHUNK_SIZE);
        if (cx == pos.x && cz == pos.z)
            return get(wx - pos.x * CHUNK_SIZE, wy, wz - pos.z * CHUNK_SIZE);
        for (const auto& n : neighbors) {
            if (n.pos.x == cx && n.pos.z == cz) {
                int lx = wx - cx * CHUNK_SIZE, lz = wz - cz * CHUNK_SIZE;
                return (*n.blocks)[wy * CHUNK_SIZE * CHUNK_SIZE + lz * CHUNK_SIZE + lx];
            }
        }
        return BlockType::Air;
    };

    // Returns raw light byte (sky nibble | block nibble) for an air-side world block.
    // Falls back to full sky light for unloaded neighbours so borders don't go black.
    auto worldGetLight = [&](int wx, int wy, int wz) -> uint8_t {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return 0xF0; // treat out-of-bounds as sky=15
        int cx = chunkCoord(wx, CHUNK_SIZE);
        int cz = chunkCoord(wz, CHUNK_SIZE);
        if (cx == pos.x && cz == pos.z) {
            int li = lightIdx(wx - pos.x * CHUNK_SIZE, wy, wz - pos.z * CHUNK_SIZE);
            return lightMap[li];
        }
        for (const auto& n : neighbors) {
            if (n.pos.x == cx && n.pos.z == cz && n.lights) {
                int lx = wx - cx * CHUNK_SIZE, lz = wz - cz * CHUNK_SIZE;
                return (*n.lights)[wy * CHUNK_SIZE * CHUNK_SIZE + lz * CHUNK_SIZE + lx];
            }
        }
        return 0xF0; // unloaded neighbour: assume open sky
    };

    // Shore distance sampled at an exact xz position so adjacent blocks' shared
    // edge vertices always evaluate identically, preventing cracks at block seams.
    auto shoreDistAt = [&](int vx, int vy, int vz) -> float {
        const int R = 6;
        int minDistSq = (R + 1) * (R + 1);
        for (int dz2 = -R; dz2 <= R; dz2++) {
            for (int dx2 = -R; dx2 <= R; dx2++) {
                if (dx2 == 0 && dz2 == 0) continue;
                int dSq = dx2*dx2 + dz2*dz2;
                if (dSq >= minDistSq) continue;
                BlockType nb = worldGet(vx + dx2, vy, vz + dz2);
                if (nb != BlockType::Air && nb != BlockType::Water) { minDistSq = dSq; continue; }
                nb = worldGet(vx + dx2, vy + 1, vz + dz2);
                if (nb != BlockType::Air && nb != BlockType::Water) minDistSq = dSq;
            }
        }
        return std::min(sqrtf((float)minDistSq) / (float)R, 1.0f);
    };

    // Vertical water depth (sea surface → floor) sampled at an exact xz so that
    // shared edge vertices always agree, just like shoreDistAt. Counts water
    // blocks straight down, normalised over WATER_OPAQUE_DEPTH; the water shader
    // fades the surface to opaque as this nears 1, hiding the floor of deep
    // ocean while leaving shallows (shore, rivers, ponds) clear.
    auto waterDepthAt = [&](int vx, int vy, int vz) -> float {
        const int WATER_OPAQUE_DEPTH = 26;
        int d = 0;
        for (int k = 0; k < WATER_OPAQUE_DEPTH; k++) {
            if (worldGet(vx, vy - k, vz) != BlockType::Water) break;
            d++;
        }
        return std::min((float)d / (float)WATER_OPAQUE_DEPTH, 1.0f);
    };

    static const int   FDX[6] = {1,-1, 0, 0, 0, 0};
    static const int   FDY[6] = {0, 0, 1,-1, 0, 0};
    static const int   FDZ[6] = {0, 0, 0, 0, 1,-1};
    static const float FNX[6] = {1,-1, 0, 0, 0, 0};
    static const float FNY[6] = {0, 0, 1,-1, 0, 0};
    static const float FNZ[6] = {0, 0, 0, 0, 1,-1};
    // Face shading: top=1.0, bottom=0.5, sides=0.8 (classic voxel directional shading)
    static const float SHADE[6] = {0.8f, 0.8f, 1.0f, 0.5f, 0.8f, 0.8f};
    static const float LU[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    static const float LV[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    static const float FV[6][4][3] = {
        {{1,0,1},{1,1,1},{1,1,0},{1,0,0}}, {{0,0,0},{0,1,0},{0,1,1},{0,0,1}},
        {{0,1,0},{1,1,0},{1,1,1},{0,1,1}}, {{0,0,1},{1,0,1},{1,0,0},{0,0,0}},
        {{0,0,1},{0,1,1},{1,1,1},{1,0,1}}, {{1,0,0},{1,1,0},{0,1,0},{0,0,0}},
    };

    auto pushQuad = [](std::vector<Vertex>& dst, const Vertex q[4]) {
        dst.push_back(q[0]); dst.push_back(q[2]); dst.push_back(q[1]);
        dst.push_back(q[0]); dst.push_back(q[3]); dst.push_back(q[2]);
    };

    for (int y = 0; y < CHUNK_HEIGHT; y++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            for (int x = 0; x < CHUNK_SIZE; x++) {
                BlockType bt = get(x, y, z);
                if (bt == BlockType::Air || isWheatBlock(bt)) continue;   // wheat → foliage mesh, no cube

                int wx = pos.x * CHUNK_SIZE + x;
                int wz = pos.z * CHUNK_SIZE + z;
                bool isWater = (bt == BlockType::Water);
                bool isGlass = (bt == BlockType::Glass);

                for (int face = 0; face < 6; face++) {
                    int ax = wx + FDX[face], ay = y + FDY[face], az = wz + FDZ[face];
                    BlockType adj = worldGet(ax, ay, az);

                    if (isWater) {
                        // Water: only expose faces adjacent to Air; skip bottom face
                        if (face == 3) continue; // bottom face (–Y): never seen
                        if (adj != BlockType::Air) continue;
                    } else if (isGlass) {
                        // Glass: hide faces shared with other glass or behind solids
                        if (adj == BlockType::Glass || isOpaque(adj)) continue;
                    } else {
                        // Opaque: expose faces adjacent to Air OR Water so seafloor shows through
                        if (isOpaque(adj)) continue;
                    }

                    uint8_t rawLight = worldGetLight(ax, ay, az);
                    float shade = SHADE[face];
                    float skyL   = ((rawLight >> 4) & 0xF) / 15.0f * shade;
                    float blockL = (rawLight & 0xF)        / 15.0f * shade;
                    if (bt == BlockType::Glowstone || bt == BlockType::Lantern) blockL = shade;

                    TileID tile = getTile(bt, face);
                    float u0, v0, u1, v1;
                    tileUV(tile, u0, v0, u1, v1);

                    // Snowable detection — only +Y faces of painted (building)
                    // blocks qualify, and only when the column above is open to
                    // sky. The air-column count discriminates roof tops (sky
                    // above for many cells) from indoor floor tiles (a ceiling
                    // 5-11 cells up). Threshold 16 covers even the tall Hall
                    // template's 11-block ceiling without flagging it.
                    float snowable = 0.0f;
                    if (face == 2 && (int)bt >= (int)BlockType::PaintFirst) {
                        int airAbove = 0;
                        for (int dy = 1; dy <= 16; dy++) {
                            BlockType up = worldGet(wx, y + dy, wz);
                            if (up != BlockType::Air) break;
                            airAbove = dy;
                        }
                        if (airAbove >= 12) snowable = 1.0f;
                    }

                    Vertex quad[4];
                    for (int vi = 0; vi < 4; vi++) {
                        float sd = (isWater && face == 2)
                            ? shoreDistAt(wx + (int)FV[face][vi][0], y, wz + (int)FV[face][vi][2])
                            : 0.0f;
                        float wd = (isWater && face == 2)
                            ? waterDepthAt(wx + (int)FV[face][vi][0], y, wz + (int)FV[face][vi][2])
                            : 0.0f;
                        quad[vi] = {
                            (float)wx + FV[face][vi][0], (float)y + FV[face][vi][1], (float)wz + FV[face][vi][2],
                            FNX[face], FNY[face], FNZ[face],
                            u0 + LU[vi] * (u1 - u0), v0 + LV[vi] * (v1 - v0),
                            (float)bt, skyL, blockL, sd, snowable, wd
                        };
                    }
                    pushQuad(isWater ? wverts : (isGlass ? gverts : verts), quad);
                }
            }
        }
    }

    // --- Vegetation mesh ---
    // Detailed procedural ground cover (ferns, grasses, logs, ...) — see
    // vegetation.cpp. Replaces the old cross-quad grass/flowers; baked
    // per-chunk so a whole chunk of vegetation is still a single draw call.
    std::vector<VegVertex> fverts;
    fverts.reserve(4096);

    for (int z = 0; z < CHUNK_SIZE; z++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int topY = -1;
            BlockType topBlock = BlockType::Air;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                BlockType b = get(x, y, z);
                if (b != BlockType::Air) { topY = y; topBlock = b; break; }
            }
            if (topY < 0) continue;

            int wx = pos.x * CHUNK_SIZE + x;
            int wz = pos.z * CHUNK_SIZE + z;

            // Crop wheat renders as swaying voxel stalks rooted in its own cell
            // (on the tilled Farmland below), by growth stage.
            if (isWheatBlock(topBlock)) {
                int stage = (topBlock == BlockType::WheatYoung) ? 0
                          : (topBlock == BlockType::WheatTall)  ? 1 : 2;
                float skyW = getSkyLight (x, topY, z) / 15.0f;
                float blkW = getBlockLight(x, topY, z) / 15.0f;
                uint32_t ws = (uint32_t)wx * 73856093u ^ (uint32_t)wz * 19349663u;
                Vegetation::emitWheat(fverts, stage, (float)wx, (float)topY, (float)wz, skyW, blkW, ws);
                continue;
            }

            // Wild vegetation only roots in natural ground cover — never on snow,
            // and never on tilled Farmland (so crop fields stay clear).
            if (topBlock != BlockType::Grass && topBlock != BlockType::Dirt &&
                topBlock != BlockType::Sand) continue;
            int fy = topY + 1;
            if (fy >= CHUNK_HEIGHT || get(x, fy, z) != BlockType::Air) continue;

            VegetationType vt =
                Vegetation::pick(sampleSurface(wx, wz).biome, wx, wz);
            if (vt == VegetationType::None) continue;

            float skyL = getSkyLight (x, fy, z) / 15.0f;
            float blkL = getBlockLight(x, fy, z) / 15.0f;
            uint32_t seed = (uint32_t)wx * 73856093u ^ (uint32_t)wz * 19349663u;
            Vegetation::emit(fverts, vt, (float)wx, (float)fy, (float)wz,
                             skyL, blkL, seed);
        }
    }

    {
        std::lock_guard<std::mutex> lock(meshMutex);
        meshData     = std::move(verts);
        waterData    = std::move(wverts);
        foliageData  = std::move(fverts);
        glassData    = std::move(gverts);
    }
    neighborsAtMeshTime = (int)neighbors.size();
    state = ChunkState::MeshReady;
}

static void setupVertexAttribs() {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nx));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, materialID));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, skyLight));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, blockLight));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, shoreDistance));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, snowable));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, waterDepth));
    glEnableVertexAttribArray(8);
}

static void setupVegVertexAttribs() {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VegVertex), (void*)offsetof(VegVertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VegVertex), (void*)offsetof(VegVertex, nx));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(VegVertex), (void*)offsetof(VegVertex, r));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(VegVertex), (void*)offsetof(VegVertex, sway));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(VegVertex), (void*)offsetof(VegVertex, skyLight));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(VegVertex), (void*)offsetof(VegVertex, blockLight));
    glEnableVertexAttribArray(5);
}

void Chunk::uploadMesh() {
    if (isServer) return;
    std::lock_guard<std::mutex> lock(meshMutex);

    // Opaque mesh
    if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, meshData.size() * sizeof(Vertex), meshData.data(), GL_STATIC_DRAW);
    setupVertexAttribs();
    vertexCount = (int)meshData.size();
    meshData.clear();

    // Water mesh
    if (!waterVao) { glGenVertexArrays(1, &waterVao); glGenBuffers(1, &waterVbo); }
    glBindVertexArray(waterVao);
    glBindBuffer(GL_ARRAY_BUFFER, waterVbo);
    glBufferData(GL_ARRAY_BUFFER, waterData.size() * sizeof(Vertex), waterData.data(), GL_STATIC_DRAW);
    setupVertexAttribs();
    waterVertexCount = (int)waterData.size();
    waterData.clear();

    // Vegetation mesh
    if (!foliageData.empty()) {
        if (!foliageVao) { glGenVertexArrays(1, &foliageVao); glGenBuffers(1, &foliageVbo); }
        glBindVertexArray(foliageVao);
        glBindBuffer(GL_ARRAY_BUFFER, foliageVbo);
        glBufferData(GL_ARRAY_BUFFER, foliageData.size() * sizeof(VegVertex), foliageData.data(), GL_STATIC_DRAW);
        setupVegVertexAttribs();
        foliageVertexCount = (int)foliageData.size();
        foliageData.clear();
    }

    // Glass mesh
    if (!glassData.empty()) {
        if (!glassVao) { glGenVertexArrays(1, &glassVao); glGenBuffers(1, &glassVbo); }
        glBindVertexArray(glassVao);
        glBindBuffer(GL_ARRAY_BUFFER, glassVbo);
        glBufferData(GL_ARRAY_BUFFER, glassData.size() * sizeof(Vertex), glassData.data(), GL_STATIC_DRAW);
        setupVertexAttribs();
        glassVertexCount = (int)glassData.size();
        glassData.clear();
    } else {
        glassVertexCount = 0;
    }

    glBindVertexArray(0);
    state = ChunkState::Ready;
}

// Draw whatever mesh has been uploaded. We gate on the GL buffers existing
// (vao != 0, vertexCount > 0) rather than state == Ready: uploadMesh reuses the
// same vao/vbo and only swaps the contents at the end, so the previously-built
// mesh stays valid while a chunk is being RE-meshed (e.g. after a block change /
// relight). Skipping it during the rebuild made edited chunks blink out for a
// frame — the flicker seen when farmers change crop blocks.
void Chunk::draw() const {
    if (isServer || vao == 0 || vertexCount == 0) return;
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

void Chunk::drawWater() const {
    if (isServer || waterVao == 0 || waterVertexCount == 0) return;
    glBindVertexArray(waterVao);
    glDrawArrays(GL_TRIANGLES, 0, waterVertexCount);
    glBindVertexArray(0);
}

void Chunk::drawFoliage() const {
    if (isServer || foliageVao == 0 || foliageVertexCount == 0) return;
    glBindVertexArray(foliageVao);
    glDrawArrays(GL_TRIANGLES, 0, foliageVertexCount);
    glBindVertexArray(0);
}

void Chunk::drawGlass() const {
    if (isServer || glassVao == 0 || glassVertexCount == 0) return;
    glBindVertexArray(glassVao);
    glDrawArrays(GL_TRIANGLES, 0, glassVertexCount);
    glBindVertexArray(0);
}

// ---- World map helpers ----

static void blockToMapRGB(BlockType bt, int y, uint8_t& r, uint8_t& g, uint8_t& b) {
    int ri, gi, bi;
    switch (bt) {
        case BlockType::Water:     ri=22;  gi=90;  bi=200; break;
        case BlockType::Ice:       ri=178; gi=210; bi=240; break;
        case BlockType::Grass:     ri=67;  gi=178; bi=35;  break;
        case BlockType::Dirt:      ri=130; gi=88;  bi=50;  break;
        case BlockType::Sand:      ri=220; gi=198; bi=115; break;
        case BlockType::Gravel:    ri=138; gi=136; bi=130; break;
        case BlockType::Snow:      ri=238; gi=242; bi=255; break;
        case BlockType::Farmland:  ri=104; gi=70;  bi=42;  break;
        case BlockType::WheatYoung:ri=120; gi=150; bi=58;  break;
        case BlockType::WheatTall: ri=170; gi=160; bi=70;  break;
        case BlockType::WheatRipe: ri=214; gi=180; bi=82;  break;
        case BlockType::Stone:     ri=118; gi=118; bi=125; break;
        case BlockType::Sandstone: ri=198; gi=168; bi=88;  break;
        case BlockType::Leaves:       ri=38;  gi=128; bi=22;  break;
        case BlockType::LeavesOrange: ri=220; gi=105; bi=22;  break;
        case BlockType::LeavesRed:    ri=175; gi=35;  bi=18;  break;
        case BlockType::LeavesPink:   ri=255; gi=165; bi=200; break;
        case BlockType::Wood:      ri=165; gi=110; bi=52;  break;
        case BlockType::Cactus:    ri=30;  gi=108; bi=22;  break;
        case BlockType::Glowstone: ri=255; gi=200; bi=50;  break;
        case BlockType::Lantern:   ri=255; gi=190; bi=90;  break;
        default:                   ri=100; gi=100; bi=100; break;
    }
    if (bt != BlockType::Water) {
        float shade = std::clamp((y - SEA_LEVEL) / 80.0f, -0.25f, 0.35f);
        ri = std::clamp((int)(ri * (1.0f + shade)), 0, 255);
        gi = std::clamp((int)(gi * (1.0f + shade)), 0, 255);
        bi = std::clamp((int)(bi * (1.0f + shade)), 0, 255);
    }
    r = (uint8_t)ri; g = (uint8_t)gi; b = (uint8_t)bi;
}

static void biomeToMapRGB(Biome bm, float surfH, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (surfH < (float)(SEA_LEVEL - 1)) { r = 18; g = 65; b = 175; return; }
    int ri, gi, bi;
    switch (bm) {
        case Biome::Plains:    ri=75;  gi=155; bi=38;  break;
        case Biome::Forest:    ri=28;  gi=105; bi=18;  break;
        case Biome::Desert:    ri=205; gi=185; bi=108; break;
        case Biome::Mountains: ri=135; gi=135; bi=142; break;
        case Biome::Tundra:    ri=195; gi=210; bi=225; break;
        case Biome::Savanna:   ri=155; gi=165; bi=55;  break;
        case Biome::Jungle:    ri=18;  gi=125; bi=12;  break;
        default:               ri=100; gi=100; bi=100; break;
    }
    float shade = std::clamp((surfH - (float)SEA_LEVEL) / 100.0f, -0.25f, 0.35f);
    r = (uint8_t)std::clamp((int)(ri * (1.0f + shade)), 0, 255);
    g = (uint8_t)std::clamp((int)(gi * (1.0f + shade)), 0, 255);
    b = (uint8_t)std::clamp((int)(bi * (1.0f + shade)), 0, 255);
}

void World::fillOpacityVolume(uint8_t* out, int size, int ox, int oy, int oz) const {
    std::fill(out, out + (size_t)size * size * size, (uint8_t)0);
    std::lock_guard<std::mutex> lock(chunksMutex);
    for (const auto& kv : chunks) {
        const ChunkPos& cp = kv.first;
        Chunk* c = kv.second.get();
        if (!c) continue;
        ChunkState st = c->state.load();
        if (st == ChunkState::Empty || st == ChunkState::Generating) continue;
        int cwx = cp.x * CHUNK_SIZE, cwz = cp.z * CHUNK_SIZE;
        if (cwx + CHUNK_SIZE <= ox || cwx >= ox + size) continue;
        if (cwz + CHUNK_SIZE <= oz || cwz >= oz + size) continue;
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            int tx = cwx + lx - ox;
            if (tx < 0 || tx >= size) continue;
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int tz = cwz + lz - oz;
                if (tz < 0 || tz >= size) continue;
                for (int ty = 0; ty < size; ty++) {
                    int wy = oy + ty;
                    if (wy < 0 || wy >= CHUNK_HEIGHT) continue;
                    BlockType b = c->get(lx, wy, lz);
                    if (b != BlockType::Air && b != BlockType::Water &&
                        b != BlockType::Glass && !isWheatBlock(b))   // wheat doesn't occlude point lights
                        out[((size_t)tz * size + ty) * size + tx] = 255;
                }
            }
        }
    }
}

void World::fillMapPixels(uint8_t* rgba, int texSize, float cx, float cz, float worldRadius) const {
    // Snapshot surface data for all ready chunks in one short critical section
    struct ColData { int16_t y; uint8_t bt; };
    std::unordered_map<ChunkPos, std::vector<ColData>, ChunkPosHash> snap;
    {
        std::lock_guard<std::mutex> lock(chunksMutex);
        for (auto& [pos, chunk] : chunks) {
            if (!chunk->surfaceReady) continue;
            auto& cols = snap[pos];
            cols.resize(CHUNK_SIZE * CHUNK_SIZE);
            for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE; i++) {
                cols[i].y  = chunk->surfaceY[i];
                cols[i].bt = chunk->surfaceBT[i];
            }
        }
    }

    float bpp = (worldRadius * 2.0f) / (float)texSize;

    for (int py = 0; py < texSize; py++) {
        for (int px = 0; px < texSize; px++) {
            float wx = cx + (px - texSize * 0.5f) * bpp;
            float wz = cz + (py - texSize * 0.5f) * bpp;
            int   iwx = (int)floorf(wx), iwz = (int)floorf(wz);
            int   chx = (iwx < 0 && iwx % CHUNK_SIZE != 0) ? iwx / CHUNK_SIZE - 1 : iwx / CHUNK_SIZE;
            int   chz = (iwz < 0 && iwz % CHUNK_SIZE != 0) ? iwz / CHUNK_SIZE - 1 : iwz / CHUNK_SIZE;
            int   lx  = iwx - chx * CHUNK_SIZE;
            int   lz  = iwz - chz * CHUNK_SIZE;

            int byteIdx = (py * texSize + px) * 4;
            uint8_t r, g, b;

            auto it = snap.find({chx, chz});
            if (it != snap.end()) {
                const auto& col = it->second[lz * CHUNK_SIZE + lx];
                blockToMapRGB((BlockType)col.bt, (int)col.y, r, g, b);
            } else {
                auto info = computeColumn(wx, wz);
                biomeToMapRGB(info.biome, info.surfH, r, g, b);
                // Darken unexplored areas (fog-of-war)
                r = r * 55 / 100; g = g * 55 / 100; b = b * 55 / 100;
            }

            rgba[byteIdx + 0] = r;
            rgba[byteIdx + 1] = g;
            rgba[byteIdx + 2] = b;
            rgba[byteIdx + 3] = 255;
        }
    }
}

// ---- World ----

World::World(bool isServer) : isServer(isServer) {
    // Leave two cores for the render/main thread and the GL driver so heavy
    // chunk streaming doesn't starve the frame rate.
    int numWorkers = std::max(1, (int)std::thread::hardware_concurrency() - 2);
    for (int i = 0; i < numWorkers; i++) {
        workers.emplace_back(&World::workerThread, this);
    }
}

World::~World() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopWorkers = true;
    }
    cv.notify_all();
    for (auto& t : workers) t.join();
    // Chunks map will be cleared by unique_ptr
}

void World::workerThread() {
    while (true) {
        Chunk* chunkToGen = nullptr;
        Chunk* chunkToMesh = nullptr;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [this] { return stopWorkers || !generationQueue.empty() || !meshingQueue.empty(); });
            if (stopWorkers && generationQueue.empty() && meshingQueue.empty()) return;

            if (!generationQueue.empty()) {
                chunkToGen = generationQueue.front();
                generationQueue.pop();
            } else if (!meshingQueue.empty()) {
                chunkToMesh = meshingQueue.front();
                meshingQueue.pop();
            }
        }

        if (chunkToGen) {
            generateChunk(chunkToGen);
        } else if (chunkToMesh) {
            chunkToMesh->buildMesh(this);
        }
    }
}

void World::generate(int cx, int cz) {
    std::cout << "Starting world generation..." << std::endl;
    update(cx, cz);
    std::cout << "World generation started asynchronously." << std::endl;
}

static constexpr int MAX_UPLOADS_PER_FRAME = 6;

// Chebyshev distance from player chunk
static int chunkDist(const ChunkPos& a, int cx, int cz) {
    return std::max(abs(a.x - cx), abs(a.z - cz));
}

void World::update(int cx, int cz) {
    const bool chunkChanged = (cx != lastUpdateCX || cz != lastUpdateCZ);
    lastUpdateCX = cx;
    lastUpdateCZ = cz;

    // 1. Discover new chunks — only when the player crosses into a new chunk.
    if (chunkChanged) {
        std::vector<std::pair<int,Chunk*>> newChunks;
        {
            std::lock_guard<std::mutex> lock(chunksMutex);
            for (int dx = -renderDistance; dx <= renderDistance; dx++) {
                for (int dz = -renderDistance; dz <= renderDistance; dz++) {
                    ChunkPos cp{cx+dx, cz+dz};
                    if (chunks.find(cp) != chunks.end()) continue;
                    
                    if (isServer) {
                        auto c = std::make_unique<Chunk>(cp, true);
                        Chunk* ptr = c.get();
                        ptr->state = ChunkState::Generating;
                        chunks[cp] = std::move(c);
                        newChunks.push_back({std::max(abs(dx),abs(dz)), ptr});
                    } else if (onRequestChunk) {
                        // Create a placeholder chunk so we don't request it again
                        auto c = std::make_unique<Chunk>(cp, false);
                        c->state = ChunkState::Empty; // Mark as empty/requesting
                        chunks[cp] = std::move(c);
                        onRequestChunk(cp.x, cp.z);
                    }
                }
            }
        }
        if (!newChunks.empty() && isServer) {
            std::sort(newChunks.begin(), newChunks.end());
            std::lock_guard<std::mutex> qlock(queueMutex);
            for (auto& [d, ptr] : newChunks) generationQueue.push(ptr);
            cv.notify_all();
        }
    }

    // 2. Identify Generated chunks ready to mesh, and MeshReady chunks to upload
    std::vector<std::pair<int,Chunk*>> toMesh;
    std::vector<Chunk*> toUpload;
    {
        std::lock_guard<std::mutex> lock(chunksMutex);
        static const ChunkPos offsets[] = {{-1,0},{1,0},{0,-1},{0,1}};

        for (auto& [pos, c] : chunks) {
            ChunkState s = c->state.load();

            if (!isServer) {
                if (s == ChunkState::Generated) {
                    // Mesh as soon as possible, even if not all neighbors are present.
                    // Incomplete seams will be fixed when the missing neighbor loads.
                    toMesh.push_back({chunkDist(pos, cx, cz), c.get()});
                    c->state = ChunkState::Meshing;

                } else if (s == ChunkState::Ready && c->neighborsAtMeshTime < 4) {
                    // This chunk was meshed when some neighbors weren't loaded yet.
                    // Check if all 4 neighbors are now at least Generated so we can fix seams.
                    bool allPresent = true;
                    for (auto& off : offsets) {
                        auto it = chunks.find({pos.x+off.x, pos.z+off.z});
                        if (it == chunks.end() || it->second->state == ChunkState::Empty
                                              || it->second->state == ChunkState::Generating) {
                            allPresent = false; break;
                        }
                    }
                    if (allPresent) {
                        toMesh.push_back({chunkDist(pos, cx, cz), c.get()});
                        c->state = ChunkState::Meshing;
                    }

                } else if (s == ChunkState::MeshReady) {
                    toUpload.push_back(c.get());
                }
            }
        }
    }

    // 3. Queue meshing jobs closest-first
    if (!toMesh.empty()) {
        std::sort(toMesh.begin(), toMesh.end());
        std::lock_guard<std::mutex> qlock(queueMutex);
        for (auto& [d, c] : toMesh) meshingQueue.push(c);
        cv.notify_all();
    }

    // 4. Upload finished meshes on the main thread — only a few per frame, and
    //    nearest-first, so streaming chunks in never spikes the frame time.
    std::sort(toUpload.begin(), toUpload.end(), [cx, cz](Chunk* a, Chunk* b) {
        return chunkDist(a->pos, cx, cz) < chunkDist(b->pos, cx, cz);
    });
    int uploaded = 0;
    for (auto* c : toUpload) {
        if (uploaded >= MAX_UPLOADS_PER_FRAME) break;
        c->uploadMesh();
        uploaded++;
    }

    // 5. Unload distant chunks — only after the player crosses into a new chunk.
    if (chunkChanged) {
        std::vector<ChunkPos> toRemove;
        std::lock_guard<std::mutex> lock(chunksMutex);
        for (auto& [k, v] : chunks) {
            if (abs(k.x - cx) > renderDistance + 2 || abs(k.z - cz) > renderDistance + 2) {
                ChunkState s = v->state.load();
                if (s != ChunkState::Generating && s != ChunkState::Meshing)
                    toRemove.push_back(k);
            }
        }
        for (auto& k : toRemove) chunks.erase(k);
    }
}

void World::updateForPlayers(std::vector<ChunkPos> centers) {
    if (centers.empty()) return;   // no players: leave the world as-is

    // Normalise (sort + dedupe) so the no-change check below is order-independent
    // and two players in the same chunk count once.
    std::sort(centers.begin(), centers.end(),
              [](const ChunkPos& a, const ChunkPos& b) { return a.x != b.x ? a.x < b.x : a.z < b.z; });
    centers.erase(std::unique(centers.begin(), centers.end(),
                              [](const ChunkPos& a, const ChunkPos& b) { return a == b; }),
                  centers.end());
    if (centers == lastServerCenters) return;   // nobody crossed a chunk boundary
    lastServerCenters = centers;

    auto nearAnyPlayer = [&](const ChunkPos& k, int margin) {
        for (const ChunkPos& c : centers)
            if (abs(k.x - c.x) <= renderDistance + margin &&
                abs(k.z - c.z) <= renderDistance + margin) return true;
        return false;
    };

    // 1. Discover + queue missing chunks around every player (union of regions).
    std::vector<std::pair<int, Chunk*>> newChunks;
    {
        std::lock_guard<std::mutex> lock(chunksMutex);
        for (const ChunkPos& c : centers)
            for (int dx = -renderDistance; dx <= renderDistance; dx++)
                for (int dz = -renderDistance; dz <= renderDistance; dz++) {
                    ChunkPos cp{ c.x + dx, c.z + dz };
                    if (chunks.find(cp) != chunks.end()) continue;
                    auto chunk = std::make_unique<Chunk>(cp, true);
                    Chunk* ptr = chunk.get();
                    ptr->state = ChunkState::Generating;
                    chunks[cp] = std::move(chunk);
                    newChunks.push_back({ std::max(abs(dx), abs(dz)), ptr });
                }
    }
    if (!newChunks.empty()) {
        std::sort(newChunks.begin(), newChunks.end());
        std::lock_guard<std::mutex> qlock(queueMutex);
        for (auto& [d, ptr] : newChunks) generationQueue.push(ptr);
        cv.notify_all();
    }

    // 2. Unload chunks that are far from EVERY player (never the in-flight ones).
    std::vector<ChunkPos> toRemove;
    {
        std::lock_guard<std::mutex> lock(chunksMutex);
        for (auto& [k, v] : chunks) {
            if (nearAnyPlayer(k, 2)) continue;
            ChunkState s = v->state.load();
            if (s != ChunkState::Generating && s != ChunkState::Meshing)
                toRemove.push_back(k);
        }
        for (auto& k : toRemove) chunks.erase(k);
    }
}

void Frustum::fromMatrix(const glm::mat4& m) {
    // Rows of the matrix (glm is column-major, so row i = (m[0][i]..m[3][i])).
    glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    planes[0] = r3 + r0;   // left
    planes[1] = r3 - r0;   // right
    planes[2] = r3 + r1;   // bottom
    planes[3] = r3 - r1;   // top
    planes[4] = r3 + r2;   // near
    planes[5] = r3 - r2;   // far
    for (auto& p : planes) {
        float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
}

bool Frustum::intersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const {
    for (const glm::vec4& p : planes) {
        // Positive vertex: the AABB corner furthest along the plane normal.
        glm::vec3 pv(p.x >= 0.0f ? mx.x : mn.x,
                     p.y >= 0.0f ? mx.y : mn.y,
                     p.z >= 0.0f ? mx.z : mn.z);
        if (p.x * pv.x + p.y * pv.y + p.z * pv.z + p.w < 0.0f)
            return false;   // wholly outside this plane
    }
    return true;
}

void World::collectVisible(const Frustum* fr, std::vector<Chunk*>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lock(chunksMutex);
    out.reserve(chunks.size());
    for (auto& [k, c] : chunks) {
        if (fr) {
            glm::vec3 mn((float)(k.x * CHUNK_SIZE), 0.0f, (float)(k.z * CHUNK_SIZE));
            glm::vec3 mx(mn.x + CHUNK_SIZE, (float)CHUNK_HEIGHT, mn.z + CHUNK_SIZE);
            if (!fr->intersectsAABB(mn, mx)) continue;
        }
        out.push_back(c.get());
    }
}

void World::drawAll(const Frustum* fr) const {
    static std::vector<Chunk*> vis;
    collectVisible(fr, vis);
    for (Chunk* c : vis) c->draw();
}

void World::drawAllWater(const Frustum* fr) const {
    static std::vector<Chunk*> vis;
    collectVisible(fr, vis);
    for (Chunk* c : vis) c->drawWater();
}

void World::drawAllFoliage(const Frustum* fr) const {
    static std::vector<Chunk*> vis;
    collectVisible(fr, vis);
    for (Chunk* c : vis) c->drawFoliage();
}

void World::drawAllGlass(const Frustum* fr) const {
    static std::vector<Chunk*> vis;
    collectVisible(fr, vis);
    for (Chunk* c : vis) c->drawGlass();
}

BlockType World::getBlockInternal(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return BlockType::Air;
    int cx = (wx < 0 && wx % CHUNK_SIZE != 0) ? wx/CHUNK_SIZE - 1 : wx/CHUNK_SIZE;
    int cz = (wz < 0 && wz % CHUNK_SIZE != 0) ? wz/CHUNK_SIZE - 1 : wz/CHUNK_SIZE;
    
    std::lock_guard<std::mutex> lock(chunksMutex);
    auto it = chunks.find({cx, cz});
    if (it == chunks.end() || it->second->state == ChunkState::Empty || it->second->state == ChunkState::Generating) return BlockType::Air;
    return it->second->get(wx - cx * CHUNK_SIZE, wy, wz - cz * CHUNK_SIZE);
}

BlockType World::getBlock(int wx, int wy, int wz) const {
    return getBlockInternal(wx, wy, wz);
}

uint8_t World::getSkyLight(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return 15;
    int cx = (wx < 0 && wx % CHUNK_SIZE != 0) ? wx / CHUNK_SIZE - 1 : wx / CHUNK_SIZE;
    int cz = (wz < 0 && wz % CHUNK_SIZE != 0) ? wz / CHUNK_SIZE - 1 : wz / CHUNK_SIZE;

    std::lock_guard<std::mutex> lock(chunksMutex);
    auto it = chunks.find({cx, cz});
    if (it == chunks.end() || it->second->state == ChunkState::Empty ||
        it->second->state == ChunkState::Generating)
        return 15;
    return it->second->getSkyLight(wx - cx * CHUNK_SIZE, wy, wz - cz * CHUNK_SIZE);
}

void World::setBlock(int wx, int wy, int wz, BlockType t) {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return;
    int cx = (wx < 0 && wx % CHUNK_SIZE != 0) ? wx/CHUNK_SIZE - 1 : wx/CHUNK_SIZE;
    int cz = (wz < 0 && wz % CHUNK_SIZE != 0) ? wz/CHUNK_SIZE - 1 : wz/CHUNK_SIZE;

    std::lock_guard<std::mutex> lock(chunksMutex);
    auto it = chunks.find({cx, cz});
    if (it == chunks.end()) return;

    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;
    it->second->set(lx, wy, lz, t);
    it->second->state = ChunkState::Generated;

    // If the modified block sits on a chunk boundary, the adjacent chunk's face
    // visibility changes too — mark it Generated so update() re-meshes it.
    auto markNeighbor = [&](int nx, int nz) {
        auto nit = chunks.find({nx, nz});
        if (nit == chunks.end()) return;
        ChunkState s = nit->second->state.load();
        if (s == ChunkState::Ready || s == ChunkState::MeshReady)
            nit->second->state = ChunkState::Generated;
    };

    if (lx == 0)              markNeighbor(cx - 1, cz);
    if (lx == CHUNK_SIZE - 1) markNeighbor(cx + 1, cz);
    if (lz == 0)              markNeighbor(cx, cz - 1);
    if (lz == CHUNK_SIZE - 1) markNeighbor(cx, cz + 1);
}

void World::relightAt(int wx, int wy, int wz) {
    (void)wy;
    int cx = (wx < 0 && wx % CHUNK_SIZE != 0) ? wx / CHUNK_SIZE - 1 : wx / CHUNK_SIZE;
    int cz = (wz < 0 && wz % CHUNK_SIZE != 0) ? wz / CHUNK_SIZE - 1 : wz / CHUNK_SIZE;

    std::lock_guard<std::mutex> lock(chunksMutex);
    auto relightChunk = [](Chunk* c) {
        if (!c) return;
        c->computeLight();
        ChunkState s = c->state.load();
        if (s == ChunkState::Ready || s == ChunkState::MeshReady)
            c->state = ChunkState::Generated;
    };

    auto it = chunks.find({cx, cz});
    if (it != chunks.end()) relightChunk(it->second.get());

    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;
    if (lx == 0) {
        auto n = chunks.find({cx - 1, cz});
        if (n != chunks.end()) relightChunk(n->second.get());
    }
    if (lx == CHUNK_SIZE - 1) {
        auto n = chunks.find({cx + 1, cz});
        if (n != chunks.end()) relightChunk(n->second.get());
    }
    if (lz == 0) {
        auto n = chunks.find({cx, cz - 1});
        if (n != chunks.end()) relightChunk(n->second.get());
    }
    if (lz == CHUNK_SIZE - 1) {
        auto n = chunks.find({cx, cz + 1});
        if (n != chunks.end()) relightChunk(n->second.get());
    }
}

bool World::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                    glm::ivec3& hitBlock, glm::ivec3& hitNormal) const {
    glm::vec3 pos = origin;
    glm::ivec3 ipos = glm::ivec3(floorf(pos.x), floorf(pos.y), floorf(pos.z));
    glm::ivec3 step(dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1);
    glm::vec3 tDelta(fabsf(1.0f / (dir.x != 0 ? dir.x : 1e-8f)),
                     fabsf(1.0f / (dir.y != 0 ? dir.y : 1e-8f)),
                     fabsf(1.0f / (dir.z != 0 ? dir.z : 1e-8f)));
    glm::vec3 tMax;
    tMax.x = (step.x > 0 ? (ipos.x + 1 - pos.x) : (pos.x - ipos.x)) * tDelta.x;
    tMax.y = (step.y > 0 ? (ipos.y + 1 - pos.y) : (pos.y - ipos.y)) * tDelta.y;
    tMax.z = (step.z > 0 ? (ipos.z + 1 - pos.z) : (pos.z - ipos.z)) * tDelta.z;
    glm::ivec3 normal(0);
    float dist = 0;
    while (dist < maxDist) {
        BlockType b = getBlock(ipos.x, ipos.y, ipos.z);
        if (b != BlockType::Air) {
            hitBlock = ipos;
            hitNormal = normal;
            return true;
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            dist = tMax.x; tMax.x += tDelta.x; ipos.x += step.x; normal = {-step.x,0,0};
        } else if (tMax.y < tMax.z) {
            dist = tMax.y; tMax.y += tDelta.y; ipos.y += step.y; normal = {0,-step.y,0};
        } else {
            dist = tMax.z; tMax.z += tDelta.z; ipos.z += step.z; normal = {0,0,-step.z};
        }
    }
    return false;
}

