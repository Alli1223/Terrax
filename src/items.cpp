#include "items.h"
#include "weapon_builder.h"
#include "projectile.h"
#include "app_context.h"
#include "network.h"
#include "npc.h"
#include "gameplay.h"   // castChainHeal / castHealZone (HealingStaffItem)

// ---------------------------------------------------------------------------
// Item type implementations — constructors, identity, and label helpers.
// Voxel painters, weapon meshes, and procedural generators live in their
// own files (clothing_painter.cpp, weapon_builder.cpp, item_generator.cpp).
// ---------------------------------------------------------------------------

static int g_nextItemId = 1;

int Item::nextId() { return g_nextItemId++; }

Item::Item(std::string n, ItemKind k)
    : name(std::move(n)), kind(k), id(nextId()) {}

Item::~Item() {
    delete meshCache;
}

VoxelVolume* Item::getVoxelVolume() {
    if (!meshCache) {
        meshCache = buildVoxelVolume();
        if (meshCache) meshCache->updateMesh();
    }
    return meshCache;
}

void Item::invalidateMesh() {
    delete meshCache;
    meshCache = nullptr;
}

// --- ClothingItem -----------------------------------------------------------

ClothingItem::ClothingItem(std::string n, EquipSlot s, ClothingTier t)
    : Item(std::move(n), ItemKind::Clothing), tier(t) {
    slot = s;
    switch (tier) {
        case ClothingTier::Cloth:   defenseValue =  2.0f; break;
        case ClothingTier::Leather: defenseValue =  5.0f; break;
        case ClothingTier::Plate:   defenseValue = 10.0f; break;
    }
    // Default palette per tier (procedural generation overrides these).
    switch (tier) {
        case ClothingTier::Cloth:
            primaryColor = { 90, 110, 160, 255};   // muted blue cloth
            accentColor  = { 60,  80, 120, 255};
            break;
        case ClothingTier::Leather:
            primaryColor = {110,  70,  40, 255};   // tan leather
            accentColor  = { 70,  40,  20, 255};
            break;
        case ClothingTier::Plate:
            primaryColor = {180, 185, 200, 255};   // steel
            accentColor  = {120, 125, 140, 255};
            break;
    }
}

VoxelVolume* ClothingItem::buildVoxelVolume() {
    // Equipped clothing is painted directly onto the player rig
    // (clothing_painter.cpp). This standalone mesh is for the
    // *world-drop* visual — a small "folded garment" cuboid that the
    // player can spot on the ground, coloured by the item's palette.
    // Sized at 6x4x6 atlas-pixels so it scales down to a tidy ~30 cm
    // bundle at LootDrop's draw-scale (~0.05).
    VoxelVolume* v = new VoxelVolume(6, 4, 6);
    for (int x = 0; x < 6; x++)
        for (int y = 0; y < 4; y++)
            for (int z = 0; z < 6; z++) {
                bool corner = (x == 0 || x == 5) && (z == 0 || z == 5);
                bool topRow = (y == 3);
                Voxel c = (corner || topRow) ? accentColor : primaryColor;
                v->setVoxel(x, y, z, c);
            }
    return v;
}

// --- ConsumableItem ---------------------------------------------------------

ConsumableItem::ConsumableItem(std::string n, ConsumableKind k)
    : Item(std::move(n), ItemKind::Consumable), consumable(k) {
    slot = EquipSlot::None;   // never equipped
    switch (k) {
        case ConsumableKind::HealthPotion:
            restoreHealthPct   = 0.5f;  liquidColor = {210,  55,  55, 255}; break;  // red
        case ConsumableKind::ManaPotion:
            restoreResourcePct = 0.6f;  liquidColor = { 70, 110, 220, 255}; break;  // blue
    }
}

VoxelVolume* ConsumableItem::buildVoxelVolume() {
    // A small glass potion bottle: a liquid-filled rounded body, a narrow
    // glass neck and a cork stopper. Coloured by the liquid (red / blue).
    VoxelVolume* v = new VoxelVolume(6, 9, 6);
    const Voxel glass = {185, 205, 210, 255};
    const Voxel cork  = {120,  85,  50, 255};
    for (int x = 1; x <= 4; x++)            // body (liquid behind a glass rim)
        for (int z = 1; z <= 4; z++)
            for (int y = 0; y <= 4; y++) {
                bool rim = (x == 1 || x == 4) && (z == 1 || z == 4);
                v->setVoxel(x, y, z, rim ? glass : liquidColor);
            }
    for (int x = 2; x <= 3; x++)            // neck
        for (int z = 2; z <= 3; z++)
            for (int y = 5; y <= 6; y++)
                v->setVoxel(x, y, z, glass);
    for (int x = 2; x <= 3; x++)            // cork
        for (int z = 2; z <= 3; z++)
            for (int y = 7; y <= 8; y++)
                v->setVoxel(x, y, z, cork);
    return v;
}

std::unique_ptr<ConsumableItem> makeConsumable(ConsumableKind kind) {
    std::string nm = (kind == ConsumableKind::HealthPotion) ? "Health Potion"
                                                            : "Mana Potion";
    auto p = std::make_unique<ConsumableItem>(std::move(nm), kind);
    p->level  = 1;
    p->rarity = ItemRarity::Common;
    return p;
}

// --- WeaponItem -------------------------------------------------------------

WeaponItem::WeaponItem(std::string n, WeaponType t)
    : Item(std::move(n), ItemKind::Weapon), type(t) {
    slot = defaultSlotForWeapon(t);
    switch (type) {
        case WeaponType::Sword:  attackPower  = 10.0f; break;
        case WeaponType::Axe:    attackPower  = 14.0f; break;
        case WeaponType::Bow:    attackPower  =  8.0f; break;
        case WeaponType::Staff:  attackPower  =  6.0f; break;
        case WeaponType::Shield: defenseValue =  6.0f; break;
        case WeaponType::Dagger: attackPower  =  8.0f; break;   // light + fast
        case WeaponType::Mace:   attackPower  = 13.0f; break;   // heavy bludgeon
        case WeaponType::Spear:  attackPower  = 11.0f; break;   // reach
        case WeaponType::Greatsword: attackPower = 17.0f; break; // heavy two-hander
        case WeaponType::Warhammer:  attackPower = 16.0f; break; // crushing two-hander
        default: break;
    }
}

VoxelVolume* WeaponItem::buildVoxelVolume() {
    return buildWeaponVolume(type, rarity, primaryColor, accentColor);
}

// --- WeaponItem default polymorphic methods -------------------------------
// Default `onPrimaryAttack` models a generic melee swing: target the
// nearest NPC in front of the player, send a PlayerAttackPacket so the
// server resolves damage. Subclasses (StaffItem etc.) override to spawn
// projectiles or apply special effects.
void WeaponItem::onPrimaryAttack(AppContext& ctx, float chargeAmount, NPC* target) {
    if (!ctx.client) return;
    PlayerAttackPacket ap {};
    ap.clientID    = ctx.client->clientID;
    ap.targetNpcId = target ? target->id : 0u;
    ap.damageScale = damageMultiplier() * chargeAmount;
    ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));
}

// --- StaffItem ------------------------------------------------------------
// A staff is a polymorphic example: same item slot + visual rig position
// as the other weapons, but a completely different `onPrimaryAttack`. The
// player gets a fast, gravity-free magic bolt instead of a swing, no
// charge required. Bolts inherit ownerClientId so future server-side
// projectile authority can attribute them.

StaffItem::StaffItem(std::string n) : WeaponItem(std::move(n), WeaponType::Staff) {
    // Staves are tuned weaker than a charged bow, stronger than a basic
    // swing — fast cooldown makes them attractive for sustained DPS.
    attackPower = 9.0f;
}

void StaffItem::onPrimaryAttack(AppContext& ctx, float chargeAmount, NPC* target) {
    if (!ctx.client) return;

    // Pick the bolt subclass that matches this staff's element. The
    // polymorphism lives in the Projectile hierarchy — each subclass
    // sets its own colours / speed / on-hit behaviour, the call site
    // just needs to instantiate the right one.
    std::unique_ptr<MagicBoltProjectile> bolt;
    float speed = 32.0f;
    switch (element) {
        case WeaponElement::Fire:
            bolt = std::make_unique<FireBoltProjectile>();
            speed = 26.0f;          // heavier, hangs in the air longer
            break;
        case WeaponElement::Ice:
            bolt = std::make_unique<IceBoltProjectile>();
            speed = 42.0f;
            break;
        case WeaponElement::Arcane:
            bolt = std::make_unique<ArcaneBoltProjectile>();
            speed = 52.0f;
            break;
        case WeaponElement::None:
        default:
            bolt = std::make_unique<MagicBoltProjectile>();
            break;
    }

    bolt->position = ctx.camera.position
                   + glm::vec3(0.0f, 1.5f, 0.0f)
                   + ctx.camera.front * 0.6f;
    bolt->velocity      = ctx.camera.front * speed;
    bolt->restingDir    = glm::normalize(ctx.camera.front);
    bolt->ownerClientId = ctx.client->clientID;
    bolt->damageScale   = damageMultiplier() * chargeAmount;
    ctx.objectManager.add(std::move(bolt));

    // Damage is still server-authoritative — the projectile is the
    // visual, the packet is the rule.
    PlayerAttackPacket ap {};
    ap.clientID    = ctx.client->clientID;
    ap.targetNpcId = target ? target->id : 0u;
    ap.damageScale = damageMultiplier() * chargeAmount;
    ctx.client->send(PacketType::PlayerAttack, &ap, sizeof(ap));
}

// --- HealingStaffItem -----------------------------------------------------
// Support caster. Both buttons defer to gameplay-side helpers that own the
// heal targeting + particle work; here we set the heal power, tint the staff
// green/gold, and gate each ability behind its own cooldown timer stored on
// AppContext (decremented every frame in updateGameplay).

HealingStaffItem::HealingStaffItem(std::string n)
    : WeaponItem(std::move(n), WeaponType::Staff) {
    element      = WeaponElement::Holy;
    attackPower  = 24.0f;   // repurposed as heal-per-target / chain power
    // Verdant green shaft with a warm gold crystal — reads as "restorative"
    // at a glance next to the offensive elemental staves.
    primaryColor = { 70, 190, 110, 255};
    accentColor  = {235, 222, 150, 255};
}

void HealingStaffItem::onPrimaryAttack(AppContext& ctx, float /*charge*/, NPC* /*target*/) {
    // Still on cooldown — cancel the cast pose gameplay just started so the
    // arms don't fizzle, and wait. gameplay re-polls the button next frame.
    if (ctx.healCdPrimary > 0.0f) {
        if (ctx.playerRig) { ctx.playerRig->isCasting = false; ctx.playerRig->castAnim = 0.0f; }
        return;
    }
    ctx.healCdPrimary = cooldown();
    castChainHeal(ctx, attackPower);
}

void HealingStaffItem::onSecondaryAttack(AppContext& ctx) {
    if (ctx.healCdSecondary > 0.0f) return;
    ctx.healCdSecondary = 8.0f;
    // Per-pulse heal is half the chain value; the zone makes up for the
    // smaller tick with sustained healing across its lifetime.
    castHealZone(ctx, attackPower * 0.5f, 4.5f, 6.0f);
}

// --- Factory --------------------------------------------------------------
// All weapon construction routes through here so the only place that
// knows the WeaponType → concrete-class mapping is this file.

std::unique_ptr<WeaponItem> createWeaponItem(std::string name, WeaponType type,
                                             WeaponElement element) {
    std::unique_ptr<WeaponItem> w;
    switch (type) {
        case WeaponType::Staff:
            // A Holy staff mends instead of harming — a distinct concrete
            // class so it round-trips through drop/pickup (loot rebuilds
            // items via this factory, keyed on type + element).
            if (element == WeaponElement::Holy)
                w = std::make_unique<HealingStaffItem>(std::move(name));
            else
                w = std::make_unique<StaffItem>(std::move(name));
            break;
        // Other types currently use the default WeaponItem (melee or
        // bow-style behaviour driven by gameplay.cpp). Adding a subclass
        // for any of them is a drop-in change — see CLAUDE.md's
        // "Polymorphic hierarchies" section.
        case WeaponType::Sword:
        case WeaponType::Shield:
        case WeaponType::Bow:
        case WeaponType::Axe:
        case WeaponType::Hoe:
        case WeaponType::Scythe:
            w = std::make_unique<WeaponItem>(std::move(name), type);
            break;
        default:
            return nullptr;
    }
    if (w) w->element = element;
    return w;
}

// --- Labels -----------------------------------------------------------------

const char* slotName(EquipSlot s) {
    switch (s) {
        case EquipSlot::Helmet:    return "Helmet";
        case EquipSlot::Shoulders: return "Shoulders";
        case EquipSlot::Chest:     return "Chest";
        case EquipSlot::Legs:      return "Legs";
        case EquipSlot::Feet:      return "Feet";
        case EquipSlot::MainHand:  return "Main Hand";
        case EquipSlot::OffHand:   return "Off Hand";
        default:                   return "(none)";
    }
}

const char* tierName(ClothingTier t) {
    switch (t) {
        case ClothingTier::Cloth:   return "Cloth";
        case ClothingTier::Leather: return "Leather";
        case ClothingTier::Plate:   return "Plate";
    }
    return "";
}

const char* weaponTypeName(WeaponType t) {
    switch (t) {
        case WeaponType::Sword:  return "Sword";
        case WeaponType::Shield: return "Shield";
        case WeaponType::Bow:    return "Bow";
        case WeaponType::Staff:  return "Staff";
        case WeaponType::Axe:    return "Axe";
        case WeaponType::Hoe:    return "Hoe";
        case WeaponType::Scythe: return "Scythe";
        case WeaponType::Dagger: return "Dagger";
        case WeaponType::Mace:   return "Mace";
        case WeaponType::Spear:  return "Spear";
        case WeaponType::Greatsword: return "Greatsword";
        case WeaponType::Warhammer:  return "Warhammer";
        default:                 return "";
    }
}

EquipSlot defaultSlotForWeapon(WeaponType t) {
    return t == WeaponType::Shield ? EquipSlot::OffHand : EquipSlot::MainHand;
}

const char* rarityName(ItemRarity r) {
    switch (r) {
        case ItemRarity::Common:    return "Common";
        case ItemRarity::Rare:      return "Rare";
        case ItemRarity::Legendary: return "Legendary";
    }
    return "";
}

Voxel rarityUiColor(ItemRarity r) {
    switch (r) {
        case ItemRarity::Common:    return {200, 200, 200, 255};   // light grey
        case ItemRarity::Rare:      return { 90, 160, 255, 255};   // sapphire blue
        case ItemRarity::Legendary: return {255, 185,  55, 255};   // gold
    }
    return {255, 255, 255, 255};
}
