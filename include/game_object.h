#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include "gl_loader.h"

class World;

// --- Object-oriented world -------------------------------------------------
// The voxel terrain stays chunk-based; GameObjects are the dynamic entities
// layered on top of it — players, props (furniture, decorations) and vehicles
// (boats, ferries). They are owned polymorphically by an ObjectManager (the
// local player is the one exception: AppContext holds it directly).

enum class ObjectKind : uint8_t { Player = 0, Prop, Vehicle, Door, NPC };

class GameObject {
public:
    explicit GameObject(ObjectKind k) : kind(k) {}
    virtual ~GameObject() = default;

    // Per-frame client-side advance (animation, interpolation).
    virtual void update(float dt, World& world) = 0;

    // Issue draw calls. The caller has already bound a shader; `modelLoc` is
    // the location of that shader's "model" uniform.
    virtual void draw(GLuint modelLoc) const = 0;

    // World-space AABB, for culling / streaming / collision broadphase.
    virtual void getAABB(glm::vec3& mn, glm::vec3& mx) const = 0;

    ObjectKind kind;
    glm::vec3  position{0.0f};
    float      yaw  = 0.0f;     // degrees about +Y
    uint32_t   id   = 0;        // 0 = local-only; >0 = networked id
    bool       dead = false;    // lifecycle removal flag

protected:
    // Translate + Y-rotate + uniform-scale matrix from position/yaw.
    glm::mat4 baseMatrix(float uniformScale) const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, glm::radians(yaw), glm::vec3(0, 1, 0));
        m = glm::scale(m, glm::vec3(uniformScale));
        return m;
    }
};
