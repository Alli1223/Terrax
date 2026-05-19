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

    noclip = true;
    camera.pitch = -20.0f;
    camera.updateVectors();
}

AppContext::~AppContext() {
    if (mapBuilding && mapFuture.valid()) mapFuture.wait();
    delete playerRig;
}
