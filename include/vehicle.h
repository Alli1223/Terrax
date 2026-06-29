#pragma once
#include "game_object.h"

class VoxelVolume;

// Base for moving vehicles. The player can stand on a vehicle's deck and be
// carried; gameplay reads `velocity` to do the carrying.
class Vehicle : public GameObject {
public:
    Vehicle() : GameObject(ObjectKind::Vehicle) {}
    glm::vec3 velocity{0.0f};

    virtual float deckTopY() const = 0;                    // walkable deck height
    virtual bool  onDeck(const glm::vec3& worldPos) const = 0;
};

// A ferry that shuttles back and forth across a wide water crossing. The
// server simulates it (serverStep) and broadcasts its state; clients create
// one per network id and interpolate it (update).
class Ferry : public Vehicle {
public:
    Ferry();

    void  update(float dt, World& world) override;         // client interpolation
    void  draw(GLuint modelLoc) const override;
    void  getAABB(glm::vec3& mn, glm::vec3& mx) const override;
    float deckTopY() const override;
    bool  onDeck(const glm::vec3& worldPos) const override;

    void  serverStep(float dt);                            // server-authoritative

    // Server-side route state.
    int   routeIndex = -1;
    float routeT     = 0.0f;        // 0..1 along dockA -> dockB
    int   dir        = 1;          // +1 toward B, -1 toward A
    float waitTimer  = 0.0f;       // pause at a dock

    // Client-side interpolation target.
    glm::vec3 targetPos{0.0f};
    float     targetYaw = 0.0f;

    VoxelVolume* mesh = nullptr;    // borrowed shared mesh (client only)
};

// Lazily builds (once, main thread) and returns the shared ferry mesh.
VoxelVolume* getFerryMesh();
// Builds the ferry voxel model (CPU only — caller uploads with updateMesh()).
VoxelVolume* buildFerryVolume();

// --- Personal vehicles (horse / wagon / kite) ------------------------------
// Shared voxel models for the player-attached vehicles. Lazily built once on
// the main thread (mirrors getFerryMesh) and rendered under/behind/above the
// player by Player::draw. The CPU-only builders are exposed for the asset
// catalog / tests; gameplay uses the cached getXMesh() singletons.
VoxelVolume* buildHorseVolume();
VoxelVolume* buildWagonVolume();
VoxelVolume* buildKiteVolume();
VoxelVolume* getHorseMesh();
VoxelVolume* getWagonMesh();
VoxelVolume* getKiteMesh();
