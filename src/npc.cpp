#include "npc.h"
#include "voxel_model.h"
#include "town.h"
#include "world.h"
#include "network.h"
#include "npc_appearance.h"
#include "farm_director.h"    // FarmDirector — crop state queried by farmer AI
#include "dungeon.h"          // getDungeonPlan — enemy rosters per dungeon
#include "prop_placement.h"   // getPropPlacements() — deterministic, server-safe
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <utility>
#include <functional>

// --- small helpers ---------------------------------------------------------

static uint32_t hashU32(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x85EBCA77u + (a << 6) + (a >> 2));
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

static float frand01(std::mt19937& r) {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(r);
}

static bool isNight(float gameTime) {
    return gameTime < 0.24f || gameTime > 0.76f;
}

// True if nothing solid blocks the straight line from an NPC's eye to a target
// — used so ranged enemies don't fire (or chase) through dungeon walls.
static bool hasLineOfSight(World& world, glm::vec3 fromFeet, glm::vec3 to) {
    glm::vec3 eye = fromFeet + glm::vec3(0.0f, 1.4f, 0.0f);
    glm::vec3 d   = to - eye;
    float dist = glm::length(d);
    if (dist < 0.5f) return true;
    glm::ivec3 hb, hn;
    // Stop a touch short so we don't count the block the target stands in.
    return !world.raycast(eye, d / dist, std::max(0.5f, dist - 1.0f), hb, hn);
}

static bool isSolidBlk(BlockType b) { return b != BlockType::Air && b != BlockType::Water; }

// Move an NPC by (dx,dz) only if no wall blocks the destination at body height,
// so dungeon enemies stay in their rooms/corridors instead of ghosting through
// walls. A 1-block step (gentle slope) is still allowed.
static void moveNpcXZ(NPC& n, World& world, float dx, float dz) {
    int   gy = (int)n.groundY;
    float nx = n.position.x + dx, nz = n.position.z + dz;
    int   ix = (int)floorf(nx), iz = (int)floorf(nz);
    // A wall at head height blocks the move (keeps enemies in rooms/corridors).
    if (isSolidBlk(world.getBlock(ix, gy + 1, iz))) return;
    // Don't step off a ledge into a hole — a pit, a water channel, or a stairwell
    // shaft. Require standable ground within one step of the destination so
    // enemies stop at the edge instead of falling through to the floor below.
    if (!isSolidBlk(world.getBlock(ix, gy,     iz)) &&   // a 1-block step up
        !isSolidBlk(world.getBlock(ix, gy - 1, iz)) &&   // the same level
        !isSolidBlk(world.getBlock(ix, gy - 2, iz)))     // a 1-block step down
        return;
    n.position.x = nx;
    n.position.z = nz;
}

static constexpr int   CAMP_GRID  = 256;   // bandit-camp survey cell size, blocks
static constexpr float RAID_RANGE = 240.0f; // a camp this near a town may raid it

// Snaps an NPC's Y onto the surface directly under it.
//
// NPCs intentionally don't collide with blocks (they ghost through interior
// partition walls so the AI can route them straight to a bed), but a naive
// "stand on the first solid block below me" rule then turns a building's
// exterior wall into a stair: a villager walking into the wall finds the
// wall block under their feet, snaps onto it, and the same trick repeats
// next frame, climbing one block per tick all the way up to the roof. Once
// up there the short 7-block downward scan can't reach the ground again,
// so they wander around in the sky.
//
// Two rules avoid both halves of that:
//   1. Never snap UP by more than one block per call — natural slopes and
//      stairs are fine (one-block rises), but a 4-block-tall wall can't be
//      climbed because each frame the new ground is too far above the last
//      one, so the snap is refused and the NPC keeps its previous Y. They
//      ghost through the wall at floor level instead of climbing it.
//   2. The downward scan is generous (~30 blocks) so a previously-stranded
//      NPC, or one stepping off a ledge, can drop back to the real ground.
static void groundSnap(NPC& n, World& world) {
    int wx = (int)floorf(n.position.x), wz = (int)floorf(n.position.z);
    int prevI = (int)floorf(n.groundY);
    int yTop = std::min(prevI + 1, CHUNK_HEIGHT - 2);
    int yBot = std::max(1, prevI - 30);
    for (int y = yTop; y >= yBot; y--) {
        BlockType b = world.getBlock(wx, y, wz);
        if (b == BlockType::Air || b == BlockType::Water) continue;
        float newGround = (float)(y + 1);
        if (newGround > n.groundY + 1.0f + 1e-3f) {
            // The only standable spot in this column is above stepping
            // height — almost certainly a wall the NPC has ghosted into.
            // Refuse the snap and keep the previous Y so they pass through.
            return;
        }
        n.groundY = newGround;
        break;
    }
    n.position.y = n.groundY;
}

// --- NPC -------------------------------------------------------------------

NPC::NPC() : GameObject(ObjectKind::NPC) {}

NPC::~NPC() {
    delete rig;
}

void NPC::initClientVisual() {
    if (rig) return;
    rig = new BipedalRig();
    rig->setupDefaultHuman(true);
    std::mt19937 arng(appearanceSeed ? appearanceSeed : 1u);
    rig->randomizeAppearance(arng);
    // Paint a coherent themed loadout — villagers get cloth peasant
    // outfits with no weapon, bandits get matching dark leather sets
    // with a melee/ranged weapon, guards get plate town-livery with
    // sword/axe + shield. All five clothing slots share one palette so
    // each NPC reads as one outfit, not five mismatched pieces.
    applyNpcThemedLoadout(*rig, appearanceSeed, type);
}

void NPC::update(float dt, World& world) {
    (void)world;
    // Smoothly chase the latest server state — identical scheme to Ferry.
    float k = std::min(1.0f, 10.0f * dt);
    position = glm::mix(position, targetPos, k);
    float dyaw = targetYaw - yaw;
    while (dyaw >  180.0f) dyaw -= 360.0f;
    while (dyaw < -180.0f) dyaw += 360.0f;
    yaw += dyaw * k;

    if (dyingFlag)
        dyingLerp = std::min(1.0f, dyingLerp + dt * 2.5f);

    if (rig) {
        // Trigger the swing on the rising edge of the server's attack flag.
        // Farmers play a tool-work clip (hoe/scythe) instead of a combat swing.
        if (attackFlag && !prevAttackFlag) {
            if (type == NPCType::Farmer) {
                rig->playClip(ClipKind::Hoe, 0.7f);
            } else if (type == NPCType::Cultist) {
                rig->isCasting = true;
                rig->castAnim  = 0.0f;
            } else {
                rig->isAttacking = true;
                rig->attackAnim  = 0.0f;
            }
        }
        prevAttackFlag = attackFlag;
        // A seated villager snaps to the resting pose; otherwise stand & walk.
        rig->pose = sitting ? PlayerPose::Sitting : PlayerPose::Standing;
        // Drive the gait from the server's authoritative speed, not the noisy
        // frame-to-frame interpolation delta, so the walk cycle stays steady.
        rig->update(dt, glm::length(velocity) * 0.6f);
    }
}

void NPC::draw(GLuint modelLoc) const {
    if (!rig) return;
    glm::mat4 m = baseMatrix(0.06f * rig->heightScale);
    if (dyingLerp > 0.0f)   // collapse forward as the NPC dies
        m = glm::rotate(m, glm::radians(dyingLerp * 82.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    rig->draw(m, modelLoc);
}

void NPC::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    const float hw = 0.4f, h = 1.9f;
    mn = position - glm::vec3(hw, 0.0f, hw);
    mx = position + glm::vec3(hw, h,    hw);
}

// --- TownNav ---------------------------------------------------------------

void TownNav::worldToCell(float wx, float wz, int& cx, int& cz) const {
    cx = (int)floorf((wx - (float)originX) / (float)CELL);
    cz = (int)floorf((wz - (float)originZ) / (float)CELL);
}

glm::vec2 TownNav::cellCenter(int cx, int cz) const {
    return glm::vec2((float)originX + cx * CELL + CELL * 0.5f,
                     (float)originZ + cz * CELL + CELL * 0.5f);
}

void TownNav::build(const Town& town) {
    int R = town.radius + 40;
    originX = town.center.x - R;
    originZ = town.center.y - R;
    gw = (2 * R) / CELL + 2;
    gh = gw;
    cells = gw * gh;
    blocked.assign((size_t)cells, 0);

    const int margin = 2;   // keep NPCs a couple of blocks clear of walls
    for (const TownBuilding& b : town.buildings) {
        if (b.dimX == 0) continue;
        for (int wz = b.wz - margin; wz < b.wz + b.dimZ + margin; wz++)
            for (int wx = b.wx - margin; wx < b.wx + b.dimX + margin; wx++) {
                int cx, cz;
                worldToCell((float)wx, (float)wz, cx, cz);
                if (inBounds(cx, cz)) blocked[idx(cx, cz)] = 1;
            }
    }
}

bool TownNav::walkable(float wx, float wz) const {
    int cx, cz;
    worldToCell(wx, wz, cx, cz);
    return inBounds(cx, cz) && !blocked[idx(cx, cz)];
}

glm::vec2 TownNav::nearestWalkable(glm::vec2 p) const {
    int cx, cz;
    worldToCell(p.x, p.y, cx, cz);
    cx = std::clamp(cx, 0, gw - 1);
    cz = std::clamp(cz, 0, gh - 1);
    if (!blocked[idx(cx, cz)]) return cellCenter(cx, cz);
    int maxR = std::max(gw, gh);
    for (int r = 1; r < maxR; r++) {
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                int nx = cx + dx, nz = cz + dz;
                if (inBounds(nx, nz) && !blocked[idx(nx, nz)])
                    return cellCenter(nx, nz);
            }
    }
    return cellCenter(cx, cz);
}

glm::vec2 TownNav::randomWalkableNear(glm::vec2 around, float radius,
                                     std::mt19937& rng) const {
    std::uniform_real_distribution<float> angD(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> radD(0.0f, radius);
    for (int i = 0; i < 24; i++) {
        float a = angD(rng), r = radD(rng);
        glm::vec2 p(around.x + cosf(a) * r, around.y + sinf(a) * r);
        if (walkable(p.x, p.y)) {
            int cx, cz;
            worldToCell(p.x, p.y, cx, cz);
            return cellCenter(cx, cz);
        }
    }
    return nearestWalkable(around);
}

std::vector<glm::vec2> TownNav::findPath(glm::vec2 fromW, glm::vec2 toW) const {
    std::vector<glm::vec2> out;
    if (!ready()) return out;

    glm::vec2 sW = nearestWalkable(fromW), gW = nearestWalkable(toW);
    int sx, sz, gx, gz;
    worldToCell(sW.x, sW.y, sx, sz);
    worldToCell(gW.x, gW.y, gx, gz);
    if (!inBounds(sx, sz) || !inBounds(gx, gz)) return out;

    int s = idx(sx, sz), goal = idx(gx, gz);
    std::vector<float>   gScore((size_t)cells, 1e30f);
    std::vector<int>     cameFrom((size_t)cells, -1);
    std::vector<uint8_t> closed((size_t)cells, 0);

    auto H = [&](int cx, int cz) {
        float dx = fabsf((float)(cx - gx)), dz = fabsf((float)(cz - gz));
        return (dx + dz) + (1.41421356f - 2.0f) * std::min(dx, dz);
    };
    typedef std::pair<float, int> PQE;
    std::priority_queue<PQE, std::vector<PQE>, std::greater<PQE>> open;
    gScore[s] = 0.0f;
    open.push({ H(sx, sz), s });

    static const int DX[8] = { 1, -1, 0, 0,  1,  1, -1, -1 };
    static const int DZ[8] = { 0,  0, 1, -1, 1, -1,  1, -1 };
    int guard = 0, cap = cells + 8;
    while (!open.empty() && guard++ < cap) {
        int cur = open.top().second; open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == goal) break;
        int cx = cur % gw, cz = cur / gw;
        for (int d = 0; d < 8; d++) {
            int nx = cx + DX[d], nz = cz + DZ[d];
            if (!inBounds(nx, nz)) continue;
            int ni = idx(nx, nz);
            if (blocked[ni] || closed[ni]) continue;
            if (d >= 4 && (blocked[idx(cx + DX[d], cz)] ||
                           blocked[idx(cx, cz + DZ[d])]))
                continue;   // no diagonal corner-cutting through walls
            float step = (d < 4) ? 1.0f : 1.41421356f;
            float ng = gScore[cur] + step;
            if (ng < gScore[ni]) {
                gScore[ni] = ng;
                cameFrom[ni] = cur;
                open.push({ ng + H(nx, nz), ni });
            }
        }
    }

    if (goal != s && cameFrom[goal] < 0) return out;   // unreachable
    std::vector<int> rev;
    for (int c = goal; c != -1; c = cameFrom[c]) {
        rev.push_back(c);
        if (c == s) break;
    }
    for (int i = (int)rev.size() - 1; i >= 0; i--)
        out.push_back(cellCenter(rev[i] % gw, rev[i] / gw));
    return out;
}

// --- NpcDirector -----------------------------------------------------------

void NpcDirector::update(float dt, const std::vector<DirectorPlayer>& players,
                         World& world, float gameTime) {
    const TownPlan& plan = getTownPlan();
    for (auto& w : wantedTimer)
        if (w.second > 0.0f) w.second -= dt;
    for (int ti = 0; ti < (int)plan.towns.size(); ti++) {
        const Town& t = plan.towns[ti];
        float best2 = 1e18f;
        for (const DirectorPlayer& p : players) {
            float dx = p.pos.x - (float)t.center.x, dz = p.pos.z - (float)t.center.y;
            best2 = std::min(best2, dx * dx + dz * dz);
        }
        float act   = (float)t.radius + 110.0f;
        float deact = (float)t.radius + 170.0f;
        bool  pop   = populated.count(ti) > 0;
        if (!pop && best2 < act * act && (int)active.size() < 220)
            populateTown(ti);
        else if (pop && best2 > deact * deact)
            depopulateTown(ti);
    }
    updateCamps(players);
    if (farmDir) updateFarms(players);
    updateDungeons(players);

    for (auto& n : active) {
        // A dying NPC plays its fall-over, then is swept from the world.
        if (n->dyingTimer > 0.0f) {
            n->dyingTimer -= dt;
            n->walking  = false;
            n->velocity = glm::vec3(0.0f);
            if (n->dyingTimer <= 0.0f) n->dead = true;
            continue;
        }
        if (n->type == NPCType::Enemy || n->type == NPCType::Skeleton ||
            n->type == NPCType::Brute)        stepBandit(*n, dt, world, players);
        else if (n->type == NPCType::Cultist) stepRangedEnemy(*n, dt, world, players);
        else if (n->type == NPCType::Guard)   stepGuard(*n, dt, world, players);
        else if (n->type == NPCType::Farmer)  stepFarmer(*n, dt, world, gameTime);
        else                                  stepVillager(*n, dt, world, gameTime);
    }

    active.erase(std::remove_if(active.begin(), active.end(),
                     [](const std::unique_ptr<NPC>& n) { return n->dead; }),
                 active.end());
}

// Farmers tend a field: walk the furrows to ripe wheat (scythe it) or bare soil
// (hoe it), play a tool-work stroke, then move on. Crop state lives in the
// FarmDirector; this only drives the body. Repurposed NPC fields:
//   townIndex   = the farm index this farmer tends
//   seatIndex   = the target crop tile index
//   pendingAct  = the queued action (1 = scythe ripe, 2 = hoe bare)
//   restAnchor  = the crop tile centre (used to face it while working)
//   attackAnimTimer = work-stroke countdown (also drives the client clip flag)
void NpcDirector::stepFarmer(NPC& n, float dt, World& world, float gameTime) {
    (void)gameTime;
    groundSnap(n, world);
    int fi = n.townIndex;

    // Mid work-stroke: hold still, then apply the crop edit when it finishes.
    if (n.attackAnimTimer > 0.0f) {
        n.attackAnimTimer -= dt;
        n.walking  = false;
        n.velocity = glm::vec3(0.0f);
        if (n.attackAnimTimer <= 0.0f && farmDir) {
            if (n.pendingAct == 1) farmDir->harvestTile(fi, n.seatIndex, world);
            else                   farmDir->hoeTile(fi, n.seatIndex, world);
            n.pendingAct = 0;
            n.seatIndex  = -1;
            n.idleTimer  = 0.3f + frand01(rng) * 0.7f;
        }
        return;
    }

    // Walking to the work cell.
    if (n.pathIndex < n.path.size()) {
        glm::vec2 wp = n.path[n.pathIndex];
        glm::vec2 cur(n.position.x, n.position.z);
        glm::vec2 d = wp - cur;
        float dist = glm::length(d);
        if (dist < 0.5f) {
            n.pathIndex++;
            if (n.pathIndex >= n.path.size()) {
                n.walking  = false;
                n.velocity = glm::vec3(0.0f);
                glm::vec2 fd(n.restAnchor.x - cur.x, n.restAnchor.z - cur.y);
                if (glm::length(fd) > 0.01f) n.yaw = glm::degrees(atan2f(fd.x, fd.y));
                n.attackAnimTimer = 0.9f;   // work stroke (drives the hoe/scythe clip)
            }
            return;
        }
        const float speed = 1.7f;
        glm::vec2 dir = d / dist;
        n.position.x += dir.x * speed * dt;
        n.position.z += dir.y * speed * dt;
        n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
        n.walking  = true;
        n.yaw      = glm::degrees(atan2f(dir.x, dir.y));
        return;
    }

    // Idle between tasks.
    if (n.idleTimer > 0.0f) {
        n.idleTimer -= dt;
        n.walking  = false;
        n.velocity = glm::vec3(0.0f);
        return;
    }

    // Pick the next tile to work and route to a furrow cell beside it.
    glm::ivec2 cropXZ; int tileIdx = -1; bool ripe = false;
    glm::vec2 pos(n.position.x, n.position.z);
    if (farmDir && farmDir->nearestWorkTile(fi, pos, cropXZ, tileIdx, ripe)) {
        int cy = (int)n.groundY;
        glm::ivec2 stand = cropXZ;
        bool found = false;
        const int off[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (auto& o : off) {
            int sx = cropXZ.x + o[0], sz = cropXZ.y + o[1];
            if (world.getBlock(sx, cy, sz) == BlockType::Air) { stand = glm::ivec2(sx, sz); found = true; break; }
        }
        if (!found) { n.idleTimer = 0.5f; return; }   // boxed in — try again shortly
        n.path.assign(1, glm::vec2((float)stand.x + 0.5f, (float)stand.y + 0.5f));
        n.pathIndex  = 0;
        n.seatIndex  = tileIdx;
        n.pendingAct = ripe ? 1 : 2;
        n.restAnchor = glm::vec3((float)cropXZ.x + 0.5f, n.groundY, (float)cropXZ.y + 0.5f);
        n.walking    = true;
    } else {
        n.idleTimer = 1.5f + frand01(rng) * 2.5f;     // no work right now — wait
        n.walking   = false;
        n.velocity  = glm::vec3(0.0f);
    }
}

// Stream farmer NPCs in and out by player proximity. The farm registry + crop
// state live in the FarmDirector; here we just keep 1-2 farmer bodies alive
// near each active field (they flow through the normal NPCState broadcast).
void NpcDirector::updateFarms(const std::vector<DirectorPlayer>& players) {
    const std::vector<FarmField>& farms = farmDir->farms();
    for (int fi = 0; fi < (int)farms.size(); fi++) {
        const FarmField& f = farms[fi];
        float best2 = 1e18f;
        for (const DirectorPlayer& p : players) {
            float dx = p.pos.x - (float)f.center.x, dz = p.pos.z - (float)f.center.y;
            best2 = std::min(best2, dx * dx + dz * dz);
        }
        float act   = (float)f.radius + 120.0f;
        float deact = (float)f.radius + 180.0f;
        bool  pop   = populatedFarms.count(fi) > 0;
        if (!pop && best2 < act * act && (int)active.size() < 220) spawnFarmers(fi);
        else if (pop && best2 > deact * deact)                     despawnFarmers(fi);
    }
}

void NpcDirector::spawnFarmers(int fi) {
    const FarmField& f = farmDir->farms()[fi];
    populatedFarms.insert(fi);
    int count = 1 + (int)(rng() % 2u);   // 1..2 farmers per field
    for (int k = 0; k < count; k++) {
        auto n = std::make_unique<NPC>();
        n->id   = nextFarmerId++;
        n->type = NPCType::Farmer;
        uint32_t aseed = hashU32((uint32_t)f.id ^ (uint32_t)(f.id >> 32), 0xFA12u + (uint32_t)k);
        n->appearanceSeed = aseed ? aseed : 1u;
        n->townIndex = fi;                    // repurposed: which farm this farmer tends
        n->homePos   = glm::vec2((float)f.center.x, (float)f.center.y);
        n->position  = glm::vec3((float)f.center.x + 0.5f, (float)f.cropY, (float)f.center.y + 0.5f);
        n->groundY   = (float)f.cropY;
        n->seatIndex = -1;
        n->idleTimer = frand01(rng) * 1.5f;
        active.push_back(std::move(n));
    }
}

void NpcDirector::despawnFarmers(int fi) {
    populatedFarms.erase(fi);
    for (auto& n : active)
        if (n->type == NPCType::Farmer && n->townIndex == fi) n->dead = true;
}

// --- dungeons --------------------------------------------------------------

void NpcDirector::updateDungeons(const std::vector<DirectorPlayer>& players) {
    const auto& dungeons = getDungeonPlan().dungeons;
    for (size_t di = 0; di < dungeons.size(); di++) {
        const Dungeon& d = *dungeons[di];
        float best2 = 1e18f;
        for (const DirectorPlayer& p : players) {
            float dx = p.pos.x - (float)d.anchor.x, dz = p.pos.z - (float)d.anchor.y;
            best2 = std::min(best2, dx * dx + dz * dz);
        }
        // Scale the stream radius to the dungeon's footprint so a large complex
        // doesn't despawn while the player is still deep inside it.
        float reach = 0.5f * (float)std::max(d.bbMax.x - d.bbMin.x, d.bbMax.y - d.bbMin.y);
        const float ACT = 200.0f + reach, DEACT = ACT + 120.0f;
        bool act = activeDungeons.count(di) > 0;
        if (!act && best2 < ACT * ACT && (int)active.size() < 220) spawnDungeon(di);
        else if (act && best2 > DEACT * DEACT)                     despawnDungeon(di);
    }
}

void NpcDirector::spawnDungeon(size_t di) {
    const auto& dungeons = getDungeonPlan().dungeons;
    if (di >= dungeons.size()) return;
    const Dungeon& d = *dungeons[di];
    activeDungeons.insert(di);
    uint32_t dseed = hashU32((uint32_t)d.anchor.x, (uint32_t)d.anchor.y);
    std::vector<DungeonSpawn> spawns;
    d.fillSpawnTable(spawns, dseed);
    // Cap minions per dungeon for performance (big complexes have many rooms);
    // the boss always spawns regardless of the cap.
    const int DUNGEON_MINION_CAP = 55;
    int minions = 0;
    for (size_t k = 0; k < spawns.size(); k++) {
        if ((int)active.size() >= 220) break;
        const DungeonSpawn& s = spawns[k];
        if (!s.boss && minions >= DUNGEON_MINION_CAP) continue;
        if (!s.boss) minions++;
        auto n = std::make_unique<NPC>();
        n->id   = nextDungeonEnemyId++;
        n->type = (NPCType)s.npcType;
        n->boss = s.boss;
        uint32_t aseed = hashU32((uint32_t)di * 2654435761u + (uint32_t)k, 0xD0E2u);
        n->appearanceSeed = aseed ? aseed : 1u;
        n->townIndex = (int)di;                       // tag the owning dungeon
        n->position  = glm::vec3((float)s.pos.x + 0.5f, (float)s.pos.y, (float)s.pos.z + 0.5f);
        n->groundY   = (float)s.pos.y;
        n->homePos   = glm::vec2((float)s.pos.x, (float)s.pos.z);
        n->health    = defaultNpcHealth((NPCType)s.npcType);
        active.push_back(std::move(n));
    }
}

void NpcDirector::despawnDungeon(size_t di) {
    activeDungeons.erase(di);
    for (auto& n : active)
        if (n->id >= 0xA0000000u && n->id < 0xC0000000u && n->townIndex == (int)di)
            n->dead = true;
}

void NpcDirector::populateTown(int ti) {
    const Town& t = getTownPlan().towns[ti];
    TownNav& nav = navCache[ti];
    if (!nav.ready()) nav.build(t);

    int local = 0;
    // Villager ids are packed as 0x40000000 + ti*128 + local, so a settlement
    // must keep its NPC count well under 128 or it collides with the next
    // town's id range. Large towns therefore house one villager per building
    // and stop spawning once this budget is spent (guards take the remainder up
    // to the 128 ceiling). It also keeps the active-NPC count in check.
    const int VILLAGER_BUDGET = 112;
    const int maxOccupants    = (t.targetHouses > 40) ? 1 : 2;
    for (int bi = 0; bi < (int)t.buildings.size(); bi++) {
        if (local >= VILLAGER_BUDGET) break;
        const TownBuilding& b = t.buildings[bi];
        // Spawn villagers in any building that has interior rooms — houses,
        // pubs, blacksmiths and mage towers all qualify. Centrepieces and
        // farms have empty rooms and are skipped.
        if (b.rooms.empty()) continue;

        // Villagers living in a trade building (tavern, smithy, mage tower,
        // stable, chapel, apothecary, bakery) tend it during the day instead of
        // drifting off to the plaza the way ordinary house-dwellers do.
        const bool isWorkplace = (b.kind >= (int)BuildingKind::Pub &&
                                  b.kind <= (int)BuildingKind::Bakery);

        float cx = (float)b.wx + b.dimX * 0.5f;
        float cz = (float)b.wz + b.dimZ * 0.5f;
        float halfAlong = (b.doorDX != 0) ? b.dimX * 0.5f : b.dimZ * 0.5f;
        glm::vec2 centre(cx, cz);
        glm::vec2 doorDir((float)b.doorDX, (float)b.doorDZ);
        bool hasDoor = (b.doorDX != 0 || b.doorDZ != 0);
        glm::vec2 home   = nav.nearestWalkable(centre + doorDir * (halfAlong + 3.0f));
        glm::vec2 door   = hasDoor ? (centre + doorDir * (halfAlong + 0.5f)) : home;
        glm::vec2 inside = hasDoor ? (centre + doorDir * (halfAlong - 2.5f)) : home;

        // Collect ground-floor bedrooms — these are the preferred sleep spots
        // for each villager, so the night-time routine ends at a bed instead
        // of in the doorway (which kept the door's 4-block trigger active).
        // Upper-floor bedrooms are skipped here: there's no in-house stair
        // pathfinder yet, so we'd walk into the underside of a slab.
        int minFloorY = (1 << 30);
        for (const Room& r : b.rooms)
            if (r.type == RoomType::Bedroom && r.floorY < minFloorY)
                minFloorY = r.floorY;
        std::vector<glm::vec2> bedrooms;
        for (const Room& r : b.rooms) {
            if (r.type != RoomType::Bedroom) continue;
            if (r.floorY > minFloorY + 2) continue;   // ground-floor only
            float rx = (float)b.wx + (float)(r.x0 + r.x1) * 0.5f + 0.5f;
            float rz = (float)b.wz + (float)(r.z0 + r.z1) * 0.5f + 0.5f;
            bedrooms.push_back(glm::vec2(rx, rz));
        }
        // Fallback: a point a few blocks further inward than insidePos so the
        // villager at least clears the door trigger when there's no usable
        // ground-floor bedroom (Tower / Manor / Hall / etc.).
        glm::vec2 bedFallback = inside + doorDir * -4.0f;

        int occupants = 1 + (int)(hashU32((uint32_t)ti * 131u + (uint32_t)bi,
                                          0xA17u) % 2u);
        if (occupants > maxOccupants) occupants = maxOccupants;
        for (int k = 0; k < occupants && local < VILLAGER_BUDGET; k++) {
            auto n = std::make_unique<NPC>();
            n->id   = 0x40000000u + (uint32_t)ti * 128u + (uint32_t)local;
            n->type = NPCType::Villager;
            n->appearanceSeed = hashU32((uint32_t)ti * 977u + (uint32_t)bi * 31u
                                        + (uint32_t)k, 0x5EEDu);
            n->townIndex = ti;
            n->worker    = isWorkplace;
            n->groundY   = (float)t.baseY + 1.0f;
            n->homePos   = home;
            n->doorPos   = door;
            n->insidePos = inside;
            // Different occupants get different bedrooms where possible.
            n->bedPos = bedrooms.empty() ? bedFallback
                                         : bedrooms[k % bedrooms.size()];
            n->position  = glm::vec3(home.x, n->groundY, home.y);
            n->idleTimer = frand01(rng) * 3.0f;
            active.push_back(std::move(n));
            local++;
        }
    }

    // Town guards — patrol the streets and answer trouble. Scaled to settlement
    // size (a sprawling city needs a real watch), capped at 8 so villagers (≤112
    // above) plus guards stay within the per-town 128-id budget.
    int guardCount = std::max(2, std::min(8, 2 + t.targetHouses / 14));
    glm::vec2 centre((float)t.center.x, (float)t.center.y);
    for (int k = 0; k < guardCount; k++) {
        auto g = std::make_unique<NPC>();
        g->id             = 0x40000000u + (uint32_t)ti * 128u + (uint32_t)local;
        g->type           = NPCType::Guard;
        g->appearanceSeed = hashU32((uint32_t)ti * 6151u + (uint32_t)k, 0xA9D2u);
        g->townIndex      = ti;
        g->groundY        = (float)t.baseY + 1.0f;
        g->homePos        = centre;
        float ang = (float)k / (float)guardCount * 6.2831853f;
        // Walled towns post their guards around the wall ring; open villages
        // start them milling near the centre.
        glm::vec2 dir(cosf(ang), sinf(ang));
        glm::vec2 sp = (t.wallRadius > 0)
            ? centre + dir * ((float)t.wallRadius - 3.0f)
            : nav.nearestWalkable(centre + dir * 11.0f);
        g->position  = glm::vec3(sp.x, g->groundY, sp.y);
        g->idleTimer = frand01(rng) * 2.0f;
        active.push_back(std::move(g));
        local++;
    }

    populated.insert(ti);
}

void NpcDirector::depopulateTown(int ti) {
    active.erase(std::remove_if(active.begin(), active.end(),
                     [ti](const std::unique_ptr<NPC>& n) {
                         return n->townIndex == ti;
                     }),
                 active.end());
    populated.erase(ti);
    seatTaken.erase(ti);   // its villagers are gone — free every seat
}

// Plaza bench seats for a town, derived once from the deterministic prop
// placements so villagers sit on the actual benches. getPropPlacements() is a
// pure-data, lazy-once global (no GL), so it's safe to read on the server thread.
const std::vector<SeatSpot>& NpcDirector::townSeats(int ti) {
    auto it = seatCache.find(ti);
    if (it != seatCache.end()) return it->second;

    std::vector<SeatSpot> seats;
    const Town& t = getTownPlan().towns[ti];
    const float cx = (float)t.center.x, cz = (float)t.center.y;
    const float reach = (float)t.radius + 10.0f;
    const float reach2 = reach * reach;
    const float BENCH_SIT_Y = 0.55f;   // seat surface above the bench's base
    for (const PropPlacement& pp : getPropPlacements()) {
        if (pp.type != PropType::Bench) continue;
        float dx = pp.pos.x - cx, dz = pp.pos.z - cz;
        if (dx * dx + dz * dz > reach2) continue;
        seats.push_back({ pp.pos + glm::vec3(0.0f, BENCH_SIT_Y, 0.0f), pp.yaw });
    }
    return seatCache.emplace(ti, std::move(seats)).first->second;
}

void NpcDirector::releaseSeat(NPC& n) {
    if (n.seatIndex < 0) return;
    auto it = seatTaken.find(n.townIndex);
    if (it != seatTaken.end() && n.seatIndex < (int)it->second.size())
        it->second[n.seatIndex] = 0;
    n.seatIndex = -1;
}

void NpcDirector::stepVillager(NPC& n, float dt, World& world, float gameTime) {
    bool night = isNight(gameTime);
    TownNav& nav = navCache[n.townIndex];

    if (!n.sitting) groundSnap(n, world);   // a seated villager stays on the bench

    // Panic — flee from a recent attacker until the fright passes.
    if (n.fleeTimer > 0.0f) {
        n.fleeTimer -= dt;
        if (n.fleeTimer <= 0.0f) {            // calmed down — resume normal life
            n.fleeTimer = 0.0f;
            n.path.clear();
            n.pathIndex = 0;
            n.idleTimer = 0.0f;
            return;
        }
        if (n.pathIndex >= n.path.size()) {
            glm::vec2 cur(n.position.x, n.position.z);
            glm::vec2 away = cur - n.fleeFrom;
            if (glm::length(away) < 0.01f) away = glm::vec2(1.0f, 0.0f);
            away = glm::normalize(away);
            n.path = nav.findPath(cur, nav.nearestWalkable(cur + away * 22.0f));
            n.pathIndex = 0;
            if (n.path.empty()) { n.fleeTimer = 0.0f; return; }
        }
        glm::vec2 wp(n.path[n.pathIndex]);
        glm::vec2 cur(n.position.x, n.position.z);
        glm::vec2 d = wp - cur;
        float dist = glm::length(d);
        if (dist < 0.55f) { n.pathIndex++; return; }
        const float speed = 4.3f;
        glm::vec2 dir = d / dist;
        float stepLen = std::min(speed * dt, dist);
        n.position.x += dir.x * stepLen;
        n.position.z += dir.y * stepLen;
        n.yaw = glm::degrees(atan2f(dir.x, dir.y));
        n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
        n.walking = true;
        return;
    }

    // Routes to the doorstep, then appends straight-line waypoints in through
    // the door and on to the assigned bed. The indoor leg is unguided by
    // TownNav (which treats whole building footprints as blocked) — villagers
    // walk straight to the bed, phasing through any interior partition walls
    // on the way. That's a deliberate simplification until there's a proper
    // in-house pathfinder; the win is that the villager ends up at the bed
    // rather than standing in the doorway keeping the door triggered.
    auto routeHome = [&]() {
        n.path = nav.findPath(glm::vec2(n.position.x, n.position.z), n.homePos);
        if (!n.path.empty()) {
            n.path.push_back(n.doorPos);     // line up on the doorway...
            n.path.push_back(n.insidePos);   // ...step inside...
            n.path.push_back(n.bedPos);      // ...and walk to the bed
        }
        n.pathIndex = 0;
    };

    // React to dusk/dawn at once: head indoors when night falls, resume
    // wandering at dawn — don't wait for the current route to finish.
    if (night && !n.goingHome) {
        n.goingHome  = true;
        n.sitting    = false;          // rise from any bench when dusk falls
        n.pendingAct = 0;
        releaseSeat(n);
        routeHome();
        n.idleTimer = 0.0f;
    } else if (!night && n.goingHome) {
        n.goingHome = false;
        n.path.clear();
        n.pathIndex = 0;
        n.idleTimer = 0.0f;
        // If indoors, step out through the doorway before wandering.
        if (glm::distance(glm::vec2(n.position.x, n.position.z), n.insidePos) < 3.0f) {
            n.path.push_back(n.doorPos);
            n.path.push_back(n.homePos);
        }
    }

    // Reached the end of the route (or have none) — idle, then pick a new goal.
    if (n.pathIndex >= n.path.size()) {
        n.walking  = false;
        n.velocity = glm::vec3(0.0f);
        n.idleTimer -= dt;
        if (n.idleTimer > 0.0f) return;
        if (n.sitting) { n.sitting = false; n.pendingAct = 0; releaseSeat(n); }   // rise from the bench

        glm::vec2 cur(n.position.x, n.position.z);
        if (n.goingHome) {
            // Settled at the bed for the night — just wait it out. The bed
            // is well clear of the door's 4-block trigger, so the door
            // closes properly behind the villager.
            if (glm::distance(cur, n.bedPos) < 2.0f) {
                n.idleTimer = 4.0f + frand01(rng) * 4.0f;
                return;
            }
            routeHome();                       // not at the bed yet — (re)route home
            if (n.path.empty())
                n.idleTimer = 1.0f + frand01(rng) * 2.0f;
            return;
        }
        const Town& t = getTownPlan().towns[n.townIndex];
        glm::vec2 centre((float)t.center.x, (float)t.center.y);

        // By day, make use of the town: sit on a plaza bench, gather around the
        // square, or wander the lanes. pendingAct records what to do on arrival.
        n.pendingAct = 0;
        glm::vec2 goal;
        float roll = frand01(rng);
        const std::vector<SeatSpot>& seats = townSeats(n.townIndex);
        std::vector<uint8_t>& taken = seatTaken[n.townIndex];
        if (taken.size() != seats.size()) taken.assign(seats.size(), 0);

        // Claim a *free* bench so no two villagers share one seat.
        int seat = -1;
        if (!seats.empty() && roll < 0.22f) {
            for (int a = 0; a < 8 && seat < 0; a++) {
                int i = (int)(rng() % seats.size());
                if (!taken[i]) seat = i;
            }
            if (seat < 0)
                for (size_t i = 0; i < seats.size(); i++)
                    if (!taken[i]) { seat = (int)i; break; }
        }
        if (seat >= 0) {
            taken[seat]  = 1;
            n.seatIndex  = seat;
            n.restAnchor = seats[seat].pos;
            n.restYaw    = seats[seat].yaw;
            n.pendingAct = 1;                                   // sit on arrival
            goal = nav.nearestWalkable(glm::vec2(seats[seat].pos.x, seats[seat].pos.z));
        } else if (n.worker && roll < 0.55f) {
            n.pendingAct = 2;                                   // tend the workplace
            goal = nav.randomWalkableNear(n.homePos, 4.0f, rng);
        } else if (roll < 0.74f) {
            n.pendingAct = 2;                                   // linger at the plaza
            goal = nav.randomWalkableNear(
                centre, std::max(8.0f, (float)t.plazaR * 0.7f), rng);
        } else {
            goal = nav.randomWalkableNear(cur, 28.0f, rng);
        }
        n.path = nav.findPath(cur, goal);
        n.pathIndex = 0;
        if (n.path.empty()) {
            releaseSeat(n);                                     // couldn't route — give it back
            n.pendingAct = 0;
            n.idleTimer  = 1.0f + frand01(rng) * 2.0f;          // retry shortly
        }
        return;
    }

    // Walk toward the current waypoint.
    glm::vec2 wp(n.path[n.pathIndex]);
    glm::vec2 cur(n.position.x, n.position.z);
    glm::vec2 d = wp - cur;
    float dist = glm::length(d);
    if (dist < 0.55f) {
        n.pathIndex++;
        if (n.pathIndex >= n.path.size()) {
            n.walking  = false;
            n.velocity = glm::vec3(0.0f);
            if (!night && n.pendingAct == 1) {                 // settle onto the bench
                n.sitting   = true;
                n.position  = n.restAnchor;
                n.groundY   = n.restAnchor.y;
                n.yaw       = n.restYaw;
                n.idleTimer = 8.0f + frand01(rng) * 10.0f;
            } else {
                n.idleTimer = night ? (4.0f + frand01(rng) * 4.0f)
                            : (n.pendingAct == 2 ? (5.0f + frand01(rng) * 7.0f)
                                                 : (2.0f + frand01(rng) * 4.0f));
            }
        }
        return;
    }

    const float speed = 2.2f;
    glm::vec2 dir = d / dist;
    float stepLen = std::min(speed * dt, dist);
    n.position.x += dir.x * stepLen;
    n.position.z += dir.y * stepLen;
    n.yaw = glm::degrees(atan2f(dir.x, dir.y));
    n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
    n.walking = true;
}

// --- bandit camps ----------------------------------------------------------

const Camp& NpcDirector::evalCamp(int gx, int gz, uint64_t key) {
    auto it = campCache.find(key);
    if (it != campCache.end()) return it->second;

    Camp camp;
    uint32_t seed = hashU32((uint32_t)gx * 0x1B873593u + 0x9E3779B9u,
                            (uint32_t)gz * 0xCC9E2D51u) ^ worldSeed();
    if ((seed % 3u) < 2u) {                 // ~66% of survey cells hold a camp
        uint32_t span = (uint32_t)(CAMP_GRID - 80);
        float cxw = (float)(gx * CAMP_GRID + 40 + (int)((seed >> 4)  % span));
        float czw = (float)(gz * CAMP_GRID + 40 + (int)((seed >> 13) % span));
        camp.center      = glm::vec2(cxw, czw);
        camp.banditCount = 3 + (int)((seed >> 22) % 4u);

        // A camp must sit clear of water, towns and highways. The clearances
        // exceed the bandit wander radius, so the group can never reach them.
        bool ok = sampleSurface((int)cxw, (int)czw).height >= WORLD_SEA_LEVEL + 3;
        const TownPlan& plan = getTownPlan();
        if (ok)
            for (const Town& t : plan.towns) {
                float dx = cxw - (float)t.center.x, dz = czw - (float)t.center.y;
                float clr = (float)t.radius + 90.0f;
                if (dx * dx + dz * dz < clr * clr) { ok = false; break; }
            }
        if (ok)
            for (const TownRoad& hw : plan.highways) {
                for (const glm::ivec2& p : hw.pts) {
                    float dx = cxw - (float)p.x, dz = czw - (float)p.y;
                    if (dx * dx + dz * dz < 70.0f * 70.0f) { ok = false; break; }
                }
                if (!ok) break;
            }
        camp.valid = ok;
    }
    return campCache.emplace(key, camp).first->second;
}

void NpcDirector::spawnCamp(uint64_t key, const Camp& camp) {
    float gy = (float)sampleSurface((int)camp.center.x,
                                    (int)camp.center.y).height + 1.0f;
    uint32_t cseed = (uint32_t)(key ^ (key >> 32));
    for (int k = 0; k < camp.banditCount; k++) {
        auto n = std::make_unique<NPC>();
        n->id            = nextBanditId++;
        n->type          = NPCType::Enemy;
        n->appearanceSeed = hashU32(cseed * 49157u + (uint32_t)k, 0xB4D17u);
        n->campKey       = key;
        n->homePos       = camp.center;
        n->groundY       = gy;
        float ang = frand01(rng) * 6.2831853f, r = frand01(rng) * 6.0f;
        n->position = glm::vec3(camp.center.x + cosf(ang) * r, gy,
                                camp.center.y + sinf(ang) * r);
        n->idleTimer = frand01(rng) * 2.0f;
        active.push_back(std::move(n));
    }
    activeCamps.insert(key);
}

void NpcDirector::despawnCamp(uint64_t key) {
    active.erase(std::remove_if(active.begin(), active.end(),
                     [key](const std::unique_ptr<NPC>& n) {
                         return n->type == NPCType::Enemy && n->campKey == key;
                     }),
                 active.end());
    activeCamps.erase(key);
}

void NpcDirector::updateCamps(const std::vector<DirectorPlayer>& players) {
    const float ACT = 200.0f, DEACT = 280.0f;

    for (const DirectorPlayer& p : players) {
        int pgx = (int)floorf(p.pos.x / (float)CAMP_GRID);
        int pgz = (int)floorf(p.pos.z / (float)CAMP_GRID);
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                int gx = pgx + dx, gz = pgz + dz;
                uint64_t key = ((uint64_t)gx << 32) | (uint32_t)gz;
                const Camp& camp = evalCamp(gx, gz, key);
                if (!camp.valid || activeCamps.count(key)) continue;
                if ((int)active.size() >= 220) continue;
                float ex = p.pos.x - camp.center.x, ez = p.pos.z - camp.center.y;
                if (ex * ex + ez * ez < ACT * ACT)
                    spawnCamp(key, camp);
            }
    }

    std::vector<uint64_t> gone;
    for (uint64_t key : activeCamps) {
        const Camp& camp = campCache[key];
        float best2 = 1e18f;
        for (const DirectorPlayer& p : players) {
            float ex = p.pos.x - camp.center.x, ez = p.pos.z - camp.center.y;
            best2 = std::min(best2, ex * ex + ez * ez);
        }
        if (best2 > DEACT * DEACT) gone.push_back(key);
    }
    for (uint64_t key : gone) despawnCamp(key);
}

bool NpcDirector::inAnyTown(glm::vec2 worldXZ) const {
    for (const Town& t : getTownPlan().towns) {
        float dx = worldXZ.x - (float)t.center.x;
        float dz = worldXZ.y - (float)t.center.y;
        if (dx * dx + dz * dz < (float)t.radius * (float)t.radius) return true;
    }
    return false;
}

void NpcDirector::playerHitNpc(uint32_t attackerId, uint32_t npcId,
                               glm::vec3 attackerPos, float damageScale) {
    for (auto& n : active) {
        if (n->id != npcId || n->dyingTimer > 0.0f) continue;
        float dx = n->position.x - attackerPos.x;
        float dz = n->position.z - attackerPos.z;
        if (dx * dx + dz * dz > 5.0f * 5.0f) return;          // out of reach

        if (n->type == NPCType::Villager) {
            n->fleeTimer = 6.0f;                               // panic
            n->fleeFrom  = glm::vec2(attackerPos.x, attackerPos.z);
            n->sitting   = false;                              // leap up from any bench
            n->pendingAct = 0;
            releaseSeat(*n);
        }
        if (n->type == NPCType::Villager || n->type == NPCType::Guard) {
            wantedTimer[attackerId] = 20.0f;                   // a crime — guards respond
            if (inAnyTown(glm::vec2(n->position.x, n->position.z)))
                return;                                        // townsfolk are safe in towns
        }
        n->health -= 25.0f * damageScale;
        if (n->health <= 0.0f) {
            n->health     = 0.0f;
            n->dyingTimer = 2.0f;
            // Spawn server-authoritative loot exactly once per kill. Only
            // bandits (Enemy) drop loot — villagers and guards don't.
            if (!n->lootDropped && isHostileNpc(n->type) && g_server) {
                n->lootDropped = true;
                g_server->spawnLootForKill(attackerId, n->position, n->boss);   // boss → legendary
            }
        }
        return;
    }
}

void NpcDirector::stepBandit(NPC& n, float dt, World& world,
                             const std::vector<DirectorPlayer>& players) {
    groundSnap(n, world);
    if (n.attackCooldown  > 0.0f) n.attackCooldown  -= dt;
    if (n.attackAnimTimer > 0.0f) n.attackAnimTimer -= dt;

    // Per-type combat profile: brutes hit hard and slow, skeletons fast and
    // light, bandits in between.
    float chaseSpeed = 3.4f, dmgPlayer = 7.0f, dmgGuard = 8.0f, atkCd = 1.5f;
    if (n.type == NPCType::Brute)         { chaseSpeed = 2.6f; dmgPlayer = 18.0f; dmgGuard = 16.0f; atkCd = 2.2f; }
    else if (n.type == NPCType::Skeleton) { chaseSpeed = 3.8f; dmgPlayer = 6.0f;  dmgGuard = 7.0f;  atkCd = 1.3f; }

    // Acquire a target: the nearest aggro-range player outside a town, or a town
    // guard that has closed within striking distance — so a raiding or cornered
    // bandit fights the watch back instead of ignoring it.
    const DirectorPlayer* pTarget = nullptr;
    float pBest = 22.0f * 22.0f;
    for (const DirectorPlayer& p : players) {
        if (inAnyTown(glm::vec2(p.pos.x, p.pos.z))) continue;
        if (std::fabs(p.pos.y - n.position.y) > 4.0f) continue;  // no hitting through floors/ceilings
        float dx = p.pos.x - n.position.x, dz = p.pos.z - n.position.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < pBest) { pBest = d2; pTarget = &p; }
    }
    NPC* gTarget = nullptr;
    float gBest = 14.0f * 14.0f;
    for (auto& o : active) {
        if (o->type != NPCType::Guard || o->dyingTimer > 0.0f) continue;
        if (std::fabs(o->position.y - n.position.y) > 4.0f) continue;
        float dx = o->position.x - n.position.x, dz = o->position.z - n.position.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < gBest) { gBest = d2; gTarget = o.get(); }
    }
    // Prefer whichever hostile is closer; the guard's range is tighter, so a
    // guard only wins the contest once it has genuinely closed in.
    const bool hitGuard = gTarget && (!pTarget || gBest < pBest);

    if (pTarget || gTarget) {
        glm::vec2 cur(n.position.x, n.position.z);
        glm::vec2 tpos = hitGuard ? glm::vec2(gTarget->position.x, gTarget->position.z)
                                  : glm::vec2(pTarget->pos.x, pTarget->pos.z);
        glm::vec2 d = tpos - cur;
        float dist = glm::length(d);
        glm::vec2 dir = (dist > 0.01f) ? d / dist : glm::vec2(0.0f, 1.0f);
        n.yaw = glm::degrees(atan2f(dir.x, dir.y));
        if (dist > 2.0f) {                              // close the distance
            float speed   = chaseSpeed;
            float stepLen = std::min(speed * dt, dist - 1.9f);
            if (stepLen > 0.0f) moveNpcXZ(n, world, dir.x * stepLen, dir.y * stepLen);
            n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
            n.walking  = true;
        } else {                                        // in range — strike
            n.velocity = glm::vec3(0.0f);
            n.walking  = false;
            if (n.attackCooldown <= 0.0f) {
                n.attackCooldown  = atkCd;
                n.attackAnimTimer = 0.45f;
                if (hitGuard) {
                    gTarget->health -= dmgGuard;
                    if (gTarget->health <= 0.0f) {
                        gTarget->health     = 0.0f;
                        gTarget->dyingTimer = 2.0f;
                    }
                } else {
                    pendingDamage.push_back({ pTarget->id, dmgPlayer });
                }
            }
        }
        n.path.clear();
        n.pathIndex = 0;
        return;
    }

    // No target — loiter around the camp. Pick a new point when idle expires.
    if (n.pathIndex >= n.path.size()) {
        n.walking  = false;
        n.velocity = glm::vec3(0.0f);
        n.idleTimer -= dt;
        if (n.idleTimer > 0.0f) return;

        // Occasionally march on a nearby settlement so the watch has something to
        // answer. Raids are rare by design — a flavour skirmish, not a siege — and
        // a raider that arrives unopposed soon drifts back toward its camp.
        if (!n.raiding) {
            const Town* rt = nullptr;
            float rBest = RAID_RANGE * RAID_RANGE;
            for (const Town& tt : getTownPlan().towns) {
                float dx = (float)tt.center.x - n.position.x;
                float dz = (float)tt.center.y - n.position.z;
                float d2 = dx * dx + dz * dz;
                if (d2 < rBest) { rBest = d2; rt = &tt; }
            }
            if (rt && frand01(rng) < 0.05f) {
                glm::vec2 c((float)rt->center.x, (float)rt->center.y);
                glm::vec2 dir = c - n.homePos;
                float L = glm::length(dir);
                dir = (L > 0.01f) ? dir / L : glm::vec2(1.0f, 0.0f);
                n.raidTarget = c - dir * ((float)rt->radius + 6.0f);  // halt at the wall
                n.raiding    = true;
            }
        }
        if (n.raiding) {
            glm::vec2 cur(n.position.x, n.position.z);
            if (glm::distance(cur, n.raidTarget) < 5.0f) {
                n.raiding   = false;            // reached the walls — let guards come
                n.idleTimer = 1.5f + frand01(rng) * 2.5f;
                return;
            }
            n.path.clear();
            n.path.push_back(n.raidTarget);
            n.pathIndex = 0;
            return;
        }

        float ang = frand01(rng) * 6.2831853f;
        float r   = 6.0f + frand01(rng) * 22.0f;   // stays within ~28 of the camp
        n.path.clear();
        n.path.push_back(glm::vec2(n.homePos.x + cosf(ang) * r,
                                   n.homePos.y + sinf(ang) * r));
        n.pathIndex = 0;
        return;
    }

    // Walk toward the loiter point.
    glm::vec2 wp(n.path[n.pathIndex]);
    glm::vec2 cur(n.position.x, n.position.z);
    glm::vec2 d = wp - cur;
    float dist = glm::length(d);
    if (dist < 0.6f) {
        n.pathIndex++;
        n.walking  = false;
        n.velocity = glm::vec3(0.0f);
        n.idleTimer = 2.5f + frand01(rng) * 4.0f;
        return;
    }
    const float speed = 2.8f;
    glm::vec2 dir = d / dist;
    float stepLen = std::min(speed * dt, dist);
    moveNpcXZ(n, world, dir.x * stepLen, dir.y * stepLen);
    n.yaw = glm::degrees(atan2f(dir.x, dir.y));
    n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
    n.walking = true;
}

// Ranged caster enemy (cultist): keeps its distance and hurls bolts. Acquires
// a target like a bandit but fires from range instead of closing to melee.
void NpcDirector::stepRangedEnemy(NPC& n, float dt, World& world,
                                  const std::vector<DirectorPlayer>& players) {
    groundSnap(n, world);
    if (n.attackCooldown  > 0.0f) n.attackCooldown  -= dt;
    if (n.attackAnimTimer > 0.0f) n.attackAnimTimer -= dt;

    const float AGGRO = 24.0f, FIRE = 15.0f;
    const DirectorPlayer* tgt = nullptr;
    float best = AGGRO * AGGRO;
    for (const DirectorPlayer& p : players) {
        if (inAnyTown(glm::vec2(p.pos.x, p.pos.z))) continue;
        if (std::fabs(p.pos.y - n.position.y) > 4.0f) continue;  // no casting through floors/ceilings
        float dx = p.pos.x - n.position.x, dz = p.pos.z - n.position.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < best) { best = d2; tgt = &p; }
    }

    // Only engage a target the caster can actually see; otherwise it holds its
    // room and loiters. Approaching only happens along a clear line, so it never
    // shoots — or charges — through a wall.
    if (tgt && hasLineOfSight(world, n.position, tgt->pos)) {
        glm::vec2 cur(n.position.x, n.position.z);
        glm::vec2 d(tgt->pos.x - cur.x, tgt->pos.z - cur.y);
        float dist = glm::length(d);
        glm::vec2 dir = (dist > 0.01f) ? d / dist : glm::vec2(0.0f, 1.0f);
        n.yaw = glm::degrees(atan2f(dir.x, dir.y));
        if (dist > FIRE) {                              // close to firing range
            float speed = 2.6f;
            float stepLen = std::min(speed * dt, dist - FIRE + 0.5f);
            if (stepLen > 0.0f) moveNpcXZ(n, world, dir.x * stepLen, dir.y * stepLen);
            n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
            n.walking  = true;
        } else {                                        // in range — cast a bolt
            n.velocity = glm::vec3(0.0f);
            n.walking  = false;
            if (n.attackCooldown <= 0.0f) {
                n.attackCooldown  = 2.0f;
                n.attackAnimTimer = 0.6f;               // drives the cast pose on clients
                pendingDamage.push_back({ tgt->id, 9.0f });
            }
        }
        n.path.clear(); n.pathIndex = 0;
        return;
    }

    // No target — wander around the home anchor.
    if (n.pathIndex >= n.path.size()) {
        n.walking = false; n.velocity = glm::vec3(0.0f);
        n.idleTimer -= dt;
        if (n.idleTimer > 0.0f) return;
        float ang = frand01(rng) * 6.2831853f, r = 5.0f + frand01(rng) * 16.0f;
        n.path.clear();
        n.path.push_back(glm::vec2(n.homePos.x + cosf(ang) * r, n.homePos.y + sinf(ang) * r));
        n.pathIndex = 0;
        return;
    }
    glm::vec2 wp(n.path[n.pathIndex]);
    glm::vec2 cur(n.position.x, n.position.z);
    glm::vec2 d = wp - cur;
    float dist = glm::length(d);
    if (dist < 0.6f) {
        n.pathIndex++; n.walking = false; n.velocity = glm::vec3(0.0f);
        n.idleTimer = 2.0f + frand01(rng) * 3.0f;
        return;
    }
    const float speed = 2.4f;
    glm::vec2 dir = d / dist;
    moveNpcXZ(n, world, dir.x * speed * dt, dir.y * speed * dt);
    n.yaw = glm::degrees(atan2f(dir.x, dir.y));
    n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
    n.walking = true;
}

// --- town guards -----------------------------------------------------------

void NpcDirector::stepGuard(NPC& n, float dt, World& world,
                            const std::vector<DirectorPlayer>& players) {
    groundSnap(n, world);
    if (n.attackCooldown  > 0.0f) n.attackCooldown  -= dt;
    if (n.attackAnimTimer > 0.0f) n.attackAnimTimer -= dt;
    TownNav& nav = navCache[n.townIndex];

    // Find the nearest hostile — a bandit, or a player wanted for a crime.
    float     bestD2    = 25.0f * 25.0f;
    NPC*      banditTgt = nullptr;
    uint32_t  playerTgt = 0;
    glm::vec2 hostileXZ(0.0f);
    for (auto& o : active) {
        if (!isHostileNpc(o->type) || o->dyingTimer > 0.0f) continue;
        float dx = o->position.x - n.position.x, dz = o->position.z - n.position.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < bestD2) {
            bestD2 = d2; banditTgt = o.get(); playerTgt = 0;
            hostileXZ = glm::vec2(o->position.x, o->position.z);
        }
    }
    for (const DirectorPlayer& p : players) {
        auto it = wantedTimer.find(p.id);
        if (it == wantedTimer.end() || it->second <= 0.0f) continue;
        float dx = p.pos.x - n.position.x, dz = p.pos.z - n.position.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < bestD2) {
            bestD2 = d2; banditTgt = nullptr; playerTgt = p.id;
            hostileXZ = glm::vec2(p.pos.x, p.pos.z);
        }
    }

    if (banditTgt || playerTgt != 0) {
        glm::vec2 cur(n.position.x, n.position.z);
        glm::vec2 d = hostileXZ - cur;
        float dist = glm::length(d);
        glm::vec2 dir = (dist > 0.01f) ? d / dist : glm::vec2(0.0f, 1.0f);
        n.yaw = glm::degrees(atan2f(dir.x, dir.y));
        if (dist > 2.0f) {                              // close the distance
            const float speed = 3.6f;
            float stepLen = std::min(speed * dt, dist - 1.9f);
            if (stepLen > 0.0f) {
                n.position.x += dir.x * stepLen;
                n.position.z += dir.y * stepLen;
            }
            n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
            n.walking  = true;
        } else {                                        // in range — strike
            n.velocity = glm::vec3(0.0f);
            n.walking  = false;
            if (n.attackCooldown <= 0.0f) {
                n.attackCooldown  = 1.4f;
                n.attackAnimTimer = 0.45f;
                if (banditTgt) {
                    banditTgt->health -= 20.0f;
                    if (banditTgt->health <= 0.0f) {
                        banditTgt->health     = 0.0f;
                        banditTgt->dyingTimer = 2.0f;
                    }
                } else {
                    pendingDamage.push_back({ playerTgt, 6.0f });
                }
            }
        }
        n.path.clear();
        n.pathIndex = 0;
        return;
    }

    // No trouble — patrol. In a walled town the watch laps the wall perimeter;
    // an open village is wandered at random.
    if (n.pathIndex >= n.path.size()) {
        n.walking  = false;
        n.velocity = glm::vec3(0.0f);
        n.idleTimer -= dt;
        if (n.idleTimer > 0.0f) return;
        const Town& t = getTownPlan().towns[n.townIndex];
        glm::vec2 centre((float)t.center.x, (float)t.center.y);
        if (t.wallRadius > 0) {
            // March the next arc of the ring just inside the wall. a0 is the
            // guard's current bearing from the centre, so it continues from
            // wherever it is; all guards step the same way (counter-clockwise)
            // so they trail each other round the wall rather than meet head-on.
            float pr  = (float)t.wallRadius - 3.0f;
            glm::vec2 rel = glm::vec2(n.position.x, n.position.z) - centre;
            float a0   = (glm::length(rel) > 0.5f) ? atan2f(rel.y, rel.x) : 0.0f;
            float step = 8.0f / pr;                 // ~8 blocks between waypoints
            n.path.clear();
            for (int k = 1; k <= 8; k++) {
                float a = a0 + step * (float)k;
                n.path.push_back(glm::vec2(centre.x + cosf(a) * pr,
                                           centre.y + sinf(a) * pr));
            }
            n.pathIndex = 0;
            return;
        }
        glm::vec2 goal = nav.randomWalkableNear(centre, (float)t.radius * 0.9f, rng);
        n.path = nav.findPath(glm::vec2(n.position.x, n.position.z), goal);
        n.pathIndex = 0;
        if (n.path.empty())
            n.idleTimer = 1.0f + frand01(rng) * 2.0f;
        return;
    }
    glm::vec2 wp(n.path[n.pathIndex]);
    glm::vec2 cur(n.position.x, n.position.z);
    glm::vec2 d = wp - cur;
    float dist = glm::length(d);
    if (dist < 0.55f) {
        n.pathIndex++;
        if (n.pathIndex >= n.path.size()) {
            n.walking  = false;
            n.velocity = glm::vec3(0.0f);
            n.idleTimer = 1.5f + frand01(rng) * 3.0f;
        }
        return;
    }
    const float speed = 2.4f;
    glm::vec2 dir = d / dist;
    float stepLen = std::min(speed * dt, dist);
    n.position.x += dir.x * stepLen;
    n.position.z += dir.y * stepLen;
    n.yaw = glm::degrees(atan2f(dir.x, dir.y));
    n.velocity = glm::vec3(dir.x * speed, 0.0f, dir.y * speed);
    n.walking = true;
}

// --- procedural identity ---------------------------------------------------

std::string npcName(uint32_t seed) {
    static const char* kNames[] = {
        "Aldric", "Bryn", "Cora", "Doran", "Elsa", "Finn", "Greta", "Hew",
        "Isolde", "Joren", "Kara", "Loris", "Mabel", "Nessa", "Orrin", "Petra",
        "Quinn", "Rowan", "Sable", "Tomas", "Una", "Vance", "Wrenna", "Yorick",
        "Bula", "Cedric", "Dilla", "Embry", "Fenn", "Hazel",
    };
    const int n = (int)(sizeof(kNames) / sizeof(kNames[0]));
    return kNames[seed % (uint32_t)n];
}

std::string npcFlavorLine(uint32_t seed, int variant) {
    static const char* kLines[] = {
        "Lovely weather we're having today.",
        "Welcome, traveller. Mind the road after dark.",
        "I've lived in this village my whole life.",
        "The harvest has been kind to us this year.",
        "Have you stopped by the well yet?",
        "My cousin lives in the next village over.",
        "Strange folk have been seen out past the fields.",
        "Best be home before nightfall, friend.",
        "The ferryman will see you across the water.",
        "Nothing ever changes around here, and I like it.",
        "Careful on the highway, but it's safe enough.",
        "Good day to you. Safe travels.",
    };
    const int n = (int)(sizeof(kLines) / sizeof(kLines[0]));
    int i = (int)((seed / 7u + (uint32_t)variant) % (uint32_t)n);
    return kLines[i];
}
