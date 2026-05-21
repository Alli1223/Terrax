#pragma once
#include "game_object.h"
#include <array>

class VoxelVolume;

// 1 voxel = this many world units (the character-rig scale); shared by Prop
// rendering and the deterministic placement pass.
static constexpr float PROP_SCALE = 0.06f;

// Furniture and town decorations. The order matters: PropLibrary indexes by it.
enum class PropType : uint8_t {
    Bookshelf = 0, Bed, Lantern, Cooker, Table, Chair, Crockery,   // furniture
    StreetLamp, PottedPlant, Bush, Bench, Fence,                   // decorations
    Count
};

// One shared VoxelVolume mesh per PropType, built once on the main thread.
// Prop instances are lightweight {type, position, yaw} and only reference the
// shared mesh, so spawning a prop costs nothing on the GPU.
class PropLibrary {
public:
    ~PropLibrary();
    void buildAll();                 // main thread + GL context; idempotent
    void destroy();
    bool built() const { return isBuilt; }
    VoxelVolume* mesh(PropType t) const;

private:
    std::array<VoxelVolume*, (int)PropType::Count> volumes{};
    bool isBuilt = false;
};

// A static, physical, small-voxel object — furniture or a town decoration.
class Prop : public GameObject {
public:
    Prop(PropType t, glm::vec3 pos, float yawDeg, const PropLibrary* lib);

    void update(float dt, World& world) override;
    void draw(GLuint modelLoc) const override;
    void getAABB(glm::vec3& mn, glm::vec3& mx) const override;

    PropType type;
    uint32_t placementIndex = 0xFFFFFFFFu;   // index into getPropPlacements()

private:
    const PropLibrary* library = nullptr;    // borrowed
};
