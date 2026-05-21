#pragma once
#include "game_types.h"
#include "game_settings.h"
#include "game_session.h"
#include "camera.h"
#include "world.h"
#include "network.h"
#include "voxel_model.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <future>

struct LeafParticle {
    glm::vec3 pos, vel;
    float     life = 0.0f, maxLife = 1.0f;
    uint8_t   leafBT = 0;
};


struct AppContext {
    // --- State ---
    GameState   state       = GameState::MainMenu;
    SessionMode sessionMode = SessionMode::None;
    bool paused   = false;
    bool noclip   = false;

    // --- Timing ---
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    float gameTime  = 0.3f;

    // --- Player ---
    BipedalRig* playerRig  = nullptr;
    char playerName[MAX_PLAYER_NAME + 1] = {};
    float playerYaw        = 0.0f;
    bool spawnedOnGround   = false;
    float playerHealth     = 1.0f;

    // --- Camera / mouse ---
    Camera camera;
    double lastMouseX = WINDOW_WIDTH  / 2.0;
    double lastMouseY = WINDOW_HEIGHT / 2.0;
    bool   firstMouse = true;

    // --- Movement keys ---
    int keyFwd = 0, keyBack = 0, keyLeft = 0, keyRight = 0, keyJump = 0, keySprint = 0;

    // --- Lantern ---
    bool  lanternHeld = false;
    float flickerTime = 0.0f;

    // --- Settings ---
    GameSettings settings;

    // --- World ---
    World world;

    // --- Network ---
    NetworkClient* client          = nullptr;
    bool weOwnServer               = false;
    bool clientInitialized         = false;
    bool joinNameSent              = false;
    std::string connectHost        = "127.0.0.1";
    unsigned short connectPort     = DEFAULT_SERVER_PORT;
    std::unordered_map<uint32_t, RemotePlayer> remotePlayers;

    // --- Chat / HUD ---
    bool chatOpen       = false;
    char chatInput[MAX_CHAT_TEXT + 1] = {};
    bool showPlayerList = false;

    // --- Character / house editor ---
    float editorRotX = 0.0f, editorRotY = 0.0f;
    bool  wasEditorClick   = false;
    bool  isEditorRotating = false;
    double lastEditorX     = 0.0, lastEditorY = 0.0;
    glm::vec4 editorColor  = glm::vec4(1.0f);
    EditorTool editorTool  = EditorTool::Paint;
    float camDist          = 10.0f;
    int   editorCharType   = 0;
    int   editorBlock      = 0;   // selected build-block index in the house editor

    // --- House generator ---
    HouseModel* houseModel        = nullptr;
    bool        housePreviewActive = false;   // H once = preview, H again = confirm
    glm::vec3   housePreviewPos    = glm::vec3(0.0f);
    float       housePreviewYaw    = 0.0f;

    // --- Gameplay state (updated each frame by gameplay system) ---
    bool  headUnderwater = false;
    float breathTime     = 30.0f;
    float posSendTimer   = 0.0f;

    // --- World Map ---
    bool   showMap         = false;
    GLuint mapTex          = 0;
    float  mapZoom         = 1.0f;   // half-extent = 256 / zoom world blocks
    float  mapRotDeg       = 0.0f;
    float  mapPanX         = 0.0f;   // world-space pan relative to player
    float  mapPanZ         = 0.0f;
    float  mapBuiltCX      = 0.0f;   // player position when texture was last built
    float  mapBuiltCZ      = 0.0f;
    bool   mapNeedsRebuild = true;
    bool   mapBuilding     = false;
    std::future<std::vector<uint8_t>> mapFuture;

    // --- Leaf particles ---
    std::vector<LeafParticle> leafParticles;
    float leafSpawnTimer = 0.0f;

    AppContext();
    ~AppContext();
};
