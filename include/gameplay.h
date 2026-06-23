#pragma once

struct GLFWwindow;
struct AppContext;

void disconnectFromGame(AppContext& ctx);
void updateGameplay(AppContext& ctx, GLFWwindow* window);

// Bakes the current house design into the world at the live preview location
// by sending a HousePlace packet to the server.
void sendHousePlacement(AppContext& ctx);

// Re-broadcasts the local player's PlayerModel packet so other clients see
// the current loadout. Cheap; safe to call any time equipment changes.
void sendPlayerModelUpdate(AppContext& ctx);

// Turn in a Complete quest (index into ctx.activeQuests): grants XP / gold / a
// rolled item, marks it TurnedIn and drops it from the active list. No-op if the
// index is out of range or the quest isn't Complete.
void turnInQuest(AppContext& ctx, int activeIndex);

// --- Healing staff abilities ----------------------------------------------
// Invoked by HealingStaffItem's primary / secondary attacks. They own the
// heal targeting, particle visuals and heal/effect packets so the item class
// stays a thin button-to-behaviour binding.

// Chain Heal: mend the caster, then leap between nearby players healing each
// for `healPerTarget` HP. Heals the local player directly; relays heals to
// remote players over the network.
void castChainHeal(AppContext& ctx, float healPerTarget);

// Healing Sanctuary: drop an AOE heal zone where the player is aiming (or at
// their feet). The zone pulses `healPerPulse` HP to players inside `radius`
// every second for `duration` seconds.
void castHealZone(AppContext& ctx, float healPerPulse, float radius, float duration);
