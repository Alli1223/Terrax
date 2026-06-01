#include "inventory.h"
#include "clothing_painter.h"
#include "weapon_builder.h"
#include "network.h"
#include "voxel_model.h"

Inventory::Inventory() {
    bag.resize(CAPACITY);   // all cells start empty (null unique_ptr)
}

Inventory::~Inventory() = default;

Item* Inventory::addItem(std::unique_ptr<Item> item) {
    if (!item) return nullptr;
    for (auto& cell : bag) {
        if (!cell) {
            Item* raw = item.get();
            cell = std::move(item);
            return raw;
        }
    }
    return nullptr;   // inventory full
}

bool Inventory::removeItem(Item* item) {
    if (!item) return false;
    // Drop any equip pointer first so we never leave a dangling slot ref.
    auto rev = equippedById.find(item->getId());
    if (rev != equippedById.end()) {
        equippedSlots.erase(rev->second);
        equippedById.erase(rev);
    }
    for (auto& cell : bag) {
        if (cell.get() == item) { cell.reset(); return true; }
    }
    return false;
}

void Inventory::swap(int i, int j) {
    if (i == j) return;
    if (i < 0 || j < 0 || i >= (int)bag.size() || j >= (int)bag.size()) return;
    std::swap(bag[i], bag[j]);
}

Item* Inventory::at(int index) const {
    if (index < 0 || index >= (int)bag.size()) return nullptr;
    return bag[index].get();
}

int Inventory::indexOf(const Item* item) const {
    if (!item) return -1;
    for (int i = 0; i < (int)bag.size(); i++)
        if (bag[i].get() == item) return i;
    return -1;
}

Item* Inventory::equip(Item* item, EquipSlot slotOverride) {
    if (!item) return nullptr;
    EquipSlot s = (slotOverride != EquipSlot::None) ? slotOverride : item->getSlot();
    if (s == EquipSlot::None) return nullptr;

    // If the item is already equipped somewhere else, free that slot.
    auto rev = equippedById.find(item->getId());
    if (rev != equippedById.end() && rev->second != s) {
        equippedSlots.erase(rev->second);
        equippedById.erase(rev);
    }

    Item* old = nullptr;
    auto it = equippedSlots.find(s);
    if (it != equippedSlots.end()) {
        old = it->second;
        if (old) equippedById.erase(old->getId());
    }
    equippedSlots[s] = item;
    equippedById[item->getId()] = s;
    return old;
}

Item* Inventory::unequip(EquipSlot slot) {
    auto it = equippedSlots.find(slot);
    if (it == equippedSlots.end()) return nullptr;
    Item* old = it->second;
    equippedSlots.erase(it);
    if (old) equippedById.erase(old->getId());
    return old;
}

Item* Inventory::equipped(EquipSlot slot) const {
    auto it = equippedSlots.find(slot);
    return (it == equippedSlots.end()) ? nullptr : it->second;
}

bool Inventory::isEquipped(const Item* item) const {
    if (!item) return false;
    return equippedById.find(item->getId()) != equippedById.end();
}

const EquipSlot SYNCED_SLOT_ORDER[7] = {
    EquipSlot::Helmet, EquipSlot::Shoulders, EquipSlot::Chest,
    EquipSlot::Legs,   EquipSlot::Feet,
    EquipSlot::MainHand, EquipSlot::OffHand,
};

void fillSlotsFromInventory(EquippedSlotState* slots, const Inventory& inv) {
    for (int i = 0; i < 7; i++) {
        EquippedSlotState& s = slots[i];
        s.kind = 0;
        s.variant = 0;
        s.rarity = 0;
        s.primary = {0, 0, 0, 0};
        s.accent  = {0, 0, 0, 0};
        Item* it = inv.equipped(SYNCED_SLOT_ORDER[i]);
        if (!it) continue;
        s.rarity = (uint8_t)it->rarity;
        if (it->getKind() == ItemKind::Clothing) {
            const ClothingItem* c = static_cast<const ClothingItem*>(it);
            s.kind    = 1;
            s.variant = (uint8_t)c->getTier();
            s.primary = c->primaryColor;
            s.accent  = c->accentColor;
        } else if (it->getKind() == ItemKind::Weapon) {
            const WeaponItem* w = static_cast<const WeaponItem*>(it);
            s.kind    = 2;
            s.variant = (uint8_t)w->getType();
            s.primary = w->primaryColor;
            s.accent  = w->accentColor;
        }
    }
}

void applyEquipmentToRig(BipedalRig& rig, const EquippedSlotState* slots) {
    rig.resetBaseBody();
    rig.applyCustomization();
    // Clothing first (indices 0..4 — Helmet through Feet).
    for (int i = 0; i < 5; i++) {
        const EquippedSlotState& s = slots[i];
        if (s.kind != 1) continue;
        ClothingTier tier = (ClothingTier)s.variant;
        ItemRarity   rar  = (ItemRarity)s.rarity;
        paintClothingOnto(rig, tier, rar, SYNCED_SLOT_ORDER[i], s.primary, s.accent);
    }
    // Weapons: main hand at slot[5], off hand at slot[6].
    const EquippedSlotState& mh = slots[5];
    const EquippedSlotState& oh = slots[6];
    WeaponType mt = (mh.kind == 2) ? (WeaponType)mh.variant : WeaponType::None;
    WeaponType ot = (oh.kind == 2) ? (WeaponType)oh.variant : WeaponType::None;
    ItemRarity mr = (ItemRarity)mh.rarity;
    ItemRarity orr = (ItemRarity)oh.rarity;
    applyWeaponsToRig(rig, mt, mr, mh.primary, mh.accent,
                          ot, orr, oh.primary, oh.accent);
}

void rebuildRigFromInventory(BipedalRig& rig, const Inventory& inv) {
    rig.resetBaseBody();
    rig.applyCustomization();
    // Iterate in a stable order so layering is deterministic regardless of
    // which order the player equipped things.
    static const EquipSlot order[] = {
        EquipSlot::Helmet, EquipSlot::Shoulders, EquipSlot::Chest,
        EquipSlot::Legs,   EquipSlot::Feet,
    };
    for (EquipSlot s : order) {
        Item* it = inv.equipped(s);
        if (!it || it->getKind() != ItemKind::Clothing) continue;
        paintClothingOnto(rig, *static_cast<const ClothingItem*>(it));
    }
    // Weapons: main hand goes to the right, off-hand (shield) to the left.
    Item* main = inv.equipped(EquipSlot::MainHand);
    Item* off  = inv.equipped(EquipSlot::OffHand);
    const WeaponItem* mw = (main && main->getKind() == ItemKind::Weapon)
        ? static_cast<const WeaponItem*>(main) : nullptr;
    const WeaponItem* ow = (off && off->getKind() == ItemKind::Weapon)
        ? static_cast<const WeaponItem*>(off) : nullptr;
    applyWeaponsToRig(rig, mw, ow);
}
