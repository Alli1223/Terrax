#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <random>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>
#include "game_object.h"

class BipedalRig;
class World;
struct Town;

// What an NPC is and how it behaves. Only Villager is used in phase 2; Enemy
// and Guard arrive in later phases.
enum class NPCType : uint8_t { Villager = 0, Enemy = 1, Guard = 2 };

// A non-player character. Server-authoritative, exactly like Ferry: the server
// owns motion / AI and broadcasts NPCState packets; each client creates one NPC
// per network id and interpolates it (update).
class NPC : public GameObject {
public:
    NPC();
    ~NPC() override;

    void update(float dt, World& world) override;   // client interpolation
    void draw(GLuint modelLoc) const override;
    void getAABB(glm::vec3& mn, glm::vec3& mx) const override;

    // Builds the owned rig from appearanceSeed. Client-only — needs a GL
    // context, so call it on the main thread once appearanceSeed is set.
    void initClientVisual();

    NPCType  type           = NPCType::Villager;
    float    health         = 100.0f;
    uint32_t appearanceSeed = 0;
    bool     walking        = false;

    // Client-side interpolation targets, fed from NPCState packets.
    glm::vec3 targetPos{0.0f};
    float     targetYaw = 0.0f;
    glm::vec3 velocity{0.0f};
    double    lastUpdate = 0.0;   // client: last NPCState arrival, for stream timeout

    // --- combat ---
    float     attackCooldown  = 0.0f;   // server: time until the next attack
    float     attackAnimTimer = 0.0f;   // server: remaining swing-animation time
    float     dyingTimer      = 0.0f;   // server: > 0 while dying, then removed
    bool      attackFlag      = false;  // client: server reports mid-swing this tick
    bool      prevAttackFlag  = false;  // client: edge-detect for the swing
    bool      dyingFlag       = false;  // client: this NPC is dying
    float     dyingLerp       = 0.0f;   // client: 0..1 fall-over progress
    float     fleeTimer       = 0.0f;   // server: villager panic countdown
    glm::vec2 fleeFrom{0.0f};           // server: the point a villager flees from

    // --- Server-side AI state (owned by NpcDirector) ---
    int       townIndex = -1;           // villager: home town index
    uint64_t   campKey   = 0;            // enemy: owning camp key
    glm::vec2 homePos{0.0f};            // villager: door approach / enemy: camp centre
    glm::vec2 doorPos{0.0f};            // on-axis point right at the doorway
    glm::vec2 insidePos{0.0f};          // spot just inside the house, through the door
    glm::vec2 bedPos{0.0f};             // sleep spot deep inside the house (ground-floor
                                        // Bedroom centre if present, otherwise a point
                                        // a few blocks further past insidePos)
    float     groundY = 65.0f;          // standing height, snapped to the surface
    std::vector<glm::vec2> path;        // current route, world XZ
    size_t    pathIndex = 0;
    float     idleTimer = 0.0f;
    bool      goingHome = false;        // villager night routine: heading to / staying home

private:
    BipedalRig* rig = nullptr;          // client-only, owned
};

// Per-town walkability grid + A* path planner for in-town NPC navigation.
// Building footprints are blocked; everything else inside the town is walkable.
class TownNav {
public:
    void build(const Town& town);
    bool ready() const { return cells > 0; }

    bool walkable(float wx, float wz) const;
    glm::vec2 nearestWalkable(glm::vec2 p) const;
    glm::vec2 randomWalkableNear(glm::vec2 around, float radius, std::mt19937& rng) const;

    // A* route of world-XZ waypoints from 'from' to 'to'. Empty if unreachable.
    std::vector<glm::vec2> findPath(glm::vec2 from, glm::vec2 to) const;

private:
    static constexpr int CELL = 2;      // world blocks per nav cell
    int originX = 0, originZ = 0;       // world XZ of cell (0,0)'s corner
    int gw = 0, gh = 0;                 // grid dimensions in cells
    int cells = 0;
    std::vector<uint8_t> blocked;       // gw*gh, 1 = impassable

    bool inBounds(int cx, int cz) const { return cx >= 0 && cx < gw && cz >= 0 && cz < gh; }
    int  idx(int cx, int cz) const { return cz * gw + cx; }
    void worldToCell(float wx, float wz, int& cx, int& cz) const;
    glm::vec2 cellCenter(int cx, int cz) const;
};

// A wilderness bandit camp — a deterministic anchor a group of enemies loiters
// around. Sites that fall on a town, a highway, or water are marked invalid.
struct Camp {
    glm::vec2 center{0.0f};
    bool      valid = false;
    int       banditCount = 0;
};

// One player's networked state, as the director sees it each tick.
struct DirectorPlayer {
    uint32_t  id;
    glm::vec3 pos;
};

// Damage an NPC dealt to a player; the server loop drains it each tick.
struct PlayerDamage {
    uint32_t playerId;
    float    amount;
};

// Server-side: owns every live NPC, streams town populations and bandit camps
// in and out by player proximity, and runs villager / bandit AI each tick.
class NpcDirector {
public:
    void update(float dt, const std::vector<DirectorPlayer>& players,
                World& world, float gameTime);
    const std::vector<std::unique_ptr<NPC>>& npcs() const { return active; }

    // Resolves a melee hit a player landed on an NPC: damages it (unless it is
    // protected townsfolk in a town), and flags the attacker wanted / panics
    // villagers as appropriate.
    void playerHitNpc(uint32_t attackerId, uint32_t npcId, glm::vec3 attackerPos);

    // Damage NPCs dealt to players this tick; the server loop drains it.
    std::vector<PlayerDamage> pendingDamage;

private:
    void populateTown(int townIndex);
    void depopulateTown(int townIndex);
    void stepVillager(NPC& n, float dt, World& world, float gameTime);
    void stepGuard(NPC& n, float dt, World& world,
                   const std::vector<DirectorPlayer>& players);

    void updateCamps(const std::vector<DirectorPlayer>& players);
    const Camp& evalCamp(int gx, int gz, uint64_t key);
    void spawnCamp(uint64_t key, const Camp& camp);
    void despawnCamp(uint64_t key);
    void stepBandit(NPC& n, float dt, World& world,
                    const std::vector<DirectorPlayer>& players);

    bool inAnyTown(glm::vec2 worldXZ) const;

    std::vector<std::unique_ptr<NPC>> active;
    std::unordered_map<int, TownNav>  navCache;
    std::unordered_set<int>           populated;
    std::unordered_map<uint64_t, Camp> campCache;
    std::unordered_set<uint64_t>       activeCamps;
    std::unordered_map<uint32_t, float> wantedTimer;   // player id -> guard-aggro time left
    uint32_t     nextBanditId = 0x80000000u;   // id range disjoint from villagers
    std::mt19937 rng{0x4E504332u};
};

// Deterministic procedural villager identity, keyed off the appearance seed.
std::string npcName(uint32_t seed);
std::string npcFlavorLine(uint32_t seed, int variant);
