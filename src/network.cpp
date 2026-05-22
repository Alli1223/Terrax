#include "network.h"
#include "game_session.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>

NetworkServer* g_server = nullptr;

static void ensureRemoteRig(BipedalRig*& rig) {
    if (!rig) {
        rig = new BipedalRig();
        rig->setupDefaultHuman(true);
        return;
    }
    if (!rig->torso || !rig->torso->volume)
        rig->setupDefaultHuman(true);
}

// --- Connection ---

void Connection::start(std::function<void(std::shared_ptr<Connection>, PacketType, std::vector<uint8_t>)> onMessage,
                       std::function<void(std::shared_ptr<Connection>)> onDisconnect) {
    messageHandler = onMessage;
    disconnectHandler = onDisconnect;
    doReadHeader();
}

void Connection::doReadHeader() {
    auto self = shared_from_this();
    auto header = std::make_shared<PacketHeader>();
    boost::asio::async_read(socket, boost::asio::buffer(header.get(), sizeof(PacketHeader)),
        [this, self, header](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                doReadBody(*header);
            } else if (disconnectHandler) {
                disconnectHandler(self);
            }
        });
}

void Connection::doReadBody(PacketHeader header) {
    auto self = shared_from_this();
    auto body = std::make_shared<std::vector<uint8_t>>(header.size);
    if (header.size == 0) {
        if (messageHandler) messageHandler(self, header.type, *body);
        doReadHeader();
        return;
    }
    boost::asio::async_read(socket, boost::asio::buffer(body->data(), body->size()),
        [this, self, header, body](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                if (messageHandler) messageHandler(self, header.type, *body);
                doReadHeader();
            } else if (disconnectHandler) {
                disconnectHandler(self);
            }
        });
}

void Connection::send(PacketType type, const void* data, size_t size) {
    PacketHeader header { type, (uint32_t)size };
    std::vector<uint8_t> packet(sizeof(PacketHeader) + size);
    memcpy(packet.data(), &header, sizeof(PacketHeader));
    if (size > 0 && data) memcpy(packet.data() + sizeof(PacketHeader), data, size);
    
    std::lock_guard<std::mutex> lock(writeMutex);
    bool idle = writeQueue.empty();
    writeQueue.push(std::move(packet));
    if (idle) doWrite();
}

void Connection::doWrite() {
    boost::asio::async_write(socket, boost::asio::buffer(writeQueue.front().data(), writeQueue.front().size()),
        [this, self = shared_from_this()](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(writeMutex);
                writeQueue.pop();
                if (!writeQueue.empty()) doWrite();
            } else if (disconnectHandler) {
                disconnectHandler(self);
            }
        });
}

// --- NetworkServer helpers ---

bool NetworkServer::isAllowedBlockType(BlockType t) {
    switch (t) {
        case BlockType::Air:
        case BlockType::Stone:
        case BlockType::Dirt:
        case BlockType::Grass:
        case BlockType::Sand:
        case BlockType::Wood:
        case BlockType::Leaves:
            return true;
        default:
            return false;
    }
}

bool NetworkServer::validateBlockUpdate(const BlockUpdatePacket& pkt, const glm::vec3& playerPos,
                                      World& world) {
    if (pkt.y < 0 || pkt.y >= CHUNK_HEIGHT) return false;

    BlockType newType = (BlockType)pkt.type;
    if (!isAllowedBlockType(newType)) return false;

    glm::vec3 eye = playerPos + glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 blockCenter((float)pkt.x + 0.5f, (float)pkt.y + 0.5f, (float)pkt.z + 0.5f);
    glm::vec3 delta = blockCenter - eye;
    if (glm::dot(delta, delta) > BLOCK_REACH_SQ + 0.25f) return false;

    BlockType current = world.getBlock(pkt.x, pkt.y, pkt.z);

    if (newType == BlockType::Air) {
        return current != BlockType::Air && current != BlockType::Water;
    }

    // Placement: target cell must be air; must be adjacent to a solid block.
    if (current != BlockType::Air) return false;

    static const int dirs[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    bool hasNeighbor = false;
    for (auto& d : dirs) {
        BlockType nb = world.getBlock(pkt.x + d[0], pkt.y + d[1], pkt.z + d[2]);
        if (nb != BlockType::Air && nb != BlockType::Water) {
            hasNeighbor = true;
            break;
        }
    }
    return hasNeighbor;
}

void NetworkServer::onClientDisconnected(uint32_t clientId) {
    {
        std::lock_guard<std::mutex> lock(modelsMutex);
        playerModels.erase(clientId);
        playerNames.erase(clientId);
    }
    PlayerDisconnectPacket dp { clientId };
    broadcast(PacketType::PlayerDisconnect, &dp, sizeof(dp));
    std::cout << "Client disconnected: ID " << clientId << std::endl;
}

glm::vec3 NetworkServer::getPlayerPosition(uint32_t clientId) {
    std::lock_guard<std::mutex> lock(udpClientsMutex);
    for (auto& uc : udp_clients) {
        if (uc.id == clientId) return uc.lastPos;
    }
    return glm::vec3(0.0f);
}

std::string NetworkServer::getPlayerName(uint32_t clientId) {
    std::lock_guard<std::mutex> lock(modelsMutex);
    auto it = playerNames.find(clientId);
    if (it != playerNames.end()) return it->second;
    return "Player" + std::to_string(clientId);
}

void NetworkServer::sendExistingPlayersTo(std::shared_ptr<Connection> client) {
    std::lock_guard<std::mutex> lock(modelsMutex);
    for (auto& [id, name] : playerNames) {
        if (id == client->id) continue;
        PlayerJoinPacket pj {};
        pj.clientID = id;
        strncpy(pj.name, name.c_str(), MAX_PLAYER_NAME);
        pj.name[MAX_PLAYER_NAME] = '\0';
        client->send(PacketType::PlayerJoin, &pj, sizeof(pj));
    }
}

// --- NetworkServer ---

NetworkServer::NetworkServer(unsigned short port)
    : acceptor(io_context, tcp::endpoint(tcp::v4(), port)),
      udp_socket(io_context, udp::endpoint(udp::v4(), port)),
      udp_buffer(1024) {
    doAccept();
    doReceiveUDP();
    io_thread = std::thread([this]() { io_context.run(); });
}

NetworkServer::~NetworkServer() {
    // Stop the io_context and join its thread before any members are destroyed,
    // otherwise the still-running io thread dereferences freed vectors/queues.
    io_context.stop();
    if (io_thread.joinable()) io_thread.join();
}

void NetworkServer::doAccept() {
    acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            auto conn = std::make_shared<Connection>(std::move(socket));
            conn->id = nextClientID++;
            
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.push_back(conn);
            }
            
            std::cout << "Client connected: ID " << conn->id << std::endl;
            
            HandshakePacket hp { conn->id };
            conn->send(PacketType::Handshake, &hp, sizeof(hp));

            DayTimePacket dt { getServerGameTime() };
            conn->send(PacketType::DayTime, &dt, sizeof(dt));

            {
                std::lock_guard<std::mutex> lock(modelsMutex);
                for (auto& [id, model] : playerModels) {
                    (void)id;
                    conn->send(PacketType::PlayerModel, &model, sizeof(model));
                }
            }
            sendExistingPlayersTo(conn);

            {
                std::lock_guard<std::mutex> lock(udpClientsMutex);
                for (auto& uc : udp_clients) {
                    if (uc.id == conn->id) continue;
                    PlayerPosPacket pp {};
                    pp.id = uc.id;
                    pp.x = uc.lastPos.x;
                    pp.y = uc.lastPos.y;
                    pp.z = uc.lastPos.z;
                    conn->send(PacketType::PlayerPos, &pp, sizeof(pp));
                }
            }

            conn->start(
                [this](std::shared_ptr<Connection> c, PacketType type, std::vector<uint8_t> data) {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    messageQueue.push({c, type, std::move(data)});
                },
                [this](std::shared_ptr<Connection> c) {
                    {
                        std::lock_guard<std::mutex> lock(clientsMutex);
                        clients.erase(std::remove(clients.begin(), clients.end(), c), clients.end());
                    }
                    {
                        std::lock_guard<std::mutex> lock(udpClientsMutex);
                        udp_clients.erase(std::remove_if(udp_clients.begin(), udp_clients.end(), 
                            [c](const ClientUDPInfo& uc) { return uc.id == c->id; }), udp_clients.end());
                    }
                    onClientDisconnected(c->id);
                }
            );
        }
        doAccept();
    });
}

void NetworkServer::doReceiveUDP() {
    udp_socket.async_receive_from(boost::asio::buffer(udp_buffer), remote_endpoint,
        [this](boost::system::error_code ec, std::size_t bytes_recvd) {
            if (!ec && bytes_recvd >= sizeof(PacketHeader)) {
                PacketHeader* h = (PacketHeader*)udp_buffer.data();
                if (h->type == PacketType::PlayerPos && h->size == sizeof(PlayerPosPacket)) {
                    PlayerPosPacket* p = (PlayerPosPacket*)(udp_buffer.data() + sizeof(PacketHeader));
                    
                    {
                        std::lock_guard<std::mutex> lock(udpClientsMutex);
                        bool found = false;
                        for (auto& uc : udp_clients) {
                            if (uc.id == p->id) {
                                uc.endpoint = remote_endpoint;
                                uc.lastPos = glm::vec3(p->x, p->y, p->z);
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            udp_clients.push_back({p->id, remote_endpoint, glm::vec3(p->x, p->y, p->z)});
                        }
                    }
                    
                    std::vector<uint8_t> data(h->size);
                    memcpy(data.data(), p, h->size);
                    
                    std::lock_guard<std::mutex> lock(queueMutex);
                    messageQueue.push({nullptr, h->type, std::move(data), remote_endpoint});
                }
            }
            doReceiveUDP();
        });
}

// Floor-divide a world coordinate to its chunk coordinate.
static int chunkOf(int w) {
    return (w < 0 && w % CHUNK_SIZE != 0) ? w / CHUNK_SIZE - 1 : w / CHUNK_SIZE;
}

// Stamps a pre-rotated house block grid into the world and grounds it onto the
// terrain by filling foundation pillars beneath the footprint.
static void stampHouse(World& world, const HousePlaceHeader& h, const uint8_t* blk) {
    auto idx = [&](int x, int y, int z) {
        return ((size_t)y * h.dimZ + z) * h.dimX + x;
    };
    // Build the house (and carve any terrain inside its footprint).
    for (int y = 0; y < h.dimY; y++)
        for (int z = 0; z < h.dimZ; z++)
            for (int x = 0; x < h.dimX; x++)
                world.setBlock(h.worldX + x, h.baseY + y, h.worldZ + z,
                               (BlockType)blk[idx(x, y, z)]);

    // Ground attachment: under every solid footprint column, fill the gap down
    // to the terrain surface so the house never floats over uneven ground.
    for (int z = 0; z < h.dimZ; z++)
        for (int x = 0; x < h.dimX; x++) {
            if ((BlockType)blk[idx(x, 0, z)] == BlockType::Air) continue;
            int wx = h.worldX + x, wz = h.worldZ + z;
            int gy = h.baseY - 1;
            while (gy > 0) {
                BlockType g = world.getBlock(wx, gy, wz);
                if (g != BlockType::Air && g != BlockType::Water) break;
                gy--;
            }
            for (int wy = gy + 1; wy < h.baseY; wy++)
                world.setBlock(wx, wy, wz, BlockType::Stone);
        }
}

void NetworkServer::update(World& world) {
    std::queue<QueuedMessage> localQueue;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        std::swap(localQueue, messageQueue);
    }

    while (!localQueue.empty()) {
        auto msg = localQueue.front();
        localQueue.pop();

        if (msg.type == PacketType::BlockUpdate) {
            if (msg.data.size() == sizeof(BlockUpdatePacket) && msg.client) {
                BlockUpdatePacket* p = (BlockUpdatePacket*)msg.data.data();
                glm::vec3 pos = getPlayerPosition(msg.client->id);
                if (validateBlockUpdate(*p, pos, world)) {
                    world.setBlock(p->x, p->y, p->z, (BlockType)p->type);
                    world.relightAt(p->x, p->y, p->z);
                    broadcast(PacketType::BlockUpdate, p, sizeof(BlockUpdatePacket), msg.client);
                }
            }
        } else if (msg.type == PacketType::ChunkRequest) {
            if (msg.data.size() == sizeof(ChunkRequestPacket)) {
                ChunkRequestPacket* p = (ChunkRequestPacket*)msg.data.data();
                ChunkPos pos { p->x, p->z };
                std::lock_guard<std::mutex> lock(world.chunksMutex);
                auto it = world.chunks.find(pos);
                if (it != world.chunks.end() && it->second->state != ChunkState::Empty && it->second->state != ChunkState::Generating) {
                    sendChunk(msg.client, *it->second);
                    if (msg.client) msg.client->sentChunks.insert(pos);
                }
            }
        } else if (msg.type == PacketType::PlayerPos) {
            broadcastUDP(msg.data.data(), msg.data.size());
        } else if (msg.type == PacketType::PlayerModel) {
            if (msg.data.size() >= sizeof(PlayerModelHeader)) {
                PlayerModelHeader* h = (PlayerModelHeader*)msg.data.data();
                {
                    std::lock_guard<std::mutex> lock(modelsMutex);
                    playerModels[h->clientID] = *h;
                }
                broadcast(PacketType::PlayerModel, msg.data.data(), msg.data.size(), msg.client);
            }
        } else if (msg.type == PacketType::PlayerAttack) {
            broadcast(PacketType::PlayerAttack, msg.data.data(), msg.data.size(), msg.client);
            if (msg.data.size() == sizeof(PlayerAttackPacket) && msg.client) {
                PlayerAttackPacket* ap = (PlayerAttackPacket*)msg.data.data();
                if (ap->targetNpcId != 0)
                    npcHits.push_back({ msg.client->id, ap->targetNpcId });
            }
        } else if (msg.type == PacketType::Chat) {
            if (msg.data.size() == sizeof(ChatPacket) && msg.client) {
                ChatPacket* cp = (ChatPacket*)msg.data.data();
                cp->senderID = msg.client->id;
                cp->text[MAX_CHAT_TEXT] = '\0';
                broadcast(PacketType::Chat, cp, sizeof(ChatPacket));
            }
        } else if (msg.type == PacketType::PlayerJoin) {
            if (msg.data.size() == sizeof(PlayerJoinPacket) && msg.client) {
                PlayerJoinPacket* pj = (PlayerJoinPacket*)msg.data.data();
                pj->clientID = msg.client->id;
                pj->name[MAX_PLAYER_NAME] = '\0';
                {
                    std::lock_guard<std::mutex> lock(modelsMutex);
                    playerNames[pj->clientID] = pj->name;
                }
                broadcast(PacketType::PlayerJoin, pj, sizeof(PlayerJoinPacket));
            }
        } else if (msg.type == PacketType::HousePlace) {
            if (msg.data.size() >= sizeof(HousePlaceHeader) && msg.client) {
                HousePlaceHeader hd;
                memcpy(&hd, msg.data.data(), sizeof(hd));
                size_t need = sizeof(HousePlaceHeader)
                            + (size_t)hd.dimX * hd.dimY * hd.dimZ;
                bool dimsOk = hd.dimX > 0 && hd.dimY > 0 && hd.dimZ > 0 &&
                              hd.dimX <= 64 && hd.dimY <= 128 && hd.dimZ <= 64 &&
                              hd.baseY >= 1 && hd.baseY + hd.dimY <= CHUNK_HEIGHT;
                // Anti-grief: the house must be placed near the requesting player.
                glm::vec3 pp = getPlayerPosition(msg.client->id);
                float hcx = hd.worldX + hd.dimX * 0.5f, hcz = hd.worldZ + hd.dimZ * 0.5f;
                float ddx = hcx - pp.x, ddz = hcz - pp.z;
                bool nearPlayer = (ddx * ddx + ddz * ddz) <= 96.0f * 96.0f;

                if (dimsOk && nearPlayer && msg.data.size() == need) {
                    stampHouse(world, hd, msg.data.data() + sizeof(HousePlaceHeader));

                    // Relight and re-broadcast every chunk the house touched so
                    // all clients (including the placer) see the new blocks.
                    int cx0 = chunkOf(hd.worldX - 1), cx1 = chunkOf(hd.worldX + hd.dimX);
                    int cz0 = chunkOf(hd.worldZ - 1), cz1 = chunkOf(hd.worldZ + hd.dimZ);
                    std::vector<std::vector<uint8_t>> chunkPackets;
                    {
                        std::lock_guard<std::mutex> wlock(world.chunksMutex);
                        for (int cx = cx0; cx <= cx1; cx++)
                            for (int cz = cz0; cz <= cz1; cz++) {
                                auto it = world.chunks.find({cx, cz});
                                if (it == world.chunks.end()) continue;
                                Chunk* c = it->second.get();
                                c->computeLight();
                                std::vector<uint8_t> data(
                                    sizeof(ChunkPos) + c->blocks.size() + c->lightMap.size());
                                memcpy(data.data(), &c->pos, sizeof(ChunkPos));
                                memcpy(data.data() + sizeof(ChunkPos),
                                       c->blocks.data(), c->blocks.size());
                                memcpy(data.data() + sizeof(ChunkPos) + c->blocks.size(),
                                       c->lightMap.data(), c->lightMap.size());
                                chunkPackets.push_back(std::move(data));
                            }
                    }
                    {
                        std::lock_guard<std::mutex> clock(clientsMutex);
                        for (auto& cl : clients)
                            for (auto& pkt : chunkPackets)
                                cl->send(PacketType::ChunkData, pkt.data(), pkt.size());
                    }
                }
            }
        }
    }

    auto playerStates = getPlayerStates();
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto& client : clients) {
        glm::vec3 pos(0);
        bool found = false;
        for (auto& ps : playerStates) {
            if (ps.id == client->id) { pos = ps.pos; found = true; break; }
        }
        if (!found) continue;

        int pcx = (int)floorf(pos.x / (float)CHUNK_SIZE);
        int pcz = (int)floorf(pos.z / (float)CHUNK_SIZE);

        const int streamRadius = world.renderDistance;
        for (int dx = -streamRadius; dx <= streamRadius; dx++) {
            for (int dz = -streamRadius; dz <= streamRadius; dz++) {
                ChunkPos cp { pcx + dx, pcz + dz };
                if (client->sentChunks.find(cp) != client->sentChunks.end()) continue;

                std::lock_guard<std::mutex> wlock(world.chunksMutex);
                auto it = world.chunks.find(cp);
                if (it != world.chunks.end()) {
                    Chunk* chunk = it->second.get();
                    if (chunk->state != ChunkState::Empty && chunk->state != ChunkState::Generating) {
                        sendChunk(client, *chunk);
                        client->sentChunks.insert(cp);
                    }
                }
            }
        }

        for (auto it = client->sentChunks.begin(); it != client->sentChunks.end(); ) {
            if (abs(it->x - pcx) > streamRadius + 2 || abs(it->z - pcz) > streamRadius + 2) {
                it = client->sentChunks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void NetworkServer::broadcastUDP(const void* data, size_t size) {
    PacketHeader header { PacketType::PlayerPos, (uint32_t)size };
    std::vector<uint8_t> packet(sizeof(PacketHeader) + size);
    memcpy(packet.data(), &header, sizeof(PacketHeader));
    memcpy(packet.data() + sizeof(PacketHeader), data, packet.size() - sizeof(PacketHeader));

    std::lock_guard<std::mutex> lock(udpClientsMutex);
    for (auto& uc : udp_clients) {
        udp_socket.send_to(boost::asio::buffer(packet), uc.endpoint);
    }
}

void NetworkServer::broadcast(PacketType type, const void* data, size_t size, std::shared_ptr<Connection> skip) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto& c : clients) {
        if (c != skip) c->send(type, data, size);
    }
}

void NetworkServer::sendChunk(std::shared_ptr<Connection> client, const Chunk& chunk) {
    size_t payload = chunk.blocks.size() + chunk.lightMap.size();
    size_t dataSize = sizeof(ChunkPos) + payload;
    std::vector<uint8_t> data(dataSize);
    memcpy(data.data(), &chunk.pos, sizeof(ChunkPos));
    memcpy(data.data() + sizeof(ChunkPos), chunk.blocks.data(), chunk.blocks.size());
    memcpy(data.data() + sizeof(ChunkPos) + chunk.blocks.size(), chunk.lightMap.data(), chunk.lightMap.size());
    
    client->send(PacketType::ChunkData, data.data(), data.size());
}

std::vector<NetworkServer::PlayerState> NetworkServer::getPlayerStates() {
    std::lock_guard<std::mutex> lock(udpClientsMutex);
    std::vector<PlayerState> states;
    for (auto& uc : udp_clients) {
        states.push_back({uc.id, uc.lastPos});
    }
    return states;
}

// --- NetworkClient ---

void NetworkClient::copyStringField(char* dest, size_t destSize, const std::string& src) {
    if (destSize == 0) return;
    strncpy(dest, src.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
}

NetworkClient::NetworkClient()
    : udp_socket(io_context, udp::endpoint(udp::v4(), 0)),
      udp_buffer(1024) {}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& host, unsigned short port) {
    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(host, std::to_string(port));
    
    auto socket = tcp::socket(io_context);
    boost::system::error_code ec;
    boost::asio::connect(socket, endpoints, ec);
    if (ec) return false;

    server_udp_endpoint = udp::endpoint(boost::asio::ip::make_address(host), port);
    
    connection = std::make_shared<Connection>(std::move(socket));
    connection->start(
        [this](std::shared_ptr<Connection>, PacketType type, std::vector<uint8_t> data) {
            std::lock_guard<std::mutex> lock(queueMutex);
            messageQueue.push({type, std::move(data)});
        },
        [this](std::shared_ptr<Connection>) {
            connected = false;
        }
    );
    
    doReceiveUDP();
    connected = true;
    joinSent = false;
    if (io_thread.joinable()) {
        io_context.stop();
        io_thread.join();
        io_context.restart();
    }
    io_thread = std::thread([this]() { io_context.run(); });
    return true;
}

void NetworkClient::disconnect() {
    connected = false;
    joinSent = false;
    if (connection) {
        boost::system::error_code ec;
        connection->socket.close(ec);
        connection.reset();
    }
    if (io_thread.joinable()) {
        io_context.stop();
        io_thread.join();
        io_context.restart();
    }
}

void NetworkClient::doReceiveUDP() {
    udp_socket.async_receive_from(boost::asio::buffer(udp_buffer), server_udp_endpoint,
        [this](boost::system::error_code ec, std::size_t bytes_recvd) {
            if (!ec && bytes_recvd >= sizeof(PacketHeader)) {
                PacketHeader* h = (PacketHeader*)udp_buffer.data();
                if (h->type == PacketType::PlayerPos && h->size == sizeof(PlayerPosPacket)) {
                    std::vector<uint8_t> data(h->size);
                    memcpy(data.data(), udp_buffer.data() + sizeof(PacketHeader), h->size);
                    std::lock_guard<std::mutex> lock(queueMutex);
                    messageQueue.push({h->type, std::move(data)});
                }
            }
            if (connected) doReceiveUDP();
        });
}

void NetworkClient::pushChat(uint32_t senderId, const std::string& senderName, const std::string& text) {
    ChatMessage msg;
    msg.senderID = senderId;
    msg.senderName = senderName;
    msg.text = text;
    msg.timestamp = glfwGetTime();
    chatLog.push_back(std::move(msg));
    if (chatLog.size() > MAX_CHAT_LOG)
        chatLog.erase(chatLog.begin(), chatLog.begin() + (chatLog.size() - MAX_CHAT_LOG));
}

void NetworkClient::sendChat(const std::string& text) {
    if (!connected || text.empty()) return;
    ChatPacket cp {};
    cp.senderID = clientID;
    copyStringField(cp.text, sizeof(cp.text), text);
    send(PacketType::Chat, &cp, sizeof(cp));
}

void NetworkClient::sendPlayerJoin(const std::string& name) {
    if (!connected || clientID == 0) return;
    PlayerJoinPacket pj {};
    pj.clientID = clientID;
    copyStringField(pj.name, sizeof(pj.name), name);
    send(PacketType::PlayerJoin, &pj, sizeof(pj));
    joinSent = true;
}

void NetworkClient::update(World& world, std::unordered_map<uint32_t, RemotePlayer>& players) {
    if (connected && clientID != 0 && !joinSent) {
        // joinSent set by sendPlayerJoin from main after connect
    }

    std::queue<QueuedMessage> localQueue;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        std::swap(localQueue, messageQueue);
    }
    
    while (!localQueue.empty()) {
        auto msg = localQueue.front();
        localQueue.pop();
        
        if (msg.type == PacketType::Handshake) {
            if (msg.data.size() == sizeof(HandshakePacket)) {
                HandshakePacket* p = (HandshakePacket*)msg.data.data();
                clientID = p->clientID;
            }
        } else if (msg.type == PacketType::ChunkData) {
            if (msg.data.size() >= sizeof(ChunkPos)) {
                ChunkPos pos;
                memcpy(&pos, msg.data.data(), sizeof(ChunkPos));
                
                std::lock_guard<std::mutex> lock(world.chunksMutex);
                auto it = world.chunks.find(pos);
                Chunk* chunk;
                if (it == world.chunks.end()) {
                    auto c = std::make_unique<Chunk>(pos, false);
                    chunk = c.get();
                    world.chunks[pos] = std::move(c);
                } else {
                    chunk = it->second.get();
                }
                
                size_t blocksOnly = sizeof(ChunkPos) + chunk->blocks.size();
                size_t withLight  = blocksOnly + chunk->lightMap.size();
                if (msg.data.size() == withLight) {
                    memcpy(chunk->blocks.data(), msg.data.data() + sizeof(ChunkPos), chunk->blocks.size());
                    memcpy(chunk->lightMap.data(), msg.data.data() + blocksOnly, chunk->lightMap.size());
                    chunk->state = ChunkState::Generated;
                } else if (msg.data.size() == blocksOnly) {
                    memcpy(chunk->blocks.data(), msg.data.data() + sizeof(ChunkPos), chunk->blocks.size());
                    chunk->computeLight();
                    chunk->state = ChunkState::Generated;
                } else {
                    std::cerr << "[Client] Malformed chunk (" << pos.x << "," << pos.z << ") size "
                              << msg.data.size() << " expected " << blocksOnly << " or " << withLight << std::endl;
                }
            }
        } else if (msg.type == PacketType::BlockUpdate) {
            if (msg.data.size() == sizeof(BlockUpdatePacket)) {
                BlockUpdatePacket* p = (BlockUpdatePacket*)msg.data.data();
                world.setBlock(p->x, p->y, p->z, (BlockType)p->type);
                world.relightAt(p->x, p->y, p->z);
            }
        } else if (msg.type == PacketType::PlayerPos) {
            if (msg.data.size() == sizeof(PlayerPosPacket)) {
                PlayerPosPacket* p = (PlayerPosPacket*)msg.data.data();
                if (p->id == clientID) continue;

                auto& rp = players[p->id];
                rp.id = p->id;
                if (rp.name.empty()) rp.name = "Player" + std::to_string(p->id);
                rp.targetPosition = glm::vec3(p->x, p->y, p->z);
                rp.targetPitch    = p->pitch;
                rp.targetYaw      = p->yaw;
                rp.lanternHeld    = p->lanternHeld != 0;
                
                if (rp.lastUpdate == 0) {
                    rp.position = rp.targetPosition;
                    rp.pitch = rp.targetPitch;
                    rp.yaw = rp.targetYaw;
                }
                rp.lastUpdate = glfwGetTime();
            }
        } else if (msg.type == PacketType::PlayerModel) {
            if (msg.data.size() >= sizeof(PlayerModelHeader)) {
                PlayerModelHeader* h = (PlayerModelHeader*)msg.data.data();
                auto& rp = players[h->clientID];
                rp.id = h->clientID;
                ensureRemoteRig(rp.rig);
                rp.rig->hairStyle = h->hairStyle;
                rp.rig->hairColor = h->hairColor;
                rp.rig->eyeColor = h->eyeColor;
                rp.rig->eyeType = h->eyeType;
                rp.rig->noseStyle = h->noseStyle;
                rp.rig->eyebrowStyle = h->eyebrowStyle;
                rp.rig->earType = h->earType;
                rp.rig->armorType = h->armorType;
                rp.rig->applyCustomization();
            }
        } else if (msg.type == PacketType::PlayerAttack) {
            if (msg.data.size() == sizeof(PlayerAttackPacket)) {
                PlayerAttackPacket* p = (PlayerAttackPacket*)msg.data.data();
                auto& rp = players[p->clientID];
                rp.isAttacking = true;
                rp.attackAnim = 0.0f;
            }
        } else if (msg.type == PacketType::PlayerDisconnect) {
            if (msg.data.size() == sizeof(PlayerDisconnectPacket)) {
                PlayerDisconnectPacket* p = (PlayerDisconnectPacket*)msg.data.data();
                auto it = players.find(p->clientID);
                if (it != players.end()) {
                    if (it->second.rig) delete it->second.rig;
                    players.erase(it);
                }
            }
        } else if (msg.type == PacketType::PlayerJoin) {
            if (msg.data.size() == sizeof(PlayerJoinPacket)) {
                PlayerJoinPacket* pj = (PlayerJoinPacket*)msg.data.data();
                auto& rp = players[pj->clientID];
                rp.id = pj->clientID;
                rp.name = pj->name;
                if (rp.name.empty()) rp.name = "Player" + std::to_string(pj->clientID);
            }
        } else if (msg.type == PacketType::Chat) {
            if (msg.data.size() == sizeof(ChatPacket)) {
                ChatPacket* cp = (ChatPacket*)msg.data.data();
                std::string senderName = "Player" + std::to_string(cp->senderID);
                auto pit = players.find(cp->senderID);
                if (pit != players.end() && !pit->second.name.empty())
                    senderName = pit->second.name;
                if (cp->senderID == clientID) senderName = "You";
                pushChat(cp->senderID, senderName, cp->text);
            }
        } else if (msg.type == PacketType::DayTime) {
            if (msg.data.size() == sizeof(DayTimePacket)) {
                DayTimePacket* p = (DayTimePacket*)msg.data.data();
                serverGameTime = p->gameTime;
                hasServerGameTime = true;
                dayTimeUpdated = true;
            }
        } else if (msg.type == PacketType::EntityState) {
            if (msg.data.size() == sizeof(EntityStatePacket)) {
                EntityStatePacket* p = (EntityStatePacket*)msg.data.data();
                entityUpdates.push_back(*p);
            }
        } else if (msg.type == PacketType::NPCState) {
            if (msg.data.size() == sizeof(NPCStatePacket)) {
                NPCStatePacket* p = (NPCStatePacket*)msg.data.data();
                npcUpdates.push_back(*p);
            }
        } else if (msg.type == PacketType::PlayerHealth) {
            if (msg.data.size() == sizeof(PlayerHealthPacket)) {
                PlayerHealthPacket* p = (PlayerHealthPacket*)msg.data.data();
                if (p->clientID == clientID) pendingSelfDamage += p->damage;
            }
        }
    }
}

void NetworkClient::send(PacketType type, const void* data, size_t size) {
    if (connection) connection->send(type, data, size);
}

void NetworkClient::sendUDP(const void* data, size_t size) {
    if (!connected || size == 0 || !data) return;
    std::vector<uint8_t> packet(sizeof(PacketHeader) + size);
    PacketHeader header { PacketType::PlayerPos, (uint32_t)size };
    memcpy(packet.data(), &header, sizeof(PacketHeader));
    memcpy(packet.data() + sizeof(PacketHeader), data, packet.size() - sizeof(PacketHeader));
    udp_socket.send_to(boost::asio::buffer(packet), server_udp_endpoint);
}
