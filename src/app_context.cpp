#include "app_context.h"
#include "world.h"
#include <random>
#include <cstdio>

AppContext::AppContext()
    : camera(glm::vec3(8.5f, 42.0f, 8.5f))
    , world(false)
{
    world.renderDistance = settings.renderDistance;
    {
        std::mt19937 rng(std::random_device{}());
        setWorldSeed(rng());
    }

    {
        static const char* firstNames[] = {
            "Alder","Bryn","Cael","Dara","Edwyn","Faye","Gorn","Hana",
            "Idris","Juno","Kael","Lira","Morn","Nyla","Oswin","Pira",
            "Quen","Riva","Soren","Tara","Ulan","Vera","Wren","Xara",
            "Yael","Zora","Bael","Cira","Dwyn","Elva",
        };
        static const char* lastNames[] = {
            "Ashvale","Blackwood","Crestfall","Dawnmere","Emberveil",
            "Frostholm","Greyveil","Harrow","Ironfeld","Jademoor",
            "Kindrel","Lochfall","Mistwood","Nighthollow","Oakhaven",
            "Pinecroft","Quickfen","Ravenmoor","Stoneholt","Thornwick",
            "Umbravel","Voidmarch","Westmere","Xandrel","Yarrowfen","Zephyrholt",
        };
        std::mt19937 rng(std::random_device{}());
        const char* fn = firstNames[std::uniform_int_distribution<int>(0, 29)(rng)];
        const char* ln = lastNames[std::uniform_int_distribution<int>(0, 25)(rng)];
        snprintf(playerName, sizeof(playerName), "%s %s", fn, ln);
    }

    playerRig = new BipedalRig();
    playerRig->setupDefaultHuman(true);
    playerRig->randomizeAppearance();
    // The player always carries the lantern from their belt — F still
    // raises it overhead for the brighter "held" radius (see input.cpp
    // and renderer.cpp's lanternWorldPos).
    playerRig->hasLantern = true;

    localPlayer = new Player(&camera, playerRig);

    // Seed a starter cloth loadout and equip it so the player begins dressed.
    Item* startCap  = inventory.addItem(std::make_unique<ClothingItem>(
        "Cloth Cap",       EquipSlot::Helmet,    ClothingTier::Cloth));
    Item* startMant = inventory.addItem(std::make_unique<ClothingItem>(
        "Cloth Mantle",    EquipSlot::Shoulders, ClothingTier::Cloth));
    Item* startTun  = inventory.addItem(std::make_unique<ClothingItem>(
        "Cloth Tunic",     EquipSlot::Chest,     ClothingTier::Cloth));
    Item* startTrs  = inventory.addItem(std::make_unique<ClothingItem>(
        "Cloth Trousers",  EquipSlot::Legs,      ClothingTier::Cloth));
    Item* startShoe = inventory.addItem(std::make_unique<ClothingItem>(
        "Cloth Shoes",     EquipSlot::Feet,      ClothingTier::Cloth));
    Item* startSword = inventory.addItem(
        createWeaponItem("Iron Sword",       WeaponType::Sword));
    inventory.addItem(createWeaponItem("Wooden Shield",   WeaponType::Shield));
    inventory.addItem(createWeaponItem("Hunter's Bow",    WeaponType::Bow));
    inventory.addItem(createWeaponItem("Apprentice Staff",WeaponType::Staff));
    inventory.addItem(createWeaponItem("Hand Axe",        WeaponType::Axe));

    // Extra leather + plate pieces so the higher tiers can be tested from
    // the inventory screen straight away. The character starts in cloth.
    inventory.addItem(std::make_unique<ClothingItem>(
        "Leather Cap",     EquipSlot::Helmet,    ClothingTier::Leather));
    inventory.addItem(std::make_unique<ClothingItem>(
        "Leather Pauldrons", EquipSlot::Shoulders, ClothingTier::Leather));
    inventory.addItem(std::make_unique<ClothingItem>(
        "Leather Vest",    EquipSlot::Chest,     ClothingTier::Leather));
    inventory.addItem(std::make_unique<ClothingItem>(
        "Leather Breeches", EquipSlot::Legs,     ClothingTier::Leather));
    inventory.addItem(std::make_unique<ClothingItem>(
        "Leather Boots",   EquipSlot::Feet,      ClothingTier::Leather));

    inventory.addItem(std::make_unique<ClothingItem>(
        "Plate Helm",      EquipSlot::Helmet,    ClothingTier::Plate));
    inventory.addItem(std::make_unique<ClothingItem>(
        "Steel Pauldrons", EquipSlot::Shoulders, ClothingTier::Plate));
    inventory.addItem(std::make_unique<ClothingItem>(
        "Steel Cuirass",   EquipSlot::Chest,     ClothingTier::Plate));
    inventory.addItem(std::make_unique<ClothingItem>(
        "Steel Greaves",   EquipSlot::Legs,      ClothingTier::Plate));
    inventory.addItem(std::make_unique<ClothingItem>(
        "Steel Sabatons",  EquipSlot::Feet,      ClothingTier::Plate));

    inventory.equip(startCap);
    inventory.equip(startMant);
    inventory.equip(startTun);
    inventory.equip(startTrs);
    inventory.equip(startShoe);
    inventory.equip(startSword);

    rebuildRigFromInventory(*playerRig, inventory);

    houseModel = new HouseModel();

    noclip = true;
    camera.pitch = -20.0f;
    camera.updateVectors();
}

AppContext::~AppContext() {
    if (mapBuilding && mapFuture.valid()) mapFuture.wait();
    objectManager.clear();
    delete localPlayer;
    delete playerRig;
    delete houseModel;
}
