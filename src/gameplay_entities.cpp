// Network entity sync (remote players, ferries, NPCs, animals, loot drops) plus
// the player-facing interaction and combat helpers (prop/NPC interaction, target
// selection, loot pickup, vitals, kill rewards). Split out of gameplay.cpp;
// updateGameplay calls these each frame. Entry points: gameplay_internal.h.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gameplay.h"
#include "app_context.h"
#include "physics.h"
#include "network.h"
#include "game_session.h"
#include "town.h"
#include "prop_placement.h"
#include "vehicle.h"
#include "npc.h"
#include "animal.h"
#include "item_generator.h"
#include "loot_drop.h"
#include "projectile.h"
#include "voxel_model.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <cmath>
#include <cstring>
#include <mutex>
#include <random>
#include <memory>
#include "gameplay_internal.h"

void cleanupRemotePlayers(AppContext& ctx) {
    for (auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        if (p.rig) delete p.rig;
    }
    ctx.remotePlayers.clear();
}

// Keeps the ObjectManager's remote-player objects in step with the
// remotePlayers map: drop objects whose player has gone, add objects for
// newly-seen players. Must run before objectManager.updateAll so a Player
// never dereferences a freed RemotePlayer record.
void syncRemotePlayerObjects(AppContext& ctx) {
    std::vector<uint32_t> gone;
    for (auto& o : ctx.objectManager.objects())
        if (o->kind == ObjectKind::Player && o->id != 0 &&
            ctx.remotePlayers.find(o->id) == ctx.remotePlayers.end())
            gone.push_back(o->id);
    for (uint32_t id : gone) ctx.objectManager.removeById(id);

    for (auto& [id, rp] : ctx.remotePlayers)
        if (!ctx.objectManager.findById(id))
            ctx.objectManager.add(std::make_unique<Player>(&rp, id));
}

// Creates/updates client-side Ferry objects from the server's EntityState
// broadcasts. The server owns ferry motion; clients only interpolate + render.
void syncFerryObjects(AppContext& ctx) {
    if (!ctx.client) return;
    for (const EntityStatePacket& ep : ctx.client->entityUpdates) {
        GameObject* o = ctx.objectManager.findById(ep.entityId);
        Ferry* f = nullptr;
        if (!o) {
            auto nf = std::make_unique<Ferry>();
            nf->id       = ep.entityId;
            nf->mesh     = getFerryMesh();
            nf->position = glm::vec3(ep.x, ep.y, ep.z);
            nf->yaw      = ep.yaw;
            f = nf.get();
            ctx.objectManager.add(std::move(nf));
        } else if (o->kind == ObjectKind::Vehicle) {
            f = static_cast<Ferry*>(o);
        }
        if (f) {
            f->targetPos = glm::vec3(ep.x, ep.y, ep.z);
            f->targetYaw = ep.yaw;
        }
    }
    ctx.client->entityUpdates.clear();
}

// XP threshold to advance from `level` to `level+1`. Curve is mild so
// early-game progression is brisk: 100, 150, 200, 250, ...
static int xpForNextLevel(int level) {
    return 100 + 50 * (level - 1);
}

// Base XP for an enemy of the given level — higher-level foes (found further
// from spawn) are worth more, so venturing out pays off.
static int xpForEnemyKill(int enemyLevel) {
    return 20 + 5 * enemyLevel;
}

// Push a transient HUD message. UI renders the queue in renderPlayUI;
// updateGameplay decrements lifeTime and prunes expired entries.
static void pushToast(AppContext& ctx, std::string text, Voxel color,
                      float life = 3.5f) {
    AppContext::HudToast t;
    t.text = std::move(text);
    t.color = color;
    t.lifeTime = life;
    ctx.toasts.push_back(std::move(t));
    if (ctx.toasts.size() > 8) ctx.toasts.erase(ctx.toasts.begin());
}

// Sample a swarm of small voxel cubes from a dying NPC's rig — used to
// give kills a satisfying "bursts into pieces" effect. Each particle
// inherits the colour of whichever rig voxel it was sampled from, gets
// an outward-and-up initial velocity, then falls and lands on terrain.
static void spawnDeathParticles(AppContext& ctx, const NPC* npc) {
    if (!npc) return;
    const BipedalRig* rig = npc->getRig();
    if (!rig) return;

    static std::mt19937 rng((uint32_t)std::random_device{}());
    auto frand = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    CharacterNode* parts[6] = {
        rig->head,  rig->torso,
        rig->lArm,  rig->rArm,
        rig->lLeg,  rig->rLeg,
    };

    // Aim for ~80 particles total across all body parts. Each part
    // contributes proportional to its voxel count.
    for (CharacterNode* part : parts) {
        if (!part || !part->volume) continue;
        VoxelVolume* v = part->volume;

        // Try up to 30 random samples per part; keep the first dozen that
        // hit a non-empty voxel. Skip-loop keeps the cost bounded.
        int kept = 0;
        const int maxKeep = 14;
        for (int tries = 0; tries < 40 && kept < maxKeep; tries++) {
            int rx = (int)frand(0.0f, (float)v->sizeX);
            int ry = (int)frand(0.0f, (float)v->sizeY);
            int rz = (int)frand(0.0f, (float)v->sizeZ);
            Voxel vx = v->getVoxel(rx, ry, rz);
            if (vx.a == 0) continue;
            kept++;

            VoxelDeathParticle p;
            p.color = vx;
            // Spawn in a small cloud around the NPC chest.
            p.pos = npc->position
                  + glm::vec3(frand(-0.3f, 0.3f),
                              0.7f + frand(0.0f, 0.7f),
                              frand(-0.3f, 0.3f));
            // Outward burst plus a hop upward.
            p.vel = glm::vec3(frand(-3.5f, 3.5f),
                              frand( 2.5f, 6.5f),
                              frand(-3.5f, 3.5f));
            p.life    = 5.0f + frand(0.0f, 2.0f);
            p.maxLife = p.life;
            p.size    = 0.07f + frand(-0.01f, 0.02f);
            ctx.voxelParticles.push_back(p);
        }
    }
    // Bound the list so a kill spree doesn't accumulate thousands of
    // particles in the queue.
    if (ctx.voxelParticles.size() > 600)
        ctx.voxelParticles.erase(ctx.voxelParticles.begin(),
                                  ctx.voxelParticles.begin()
                                  + (ctx.voxelParticles.size() - 600));
}

// Called when the local client observes a nearby Enemy NPC transition
// from alive to dying. Awards XP (may level the player up) and showers
// the kill site with voxel "rubble" particles sampled from the body's
// rig. The actual loot drops are spawned **server-side** so every player
// on the server sees the same items — those arrive as LootSpawn packets
// processed by syncLootDrops below. Simple attribution — any nearby
// observer awards themselves XP — good enough for single-player and
// small-coop play.
void awardEnemyKill(AppContext& ctx, const NPC* npc) {
    int enemyLevel = std::max(1, (int)npc->level);
    int xp = xpForEnemyKill(enemyLevel);
    // Con-based scaling: trivial (far-below) kills give a fraction; equal-or-above
    // foes give full XP. Keeps low-tier grinding from out-pacing venturing out.
    int diff = enemyLevel - ctx.playerLevel;
    if (diff < -8)     xp = std::max(1, xp / 5);
    else if (diff < 0) xp = std::max(1, (int)(xp * (1.0f + 0.06f * (float)diff)));
    ctx.playerXp += float(xp);
    pushToast(ctx, std::string("+") + std::to_string(xp) + " XP",
              Voxel{160, 210, 255, 255}, 2.5f);

    bool leveledUp = false;
    while (ctx.playerXp >= float(xpForNextLevel(ctx.playerLevel))) {
        ctx.playerXp -= float(xpForNextLevel(ctx.playerLevel));
        ctx.playerLevel++;
        ctx.skillPoints++;                 // one skill point per level (press K to spend)
        leveledUp = true;
        pushToast(ctx, std::string("Level Up!  You are now level ")
                       + std::to_string(ctx.playerLevel) + "  (+1 skill point)",
                  Voxel{255, 220, 80, 255}, 5.5f);
    }
    // Resend the PlayerModel so the server knows our new level — future
    // loot rolls for our kills will scale to the new level. Refresh the cached
    // role stats so the larger HP pool takes effect immediately.
    if (leveledUp) {
        ctx.recomputeRoleStats();
        sendPlayerModelUpdate(ctx);
    }

    // Kill-quest progress: credit any active KillEnemies quest whose foe + region
    // match this kill (region = within one danger tier of the kill location).
    int killTier = dangerTierAt(npc->position.x, npc->position.z);
    static std::mt19937 questDropRng(0xC0FFEEu);
    for (Quest& q : ctx.activeQuests) {
        bool credit = false;
        if (questKillCounts(q, (uint8_t)npc->type, killTier)) {
            credit = true;
        } else if (questCollectCounts(q, killTier)) {
            // The slain foe yields the quest collectible ~70% of the time.
            if ((questDropRng() % 100u) < 70u) {
                credit = true;
                if (q.progress + 1 < q.requiredCount)
                    pushToast(ctx, "+1 " + q.collectName + " ("
                                   + std::to_string(q.progress + 1) + "/"
                                   + std::to_string(q.requiredCount) + ")",
                              Voxel{200, 220, 160, 255}, 1.6f);
            }
        }
        if (!credit) continue;
        if (q.progress < q.requiredCount) q.progress++;
        if (q.progress >= q.requiredCount && q.status == QuestStatus::Active) {
            q.status = QuestStatus::Complete;
            pushToast(ctx, std::string("Quest complete: ") + q.title
                           + " - return to a quest giver",
                      Voxel{120, 230, 140, 255}, 5.0f);
        }
    }

    spawnDeathParticles(ctx, npc);
}

// Fire hotbar slot `slot` if its ability is off cooldown and affordable. All
// the per-ability effect logic lives in Ability::activate; this is just the
// gate (cooldown + resource + not mid-swing) plus spending the cost.
void tryActivateHotbar(AppContext& ctx, int slot) {
    if (slot < 0 || slot >= AppContext::HOTBAR_SLOTS) return;
    if (ctx.castTimer > 0.0f) return;                            // already channelling a cast
    AbilityId aid = ctx.hotbar[slot];
    if (aid == AbilityId::None) return;
    Ability* ab = ctx.findAbility(aid);
    if (!ab) return;
    if (ctx.hotbarCooldown[slot] > 0.0f) return;                 // still cooling down
    if (ctx.playerRig && (ctx.playerRig->isAttacking || ctx.playerRig->isCasting))
        return;                                                  // don't interrupt a swing/cast
    if (ctx.resource < ab->resourceCost()) {
        pushToast(ctx, std::string("Not enough ") + resourceName(ctx.resourceType),
                  Voxel{210, 170, 120, 255}, 1.2f);
        return;
    }
    ctx.resource            -= ab->resourceCost();
    ctx.hotbarCooldown[slot] = ab->cooldown();
    ctx.selectedHotbar       = slot;
    if (ab->castTime() > 0.0f) {
        // Channelled cast — the effect fires when it completes (updateGameplay)
        // and the player moves slowly until then.
        ctx.castingAbility = aid;
        ctx.castTimer = ab->castTime();
        ctx.castTotal = ab->castTime();
        if (ctx.playerRig) {
            ctx.playerRig->isCasting = true; ctx.playerRig->castAnim = 0.0f;
            // Defensive buffs (Shield Wall, Last Stand) brace behind the shield
            // for the whole channel instead of the staff weave — the clip is
            // applied last so it overrides the cast pose.
            if (ab->kind() == AbilityKind::Buff)
                ctx.playerRig->playClip(ClipKind::Brace, ab->castTime() + 0.35f);
        }
    } else {
        ab->activate(ctx);
    }
}

// Spawn a client-side visual bolt for each enemy projectile the server fired.
// The bolt flies the straight server-authored path; the server independently
// applies the damage on hit, so dodging is handled there. Fire-and-forget: the
// bolt self-expires on terrain / lifetime (Projectile::update).
void syncEnemyProjectiles(AppContext& ctx) {
    if (!ctx.client) return;
    for (const EnemyProjectileSpawnPacket& e : ctx.client->enemyProjectiles) {
        auto bolt = std::make_unique<EnemyBoltProjectile>();
        bolt->position = glm::vec3(e.x, e.y, e.z);
        bolt->velocity = glm::vec3(e.vx, e.vy, e.vz);
        float spd = glm::length(bolt->velocity);
        bolt->restingDir = (spd > 0.001f) ? bolt->velocity / spd : glm::vec3(0.0f, 0.0f, 1.0f);
        bolt->lifeTime   = e.ttl;
        ctx.objectManager.add(std::move(bolt));
    }
    ctx.client->enemyProjectiles.clear();
}

// Creates/updates client-side NPC objects from the server's NPCState
// broadcasts, and times out NPCs the server has stopped sending (their town
// streamed out of range). The server owns NPC motion/AI; clients interpolate.
void syncNPCObjects(AppContext& ctx) {
    if (!ctx.client) return;
    double now = glfwGetTime();
    for (const NPCStatePacket& np : ctx.client->npcUpdates) {
        GameObject* o = ctx.objectManager.findById(np.entityId);
        bool isNewNpc = (o == nullptr);
        NPC* n = nullptr;
        if (!o) {
            auto nn = std::make_unique<NPC>();
            nn->id             = np.entityId;
            nn->type           = (NPCType)np.npcType;
            nn->level          = np.level;
            nn->appearanceSeed = np.appearanceSeed;
            nn->position       = glm::vec3(np.x, np.y, np.z);
            nn->yaw            = np.yaw;
            nn->initClientVisual();
            n = nn.get();
            ctx.objectManager.add(std::move(nn));
        } else if (o->kind == ObjectKind::NPC) {
            n = static_cast<NPC*>(o);
        }
        if (n) {
            // Detect the alive→dying transition for nearby enemies so we
            // can award XP/loot exactly once per kill.
            bool wasDying = (!isNewNpc) ? n->dyingFlag : true;
            bool nowDying = (np.flags & 4) != 0;

            n->targetPos  = glm::vec3(np.x, np.y, np.z);
            n->targetYaw  = np.yaw;
            n->velocity   = glm::vec3(np.vx, np.vy, np.vz);
            n->health     = np.health;
            n->level      = np.level;
            n->walking    = (np.flags & 1) != 0;
            n->attackFlag = (np.flags & 2) != 0;
            n->sitting    = (np.flags & 8) != 0;
            n->dyingFlag  = nowDying;
            n->lastUpdate = now;

            if (!wasDying && nowDying && isHostileNpc(n->type)) {
                float dist = glm::distance(n->position, ctx.camera.position);
                if (dist < 22.0f) awardEnemyKill(ctx, n);
            }
        }
    }
    ctx.client->npcUpdates.clear();

    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        if (now - static_cast<NPC*>(o.get())->lastUpdate > 2.0)
            o->dead = true;
    }
}

// Creates/updates client-side Animal objects from the server's AnimalState
// broadcasts, and times out animals the server has stopped sending.
void syncAnimalObjects(AppContext& ctx) {
    if (!ctx.client) return;
    double now = glfwGetTime();
    for (const AnimalStatePacket& ap : ctx.client->animalUpdates) {
        GameObject* o = ctx.objectManager.findById(ap.entityId);
        Animal* a = nullptr;
        if (!o) {
            auto na = std::make_unique<Animal>();
            na->id       = ap.entityId;
            na->species  = (AnimalSpecies)ap.species;
            na->variant  = ap.variant;
            na->position = glm::vec3(ap.x, ap.y, ap.z);
            na->yaw      = ap.yaw;
            na->initClientVisual();
            a = na.get();
            ctx.objectManager.add(std::move(na));
        } else if (o->kind == ObjectKind::Animal) {
            a = static_cast<Animal*>(o);
        }
        if (a) {
            a->targetPos  = glm::vec3(ap.x, ap.y, ap.z);
            a->targetYaw  = ap.yaw;
            a->velocity   = glm::vec3(ap.vx, ap.vy, ap.vz);
            a->walking    = (ap.flags & 1) != 0;
            a->lastUpdate = now;
        }
    }
    ctx.client->animalUpdates.clear();

    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::Animal) continue;
        if (now - static_cast<Animal*>(o.get())->lastUpdate > 2.0)
            o->dead = true;
    }
}

// Rebuild a local Item from the raw fields in a LootSpawnPacket. Mirrors
// the data the server captured via `itemToLootPacket` in network.cpp.
static std::unique_ptr<Item> itemFromLootPacket(const LootSpawnPacket& p) {
    std::string name(p.name);
    std::string setName(p.setName);
    if (p.kind == 1) {
        auto c = std::make_unique<ClothingItem>(
            name, (EquipSlot)p.slot, (ClothingTier)p.subtype);
        c->rarity       = (ItemRarity)p.rarity;
        c->level        = p.level;
        c->primaryColor = p.primary;
        c->accentColor  = p.accent;
        c->patternSeed  = p.patternSeed;
        c->attackPower  = p.attackPower;
        c->defenseValue = p.defenseValue;
        c->setKey       = setName;
        return c;
    } else if (p.kind == 2) {
        // Pass the element so a Holy staff rebuilds as HealingStaffItem (the
        // factory keys the concrete subclass on type + element).
        auto w = createWeaponItem(name, (WeaponType)p.subtype, (WeaponElement)p.element);
        if (!w) return nullptr;
        w->rarity       = (ItemRarity)p.rarity;
        w->level        = p.level;
        w->primaryColor = p.primary;
        w->accentColor  = p.accent;
        w->attackPower  = p.attackPower;
        w->defenseValue = p.defenseValue;
        w->setKey       = setName;
        w->element      = (WeaponElement)p.element;
        return w;
    }
    return nullptr;
}

// Drain the server's loot broadcasts. LootSpawn entries become local
// LootDrop GameObjects; LootRemoved entries kill the matching local drop.
// If the removal names us as the new owner, we also pocket the item.
void syncLootDrops(AppContext& ctx) {
    if (!ctx.client) return;

    for (auto& sp : ctx.client->lootSpawns) {
        auto drop = std::make_unique<LootDrop>();
        drop->position     = glm::vec3(sp.x, sp.y, sp.z);
        // Server doesn't send velocity, so use a small randomised hop.
        drop->velocity     = glm::vec3(((rand() % 200) - 100) / 50.0f,
                                        2.5f + (rand() % 200) / 100.0f,
                                       ((rand() % 200) - 100) / 50.0f);
        drop->serverLootId = sp.dropId;
        if (auto item = itemFromLootPacket(sp))
            drop->setItem(std::move(item));
        ctx.objectManager.add(std::move(drop));
    }
    ctx.client->lootSpawns.clear();

    for (auto& rm : ctx.client->lootRemovals) {
        for (auto& o : ctx.objectManager.objects()) {
            if (o->dead || o->kind != ObjectKind::Loot) continue;
            LootDrop* d = static_cast<LootDrop*>(o.get());
            if (d->serverLootId != rm.dropId) continue;
            if (rm.newOwnerId == ctx.client->clientID) {
                // We won the pickup — take ownership and stash it.
                if (auto taken = d->takeItem()) {
                    Voxel col = rarityUiColor(taken->rarity);
                    std::string name = taken->getName();
                    ctx.inventory.addItem(std::move(taken));
                    pushToast(ctx, "Picked up: " + name, col, 3.0f);
                }
            }
            d->dead = true;
            break;
        }
    }
    ctx.client->lootRemovals.clear();
}

// Finds the closest pickup-eligible loot drop within a few blocks of the
// player, surfaces an "[E] Pick up <name>" prompt, and sends a
// LootPickupRequest to the server when interact is pressed. Actual item
// transfer happens when LootRemoved arrives back from the server (see
// syncLootDrops). Runs before NPC interaction so loot wins over villager
// talk when both prompts compete on E.
void updateLootPickup(AppContext& ctx) {
    ctx.lootHintName.clear();

    LootDrop* best = nullptr;
    float     bestD2 = 3.0f * 3.0f;
    glm::vec3 eye = ctx.camera.position;
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::Loot) continue;
        LootDrop* d = static_cast<LootDrop*>(o.get());
        if (!d->peekItem()) continue;
        glm::vec3 to = d->position - eye;
        float d2 = to.x * to.x + to.y * to.y + to.z * to.z;
        if (d2 < bestD2) {
            bestD2 = d2;
            best   = d;
        }
    }
    if (!best) return;

    const Item* shown = best->peekItem();
    ctx.lootHintName  = shown->getName();
    ctx.lootHintColor = rarityUiColor(shown->rarity);

    if (!ctx.interactPressed) return;

    bool hasRoom = false;
    for (int i = 0; i < ctx.inventory.capacity(); i++) {
        if (!ctx.inventory.at(i)) { hasRoom = true; break; }
    }
    if (!hasRoom) {
        pushToast(ctx, "Bag full!", Voxel{220, 100, 80, 255}, 2.5f);
        ctx.interactPressed = false;
        return;
    }

    // Send a pickup request. Server will decide who actually gets it
    // (in case two players race) and broadcast LootRemoved, which
    // syncLootDrops above turns into the actual inventory transfer.
    if (ctx.client && best->serverLootId != 0) {
        LootPickupRequestPacket req {};
        req.dropId = best->serverLootId;
        ctx.client->send(PacketType::LootPickupRequest, &req, sizeof(req));
    }
    ctx.interactPressed = false;
}

// Finds the villager the player is facing within talk range, drives the talk
// prompt, and opens the dialogue box when the interact key was pressed.
// Scans nearby GameObjects for the best interactable in front of the player.
// Drives `ctx.pendingInteraction` (read by the HUD for the E-hint) and, on E
// press, enters or exits a player pose. Designed to be extended: adding a new
// InteractAction value + a switch case here is enough to wire a new action.
void updatePropInteraction(AppContext& ctx) {
    glm::vec3 eye = ctx.camera.position;
    glm::vec3 fwd = glm::vec3(ctx.camera.front.x, 0.0f, ctx.camera.front.z);
    if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);

    Interaction best{};
    float bestD2 = 2.5f * 2.5f;            // ~ arm's reach
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead) continue;
        Interaction off;
        if (!o->getInteraction(off)) continue;
        glm::vec3 to = o->position - eye;
        float dy = to.y;
        to.y = 0.0f;
        float d2 = to.x * to.x + to.z * to.z;
        if (d2 > bestD2) continue;
        if (std::abs(dy) > 1.5f) continue;     // same-floor only
        if (d2 > 0.04f && glm::dot(glm::normalize(to), fwd) < 0.2f) continue;
        bestD2 = d2;
        best   = off;
    }
    // While already in a pose we don't surface another object's hint; the HUD
    // shows the "press E to get up" prompt instead.
    ctx.pendingInteraction = (ctx.playerPose == PlayerPose::Standing) ? best
                                                                      : Interaction{};

    // Handle E press: enter a new pose, or exit the current one.
    if (ctx.interactPressed) {
        if (ctx.playerPose != PlayerPose::Standing) {
            ctx.playerPose      = PlayerPose::Standing;
            ctx.interactPressed = false;
        } else if (best.action != InteractAction::None) {
            ctx.poseAnchorPos = best.anchorPos;
            ctx.poseAnchorYaw = best.anchorYaw;
            switch (best.action) {
                case InteractAction::SitChair:
                    ctx.playerPose = PlayerPose::Sitting; break;
                case InteractAction::LieBed:
                    ctx.playerPose = PlayerPose::Lying;   break;
                default: break;
            }
            ctx.interactPressed = false;
        }
    }

    // Any movement input pops the player out of the pose.
    if (ctx.playerPose != PlayerPose::Standing) {
        if (ctx.keyFwd || ctx.keyBack || ctx.keyLeft ||
            ctx.keyRight || ctx.keyJump) {
            ctx.playerPose = PlayerPose::Standing;
        }
    }

    // Push the pose into the rig and snap the camera to the pose anchor.
    if (ctx.playerRig) ctx.playerRig->pose = ctx.playerPose;
    if (ctx.playerPose != PlayerPose::Standing) {
        const float eyeOffset =
            (ctx.playerPose == PlayerPose::Sitting) ? 0.95f : 0.55f;
        ctx.camera.position = ctx.poseAnchorPos
                            + glm::vec3(0.0f, eyeOffset, 0.0f);
        ctx.camera.velocity = glm::vec3(0.0f);
        ctx.camera.onGround = true;
    }
}

void updateNpcInteraction(AppContext& ctx) {
    glm::vec3 eye = ctx.camera.position;
    glm::vec3 fwd = glm::vec3(ctx.camera.front.x, 0.0f, ctx.camera.front.z);
    if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);

    NPC* best = nullptr;
    float bestD2 = 4.0f * 4.0f;
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        NPC* n = static_cast<NPC*>(o.get());
        if (n->type != NPCType::Villager && n->type != NPCType::Trainer &&
            n->type != NPCType::Questgiver) continue;
        glm::vec3 to = n->position - eye; to.y = 0.0f;
        float d2 = to.x * to.x + to.z * to.z;
        if (d2 > bestD2) continue;
        if (d2 > 0.04f && glm::dot(glm::normalize(to), fwd) < 0.35f) continue;
        bestD2 = d2;
        best   = n;
    }

    if (best) {
        ctx.talkTargetName = (best->type == NPCType::Trainer)    ? "Class Trainer"
                           : (best->type == NPCType::Questgiver) ? "Quest Giver"
                           : npcName(best->appearanceSeed);
        ctx.talkTargetSeed = best->appearanceSeed;
        ctx.talkTargetPos  = best->position;
    } else {
        ctx.talkTargetName.clear();
    }

    if (ctx.interactPressed && best) {
        if (best->type == NPCType::Trainer) {
            // Open the class-change window instead of a flavour line.
            ctx.showTrainer = true;
        } else if (best->type == NPCType::Questgiver) {
            // Open the town quest board. The client object doesn't carry the
            // town index, so map the giver's position to the nearest town in
            // the (deterministic) plan.
            const TownPlan& tp = getTownPlan();
            int bestT = -1; long long bestTD = -1;
            for (size_t i = 0; i < tp.towns.size(); ++i) {
                long long dx = (long long)tp.towns[i].center.x - (long long)best->position.x;
                long long dz = (long long)tp.towns[i].center.y - (long long)best->position.z;
                long long d2 = dx * dx + dz * dz;
                if (bestTD < 0 || d2 < bestTD) { bestTD = d2; bestT = (int)i; }
            }
            ctx.questGiverTown = bestT;
            ctx.showQuestGiver = true;
        } else {
            ctx.talkName  = npcName(best->appearanceSeed);
            ctx.talkLine  = npcFlavorLine(best->appearanceSeed, ctx.talkCount);
            ctx.talkTimer = 6.0f;
            ctx.talkCount++;
        }
    }
    ctx.interactPressed = false;
    if (ctx.talkTimer > 0.0f) ctx.talkTimer -= ctx.deltaTime;
}

// The NPC a swing/shot should land on — nearest one ahead within range.
// Melee uses a 3.8-block radius and a generous facing cone; bows use a
// 28-block radius and a tighter cone (you have to actually aim).
NPC* findTargetNpc(AppContext& ctx, float maxRange, float minFacing) {
    glm::vec3 eye = ctx.camera.position;
    glm::vec3 fwd = glm::vec3(ctx.camera.front.x, 0.0f, ctx.camera.front.z);
    if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
    NPC* best = nullptr;
    float bestD2 = maxRange * maxRange;
    for (auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::NPC) continue;
        NPC* n = static_cast<NPC*>(o.get());
        glm::vec3 to = n->position - eye; to.y = 0.0f;
        float d2 = to.x * to.x + to.z * to.z;
        if (d2 > bestD2) continue;
        if (d2 > 0.04f && glm::dot(glm::normalize(to), fwd) < minFacing) continue;
        bestD2 = d2;
        best   = n;
    }
    return best;
}

NPC* findMeleeTargetNpc(AppContext& ctx) {
    return findTargetNpc(ctx, 3.8f, 0.3f);
}

NPC* findRangedTargetNpc(AppContext& ctx) {
    return findTargetNpc(ctx, 28.0f, 0.95f);   // tight cone for bow aim
}

// Applies incoming damage to the player, regenerates health out of combat,
// and respawns at the spawn town on death.
void updatePlayerVitals(AppContext& ctx) {
    if (ctx.client && ctx.client->pendingSelfDamage > 0.0f) {
        float dmg = ctx.client->pendingSelfDamage;

        // Dodge-roll i-frames: shrug the hit off entirely.
        if (ctx.rollTimer > 0.0f) {
            dmg = 0.0f;
            pushToast(ctx, "Dodge!", Voxel{160, 230, 255, 255}, 1.0f);
        }

        // Active shield block — if the player is raising the shield AND
        // has one equipped, soak damage equal to (defense * a flat
        // efficiency). 1.0 defence ~= 1 HP soaked; legendary plate
        // shields can fully eat low-tier hits.
        if (ctx.shieldRaised) {
            if (Item* off = ctx.inventory.equipped(EquipSlot::OffHand)) {
                if (off->getKind() == ItemKind::Weapon
                    && static_cast<WeaponItem*>(off)->getType() == WeaponType::Shield) {
                    float soak = off->defenseValue * 1.5f;
                    float absorbed = std::min(dmg, soak);
                    dmg -= absorbed;
                    if (absorbed > 0.5f) {
                        AppContext::HudToast t;
                        t.text     = "Blocked " + std::to_string((int)absorbed);
                        t.color    = Voxel{120, 200, 255, 255};
                        t.lifeTime = 1.6f;
                        ctx.toasts.push_back(std::move(t));
                    }
                }
            }
        }
        // Role defence (plus any active defence buff such as Shield Wall)
        // reduces the hit; max HP scales the fraction so a Tank's larger pool
        // drains slower than a DPS's for the same raw damage.
        float defense = ctx.defenseMult;
        for (const ActiveBuff& b : ctx.activeBuffs)
            if (b.kind == BuffKind::Defense) defense += b.magnitude;
        ctx.playerHealth -= (dmg / defense) / ctx.maxHpScaled;
        // Tanks build Rage by weathering hits.
        if (ctx.resourceType == ResourceType::Rage)
            ctx.resource = std::min(ctx.resourceMax, ctx.resource + dmg * 0.5f);
        ctx.client->pendingSelfDamage = 0.0f;
        ctx.regenDelay = 5.0f;
    }
    // Incoming heals (chain heal / sanctuary cast by any player, including us)
    // top the bar back up. Capped at full; never blocked by the regen delay.
    if (ctx.client && ctx.client->pendingSelfHeal > 0.0f) {
        ctx.playerHealth = std::min(1.0f, ctx.playerHealth + ctx.client->pendingSelfHeal / ctx.maxHpScaled);
        ctx.client->pendingSelfHeal = 0.0f;
    }
    if (ctx.regenDelay > 0.0f) {
        ctx.regenDelay -= ctx.deltaTime;
    } else if (ctx.playerHealth < 1.0f) {
        ctx.playerHealth = std::min(1.0f, ctx.playerHealth + 0.045f * ctx.deltaTime);
    }
    if (ctx.playerHealth <= 0.0f) {
        // Death — respawn at the nearest graveyard (falling back to the spawn
        // town if the world has none). The ground-snap on the next frame settles
        // the player onto the surface, so the exact Y here doesn't matter.
        glm::ivec2 gc; int gy;
        if (findNearestGraveyard(ctx.camera.position.x, ctx.camera.position.z, gc, gy)) {
            ctx.spawnX = gc.x;
            ctx.spawnZ = gc.y;
        }
        ctx.camera.position = glm::vec3((float)ctx.spawnX + 0.5f, 140.0f,
                                        (float)ctx.spawnZ + 0.5f);
        ctx.camera.velocity = glm::vec3(0.0f);
        ctx.playerHealth    = 1.0f;
        ctx.regenDelay      = 0.0f;
        ctx.spawnedOnGround = false;
        ctx.noclip          = true;
    }
}

