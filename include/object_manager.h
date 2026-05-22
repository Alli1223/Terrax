#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_set>
#include "game_object.h"

class World;
class PropLibrary;
struct PropPlacement;
struct DoorPlacement;

// Owns every managed GameObject (remote players, props, vehicles). The local
// player is held directly by AppContext and stays outside the manager so its
// hot path is never a container lookup.
class ObjectManager {
public:
    void add(std::unique_ptr<GameObject> obj);
    void clear();

    void updateAll(float dt, World& world);   // advance live objects, sweep dead
    void drawAll(GLuint modelLoc) const;      // a shader must already be bound

    GameObject* findById(uint32_t id);
    void        removeById(uint32_t id);

    // Spawns Prop objects for placements within `radius` of `center` and
    // retires those that drifted out of range. Cheap — only references the
    // already-built shared meshes in `lib`.
    void streamProps(const glm::vec3& center, float radius,
                     const std::vector<PropPlacement>& placements,
                     const PropLibrary& lib);

    // Spawns/retires Door objects like streamProps. `playerPos` is borrowed —
    // each Door reads it every frame to drive its open/close animation.
    void streamDoors(const glm::vec3& center, float radius,
                     const std::vector<DoorPlacement>& placements,
                     const PropLibrary& lib, const glm::vec3* playerPos);

    std::vector<std::unique_ptr<GameObject>>&       objects()       { return objs; }
    const std::vector<std::unique_ptr<GameObject>>& objects() const { return objs; }

private:
    std::vector<std::unique_ptr<GameObject>> objs;
    std::unordered_set<uint32_t>             liveProps;   // prop placement indices spawned
    std::unordered_set<uint32_t>             liveDoors;   // door placement indices spawned
};
