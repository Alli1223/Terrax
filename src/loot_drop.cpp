#include "loot_drop.h"
#include "world.h"
#include <cmath>

LootDrop::LootDrop() : GameObject(ObjectKind::Loot) {
    bobPhase = (float)(rand() % 1000) * 0.01f;   // de-sync drops nearby
}

LootDrop::~LootDrop() = default;

void LootDrop::setItem(std::unique_ptr<Item> it) {
    item = std::move(it);
    if (item) mesh = item->getVoxelVolume();
}

std::unique_ptr<Item> LootDrop::takeItem() {
    mesh = nullptr;
    return std::move(item);
}

void LootDrop::update(float dt, World& world) {
    age += dt;
    if (age >= lifeTime) {
        dead = true;
        return;
    }
    spinAngle = std::fmod(spinAngle + 60.0f * dt, 360.0f);
    bobPhase += dt * 1.8f;

    if (!grounded) {
        // Simple gravity + integrate. World blocks are 1-unit cubes.
        velocity.y -= 18.0f * dt;
        glm::vec3 next = position + velocity * dt;

        // Step down through any blocks below until we land on a non-air.
        int bx = (int)std::floor(next.x);
        int by = (int)std::floor(next.y);
        int bz = (int)std::floor(next.z);
        if (world.getBlock(bx, by, bz) != BlockType::Air
            && world.getBlock(bx, by, bz) != BlockType::Water) {
            // Sat on top of the block we'd otherwise enter.
            next.y = (float)(by + 1);
            velocity = glm::vec3(0.0f);
            grounded = true;
        }
        position = next;
    }
}

void LootDrop::draw(GLuint modelLoc) const {
    if (!mesh) return;

    // Float a little above the resting position and spin slowly so the
    // player can spot the drop from a distance.
    float hover = grounded ? (0.25f + 0.05f * std::sin(bobPhase)) : 0.0f;
    glm::mat4 m = glm::translate(glm::mat4(1.0f),
                                  position + glm::vec3(0.0f, hover, 0.0f));
    m = glm::rotate(m, glm::radians(spinAngle), glm::vec3(0.0f, 1.0f, 0.0f));
    // Voxel meshes are built in atlas-pixel units (~16 per metre on the
    // character rigs). Scale way down so drops fit comfortably on a
    // single world block.
    m = glm::scale(m, glm::vec3(0.05f));
    // Centre the mesh on its volume bounds (mesh local-origin is the
    // bottom corner of the volume).
    m = glm::translate(m, glm::vec3(-mesh->sizeX * 0.5f,
                                     0.0f,
                                    -mesh->sizeZ * 0.5f));
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &m[0][0]);
    mesh->draw();
}

void LootDrop::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    const float r = 0.4f;
    mn = position + glm::vec3(-r, 0.0f, -r);
    mx = position + glm::vec3( r, 0.8f,  r);
}
