#pragma once
#include "items.h"
#include <vector>
#include <memory>
#include <unordered_map>

// Per-player inventory: a fixed-size grid of cells (any can be empty)
// plus a map from EquipSlot to the currently equipped item. Equipping
// never moves the item out of the bag — `equippedSlots` just points into
// it. Removing an item auto-unequips it first. Slot ordering in the grid
// is user-controlled via `swap()` (used by the drag-drop UI).
class Inventory {
public:
    static constexpr int CAPACITY = 24;   // 6 columns × 4 rows in the grid UI

    Inventory();
    ~Inventory();

    Inventory(const Inventory&)            = delete;
    Inventory& operator=(const Inventory&) = delete;

    // Place an item in the first empty grid cell. Returns the inserted
    // raw pointer, or nullptr if the inventory is full.
    Item* addItem(std::unique_ptr<Item> item);
    bool  removeItem(Item* item);

    // Swap the contents of two grid cells. Either or both can be empty.
    // No-op if indices are out of range or equal.
    void  swap(int i, int j);

    // Grid access — returns nullptr if the cell is empty or `index` is
    // out of range.
    Item* at(int index) const;
    int   capacity() const { return CAPACITY; }
    int   indexOf(const Item* item) const;

    // Equip `item` in `slot` (or its natural slot if EquipSlot::None).
    // Returns the previously-equipped item (still owned by inventory),
    // or nullptr if the slot was empty.
    Item* equip(Item* item, EquipSlot slot = EquipSlot::None);
    Item* unequip(EquipSlot slot);
    Item* equipped(EquipSlot slot) const;

    // The full grid as owning slots. Some entries may be null — callers
    // that only care about populated cells should check before use.
    const std::vector<std::unique_ptr<Item>>&    items() const { return bag; }
    const std::unordered_map<int, EquipSlot>&    equippedIds() const { return equippedById; }
    bool isEquipped(const Item* item) const;

private:
    std::vector<std::unique_ptr<Item>>       bag;
    std::unordered_map<EquipSlot, Item*>     equippedSlots;
    std::unordered_map<int, EquipSlot>       equippedById;
};

// Wipe the rig's base body, re-paint face/hair, then layer every equipped
// clothing piece on top. Call this every time equipment changes (and once
// at startup once the inventory has been populated).
void rebuildRigFromInventory(class BipedalRig& rig, const Inventory& inv);

// Forward-declared in network.h. Used here so the network-sync helpers
// don't drag Boost.Asio into every header that pulls inventory.h.
struct EquippedSlotState;

// Pack the inventory's current loadout into a `slots[7]` array suitable for
// sending in a PlayerModelHeader. Order is the SYNCED_SLOT_ORDER below.
void fillSlotsFromInventory(EquippedSlotState* slots, const Inventory& inv);

// Render-only equivalent of rebuildRigFromInventory for remote players.
// Resets the base body, applies face/hair, then layers clothing + weapons
// based on what arrived over the wire — no Inventory object required.
void applyEquipmentToRig(class BipedalRig& rig, const EquippedSlotState* slots);

// The fixed slot order for the 7-element array in PlayerModelHeader.
// Helmet, Shoulders, Chest, Legs, Feet, MainHand, OffHand.
extern const EquipSlot SYNCED_SLOT_ORDER[7];
