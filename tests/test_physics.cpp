#include "terrax_test.h"
#include "physics.h"
#include "world.h"
#include "camera.h"
#include <memory>
#include <cmath>

// resolveCollision (src/physics.cpp) sweeps the player AABB against the
// voxel world: settle onto floors, stop under ceilings, slide along walls,
// and clamp at the world floor (y=0). We build a single loaded chunk and
// lay blocks by hand so every scenario is fully deterministic.

static bool approx(float a, float b, float eps = 1e-2f) { return std::fabs(a - b) <= eps; }

static void loadEmptyChunk(World& w) {
    auto c = std::make_unique<Chunk>(ChunkPos{0, 0}, false);
    c->state = ChunkState::Generated;       // not Empty/Generating -> readable
    w.chunks[ChunkPos{0, 0}] = std::move(c);
}

static void layLayer(World& w, int y, BlockType t) {
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int z = 0; z < CHUNK_SIZE; z++)
            w.setBlock(x, y, z, t);
}

static const float HW = 0.3f, PH = 1.8f, DT = 0.1f;

TEST_CASE(Physics_FallSettlesOnFloor) {
    World w(false); loadEmptyChunk(w);
    layLayer(w, 63, BlockType::Stone);          // floor: top surface at y=64

    Camera cam;
    cam.onGround = false;
    cam.velocity = glm::vec3(0.0f, -5.0f, 0.0f);
    glm::vec3 r = resolveCollision(glm::vec3(8.0f, 63.9f, 8.0f), cam, HW, PH, w, DT);

    CHECK(approx(r.y, 64.0f));
    CHECK(cam.onGround);
    CHECK(approx(cam.velocity.y, 0.0f));
}

TEST_CASE(Physics_NoFloorKeepsFalling) {
    World w(false); loadEmptyChunk(w);          // all air

    Camera cam;
    cam.onGround = false;
    cam.velocity = glm::vec3(0.0f, -5.0f, 0.0f);
    glm::vec3 r = resolveCollision(glm::vec3(8.0f, 64.0f, 8.0f), cam, HW, PH, w, DT);

    CHECK(!cam.onGround);
    CHECK(approx(cam.velocity.y, -5.0f));       // gravity untouched
    CHECK(approx(r.y, 64.0f));                   // position unchanged
}

TEST_CASE(Physics_CeilingStopsRise) {
    World w(false); loadEmptyChunk(w);
    layLayer(w, 65, BlockType::Stone);          // ceiling cell at the head layer

    Camera cam;
    cam.onGround = false;
    cam.velocity = glm::vec3(0.0f, 5.0f, 0.0f); // rising
    glm::vec3 r = resolveCollision(glm::vec3(8.0f, 64.0f, 8.0f), cam, HW, PH, w, DT);

    CHECK(approx(cam.velocity.y, 0.0f));        // bonk: upward velocity killed
    CHECK(r.y < 64.0f);                          // pushed back down
}

TEST_CASE(Physics_WallStopsMovementX) {
    World w(false); loadEmptyChunk(w);
    for (int y = 63; y <= 66; y++)
        for (int z = 0; z < CHUNK_SIZE; z++)
            w.setBlock(10, y, z, BlockType::Stone);   // wall plane at x=10

    Camera cam;
    cam.onGround = false;
    cam.velocity = glm::vec3(5.0f, 0.0f, 0.0f);       // walking +X into wall
    glm::vec3 r = resolveCollision(glm::vec3(9.8f, 64.0f, 8.0f), cam, HW, PH, w, DT);

    CHECK(approx(cam.velocity.x, 0.0f));
    CHECK(r.x < 9.8f);                                 // shoved out of the wall
}

TEST_CASE(Physics_WallStopsMovementZ) {
    World w(false); loadEmptyChunk(w);
    for (int y = 63; y <= 66; y++)
        for (int x = 0; x < CHUNK_SIZE; x++)
            w.setBlock(x, y, 10, BlockType::Stone);   // wall plane at z=10

    Camera cam;
    cam.onGround = false;
    cam.velocity = glm::vec3(0.0f, 0.0f, 5.0f);
    glm::vec3 r = resolveCollision(glm::vec3(8.0f, 64.0f, 9.8f), cam, HW, PH, w, DT);

    CHECK(approx(cam.velocity.z, 0.0f));
    CHECK(r.z < 9.8f);
}

TEST_CASE(Physics_ClampsAtWorldFloor) {
    World w(false);                              // no chunks needed
    Camera cam;
    cam.onGround = false;
    cam.velocity = glm::vec3(0.0f, -10.0f, 0.0f);
    glm::vec3 r = resolveCollision(glm::vec3(8.0f, -5.0f, 8.0f), cam, HW, PH, w, DT);

    CHECK(approx(r.y, 0.0f));
    CHECK(cam.onGround);
    CHECK(approx(cam.velocity.y, 0.0f));
}

// Walking into a one-block kerb climbs it (auto step-up) and keeps moving.
// The feet are placed at the step's edge (x+hw == 10.0) so the vertical settle
// can't see the step column — this exercises the horizontal step-up path.
TEST_CASE(Physics_StepUpOneBlock) {
    World w(false); loadEmptyChunk(w);
    layLayer(w, 63, BlockType::Stone);                     // floor: top surface at y=64
    for (int z = 0; z < CHUNK_SIZE; z++)
        w.setBlock(10, 64, z, BlockType::Stone);           // one-block step at x=10 (top y=65)

    Camera cam;
    cam.onGround = true;
    cam.velocity = glm::vec3(5.0f, 0.0f, 0.0f);            // walking +X into the step
    glm::vec3 r = resolveCollision(glm::vec3(9.7f, 64.0f, 8.0f), cam, HW, PH, w, DT);

    CHECK(approx(r.y, 65.0f));                              // climbed onto the step
    CHECK(cam.velocity.x > 0.1f);                           // kept its forward momentum
}

// A two-block (or taller) wall is NOT a step: the player is shoved out and
// stopped, exactly like the plain wall cases above.
TEST_CASE(Physics_NoStepUpTwoBlocks) {
    World w(false); loadEmptyChunk(w);
    layLayer(w, 63, BlockType::Stone);                     // floor: top surface at y=64
    for (int z = 0; z < CHUNK_SIZE; z++) {
        w.setBlock(10, 64, z, BlockType::Stone);           // two-block wall at x=10
        w.setBlock(10, 65, z, BlockType::Stone);
    }

    Camera cam;
    cam.onGround = true;
    cam.velocity = glm::vec3(5.0f, 0.0f, 0.0f);
    glm::vec3 r = resolveCollision(glm::vec3(9.7f, 64.0f, 8.0f), cam, HW, PH, w, DT);

    CHECK(approx(cam.velocity.x, 0.0f));                    // blocked, not climbed
    CHECK(r.x < 9.7f);                                      // shoved out of the wall
    CHECK(approx(r.y, 64.0f));                              // stayed at ground level
}
