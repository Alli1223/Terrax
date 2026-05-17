#pragma once
#include <boost/asio.hpp>
#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <thread>
#include <iostream>
#include <unordered_set>
#include <glm/glm.hpp>
#include "world.h"
#include "voxel_model.h"

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

enum class PacketType : uint8_t {
    Handshake = 0,
    ChunkData = 1,
    BlockUpdate = 2,
    PlayerPos = 3, // UDP
    PlayerDisconnect = 4,
    ChunkRequest = 5,
    PlayerModel = 6,
    PlayerAttack = 7,
    PlayerHealth = 8
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
    uint32_t clientID; // Server assigns this
};
#pragma pack(pop)

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
    uint32_t id;
    glm::vec3 position;
    glm::vec3 targetPosition;
    float pitch, yaw;
    float targetPitch, targetYaw;
    double lastUpdate;
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

    struct PlayerState {
        uint32_t id;
        glm::vec3 pos;
    };
    std::vector<PlayerState> getPlayerStates();

private:
    void doAccept();
    void doReceiveUDP();
    
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
    std::mutex modelsMutex;

    uint32_t nextClientID = 1;

    struct QueuedMessage {
        std::shared_ptr<Connection> client;
        PacketType type;
        std::vector<uint8_t> data;
        udp::endpoint udp_sender; // For UDP packets
    };
    std::queue<QueuedMessage> messageQueue;
    std::mutex queueMutex;
};

class NetworkClient {
public:
    NetworkClient();
    bool connect(const std::string& host, unsigned short port);
    void update(World& world, std::unordered_map<uint32_t, RemotePlayer>& players);
    void send(PacketType type, const void* data, size_t size);
    void sendUDP(const void* data, size_t size);
    
    uint32_t clientID = 0;
    bool connected = false;

private:
    void doReceiveUDP();

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
};
