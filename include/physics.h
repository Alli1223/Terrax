#pragma once
#include <glm/glm.hpp>

class World;
class Camera;

glm::vec3 resolveCollision(const glm::vec3& pos, Camera& camera,
                            float playerHalfWidth, float playerHeight,
                            const World& world);
