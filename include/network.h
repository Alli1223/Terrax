#pragma once
#include <boost/asio.hpp>
#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <thread>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <array>
#include <glm/glm.hpp>
#include "world.h"
#include "voxel_model.h"

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

static constexpr float BLOCK_REACH = 5.0f;
static constexpr float BLOCK_REACH_SQ = BLOCK_REACH * BLOCK_REACH;
static constexpr size_t MAX_PLAYER_NAME = 31;
static constexpr size_t MAX_CHAT_TEXT = 199;

enum class PacketType : uint8_t {
    Handshake = 0,
    ChunkData = 1,
    BlockUpdate = 2,
    PlayerPos = 3, // UDP
    PlayerDisconnect = 4,
    ChunkRequest = 5,
    PlayerModel = 6,
    PlayerAttack = 7,
    PlayerHealth = 8,
    Chat = 9,
    PlayerJoin = 10,
    DayTime = 11,
    HousePlace = 12,
    EntityState = 13,
    NPCState = 14,
    AnimalState = 15,
    LootSpawn = 16,             // server -> all: a new world loot drop appeared
    LootPickupRequest = 17,     // client -> server: I'm trying to pick up drop N
    LootRemoved = 18,           // server -> all: drop N is gone (picked up / expired)
    DropItemRequest = 19,       // client -> server: drop one of my items at pos P
    PlayerHeal = 20,            // client -> server -> all: a heal applied to a player
    SpellEffect = 21,           // client -> server -> all: cosmetic spell effect to spawn
    AbilityCast = 22,           // client -> server: area/aggro ability for the server to resolve
    EnemyProjectileSpawn = 23,  // server -> all: a hostile NPC fired a bolt; clients render it
};

#pragma pack(push, 1)
struct PacketHeader {
    PacketType type;
    uint32_t size;
};

// Equipped-item entry shared in PlayerModelHeader. One per slot in the order
// [Helmet, Shoulders, Chest, Legs, Feet, MainHand, OffHand]. kind=0 means
// the slot is empty; kind=1 is clothing (variant = ClothingTier); kind=2 is
// a weapon (variant = WeaponType). `rarity` (ItemRarity) drives the extra
// visual flourishes painted by the receiver. The two colour fields drive
// the remote-side rendering — no item names or stats are synced.
struct EquippedSlotState {
    uint8_t kind;
    uint8_t variant;
    uint8_t rarity;
    Voxel   primary;
    Voxel   accent;
};

struct PlayerModelHeader {
    uint32_t clientID;
    int hairStyle;
    Voxel hairColor;
    Voxel eyeColor;
    int eyeType;
    int noseStyle;
    int eyebrowStyle;
    int earType;
    int armorType;                  // legacy, kept zeroed by new clients
    EquippedSlotState slots[7];     // current loadout
    int32_t playerLevel;            // 1+; used by server to scale loot rolls
};

struct PlayerAttackPacket {
    uint32_t clientID;
    uint32_t targetNpcId;   // NPC the swing landed on, 0 = none
    float    damageScale;   // 1.0 = melee default; bow scales by charge
};

// Server -> client: damage an NPC dealt to a player.
struct PlayerHealthPacket {
    uint32_t clientID;
    float    damage;
};

struct ChunkRequestPacket {
    int x, z;
};

struct PlayerPosPacket {
    uint32_t id;
    float x, y, z;
    float pitch, yaw;
    uint8_t lanternHeld;
    uint8_t shieldRaised;   // 1 = off-hand shield in block stance
};

struct BlockUpdatePacket {
    int x, y, z;
    uint8_t type;
};

struct HandshakePacket {
    uint32_t clientID;
    uint32_t worldSeed;   // server's world seed. The client adopts it so every
                          // seed-derived thing it builds locally (terrain oracle,
                          // town plan, props, vegetation, NPC placement, the map)
                          // matches the server's authoritative world.
};

struct PlayerDisconnectPacket {
    uint32_t clientID;
};

struct PlayerJoinPacket {
    uint32_t clientID;
    char name[MAX_PLAYER_NAME + 1];
};

struct ChatPacket {
    uint32_t senderID;
    char text[MAX_CHAT_TEXT + 1];
};

struct DayTimePacket {
    float gameTime;
};

// HousePlace payload: this header, followed by dimX*dimY*dimZ bytes of BlockType.
// Block index order is ((y * dimZ) + z) * dimX + x. The grid is pre-rotated by
// the client so the server can stamp it axis-aligned at (worldX, baseY, worldZ).
struct HousePlaceHeader {
    int worldX, baseY, worldZ;   // world position of the grid's (0,0,0) corner
    int dimX, dimY, dimZ;        // dimensions of the block grid that follows
};

// Server -> client state for a non-player object (currently ferries). The
// server owns the motion; clients interpolate and render.
struct EntityStatePacket {
    uint32_t entityId;
    uint8_t  kind;       // ObjectKind value
    uint8_t  subType;    // 0 = ferry
    float    x, y, z;
    float    yaw;
    float    vx, vy, vz;
};

// Server -> client state for one NPC. Like EntityStatePacket the server owns
// the simulation; clients create one NPC per id, interpolate and render it.
struct NPCStatePacket {
    uint32_t entityId;
    uint8_t  npcType;        // NPCType value
    uint8_t  flags;          // bit 0 = walking
    uint8_t  level;          // hostile-NPC level (from spawn danger tier); 0 for townsfolk
    uint32_t appearanceSeed; // seed for deterministic procedural appearance
    float    x, y, z;
    float    yaw;
    float    vx, vy, vz;
    float    health;
};

// Server -> client state for one wild animal.
struct AnimalStatePacket {
    uint32_t entityId;
    uint8_t  species;        // AnimalSpecies value
    uint8_t  flags;          // bit 0 = walking
    uint32_t variant;        // per-individual size / colour variation
    float    x, y, z;
    float    yaw;
    float    vx, vy, vz;
};

// Server -> all clients: a new world-space loot drop has spawned. Carries
// the full item data so clients can reconstruct an identical Item object
// without rerunning the procedural generator (which would otherwise
// diverge between clients/server). Name is null-terminated, truncated.
struct LootSpawnPacket {
    uint32_t dropId;
    float    x, y, z;
    uint8_t  kind;          // 1 = clothing, 2 = weapon
    uint8_t  subtype;       // ClothingTier or WeaponType
    uint8_t  slot;          // EquipSlot
    uint8_t  rarity;        // ItemRarity
    int32_t  level;
    Voxel    primary;
    Voxel    accent;
    int32_t  patternSeed;
    float    attackPower;
    float    defenseValue;
    char     name[64];
    char     setName[24];   // empty string = free-roll, no set
    uint8_t  element;       // WeaponElement value (None/Fire/Ice/Arcane)
};

// Client -> server: I want to pick up drop N. Server validates distance
// and replies with LootRemoved if accepted.
struct LootPickupRequestPacket {
    uint32_t dropId;
};

// Server -> all clients: drop N has been removed from the world.
// `newOwnerId` is the clientID that picked it up, or 0 if the drop
// expired without being claimed.
struct LootRemovedPacket {
    uint32_t dropId;
    uint32_t newOwnerId;
};

// Client -> server: I want to drop this item from my inventory into the
// world at my feet. The server takes the included item data (same
// payload as a LootSpawn except no dropId — server assigns one) and
// broadcasts it as a regular world drop so all players see it. This is
// how the player shares loot with teammates.
struct DropItemRequestPacket {
    float    x, y, z;
    uint8_t  kind;          // 1 = clothing, 2 = weapon
    uint8_t  subtype;
    uint8_t  slot;
    uint8_t  rarity;
    int32_t  level;
    Voxel    primary;
    Voxel    accent;
    int32_t  patternSeed;
    float    attackPower;
    float    defenseValue;
    char     name[64];
    char     setName[24];
    uint8_t  element;
};

// Client -> server -> all: a heal applied to one player. The caster sends
// one packet per healed target; the server rebroadcasts (skipping the
// caster). The targeted client adds `amount` to its own health (health is
// client-owned, same as damage), and EVERY client spawns a heal sparkle at
// (x,y,z) so the heal reads visibly for spectators too. This is the heal
// mirror of PlayerHealthPacket.
struct PlayerHealPacket {
    uint32_t targetID;
    float    amount;     // HP restored (same 0..100 scale as PlayerHealthPacket.damage)
    float    x, y, z;    // world position for the heal sparkle
};

// Client -> server -> all: a cosmetic spell effect for spectators to spawn.
// The caster already renders its own copy locally; the server rebroadcasts
// to the other clients so they see the chain beam / healing zone as well.
// No gameplay authority rides on this packet — actual healing travels via
// PlayerHealPacket.
struct SpellEffectPacket {
    uint32_t casterID;
    uint8_t  kind;        // 0=healing zone, 1=chain-heal burst, 2=aoe slash,
                          // 3=taunt ring, 4=cast flash, 5=buff aura
    float    x, y, z;
    float    radius;      // zone radius in world units (0 for point effects)
    float    ttl;         // seconds the effect should live
};

// Client -> server: a player activated an area/aggro ability the server must
// resolve authoritatively. Single-target abilities don't use this — they ride
// PlayerAttack. `effect` selects the resolution: 0 = AoE damage (scale = damage
// multiplier), 1 = taunt (scale = duration in seconds). Not rebroadcast; the
// spectator visuals ride SpellEffect instead.
struct AbilityCastPacket {
    uint32_t casterID;
    uint16_t abilityId;   // AbilityId, for server-side logic / logging
    uint8_t  effect;      // 0 = aoe damage, 1 = taunt
    uint8_t  _pad;
    float    x, y, z;     // cast centre
    float    radius;
    float    scale;       // aoe: damage multiplier; taunt: duration seconds
};

// Server -> all: a hostile NPC fired a projectile. Clients spawn a visible bolt
// that flies the given straight path (no gravity) for `ttl` seconds. The server
// simulates the same flight authoritatively and applies damage on a player hit,
// so dodging by moving works. Fire-and-forget — no per-tick state sync; the
// client dead-reckons the straight line and self-expires on terrain / ttl.
struct EnemyProjectileSpawnPacket {
    uint32_t id;
    float    x, y, z;        // origin
    float    vx, vy, vz;     // velocity (units/sec)
    float    ttl;            // seconds to live
    uint8_t  type;           // 0 = cultist bolt (room for more visuals later)
};
#pragma pack(pop)

// A melee/ranged hit a client landed on an NPC; the server game loop
// resolves it. `damageScale` is 1.0 for a default melee swing and scales
// with bow charge / weapon power.
struct NpcHitEvent {
    uint32_t attackerId;
    uint32_t npcId;
    float    damageScale;
};

// A parsed AbilityCast a client sent this frame; the server game loop resolves
// it in NpcDirector (AoE damage to NPCs in radius, or a taunt that fixes their
// aggro on the caster). `effect`: 0 = aoe damage, 1 = taunt.
struct AbilityCastEvent {
    uint32_t  attackerId;
    uint8_t   effect;
    glm::vec3 center;
    float     radius;
    float     scale;        // aoe: damage multiplier; taunt: duration seconds
};

struct ChatMessage {
    uint32_t senderID = 0;
    std::string senderName;
    std::string text;
    double timestamp = 0.0;
};

class Connection : public std::enable_shared_from_this<Connection> {
public:
    tcp::socket socket;
    uint32_t id = 0;
    std::vector<uint8_t> readBuffer;
    std::unordered_set<ChunkPos, ChunkPosHash> sentChunks;
    
    Connection(tcp::socket s) : socket(std::move(s)) {}
    
    void start(std::function<void(std::shared_ptr<Connection>, PacketType, std::vector<uint8_t>)> onMessage,
               std::function<void(std::shared_ptr<Connection>)> onDisconnect);
    
    void send(PacketType type, const void* data, size_t size);

private:
    void doReadHeader();
    void doReadBody(PacketHeader header);
    void doWrite();
    
    std::function<void(std::shared_ptr<Connection>, PacketType, std::vector<uint8_t>)> messageHandler;
    std::function<void(std::shared_ptr<Connection>)> disconnectHandler;

    std::queue<std::vector<uint8_t>> writeQueue;
    std::mutex writeMutex;
};

struct RemotePlayer {
    uint32_t id = 0;
    std::string name;
    glm::vec3 position{0.0f};
    glm::vec3 targetPosition{0.0f};
    float pitch = 0.0f, yaw = 0.0f;
    float targetPitch = 0.0f, targetYaw = 0.0f;
    double lastUpdate = 0.0;
    BipedalRig* rig = nullptr;
    float health = 100.0f;
    bool isAttacking = false;
    float attackAnim = 0.0f;
    bool lanternHeld = false;
    bool shieldRaised = false;
};

class NetworkServer {
public:
    NetworkServer(unsigned short port);
    ~NetworkServer();
    void update(World& world);
    void broadcast(PacketType type, const void* data, size_t size, std::shared_ptr<Connection> skip = nullptr);
    void broadcastUDP(const void* data, size_t size);
    void sendChunk(std::shared_ptr<Connection> client, const Chunk& chunk);
    void sendExistingPlayersTo(std::shared_ptr<Connection> client);

    struct PlayerState {
        uint32_t id;
        glm::vec3 pos;
    };
    std::vector<PlayerState> getPlayerStates();
    glm::vec3 getPlayerPosition(uint32_t clientId);
    std::string getPlayerName(uint32_t clientId);

    // Melee hits clients landed on NPCs this frame; drained by the game loop.
    std::vector<NpcHitEvent> npcHits;

    // Area/aggro ability casts clients sent this frame; drained by the game loop.
    std::vector<AbilityCastEvent> abilityCasts;

    // Server-owned list of active world loot drops. Each entry mirrors the
    // LootSpawnPacket payload plus a spawn timestamp for expiry. Adding /
    // removing entries is the canonical operation — broadcasts wrap that.
    struct ServerLootEntry {
        LootSpawnPacket pkt;
        double          spawnTime;   // glfwGetTime() / steady_clock when spawned
    };
    std::vector<ServerLootEntry> activeLoot;
    std::mutex                   lootMutex;
    uint32_t                     nextLootId = 1;

    // Convenience for callers (npc.cpp, game_session.cpp) that don't want
    // to deal with the playerModels map directly.
    int  getPlayerLevel(uint32_t clientId);

    // Spawn server-side loot for an enemy killed by `attackerId` at `pos`.
    // Rolls items at max(attacker level, enemyLevel) so a low-level player who
    // bags a high-tier foe still gets level-appropriate loot. Records each entry
    // in `activeLoot`, and broadcasts a LootSpawnPacket per drop.
    void spawnLootForKill(uint32_t attackerId, const glm::vec3& pos,
                          bool legendary = false, int enemyLevel = 1,
                          bool elite = false);

    // Try to honour a client's pickup request. If the drop still exists
    // and the requester is within range, removes it and broadcasts
    // LootRemoved naming the requester as the new owner.
    void handleLootPickup(uint32_t clientId, uint32_t dropId,
                          const glm::vec3& clientPos);

    // Player wants to drop one of their items into the world (to share
    // with a teammate, free up bag space, etc). The packet carries the
    // full item payload — server assigns a drop ID, records the entry,
    // broadcasts a LootSpawn so every client sees it.
    void handleDropItemRequest(uint32_t clientId,
                                const DropItemRequestPacket& req);

    // Drop entries older than `maxAge` (seconds). Broadcasts LootRemoved
    // with newOwnerId=0 for each. Call once per tick from the game loop.
    void expireStaleLoot(double now, double maxAge);

    static bool isAllowedBlockType(BlockType t);
    static bool validateBlockUpdate(const BlockUpdatePacket& pkt, const glm::vec3& playerPos, World& world);

private:
    void doAccept();
    void doReceiveUDP();
    void onClientDisconnected(uint32_t clientId);
    
    boost::asio::io_context io_context;
    tcp::acceptor acceptor;
    udp::socket udp_socket;
    udp::endpoint remote_endpoint;
    std::vector<uint8_t> udp_buffer;
    std::thread io_thread;

    std::vector<std::shared_ptr<Connection>> clients;
    std::mutex clientsMutex;
    
    struct ClientUDPInfo {
        uint32_t id;
        udp::endpoint endpoint;
        glm::vec3 lastPos;
    };
    std::vector<ClientUDPInfo> udp_clients;
    std::mutex udpClientsMutex;

    std::unordered_map<uint32_t, PlayerModelHeader> playerModels;
    std::unordered_map<uint32_t, std::string> playerNames;
    std::mutex modelsMutex;

    uint32_t nextClientID = 1;

    struct QueuedMessage {
        std::shared_ptr<Connection> client;
        PacketType type;
        std::vector<uint8_t> data;
        udp::endpoint udp_sender;
    };
    std::queue<QueuedMessage> messageQueue;
    std::mutex queueMutex;
};

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();
    bool connect(const std::string& host, unsigned short port);
    void disconnect();
    void update(World& world, std::unordered_map<uint32_t, RemotePlayer>& players);
    void send(PacketType type, const void* data, size_t size);
    void sendUDP(const void* data, size_t size);
    void sendChat(const std::string& text);
    void sendPlayerJoin(const std::string& name);
    
    uint32_t clientID = 0;
    bool connected = false;
    uint32_t serverWorldSeed = 0;     // world seed from the Handshake (server-authoritative)
    bool     hasWorldSeed    = false; // set once the Handshake delivered the seed
    float serverGameTime = 0.3f;
    bool hasServerGameTime = false;
    bool dayTimeUpdated = false;
    std::vector<ChatMessage> chatLog;
    static constexpr size_t MAX_CHAT_LOG = 100;
    std::vector<EntityStatePacket>  entityUpdates;   // drained by gameplay each frame
    std::vector<NPCStatePacket>     npcUpdates;      // drained by gameplay each frame
    std::vector<AnimalStatePacket>  animalUpdates;   // drained by gameplay each frame
    std::vector<LootSpawnPacket>    lootSpawns;      // drained by gameplay each frame
    std::vector<LootRemovedPacket>  lootRemovals;    // drained by gameplay each frame
    std::vector<PlayerHealPacket>   healEvents;      // heals to apply/show, drained by gameplay
    std::vector<SpellEffectPacket>  spellEffects;    // cosmetic spell fx, drained by gameplay
    std::vector<EnemyProjectileSpawnPacket> enemyProjectiles;  // enemy bolts to render, drained by gameplay
    float pendingSelfDamage = 0.0f;                  // damage dealt to us, drained by gameplay
    float pendingSelfHeal   = 0.0f;                  // heal dealt to us, drained by gameplay

private:
    void doReceiveUDP();
    void pushChat(uint32_t senderId, const std::string& senderName, const std::string& text);
    static void copyStringField(char* dest, size_t destSize, const std::string& src);
    
    boost::asio::io_context io_context;
    std::shared_ptr<Connection> connection;
    
    udp::socket udp_socket;
    udp::endpoint server_udp_endpoint;
    std::vector<uint8_t> udp_buffer;

    struct QueuedMessage {
        PacketType type;
        std::vector<uint8_t> data;
    };
    std::queue<QueuedMessage> messageQueue;
    std::mutex queueMutex;
    std::thread io_thread;
    bool joinSent = false;
};

extern NetworkServer* g_server;
