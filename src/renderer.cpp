#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "renderer.h"
#include "app_context.h"
#include "game_types.h"
#include "atlas.h"
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
};

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
    lights.intensity[i] = (held ? 1.2f : 0.55f) * flicker;
    lights.radius[i]    = held ? 14.0f : 6.0f;
}

static void collectLanternLights(const AppContext& ctx, float flicker, LanternLightList& lights) {
    lights.count = 0;
    addLantern(lights, ctx.camera.position, ctx.playerYaw, ctx.lanternHeld, flicker);
    for (const auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        addLantern(lights, p.position, p.yaw, p.lanternHeld, flicker);
    }
}

static void bindLanternLights(const Shader& shader, const LanternLightList& lights) {
    shader.setLanternLights(lights.count, lights.pos, lights.intensity, lights.radius);
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

    return true;
}

void Renderer::resizeFramebuffers(int width, int height) {
    if (width <= 0 || height <= 0 || reflColorTex == 0) return;
    glBindTexture(GL_TEXTURE_2D, reflColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, reflDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::renderSkybox(const glm::mat4& view, const glm::mat4& proj,
                              float gameTime, float time) {
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    skyShader.use();
    skyShader.setMat4("view", glm::mat4(glm::mat3(view)));
    skyShader.setMat4("projection", proj);
    skyShader.setFloat("timeOfDay", gameTime);
    skyShader.setFloat("time", time);
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
    charShader.setLanternLights(0, nullptr, nullptr, nullptr);
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

void Renderer::renderWorld(AppContext& ctx, GLFWwindow* window, float currentTime) {
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    float aspect = fbW / (float)fbH;

    glm::mat4 proj   = glm::perspective(glm::radians(ctx.camera.fov), aspect, 0.1f, 1000.0f);
    glm::vec3 eyePos = ctx.camera.position + glm::vec3(0, 1.6f, 0) - (ctx.camera.front * ctx.camDist);
    glm::mat4 view   = glm::lookAt(eyePos, ctx.camera.position + glm::vec3(0, 1.2f, 0), ctx.camera.worldUp);

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

    // --- Lantern ---
    ctx.flickerTime += ctx.deltaTime;
    float flicker = 1.0f
        + 0.08f * sinf(ctx.flickerTime * 7.3f)
        + 0.05f * sinf(ctx.flickerTime * 11.7f + 0.5f)
        + 0.03f * sinf(ctx.flickerTime * 19.1f + 1.2f);
    LanternLightList lanternLights;
    collectLanternLights(ctx, flicker, lanternLights);

    // --- Build character draw list ---
    glm::mat4 playerM(1.0f);
    if (ctx.playerRig) {
        ctx.playerRig->lanternHeld = ctx.lanternHeld;
        float velocity = glm::length(ctx.camera.velocity);
        ctx.playerRig->update(ctx.deltaTime, std::min(velocity * 0.5f, 5.0f));
        playerM = glm::translate(glm::mat4(1.0f), ctx.camera.position);
        playerM = glm::rotate(playerM, glm::radians(ctx.playerYaw), glm::vec3(0, 1, 0));
        playerM = glm::scale(playerM, glm::vec3(0.06f * ctx.playerRig->heightScale));
    }

    struct CharEntry { glm::mat4 m; BipedalRig* rig; };
    std::vector<CharEntry> remoteChars;
    for (auto& [id, p] : ctx.remotePlayers) {
        (void)id;
        if (!p.rig || !p.rig->torso || !p.rig->torso->volume) {
            if (!p.rig) p.rig = new BipedalRig();
            p.rig->setupDefaultHuman(true);
        }
        float lerpF   = 10.0f * ctx.deltaTime;
        glm::vec3 lastPos = p.position;
        p.position = glm::mix(p.position, p.targetPosition, std::min(1.0f, lerpF));
        p.pitch    = glm::mix(p.pitch, p.targetPitch, std::min(1.0f, lerpF));
        p.yaw      = glm::mix(p.yaw,   p.targetYaw,   std::min(1.0f, lerpF));
        float vel  = glm::length(p.position - lastPos) / (ctx.deltaTime > 0 ? ctx.deltaTime : 1.0f);
        if (p.isAttacking) {
            p.attackAnim += ctx.deltaTime * 5.0f;
            if (p.attackAnim > 1.0f) { p.isAttacking = false; p.attackAnim = 0.0f; }
        }
        p.rig->lanternHeld   = p.lanternHeld;
        p.rig->isAttacking = p.isAttacking;
        p.rig->attackAnim  = p.attackAnim;
        p.rig->update(ctx.deltaTime, std::min(vel, 10.0f));
        glm::mat4 pm = glm::translate(glm::mat4(1.0f), p.position);
        pm = glm::rotate(pm, glm::radians(p.yaw), glm::vec3(0, 1, 0));
        pm = glm::scale(pm, glm::vec3(0.06f * p.rig->heightScale));
        remoteChars.push_back({pm, p.rig});
    }

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
        renderSkybox(reflView, proj, ctx.gameTime, currentTime);
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
        bindLanternLights(chunkShader, lanternLights);
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
        if (ctx.playerRig) ctx.playerRig->draw(playerM, sml);
        for (auto& ce : remoteChars) ce.rig->draw(ce.m, sml);
    }
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);

    // --- Main pass ---
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderSkybox(view, proj, ctx.gameTime, currentTime);
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
    bindLanternLights(chunkShader, lanternLights);
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
    bindLanternLights(charShader, lanternLights);
    charShader.setFloat("time", currentTime);
    {
        GLuint ml = glGetUniformLocation(charShader.id, "model");
        if (ctx.playerRig) ctx.playerRig->draw(playerM, ml);
        for (auto& ce : remoteChars) ce.rig->draw(ce.m, ml);
    }

    // Foliage (alpha-cutout, no back-face culling)
    glDisable(GL_CULL_FACE);
    chunkShader.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlasTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shadowMapTex);
    chunkShader.setMat4("view", view); chunkShader.setMat4("projection", proj);
    chunkShader.setMat4("lightSpaceMatrix", lightSpaceMat);
    chunkShader.setFloat("sunFactor", sunFactor);
    chunkShader.setVec3("skyAmbient", skyAmbient);
    chunkShader.setVec3("camPos", eyePos);
    chunkShader.setVec3("u_sunDir", sunDir);
    bindLanternLights(chunkShader, lanternLights);
    chunkShader.setFloat("time", currentTime);
    chunkShader.setVec4("u_clipPlane", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    ctx.world.drawAllFoliage();

    // Leaf particles — tiny coloured cubes, no culling (already disabled)
    if (!ctx.leafParticles.empty()) {
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

    // Water
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    waterShader.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlasTexture);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, reflColorTex);
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
    bindLanternLights(waterShader, lanternLights);
    ctx.world.drawAllWater();
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);

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
