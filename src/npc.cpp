#include "npc.h"
#include "voxel_model.h"
#include "town.h"
#include "world.h"
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

static constexpr int CAMP_GRID = 256;   // bandit-camp survey cell size, blocks

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
    if (type == NPCType::Enemy) {
        rig->armorType = 2;          // brown leather — a rough bandit look
        rig->applyCustomization();
    } else if (type == NPCType::Guard) {
        rig->armorType = 3;          // steel — a town guard
        rig->applyCustomization();
    }
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
        if (attackFlag && !prevAttackFlag) {
            rig->isAttacking = true;
            rig->attackAnim  = 0.0f;
        }
        prevAttackFlag = attackFlag;
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

    for (auto& n : active) {
        // A dying NPC plays its fall-over, then is swept from the world.
        if (n->dyingTimer > 0.0f) {
            n->dyingTimer -= dt;
            n->walking  = false;
            n->velocity = glm::vec3(0.0f);
            if (n->dyingTimer <= 0.0f) n->dead = true;
            continue;
        }
        if      (n->type == NPCType::Enemy) stepBandit(*n, dt, world, players);
        else if (n->type == NPCType::Guard) stepGuard(*n, dt, world, players);
        else                                stepVillager(*n, dt, world, gameTime);
    }

    active.erase(std::remove_if(active.begin(), active.end(),
                     [](const std::unique_ptr<NPC>& n) { return n->dead; }),
                 active.end());
}

void NpcDirector::populateTown(int ti) {
    const Town& t = getTownPlan().towns[ti];
    TownNav& nav = navCache[ti];
    if (!nav.ready()) nav.build(t);

    int local = 0;
    for (int bi = 0; bi < (int)t.buildings.size(); bi++) {
        const TownBuilding& b = t.buildings[bi];
        // Spawn villagers in any building that has interior rooms — houses,
        // pubs, blacksmiths and mage towers all qualify. Centrepieces and
        // farms have empty rooms and are skipped.
        if (b.rooms.empty()) continue;

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
        for (int k = 0; k < occupants; k++) {
            auto n = std::make_unique<NPC>();
            n->id   = 0x40000000u + (uint32_t)ti * 128u + (uint32_t)local;
            n->type = NPCType::Villager;
            n->appearanceSeed = hashU32((uint32_t)ti * 977u + (uint32_t)bi * 31u
                                        + (uint32_t)k, 0x5EEDu);
            n->townIndex = ti;
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

    // Town guards — patrol the streets and answer trouble.
    int guardCount = (t.size == TownSize::Town) ? 4 : 2;
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
        glm::vec2 sp = nav.nearestWalkable(centre +
                                           glm::vec2(cosf(ang), sinf(ang)) * 11.0f);
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
}

void NpcDirector::stepVillager(NPC& n, float dt, World& world, float gameTime) {
    bool night = isNight(gameTime);
    TownNav& nav = navCache[n.townIndex];

    groundSnap(n, world);   // plant on the surface (terrain or engraved paths)

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
        n.goingHome = true;
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
        glm::vec2 goal = (frand01(rng) < 0.4f)
            ? nav.randomWalkableNear(centre, t.radius * 0.5f, rng)
            : nav.randomWalkableNear(cur, 28.0f, rng);
        n.path = nav.findPath(cur, goal);
        n.pathIndex = 0;
        if (n.path.empty())
            n.idleTimer = 1.0f + frand01(rng) * 2.0f;   // retry shortly
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
            n.idleTimer = night ? (4.0f + frand01(rng) * 4.0f)
                                : (2.0f + frand01(rng) * 4.0f);
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
                               glm::vec3 attackerPos) {
    for (auto& n : active) {
        if (n->id != npcId || n->dyingTimer > 0.0f) continue;
        float dx = n->position.x - attackerPos.x;
        float dz = n->position.z - attackerPos.z;
        if (dx * dx + dz * dz > 5.0f * 5.0f) return;          // out of reach

        if (n->type == NPCType::Villager) {
            n->fleeTimer = 6.0f;                               // panic
            n->fleeFrom  = glm::vec2(attackerPos.x, attackerPos.z);
        }
        if (n->type == NPCType::Villager || n->type == NPCType::Guard) {
            wantedTimer[attackerId] = 20.0f;                   // a crime — guards respond
            if (inAnyTown(glm::vec2(n->position.x, n->position.z)))
                return;                                        // townsfolk are safe in towns
        }
        n->health -= 25.0f;
        if (n->health <= 0.0f) {
            n->health     = 0.0f;
            n->dyingTimer = 2.0f;
        }
        return;
    }
}

void NpcDirector::stepBandit(NPC& n, float dt, World& world,
                             const std::vector<DirectorPlayer>& players) {
    groundSnap(n, world);
    if (n.attackCooldown  > 0.0f) n.attackCooldown  -= dt;
    if (n.attackAnimTimer > 0.0f) n.attackAnimTimer -= dt;

    // Hunt the nearest player in aggro range that isn't safe inside a town.
    const DirectorPlayer* target = nullptr;
    float bestD2 = 22.0f * 22.0f;
    for (const DirectorPlayer& p : players) {
        if (inAnyTown(glm::vec2(p.pos.x, p.pos.z))) continue;
        float dx = p.pos.x - n.position.x, dz = p.pos.z - n.position.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < bestD2) { bestD2 = d2; target = &p; }
    }

    if (target) {
        glm::vec2 cur(n.position.x, n.position.z);
        glm::vec2 d(target->pos.x - cur.x, target->pos.z - cur.y);
        float dist = glm::length(d);
        glm::vec2 dir = (dist > 0.01f) ? d / dist : glm::vec2(0.0f, 1.0f);
        n.yaw = glm::degrees(atan2f(dir.x, dir.y));
        if (dist > 2.0f) {                              // close the distance
            float speed   = 3.4f;
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
                n.attackCooldown  = 1.5f;
                n.attackAnimTimer = 0.45f;
                pendingDamage.push_back({ target->id, 7.0f });
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
    n.position.x += dir.x * stepLen;
    n.position.z += dir.y * stepLen;
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
        if (o->type != NPCType::Enemy || o->dyingTimer > 0.0f) continue;
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

    // No trouble — patrol the town.
    if (n.pathIndex >= n.path.size()) {
        n.walking  = false;
        n.velocity = glm::vec3(0.0f);
        n.idleTimer -= dt;
        if (n.idleTimer > 0.0f) return;
        const Town& t = getTownPlan().towns[n.townIndex];
        glm::vec2 centre((float)t.center.x, (float)t.center.y);
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
