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
};

#pragma pack(push, 1)
struct PacketHeader {
    PacketType type;
    uint32_t size;
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
    int armorType;
};

struct PlayerAttackPacket {
    uint32_t clientID;
};

struct PlayerHealthPacket {
    uint32_t clientID;
    float health;
};

struct ChunkRequestPacket {
    int x, z;
};

struct PlayerPosPacket {
    uint32_t id;
    float x, y, z;
    float pitch, yaw;
};

struct BlockUpdatePacket {
    int x, y, z;
    uint8_t type;
};

struct HandshakePacket {
    uint32_t clientID;
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
#pragma pack(pop)

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
};

class NetworkServer {
public:
    NetworkServer(unsigned short port);
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
    std::vector<ChatMessage> chatLog;
    static constexpr size_t MAX_CHAT_LOG = 100;

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
