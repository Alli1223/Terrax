#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "renderer.h"
#include "app_context.h"
#include "game_types.h"
#include "atlas.h"
#include "town.h"
#include "prop_placement.h"
#include "projectile.h"   // for collectProjectileLights / MagicBoltProjectile
#include "npc.h"          // for NPC + getRig() in collectLanternLights
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

static const float SKY_VERTS[] = {
    -1,-1,-1,  1,-1,-1,  1, 1,-1,  1, 1,-1, -1, 1,-1, -1,-1,-1,
    -1,-1, 1, -1, 1, 1,  1, 1, 1,  1, 1, 1,  1,-1, 1, -1,-1, 1,
    -1, 1, 1, -1, 1,-1,  1, 1,-1,  1, 1,-1,  1, 1, 1, -1, 1, 1,
    -1,-1,-1, -1,-1, 1,  1,-1,-1,  1,-1,-1, -1,-1, 1,  1,-1, 1,
     1,-1,-1,  1,-1, 1,  1, 1, 1,  1, 1, 1,  1, 1,-1,  1,-1,-1,
    -1,-1,-1, -1, 1,-1, -1, 1, 1, -1, 1, 1, -1,-1, 1, -1,-1,-1,
};

static float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float sunElevation(float t) {
    return sinf((t - 0.25f) * 2.0f * 3.14159f) / sqrtf(1.0625f);
}

struct LanternLightList {
    int count = 0;
    glm::vec3 pos[MAX_LANTERNS];
    float intensity[MAX_LANTERNS];
    float radius[MAX_LANTERNS];
    // Per-light colour — defaults to warm lantern orange. Coloured
    // sources (magic bolts: fire = orange, ice = blue, arcane = violet)
    // override this when they append themselves to the list.
    glm::vec3 color[MAX_LANTERNS];
};

// Standard tungsten-lantern colour used by held lanterns, props,
// streetlamps, and town campfires. Magic-bolt lights set their own.
static constexpr glm::vec3 LANTERN_DEFAULT_COLOR = glm::vec3(1.00f, 0.76f, 0.40f);
// Warmer, redder cast for open hearth flame.
static constexpr glm::vec3 FIRE_COLOR = glm::vec3(1.00f, 0.52f, 0.22f);
// Softer, warmer amber for static town pools (house lanterns, street lamps) so
// lit streets glow cosily rather than glaring a hard white-gold.
static constexpr glm::vec3 POOL_COLOR = glm::vec3(1.00f, 0.70f, 0.36f);

static glm::vec3 lanternWorldPos(const glm::vec3& feetPos, float yaw, bool held) {
    glm::vec3 fwd   = glm::vec3(sinf(glm::radians(yaw)), 0.0f, cosf(glm::radians(yaw)));
    glm::vec3 right = glm::vec3(cosf(glm::radians(yaw)), 0.0f, -sinf(glm::radians(yaw)));
    return held
        ? feetPos + glm::vec3(0.0f, 1.35f, 0.0f) - right * 0.28f + fwd * 0.15f
        : feetPos + glm::vec3(0.0f, 0.78f, 0.0f) - right * 0.22f + fwd * 0.12f;
}

static void addLantern(LanternLightList& lights, const glm::vec3& feetPos, float yaw,
                       bool held, float flicker) {
    if (lights.count >= MAX_LANTERNS) return;
    int i = lights.count++;
    lights.pos[i]       = lanternWorldPos(feetPos, yaw, held);
    lights.intensity[i] = (held ? 0.9f : 0.4f) * flicker;
    lights.radius[i]    = held ? 22.0f : 12.0f;   // a little broader = softer pool
    lights.color[i]     = LANTERN_DEFAULT_COLOR;
}

// Generic "point light" appender — used for non-lantern emitters such
// as in-flight magic bolts. Caller passes their own colour/intensity/
// radius. Silently drops if the per-frame light list is already full
// (lanterns get priority since they were appended first).
static void addColoredLight(LanternLightList& lights, const glm::vec3& pos,
                             const glm::vec3& color, float intensity,
                             float radius) {
    if (lights.count >= MAX_LANTERNS) return;
    int i = lights.count++;
    lights.pos[i]       = pos;
    lights.intensity[i] = intensity;
    lights.radius[i]    = radius;
    lights.color[i]     = color;
}

// Per-light flicker. Each lantern / lamp / campfire gets its own rate-and-
// phase set hashed from its world XZ, so neighbouring lights no longer pulse
// in lockstep — some breathe slowly, some flutter quickly, and the row of
// lamps down a street looks alive instead of synchronised. Returns a small
// brightness multiplier hovering around 1.0.
static float perLightFlicker(float t, float seedX, float seedZ) {
    uint32_t h = (uint32_t)((int)(seedX * 13.0f) * 0x9E3779B1u)
               ^ (uint32_t)((int)(seedZ * 17.0f) * 0x85EBCA77u)
               ^ 0xCABBA6Eu;
    auto u01 = [&](int shift) {
        return (float)((h >> shift) & 0xFFu) / 255.0f;
    };
    float r0 = 0.65f + u01(0)  * 0.70f;     // 0.65 .. 1.35
    float r1 = 0.65f + u01(8)  * 0.70f;
    float r2 = 0.65f + u01(16) * 0.70f;
    float p0 = u01(24)              * 6.2831853f;
    float p1 = u01(4)               * 6.2831853f;
    float p2 = u01(12)              * 6.2831853f;
    return 1.0f
         + 0.11f * std::sin(t * (6.8f  * r0) + p0)
         + 0.07f * std::sin(t * (11.2f * r1) + p1)
         + 0.04f * std::sin(t * (18.5f * r2) + p2);
}

static void collectLanternLights(const AppContext& ctx, float flicker, LanternLightList& lights) {
    lights.count = 0;
    addLantern(lights, ctx.camera.position, ctx.playerYaw, ctx.lanternHeld, flicker);
    for (const auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        addLantern(lights, p.position, p.yaw, p.lanternHeld, flicker);
    }
    // NPC lanterns — walk the object manager and add lights for any
    // NPC whose rig was tagged `hasLantern` by `applyNpcThemedLoadout`
    // (some villagers, most guards, never bandits). Always belt-mode
    // (never raised), so we pass `held = false`.
    for (const auto& obj : ctx.objectManager.objects()) {
        if (obj->dead || obj->kind != ObjectKind::NPC) continue;
        auto* n = static_cast<NPC*>(obj.get());
        const BipedalRig* r = n->getRig();
        if (!r || !r->hasLantern) continue;
        addLantern(lights, n->position, n->yaw, /*held=*/false, flicker);
    }

    // Indoor hearths glow day AND night (interiors stay dark by daylight);
    // house lanterns, street lamps and campfires only kick in after dark.
    float sunY        = sunElevation(ctx.gameTime);
    float nightFactor = 1.0f - smoothstep(-0.08f, 0.12f, sunY);
    const bool night  = nightFactor > 0.01f;

    // Collected nearest-first so distant lights drop off the fixed-size list.
    const glm::vec3 cam = ctx.camera.position;
    // Gather town lights out to ~16 chunks so a whole nearby town stays lit at
    // night. Candidates are sorted nearest-first below and the closest
    // MAX_LANTERNS kept (player/NPC lanterns were already added above).
    const float LIGHT_RANGE = 16.0f * (float)CHUNK_SIZE;   // 256 blocks (> 15 chunks)
    const float COLLECT2     = LIGHT_RANGE * LIGHT_RANGE;
    const float t            = ctx.flickerTime;
    struct Cand { glm::vec3 pos; float intensity, radius, d2; glm::vec3 color; };
    std::vector<Cand> cand;

    for (const PropPlacement& pp : getPropPlacements()) {
        glm::vec3 lp;
        float intensity = 0.0f, radius = 0.0f;
        glm::vec3 color = LANTERN_DEFAULT_COLOR;
        if (pp.type == PropType::Fireplace) {
            lp        = pp.pos + glm::vec3(0.0f, 0.70f, 0.0f);
            intensity = 0.70f * perLightFlicker(t, pp.pos.x, pp.pos.z);
            radius    = 15.0f;
            color     = FIRE_COLOR;
        } else if (night && pp.type == PropType::Lantern) {
            lp        = pp.pos + glm::vec3(0.0f, 0.45f, 0.0f);
            intensity = 0.55f * perLightFlicker(t, pp.pos.x, pp.pos.z) * nightFactor;
            radius    = 25.0f;                     // wider, gentler pool
            color     = POOL_COLOR;
        } else if (night && pp.type == PropType::StreetLamp) {
            lp        = pp.pos + glm::vec3(0.0f, 3.15f, 0.0f);
            intensity = 0.58f * perLightFlicker(t, pp.pos.x, pp.pos.z) * nightFactor;
            radius    = 30.0f;
            color     = POOL_COLOR;
        } else {
            continue;
        }
        float dx = lp.x - cam.x, dz = lp.z - cam.z;
        float d2 = dx * dx + dz * dz;
        if (d2 > COLLECT2) continue;
        cand.push_back({ lp, intensity, radius, d2, color });
    }
    // Town campfires glow after dark too.
    if (night)
        for (const Town& t2 : getTownPlan().towns) {
            if (t2.centerpiece != TownCenter::Campfire) continue;
            glm::vec3 lp((float)t2.center.x + 0.5f, (float)t2.baseY + 2.5f,
                         (float)t2.center.y + 0.5f);
            float dx = lp.x - cam.x, dz = lp.z - cam.z;
            float d2 = dx * dx + dz * dz;
            if (d2 > COLLECT2) continue;
            cand.push_back({ lp,
                             0.75f * perLightFlicker(t, lp.x, lp.z) * nightFactor,
                             28.0f, d2, LANTERN_DEFAULT_COLOR });
        }
    std::sort(cand.begin(), cand.end(),
              [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
    for (const Cand& c : cand) {
        if (lights.count >= MAX_LANTERNS) break;
        int i = lights.count++;
        lights.pos[i]       = c.pos;
        lights.intensity[i] = c.intensity;
        lights.radius[i]    = c.radius;
        lights.color[i]     = c.color;
    }
}

// Walk the ObjectManager and add a point light for every magic-bolt
// projectile in flight. The bolt's element drives the colour so a fire
// bolt washes its surroundings in orange, an ice bolt in cool blue,
// arcane in violet. Lights persist only for the frame each bolt
// exists — they're added/removed from the per-frame list naturally.
static void collectProjectileLights(const AppContext& ctx, LanternLightList& lights) {
    for (const auto& obj : ctx.objectManager.objects()) {
        if (obj->dead || obj->kind != ObjectKind::Projectile) continue;
        auto* bolt = dynamic_cast<MagicBoltProjectile*>(obj.get());
        if (!bolt) continue;
        Voxel pri = bolt->trailColorA;
        // Saturate the colour slightly so the light reads as glowing
        // rather than tinted-off-white. Lantern shader applies the
        // colour multiplicatively so darker colours just dim.
        glm::vec3 col(pri.r / 255.0f, pri.g / 255.0f, pri.b / 255.0f);
        addColoredLight(lights, bolt->position, col, 0.85f, 9.0f);
    }
}

static void bindLanternLights(const Shader& shader, const LanternLightList& lights,
                              const glm::ivec3& volOrigin, int volSize, int volTexUnit) {
    shader.setLanternLights(lights.count, lights.pos, lights.intensity,
                             lights.radius, lights.color);
    shader.setInt("u_lightVol", volTexUnit);
    shader.setVec3("u_lightVolOrigin", glm::vec3(volOrigin));
    shader.setFloat("u_lightVolSize", (float)volSize);
}

void Renderer::setupSkybox() {
    glGenVertexArrays(1, &skyVAO);
    glGenBuffers(1, &skyVBO);
    glBindVertexArray(skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(SKY_VERTS), SKY_VERTS, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

bool Renderer::init(int width, int height) {
    chunkShader  = Shader("shaders/chunk.vert",  "shaders/chunk.frag");
    waterShader  = Shader("shaders/water.vert",  "shaders/water.frag");
    skyShader    = Shader("shaders/sky.vert",    "shaders/sky.frag");
    charShader   = Shader("shaders/char.vert",   "shaders/char.frag");
    shadowShader = Shader("shaders/shadow.vert", "shaders/shadow.frag");
    glassShader  = Shader("shaders/glass.vert",  "shaders/glass.frag");
    weatherShader = Shader("shaders/weather.vert", "shaders/weather.frag");
    postShader    = Shader("shaders/post.vert",    "shaders/post.frag");
    vegetationShader = Shader("shaders/vegetation.vert", "shaders/vegetation.frag");
    ambientShader = Shader("shaders/ambient.vert", "shaders/ambient.frag");

    setupSkybox();
    atlasTexture = generateAtlas();

    // Particle geometry buffer (dynamic, re-uploaded each frame)
    glGenVertexArrays(1, &particleVao);
    glGenBuffers(1, &particleVbo);
    glBindVertexArray(particleVao);
    glBindBuffer(GL_ARRAY_BUFFER, particleVbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nx));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, materialID));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, skyLight));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, blockLight));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, shoreDistance));
    glEnableVertexAttribArray(6);
    glBindVertexArray(0);

    // Voxel-death-particle buffer — same vertex layout as the character
    // shader expects (pos + normal + rgba). Re-uploaded each frame.
    struct VxVtx { float px, py, pz; float nx, ny, nz; float r, g, b, a; };
    glGenVertexArrays(1, &voxelDeathVao);
    glGenBuffers(1, &voxelDeathVbo);
    glBindVertexArray(voxelDeathVao);
    glBindBuffer(GL_ARRAY_BUFFER, voxelDeathVbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VxVtx),
                          (void*)offsetof(VxVtx, px));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VxVtx),
                          (void*)offsetof(VxVtx, nx));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(VxVtx),
                          (void*)offsetof(VxVtx, r));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // Shadow map framebuffer
    glGenFramebuffers(1, &shadowFBO);
    glGenTextures(1, &shadowMapTex);
    glBindTexture(GL_TEXTURE_2D, shadowMapTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float shadowBorder[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, shadowBorder);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMapTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Water reflection framebuffer
    glGenFramebuffers(1, &reflFBO);
    glGenTextures(1, &reflColorTex);
    glBindTexture(GL_TEXTURE_2D, reflColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &reflDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, reflDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, reflFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, reflColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, reflDepthRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Scene framebuffer — the world renders here so the post pass can add
    // volumetric light shafts. Depth is a texture so the post shader can
    // reconstruct world positions and bound the ray march.
    glGenFramebuffers(1, &sceneFBO);
    glGenTextures(1, &sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &sceneDepthTex);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, sceneDepthTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenVertexArrays(1, &postVao);   // empty VAO for the fullscreen post pass

    return true;
}

void Renderer::resizeFramebuffers(int width, int height) {
    if (width <= 0 || height <= 0 || reflColorTex == 0) return;
    glBindTexture(GL_TEXTURE_2D, reflColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, reflDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::renderSkybox(const glm::mat4& view, const glm::mat4& proj,
                              float gameTime, float time, float weather) {
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    skyShader.use();
    skyShader.setMat4("view", glm::mat4(glm::mat3(view)));
    skyShader.setMat4("projection", proj);
    skyShader.setFloat("timeOfDay", gameTime);
    skyShader.setFloat("time", time);
    skyShader.setFloat("u_weather", weather);
    glBindVertexArray(skyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}

void Renderer::renderEditorCharacter(AppContext& ctx, const glm::mat4& model,
                                      const glm::mat4& view, const glm::mat4& proj) {
    charShader.use();
    charShader.setMat4("projection", proj);
    charShader.setMat4("view", view);
    charShader.setVec3("u_sunDir", glm::normalize(glm::vec3(0.5f, 1.0f, 0.4f)));
    charShader.setFloat("sunFactor", 1.0f);
    charShader.setVec3("skyAmbient", glm::vec3(0.85f, 0.90f, 1.00f));
    charShader.setLanternLights(0, nullptr, nullptr, nullptr, nullptr);
    charShader.setFloat("u_alpha", 1.0f);
    charShader.setFloat("u_skyExposure", 1.0f);
    charShader.setVec3("camPos", glm::vec3(glm::inverse(view)[3]));
    charShader.setFloat("u_weather", 0.0f);
    // Shadow disabled: map all fragments outside clip-space so calcShadow returns 0
    glm::mat4 editorLSM = glm::mat4(0.0f);
    editorLSM[3][2] = 2.0f;
    editorLSM[3][3] = 1.0f;
    charShader.setMat4("lightSpaceMatrix", editorLSM);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowMapTex);
    charShader.setInt("shadowMap", 1);

    if (ctx.playerRig) {
        ctx.playerRig->update(ctx.deltaTime, 0.0f);
        ctx.playerRig->draw(model, glGetUniformLocation(charShader.id, "model"));
    }
}

void Renderer::renderEditorHouse(AppContext& ctx, const glm::mat4& model,
                                  const glm::mat4& view, const glm::mat4& proj) {
    if (!ctx.houseModel || !ctx.houseModel->volume) return;

    charShader.use();
    charShader.setMat4("projection", proj);
    charShader.setMat4("view", view);
    charShader.setVec3("u_sunDir", glm::normalize(glm::vec3(0.5f, 1.0f, 0.4f)));
    charShader.setFloat("sunFactor", 1.0f);
    charShader.setVec3("skyAmbient", glm::vec3(0.85f, 0.90f, 1.00f));
    charShader.setLanternLights(0, nullptr, nullptr, nullptr, nullptr);
    charShader.setFloat("u_alpha", 1.0f);
    charShader.setFloat("u_skyExposure", 1.0f);
    charShader.setVec3("camPos", glm::vec3(glm::inverse(view)[3]));
    charShader.setFloat("u_weather", 0.0f);
    // Shadow disabled: map all fragments outside clip-space so calcShadow returns 0
    glm::mat4 editorLSM = glm::mat4(0.0f);
    editorLSM[3][2] = 2.0f;
    editorLSM[3][3] = 1.0f;
    charShader.setMat4("lightSpaceMatrix", editorLSM);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowMapTex);
    charShader.setInt("shadowMap", 1);

    ctx.houseModel->volume->updateMesh();
    glUniformMatrix4fv(glGetUniformLocation(charShader.id, "model"), 1, GL_FALSE, &model[0][0]);
    ctx.houseModel->volume->draw();
}

// Model matrix that places the house design at the live placement-preview spot.
static glm::mat4 housePreviewMatrix(const AppContext& ctx) {
    const HouseModel* hm = ctx.houseModel;
    glm::vec3 ctr((hm->boundMin.x + hm->boundMax.x + 1) * 0.5f,
                  (float)hm->boundMin.y,
                  (hm->boundMin.z + hm->boundMax.z + 1) * 0.5f);
    glm::mat4 m = glm::translate(glm::mat4(1.0f),
        glm::vec3(floorf(ctx.housePreviewPos.x) + 0.5f,
                  floorf(ctx.housePreviewPos.y),
                  floorf(ctx.housePreviewPos.z) + 0.5f));
    m = glm::rotate(m, glm::radians(ctx.housePreviewYaw), glm::vec3(0, 1, 0));
    m = glm::translate(m, -ctr);
    return m;
}

// Casts backward from the player and returns how far the third-person camera
// can sit before a solid block would come between it and the player.
static float cameraClipDistance(const World& world, const glm::vec3& base,
                                const glm::vec3& backDir, float desired) {
    const float margin  = 0.30f;   // keep the camera off the wall surface
    const float step    = 0.25f;
    const float minDist = 0.50f;
    for (float t = step; t <= desired; t += step) {
        glm::vec3 sp = base + backDir * t;
        BlockType b = world.getBlock((int)floorf(sp.x), (int)floorf(sp.y), (int)floorf(sp.z));
        if (b != BlockType::Air && b != BlockType::Water)
            return std::max(t - margin, minDist);
    }
    return desired;
}

// Sky-light exposure (0..1) at a character's position — drives how brightly the
// character is lit, so someone standing inside a house renders dark like the room.
static float skyExposureAt(const World& world, const glm::vec3& feetPos) {
    int wx = (int)floorf(feetPos.x);
    int wy = (int)floorf(feetPos.y) + 1;
    int wz = (int)floorf(feetPos.z);
    return world.getSkyLight(wx, wy, wz) / 15.0f;
}

// Rebuilds the 3D opacity texture of the blocks around the player when needed.
// The shaders raymarch this so dynamic point lights are blocked by walls.
void Renderer::updateLightVolume(AppContext& ctx) {
    const int S = LIGHTVOL_SIZE;
    if (!lightVolTex) {
        glGenTextures(1, &lightVolTex);
        glBindTexture(GL_TEXTURE_3D, lightVolTex);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, S, S, S, 0,
                     GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
    glm::ivec3 desired((int)floorf(ctx.camera.position.x) - S / 2,
                       (int)floorf(ctx.camera.position.y) - S / 2,
                       (int)floorf(ctx.camera.position.z) - S / 2);
    lightVolTimer += ctx.deltaTime;
    glm::ivec3 d = desired - lightVolOrigin;
    bool moved = std::abs(d.x) >= 16 || std::abs(d.y) >= 16 || std::abs(d.z) >= 16;

    // A shut door occludes light through its doorway; a change in which doors
    // are shut also forces a rebuild so the leak tracks the swing.
    uint32_t doorSig = 0;
    for (const auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::Door) continue;
        Door* dr = static_cast<Door*>(o.get());
        if (dr->blocksLight()) doorSig = doorSig * 31u + dr->placementIndex + 1u;
    }
    static uint32_t lastDoorSig = 0;
    if (!moved && lightVolTimer < 1.5f && doorSig == lastDoorSig) return;
    lastDoorSig = doorSig;

    lightVolOrigin = desired;
    lightVolTimer  = 0.0f;
    static std::vector<unsigned char> buf;
    buf.resize((size_t)S * S * S);
    ctx.world.fillOpacityVolume(buf.data(), S, desired.x, desired.y, desired.z);

    // Stamp shut doors opaque so interior light cannot leak past them.
    for (const auto& o : ctx.objectManager.objects()) {
        if (o->dead || o->kind != ObjectKind::Door) continue;
        Door* dr = static_cast<Door*>(o.get());
        if (!dr->blocksLight()) continue;
        int by = (int)o->position.y;
        for (int tt = -1; tt <= 1; tt++) {
            int wx = dr->wallCell.x + tt * dr->wallDir.x;
            int wz = dr->wallCell.y + tt * dr->wallDir.y;
            for (int dyy = 0; dyy < 4; dyy++) {
                int tx = wx - desired.x, ty = by + dyy - desired.y, tz = wz - desired.z;
                if (tx < 0 || tx >= S || ty < 0 || ty >= S || tz < 0 || tz >= S) continue;
                buf[((size_t)tz * S + ty) * S + tx] = 255;
            }
        }
    }

    glBindTexture(GL_TEXTURE_3D, lightVolTex);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, S, S, S,
                    GL_RED, GL_UNSIGNED_BYTE, buf.data());
    glBindTexture(GL_TEXTURE_3D, 0);
}

void Renderer::renderWorld(AppContext& ctx, GLFWwindow* window, float currentTime) {
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    float aspect = fbW / (float)fbH;

    glm::mat4 proj = glm::perspective(glm::radians(ctx.camera.fov), aspect, 0.1f, 1000.0f);

    // Third-person camera with wall clipping: pull the camera in so a solid
    // block never sits between it and the player. Snap inward immediately,
    // ease back outward so leaving a tight space isn't jarring.
    glm::vec3 camBase    = ctx.camera.position + glm::vec3(0, 1.6f, 0);
    float     targetDist = cameraClipDistance(ctx.world, camBase, -ctx.camera.front, ctx.camDist);
    if (targetDist < ctx.camDistSmooth)
        ctx.camDistSmooth = targetDist;
    else
        ctx.camDistSmooth += (targetDist - ctx.camDistSmooth)
                           * std::min(1.0f, ctx.deltaTime * 8.0f);

    glm::vec3 eyePos = camBase - ctx.camera.front * ctx.camDistSmooth;
    glm::vec3 lookAt = ctx.camera.position + glm::vec3(0, 1.2f, 0);

    // Over-the-shoulder offset while drawing a bow — both the camera
    // and the lookAt slide laterally so the player isn't blocking the
    // crosshair (and so they can see what they're aiming at). The
    // forward direction stays the same so `camera.front` still aims
    // straight ahead through the reticle.
    if (ctx.bowChargingHeld) {
        // Use the smoothed charge value for a gentle swoop rather than
        // an instant snap. 1.4 blocks at full draw is enough to clear
        // the player's silhouette without feeling like a cinematic.
        float shift = ctx.bowCharge * 1.4f;
        glm::vec3 lateral = ctx.camera.right * shift;
        eyePos += lateral;
        lookAt += lateral;
    }

    glm::mat4 view = glm::lookAt(eyePos, lookAt, ctx.camera.worldUp);

    // Expose to UI for nametags
    frameView  = view;
    frameProj  = proj;
    frameEyePos = eyePos;
    frameFbW   = fbW;
    frameFbH   = fbH;

    // --- Lighting ---
    float sunY     = sunElevation(ctx.gameTime);
    float dayness  = smoothstep(-0.12f, 0.22f, sunY);
    float dawnDusk = smoothstep(-0.30f, 0.0f, sunY) * (1.0f - smoothstep(0.0f, 0.30f, sunY));
    float sunFactor = 0.04f + 0.96f * dayness;
    glm::vec3 dayAmb(0.70f, 0.84f, 1.00f), dawnAmb(1.00f, 0.58f, 0.24f), nightAmb(0.18f, 0.22f, 0.50f);
    glm::vec3 skyAmbient = glm::mix(nightAmb, glm::mix(dayAmb, dawnAmb, dawnDusk), dayness);

    float sunAngle = (ctx.gameTime - 0.25f) * 2.0f * 3.14159f;
    glm::vec3 sunDir = glm::normalize(glm::vec3(
        cosf(sunAngle) * 0.6f, sinf(sunAngle), 0.35f));

    // --- Weather mood (storm-driven) ---
    // Clear weather keeps the current daytime brightness; as a storm rolls in
    // the sun dims and the scene floods with cold, flat overcast light.
    float weather = std::clamp(ctx.weatherIntensity, 0.0f, 1.0f);
    sunFactor *= 1.0f - 0.66f * weather;
    glm::vec3 overcastAmb = glm::vec3(0.30f, 0.33f, 0.40f) * (0.32f + 0.68f * dayness);
    skyAmbient = glm::mix(skyAmbient, overcastAmb, weather * 0.80f);

    // --- Per-area mood ---
    // Tint the ambient toward the local biome's character so each region of the
    // world is lit differently — warm bleached deserts, cold blue tundra, green
    // humid jungle, crisp mountains. The tint eases over a couple of seconds so
    // biome borders don't snap, and because the fog colour below is derived from
    // skyAmbient, the haze picks up the same mood.
    {
        static const glm::vec3 BIOME_TINT[7] = {
            glm::vec3(1.00f, 1.00f, 1.00f),   // Plains    — neutral
            glm::vec3(0.94f, 1.02f, 0.93f),   // Forest    — soft green
            glm::vec3(1.10f, 1.02f, 0.86f),   // Desert    — warm, sun-bleached
            glm::vec3(0.96f, 1.00f, 1.07f),   // Mountains — cool, crisp
            glm::vec3(0.92f, 0.99f, 1.10f),   // Tundra    — cold blue
            glm::vec3(1.10f, 1.01f, 0.84f),   // Savanna   — golden, dry
            glm::vec3(0.87f, 1.02f, 0.90f),   // Jungle    — lush green
        };
        const glm::vec3& cp = ctx.camera.position;
        int b = sampleSurface((int)cp.x, (int)cp.z).biome;
        glm::vec3 target = (b >= 0 && b < 7) ? BIOME_TINT[b] : glm::vec3(1.0f);
        areaTint = glm::mix(areaTint, target, glm::min(1.0f, ctx.deltaTime * 0.6f));
        skyAmbient *= areaTint;
    }

    // --- Lantern ---
    ctx.flickerTime += ctx.deltaTime;
    float flicker = 1.0f
        + 0.08f * sinf(ctx.flickerTime * 7.3f)
        + 0.05f * sinf(ctx.flickerTime * 11.7f + 0.5f)
        + 0.03f * sinf(ctx.flickerTime * 19.1f + 1.2f);
    LanternLightList lanternLights;
    collectLanternLights(ctx, flicker, lanternLights);
    // Transient point lights from in-flight magic projectiles — added
    // every frame, removed naturally when the bolt dies.
    collectProjectileLights(ctx, lanternLights);

    // Rebuild the occlusion volume and bind it to texture unit 2 for the frame.
    updateLightVolume(ctx);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, lightVolTex);

    // Characters and other objects are advanced in updateGameplay (the local
    // player + the ObjectManager); the renderer only draws them.

    // --- Reflection pass ---
    {
        glm::vec3 lookAt     = ctx.camera.position + glm::vec3(0, 1.2f, 0);
        glm::vec3 reflEye    = glm::vec3(eyePos.x,  2.0f * WATER_Y - eyePos.y,  eyePos.z);
        glm::vec3 reflTarget = glm::vec3(lookAt.x,  2.0f * WATER_Y - lookAt.y,  lookAt.z);
        glm::mat4 reflView   = glm::lookAt(reflEye, reflTarget, -ctx.camera.worldUp);
        waterShader.use();
        waterShader.setMat4("u_reflProjView", proj * reflView);

        glViewport(0, 0, fbW, fbH);
        glBindFramebuffer(GL_FRAMEBUFFER, reflFBO);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderSkybox(reflView, proj, ctx.gameTime, currentTime, weather);
        glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
        glEnable(GL_CLIP_DISTANCE0);
        chunkShader.use();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlasTexture);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shadowMapTex);
        chunkShader.setInt("atlas", 0); chunkShader.setInt("shadowMap", 1);
        chunkShader.setMat4("model",           glm::mat4(1.0f));
        chunkShader.setMat4("view",            reflView);
        chunkShader.setMat4("projection",      proj);
        chunkShader.setMat4("lightSpaceMatrix",glm::mat4(1.0f));
        chunkShader.setFloat("sunFactor",      sunFactor);
        chunkShader.setVec3("skyAmbient",      skyAmbient);
        chunkShader.setVec3("camPos",          reflEye);
        chunkShader.setVec3("u_sunDir",        sunDir);
        bindLanternLights(chunkShader, lanternLights, lightVolOrigin, LIGHTVOL_SIZE, 2);
        chunkShader.setFloat("u_weather", weather);
        chunkShader.setFloat("u_snowAmount",
            (ctx.weatherKind == 1) ? weather : 0.0f);
        chunkShader.setVec4("u_clipPlane", glm::vec4(0.0f, 1.0f, 0.0f, -WATER_Y));
        ctx.world.drawAll();
        glDisable(GL_CLIP_DISTANCE0); glCullFace(GL_BACK); glEnable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // --- Shadow pass ---
    glm::vec3 shadowDir = glm::normalize(glm::vec3(sunDir.x, glm::max(sunDir.y, 0.25f), sunDir.z));
    glm::vec3 lightEye  = ctx.camera.position + shadowDir * 70.0f;
    glm::vec3 lightUp   = (fabsf(shadowDir.y) > 0.95f) ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
    glm::mat4 lightView = glm::lookAt(lightEye, ctx.camera.position, lightUp);
    glm::mat4 lightProj = glm::ortho(-38.0f, 38.0f, -38.0f, 38.0f, 1.0f, 200.0f);
    glm::mat4 lightSpaceMat = lightProj * lightView;

    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_FRONT);
    shadowShader.use();
    shadowShader.setMat4("lightSpaceMatrix", lightSpaceMat);
    shadowShader.setMat4("model", glm::mat4(1.0f));
    ctx.world.drawAll();
    {
        GLuint sml = glGetUniformLocation(shadowShader.id, "model");
        if (ctx.localPlayer) ctx.localPlayer->draw(sml);
        ctx.objectManager.drawAll(sml);
    }
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);

    // --- Main pass (into the scene framebuffer so the post pass can run) ---
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderSkybox(view, proj, ctx.gameTime, currentTime, weather);
    glEnable(GL_CULL_FACE);

    // Terrain
    chunkShader.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlasTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shadowMapTex);
    chunkShader.setInt("atlas", 0); chunkShader.setInt("shadowMap", 1);
    chunkShader.setMat4("model",           glm::mat4(1.0f));
    chunkShader.setMat4("view",            view);
    chunkShader.setMat4("projection",      proj);
    chunkShader.setMat4("lightSpaceMatrix",lightSpaceMat);
    chunkShader.setFloat("sunFactor",      sunFactor);
    chunkShader.setVec3("skyAmbient",      skyAmbient);
    chunkShader.setVec3("camPos",          eyePos);
    chunkShader.setVec3("u_sunDir",        sunDir);
    bindLanternLights(chunkShader, lanternLights, lightVolOrigin, LIGHTVOL_SIZE, 2);
    chunkShader.setFloat("u_weather",   weather);
    // Snow tint only takes effect in snow biomes (Mountains, Tundra). In rain
    // biomes the same weatherIntensity drives rain particles + atmospheric
    // wash but leaves roof colours alone.
    chunkShader.setFloat("u_snowAmount",
        (ctx.weatherKind == 1) ? weather : 0.0f);
    chunkShader.setVec4("u_clipPlane", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    ctx.world.drawAll();

    // Characters
    charShader.use();
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shadowMapTex);
    charShader.setInt("shadowMap", 1);
    charShader.setMat4("view",            view);
    charShader.setMat4("projection",      proj);
    charShader.setMat4("lightSpaceMatrix",lightSpaceMat);
    charShader.setFloat("sunFactor",      sunFactor);
    charShader.setVec3("skyAmbient",      skyAmbient);
    charShader.setVec3("u_sunDir",        sunDir);
    charShader.setVec3("camPos",          eyePos);
    charShader.setFloat("u_weather",      weather);
    bindLanternLights(charShader, lanternLights, lightVolOrigin, LIGHTVOL_SIZE, 2);
    charShader.setFloat("time", currentTime);
    charShader.setFloat("u_alpha", 1.0f);
    {
        GLuint ml    = glGetUniformLocation(charShader.id, "model");
        GLint  seLoc = glGetUniformLocation(charShader.id, "u_skyExposure");
        GLint  swLoc = glGetUniformLocation(charShader.id, "u_sway");
        glUniform1f(swLoc, 0.0f);                 // player + everything rigid by default
        if (ctx.localPlayer) {
            glUniform1f(seLoc, skyExposureAt(ctx.world, ctx.localPlayer->position));
            ctx.localPlayer->draw(ml);
        }
        for (const auto& o : ctx.objectManager.objects()) {
            if (o->dead) continue;
            // Bushes catch the wind like the grass; everything else stays rigid.
            float sway = 0.0f;
            if (o->kind == ObjectKind::Prop) {
                PropType pt = static_cast<const Prop*>(o.get())->type;
                if (pt == PropType::Bush      || pt == PropType::BushFlowering ||
                    pt == PropType::BushBerry || pt == PropType::BushConifer   ||
                    pt == PropType::BushDry)
                    sway = 0.006f;
            }
            glUniform1f(swLoc, sway);
            glUniform1f(seLoc, skyExposureAt(ctx.world, o->position));
            o->draw(ml);
        }
        glUniform1f(swLoc, 0.0f);                 // reset before the death-particle batch

        // Voxel death-explosion particles — small cubes flung out when an
        // enemy dies, then settling on the ground. One draw call for the
        // whole swarm, geometry rebuilt each frame.
        if (!ctx.voxelParticles.empty()) {
            struct VxVtx { float px, py, pz; float nx, ny, nz; float r, g, b, a; };
            static std::vector<VxVtx> verts;
            verts.clear();
            verts.reserve(ctx.voxelParticles.size() * 36);

            // Six face quads worth of (offset, normal) per unit cube.
            // We expand each particle into 36 verts (2 tris per face).
            struct Face { float n[3]; float o[6][3]; };
            static const Face faces[6] = {
                // +X
                {{ 1, 0, 0}, {{1,0,0},{1,1,0},{1,1,1},{1,0,0},{1,1,1},{1,0,1}}},
                // -X
                {{-1, 0, 0}, {{0,0,1},{0,1,1},{0,1,0},{0,0,1},{0,1,0},{0,0,0}}},
                // +Y
                {{ 0, 1, 0}, {{0,1,1},{1,1,1},{1,1,0},{0,1,1},{1,1,0},{0,1,0}}},
                // -Y
                {{ 0,-1, 0}, {{0,0,0},{1,0,0},{1,0,1},{0,0,0},{1,0,1},{0,0,1}}},
                // +Z
                {{ 0, 0, 1}, {{1,0,1},{1,1,1},{0,1,1},{1,0,1},{0,1,1},{0,0,1}}},
                // -Z
                {{ 0, 0,-1}, {{0,0,0},{0,1,0},{1,1,0},{0,0,0},{1,1,0},{1,0,0}}},
            };
            for (const auto& p : ctx.voxelParticles) {
                float fade = (p.life < 1.2f) ? (p.life / 1.2f) : 1.0f;
                float a    = fade;
                float r    = p.color.r / 255.0f;
                float g    = p.color.g / 255.0f;
                float b    = p.color.b / 255.0f;
                float s    = p.size;
                for (const auto& f : faces) {
                    for (int v = 0; v < 6; v++) {
                        VxVtx vx;
                        vx.px = p.pos.x + (f.o[v][0] - 0.5f) * s;
                        vx.py = p.pos.y + (f.o[v][1]       ) * s;
                        vx.pz = p.pos.z + (f.o[v][2] - 0.5f) * s;
                        vx.nx = f.n[0]; vx.ny = f.n[1]; vx.nz = f.n[2];
                        vx.r = r; vx.g = g; vx.b = b; vx.a = a;
                        verts.push_back(vx);
                    }
                }
            }

            // Particles are emissive-ish — disable backface culling so the
            // tiny cubes never go invisible if the camera straddles them.
            glDisable(GL_CULL_FACE);
            glUniform1f(seLoc, 1.0f);
            glm::mat4 identity(1.0f);
            glUniformMatrix4fv(ml, 1, GL_FALSE, &identity[0][0]);
            glBindVertexArray(voxelDeathVao);
            glBindBuffer(GL_ARRAY_BUFFER, voxelDeathVbo);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(verts.size() * sizeof(VxVtx)),
                         verts.data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
            glBindVertexArray(0);
            glEnable(GL_CULL_FACE);
        }
    }

    // House placement ghost — translucent preview that follows the player.
    if (ctx.housePreviewActive && ctx.houseModel && ctx.houseModel->volume) {
        glm::mat4 ghostM = housePreviewMatrix(ctx);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        charShader.setFloat("u_alpha", 0.42f);
        charShader.setFloat("u_skyExposure", 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(charShader.id, "model"),
                           1, GL_FALSE, &ghostM[0][0]);
        ctx.houseModel->volume->draw();
        charShader.setFloat("u_alpha", 1.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // Vegetation — detailed procedural ground cover, two-sided (no culling)
    glDisable(GL_CULL_FACE);
    vegetationShader.use();
    vegetationShader.setMat4("view", view);
    vegetationShader.setMat4("projection", proj);
    vegetationShader.setFloat("sunFactor", sunFactor);
    vegetationShader.setVec3("skyAmbient", skyAmbient);
    vegetationShader.setVec3("camPos", eyePos);
    vegetationShader.setVec3("u_sunDir", sunDir);
    vegetationShader.setLanternLights(lanternLights.count, lanternLights.pos,
                                       lanternLights.intensity, lanternLights.radius,
                                       lanternLights.color);
    vegetationShader.setFloat("u_weather", weather);
    vegetationShader.setFloat("time", currentTime);
    // Players bend nearby vegetation as they move through it.
    {
        glm::vec3 disturb[8];
        int dn = 0;
        if (ctx.localPlayer) disturb[dn++] = ctx.localPlayer->position;
        for (const auto& [id, rp] : ctx.remotePlayers) {
            if (dn >= 8) break;
            (void)id;
            disturb[dn++] = rp.position;
        }
        vegetationShader.setInt("u_disturbCount", dn);
        if (dn > 0)
            glUniform3fv(glGetUniformLocation(vegetationShader.id, "u_disturbPos"),
                         dn, &disturb[0][0]);
    }
    ctx.world.drawAllFoliage();

    // Leaf particles — tiny coloured cubes, drawn with the chunk shader
    if (!ctx.leafParticles.empty()) {
        chunkShader.use();
        chunkShader.setFloat("time", currentTime);
        static std::vector<Vertex> pv;
        pv.clear();
        pv.reserve(ctx.leafParticles.size() * 36);

        for (const auto& lp : ctx.leafParticles) {
            TileID tile;
            switch ((BlockType)lp.leafBT) {
                case BlockType::LeavesOrange: tile = TileID::LeavesOrange; break;
                case BlockType::LeavesRed:    tile = TileID::LeavesRed;    break;
                case BlockType::LeavesPink:   tile = TileID::LeavesPink;   break;
                default:                       tile = TileID::Leaves;        break;
            }
            float u0, v0, u1, v1;
            tileUV(tile, u0, v0, u1, v1);
            const float H  = 0.1f;
            const float sl = 0.85f;
            const glm::vec3& p = lp.pos;

            auto pv6 = [&](float dx, float dy, float dz,
                           float nx, float ny, float nz, float u, float v) {
                pv.push_back({p.x+dx*H, p.y+dy*H, p.z+dz*H,
                              nx, ny, nz, u, v, 0.f, sl, 0.f, 0.f});
            };
            // +Y
            pv6(-1,1,-1, 0,1,0,u0,v0); pv6(-1,1,1, 0,1,0,u0,v1); pv6(1,1,1, 0,1,0,u1,v1);
            pv6(-1,1,-1, 0,1,0,u0,v0); pv6(1,1,1, 0,1,0,u1,v1);  pv6(1,1,-1, 0,1,0,u1,v0);
            // -Y
            pv6(-1,-1,1, 0,-1,0,u0,v0); pv6(-1,-1,-1, 0,-1,0,u0,v1); pv6(1,-1,-1, 0,-1,0,u1,v1);
            pv6(-1,-1,1, 0,-1,0,u0,v0); pv6(1,-1,-1, 0,-1,0,u1,v1);  pv6(1,-1,1, 0,-1,0,u1,v0);
            // +X
            pv6(1,-1,-1, 1,0,0,u0,v0); pv6(1,1,-1, 1,0,0,u0,v1); pv6(1,1,1, 1,0,0,u1,v1);
            pv6(1,-1,-1, 1,0,0,u0,v0); pv6(1,1,1, 1,0,0,u1,v1);  pv6(1,-1,1, 1,0,0,u1,v0);
            // -X
            pv6(-1,-1,1, -1,0,0,u0,v0); pv6(-1,1,1, -1,0,0,u0,v1); pv6(-1,1,-1, -1,0,0,u1,v1);
            pv6(-1,-1,1, -1,0,0,u0,v0); pv6(-1,1,-1, -1,0,0,u1,v1); pv6(-1,-1,-1, -1,0,0,u1,v0);
            // +Z
            pv6(1,-1,1, 0,0,1,u0,v0); pv6(1,1,1, 0,0,1,u0,v1); pv6(-1,1,1, 0,0,1,u1,v1);
            pv6(1,-1,1, 0,0,1,u0,v0); pv6(-1,1,1, 0,0,1,u1,v1); pv6(-1,-1,1, 0,0,1,u1,v0);
            // -Z
            pv6(-1,-1,-1, 0,0,-1,u0,v0); pv6(-1,1,-1, 0,0,-1,u0,v1); pv6(1,1,-1, 0,0,-1,u1,v1);
            pv6(-1,-1,-1, 0,0,-1,u0,v0); pv6(1,1,-1, 0,0,-1,u1,v1);  pv6(1,-1,-1, 0,0,-1,u1,v0);
        }

        glBindVertexArray(particleVao);
        glBindBuffer(GL_ARRAY_BUFFER, particleVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pv.size() * sizeof(Vertex)),
                     pv.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)pv.size());
        glBindVertexArray(0);
    }

    glEnable(GL_CULL_FACE);

    // Glass — translucent block faces, drawn after opaque geometry
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glassShader.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlasTexture);
    glassShader.setInt("atlas", 0);
    glassShader.setMat4("model",      glm::mat4(1.0f));
    glassShader.setMat4("view",       view);
    glassShader.setMat4("projection", proj);
    glassShader.setFloat("sunFactor", sunFactor);
    glassShader.setVec3("skyAmbient", skyAmbient);
    glassShader.setVec3("camPos",     eyePos);
    glassShader.setVec3("u_sunDir",   sunDir);
    glassShader.setFloat("u_weather", weather);
    ctx.world.drawAllGlass();
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);

    // Water
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    waterShader.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlasTexture);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, reflColorTex);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_3D, lightVolTex);
    waterShader.setInt("atlas", 0); waterShader.setInt("u_reflTex", 2);
    waterShader.setMat4("model",       glm::mat4(1.0f));
    waterShader.setMat4("view",        view);
    waterShader.setMat4("projection",  proj);
    waterShader.setFloat("sunFactor",  sunFactor);
    waterShader.setVec3("skyAmbient",  skyAmbient);
    waterShader.setVec3("camPos",      eyePos);
    waterShader.setFloat("time",       currentTime);
    waterShader.setFloat("timeOfDay",  ctx.gameTime);
    waterShader.setVec3("u_sunDir",    sunDir);
    waterShader.setFloat("u_weather",  weather);
    bindLanternLights(waterShader, lanternLights, lightVolOrigin, LIGHTVOL_SIZE, 3);
    ctx.world.drawAllWater();
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);

    // --- Weather particles (rain / snow) ---
    if (!ctx.weatherParticles.empty()) {
        bool snow = (ctx.weatherKind == 1);

        // Billboard axes from the view matrix: rain hangs vertically along
        // world-up, snow faces the camera fully.
        glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
        glm::vec3 camUp   (view[0][1], view[1][1], view[2][1]);
        glm::vec3 ax = camRight * (snow ? 0.075f : 0.055f);
        glm::vec3 ay = (snow ? camUp : glm::vec3(0.0f, 1.0f, 0.0f))
                     * (snow ? 0.075f : 0.55f);

        static std::vector<Vertex> wv;
        wv.clear();
        wv.reserve(ctx.weatherParticles.size() * 6);
        auto wvtx = [&](const glm::vec3& p, float u, float vv, float a) {
            wv.push_back({ p.x, p.y, p.z, 0.f, 0.f, 0.f, u, vv, 0.f, a, 0.f, 0.f });
        };
        for (const auto& wp : ctx.weatherParticles) {
            float a = (snow ? 0.85f : 0.60f)
                    * std::clamp(wp.life * 0.8f, 0.0f, 1.0f);
            glm::vec3 c  = wp.pos;
            glm::vec3 bl = c - ax - ay, br = c + ax - ay,
                      tr = c + ax + ay, tl = c - ax + ay;
            wvtx(bl, 0.f, 0.f, a); wvtx(br, 1.f, 0.f, a); wvtx(tr, 1.f, 1.f, a);
            wvtx(bl, 0.f, 0.f, a); wvtx(tr, 1.f, 1.f, a); wvtx(tl, 0.f, 1.f, a);
        }

        weatherShader.use();
        weatherShader.setMat4("view",       view);
        weatherShader.setMat4("projection", proj);
        weatherShader.setInt("u_kind", snow ? 1 : 0);
        glm::vec3 base = snow ? glm::vec3(0.95f, 0.97f, 1.00f)
                              : glm::vec3(0.66f, 0.73f, 0.84f);
        glm::vec3 tint = base * glm::clamp(skyAmbient * (0.45f + 0.75f * sunFactor),
                                           0.16f, 1.25f);
        weatherShader.setVec3("u_tint", tint);

        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
        glBindVertexArray(particleVao);
        glBindBuffer(GL_ARRAY_BUFFER, particleVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(wv.size() * sizeof(Vertex)),
                     wv.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)wv.size());
        glBindVertexArray(0);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
    }

    // --- Ambient particles (pollen by day, fireflies + embers by night) ---
    if (!ctx.ambientParticles.empty()) {
        glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
        glm::vec3 camUp   (view[0][1], view[1][1], view[2][1]);

        // Two lists: glowing motes (additive) and soft grey smoke (alpha-over).
        // Vertex packing: position xyz, colour in the normal slot, billboard UV
        // in u/v, per-particle alpha in skyLight.
        static std::vector<Vertex> av, smv;
        av.clear(); smv.clear();
        auto emit = [&](std::vector<Vertex>& out, const glm::vec3& c,
                        const glm::vec3& col, float size, float a) {
            glm::vec3 ax = camRight * size, ay = camUp * size;
            glm::vec3 bl = c - ax - ay, br = c + ax - ay,
                      tr = c + ax + ay, tl = c - ax + ay;
            auto pv = [&](const glm::vec3& p, float u, float vv) {
                out.push_back({ p.x, p.y, p.z, col.r, col.g, col.b,
                                u, vv, 0.f, a, 0.f, 0.f });
            };
            pv(bl, 0.f, 0.f); pv(br, 1.f, 0.f); pv(tr, 1.f, 1.f);
            pv(bl, 0.f, 0.f); pv(tr, 1.f, 1.f); pv(tl, 0.f, 1.f);
        };
        for (const auto& ap : ctx.ambientParticles) {
            float t = (ap.maxLife - ap.life) + ap.seed;
            float fadeIn  = std::min((ap.maxLife - ap.life) / 0.8f, 1.0f);
            float fadeOut = std::min(ap.life / 1.5f, 1.0f);
            float fade    = std::max(0.0f, std::min(1.0f, fadeIn * fadeOut));
            if (ap.kind == 3) {                        // smoke — soft, alpha-blended
                float a = 0.30f * fade;
                if (a > 0.0f) emit(smv, ap.pos, ap.color, ap.size, a);
                continue;
            }
            float baseA = 0.45f, pulse = 1.0f;
            if (ap.kind == 1) {
                baseA = 0.95f;
                pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf(t * 4.2f));
            } else if (ap.kind == 2) {
                baseA = 0.95f;
                pulse = 1.0f - (ap.maxLife - ap.life) / ap.maxLife * 0.65f;
                if (pulse < 0.0f) pulse = 0.0f;
            }
            float alpha = baseA * pulse * fade;
            if (alpha > 0.0f) emit(av, ap.pos, ap.color, ap.size, alpha);
        }
        if (!av.empty() || !smv.empty()) {
            ambientShader.use();
            ambientShader.setMat4("view",       view);
            ambientShader.setMat4("projection", proj);
            glEnable(GL_BLEND); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
            glBindVertexArray(particleVao);
            glBindBuffer(GL_ARRAY_BUFFER, particleVbo);
            if (!smv.empty()) {                        // smoke first, under the glints
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(smv.size() * sizeof(Vertex)),
                             smv.data(), GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)smv.size());
            }
            if (!av.empty()) {                         // glowing motes: additive
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(av.size() * sizeof(Vertex)),
                             av.data(), GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)av.size());
            }
            glBindVertexArray(0);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
        }
    }

    // --- Post-processing: volumetric light shafts + vignette ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    postShader.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, shadowMapTex);
    postShader.setInt("u_scene", 0);
    postShader.setInt("u_depth", 1);
    postShader.setInt("u_shadowMap", 2);
    postShader.setMat4("u_invViewProj", glm::inverse(proj * view));
    postShader.setMat4("u_lightSpace", lightSpaceMat);
    postShader.setVec3("u_camPos", eyePos);
    postShader.setVec3("u_sunDir", sunDir);
    // Warm shaft colour (warmer at dawn/dusk); rays fade out at night and as a
    // storm's overcast hides the sun.
    float rayStrength = glm::clamp(sunFactor * 1.05f - 0.05f, 0.0f, 1.0f)
                      * (1.0f - 0.75f * weather);
    postShader.setFloat("u_rayStrength", rayStrength);
    postShader.setVec3("u_rayColor",
        glm::mix(glm::vec3(1.00f, 0.95f, 0.82f), glm::vec3(1.00f, 0.72f, 0.40f), dawnDusk));
    // Distance fog: fade the world into the horizon as it nears the chunk-load
    // radius, so freshly generated terrain stays hidden in fog instead of
    // popping into view. The colour matches the shaders' atmospheric fog
    // (sky-ambient based, so it tracks time/weather and the per-area tint) and
    // is gamma-encoded to sit in the same space as the post scene buffer.
    float viewDistBlocks = (float)(ctx.world.renderDistance * CHUNK_SIZE);
    glm::vec3 fogLin = skyAmbient * glm::max(sunFactor, 0.12f) * glm::mix(0.90f, 0.72f, weather);
    glm::vec3 fogCol = glm::pow(glm::clamp(fogLin, glm::vec3(0.0f), glm::vec3(1.0f)),
                                glm::vec3(1.0f / 2.2f));
    postShader.setFloat("u_viewDist", viewDistBlocks);
    postShader.setVec3 ("u_fogColor", fogCol);
    glBindVertexArray(postVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);

    // FPS + chunk count in title bar
    {
        static int   fpsCount = 0;
        static float fpsTimer = 0.0f;
        fpsCount++;
        fpsTimer += ctx.deltaTime;
        if (fpsTimer >= 1.0f) {
            std::string title = "Terrax | FPS: " + std::to_string(fpsCount)
                              + " | Chunks: " + std::to_string(ctx.world.chunks.size());
            glfwSetWindowTitle(window, title.c_str());
            fpsCount = 0; fpsTimer = 0.0f;
        }
    }
}
