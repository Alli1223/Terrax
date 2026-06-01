#pragma once
#include "voxel_model.h"
#include <string>

// ---------------------------------------------------------------------------
// Item / ClothingItem / WeaponItem — the polymorphic item type hierarchy.
// ---------------------------------------------------------------------------
// Functions that build voxel meshes, paint clothing onto a rig, or generate
// random items live in their own files so this header stays focused on the
// type system. See:
//   - clothing_painter.h — paintClothingOnto()
//   - weapon_builder.h   — buildWeaponVolume(), applyWeaponsToRig()
//   - item_generator.h   — generateRandom*()

// Quick discriminator for an Item without needing RTTI.
enum class ItemKind {
    Clothing,
    Weapon,
};

// Slots on a humanoid where a piece of equipment can go.
enum class EquipSlot {
    None,
    Helmet,
    Shoulders,
    Chest,
    Legs,
    Feet,
    MainHand,
    OffHand,
};

// Material tier of a clothing/armor piece. Drives mesh thickness, default
// palette and base defense value.
enum class ClothingTier {
    Cloth,
    Leather,
    Plate,
};

// Held weapon variant. Picks the mesh and (in later phases) the attack
// animation.
enum class WeaponType {
    None,
    Sword,
    Shield,
    Bow,
    Staff,
    Axe,
};

// Elemental school carried by staves (and any future spell-casting
// weapons). Used by `StaffItem::onPrimaryAttack` to pick which
// Projectile subclass to spawn — fire/ice/arcane each have their own
// visual + behaviour.
enum class WeaponElement {
    None = 0,
    Fire,
    Ice,
    Arcane,
    Holy,     // restorative — drives the healing staff (green/gold magic)
};

// Loot tier. Drives palette pools, name pools, stat scaling, and the extra
// visual flourishes added by the painter (runes, gems, glowing seams for
// Legendary). The generator rolls this with a weighted distribution; the
// rarity also colours the item's name in the UI.
enum class ItemRarity {
    Common,
    Rare,
    Legendary,
};

// Polymorphic root for every physical thing the player can pick up, carry
// or equip. Subclasses (ClothingItem, WeaponItem) provide the voxel mesh
// used for both the in-world drop and the equipped overlay on the rig.
class Item {
public:
    Item(std::string name, ItemKind kind);
    virtual ~Item();

    const std::string& getName() const { return name; }
    ItemKind           getKind() const { return kind; }
    int                getId()   const { return id; }
    EquipSlot          getSlot() const { return slot; }

    void setName(std::string n) { name = std::move(n); }

    // Mesh used to render the item on the rig (or in the world later).
    // Built lazily and cached. Returns nullptr until a subclass provides one.
    VoxelVolume* getVoxelVolume();
    void         invalidateMesh();

    float       attackPower  = 0.0f;
    float       defenseValue = 0.0f;
    ItemRarity  rarity       = ItemRarity::Common;
    // Level requirement. Player must be at least this level to equip.
    // Higher-level items also carry higher stats — see the generator.
    int         level        = 1;
    // Set membership — empty string means a free-roll item. Generator
    // can roll any piece as part of a themed set (Wolfblood, Frostweave,
    // ...); UI surfaces this so the player can spot matching pieces.
    // Named `setKey` rather than `setName` so it doesn't clash with the
    // Item::setName(std::string) setter just above.
    std::string setKey;

protected:
    virtual VoxelVolume* buildVoxelVolume() = 0;

    std::string name;
    ItemKind    kind;
    EquipSlot   slot = EquipSlot::None;
    int         id   = 0;

    VoxelVolume* meshCache = nullptr;

    static int nextId();
};

// Wearable: helmet / shoulders / chest / legs / feet. Tier picks the
// material palette and shape; the two colour fields individualise the
// piece (procedural generation will randomise these).
class ClothingItem : public Item {
public:
    ClothingItem(std::string name, EquipSlot slot, ClothingTier tier);
    ~ClothingItem() override = default;

    ClothingTier getTier() const { return tier; }

    Voxel primaryColor = {180, 180, 180, 255};
    Voxel accentColor  = {120, 120, 120, 255};
    int   patternSeed  = 0;

protected:
    VoxelVolume* buildVoxelVolume() override;

private:
    ClothingTier tier;
};

// Held weapon — sword / shield / bow / staff / axe + future variants.
// Concrete weapons subclass this and override the virtual methods to
// express their unique behaviour (charge mechanic, projectile firing,
// AOE damage, etc). Call sites use the virtual interface — they never
// `switch (getType())` because that would defeat the polymorphism.
// Items are constructed via `createWeaponItem(name, type)` below so the
// right subclass is instantiated for each WeaponType.
//
// `slot` is auto-set from `type` (shield → OffHand, else MainHand).
struct AppContext;  // forward-decl so onPrimaryAttack can take a ref
                    // without dragging app_context.h into items.h.
class NPC;

class WeaponItem : public Item {
public:
    WeaponItem(std::string name, WeaponType type);
    ~WeaponItem() override = default;

    WeaponType getType() const { return type; }

    // --- Polymorphic combat interface ---------------------------------
    // Default behaviour models a generic melee swing. Subclasses can
    // override individually rather than wholesale.

    // True if pressing the attack button starts a charge meter that
    // fires on release (bows). Default = false.
    virtual bool  requiresCharge() const { return false; }
    virtual float maxChargeTime() const  { return 0.0f; }

    // True if the weapon fires a projectile/instant-ranged attack
    // (staves, wands, throwable). Default = false (= melee).
    virtual bool  isInstantRanged() const { return false; }

    // Range + facing cone for target acquisition.
    virtual float attackRange() const    { return 3.8f; }
    virtual float attackFacing() const   { return 0.30f; }

    // Cooldown between attacks in seconds.
    virtual float cooldown() const       { return 0.45f; }

    // Damage multiplier applied on top of the player's base damage
    // (which already scales with item level / rarity).
    virtual float damageMultiplier() const { return 1.0f; }

    // Animation hint — defaults to the right-arm sword swing. Casters
    // (staves) override to use a distinct "raise both arms + glow"
    // pose instead so the action reads as a spell, not a melee strike.
    virtual bool  usesCastAnimation() const { return false; }

    // Elemental school. Only meaningful for staves right now — used to
    // pick which bolt subclass to spawn — but lives on the base so the
    // network packet can carry it for any future spell-caster weapon.
    WeaponElement element = WeaponElement::None;

    // Called when the player triggers this weapon's primary attack with
    // a non-charging weapon, or releases a charged one. `chargeAmount`
    // is 0..1 for chargers, 1.0 for instant attacks. Default = no-op
    // (gameplay.cpp handles the melee swing path). Subclasses that
    // spawn projectiles or apply special effects override this and
    // call into AppContext/ObjectManager directly.
    virtual void onPrimaryAttack(AppContext& ctx, float chargeAmount, NPC* target);

    // True if the weapon does something on the *secondary* (right-mouse)
    // button instead of the default block-place / shield-raise. When true,
    // gameplay routes right-click to onSecondaryAttack and input.cpp skips
    // block placement. Default = false (right-click keeps its old meaning).
    virtual bool hasSecondaryAttack() const { return false; }

    // Called when the player triggers the secondary attack. Default no-op;
    // subclasses (HealingStaffItem) override to drop an AOE, raise a ward,
    // etc. Like onPrimaryAttack, the weapon reaches into AppContext to spawn
    // effects / send packets.
    virtual void onSecondaryAttack(AppContext& ctx) { (void)ctx; }

    Voxel primaryColor = {180, 180, 200, 255};
    Voxel accentColor  = {100, 60, 20, 255};

protected:
    VoxelVolume* buildVoxelVolume() override;
    WeaponType type;   // visible to subclasses for setWeaponPose etc.
};

// Staff — fires Magic Bolt projectiles. Instant ranged (no charge),
// shorter range than a fully drawn bow but no aiming-cone penalty for
// firing while moving.
class StaffItem : public WeaponItem {
public:
    explicit StaffItem(std::string name);

    bool  isInstantRanged() const override   { return true; }
    bool  usesCastAnimation() const override { return true; }
    float attackRange() const override       { return 22.0f; }
    float attackFacing() const override      { return 0.85f; }
    float cooldown() const override          { return 0.55f; }
    float damageMultiplier() const override  { return 1.4f; }

    void onPrimaryAttack(AppContext& ctx, float chargeAmount, NPC* target) override;
};

// Healing staff — a support caster that restores health instead of dealing
// damage. Shares the staff mesh / cast pose (type = Staff, element = Holy)
// but completely different behaviour:
//   * Primary (left)  — Chain Heal: heals the caster, then leaps between
//                       nearby players, mending each in turn.
//   * Secondary (right) — Healing Sanctuary: drops an AOE zone on the ground
//                       that pulses health to any players standing in it.
// `attackPower` is repurposed as the heal power. The heavy lifting (target
// chaining, zone simulation, particles, heal packets) lives in gameplay.cpp
// behind castChainHeal() / castHealZone(); this class just wires the buttons
// to them and enforces cooldowns.
class HealingStaffItem : public WeaponItem {
public:
    explicit HealingStaffItem(std::string name);

    bool  isInstantRanged() const override    { return true; }
    bool  usesCastAnimation() const override  { return true; }
    bool  hasSecondaryAttack() const override { return true; }
    float attackRange() const override        { return 18.0f; }
    float attackFacing() const override       { return 0.95f; }
    float cooldown() const override           { return 1.2f; }
    float damageMultiplier() const override   { return 0.0f; }   // never harms

    void onPrimaryAttack(AppContext& ctx, float chargeAmount, NPC* target) override;
    void onSecondaryAttack(AppContext& ctx) override;
};

// Factory — pick the right concrete subclass for a given WeaponType.
// All code that needs to build a weapon (procedural generator, network
// reconstruction, AppContext seed loadout) routes through this so the
// only place that knows the type→class mapping is items.cpp.
// `element` lets the factory pick a behaviour-specific subclass for a staff
// (Holy → HealingStaffItem); it defaults to None so existing call sites are
// unchanged. The element is also stored on the item for the bolt-colour path.
std::unique_ptr<WeaponItem> createWeaponItem(std::string name, WeaponType type,
                                             WeaponElement element = WeaponElement::None);

// Human-readable labels for the UI.
const char* slotName(EquipSlot s);
const char* tierName(ClothingTier t);
const char* weaponTypeName(WeaponType t);
const char* rarityName(ItemRarity r);
EquipSlot   defaultSlotForWeapon(WeaponType t);

// RGB swatch used to colour item names in the UI — grey/blue/gold for
// common/rare/legendary respectively.
Voxel rarityUiColor(ItemRarity r);

// True when the player meets the item's level requirement.
inline bool canEquipForLevel(const Item* item, int playerLevel) {
    return item && item->level <= playerLevel;
}
