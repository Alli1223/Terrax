#include "object_manager.h"
#include "prop.h"
#include "prop_placement.h"
#include <algorithm>

void ObjectManager::add(std::unique_ptr<GameObject> obj) {
    objs.push_back(std::move(obj));
}

void ObjectManager::clear() {
    objs.clear();
    liveProps.clear();
    liveDoors.clear();
}

void ObjectManager::updateAll(float dt, World& world) {
    for (auto& o : objs)
        if (!o->dead) o->update(dt, world);
    objs.erase(std::remove_if(objs.begin(), objs.end(),
                   [](const std::unique_ptr<GameObject>& o) { return o->dead; }),
               objs.end());
}

void ObjectManager::drawAll(GLuint modelLoc) const {
    for (const auto& o : objs)
        if (!o->dead) o->draw(modelLoc);
}

GameObject* ObjectManager::findById(uint32_t id) {
    for (auto& o : objs)
        if (o->id == id) return o.get();
    return nullptr;
}

void ObjectManager::removeById(uint32_t id) {
    objs.erase(std::remove_if(objs.begin(), objs.end(),
                   [id](const std::unique_ptr<GameObject>& o) { return o->id == id; }),
               objs.end());
}

void ObjectManager::streamProps(const glm::vec3& center, float radius,
                                const std::vector<PropPlacement>& placements,
                                const PropLibrary& lib) {
    const float in2  = radius * radius;
    const float out  = radius + 24.0f;          // hysteresis band
    const float out2 = out * out;

    // Retire props that drifted out of range.
    for (auto& o : objs) {
        if (o->dead || o->kind != ObjectKind::Prop) continue;
        float dx = o->position.x - center.x, dz = o->position.z - center.z;
        if (dx * dx + dz * dz > out2) {
            o->dead = true;
            liveProps.erase(static_cast<Prop*>(o.get())->placementIndex);
        }
    }

    // Spawn placements newly in range, budgeted so a teleport can't hitch.
    int budget = 32;
    for (size_t i = 0; i < placements.size() && budget > 0; i++) {
        uint32_t idx = (uint32_t)i;
        if (liveProps.count(idx)) continue;
        const PropPlacement& pp = placements[i];
        float dx = pp.pos.x - center.x, dz = pp.pos.z - center.z;
        if (dx * dx + dz * dz > in2) continue;
        auto prop = std::make_unique<Prop>(pp.type, pp.pos, pp.yaw, &lib);
        prop->placementIndex = idx;
        objs.push_back(std::move(prop));
        liveProps.insert(idx);
        budget--;
    }
}

void ObjectManager::streamDoors(const glm::vec3& center, float radius,
                                const std::vector<DoorPlacement>& placements,
                                const PropLibrary& lib, const glm::vec3* playerPos) {
    const float in2  = radius * radius;
    const float out  = radius + 24.0f;          // hysteresis band
    const float out2 = out * out;

    for (auto& o : objs) {
        if (o->dead || o->kind != ObjectKind::Door) continue;
        float dx = o->position.x - center.x, dz = o->position.z - center.z;
        if (dx * dx + dz * dz > out2) {
            o->dead = true;
            liveDoors.erase(static_cast<Door*>(o.get())->placementIndex);
        }
    }

    int budget = 24;
    for (size_t i = 0; i < placements.size() && budget > 0; i++) {
        uint32_t idx = (uint32_t)i;
        if (liveDoors.count(idx)) continue;
        const DoorPlacement& dp = placements[i];
        float dx = dp.hinge.x - center.x, dz = dp.hinge.z - center.z;
        if (dx * dx + dz * dz > in2) continue;
        auto door = std::make_unique<Door>(dp.hinge, dp.closedYaw, dp.wallCell,
                                           dp.wallDir, dp.variant, &lib, playerPos);
        door->placementIndex = idx;
        objs.push_back(std::move(door));
        liveDoors.insert(idx);
        budget--;
    }
}
