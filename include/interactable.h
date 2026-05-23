#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// --- Interactable system -----------------------------------------------------
//
// Anything in the world the player can press E on. Designed to grow: every new
// action (sit, sleep, trade, open chest, ...) gets a new `InteractAction`
// value and a handler in gameplay.cpp; the GameObject in question only has to
// override `GameObject::getInteraction()` to publish an offer.
//
// The single-virtual-method shape avoids multiple inheritance and lets a
// GameObject decide its interaction at runtime (a Prop with type=Bed offers
// LieBed; a Prop with type=Chair offers SitChair; future shopkeeper NPCs can
// offer Trade, etc.) without touching the base class for every new role.

enum class InteractAction : uint8_t {
    None = 0,
    SitChair,           // sit upright on a chair, bench, bar-stool
    LieBed,             // lie down on a bed
    // Future: TalkShopkeeper, OpenChest, ReadBook, Drink, ...
    Count
};

// One actionable offer surfaced by a GameObject. The gameplay layer reads it,
// shows a UI hint, and on E-press snaps the player to (anchorPos, anchorYaw)
// and transitions the player to the matching pose.
struct Interaction {
    InteractAction action    = InteractAction::None;
    glm::vec3      anchorPos = glm::vec3(0.0f);  // world XYZ the player snaps to
    float          anchorYaw = 0.0f;              // facing direction in degrees
    const char*    hint      = nullptr;           // short UI text
};

// Player pose state — drives the BipedalRig animation and the camera offset.
// Standing is the default; the others are entered/exited via the interactable
// system. Stays a free enum (not a class) so the rig can use it cheaply.
enum class PlayerPose : uint8_t {
    Standing = 0,
    Sitting,
    Lying,
};
