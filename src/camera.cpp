#include "camera.h"
#include <glm/glm.hpp>
#include <algorithm>

Camera::Camera(glm::vec3 pos)
    : position(pos), worldUp(0, 1, 0), yaw(-90.0f), pitch(0.0f),
      movementSpeed(8.0f), mouseSensitivity(0.1f), fov(70.0f),
      velocity(0), onGround(false) {
    updateVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

void Camera::updateVectors() {
    glm::vec3 f;
    f.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    f.y = sinf(glm::radians(pitch));
    f.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    front = glm::normalize(f);
    right = glm::normalize(glm::cross(front, worldUp));
    up    = glm::normalize(glm::cross(right, front));
}

void Camera::processKeyboard(int fwd, int rt, int upInput, float dt) {
    glm::vec3 flatFront = glm::normalize(glm::vec3(front.x, 0, front.z));
    glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, worldUp));

    glm::vec3 dir(0);
    if (fwd  >  0) dir += flatFront;
    if (fwd  <  0) dir -= flatFront;
    if (rt   >  0) dir += flatRight;
    if (rt   <  0) dir -= flatRight;

    if (glm::length(dir) > 0.001f)
        dir = glm::normalize(dir);

    velocity.x = dir.x * movementSpeed;
    velocity.z = dir.z * movementSpeed;

    if (upInput > 0 && onGround) {
        velocity.y = 8.5f; // jump impulse
        onGround = false;
    }
}

void Camera::applyGravity(float dt) {
    if (!onGround) {
        velocity.y -= 22.0f * dt; // gravity
        velocity.y = std::max(velocity.y, -40.0f);
    } else {
        velocity.y = std::max(0.0f, velocity.y);
    }
    position += velocity * dt;
}

void Camera::processMouseMovement(float xoff, float yoff) {
    yaw   += xoff * mouseSensitivity;
    pitch += yoff * mouseSensitivity;
    pitch = std::clamp(pitch, -89.0f, 89.0f);
    updateVectors();
}
