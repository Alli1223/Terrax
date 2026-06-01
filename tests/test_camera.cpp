#include "terrax_test.h"
#include "camera.h"
#include <cmath>

// Camera (src/camera.cpp) owns the player's orientation basis and the
// vertical physics impulse/gravity used by the third-person controller.
// These tests pin the orthonormal basis, pitch clamping, jump impulse and
// gravity integration / terminal velocity.

static bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static float len(const glm::vec3& v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }

// --- Default orientation ---

TEST_CASE(Camera_DefaultFacesNegativeZ) {
    Camera cam;
    // yaw=-90, pitch=0  ->  front = (0,0,-1)
    CHECK(approx(cam.front.x, 0.0f));
    CHECK(approx(cam.front.y, 0.0f));
    CHECK(approx(cam.front.z, -1.0f));
}

// --- The front/right/up basis stays orthonormal as we look around ---

TEST_CASE(Camera_BasisOrthonormal) {
    Camera cam;
    const float angles[][2] = {{0,0},{45,20},{-130,-40},{200,89},{-260,-89}};
    for (auto& a : angles) {
        cam.yaw = a[0]; cam.pitch = a[1];
        cam.updateVectors();
        CHECK(approx(len(cam.front), 1.0f, 1e-3f));
        CHECK(approx(len(cam.right), 1.0f, 1e-3f));
        CHECK(approx(len(cam.up),    1.0f, 1e-3f));
        CHECK(approx(glm::dot(cam.front, cam.right), 0.0f, 1e-3f));
        CHECK(approx(glm::dot(cam.front, cam.up),    0.0f, 1e-3f));
        CHECK(approx(glm::dot(cam.right, cam.up),    0.0f, 1e-3f));
    }
}

// --- Pitch is clamped to [-89, 89]; yaw wraps freely ---

TEST_CASE(Camera_PitchClampedUp) {
    Camera cam;
    cam.processMouseMovement(0.0f, 100000.0f);
    CHECK(cam.pitch <= 89.0f + 1e-4f);
    CHECK(approx(cam.pitch, 89.0f));
}

TEST_CASE(Camera_PitchClampedDown) {
    Camera cam;
    cam.processMouseMovement(0.0f, -100000.0f);
    CHECK(cam.pitch >= -89.0f - 1e-4f);
    CHECK(approx(cam.pitch, -89.0f));
}

TEST_CASE(Camera_YawTracksMouse) {
    Camera cam;
    float before = cam.yaw;
    cam.processMouseMovement(50.0f, 0.0f);     // 50 * 0.1 sensitivity = +5 deg
    CHECK(approx(cam.yaw, before + 5.0f));
}

// --- Gravity integration ---

TEST_CASE(Camera_GravityAccumulatesWhenAirborne) {
    Camera cam;
    cam.onGround = false;
    cam.velocity = glm::vec3(0.0f);
    float y0 = cam.position.y;
    cam.applyGravity(0.1f);
    CHECK(cam.velocity.y < 0.0f);        // pulled downward
    CHECK(cam.position.y < y0);          // and moved down
}

TEST_CASE(Camera_GravityTerminalVelocity) {
    Camera cam;
    cam.onGround = false;
    cam.velocity = glm::vec3(0.0f);
    for (int i = 0; i < 200; i++) cam.applyGravity(0.1f);
    CHECK(approx(cam.velocity.y, -40.0f, 1e-3f));   // clamped at terminal
}

TEST_CASE(Camera_GroundedStopsFall) {
    Camera cam;
    cam.onGround = true;
    cam.velocity.y = -12.0f;
    cam.applyGravity(0.1f);
    CHECK(approx(cam.velocity.y, 0.0f));   // standing zeroes downward velocity
}

// --- Movement & jump ---

TEST_CASE(Camera_ForwardMovesAtMovementSpeed) {
    Camera cam;                 // facing -Z
    cam.processKeyboard(1, 0, 0, 0.016f);
    float horiz = std::sqrt(cam.velocity.x*cam.velocity.x + cam.velocity.z*cam.velocity.z);
    CHECK(approx(horiz, cam.movementSpeed, 1e-3f));
    CHECK(cam.velocity.z < 0.0f);   // moving toward -Z
}

TEST_CASE(Camera_JumpOnlyWhenGrounded) {
    Camera cam;
    cam.onGround = false;
    cam.processKeyboard(0, 0, 1, 0.016f);
    CHECK(approx(cam.velocity.y, 0.0f));   // no jump in mid-air

    cam.onGround = true;
    cam.processKeyboard(0, 0, 1, 0.016f);
    CHECK(cam.velocity.y > 0.0f);          // jump impulse applied
    CHECK(!cam.onGround);                  // and we leave the ground
}

// --- View matrix maps the eye to the origin ---

TEST_CASE(Camera_ViewMatrixPlacesEyeAtOrigin) {
    Camera cam;
    cam.position = glm::vec3(5.0f, 40.0f, -3.0f);
    cam.updateVectors();
    glm::mat4 view = cam.getViewMatrix();
    glm::vec4 eye = view * glm::vec4(cam.position, 1.0f);
    CHECK(approx(eye.x, 0.0f, 1e-3f));
    CHECK(approx(eye.y, 0.0f, 1e-3f));
    CHECK(approx(eye.z, 0.0f, 1e-3f));
}
