#pragma once
#include "gl_loader.h"
#include "atlas.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <array>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <memory>
#include <functional>

enum class BlockType : uint8_t {
    Air       = 0,
    Grass     = 1,
    Dirt      = 2,
    Stone     = 3,
    Wood      = 4,
    Leaves    = 5,
    Sand      = 6,
    Gravel    = 7,
    Snow      = 8,
    Cactus    = 9,
    Sandstone = 10,
    Ice       = 11,
    Glowstone    = 12,
    Water        = 13,
    LeavesOrange = 14,
    LeavesRed    = 15,
    LeavesPink   = 16,
    Glass        = 17,
    PaintFirst   = 18,   // 16 painted-colour blocks: ids 18..33 (see PAINT_PALETTE)
};

enum class ChunkState {
    Empty,
    Generating,
    Generated,
    Meshing,
    MeshReady,
    Ready
};

static constexpr int CHUNK_SIZE = 16;
static constexpr int CHUNK_HEIGHT = 256;

struct Vertex {
    float x, y, z;       // world position
    float nx, ny, nz;    // face normal
    float u, v;          // atlas UV coordinates
    float materialID;    // BlockType cast to float
    float skyLight;      // (skyLevel/15) * faceShadeFactor, baked at mesh time
    float blockLight;    // (blockLevel/15) * faceShadeFactor
    float shoreDistance; // [0,1] distance to nearest land, used by water shader
};

struct ChunkPos {
    int x, z;
    bool operator==(const ChunkPos& o) const { return x == o.x && z == o.z; }
};

struct ChunkPosHash {
    size_t operator()(const ChunkPos& p) const {
        return std::hash<int>()(p.x) ^ (std::hash<int>()(p.z) << 16);
    }
};

class World;

class Chunk {
public:
    ChunkPos pos;
    bool isServer;
    std::array<BlockType, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE> blocks;
    // Light map: high nibble = sky light (0-15), low nibble = block light (0-15)
    std::array<uint8_t, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE> lightMap;
    GLuint vao = 0, vbo = 0;
    GLuint waterVao = 0, waterVbo = 0;
    GLuint foliageVao = 0, foliageVbo = 0;
    GLuint glassVao = 0, glassVbo = 0;
    int vertexCount = 0;
    int waterVertexCount = 0;
    int foliageVertexCount = 0;
    int glassVertexCount = 0;
    std::atomic<ChunkState> state{ChunkState::Empty};
    std::vector<Vertex> meshData;
    std::vector<Vertex> waterData;
    std::vector<Vertex> foliageData;
    std::vector<Vertex> glassData;
    std::mutex meshMutex;
    int neighborsAtMeshTime = 0;

    // Surface cache: top-solid Y and block type per column, filled after generation.
    // Used by the world map renderer; avoids re-scanning under lock.
    bool surfaceReady = false;
    std::array<int16_t, CHUNK_SIZE * CHUNK_SIZE> surfaceY{};
    std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE> surfaceBT{};

    Chunk(ChunkPos p, bool isServer);
    ~Chunk();

    BlockType get(int x, int y, int z) const;
    void set(int x, int y, int z, BlockType t);

    uint8_t getSkyLight  (int x, int y, int z) const;
    uint8_t getBlockLight(int x, int y, int z) const;
    void    setSkyLight  (int x, int y, int z, uint8_t v);
    void    setBlockLight(int x, int y, int z, uint8_t v);

    void computeLight();   // BFS flood-fill; call after block generation
    void buildMesh(World* world);
    void uploadMesh();
    void draw() const;
    void drawWater() const;
    void drawFoliage() const;
    void drawGlass() const;
};

class World {
public:
    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> chunks;
    mutable std::mutex chunksMutex;
    int renderDistance = 30;
    bool isServer;

    World(bool isServer);
    ~World();

    std::function<void(int, int)> onRequestChunk;

    void generate(int centerX, int centerZ);
    void update(int centerX, int centerZ);
    void drawAll() const;
    void drawAllWater() const;
    void drawAllFoliage() const;
    void drawAllGlass() const;

    BlockType getBlock(int wx, int wy, int wz) const;
    BlockType getBlockInternal(int wx, int wy, int wz) const;
    uint8_t   getSkyLight(int wx, int wy, int wz) const;   // 0-15; 15 for unloaded
    void setBlock(int wx, int wy, int wz, BlockType t);
    void relightAt(int wx, int wy, int wz);

    // Returns true and sets hit info if ray hits a block
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                 glm::ivec3& hitBlock, glm::ivec3& hitNormal) const;

    // Fill an RGBA pixel buffer (texSize×texSize) with a top-down terrain map.
    // (cx, cz) is the world-space centre; worldRadius is the half-extent covered.
    // Safe to call from a background thread.
    void fillMapPixels(uint8_t* rgba, int texSize, float cx, float cz, float worldRadius) const;

private:
    std::queue<Chunk*> generationQueue;
    std::queue<Chunk*> meshingQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::vector<std::thread> workers;
    bool stopWorkers = false;

    void workerThread();
};

void setWorldSeed(unsigned int seed);
