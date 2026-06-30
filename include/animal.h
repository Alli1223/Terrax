#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <random>
#include <glm/glm.hpp>
#include "game_object.h"

class QuadrupedRig;
class World;

// The wildlife species. Sheep and Rabbit ship in phase 1; the rest follow.
enum class AnimalSpecies : uint8_t {
    Sheep = 0, Cow, Rabbit, Squirrel, Deer, Fox, Pig, Chicken, Goat
};

// A wandering wild animal. Server-authoritative, like NPC and Ferry: the server
// owns motion / AI and broadcasts AnimalState packets; each client builds the
// species model, interpolates and renders it.
class Animal : public GameObject {
public:
    Animal();
    ~Animal() override;

    void update(float dt, World& world) override;   // client interpolation
    void draw(GLuint modelLoc) const override;
    void getAABB(glm::vec3& mn, glm::vec3& mx) const override;

    // Builds the owned species rig. Client-only — needs a GL context, so call
    // it on the main thread once species / variant are set.
    void initClientVisual();

    AnimalSpecies species = AnimalSpecies::Sheep;
    uint32_t      variant = 0;          // per-individual size / colour variation
    bool          walking = false;

    // Client-side interpolation targets, fed from AnimalState packets.
    glm::vec3 targetPos{0.0f};
    float     targetYaw = 0.0f;
    glm::vec3 velocity{0.0f};
    double    lastUpdate = 0.0;         // client: last packet, for stream timeout

    // Server-side wander AI.
    glm::vec2 wanderTarget{0.0f};
    bool      hasTarget = false;
    float     idleTimer = 0.0f;
    float     groundY   = 65.0f;
    float     fleeTimer = 0.0f;         // skittish species: panic countdown
    glm::vec2 fleeFrom{0.0f};           // the point a skittish animal flees from

private:
    QuadrupedRig* rig   = nullptr;      // client-only, owned
    float         scale = 0.05f;        // world-space draw scale
};

// Server-side: spawns wildlife around players, streams it in/out by distance,
// and runs the wander AI each tick.
class AnimalDirector {
public:
    void update(float dt, const std::vector<glm::vec3>& players, World& world);
    const std::vector<std::unique_ptr<Animal>>& animals() const { return active; }

private:
    void stepAnimal(Animal& a, float dt, World& world,
                    const std::vector<glm::vec3>& players);

    std::vector<std::unique_ptr<Animal>> active;
    uint32_t     nextId = 0xC0000000u;   // id range disjoint from players/NPCs
    std::mt19937 rng{0x4E494D41u};       // "NIMA"
};

// Builds the detailed voxel model for a species onto a fresh QuadrupedRig and
// returns the world-space draw scale. `variant` jitters size and colouring.
float buildAnimalRig(QuadrupedRig& rig, AnimalSpecies species, uint32_t variant);

// Walk speed (blocks/sec) for a species — rabbits scurry, sheep amble.
float animalSpeed(AnimalSpecies species);
