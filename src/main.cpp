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
#include "voxel_model.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// --- Enums ---
enum class GameState {
    MainMenu,
    CharacterEditor,
    Playing
};

// --- Config ---
static constexpr int   WIDTH  = 1280;
static constexpr int   HEIGHT = 720;
static constexpr float DAY_CYCLE_SECONDS = 120.0f; // full day in seconds
static constexpr float REACH  = 5.0f;
static constexpr float PLAYER_HEIGHT = 1.8f;
static constexpr float PLAYER_WIDTH  = 0.4f;

// --- Globals ---
GameState      g_state = GameState::MainMenu;
BipedalRig*    g_localPlayerRig = nullptr;
float          g_editorRotX = 0.0f, g_editorRotY = 0.0f;
bool           g_wasEditorClick = false;
bool           g_isEditorRotating = false;
double         g_lastEditorX = 0, g_lastEditorY = 0;
glm::vec4      g_editorColor = glm::vec4(1.0f);
float          g_camDist = 10.0f; 
float          g_playerYaw = 0.0f; // Character facing direction
enum class EditorTool { Paint, Add, Erase };
EditorTool     g_editorTool = EditorTool::Paint;
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

// --- GLFW callbacks ---
void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (g_state != GameState::Playing && g_state != GameState::CharacterEditor) return;
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoff = (float)(xpos - lastX);
    float yoff = (float)(lastY - ypos);
    lastX = xpos; lastY = ypos;
    camera.processMouseMovement(xoff, yoff);
}

void mouse_button_callback(GLFWwindow*, int button, int action, int) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (g_state != GameState::Playing) return;
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

void scroll_callback(GLFWwindow*, double, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    g_camDist -= (float)yoffset;
    g_camDist = std::clamp(g_camDist, 2.0f, 20.0f);
}

void key_callback(GLFWwindow* window, int key, int, int action, int) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (g_state != GameState::Playing) return;
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

void setupFantasyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // --- Fantasy Palette ---
    // Backgrounds: Deep Mahogany / Old Wood
    ImVec4 bg_base        = ImVec4(0.15f, 0.08f, 0.05f, 1.00f);
    ImVec4 bg_mid         = ImVec4(0.22f, 0.12f, 0.08f, 1.00f);
    // Accents: Burnished Gold / Brass
    ImVec4 gold_bright    = ImVec4(0.85f, 0.65f, 0.25f, 1.00f);
    ImVec4 gold_dim       = ImVec4(0.60f, 0.45f, 0.15f, 1.00f);
    // Text: Parchment / Old Paper
    ImVec4 parchment      = ImVec4(0.92f, 0.85f, 0.75f, 1.00f);
    ImVec4 parchment_dim  = ImVec4(0.70f, 0.65f, 0.55f, 1.00f);

    colors[ImGuiCol_Text]                   = parchment;
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = bg_base;
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = bg_base;
    colors[ImGuiCol_Border]                 = gold_dim;
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = bg_mid;
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.30f, 0.18f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.35f, 0.22f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBg]                = bg_base;
    colors[ImGuiCol_TitleBgActive]          = bg_mid;
    colors[ImGuiCol_TitleBgCollapsed]       = bg_base;
    colors[ImGuiCol_MenuBarBg]              = bg_base;
    colors[ImGuiCol_ScrollbarBg]            = bg_base;
    colors[ImGuiCol_ScrollbarGrab]          = gold_dim;
    colors[ImGuiCol_ScrollbarGrabHovered]   = gold_bright;
    colors[ImGuiCol_ScrollbarGrabActive]    = gold_bright;
    colors[ImGuiCol_CheckMark]              = gold_bright;
    colors[ImGuiCol_SliderGrab]             = gold_dim;
    colors[ImGuiCol_SliderGrabActive]       = gold_bright;
    colors[ImGuiCol_Button]                 = bg_mid;
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.40f, 0.25f, 0.15f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = gold_dim;
    colors[ImGuiCol_Header]                 = bg_mid;
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.35f, 0.20f, 0.12f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = gold_dim;
    colors[ImGuiCol_Separator]              = gold_dim;
    colors[ImGuiCol_SeparatorHovered]       = gold_bright;
    colors[ImGuiCol_SeparatorActive]        = gold_bright;
    colors[ImGuiCol_ResizeGrip]             = gold_dim;
    colors[ImGuiCol_ResizeGripHovered]      = gold_bright;
    colors[ImGuiCol_ResizeGripActive]       = gold_bright;
    colors[ImGuiCol_Tab]                    = bg_base;
    colors[ImGuiCol_TabHovered]             = bg_mid;
    colors[ImGuiCol_TabActive]              = bg_mid;
    colors[ImGuiCol_TabUnfocused]           = bg_base;
    colors[ImGuiCol_TabUnfocusedActive]     = bg_mid;
    colors[ImGuiCol_PlotLines]              = gold_bright;
    colors[ImGuiCol_PlotLinesHovered]       = parchment;
    colors[ImGuiCol_PlotHistogram]          = gold_bright;
    colors[ImGuiCol_PlotHistogramHovered]   = parchment;
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.45f, 0.30f, 0.15f, 1.00f);
    colors[ImGuiCol_DragDropTarget]         = gold_bright;
    colors[ImGuiCol_NavHighlight]           = gold_bright;
    colors[ImGuiCol_NavWindowingHighlight]  = gold_bright;
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.15f, 0.10f, 0.60f);

    // --- Style Vars ---
    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(8, 6);
    style.ItemSpacing       = ImVec2(10, 8);
    style.IndentSpacing     = 25.0f;
    style.ScrollbarSize     = 15.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabMinSize       = 12.0f;
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.TabRounding       = 4.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    style.WindowBorderSize  = 2.0f;
    style.FrameBorderSize   = 1.0f;
}

void initImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    setupFantasyStyle();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void renderMainMenu() {
    ImGui::SetNextWindowPos(ImVec2(WIDTH/2 - 150, HEIGHT/2 - 100));
    ImGui::SetNextWindowSize(ImVec2(300, 200));
    ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    
    ImGui::Text("TERRAX");
    ImGui::Separator();
    
    if (ImGui::Button("Join Local Host", ImVec2(-1, 40))) {
        // Assume host logic handled by --host or similar, but for now just transition
        g_state = GameState::Playing;
    }
    
    if (ImGui::Button("Character Editor", ImVec2(-1, 40))) {
        g_state = GameState::CharacterEditor;
    }
    
    if (ImGui::Button("Exit", ImVec2(-1, 40))) {
        exit(0);
    }
    
    ImGui::End();
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
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSwapInterval(0);
    if (!gl_load()) { std::cerr << "Failed to load OpenGL functions\n"; return 1; }
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glEnable(GL_MULTISAMPLE);

    initImGui(window);

    Shader chunkShader("shaders/chunk.vert", "shaders/chunk.frag");
    Shader waterShader("shaders/water.vert", "shaders/water.frag");
    Shader skyShader("shaders/sky.vert", "shaders/sky.frag");
    Shader charShader("shaders/char.vert", "shaders/char.frag");
    setupSkybox();
    GLuint atlasTexture = generateAtlas();

    g_localPlayerRig = new BipedalRig();
    g_localPlayerRig->setupDefaultHuman(true);

    noclip = true;
    camera.position = glm::vec3(8.5f, 42.0f, 8.5f);
    camera.pitch = -20.0f;
    camera.updateVectors();

    bool clientInitialized = false;

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        deltaTime = std::min(deltaTime, 0.05f);

        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (g_state == GameState::MainMenu) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderMainMenu();
        } 
        else if (g_state == GameState::CharacterEditor) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            int fbW, fbH; glfwGetFramebufferSize(window, &fbW, &fbH);
            glViewport(0, 0, fbW, fbH);
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), fbW / (float)fbH, 0.1f, 1000.0f);
            
            // Build model matrix from manual rotation
            glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(g_editorRotY), glm::vec3(0, 1, 0));
            model = glm::rotate(model, glm::radians(g_editorRotX), glm::vec3(1, 0, 0));
            model = glm::scale(model, glm::vec3(0.06f)); // Scale character to fit world units

            // Camera looks at center of character (height ~1.0 world units)
            glm::mat4 view = glm::lookAt(glm::vec3(0, 1.5, 4), glm::vec3(0, 1.0, 0), glm::vec3(0, 1, 0));

            charShader.use();
            charShader.setMat4("projection", proj);
            charShader.setMat4("view", view);
            charShader.setVec3("lightDir", glm::vec3(0.5f, 1.0f, 0.3f));
            charShader.setVec3("lightColor", glm::vec3(1.0f));
            charShader.setVec3("skyAmbient", glm::vec3(0.2f));

            g_localPlayerRig->update(deltaTime, 0.0f);
            g_localPlayerRig->draw(model, glGetUniformLocation(charShader.id, "model"));

            // ImGui Panels
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(300, (float)fbH), ImGuiCond_Always);
            ImGui::Begin("Character Editor", nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
            if (ImGui::Button("Back to Menu", ImVec2(-1, 0))) g_state = GameState::MainMenu;
            ImGui::Separator();

            static int charType = 0; // 0=Male, 1=Female
            if (ImGui::Combo("Character Type", &charType, "Human Male\0Human Female\0")) {
                g_localPlayerRig->setupDefaultHuman(charType == 0);
            }
            
            if (ImGui::CollapsingHeader("Face Features", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Combo("Hair Style", &g_localPlayerRig->hairStyle, "Bald\0Crew Cut\0Messy Short\0Mohawk\0Spiky\0Side Swept\0Bob\0Long Straight\0Wavy Long\0Bun\0Pigtails\0Braided\0")) {
                    g_localPlayerRig->applyCustomization();
                }
                float hCol[3] = {g_localPlayerRig->hairColor.r/255.0f, g_localPlayerRig->hairColor.g/255.0f, g_localPlayerRig->hairColor.b/255.0f};
                if (ImGui::ColorEdit3("Hair Color", hCol)) {
                    g_localPlayerRig->hairColor = {(uint8_t)(hCol[0]*255), (uint8_t)(hCol[1]*255), (uint8_t)(hCol[2]*255), 255};
                    g_localPlayerRig->applyCustomization();
                }
                if (ImGui::Combo("Eyebrow Style", &g_localPlayerRig->eyebrowStyle, "Straight\0Arched\0Thick\0Thin\0Furrowed\0")) {
                    g_localPlayerRig->applyCustomization();
                }
                float eCol[3] = {g_localPlayerRig->eyeColor.r/255.0f, g_localPlayerRig->eyeColor.g/255.0f, g_localPlayerRig->eyeColor.b/255.0f};
                if (ImGui::ColorEdit3("Eye Color", eCol)) {
                    g_localPlayerRig->eyeColor = {(uint8_t)(eCol[0]*255), (uint8_t)(eCol[1]*255), (uint8_t)(eCol[2]*255), 255};
                    g_localPlayerRig->applyCustomization();
                }
                if (ImGui::Combo("Nose Style", &g_localPlayerRig->noseStyle, "Button\0Wide\0Narrow\0Upturned\0Broad\0")) {
                    g_localPlayerRig->applyCustomization();
                }
                if (ImGui::Combo("Ear Type", &g_localPlayerRig->earType, "None\0Human\0Elven\0Rounded\0Wide\0")) {
                    g_localPlayerRig->applyCustomization();
                }
                if (ImGui::Combo("Armor Set", &g_localPlayerRig->armorType, "None\0Cloth\0Leather\0Heavy\0")) {
                    g_localPlayerRig->applyCustomization();
                }
            }

            ImGui::Separator();
            ImGui::Text("Voxel Tools");
            if (ImGui::RadioButton("Paint", g_editorTool == EditorTool::Paint)) g_editorTool = EditorTool::Paint;
            if (ImGui::RadioButton("Add", g_editorTool == EditorTool::Add)) g_editorTool = EditorTool::Add;
            if (ImGui::RadioButton("Erase", g_editorTool == EditorTool::Erase)) g_editorTool = EditorTool::Erase;
            
            ImGui::Separator();
            ImGui::Text("Color");
            ImGui::ColorPicker4("##picker", (float*)&g_editorColor);
            
            ImGui::Separator();
            if (ImGui::Button("Save Model", ImVec2(-1, 0))) {
                // Future Phase 4 save
            }
            
            ImGui::End();

            // Handling Mouse Input (Rotation vs Editing)
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                double mx, my; glfwGetCursorPos(window, &mx, &my);
                if (!ImGui::GetIO().WantCaptureMouse) {
                    // Ray from screen
                    float rx = (2.0f * (float)mx) / fbW - 1.0f;
                    float ry = 1.0f - (2.0f * (float)my) / fbH;
                    glm::vec4 clipCoords(rx, ry, -1.0f, 1.0f);
                    glm::vec4 eyeCoords = glm::inverse(proj) * clipCoords;
                    eyeCoords.z = -1.0f; eyeCoords.w = 0.0f;
                    glm::vec3 rd = glm::normalize(glm::vec3(glm::inverse(view) * eyeCoords));
                    glm::vec3 ro = glm::vec3(glm::inverse(view) * glm::vec4(0, 0, 0, 1));

                    if (!g_wasEditorClick) {
                        // First frame of click: determine if we hit character
                        auto hit = g_localPlayerRig->raycast(ro, rd, model);
                        if (hit.node) {
                            g_isEditorRotating = false;
                        } else {
                            g_isEditorRotating = true;
                        }
                        g_wasEditorClick = true;
                    }

                    if (g_isEditorRotating) {
                        float dx = (float)(mx - g_lastEditorX);
                        float dy = (float)(my - g_lastEditorY);
                        g_editorRotY += dx * 0.5f;
                        g_editorRotX += dy * 0.5f;
                        // Limit X rotation to avoid flipping
                        g_editorRotX = std::clamp(g_editorRotX, -80.0f, 80.0f);
                    } else {
                        // Continuous editing while holding click
                        auto hit = g_localPlayerRig->raycast(ro, rd, model);
                        if (hit.node && hit.node->volume) {
                            Voxel colorV = {(uint8_t)(g_editorColor.r*255), (uint8_t)(g_editorColor.g*255), (uint8_t)(g_editorColor.b*255), (uint8_t)(g_editorColor.a*255)};
                            if (g_editorTool == EditorTool::Paint) {
                                hit.node->volume->setVoxel(hit.voxel.x, hit.voxel.y, hit.voxel.z, colorV);
                            } else if (g_editorTool == EditorTool::Add) {
                                glm::ivec3 addPos = hit.voxel + hit.normal;
                                hit.node->volume->setVoxel(addPos.x, addPos.y, addPos.z, colorV);
                            } else if (g_editorTool == EditorTool::Erase) {
                                hit.node->volume->setVoxel(hit.voxel.x, hit.voxel.y, hit.voxel.z, {0,0,0,0});
                            }
                            hit.node->volume->updateMesh();
                        }
                    }
                }
                g_lastEditorX = mx; g_lastEditorY = my;
            } else {
                g_wasEditorClick = false;
            }
        }
        else if (g_state == GameState::Playing) {
            if (!clientInitialized) {
                std::cout << "Connecting to server..." << std::endl;
                g_client = new NetworkClient();
                if (!g_client->connect("127.0.0.1", 12345)) {
                    std::cerr << "Failed to connect to server\n";
                    g_state = GameState::MainMenu;
                } else {
                    clientWorld.onRequestChunk = [](int x, int z) {
                        ChunkRequestPacket p { x, z };
                        g_client->send(PacketType::ChunkRequest, &p, sizeof(p));
                    };
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    clientInitialized = true;
                    noclip = true; // Stay in noclip until ground is found
                }
            }

            if (clientInitialized) {
                // Ground spawning logic: once (0,0) is loaded, find height
                static bool spawnedOnGround = false;
                if (!spawnedOnGround) {
                    std::lock_guard<std::mutex> lock(clientWorld.chunksMutex);
                    auto it = clientWorld.chunks.find({0, 0});
                    if (it != clientWorld.chunks.end() && it->second->state != ChunkState::Empty) {
                        // Find highest block at center
                        for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                            if (it->second->get(8, y, 8) != BlockType::Air) {
                                camera.position = glm::vec3(8.5f, (float)y + 1.0f, 8.5f);
                                noclip = false;
                                spawnedOnGround = true;
                                std::cout << "[Client] Spawned on ground at Y=" << y << std::endl;
                                break;
                            }
                        }
                    }
                }

                gameTime = fmodf(gameTime + deltaTime / DAY_CYCLE_SECONDS, 1.0f);

                g_client->update(clientWorld, g_remotePlayers);
                if (g_client->clientID != 0) {
                    static float posSendTimer = 0;
                    posSendTimer += deltaTime;
                    if (posSendTimer >= 0.05f) {
                        PlayerPosPacket p { g_client->clientID, camera.position.x, camera.position.y, camera.position.z, camera.pitch, g_playerYaw };
                        g_client->sendUDP(&p, sizeof(p));
                        posSendTimer = 0;
                    }
                }

                // --- Player Movement relative to Camera ---
                glm::vec3 camForward = glm::normalize(glm::vec3(camera.front.x, 0.0f, camera.front.z));
                glm::vec3 camRight   = glm::normalize(glm::vec3(camera.right.x, 0.0f, camera.right.z));
                
                glm::vec3 moveDir(0.0f);
                if (keyFwd)   moveDir += camForward;
                if (keyBack)  moveDir -= camForward;
                if (keyRight) moveDir += camRight;
                if (keyLeft)  moveDir -= camRight;

                if (glm::length(moveDir) > 0.001f) {
                    moveDir = glm::normalize(moveDir);
                    // Update player facing direction
                    float targetYaw = glm::degrees(atan2f(moveDir.x, moveDir.z));
                    // Smoothly rotate character to face movement direction
                    float angleDiff = targetYaw - g_playerYaw;
                    while (angleDiff > 180.0f) angleDiff -= 360.0f;
                    while (angleDiff < -180.0f) angleDiff += 360.0f;
                    g_playerYaw += angleDiff * std::min(1.0f, deltaTime * 10.0f);
                }

                if (noclip) {
                    if (keyJump) moveDir.y += 1.0f;
                    // In noclip, just move position
                    camera.position += moveDir * 15.0f * deltaTime;
                    camera.velocity = glm::vec3(0);
                } else {
                    // Normal physics movement
                    // We need to translate moveDir into camera's processKeyboard style inputs or just apply velocity
                    float speed = 10.0f;
                    camera.velocity.x = moveDir.x * speed;
                    camera.velocity.z = moveDir.z * speed;
                    if (keyJump && camera.onGround) camera.velocity.y = 8.0f;

                    camera.applyGravity(deltaTime);
                    camera.position = resolveCollision(camera.position, clientWorld);
                }

                int pcx = (int)floorf(camera.position.x / (float)CHUNK_SIZE);
                int pcz = (int)floorf(camera.position.z / (float)CHUNK_SIZE);
                clientWorld.update(pcx, pcz);

                int fbW, fbH; glfwGetFramebufferSize(window, &fbW, &fbH);
                glViewport(0, 0, fbW, fbH);
                float aspect = fbW / (float)fbH;
                glm::mat4 proj = glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 1000.0f);
                
                // Third-person camera matrix: orbits around the player
                glm::vec3 eyePos = camera.position + glm::vec3(0, 1.6f, 0) - (camera.front * g_camDist);
                glm::mat4 view = glm::lookAt(eyePos, camera.position + glm::vec3(0, 1.2f, 0), camera.worldUp);
                
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
                    
                    // Render players using charShader
                    charShader.use();
                    charShader.setMat4("view", view);
                    charShader.setMat4("projection", proj);
                    charShader.setVec3("lightDir", glm::vec3(0.5f, 1.0f, 0.3f));
                    charShader.setVec3("lightColor", glm::vec3(sunFactor));
                    charShader.setVec3("skyAmbient", skyAmbient * 0.5f);
                    GLuint modelLoc = glGetUniformLocation(charShader.id, "model");

                    // Local Player
                    if (g_localPlayerRig) {
                        float velocity = glm::length(camera.velocity);
                        g_localPlayerRig->update(deltaTime, std::min(velocity * 0.5f, 5.0f));
                        glm::mat4 playerM = glm::translate(glm::mat4(1.0f), camera.position);
                        playerM = glm::rotate(playerM, glm::radians(-g_playerYaw + 90.0f), glm::vec3(0, 1, 0));
                        playerM = glm::scale(playerM, glm::vec3(0.06f));
                        g_localPlayerRig->draw(playerM, modelLoc);
                    }
                    
                    // Remote Players
                    for (auto& [id, p] : g_remotePlayers) {
                        if (!p.rig) {
                            p.rig = new BipedalRig();
                            p.rig->setupDefaultHuman(true);
                        }
                        float lerpFactor = 10.0f * deltaTime;
                        glm::vec3 lastPos = p.position;
                        p.position = glm::mix(p.position, p.targetPosition, std::min(1.0f, lerpFactor));
                        p.pitch = glm::mix(p.pitch, p.targetPitch, std::min(1.0f, lerpFactor));
                        p.yaw = glm::mix(p.yaw, p.targetYaw, std::min(1.0f, lerpFactor));

                        float velocity = glm::length(p.position - lastPos) / (deltaTime > 0 ? deltaTime : 1.0f);
                        p.rig->update(deltaTime, std::min(velocity, 10.0f));

                        glm::mat4 playerM = glm::translate(glm::mat4(1.0f), p.position);
                        playerM = glm::rotate(playerM, glm::radians(-p.yaw + 90.0f), glm::vec3(0, 1, 0));
                        playerM = glm::scale(playerM, glm::vec3(0.06f));
                        p.rig->draw(playerM, modelLoc);
                    }
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
            }
        }

        // Render ImGui
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }
    glfwTerminate(); return 0;
}
