#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gl_loader.h"
#include "atlas.h"
#include "world.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>
#include <cmath>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>

#include "shader.h"
#include "camera.h"
#include "network.h"

// --- Config ---
static constexpr int   WIDTH  = 1280;
static constexpr int   HEIGHT = 720;
static constexpr float DAY_CYCLE_SECONDS = 120.0f; // full day in seconds
static constexpr float REACH  = 5.0f;
static constexpr float PLAYER_HEIGHT = 1.8f;
static constexpr float PLAYER_WIDTH  = 0.4f;

// --- Globals ---
NetworkServer* g_server = nullptr;
NetworkClient* g_client = nullptr;
std::unordered_map<uint32_t, RemotePlayer> g_remotePlayers;
Camera         camera(glm::vec3(8.0f, 50.0f, 8.0f));
World          clientWorld(false); // The client's world
double         lastX = WIDTH/2.0, lastY = HEIGHT/2.0;
bool           firstMouse = true;
float          deltaTime = 0.0f, lastFrame = 0.0f;
float          gameTime = 0.3f; // start at morning
bool           noclip = false;

int keyFwd=0, keyBack=0, keyLeft=0, keyRight=0, keyJump=0;

// --- Skybox ---
GLuint skyVAO=0, skyVBO=0;
static const float skyVerts[] = {
    -1,-1,-1,  1,-1,-1,  1, 1,-1,  1, 1,-1, -1, 1,-1, -1,-1,-1,
    -1,-1, 1, -1, 1, 1,  1, 1, 1,  1, 1, 1,  1,-1, 1, -1,-1, 1,
    -1, 1, 1, -1, 1,-1,  1, 1,-1,  1, 1,-1,  1, 1, 1, -1, 1, 1,
    -1,-1,-1, -1,-1, 1,  1,-1,-1,  1,-1,-1, -1,-1, 1,  1,-1, 1,
     1,-1,-1,  1,-1, 1,  1, 1, 1,  1, 1, 1,  1, 1,-1,  1,-1,-1,
    -1,-1,-1, -1, 1,-1, -1, 1, 1, -1, 1, 1, -1,-1, 1, -1,-1,-1,
};

void setupSkybox() {
    glGenVertexArrays(1, &skyVAO);
    glGenBuffers(1, &skyVBO);
    glBindVertexArray(skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyVerts), skyVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// --- Crosshair (simple lines) ---
GLuint crossVAO=0, crossVBO=0;
Shader* crossShader = nullptr;

void setupCrosshair() {}

// --- Collision ---
static constexpr float SKIN        = 0.001f;
static constexpr float STEP_HEIGHT = 1.0f;

static bool hitFaceX(float px, float py, float pz, float hw, float ph, const World& w, bool posDir) {
    int bx = posDir ? (int)floorf(px + hw) : (int)floorf(px - hw - SKIN);
    for (int by = (int)floorf(py);       by <= (int)floorf(py + ph - SKIN); by++)
    for (int bz = (int)floorf(pz - hw); bz <= (int)floorf(pz + hw - SKIN); bz++)
        if (w.getBlock(bx, by, bz) != BlockType::Air) return true;
    return false;
}

static bool hitFaceZ(float px, float py, float pz, float hw, float ph, const World& w, bool posDir) {
    int bz = posDir ? (int)floorf(pz + hw) : (int)floorf(pz - hw - SKIN);
    for (int by = (int)floorf(py);       by <= (int)floorf(py + ph - SKIN); by++)
    for (int bx = (int)floorf(px - hw); bx <= (int)floorf(px + hw - SKIN); bx++)
        if (w.getBlock(bx, by, bz) != BlockType::Air) return true;
    return false;
}

static bool aabbClear(float px, float py, float pz, float hw, float ph, const World& w) {
    for (int bx = (int)floorf(px - hw); bx <= (int)floorf(px + hw - SKIN); bx++)
    for (int by = (int)floorf(py);      by <= (int)floorf(py + ph - SKIN); by++)
    for (int bz = (int)floorf(pz - hw); bz <= (int)floorf(pz + hw - SKIN); bz++)
        if (w.getBlock(bx, by, bz) != BlockType::Air) return false;
    return true;
}

glm::vec3 resolveCollision(const glm::vec3& pos, const World& w) {
    const float hw = PLAYER_WIDTH  / 2.0f;
    const float ph = PLAYER_HEIGHT;
    glm::vec3   p  = pos;

    camera.onGround = false;

    if (camera.velocity.y <= 0.0f) {
        int yFeet = (int)floorf(p.y);
        for (int by = yFeet + 3; by >= yFeet; by--) {
            if (by < 0) {
                p.y = SKIN; camera.velocity.y = 0.0f; camera.onGround = true; break;
            }
            bool hit = false;
            for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !hit; bx++)
            for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !hit; bz++)
                if (w.getBlock(bx, by, bz) != BlockType::Air) hit = true;
            if (hit) {
                float top = (float)(by + 1);
                if (p.y < top) {
                    p.y = top + SKIN;
                    camera.velocity.y = 0.0f;
                    camera.onGround = true;
                }
                break;
            }
        }
    } else {
        int byHead = (int)floorf(p.y + ph);
        bool hit = false;
        for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !hit; bx++)
        for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !hit; bz++)
            if (w.getBlock(bx, byHead, bz) != BlockType::Air) hit = true;
        if (hit) {
            p.y = (float)byHead - ph - SKIN;
            camera.velocity.y = 0.0f;
        }
    }

    if (!camera.onGround) {
        int byBelow = (int)floorf(p.y - SKIN);
        if (byBelow >= 0) {
            bool found = false;
            for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !found; bx++)
            for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !found; bz++)
                if (w.getBlock(bx, byBelow, bz) != BlockType::Air) found = true;
            camera.onGround = found;
        }
    }

    float savedVx = camera.velocity.x;
    float savedVz = camera.velocity.z;

    bool blockedX = false;
    if (camera.velocity.x != 0.0f) {
        bool posX = camera.velocity.x > 0.0f;
        if (hitFaceX(p.x, p.y, p.z, hw, ph, w, posX)) {
            blockedX = true;
            p.x = posX ? floorf(p.x + hw) - hw - SKIN : floorf(p.x - hw - SKIN) + 1.0f + hw + SKIN;
            camera.velocity.x = 0.0f;
        }
    }

    bool blockedZ = false;
    if (camera.velocity.z != 0.0f) {
        bool posZ = camera.velocity.z > 0.0f;
        if (hitFaceZ(p.x, p.y, p.z, hw, ph, w, posZ)) {
            blockedZ = true;
            p.z = posZ ? floorf(p.z + hw) - hw - SKIN : floorf(p.z - hw - SKIN) + 1.0f + hw + SKIN;
            camera.velocity.z = 0.0f;
        }
    }

    if ((blockedX || blockedZ) && camera.onGround) {
        float tryX = blockedX ? pos.x : p.x;
        float tryZ = blockedZ ? pos.z : p.z;
        float tryY = p.y + STEP_HEIGHT;
        if (aabbClear(tryX, tryY, tryZ, hw, ph, w)) {
            p = { tryX, tryY, tryZ };
            camera.velocity.x = savedVx;
            camera.velocity.z = savedVz;
        }
    }

    return p;
}

// --- Remote Players Rendering ---
void renderRemotePlayers(Shader& shader, float sunFactor, const glm::vec3& skyAmbient, const glm::vec3& eyePos, const glm::mat4& view, const glm::mat4& proj) {
    static GLuint playerVAO = 0, playerVBO = 0;
    if (playerVAO == 0) {
        float hw = PLAYER_WIDTH / 2.0f;
        float h  = PLAYER_HEIGHT;
        float v[] = {
            -hw, 0, -hw,  0,0,-1, 0,0, 3, 1, 1,   hw, 0, -hw,  0,0,-1, 1,0, 3, 1, 1,   hw, h, -hw,  0,0,-1, 1,1, 3, 1, 1,
             hw, h, -hw,  0,0,-1, 1,1, 3, 1, 1,  -hw, h, -hw,  0,0,-1, 0,1, 3, 1, 1,  -hw, 0, -hw,  0,0,-1, 0,0, 3, 1, 1,
            -hw, 0,  hw,  0,0, 1, 0,0, 3, 1, 1,   hw, 0,  hw,  0,0, 1, 1,0, 3, 1, 1,   hw, h,  hw,  0,0, 1, 1,1, 3, 1, 1,
             hw, h,  hw,  0,0, 1, 1,1, 3, 1, 1,  -hw, h,  hw,  0,0, 1, 0,1, 3, 1, 1,  -hw, 0,  hw,  0,0, 1, 0,0, 3, 1, 1,
            -hw, h,  hw, -1,0, 0, 1,0, 3, 1, 1,  -hw, h, -hw, -1,0, 0, 1,1, 3, 1, 1,  -hw, 0, -hw, -1,0, 0, 0,1, 3, 1, 1,
            -hw, 0, -hw, -1,0, 0, 0,1, 3, 1, 1,  -hw, 0,  hw, -1,0, 0, 0,0, 3, 1, 1,  -hw, h,  hw, -1,0, 0, 1,0, 3, 1, 1,
             hw, h,  hw,  1,0, 0, 1,0, 3, 1, 1,   hw, h, -hw,  1,0, 0, 1,1, 3, 1, 1,   hw, 0, -hw,  1,0, 0, 0,1, 3, 1, 1,
             hw, 0, -hw,  1,0, 0, 0,1, 3, 1, 1,   hw, 0,  hw,  1,0, 0, 0,0, 3, 1, 1,   hw, h,  hw,  1,0, 0, 1,0, 3, 1, 1,
            -hw, 0, -hw,  0,-1,0, 0,1, 3, 1, 1,   hw, 0, -hw,  0,-1,0, 1,1, 3, 1, 1,   hw, 0,  hw,  0,-1,0, 1,0, 3, 1, 1,
             hw, 0,  hw,  0,-1,0, 1,0, 3, 1, 1,  -hw, 0,  hw,  0,-1,0, 0,0, 3, 1, 1,  -hw, 0, -hw,  0,-1,0, 0,1, 3, 1, 1,
            -hw, h, -hw,  0, 1,0, 0,1, 3, 1, 1,   hw, h, -hw,  0, 1,0, 1,1, 3, 1, 1,   hw, h,  hw,  0, 1,0, 1,0, 3, 1, 1,
             hw, h,  hw,  0, 1,0, 1,0, 3, 1, 1,  -hw, h,  hw,  0, 1,0, 0,0, 3, 1, 1,  -hw, h, -hw,  0, 1,0, 0,1, 3, 1, 1,
        };
        glGenVertexArrays(1, &playerVAO);
        glGenBuffers(1, &playerVBO);
        glBindVertexArray(playerVAO);
        glBindBuffer(GL_ARRAY_BUFFER, playerVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
        for (int i=0; i<6; i++) {
            glVertexAttribPointer(i, (i==0||i==1?3:i==2?2:1), GL_FLOAT, GL_FALSE, 11*sizeof(float), (void*)( (i==0?0:i==1?3:i==2?6:i==3?8:i==4?9:10) * sizeof(float)));
            glEnableVertexAttribArray(i);
        }
    }

    shader.use();
    shader.setMat4("view", view);
    shader.setMat4("projection", proj);
    shader.setFloat("sunFactor", sunFactor);
    shader.setVec3("skyAmbient", skyAmbient);
    shader.setVec3("camPos", eyePos);

    for (auto& [id, p] : g_remotePlayers) {
        float lerpFactor = 10.0f * deltaTime;
        p.position = glm::mix(p.position, p.targetPosition, std::min(1.0f, lerpFactor));
        p.pitch = glm::mix(p.pitch, p.targetPitch, std::min(1.0f, lerpFactor));
        p.yaw = glm::mix(p.yaw, p.targetYaw, std::min(1.0f, lerpFactor));

        glm::mat4 model = glm::translate(glm::mat4(1.0f), p.position);
        shader.setMat4("model", model);
        glBindVertexArray(playerVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
}

// --- GLFW callbacks ---
void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoff = (float)(xpos - lastX);
    float yoff = (float)(lastY - ypos);
    lastX = xpos; lastY = ypos;
    camera.processMouseMovement(xoff, yoff);
}

void mouse_button_callback(GLFWwindow*, int button, int action, int) {
    if (action != GLFW_PRESS || !g_client) return;
    glm::ivec3 hitBlock, hitNormal;
    if (clientWorld.raycast(camera.position + glm::vec3(0.0f, 1.6f, 0.0f),
                      camera.front, REACH, hitBlock, hitNormal)) {
        BlockUpdatePacket p;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            p = { hitBlock.x, hitBlock.y, hitBlock.z, (uint8_t)BlockType::Air };
            g_client->send(PacketType::BlockUpdate, &p, sizeof(p));
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            glm::ivec3 place = hitBlock + hitNormal;
            if (clientWorld.getBlock(place.x, place.y, place.z) == BlockType::Air) {
                p = { place.x, place.y, place.z, (uint8_t)BlockType::Stone };
                g_client->send(PacketType::BlockUpdate, &p, sizeof(p));
            }
        }
    }
}

void key_callback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_N && action == GLFW_PRESS) noclip = !noclip;
    if (key == GLFW_KEY_W) { if(action==GLFW_PRESS) keyFwd=1; else if(action==GLFW_RELEASE) keyFwd=0; }
    if (key == GLFW_KEY_S) { if(action==GLFW_PRESS) keyBack=1; else if(action==GLFW_RELEASE) keyBack=0; }
    if (key == GLFW_KEY_A) { if(action==GLFW_PRESS) keyLeft=1; else if(action==GLFW_RELEASE) keyLeft=0; }
    if (key == GLFW_KEY_D) { if(action==GLFW_PRESS) keyRight=1; else if(action==GLFW_RELEASE) keyRight=0; }
    if (key == GLFW_KEY_SPACE) { if(action==GLFW_PRESS) keyJump=1; else if(action==GLFW_RELEASE) keyJump=0; }
}

static float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float sunElevation(float t) {
    return sinf((t - 0.25f) * 2.0f * 3.14159f) / sqrtf(1.0625f);
}

void runServer() {
    std::cout << "Starting Terrax Server..." << std::endl;
    g_server = new NetworkServer(12345);
    World serverWorld(true);
    serverWorld.renderDistance = 10; // 10x10 radius as requested
    serverWorld.generate(0, 0);
    auto lastTick = std::chrono::high_resolution_clock::now();
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;

        auto players = g_server->getPlayerStates();
        for (auto& p : players) {
            int pcx = (int)floorf(p.pos.x / (float)CHUNK_SIZE);
            int pcz = (int)floorf(p.pos.z / (float)CHUNK_SIZE);
            serverWorld.update(pcx, pcz);
        }

        g_server->update(serverWorld);
        gameTime = fmodf(gameTime + dt / DAY_CYCLE_SECONDS, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main(int argc, char** argv) {
    bool isServerOnly = false, isHost = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--server") isServerOnly = true;
        if (std::string(argv[i]) == "--host") isHost = true;
    }
    if (isServerOnly) { runServer(); return 0; }
    if (isHost) {
        std::cout << "Starting integrated server..." << std::endl;
        std::thread([]() { runServer(); }).detach();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Terrax", nullptr, nullptr);
    if (!window) { std::cerr << "Window creation failed\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSwapInterval(0);
    if (!gl_load()) { std::cerr << "Failed to load OpenGL functions\n"; return 1; }
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glEnable(GL_MULTISAMPLE);

    Shader chunkShader("shaders/chunk.vert", "shaders/chunk.frag");
    Shader waterShader("shaders/water.vert", "shaders/water.frag");
    Shader skyShader("shaders/sky.vert", "shaders/sky.frag");
    setupSkybox();
    GLuint atlasTexture = generateAtlas();

    std::cout << "Connecting to server..." << std::endl;
    g_client = new NetworkClient();
    if (!g_client->connect("127.0.0.1", 12345)) { std::cerr << "Failed to connect to server\n"; return 1; }
    clientWorld.onRequestChunk = [](int x, int z) {
        ChunkRequestPacket p { x, z };
        g_client->send(PacketType::ChunkRequest, &p, sizeof(p));
    };

    noclip = true;
    camera.position = glm::vec3(8.5f, 42.0f, 8.5f);
    camera.pitch = -20.0f;
    camera.updateVectors();

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        deltaTime = std::min(deltaTime, 0.05f);
        gameTime = fmodf(gameTime + deltaTime / DAY_CYCLE_SECONDS, 1.0f);

        g_client->update(clientWorld, g_remotePlayers);
        if (g_client->clientID != 0) {
            static float posSendTimer = 0;
            posSendTimer += deltaTime;
            if (posSendTimer >= 0.05f) {
                PlayerPosPacket p { g_client->clientID, camera.position.x, camera.position.y, camera.position.z, camera.pitch, camera.yaw };
                g_client->sendUDP(&p, sizeof(p));
                posSendTimer = 0;
            }
        }

        camera.processKeyboard(keyFwd - keyBack, keyRight - keyLeft, keyJump, deltaTime);
        if (noclip) {
            glm::vec3 move(0);
            if (keyFwd)   move += camera.front;
            if (keyBack)  move -= camera.front;
            if (keyRight) move += camera.right;
            if (keyLeft)  move -= camera.right;
            if (keyJump)  move += camera.worldUp;
            if (glm::length(move) > 0.001f) move = glm::normalize(move);
            camera.position += move * 15.0f * deltaTime;
            camera.velocity = glm::vec3(0);
        } else {
            camera.applyGravity(deltaTime);
            camera.position = resolveCollision(camera.position, clientWorld);
        }

        // Update chunks around player
        int pcx = (int)floorf(camera.position.x / (float)CHUNK_SIZE);
        int pcz = (int)floorf(camera.position.z / (float)CHUNK_SIZE);
        clientWorld.update(pcx, pcz);

        int fbW, fbH; glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        float aspect = fbW / (float)fbH;
        glm::mat4 proj = glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 1000.0f);
        glm::mat4 view = camera.getViewMatrix();
        float sunY = sunElevation(gameTime);
        float dayness = smoothstep(-0.12f, 0.22f, sunY);
        float dawnDusk = smoothstep(-0.30f, 0.0f, sunY) * (1.0f - smoothstep(0.0f, 0.30f, sunY));
        float sunFactor = 0.04f + 0.96f * dayness;
        glm::vec3 dayAmb(0.70f, 0.84f, 1.00f), dawnAmb(1.00f, 0.58f, 0.24f), nightAmb(0.18f, 0.22f, 0.50f);
        glm::vec3 skyAmbient = glm::mix(nightAmb, glm::mix(dayAmb, dawnAmb, dawnDusk), dayness);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        {
            glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
            skyShader.use();
            skyShader.setMat4("view", glm::mat4(glm::mat3(view)));
            skyShader.setMat4("projection", proj);
            skyShader.setFloat("timeOfDay", gameTime);
            skyShader.setFloat("time", currentFrame);
            glBindVertexArray(skyVAO); glDrawArrays(GL_TRIANGLES, 0, 36);
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
        }

        glm::vec3 eyePos = camera.position + glm::vec3(0.0f, 1.6f, 0.0f);
        {
            chunkShader.use();
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlasTexture);
            chunkShader.setInt("atlas", 0);
            chunkShader.setMat4("model", glm::mat4(1.0f));
            chunkShader.setMat4("view", view);
            chunkShader.setMat4("projection", proj);
            chunkShader.setFloat("sunFactor", sunFactor);
            chunkShader.setVec3("skyAmbient", skyAmbient);
            chunkShader.setVec3("camPos", eyePos);
            clientWorld.drawAll();
            renderRemotePlayers(chunkShader, sunFactor, skyAmbient, eyePos, view, proj);
        }
        {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
            waterShader.use();
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlasTexture);
            waterShader.setInt("atlas", 0);
            waterShader.setMat4("model", glm::mat4(1.0f));
            waterShader.setMat4("view", view);
            waterShader.setMat4("projection", proj);
            waterShader.setFloat("sunFactor", sunFactor);
            waterShader.setVec3("skyAmbient", skyAmbient);
            waterShader.setVec3("camPos", eyePos);
            waterShader.setFloat("time", currentFrame);
            waterShader.setFloat("timeOfDay", gameTime);
            clientWorld.drawAllWater();
            glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
        }
        {
            static int fpsCount = 0; static float fpsTimer = 0;
            fpsCount++; fpsTimer += deltaTime;
            if (fpsTimer >= 1.0f) {
                std::string title = "Terrax | FPS: " + std::to_string(fpsCount) + " | Chunks: " + std::to_string(clientWorld.chunks.size());
                glfwSetWindowTitle(window, title.c_str());
                fpsCount = 0; fpsTimer = 0;
            }
        }
        glfwSwapBuffers(window); glfwPollEvents();
    }
    glfwTerminate(); return 0;
}
