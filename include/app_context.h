#pragma once
#include "game_types.h"
#include "game_session.h"
#include "camera.h"
#include "world.h"
#include "network.h"
#include "voxel_model.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

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
    int keyFwd = 0, keyBack = 0, keyLeft = 0, keyRight = 0, keyJump = 0;

    // --- Lantern ---
    bool  lanternHeld = false;
    float flickerTime = 0.0f;

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

    // --- Character editor ---
    float editorRotX = 0.0f, editorRotY = 0.0f;
    bool  wasEditorClick   = false;
    bool  isEditorRotating = false;
    double lastEditorX     = 0.0, lastEditorY = 0.0;
    glm::vec4 editorColor  = glm::vec4(1.0f);
    EditorTool editorTool  = EditorTool::Paint;
    float camDist          = 10.0f;
    int   editorCharType   = 0;

    // --- Gameplay state (updated each frame by gameplay system) ---
    bool  headUnderwater = false;
    float breathTime     = 30.0f;
    float posSendTimer   = 0.0f;

    AppContext();
    ~AppContext();
};
