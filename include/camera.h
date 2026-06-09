#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;
    float pitch;
    float movementSpeed;
    float mouseSensitivity;
    float fov;

    // Physics
    glm::vec3 velocity;
    bool onGround;

    // Render-only easing applied after a step-up so climbing terrain glides
    // instead of snapping a whole block. resolveCollision pushes this negative
    // when the body climbs a ledge; gameplay eases it back to 0 each frame and
    // the renderer applies it to the third-person camera and body. It never
    // feeds back into collision — purely where things are drawn.
    float stepSmoothOffset = 0.0f;

    Camera(glm::vec3 pos = glm::vec3(0.0f, 40.0f, 0.0f));

    glm::mat4 getViewMatrix() const;
    void processKeyboard(int forward, int right, int up, float dt);
    void processMouseMovement(float xoffset, float yoffset);
    void applyGravity(float dt);
    void updateVectors();
};
