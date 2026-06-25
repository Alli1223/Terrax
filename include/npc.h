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
class FarmDirector;
struct Town;

// What an NPC is and how it behaves. Villagers and guards populate towns,
// enemies (bandits + dungeon foes) are hostile, farmers work the fields.
// Append new values only — npcType is serialised by value in NPCState.
enum class NPCType : uint8_t { Villager = 0, Enemy = 1, Guard = 2, Farmer = 3,
                              Skeleton = 4, Brute = 5, Cultist = 6,
                              Trainer = 7,      // static town "Class Trainer" — role swap
                              Questgiver = 8,   // static town quest-giver
                              Zombie = 9,       // shambling undead melee (crypts/ruins)
                              Knight = 10,      // fallen plate knight — sword + shield (castles)
                              Vendor = 11,      // static town merchant — buy/sell gear
                              Necromancer = 12, // ranged undead caster — staff bolts (ruins boss)
                              Ghoul = 13,       // fast, fragile undead melee — claws
                              Brigand = 14,     // tougher bandit — axe, more HP (caves/camps)
                              Wraith = 15,      // fast ethereal undead — chilling claws
                              Lich = 16,        // undead arch-caster — frost bolts (crypt boss)
                              Warlord = 17 };   // armoured war-commander — greatsword (castle boss)

// True for hostile NPC types — the town watch fights them and the player can
// kill them for loot/XP. Extended as new enemy types are added.
inline bool isHostileNpc(NPCType t) {
    return t == NPCType::Enemy || t == NPCType::Skeleton ||
           t == NPCType::Brute || t == NPCType::Cultist ||
           t == NPCType::Zombie || t == NPCType::Knight ||
           t == NPCType::Necromancer || t == NPCType::Ghoul ||
           t == NPCType::Brigand || t == NPCType::Wraith ||
           t == NPCType::Lich || t == NPCType::Warlord;
}

// Spawn health by type — brutes are tanky, skeletons brittle.
inline float defaultNpcHealth(NPCType t) {
    switch (t) {
        case NPCType::Brute:    return 220.0f;
        case NPCType::Skeleton: return 60.0f;
        case NPCType::Cultist:  return 90.0f;
        case NPCType::Zombie:      return 130.0f;   // slow but soaks hits
        case NPCType::Knight:      return 200.0f;   // armoured, near-boss durability
        case NPCType::Necromancer: return 140.0f;   // caster boss — chunky
        case NPCType::Ghoul:       return 70.0f;    // fast and fragile
        case NPCType::Brigand:     return 150.0f;   // a hardened bandit
        case NPCType::Wraith:      return 80.0f;    // ethereal, brittle
        case NPCType::Lich:        return 180.0f;   // undead arch-caster boss
        case NPCType::Warlord:     return 260.0f;   // heavily armoured boss
        default:                   return 100.0f;   // bandits and the rest
    }
}

// --- Level scaling (Track B) -------------------------------------------------
// An enemy's level is derived from the danger tier of its spawn point (see
// dangerTierAt in world.h): tier 1 (home) → level 1, each further tier adds 5,
// so the outermost tier 12 fields ~level 56 foes. Bosses sit a few levels above
// the trash around them.
inline int enemyLevelForTier(int tier) {
    int lv = 1 + (tier - 1) * 5;
    return lv < 1 ? 1 : lv;
}

// HP / damage multipliers as a function of enemy level. Gentle linear ramps so a
// level-50 foe is meaningfully tankier and hits harder, without being absurd.
inline float npcHpScaleForLevel(int level)     { return 1.0f + 0.12f * (float)(level - 1); }
inline float npcDamageScaleForLevel(int level) { return 1.0f + 0.08f * (float)(level - 1); }

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
    uint8_t  level          = 1;      // hostile-NPC level (from spawn danger tier)
    uint32_t appearanceSeed = 0;
    bool     walking        = false;
    bool     sitting        = false;  // server→client (flags bit 3): seated pose

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
    uint32_t  tauntedBy       = 0;      // server: client whose taunt locked this NPC's aggro
    float     tauntTimer      = 0.0f;   // server: seconds of forced aggro remaining

    // --- Server-side AI state (owned by NpcDirector) ---
    int       townIndex = -1;           // villager: home town index
    uint64_t   campKey   = 0;            // enemy: owning camp key
    bool      raiding   = false;         // enemy: currently marching on a town
    glm::vec2 raidTarget{0.0f};          // enemy: the town-edge point being raided
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

    // Daytime "use the town" routine (server-only): what to do at the end of the
    // current route. pendingAct — 0 = nothing, 1 = sit on the bench at restAnchor,
    // 2 = linger here a while (gather at the plaza / loiter by a shop).
    glm::vec3 restAnchor{0.0f};         // world XYZ of the seat (incl. seat height)
    float     restYaw    = 0.0f;        // facing while seated
    uint8_t   pendingAct = 0;
    bool      worker     = false;       // villager lives in a trade building — tends it by day
    int       seatIndex  = -1;          // claimed bench seat (index into the town seat list), or -1

    // Server-only: set to true the first time loot was rolled for this
    // NPC's death so we don't spawn loot on every overkill swing.
    bool      lootDropped = false;
    bool      boss        = false;  // dungeon boss — drops legendary loot on death
    float     bossSlamCd   = 3.0f;  // server: cooldown until the boss can slam again
    float     bossSlamWind = -1.0f; // server: >=0 while winding up a slam (telegraph), then impact

    // Read-only access for systems that need to inspect a dying NPC's
    // voxels (e.g. the death-explosion particle spawner).
    const BipedalRig* getRig() const { return rig; }

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

// A place a villager can rest by day — a plaza bench seat. Derived once per town
// from the deterministic prop placements so NPCs sit on the actual furniture.
struct SeatSpot {
    glm::vec3 pos;     // world XYZ of the seat surface (sitting anchor)
    float     yaw;     // facing while seated (the bench's own facing)
};

// Damage an NPC dealt to a player; the server loop drains it each tick.
struct PlayerDamage {
    uint32_t playerId;
    float    amount;
};

// A hostile NPC's projectile, simulated server-side. The client renders a
// matching visual spawned from an EnemyProjectileSpawnPacket; the server owns
// the flight + collision so a player can dodge by moving out of its path.
struct EnemyProjectile {
    uint32_t  id;
    glm::vec3 pos;
    glm::vec3 vel;
    float     ttl;
    float     damage;
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
    void playerHitNpc(uint32_t attackerId, uint32_t npcId, glm::vec3 attackerPos,
                       float damageScale = 1.0f);

    // Area damage to hostile NPCs within `radius` of `center` (e.g. DPS Cleave).
    void playerAoe(uint32_t attackerId, glm::vec3 center, float radius, float damageScale);

    // Force hostile NPCs within `radius` to fixate on the casting player for
    // `duration` seconds (Tank taunt).
    void playerTaunt(uint32_t attackerId, glm::vec3 center, float radius, float duration);

    // Damage NPCs dealt to players this tick; the server loop drains it.
    std::vector<PlayerDamage> pendingDamage;

    // Set by the server thread: lets farmer NPCs query/edit crop state.
    FarmDirector* farmDir = nullptr;

private:
    void populateTown(int townIndex);
    void depopulateTown(int townIndex);
    // Lazily builds (and caches) the list of plaza bench seats for a town.
    const std::vector<SeatSpot>& townSeats(int townIndex);
    // Frees a villager's claimed bench seat (if any) so someone else may use it.
    void releaseSeat(NPC& n);
    // Shared NPC-damage core for both the single-target hit and the AoE sweep:
    // town protection, villager panic, the wanted flag, HP loss and kill->loot.
    void applyPlayerDamageToNpc(NPC& n, uint32_t attackerId,
                                const glm::vec3& attackerPos, float damageScale);
    void stepVillager(NPC& n, float dt, World& world, float gameTime);
    void stepFarmer(NPC& n, float dt, World& world, float gameTime);
    void stepGuard(NPC& n, float dt, World& world,
                   const std::vector<DirectorPlayer>& players);

    void updateCamps(const std::vector<DirectorPlayer>& players);
    const Camp& evalCamp(int gx, int gz, uint64_t key);
    void spawnCamp(uint64_t key, const Camp& camp);
    void despawnCamp(uint64_t key);
    void stepBandit(NPC& n, float dt, World& world,
                    const std::vector<DirectorPlayer>& players);
    // Dungeon-boss signature move: a telegraphed ground-slam that booms an AoE on
    // nearby players after a brief wind-up (so they can step out of the ring).
    void stepBossSpecial(NPC& n, float dt, const std::vector<DirectorPlayer>& players);
    void stepRangedEnemy(NPC& n, float dt, World& world,
                         const std::vector<DirectorPlayer>& players);

    // Hostile-NPC projectiles, simulated server-side. spawnEnemyProjectile both
    // records the bolt and broadcasts an EnemyProjectileSpawn so clients render
    // it; stepEnemyProjectiles advances them and applies damage on a player hit.
    void spawnEnemyProjectile(glm::vec3 origin, glm::vec3 vel, float damage);
    void stepEnemyProjectiles(float dt, const std::vector<DirectorPlayer>& players, World& world);
    std::vector<EnemyProjectile> enemyProjectiles;
    uint32_t nextEnemyProjId = 1;

    // Farms: stream farmer NPCs in/out by player proximity (crop state itself
    // lives in the FarmDirector). A farmer's n.townIndex is its farm index.
    void updateFarms(const std::vector<DirectorPlayer>& players);
    void spawnFarmers(int farmIdx);
    void despawnFarmers(int farmIdx);

    // Dungeons: stream enemy packs in/out by player proximity. Spawned foes
    // carry the dungeon index in n.townIndex and ids in the 0xA0000000 range.
    void updateDungeons(const std::vector<DirectorPlayer>& players);
    void spawnDungeon(size_t dungeonIdx);
    void despawnDungeon(size_t dungeonIdx);

    bool inAnyTown(glm::vec2 worldXZ) const;

    std::vector<std::unique_ptr<NPC>> active;
    std::unordered_map<int, TownNav>  navCache;
    std::unordered_map<int, std::vector<SeatSpot>> seatCache;
    std::unordered_map<int, std::vector<uint8_t>>  seatTaken;   // 1 = a villager holds this seat
    std::unordered_set<int>           populated;
    std::unordered_set<int>           populatedFarms;
    std::unordered_map<uint64_t, Camp> campCache;
    std::unordered_set<uint64_t>       activeCamps;
    std::unordered_set<size_t>         activeDungeons;
    std::unordered_map<uint32_t, float> wantedTimer;   // player id -> guard-aggro time left
    uint32_t     nextFarmerId = 0x70000000u;   // farmers: id range below the bandits
    uint32_t     nextBanditId = 0x80000000u;   // id range disjoint from villagers
    uint32_t     nextDungeonEnemyId = 0xA0000000u;  // dungeon foes; below animals (0xC0000000)
    std::mt19937 rng{0x4E504332u};
};

// Deterministic procedural villager identity, keyed off the appearance seed.
std::string npcName(uint32_t seed);
std::string npcFlavorLine(uint32_t seed, int variant);
