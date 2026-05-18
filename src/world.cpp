#include "world.h"
#include "noise.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>
#include <chrono>

static PerlinNoise gNoise(12345);
static PerlinNoise gTempNoise(54321);
static PerlinNoise gHumidNoise(98765);
static PerlinNoise gRiverNoise(11111);

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
                if (bt != BlockType::Air && bt != BlockType::Water) break;
                setSkyLight(x, y, z, 15);
                q.push({(uint8_t)x, (uint8_t)y, (uint8_t)z, 15u});
            }
        }
    }

    // ── Block light: seed Glowstone emitters ─────────────────────────────
    for (int x = 0; x < CHUNK_SIZE; x++)
    for (int y = 0; y < CHUNK_HEIGHT; y++)
    for (int z = 0; z < CHUNK_SIZE; z++) {
        if (get(x, y, z) == BlockType::Glowstone) {
            setBlockLight(x, y, z, 15);
            q.push({(uint8_t)x, (uint8_t)y, (uint8_t)z, (uint8_t)(0x80 | 15u)}); // block channel
        }
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
            if (nb != BlockType::Air && nb != BlockType::Water) continue;
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

static bool isOpaque(BlockType b) { return b != BlockType::Air && b != BlockType::Water; }

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
        case BlockType::Leaves: return TileID::Leaves;
        case BlockType::Sand:   return TileID::Sand;
        case BlockType::Gravel: return TileID::Gravel;
        case BlockType::Snow:   return TileID::Snow;
        case BlockType::Cactus:
            return (face == 2 || face == 3) ? TileID::CactusTop : TileID::CactusSide;
        case BlockType::Sandstone: return TileID::Sandstone;
        case BlockType::Ice:       return TileID::Ice;
        case BlockType::Glowstone: return TileID::Glowstone;
        case BlockType::Water:     return TileID::Water;
        default:                   return TileID::Stone;
    }
}

void Chunk::buildMesh(World* world) {
    std::vector<Vertex> verts;
    std::vector<Vertex> wverts;
    verts.reserve(4096);
    wverts.reserve(512);

    struct NeighborData {
        ChunkPos pos;
        const std::array<BlockType, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE>* blocks;
        const std::array<uint8_t,   CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE>* lights;
    };
    std::vector<NeighborData> neighbors;
    {
        std::lock_guard<std::mutex> lock(world->chunksMutex);
        for (auto& ncp : std::vector<ChunkPos>{{pos.x-1,pos.z},{pos.x+1,pos.z},{pos.x,pos.z-1},{pos.x,pos.z+1}}) {
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
                if (bt == BlockType::Air) continue;

                int wx = pos.x * CHUNK_SIZE + x;
                int wz = pos.z * CHUNK_SIZE + z;
                bool isWater = (bt == BlockType::Water);

                for (int face = 0; face < 6; face++) {
                    int ax = wx + FDX[face], ay = y + FDY[face], az = wz + FDZ[face];
                    BlockType adj = worldGet(ax, ay, az);

                    if (isWater) {
                        // Water: only expose faces adjacent to Air; skip bottom face
                        if (face == 3) continue; // bottom face (–Y): never seen
                        if (adj != BlockType::Air) continue;
                    } else {
                        // Opaque: expose faces adjacent to Air OR Water so seafloor shows through
                        if (isOpaque(adj)) continue;
                    }

                    uint8_t rawLight = worldGetLight(ax, ay, az);
                    float shade = SHADE[face];
                    float skyL   = ((rawLight >> 4) & 0xF) / 15.0f * shade;
                    float blockL = (rawLight & 0xF)        / 15.0f * shade;
                    if (bt == BlockType::Glowstone) blockL = shade;

                    TileID tile = getTile(bt, face);
                    float u0, v0, u1, v1;
                    tileUV(tile, u0, v0, u1, v1);

                    Vertex quad[4];
                    for (int vi = 0; vi < 4; vi++) {
                        quad[vi] = {
                            (float)wx + FV[face][vi][0], (float)y + FV[face][vi][1], (float)wz + FV[face][vi][2],
                            FNX[face], FNY[face], FNZ[face],
                            u0 + LU[vi] * (u1 - u0), v0 + LV[vi] * (v1 - v0),
                            (float)bt, skyL, blockL
                        };
                    }
                    pushQuad(isWater ? wverts : verts, quad);
                }
            }
        }
    }

    // --- Foliage cross-mesh ---
    std::vector<Vertex> fverts;
    fverts.reserve(256);

    auto pushFoliageQuad = [&](
        float x0, float y0, float z0,
        float x1, float y1, float z1,
        float x2, float y2, float z2,
        float x3, float y3, float z3,
        float fu0, float fv0, float fu1, float fv1,
        float skyL, float blkL)
    {
        Vertex q[4];
        q[0] = {x0,y0,z0, 0,1,0, fu0, fv0, 0, skyL, blkL};
        q[1] = {x1,y1,z1, 0,1,0, fu0, fv1, 0, skyL, blkL};
        q[2] = {x2,y2,z2, 0,1,0, fu1, fv1, 0, skyL, blkL};
        q[3] = {x3,y3,z3, 0,1,0, fu1, fv0, 0, skyL, blkL};
        fverts.push_back(q[0]); fverts.push_back(q[1]); fverts.push_back(q[2]);
        fverts.push_back(q[0]); fverts.push_back(q[2]); fverts.push_back(q[3]);
    };

    for (int z = 0; z < CHUNK_SIZE; z++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int topY = -1;
            BlockType topBlock = BlockType::Air;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                BlockType b = get(x, y, z);
                if (b != BlockType::Air) { topY = y; topBlock = b; break; }
            }
            if (topY < 0 || topBlock != BlockType::Grass) continue;
            int fy = topY + 1;
            if (fy >= CHUNK_HEIGHT || get(x, fy, z) != BlockType::Air) continue;

            int wx = pos.x * CHUNK_SIZE + x;
            int wz = pos.z * CHUNK_SIZE + z;
            uint32_t h = (uint32_t)(wx * 1619 + wz * 31337);
            h ^= (h >> 16); h *= 0x45d9f3bu; h ^= (h >> 16);
            uint32_t sel = h & 0xFF;

            TileID tile;
            if      (sel < 64)  tile = TileID::TallGrass;
            else if (sel < 74)  tile = TileID::FlowerRed;
            else if (sel < 84)  tile = TileID::FlowerYellow;
            else if (sel < 92)  tile = TileID::FlowerBlue;
            else continue;

            float fu0, fv0, fu1, fv1;
            tileUV(tile, fu0, fv0, fu1, fv1);

            float skyL = getSkyLight (x, fy, z) / 15.0f;
            float blkL = getBlockLight(x, fy, z) / 15.0f;

            float fx = (float)wx, fz = (float)wz;
            float fy0 = (float)fy, fy1 = fy0 + 1.0f;

            // Two crossed quads (X shape)
            pushFoliageQuad(fx,   fy0, fz,   fx,   fy1, fz,   fx+1, fy1, fz+1, fx+1, fy0, fz+1, fu0, fv0, fu1, fv1, skyL, blkL);
            pushFoliageQuad(fx+1, fy0, fz,   fx+1, fy1, fz,   fx,   fy1, fz+1, fx,   fy0, fz+1, fu0, fv0, fu1, fv1, skyL, blkL);
        }
    }

    {
        std::lock_guard<std::mutex> lock(meshMutex);
        meshData     = std::move(verts);
        waterData    = std::move(wverts);
        foliageData  = std::move(fverts);
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

    // Foliage mesh
    if (!foliageData.empty()) {
        if (!foliageVao) { glGenVertexArrays(1, &foliageVao); glGenBuffers(1, &foliageVbo); }
        glBindVertexArray(foliageVao);
        glBindBuffer(GL_ARRAY_BUFFER, foliageVbo);
        glBufferData(GL_ARRAY_BUFFER, foliageData.size() * sizeof(Vertex), foliageData.data(), GL_STATIC_DRAW);
        setupVertexAttribs();
        foliageVertexCount = (int)foliageData.size();
        foliageData.clear();
    }

    glBindVertexArray(0);
    state = ChunkState::Ready;
}

void Chunk::draw() const {
    if (isServer || state != ChunkState::Ready || vertexCount == 0) return;
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

void Chunk::drawWater() const {
    if (isServer || state != ChunkState::Ready || waterVertexCount == 0) return;
    glBindVertexArray(waterVao);
    glDrawArrays(GL_TRIANGLES, 0, waterVertexCount);
    glBindVertexArray(0);
}

void Chunk::drawFoliage() const {
    if (isServer || state != ChunkState::Ready || foliageVertexCount == 0) return;
    glBindVertexArray(foliageVao);
    glDrawArrays(GL_TRIANGLES, 0, foliageVertexCount);
    glBindVertexArray(0);
}

// ---- Biome system ----

static constexpr int SEA_LEVEL = 28;

enum class Biome : uint8_t { Plains=0, Forest=1, Desert=2, Mountains=3, Tundra=4, Savanna=5, Jungle=6 };
static constexpr int NUM_BIOMES = 7;

struct BiomeDef {
    float idealTemp, idealHumid; // centre in [0,1]×[0,1] temperature-humidity space
    float freq;                  // 2-D heightmap noise frequency
    float amplitude;             // height-noise multiplier (world Y units)
    int   octaves;
    float persistence;
    BlockType surfaceBlock;
    BlockType subSurfaceBlock;
};

static const BiomeDef BIOMES[NUM_BIOMES] = {
  // temp   humid  freq     amp   oct pers   surf                   sub
    {0.50f, 0.40f, 0.008f, 10.0f, 4, 0.50f, BlockType::Grass,      BlockType::Dirt      }, // Plains
    {0.50f, 0.80f, 0.010f, 16.0f, 5, 0.55f, BlockType::Grass,      BlockType::Dirt      }, // Forest
    {0.90f, 0.10f, 0.009f,  8.0f, 3, 0.40f, BlockType::Sand,       BlockType::Sandstone }, // Desert
    {0.10f, 0.50f, 0.013f, 32.0f, 6, 0.60f, BlockType::Snow,       BlockType::Stone     }, // Mountains
    {0.10f, 0.20f, 0.007f, 10.0f, 4, 0.50f, BlockType::Snow,       BlockType::Stone     }, // Tundra
    {0.75f, 0.25f, 0.007f,  9.0f, 3, 0.45f, BlockType::Grass,      BlockType::Dirt      }, // Savanna
    {0.85f, 0.90f, 0.011f, 20.0f, 5, 0.55f, BlockType::Grass,      BlockType::Dirt      }, // Jungle
};

// ---- Decorator helpers (defined before generateChunk) ----

// Large varied broadleaf tree: base 2–6 wide, tall trunk, structural branches, wide crown canopy.
// n drives placement; n2 drives per-tree shape so every tree looks different.
static void tryPlaceTree(Chunk* c, int x, int z, int top,
                         float n, float n2, float thresh) {
    if (n < thresh) return;
    float t = std::clamp((n  - thresh) / (1.0f - thresh), 0.0f, 1.0f);
    float s = std::clamp(n2 * 0.5f + 0.5f, 0.0f, 1.0f);

    // Deterministic per-tree RNG seeded by world position
    const int wx = c->pos.x * CHUNK_SIZE + x;
    const int wz = c->pos.z * CHUNK_SIZE + z;
    auto rng = [wx, wz](int salt) -> uint32_t {
        uint32_t v = (uint32_t)(wx * 1619 + wz * 31337 + salt * 6271);
        v ^= (v >> 16); v *= 0x45d9f3bu; return v ^ (v >> 16);
    };

    // Base diameter 2–6, driven by both noise channels
    int baseDiam = 2 + (int)((t * 0.5f + s * 0.5f) * 4.99f); // 2, 3, 4, 5, or 6
    int flareR   = (baseDiam + 1) / 2;      // root-flare half-width: 1, 1, 2, 2, 3
    int trunkR   = baseDiam / 4;             // trunk column radius:   0, 0, 1, 1, 1
    int margin   = flareR + 1;
    if (x < margin || x >= CHUNK_SIZE - margin || z < margin || z >= CHUNK_SIZE - margin) return;

    int trunkH   = 14 + (int)(t * 10.0f + s * 6.0f); // 14–30
    int trunkTop = top + trunkH;

    // Root flare: 3 tapering layers
    for (int ty = top + 1; ty <= top + 3 && ty < CHUNK_HEIGHT; ty++) {
        int r = std::max(flareR - (ty - top - 1), trunkR);
        for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (std::abs(dx) == r && std::abs(dz) == r && r > 0) continue;
            int bx = x + dx, bz = z + dz;
            if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE)
                c->set(bx, ty, bz, BlockType::Wood);
        }
    }
    // Main trunk above flare
    for (int ty = top + 4; ty <= trunkTop && ty < CHUNK_HEIGHT; ty++)
    for (int dx = -trunkR; dx <= trunkR; dx++)
    for (int dz = -trunkR; dz <= trunkR; dz++) {
        int bx = x + dx, bz = z + dz;
        if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE)
            c->set(bx, ty, bz, BlockType::Wood);
    }

    // Structural branches: arms radiating from 1/3 to 9/10 of trunk height
    static const int8_t DIRS8[8][2] = {
        {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}
    };
    const int branchStart = top + trunkH / 3;
    const int branchEnd   = top + (trunkH * 9) / 10;
    const int numBranches = 4 + (int)(t * 6.0f); // 4–10

    for (int bi = 0; bi < numBranches; bi++) {
        const int ty   = branchStart + (int)((rng(bi)       & 0xFF) / 255.0f * (branchEnd - branchStart));
        const int dir  = (rng(bi + 100) >> 8) & 7;
        const int blen = 3 + (int)((rng(bi + 200) & 7)     / 7.0f * 4.0f); // 3–7
        const int rise = 1 + (int)((rng(bi + 300) & 3)     / 3.0f * 2.0f); // 1–3
        const int ddx  = DIRS8[dir][0], ddz = DIRS8[dir][1];

        // Branch wood arm
        for (int i = 1; i <= blen; i++) {
            int bx = x + ddx * i, by = ty + (rise * i) / blen, bz = z + ddz * i;
            if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE || by >= CHUNK_HEIGHT) break;
            c->set(bx, by, bz, BlockType::Wood);
        }
        // Leaf cluster at tip: oblate ellipsoid r=2 horiz, r=2 vert
        const int tipX = x + ddx * blen, tipY = ty + rise, tipZ = z + ddz * blen;
        for (int lx = -2; lx <= 2; lx++)
        for (int lz = -2; lz <= 2; lz++)
        for (int ly = -1; ly <= 2; ly++) {
            if ((float)(lx*lx + lz*lz) / 4.0f + (float)(ly*ly) / 4.0f > 1.0f) continue;
            int bx = tipX + lx, by = tipY + ly, bz = tipZ + lz;
            if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE && by > 0 && by < CHUNK_HEIGHT)
                if (c->get(bx, by, bz) == BlockType::Air)
                    c->set(bx, by, bz, BlockType::Leaves);
        }
    }

    // Crown canopy: wide oblate ellipsoid
    const int cR = 5 + (int)(t * 3.0f);  // horizontal radius 5–8
    const int cH = 3 + (int)(s * 3.0f);  // vertical half-axis 3–6
    for (int lx = -cR; lx <= cR; lx++)
    for (int lz = -cR; lz <= cR; lz++)
    for (int ly = -(cH + 1); ly <= cH; ly++) {
        if ((float)(lx*lx + lz*lz) / (float)(cR*cR) + (float)(ly*ly) / (float)(cH*cH) > 1.0f) continue;
        int bx = x + lx, by = trunkTop + ly, bz = z + lz;
        if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE && by > 0 && by < CHUNK_HEIGHT)
            if (c->get(bx, by, bz) == BlockType::Air)
                c->set(bx, by, bz, BlockType::Leaves);
    }
}

// Tall conifer: single-block trunk, broad conical canopy, whorl branches.
static void tryPlacePineTree(Chunk* c, int x, int z, int top,
                              float n, float n2, float thresh) {
    if (n < thresh || x < 4 || x >= CHUNK_SIZE - 4 || z < 4 || z >= CHUNK_SIZE - 4) return;
    float t = std::clamp((n  - thresh) / (1.0f - thresh), 0.0f, 1.0f);
    float s = std::clamp(n2 * 0.5f + 0.5f, 0.0f, 1.0f);

    const int wx = c->pos.x * CHUNK_SIZE + x;
    const int wz = c->pos.z * CHUNK_SIZE + z;
    auto rng = [wx, wz](int salt) -> uint32_t {
        uint32_t v = (uint32_t)(wx * 1619 + wz * 31337 + salt * 6271);
        v ^= (v >> 16); v *= 0x45d9f3bu; return v ^ (v >> 16);
    };

    int trunkH   = 14 + (int)(t * 8.0f); // 14–22
    for (int ty = top + 1; ty <= top + trunkH && ty < CHUNK_HEIGHT; ty++)
        c->set(x, ty, z, BlockType::Wood);

    // Conical canopy: cone starts 1/4 up the trunk, tip 1 block above crown
    int maxR     = 4 + (int)(t * 2.0f + s); // bottom-ring radius 4–7
    int coneApex = top + trunkH + 1;
    int coneBase = top + trunkH / 4;
    int coneH    = coneApex - coneBase;

    for (int ty = coneBase; ty <= coneApex && ty < CHUNK_HEIGHT; ty++) {
        float progress = (float)(coneApex - ty) / (float)coneH;
        int r = (int)(progress * maxR);
        for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (std::abs(dx) == r && std::abs(dz) == r && r > 1) continue;
            int bx = x + dx, bz = z + dz;
            if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE)
                if (c->get(bx, ty, bz) == BlockType::Air)
                    c->set(bx, ty, bz, BlockType::Leaves);
        }
    }

    // Whorl branches: two opposing arms at each tier inside the cone
    static const int8_t CARD[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    int tierStep = 3 + (int)(s); // 3 or 4 blocks between tiers

    for (int ty = coneBase + 1; ty < coneApex - 1; ty += tierStep) {
        float progress = (float)(coneApex - ty) / (float)coneH;
        int rHere = (int)(progress * maxR);
        if (rHere < 2) continue;

        int blen = std::min(rHere - 1, 2 + (int)(t * 2.0f)); // stays inside cone
        int d0   = (int)(rng(ty) & 3);
        int d1   = (d0 + 2) & 3; // opposite cardinal direction

        for (int d : {d0, d1}) {
            for (int i = 1; i <= blen; i++) {
                int bx = x + CARD[d][0] * i;
                int bz = z + CARD[d][1] * i;
                int by = ty - (i * 2 >= blen ? 1 : 0); // slight droop on outer half
                if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE) break;
                c->set(bx, by, bz, BlockType::Wood);
            }
        }
    }
}

static void tryPlaceBush(Chunk* c, int x, int z, int top, float n, float thresh) {
    if (n < thresh) return;
    int height = (n > thresh + 0.06f) ? 2 : 1;
    for (int h = 1; h <= height; h++) {
        int by = top + h;
        if (by >= CHUNK_HEIGHT) break;
        int rad = (h == 1) ? 1 : 0;
        for (int lx=-rad; lx<=rad; lx++) for (int lz=-rad; lz<=rad; lz++) {
            int bx=x+lx, bz=z+lz;
            if (bx>=0&&bx<CHUNK_SIZE&&bz>=0&&bz<CHUNK_SIZE)
                if (c->get(bx,by,bz)==BlockType::Air)
                    c->set(bx,by,bz,BlockType::Leaves);
        }
    }
}

static void tryPlaceCactus(Chunk* c, int x, int z, int top, float n, float thresh) {
    if (n < thresh) return;
    int height = std::clamp(1 + (int)((n - thresh) * 12.0f), 1, 3);
    for (int ty = top+1; ty <= top+height && ty < CHUNK_HEIGHT; ty++)
        c->set(x, ty, z, BlockType::Cactus);
}

static void tryPlaceRockFormation(Chunk* c, int x, int z, int top, float n, float thresh) {
    if (n < thresh || x < 1 || x >= CHUNK_SIZE-1 || z < 1 || z >= CHUNK_SIZE-1) return;
    int height = std::clamp(2 + (int)((n - thresh) * 22.0f), 2, 5);
    for (int ty = top+1; ty <= top+height && ty < CHUNK_HEIGHT; ty++)
        c->set(x, ty, z, BlockType::Stone);
    // Scatter base rocks
    for (int lx=-1; lx<=1; lx++) for (int lz=-1; lz<=1; lz++) {
        if (lx==0&&lz==0) continue;
        int bx=x+lx, bz=z+lz;
        if (bx>=0&&bx<CHUNK_SIZE&&bz>=0&&bz<CHUNK_SIZE&&top+1<CHUNK_HEIGHT)
            if (c->get(bx,top+1,bz)==BlockType::Air)
                c->set(bx,top+1,bz,BlockType::Stone);
    }
}

static void tryPlaceStoneSpire(Chunk* c, int x, int z, int top, float n, float thresh) {
    if (n < thresh || top < SEA_LEVEL + 12) return; // only on high peaks
    int height = std::clamp(3 + (int)((n - thresh) * 35.0f), 3, 9);
    for (int ty = top+1; ty <= top+height && ty < CHUNK_HEIGHT; ty++)
        c->set(x, ty, z, BlockType::Stone);
}

static void tryPlaceBoulder(Chunk* c, int x, int z, int top, float n, float thresh) {
    if (n < thresh || x < 1 || x >= CHUNK_SIZE-1 || z < 1 || z >= CHUNK_SIZE-1) return;
    int rad    = (n > thresh + 0.04f) ? 1 : 0;
    int height = (n > thresh + 0.07f) ? 2 : 1;
    for (int h = 1; h <= height; h++) {
        int by = top + h;
        if (by >= CHUNK_HEIGHT) break;
        int r = (h == 1) ? rad : 0;
        for (int lx=-r; lx<=r; lx++) for (int lz=-r; lz<=r; lz++) {
            int bx=x+lx, bz=z+lz;
            if (bx>=0&&bx<CHUNK_SIZE&&bz>=0&&bz<CHUNK_SIZE)
                if (c->get(bx,by,bz)==BlockType::Air)
                    c->set(bx,by,bz,BlockType::Stone);
        }
    }
}

// Savanna: tall trunk, wide flat circular canopy (acacia-style).
static void tryPlaceAcaciaTree(Chunk* c, int x, int z, int top,
                                float n, float n2, float thresh) {
    if (n < thresh || x < 4 || x >= CHUNK_SIZE - 4 || z < 4 || z >= CHUNK_SIZE - 4) return;
    float t = std::clamp((n  - thresh) / (1.0f - thresh), 0.0f, 1.0f);
    float s = std::clamp(n2 * 0.5f + 0.5f, 0.0f, 1.0f);

    int trunkH = 6 + (int)(t * 8.0f + s * 4.0f); // 6–18
    for (int ty = top + 1; ty <= top + trunkH && ty < CHUNK_HEIGHT; ty++)
        c->set(x, ty, z, BlockType::Wood);

    // Three-layer circular disc canopy — each layer narrower toward the top
    int canopyR = 3 + (int)(t * 2.0f + s); // 3–6
    for (int pass = 0; pass < 3; pass++) {
        int ly = top + trunkH + 1 + pass;
        int r  = (pass == 0) ? canopyR : (pass == 1) ? canopyR - 1 : canopyR / 2;
        if (ly >= CHUNK_HEIGHT || r <= 0) break;
        for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (dx * dx + dz * dz > r * r) continue;
            int bx = x + dx, bz = z + dz;
            if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE)
                if (c->get(bx, ly, bz) == BlockType::Air)
                    c->set(bx, ly, bz, BlockType::Leaves);
        }
    }
}

// Jungle: very tall tree, thick trunk option, high branches with leaf clusters, large canopy.
static void tryPlaceJungleTree(Chunk* c, int x, int z, int top,
                                float n, float n2, float thresh) {
    if (n < thresh) return;
    float t = std::clamp((n  - thresh) / (1.0f - thresh), 0.0f, 1.0f);
    float s = std::clamp(n2 * 0.5f + 0.5f, 0.0f, 1.0f);

    const int wx = c->pos.x * CHUNK_SIZE + x;
    const int wz = c->pos.z * CHUNK_SIZE + z;
    auto rng = [wx, wz](int salt) -> uint32_t {
        uint32_t v = (uint32_t)(wx * 1619 + wz * 31337 + salt * 6271);
        v ^= (v >> 16); v *= 0x45d9f3bu; return v ^ (v >> 16);
    };

    int trunkR = (t > 0.3f) ? 1 : 0;
    int margin = trunkR + 4;
    if (x < margin || x >= CHUNK_SIZE - margin || z < margin || z >= CHUNK_SIZE - margin) return;

    int trunkH = 16 + (int)(t * 12.0f + s * 6.0f); // 16–34

    for (int ty = top + 1; ty <= top + trunkH && ty < CHUNK_HEIGHT; ty++)
    for (int dx = -trunkR; dx <= trunkR; dx++)
    for (int dz = -trunkR; dz <= trunkR; dz++) {
        int bx = x + dx, bz = z + dz;
        if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE)
            c->set(bx, ty, bz, BlockType::Wood);
    }

    // High branches from upper half of trunk
    static const int8_t DIRS8[8][2] = {
        {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}
    };
    const int branchStart = top + trunkH / 2;
    const int branchEnd   = top + (trunkH * 9) / 10;
    const int numBranches = 3 + (int)(t * 4.0f); // 3–7

    for (int bi = 0; bi < numBranches; bi++) {
        const int ty   = branchStart + (int)((rng(bi)       & 0xFF) / 255.0f * (branchEnd - branchStart));
        const int dir  = (rng(bi + 100) >> 8) & 7;
        const int blen = 4 + (int)((rng(bi + 200) & 7)     / 7.0f * 4.0f); // 4–8
        const int rise = 1 + (int)((rng(bi + 300) & 3)     / 3.0f * 3.0f); // 1–4
        const int ddx  = DIRS8[dir][0], ddz = DIRS8[dir][1];

        for (int i = 1; i <= blen; i++) {
            int bx = x + ddx * i, by = ty + (rise * i) / blen, bz = z + ddz * i;
            if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE || by >= CHUNK_HEIGHT) break;
            c->set(bx, by, bz, BlockType::Wood);
        }
        // Large leaf cluster at tip: r=3 horiz, r=2 vert
        const int tipX = x + ddx * blen, tipY = ty + rise, tipZ = z + ddz * blen;
        for (int lx = -3; lx <= 3; lx++)
        for (int lz = -3; lz <= 3; lz++)
        for (int ly = -1; ly <= 3; ly++) {
            if ((float)(lx*lx + lz*lz) / 9.0f + (float)(ly*ly) / 4.0f > 1.0f) continue;
            int bx = tipX + lx, by = tipY + ly, bz = tipZ + lz;
            if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE && by > 0 && by < CHUNK_HEIGHT)
                if (c->get(bx, by, bz) == BlockType::Air)
                    c->set(bx, by, bz, BlockType::Leaves);
        }
    }

    // Main spherical canopy
    const int cR   = 5 + (int)(t * 3.0f + s); // 5–9
    const int cH   = cR - 1;
    const int cCtr = top + trunkH;
    for (int lx = -cR; lx <= cR; lx++)
    for (int lz = -cR; lz <= cR; lz++)
    for (int ly = -(cH + 1); ly <= cH; ly++) {
        if ((float)(lx*lx + lz*lz) / (float)(cR*cR) + (float)(ly*ly) / (float)(cH*cH) > 1.0f) continue;
        int bx = x + lx, by = cCtr + ly, bz = z + lz;
        if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE && by > 0 && by < CHUNK_HEIGHT)
            if (c->get(bx, by, bz) == BlockType::Air)
                c->set(bx, by, bz, BlockType::Leaves);
    }
}

// Jungle: ground-level dense bush clusters
static void tryPlaceJungleBush(Chunk* c, int x, int z, int top, float n, float thresh) {
    if (n < thresh) return;
    for (int lx = -1; lx <= 1; lx++) for (int lz = -1; lz <= 1; lz++) for (int ly = 1; ly <= 2; ly++) {
        if (ly == 2 && (abs(lx) == 1 || abs(lz) == 1)) continue;
        int bx = x+lx, by = top+ly, bz = z+lz;
        if (bx >= 0 && bx < CHUNK_SIZE && bz >= 0 && bz < CHUNK_SIZE && by < CHUNK_HEIGHT)
            if (c->get(bx, by, bz) == BlockType::Air)
                c->set(bx, by, bz, BlockType::Leaves);
    }
}

// Tundra: ice spike column
static void tryPlaceIceSpike(Chunk* c, int x, int z, int top, float n, float thresh) {
    if (n < thresh) return;
    int height = std::clamp(3 + (int)((n - thresh) * 32.0f), 3, 9);
    for (int ty = top+1; ty <= top+height && ty < CHUNK_HEIGHT; ty++)
        c->set(x, ty, z, BlockType::Ice);
}

// ---- Generation ----

static void generateChunk(Chunk* c) {
    const int ox = c->pos.x * CHUNK_SIZE;
    const int oz = c->pos.z * CHUNK_SIZE;

    // Pass 0: per-column biome weights → blended surface height + dominant biome
    float surfH_f[CHUNK_SIZE][CHUNK_SIZE];
    Biome dominant[CHUNK_SIZE][CHUNK_SIZE];

    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            float wx = (float)(ox + x);
            float wz = (float)(oz + z);

            // Low-frequency biome noise mapped to [0, 1]
            float temp  = gTempNoise .octave(wx * 0.003f,          wz * 0.003f,          2, 0.5f, 2.0f) * 0.5f + 0.5f;
            float humid = gHumidNoise.octave(wx * 0.003f + 100.0f, wz * 0.003f + 100.0f, 2, 0.5f, 2.0f) * 0.5f + 0.5f;

            float weights[NUM_BIOMES], wTotal = 0.0f;
            int   domIdx = 0;
            for (int i = 0; i < NUM_BIOMES; i++) {
                float dt = temp  - BIOMES[i].idealTemp;
                float dh = humid - BIOMES[i].idealHumid;
                weights[i] = expf(-7.0f * (dt*dt + dh*dh));
                wTotal += weights[i];
                if (weights[i] > weights[domIdx]) domIdx = i;
            }

            // Blend surface height from each biome's own noise sample
            float blendH = 0.0f;
            for (int i = 0; i < NUM_BIOMES; i++) {
                float w = weights[i] / wTotal;
                if (w < 0.005f) continue;
                float h = gNoise.octave(wx * BIOMES[i].freq, wz * BIOMES[i].freq,
                                        BIOMES[i].octaves, BIOMES[i].persistence, 2.0f);
                blendH += w * ((float)SEA_LEVEL + h * BIOMES[i].amplitude);
            }

            // River carving: zero-crossings of low-freq noise form meandering rivers.
            // Only carve near sea level so mountain ravines are unaffected.
            float riverN = gRiverNoise.octave(wx * 0.005f, wz * 0.005f, 2, 0.5f, 2.0f);
            if (std::abs(riverN) < 0.028f && blendH > SEA_LEVEL - 6 && blendH < SEA_LEVEL + 12) {
                blendH = (float)(SEA_LEVEL - 2);
            }

            surfH_f[x][z] = blendH;
            dominant[x][z] = (Biome)domIdx;
        }
    }

    // Pass 1: 3D density field → Stone/Gravel/Air, with cave carving
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            float wx     = (ox + x) * 0.012f;
            float wz     = (oz + z) * 0.012f;
            float surfH  = surfH_f[x][z];
            int   surfHi = std::clamp((int)surfH, 4, CHUNK_HEIGHT - 12);

            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                float d3   = gNoise.octave(wx * 2.0f, y * 0.05f, wz * 2.0f, 4, 0.5f, 2.0f);
                float bias = (surfH - y) * 0.10f;
                if (d3 + bias <= 0.0f) continue;

                if (y > 3 && y < surfHi - 2) {
                    float cave1 = gNoise.octave(wx * 3.5f + 50.0f, y * 0.12f, wz * 3.5f + 50.0f, 3, 0.5f);
                    if (cave1 > 0.36f) continue;
                    float cave2 = gNoise.octave(wx * 3.0f - 80.0f, y * 0.15f + 30.0f, wz * 3.0f - 80.0f, 2, 0.6f);
                    if (cave2 > 0.40f) continue;
                }

                BlockType bt = BlockType::Stone;
                if (y < 12) {
                    float grv = gNoise.octave(wx * 5.0f + 200.0f, (float)y * 0.2f, wz * 5.0f + 200.0f, 2);
                    if (grv > 0.30f) bt = BlockType::Gravel;
                }
                // Rare glowstone veins deep underground
                if (y > 4 && y < 24) {
                    float gn = gNoise.noise(wx * 0.11f + 333.0f, y * 0.11f + 333.0f, wz * 0.11f + 333.0f);
                    if (gn > 0.44f) bt = BlockType::Glowstone;
                }
                c->set(x, y, z, bt);
            }
        }
    }

    // Pass 2: top-down surface scan — biome-specific surface/subsurface blocks
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            const BiomeDef& bd = BIOMES[(int)dominant[x][z]];
            int depthFromAir = 0;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                BlockType bt = c->get(x, y, z);
                if (bt == BlockType::Air || bt == BlockType::Leaves) {
                    depthFromAir = 0;
                    continue;
                }
                if (bt != BlockType::Stone && bt != BlockType::Gravel) {
                    depthFromAir++;
                    continue;
                }
                depthFromAir++;
                bool aboveSea = (y >= SEA_LEVEL);
                if (depthFromAir == 1) {
                    c->set(x, y, z, aboveSea ? bd.surfaceBlock    : BlockType::Sand);
                } else if (depthFromAir <= 5) {
                    c->set(x, y, z, aboveSea ? bd.subSurfaceBlock : BlockType::Sand);
                } else {
                    break;
                }
            }
        }
    }

    // Build top-solid map for decorator pass
    int topSolid[CHUNK_SIZE][CHUNK_SIZE];
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            topSolid[x][z] = -1;
            for (int y = CHUNK_HEIGHT-1; y >= 0; y--)
                if (c->get(x,y,z) != BlockType::Air) { topSolid[x][z] = y; break; }
        }

    // Pass 3: biome-specific decorators, per column (skip underwater columns)
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int top = topSolid[x][z];
            if (top < 0 || top < SEA_LEVEL) continue; // don't decorate below water line
            BlockType topBlock = c->get(x, top, z);

            // Three independent noise values with different spatial offsets
            float n1 = gNoise.noise((ox+x)*0.090f,          (oz+z)*0.090f);
            float n2 = gNoise.noise((ox+x)*0.110f + 500.0f, (oz+z)*0.110f + 500.0f);
            float n3 = gNoise.noise((ox+x)*0.070f + 1000.0f,(oz+z)*0.070f + 1000.0f);

            switch (dominant[x][z]) {
                case Biome::Plains:
                    if (topBlock == BlockType::Grass) {
                        tryPlaceTree(c, x, z, top, n1, n2, 0.75f);
                        tryPlaceBush(c, x, z, top, n3, 0.72f);
                    }
                    break;
                case Biome::Forest:
                    if (topBlock == BlockType::Grass) {
                        tryPlaceTree(c, x, z, top, n1, n2, 0.50f);
                        tryPlaceBush(c, x, z, top, n3, 0.65f);
                    }
                    break;
                case Biome::Desert:
                    if (topBlock == BlockType::Sand) {
                        tryPlaceCactus       (c, x, z, top, n1, 0.80f);
                        tryPlaceRockFormation(c, x, z, top, n3, 0.88f);
                    }
                    break;
                case Biome::Mountains:
                    if (topBlock == BlockType::Snow || topBlock == BlockType::Stone) {
                        tryPlacePineTree  (c, x, z, top, n1, n2, 0.75f);
                        tryPlaceStoneSpire(c, x, z, top, n3, 0.87f);
                    }
                    break;
                case Biome::Tundra:
                    if (topBlock == BlockType::Snow || topBlock == BlockType::Stone) {
                        tryPlacePineTree(c, x, z, top, n1, n2, 0.80f);
                        tryPlaceBoulder (c, x, z, top, n3, 0.76f);
                        tryPlaceIceSpike(c, x, z, top, n3, 0.91f);
                    }
                    break;
                case Biome::Savanna:
                    if (topBlock == BlockType::Grass) {
                        tryPlaceAcaciaTree   (c, x, z, top, n1, n2, 0.82f);
                        tryPlaceRockFormation(c, x, z, top, n3, 0.86f);
                    }
                    break;
                case Biome::Jungle:
                    if (topBlock == BlockType::Grass) {
                        tryPlaceJungleTree(c, x, z, top, n1, n2, 0.58f);
                        tryPlaceJungleBush(c, x, z, top, n3, 0.50f);
                    }
                    break;
            }
        }
    }

    // Pass 4: water fill — columns with topSolid below sea level get filled with water
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int top = topSolid[x][z];
            if (top >= SEA_LEVEL) continue;
            for (int y = top + 1; y <= SEA_LEVEL; y++) {
                if (y >= CHUNK_HEIGHT) break;
                if (c->get(x, y, z) == BlockType::Air)
                    c->set(x, y, z, BlockType::Water);
            }
        }
    }

    c->computeLight();
    c->state = ChunkState::Generated;
}

// ---- World ----

World::World(bool isServer) : isServer(isServer) {
    int numWorkers = std::max(1u, std::thread::hardware_concurrency());
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

static constexpr int MAX_UPLOADS_PER_FRAME = 16;

// Chebyshev distance from player chunk
static int chunkDist(const ChunkPos& a, int cx, int cz) {
    return std::max(abs(a.x - cx), abs(a.z - cz));
}

void World::update(int cx, int cz) {
    // 1. Discover new chunks
    {
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

    // 4. Upload meshes on the main thread — capped per frame to avoid spikes
    int uploaded = 0;
    for (auto* c : toUpload) {
        if (uploaded >= MAX_UPLOADS_PER_FRAME) break;
        c->uploadMesh();
        uploaded++;
    }

    // 5. Unload distant chunks (skip any still being processed)
    {
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

void World::drawAll() const {
    std::lock_guard<std::mutex> lock(chunksMutex);
    for (auto& [k, c] : chunks) c->draw();
}

void World::drawAllWater() const {
    std::lock_guard<std::mutex> lock(chunksMutex);
    for (auto& [k, c] : chunks) c->drawWater();
}

void World::drawAllFoliage() const {
    std::lock_guard<std::mutex> lock(chunksMutex);
    for (auto& [k, c] : chunks) c->drawFoliage();
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

