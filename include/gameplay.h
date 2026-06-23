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

// --- Targeting (Track D) ---------------------------------------------------
class NPC;
// Resolve the sticky combat target (ctx.targetNpcId) to a live hostile NPC, or
// nullptr — clearing the id if the target died or streamed out.
NPC* currentTargetNpc(AppContext& ctx);
// Per-frame: consume the cycle-target key (T) to lock the next nearby hostile,
// and auto-drop the target if it dies or gets too far away.
void updateTargeting(AppContext& ctx);

// --- Town vendor (Track G) -------------------------------------------------
class Item;
// A town's deterministic shop stock (lazy + cached per town, rebuilt on seed
// change). Stock scales with the town's danger tier.
int         vendorStockCount(int townIndex);
const Item* vendorStockItem(int townIndex, int i);   // display only (nullptr if oob)
int         vendorStockPrice(int townIndex, int i);  // gold cost to buy
// Buy stock item `i`: if the player can afford it, deduct gold and add a fresh
// copy to the inventory. Returns true on success.
bool        vendorBuy(AppContext& ctx, int townIndex, int i);
// Gold a vendor pays for an inventory item (a fraction of its buy value).
int         itemSellPrice(const Item& it);
// Sell inventory item `i` to the vendor: removes it and credits gold. Returns true.
bool        vendorSell(AppContext& ctx, int inventoryIndex);

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
