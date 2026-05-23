#pragma once
#include "game_types.h"
#include "game_settings.h"
#include "game_session.h"
#include "camera.h"
#include "world.h"
#include "network.h"
#include "voxel_model.h"
#include "object_manager.h"
#include "player_object.h"
#include "prop.h"
#include "interactable.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <future>
#include <atomic>
#include <thread>

struct LeafParticle {
    glm::vec3 pos, vel;
    float     life = 0.0f, maxLife = 1.0f;
    uint8_t   leafBT = 0;
};

struct WeatherParticle {
    glm::vec3 pos{0.0f}, vel{0.0f};
    float     life = 0.0f;
    float     seed = 0.0f;   // per-particle phase offset for drift
};

struct AmbientParticle {
    glm::vec3 pos{0.0f}, vel{0.0f};
    glm::vec3 color{1.0f};
    float     life     = 0.0f;
    float     maxLife  = 1.0f;
    float     size     = 0.08f;
    float     seed     = 0.0f;   // phase offset for bobbing / pulsing
    uint8_t   kind     = 0;      // 0 = pollen, 1 = firefly, 2 = campfire ember
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
    Player*     localPlayer = nullptr;   // GameObject wrapper over camera + rig
    char playerName[MAX_PLAYER_NAME + 1] = {};
    float playerYaw        = 0.0f;
    bool spawnedOnGround   = false;
    int  spawnX = 8, spawnZ = 8;   // world column the player spawns at
    float playerHealth     = 1.0f;
    float regenDelay       = 0.0f;   // delay before out-of-combat health regen

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

    // --- Objects (remote players, props, vehicles) ---
    ObjectManager objectManager;
    PropLibrary   propLibrary;   // shared furniture/decoration meshes

    // --- Network ---
    NetworkClient* client          = nullptr;
    bool weOwnServer               = false;
    bool clientInitialized         = false;
    bool joinNameSent              = false;
    std::string connectHost        = "127.0.0.1";
    unsigned short connectPort     = DEFAULT_SERVER_PORT;
    std::unordered_map<uint32_t, RemotePlayer> remotePlayers;

    // --- Chat / HUD ---
    bool chatOpen         = false;
    char chatInput[MAX_CHAT_TEXT + 1] = {};
    bool showPlayerList   = false;
    bool showDebugOverlay = true;    // F3 — session/debug stats overlay

    // --- Character / house editor ---
    float editorRotX = 0.0f, editorRotY = 0.0f;
    bool  wasEditorClick   = false;
    bool  isEditorRotating = false;
    double lastEditorX     = 0.0, lastEditorY = 0.0;
    glm::vec4 editorColor  = glm::vec4(1.0f);
    EditorTool editorTool  = EditorTool::Paint;
    float camDist          = 10.0f;
    float camDistSmooth    = 10.0f;   // wall-clipped third-person distance (gameplay)
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

    // --- Loading (background world-gen) ---
    // The worker thread is spawned when the user clicks Host/Singleplayer/Join
    // and the state transitions to GameState::Loading. The worker pre-warms
    // getTownPlan() so the survey doesn't stall the first gameplay frame.
    // The main thread polls `loadingWorkerDone` from the Loading state.
    std::atomic<bool>  loadingWorkerDone{false};
    std::atomic<int>   loadingHighStage{0};      // 0..kLoadingStageCount-1 (see ui.cpp)
    std::atomic<float> loadingHighFraction{0.0f};
    std::thread        loadingThread;
    bool               loadingFinalised = false;   // GL-bound finalisation done on main thread

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

    // --- Weather (client-side: dynamic cycles with occasional storms) ---
    float weatherIntensity = 0.0f;   // eased 0 (clear) .. 1 (full storm)
    float weatherTarget    = 0.0f;   // intensity the current phase eases toward
    float weatherTimer     = 5.0f;   // seconds until the next weather phase
    int   weatherKind      = 0;      // 0 = rain biome, 1 = snow biome
    std::vector<WeatherParticle> weatherParticles;

    // --- Ambient atmosphere particles (pollen, fireflies, embers) ---
    std::vector<AmbientParticle> ambientParticles;
    float ambientSpawnTimer = 0.0f;

    // --- Player pose (sit / lie / …) ---
    // Set by the interactable system when the player presses E on a chair or
    // bed; cleared on E-again or any movement key. While non-Standing the
    // player's camera is locked to `poseAnchorPos` and movement is ignored.
    PlayerPose playerPose       = PlayerPose::Standing;
    glm::vec3  poseAnchorPos    = glm::vec3(0.0f);
    float      poseAnchorYaw    = 0.0f;
    Interaction pendingInteraction{};   // best offer in front of the player this frame

    // --- NPC interaction ---
    bool        interactPressed = false;   // E pressed this frame (set by input)
    std::string talkTargetName;            // villager currently faced ("" = none)
    uint32_t    talkTargetSeed = 0;
    glm::vec3   talkTargetPos{0.0f};
    float       talkTimer = 0.0f;          // dialogue box visible countdown
    std::string talkName;
    std::string talkLine;
    int         talkCount = 0;             // advances the flavour line each talk

    AppContext();
    ~AppContext();
};
