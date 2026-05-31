#pragma once
#include "game_object.h"
#include "items.h"
#include <memory>

// A LootDrop is the world-space presence of a single Item dropped by an
// enemy on death. It owns the Item until the player picks it up (E key),
// at which point ownership transfers to the Inventory and the drop is
// flagged dead. While alive it bobs gently and rotates so the player can
// spot it, and falls under gravity until it rests on terrain.
class LootDrop : public GameObject {
public:
    LootDrop();
    ~LootDrop() override;

    // Transfer ownership of the item into the drop. Builds the visual
    // mesh on demand via Item::buildVoxelVolume().
    void setItem(std::unique_ptr<Item> item);
    Item* peekItem() const { return item.get(); }

    // Pop the item out for pickup. Caller takes ownership. The drop will
    // be empty afterward; mark dead so it's culled next frame.
    std::unique_ptr<Item> takeItem();

    void update(float dt, World& world) override;
    void draw(GLuint modelLoc) const override;
    void getAABB(glm::vec3& mn, glm::vec3& mx) const override;

    glm::vec3 velocity   = glm::vec3(0.0f);
    bool      grounded   = false;
    float     lifeTime   = 90.0f;    // seconds until despawn (server-side
                                       // expiry is the real authority, this
                                       // is just a safety net)
    float     age        = 0.0f;
    float     bobPhase   = 0.0f;
    float     spinAngle  = 0.0f;     // degrees, advances each frame
    // Authority key from the server's `LootSpawnPacket.dropId`. Used to
    // pair a local drop with the corresponding LootRemoved packet.
    uint32_t  serverLootId = 0;

private:
    std::unique_ptr<Item> item;
    // Cache of the item's mesh — owned by `item` (via Item::meshCache).
    VoxelVolume* mesh = nullptr;
};
