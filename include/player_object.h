#pragma once
#include "game_object.h"

class Camera;
class BipedalRig;
struct RemotePlayer;

// A player in the object system. Two modes, both borrow-only — a Player never
// frees the rig it draws:
//  - local:  borrows the AppContext Camera + BipedalRig; driven by gameplay.
//  - remote: borrows a RemotePlayer record; interpolated from network state.
class Player : public GameObject {
public:
    Player(Camera* cam, BipedalRig* rig);       // local
    Player(RemotePlayer* rp, uint32_t netId);   // remote
    ~Player() override = default;

    void update(float dt, World& world) override;
    void draw(GLuint modelLoc) const override;
    void getAABB(glm::vec3& mn, glm::vec3& mx) const override;

    bool isLocal     = false;
    bool lanternHeld = false;   // local: set by gameplay each frame

private:
    Camera*       camera   = nullptr;   // local only, borrowed
    BipedalRig*   localRig = nullptr;   // local only, borrowed
    RemotePlayer* remote   = nullptr;   // remote only, borrowed
};
