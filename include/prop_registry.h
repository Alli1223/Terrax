#pragma once
#include "prop.h"           // PropType
#include "interactable.h"   // InteractAction
#include <glm/glm.hpp>
#include <vector>

class VoxelVolume;

// ---------------------------------------------------------------------------
// Prop registry — the single source of truth for every prop's metadata.
// ---------------------------------------------------------------------------
// Each prop is described by one PropDef row in prop_registry.cpp. The mesh
// builder, how it can be interacted with, whether it emits light and whether it
// sways in the wind all live there, so the per-prop knowledge isn't scattered
// across buildAll(), getInteraction() and the renderer's light/wind passes.
//
// Adding a prop is now just three co-located edits: a new PropType id, a
// builder function, and one row in the registry. Nothing else needs touching —
// this scales cleanly to hundreds or thousands of props.

// How a prop emits light. Base intensity/radius are multiplied by per-light
// flicker (and the night factor when nightOnly) by the renderer.
struct PropLightDef {
    bool      emits     = false;
    float     yOffset   = 0.0f;   // light height above the prop origin
    float     intensity = 0.0f;
    float     radius    = 0.0f;
    glm::vec3 color     = glm::vec3(1.0f);
    bool      nightOnly = false;  // lanterns / lamps only glow after dark
};

// What interacting (E) with the prop does.
struct PropInteractDef {
    InteractAction action  = InteractAction::None;
    float          yOffset = 0.0f;   // seat/mattress height above the origin
    const char*    hint    = "";
};

// One row per prop.
struct PropDef {
    PropType        type;
    const char*     name;          // human-readable, for debug / future UI
    VoxelVolume*  (*build)();       // mesh builder (see prop_builders.h)
    PropInteractDef interact{};
    PropLightDef    light{};
    bool            swaysInWind = false;   // bushes catch the wind
};

// The full table, in PropType order.
const std::vector<PropDef>& propRegistry();

// O(1) lookup by id. Returns nullptr for an unregistered / out-of-range type.
const PropDef* propDef(PropType t);
