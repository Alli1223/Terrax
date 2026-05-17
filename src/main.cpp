#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gl_loader.h"
#include "atlas.h"
#include "world.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>
#include <cmath>
#include <vector>

#include "shader.h"
#include "camera.h"

// --- Config ---
static constexpr int   WIDTH  = 1280;
static constexpr int   HEIGHT = 720;
static constexpr float DAY_CYCLE_SECONDS = 120.0f; // full day in seconds
static constexpr float REACH  = 5.0f;
static constexpr float PLAYER_HEIGHT = 1.8f;
static constexpr float PLAYER_WIDTH  = 0.4f;

// --- Globals ---
Camera  camera(glm::vec3(8.0f, 50.0f, 8.0f));
World   world;
double  lastX = WIDTH/2.0, lastY = HEIGHT/2.0;
bool    firstMouse = true;
float   deltaTime = 0.0f, lastFrame = 0.0f;
float   gameTime = 0.3f; // start at morning
bool    noclip = false;

int keyFwd=0, keyBack=0, keyLeft=0, keyRight=0, keyJump=0;

// --- Skybox ---
GLuint skyVAO=0, skyVBO=0;
static const float skyVerts[] = {
    -1,-1,-1,  1,-1,-1,  1, 1,-1,  1, 1,-1, -1, 1,-1, -1,-1,-1,
    -1,-1, 1, -1, 1, 1,  1, 1, 1,  1, 1, 1,  1,-1, 1, -1,-1, 1,
    -1, 1, 1, -1, 1,-1,  1, 1,-1,  1, 1,-1,  1, 1, 1, -1, 1, 1,
    -1,-1,-1, -1,-1, 1,  1,-1,-1,  1,-1,-1, -1,-1, 1,  1,-1, 1,
     1,-1,-1,  1,-1, 1,  1, 1, 1,  1, 1, 1,  1, 1,-1,  1,-1,-1,
    -1,-1,-1, -1, 1,-1, -1, 1, 1, -1, 1, 1, -1,-1, 1, -1,-1,-1,
};

void setupSkybox() {
    glGenVertexArrays(1, &skyVAO);
    glGenBuffers(1, &skyVBO);
    glBindVertexArray(skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyVerts), skyVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// --- Crosshair (simple lines drawn via immediate-ish approach) ---
GLuint crossVAO=0, crossVBO=0;
Shader* crossShader = nullptr;

void setupCrosshair() {
    // Will be done via a simple shader at screen center
}

// --- Collision ---

static constexpr float SKIN        = 0.001f;
static constexpr float STEP_HEIGHT = 1.0f;

// Solid blocks along the leading X face of the AABB at the given Y range.
static bool hitFaceX(float px, float py, float pz, float hw, float ph, const World& w, bool posDir) {
    int bx = posDir ? (int)floorf(px + hw) : (int)floorf(px - hw - SKIN);
    for (int by = (int)floorf(py);       by <= (int)floorf(py + ph - SKIN); by++)
    for (int bz = (int)floorf(pz - hw); bz <= (int)floorf(pz + hw - SKIN); bz++)
        if (w.getBlock(bx, by, bz) != BlockType::Air) return true;
    return false;
}

// Solid blocks along the leading Z face.
static bool hitFaceZ(float px, float py, float pz, float hw, float ph, const World& w, bool posDir) {
    int bz = posDir ? (int)floorf(pz + hw) : (int)floorf(pz - hw - SKIN);
    for (int by = (int)floorf(py);       by <= (int)floorf(py + ph - SKIN); by++)
    for (int bx = (int)floorf(px - hw); bx <= (int)floorf(px + hw - SKIN); bx++)
        if (w.getBlock(bx, by, bz) != BlockType::Air) return true;
    return false;
}

// True when the full AABB volume contains no solid block.
static bool aabbClear(float px, float py, float pz, float hw, float ph, const World& w) {
    for (int bx = (int)floorf(px - hw); bx <= (int)floorf(px + hw - SKIN); bx++)
    for (int by = (int)floorf(py);      by <= (int)floorf(py + ph - SKIN); by++)
    for (int bz = (int)floorf(pz - hw); bz <= (int)floorf(pz + hw - SKIN); bz++)
        if (w.getBlock(bx, by, bz) != BlockType::Air) return false;
    return true;
}

glm::vec3 resolveCollision(const glm::vec3& pos, const World& w) {
    const float hw = PLAYER_WIDTH  / 2.0f;
    const float ph = PLAYER_HEIGHT;
    glm::vec3   p  = pos;

    // ── Y: gravity / jump head-bump ────────────────────────────────────────
    camera.onGround = false;

    if (camera.velocity.y <= 0.0f) {
        // Scan upward from floor(p.y) to find the highest solid block the feet
        // have entered. Range +3 covers the ~2-block max fall per capped frame.
        int yFeet = (int)floorf(p.y);
        for (int by = yFeet + 3; by >= yFeet; by--) {
            if (by < 0) {
                p.y = SKIN; camera.velocity.y = 0.0f; camera.onGround = true; break;
            }
            bool hit = false;
            for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !hit; bx++)
            for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !hit; bz++)
                if (w.getBlock(bx, by, bz) != BlockType::Air) hit = true;
            if (hit) {
                float top = (float)(by + 1);
                if (p.y < top) {
                    p.y = top + SKIN;
                    camera.velocity.y = 0.0f;
                    camera.onGround = true;
                }
                break;
            }
        }
    } else {
        // Jumping: check if the top of the AABB entered a block.
        int byHead = (int)floorf(p.y + ph);
        bool hit = false;
        for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !hit; bx++)
        for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !hit; bz++)
            if (w.getBlock(bx, byHead, bz) != BlockType::Air) hit = true;
        if (hit) {
            p.y = (float)byHead - ph - SKIN;
            camera.velocity.y = 0.0f;
        }
    }

    // Ground state: solid block directly below feet?
    if (!camera.onGround) {
        int byBelow = (int)floorf(p.y - SKIN);
        if (byBelow >= 0) {
            bool found = false;
            for (int bx = (int)floorf(p.x - hw); bx <= (int)floorf(p.x + hw - SKIN) && !found; bx++)
            for (int bz = (int)floorf(p.z - hw); bz <= (int)floorf(p.z + hw - SKIN) && !found; bz++)
                if (w.getBlock(bx, byBelow, bz) != BlockType::Air) found = true;
            camera.onGround = found;
        }
    }

    // Save velocities so step-up can restore them after zeroing.
    float savedVx = camera.velocity.x;
    float savedVz = camera.velocity.z;

    // ── X ──────────────────────────────────────────────────────────────────
    bool blockedX = false;
    if (camera.velocity.x != 0.0f) {
        bool posX = camera.velocity.x > 0.0f;
        if (hitFaceX(p.x, p.y, p.z, hw, ph, w, posX)) {
            blockedX = true;
            p.x = posX ? floorf(p.x + hw)          - hw - SKIN
                       : floorf(p.x - hw - SKIN) + 1.0f + hw + SKIN;
            camera.velocity.x = 0.0f;
        }
    }

    // ── Z ──────────────────────────────────────────────────────────────────
    bool blockedZ = false;
    if (camera.velocity.z != 0.0f) {
        bool posZ = camera.velocity.z > 0.0f;
        if (hitFaceZ(p.x, p.y, p.z, hw, ph, w, posZ)) {
            blockedZ = true;
            p.z = posZ ? floorf(p.z + hw)          - hw - SKIN
                       : floorf(p.z - hw - SKIN) + 1.0f + hw + SKIN;
            camera.velocity.z = 0.0f;
        }
    }

    // ── Step-up ────────────────────────────────────────────────────────────
    // When blocked horizontally on the ground, lift by STEP_HEIGHT and retry
    // the original horizontal move. Succeeds only if the raised AABB is clear,
    // which automatically prevents stepping over walls taller than 1 block.
    if ((blockedX || blockedZ) && camera.onGround) {
        float tryX = blockedX ? pos.x : p.x;
        float tryZ = blockedZ ? pos.z : p.z;
        float tryY = p.y + STEP_HEIGHT;
        if (aabbClear(tryX, tryY, tryZ, hw, ph, w)) {
            p = { tryX, tryY, tryZ };
            camera.velocity.x = savedVx;
            camera.velocity.z = savedVz;
        }
    }

    return p;
}

// --- GLFW callbacks ---
void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoff = (float)(xpos - lastX);
    float yoff = (float)(lastY - ypos);
    lastX = xpos; lastY = ypos;
    camera.processMouseMovement(xoff, yoff);
}

void mouse_button_callback(GLFWwindow*, int button, int action, int) {
    if (action != GLFW_PRESS) return;
    glm::ivec3 hitBlock, hitNormal;
    if (world.raycast(camera.position + glm::vec3(0, PLAYER_HEIGHT * 0.9f, 0),
                      camera.front, REACH, hitBlock, hitNormal)) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, BlockType::Air);
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            glm::ivec3 place = hitBlock + hitNormal;
            if (world.getBlock(place.x, place.y, place.z) == BlockType::Air)
                world.setBlock(place.x, place.y, place.z, BlockType::Stone);
        }
    }
}

void key_callback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_N && action == GLFW_PRESS) noclip = !noclip;

    auto set = [&](int& v, int press, int release) {
        if (action == press) v = 1;
        else if (action == release) v = 0;
    };
    if (key == GLFW_KEY_W) { if(action==GLFW_PRESS) keyFwd=1; else if(action==GLFW_RELEASE) keyFwd=0; }
    if (key == GLFW_KEY_S) { if(action==GLFW_PRESS) keyBack=1; else if(action==GLFW_RELEASE) keyBack=0; }
    if (key == GLFW_KEY_A) { if(action==GLFW_PRESS) keyLeft=1; else if(action==GLFW_RELEASE) keyLeft=0; }
    if (key == GLFW_KEY_D) { if(action==GLFW_PRESS) keyRight=1; else if(action==GLFW_RELEASE) keyRight=0; }
    if (key == GLFW_KEY_SPACE) { if(action==GLFW_PRESS) keyJump=1; else if(action==GLFW_RELEASE) keyJump=0; }
    (void)set;
}

// --- Helpers ---
static float smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// Sun elevation in [-1, 1]: -1 = midnight, 0 = horizon, +1 = zenith.
// Matches sky.frag's sunDir(t).y  (divide by ||vec3(cos,sin,0.25)|| = sqrt(1.0625)).
static float sunElevation(float t) {
    return sinf((t - 0.25f) * 2.0f * 3.14159f) / sqrtf(1.0625f);
}

int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Terrax", nullptr, nullptr);
    if (!window) { std::cerr << "Window creation failed\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSwapInterval(0); // vsync off for perf testing

    if (!gl_load()) {
        std::cerr << "Failed to load OpenGL functions\n";
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_MULTISAMPLE);

    Shader chunkShader("shaders/chunk.vert", "shaders/chunk.frag");
    Shader waterShader("shaders/water.vert", "shaders/water.frag");
    Shader skyShader("shaders/sky.vert", "shaders/sky.frag");
    setupSkybox();

    GLuint atlasTexture = generateAtlas();

    std::cout << "Starting world...\n";
    world.generate(0, 0);
    std::cout << "World started. Chunks are loading in the background.\n";
    std::cout << "Controls: WASD move, Space jump, N noclip, LMB destroy, RMB place\n";

    // Spawn player in noclip mode so they don't fall while world loads
    noclip = true;
    camera.position = glm::vec3(8.5f, 42.0f, 8.5f);
    camera.pitch = -20.0f; // look slightly down so terrain fills the view
    camera.updateVectors();

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        deltaTime = std::min(deltaTime, 0.05f);

        gameTime = fmodf(gameTime + deltaTime / DAY_CYCLE_SECONDS, 1.0f);

        // Input
        camera.processKeyboard(keyFwd - keyBack, keyRight - keyLeft, keyJump, deltaTime);

        if (noclip) {
            // Fly mode: move along look direction
            glm::vec3 move(0);
            if (keyFwd)  move += camera.front;
            if (keyBack) move -= camera.front;
            if (keyRight) move += camera.right;
            if (keyLeft)  move -= camera.right;
            if (keyJump) move += camera.worldUp;
            if (glm::length(move) > 0.001f) move = glm::normalize(move);
            camera.position += move * 15.0f * deltaTime;
            camera.velocity = glm::vec3(0);
        } else {
            camera.applyGravity(deltaTime);
            camera.position = resolveCollision(camera.position, world);
        }

        // Update chunks around player
        int pcx = (int)floorf(camera.position.x / CHUNK_SIZE);
        int pcz = (int)floorf(camera.position.z / CHUNK_SIZE);
        world.update(pcx, pcz);

        // Render
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);

        float aspect = fbW / (float)fbH;
        glm::mat4 proj = glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 1000.0f);
        glm::mat4 view = camera.getViewMatrix();

        // All time-of-day transitions are driven by the sun's geometric elevation,
        // using the exact same formula as sky.frag so the world and sky stay in sync.
        float sunY    = sunElevation(gameTime);
        float dayness = smoothstep(-0.12f, 0.22f, sunY);   // 0 = night, 1 = full day
        float dawnDusk = smoothstep(-0.30f, 0.0f, sunY)
                       * (1.0f - smoothstep(0.0f, 0.30f, sunY));

        // Sky light multiplier for the chunk shader.
        float sunFactor = 0.04f + 0.96f * dayness;

        // Sky ambient colour: the tint that outdoor surfaces receive from the sky.
        // Matches the palette used in sky.frag's sky gradient.
        glm::vec3 dayAmb  (0.70f, 0.84f, 1.00f);   // mid-day  : bright blue-white
        glm::vec3 dawnAmb (1.00f, 0.58f, 0.24f);   // dawn/dusk: warm amber-orange
        glm::vec3 nightAmb(0.18f, 0.22f, 0.50f);   // night    : cool moonlit blue
        glm::vec3 skyAmbient = glm::mix(nightAmb,
                                glm::mix(dayAmb, dawnAmb, dawnDusk),
                                dayness);

        // Clear both buffers once at the top of the frame
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Sky — disable depth test entirely so sky always fills the background
        {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            skyShader.use();
            glm::mat4 skyView = glm::mat4(glm::mat3(view)); // strip translation
            skyShader.setMat4("view", skyView);
            skyShader.setMat4("projection", proj);
            skyShader.setFloat("timeOfDay", gameTime);
            skyShader.setFloat("time", currentFrame);
            glBindVertexArray(skyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
        }

        // Camera eye position in world space (eye-level offset)
        glm::vec3 eyePos = camera.position + glm::vec3(0.0f, PLAYER_HEIGHT * 0.9f, 0.0f);
        glm::mat4 model  = glm::mat4(1.0f);

        // Opaque world pass
        {
            chunkShader.use();

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, atlasTexture);
            chunkShader.setInt("atlas", 0);

            chunkShader.setMat4("model",      model);
            chunkShader.setMat4("view",       view);
            chunkShader.setMat4("projection", proj);
            chunkShader.setFloat("sunFactor", sunFactor);
            chunkShader.setVec3("skyAmbient", skyAmbient);
            chunkShader.setVec3("camPos",     eyePos);

            world.drawAll();
        }

        // Translucent water pass (alpha blend, no depth write, two-sided)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);

            waterShader.use();

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, atlasTexture);
            waterShader.setInt("atlas", 0);

            waterShader.setMat4("model",       model);
            waterShader.setMat4("view",        view);
            waterShader.setMat4("projection",  proj);
            waterShader.setFloat("sunFactor",  sunFactor);
            waterShader.setVec3("skyAmbient",  skyAmbient);
            waterShader.setVec3("camPos",      eyePos);
            waterShader.setFloat("time",       currentFrame);
            waterShader.setFloat("timeOfDay",  gameTime);

            world.drawAllWater();

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
        }

        // HUD: crosshair via simple line overlay
        {
            // Simple title bar FPS
            static int fpsCount = 0;
            static float fpsTimer = 0;
            fpsCount++;
            fpsTimer += deltaTime;
            if (fpsTimer >= 1.0f) {
                std::string title = "Terrax | FPS: " + std::to_string(fpsCount)
                    + " | Chunks: " + std::to_string(world.chunks.size())
                    + " | Pos: " + std::to_string((int)camera.position.x)
                    + "," + std::to_string((int)camera.position.y)
                    + "," + std::to_string((int)camera.position.z)
                    + " | Time: " + std::to_string((int)(gameTime * 24)) + "h"
                    + (noclip ? " [NOCLIP]" : "");
                glfwSetWindowTitle(window, title.c_str());
                fpsCount = 0; fpsTimer = 0;
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &skyVAO);
    glDeleteBuffers(1, &skyVBO);
    glfwTerminate();
    return 0;
}
