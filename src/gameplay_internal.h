#pragma once
// Shared internals for the split gameplay*.cpp translation units. updateGameplay
// (gameplay.cpp) drives the per-frame loop; the heavy lifting lives in
// gameplay_entities.cpp (network entity sync + interaction + combat),
// gameplay_effects.cpp (weather + ambient particles) and gameplay_spells.cpp
// (the healing staff). gameplay.h stays the public surface; this header is
// private to the gameplay_* sources and only lists the cross-file entry points
// updateGameplay reaches into.

struct AppContext;
class  NPC;

// --- gameplay_entities.cpp: network entity sync ----------------------------
void cleanupRemotePlayers(AppContext& ctx);
void syncRemotePlayerObjects(AppContext& ctx);
void syncFerryObjects(AppContext& ctx);
void syncNPCObjects(AppContext& ctx);
void syncAnimalObjects(AppContext& ctx);
void syncLootDrops(AppContext& ctx);

// --- gameplay_entities.cpp: interaction + combat ---------------------------
void updateLootPickup(AppContext& ctx);
void updatePropInteraction(AppContext& ctx);
void updateNpcInteraction(AppContext& ctx);
NPC* findTargetNpc(AppContext& ctx, float maxRange, float minFacing);
NPC* findMeleeTargetNpc(AppContext& ctx);
NPC* findRangedTargetNpc(AppContext& ctx);
void updatePlayerVitals(AppContext& ctx);
void awardEnemyKill(AppContext& ctx, const NPC* npc);

// --- gameplay_effects.cpp: weather + ambient particles ---------------------
void updateLeafParticles(AppContext& ctx);
void updateWeather(AppContext& ctx);
void updateWeatherParticles(AppContext& ctx);
void updateAmbientParticles(AppContext& ctx);
void updateAudio(AppContext& ctx);
void updateHousePreview(AppContext& ctx);

// --- gameplay_spells.cpp: healing staff ------------------------------------
void drainSpellEvents(AppContext& ctx);
void updateHealZones(AppContext& ctx);
