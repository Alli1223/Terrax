#pragma once
#include "game_object.h"
#include <array>

class VoxelVolume;

// 1 voxel = this many world units (the character-rig scale); shared by Prop
// rendering and the deterministic placement pass.
static constexpr float PROP_SCALE = 0.06f;

// Furniture and town decorations. The order matters: PropLibrary indexes by it.
// New entries should be appended just before Count so existing PropType ids
// stay stable (the furniture builders in furniture.cpp are looked up by id).
enum class PropType : uint8_t {
    // Generic / living-room furniture
    Bookshelf = 0, Bed, Lantern, Cooker, Table, Chair, Crockery,
    // Decorations
    StreetLamp, PottedPlant, Bush, Bench, Fence,
    // Room-specific furniture (Phase 2)
    Sink, KitchenCounter, Wardrobe, Desk, Couch, SideTable,
    // Special-building furniture (Phase 3)
    Anvil, Forge, BarCounter, BarStool, Cauldron, AlchemyTable,
    // Trade signs (hanging from a wooden post outside special buildings):
    // each is a distinct PropType so the placer can pick the right icon for
    // the building kind. Adding a new role just means adding a new PropType
    // here, a builder in furniture.cpp, and a case in placeTradeSign().
    SignAnvil, SignMug, SignStar, SignWheat,
    // Cosy home furnishings. Fireplace also registers a warm point light in the
    // renderer (collectLanternLights), so hearths glow day and night.
    Fireplace, Rug, WallPainting, FlowerVase,
    Count
};

// One shared VoxelVolume mesh per PropType, built once on the main thread.
// Prop instances are lightweight {type, position, yaw} and only reference the
// shared mesh, so spawning a prop costs nothing on the GPU.
// Number of distinct door styles/colours — see buildDoor.
static constexpr int DOOR_VARIANTS = 6;

class PropLibrary {
public:
    ~PropLibrary();
    void buildAll();                 // main thread + GL context; idempotent
    void destroy();
    bool built() const { return isBuilt; }
    VoxelVolume* mesh(PropType t) const;
    VoxelVolume* doorMesh(int variant) const;

private:
    std::array<VoxelVolume*, (int)PropType::Count> volumes{};
    std::array<VoxelVolume*, DOOR_VARIANTS>        doorVolumes{};
    bool isBuilt = false;
};

// A static, physical, small-voxel object — furniture or a town decoration.
class Prop : public GameObject {
public:
    Prop(PropType t, glm::vec3 pos, float yawDeg, const PropLibrary* lib);

    void update(float dt, World& world) override;
    void draw(GLuint modelLoc) const override;
    void getAABB(glm::vec3& mn, glm::vec3& mx) const override;

    // Maps the prop's `type` to a sit / lie / etc. offer. Beds offer LieBed,
    // chairs and bar stools offer SitChair; everything else returns false.
    // Add new offers here as more PropTypes become interactive.
    bool getInteraction(Interaction& out) const override;

    PropType type;
    uint32_t placementIndex = 0xFFFFFFFFu;   // index into getPropPlacements()

private:
    const PropLibrary* library = nullptr;    // borrowed
};

// 1 voxel = this many world units for the door mesh (a door fills a 3x4-block
// opening — see buildDoor).
static constexpr float DOOR_SCALE = 0.2f;

class ObjectManager;

// A house front door that swings open as the local player or any nearby NPC
// approaches and shuts again when they leave. Borrows the shared door mesh
// from the PropLibrary; the `objects` pointer is the ObjectManager that
// owns this door, used to scan the local-region NPC list each tick.
class Door : public GameObject {
public:
    Door(glm::vec3 hinge, float closedYawDeg, glm::ivec2 doorCell, glm::ivec2 alongWall,
         int variant, const PropLibrary* lib, const glm::vec3* playerPos,
         const ObjectManager* objects);

    void update(float dt, World& world) override;
    void draw(GLuint modelLoc) const override;
    void getAABB(glm::vec3& mn, glm::vec3& mx) const override;

    // True while the panel is more shut than open — used to occlude light.
    bool blocksLight() const { return openAmount < 0.5f; }

    uint32_t   placementIndex = 0xFFFFFFFFu;   // index into getDoorPlacements()
    glm::ivec2 wallCell{0, 0};                 // doorway centre cell (world XZ)
    glm::ivec2 wallDir{0, 0};                  // along-wall step (door spans +-1)

private:
    int   variant    = 0;                    // door style/colour index
    float closedYaw  = 0.0f;
    float openAmount = 0.0f;                 // 0 = shut, 1 = fully open
    const PropLibrary*  library   = nullptr; // borrowed
    const glm::vec3*    playerPos = nullptr; // borrowed — local player position
    const ObjectManager* objects  = nullptr; // borrowed — for NPC proximity queries
};
