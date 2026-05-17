#include "network.h"
#include <iostream>

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
    // Note: Caller must hold writeMutex or be the completion handler
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

// --- NetworkServer ---

NetworkServer::NetworkServer(unsigned short port)
    : acceptor(io_context, tcp::endpoint(tcp::v4(), port)),
      udp_socket(io_context, udp::endpoint(udp::v4(), port)),
      udp_buffer(1024) {
    doAccept();
    doReceiveUDP();
    // Run io_context in a background thread
    std::thread([this]() { io_context.run(); }).detach();
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
            
            // Send existing player models
            {
                std::lock_guard<std::mutex> lock(modelsMutex);
                for (auto& [id, model] : playerModels) {
                    conn->send(PacketType::PlayerModel, &model, sizeof(model));
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
                    std::cout << "Client disconnected: ID " << c->id << std::endl;
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
                    
                    // Register/Update client UDP endpoint
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
                    
                    // Queue for broadcast in update()
                    std::vector<uint8_t> data(h->size);
                    memcpy(data.data(), p, h->size);
                    
                    std::lock_guard<std::mutex> lock(queueMutex);
                    messageQueue.push({nullptr, h->type, std::move(data), remote_endpoint});
                }
            }
            doReceiveUDP();
        });
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
            if (msg.data.size() == sizeof(BlockUpdatePacket)) {
                BlockUpdatePacket* p = (BlockUpdatePacket*)msg.data.data();
                world.setBlock(p->x, p->y, p->z, (BlockType)p->type);
                broadcast(PacketType::BlockUpdate, p, sizeof(BlockUpdatePacket), msg.client);
            }
        } else if (msg.type == PacketType::ChunkRequest) {
            // Deprecated by proactive streaming, but kept for compatibility
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
        }
    }

    // Proactive chunk streaming
    auto playerStates = getPlayerStates();
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto& client : clients) {
        // Find this client's position
        glm::vec3 pos(0);
        bool found = false;
        for (auto& ps : playerStates) {
            if (ps.id == client->id) { pos = ps.pos; found = true; break; }
        }
        if (!found) continue;

        int pcx = (int)floorf(pos.x / (float)CHUNK_SIZE);
        int pcz = (int)floorf(pos.z / (float)CHUNK_SIZE);

        // Stream chunks in a radius
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

        // Clean up sentChunks for distant chunks to save memory and allow re-streaming
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
    memcpy(packet.data() + sizeof(PacketHeader), data, size);

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
    size_t dataSize = sizeof(ChunkPos) + chunk.blocks.size();
    std::vector<uint8_t> data(dataSize);
    memcpy(data.data(), &chunk.pos, sizeof(ChunkPos));
    memcpy(data.data() + sizeof(ChunkPos), chunk.blocks.data(), chunk.blocks.size());
    
    std::cout << "[Server] Sending chunk (" << chunk.pos.x << "," << chunk.pos.z << ") to client " << client->id << std::endl;
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

NetworkClient::NetworkClient()
    : udp_socket(io_context, udp::endpoint(udp::v4(), 0)),
      udp_buffer(1024) {}

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
    io_thread = std::thread([this]() { io_context.run(); });
    return true;
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
            doReceiveUDP();
        });
}

void NetworkClient::update(World& world, std::unordered_map<uint32_t, RemotePlayer>& players) {
    std::queue<QueuedMessage> localQueue;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        std::swap(localQueue, messageQueue);
    }
    
    while (!localQueue.empty()) {
        auto msg = localQueue.front();
        localQueue.pop();
        
        if (msg.type == PacketType::Handshake) {
            HandshakePacket* p = (HandshakePacket*)msg.data.data();
            clientID = p->clientID;
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
                
                size_t expectedSize = sizeof(ChunkPos) + chunk->blocks.size();
                if (msg.data.size() == expectedSize) {
                    memcpy(chunk->blocks.data(), msg.data.data() + sizeof(ChunkPos), chunk->blocks.size());
                    chunk->computeLight();
                    chunk->state = ChunkState::Generated;
                    std::cout << "[Client] Received chunk (" << pos.x << "," << pos.z << ")" << std::endl;
                } else {
                    std::cerr << "[Client] Received malformed chunk data for (" << pos.x << "," << pos.z << ") - Expected " << expectedSize << " bytes, got " << msg.data.size() << std::endl;
                }
            }
        } else if (msg.type == PacketType::BlockUpdate) {
            if (msg.data.size() == sizeof(BlockUpdatePacket)) {
                BlockUpdatePacket* p = (BlockUpdatePacket*)msg.data.data();
                world.setBlock(p->x, p->y, p->z, (BlockType)p->type);
            }
        } else if (msg.type == PacketType::PlayerPos) {
            if (msg.data.size() == sizeof(PlayerPosPacket)) {
                PlayerPosPacket* p = (PlayerPosPacket*)msg.data.data();
                if (p->id == clientID) continue; // Ignore self

                auto& rp = players[p->id];
                rp.id = p->id;
                rp.targetPosition = glm::vec3(p->x, p->y, p->z);
                rp.targetPitch = p->pitch;
                rp.targetYaw = p->yaw;
                
                // If first time seen, snap to position
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
                if (!rp.rig) rp.rig = new BipedalRig();
                rp.rig->hairStyle = h->hairStyle;
                rp.rig->hairColor = h->hairColor;
                rp.rig->eyeColor = h->eyeColor;
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
    memcpy(&packet[sizeof(PacketHeader)], data, size);
    udp_socket.send_to(boost::asio::buffer(packet), server_udp_endpoint);
}
