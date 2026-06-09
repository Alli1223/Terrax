#pragma once
#include <glm/glm.hpp>
#include <cstdint>
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
void syncEnemyProjectiles(AppContext& ctx);   // spawn client visuals for enemy bolts

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

// --- gameplay_spells.cpp: healing staff + ability cosmetics ----------------
void drainSpellEvents(AppContext& ctx);
void updateHealZones(AppContext& ctx);
// Coloured voxel-particle burst for an ability cast. `kind` matches
// SpellEffectPacket.kind (2=aoe slash, 3=taunt ring, 4=cast flash, 5=buff aura).
// Called by ability.cpp for the caster's own copy and by drainSpellEvents for
// spectators, so both stay in visual sync.
void spawnAbilityFx(AppContext& ctx, const glm::vec3& center, float radius, uint8_t kind);
void tryActivateHotbar(AppContext& ctx, int slot);
