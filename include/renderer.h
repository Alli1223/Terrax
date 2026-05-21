#pragma once
#include "gl_loader.h"
#include "shader.h"
#include <glm/glm.hpp>

struct AppContext;
struct GLFWwindow;

class Renderer {
public:
    Renderer()  = default;
    ~Renderer() = default;

    bool init(int width, int height);
    void resizeFramebuffers(int width, int height);

    // Renders the full play-state 3D scene (shadow, reflection, terrain, characters, water).
    void renderWorld(AppContext& ctx, GLFWwindow* window, float currentTime);

    // Renders the character 3D viewport inside the editor (call before ImGui panel).
    void renderEditorCharacter(AppContext& ctx, const glm::mat4& model,
                                const glm::mat4& view, const glm::mat4& proj);

    // Renders the house 3D viewport inside the house editor (call before ImGui panel).
    void renderEditorHouse(AppContext& ctx, const glm::mat4& model,
                            const glm::mat4& view, const glm::mat4& proj);

    GLuint getShadowMapTex() const { return shadowMapTex; }

    // Written each frame by renderWorld; readable by UI for nametag projections.
    glm::mat4 frameView  = glm::mat4(1.0f);
    glm::mat4 frameProj  = glm::mat4(1.0f);
    glm::vec3 frameEyePos;
    int frameFbW = 0, frameFbH = 0;

private:
    static constexpr int   SHADOW_RES = 2048;
    static constexpr float WATER_Y    = 29.0f;

    Shader chunkShader, waterShader, skyShader, charShader, shadowShader, glassShader;

    GLuint skyVAO = 0, skyVBO = 0;
    GLuint atlasTexture = 0;
    GLuint shadowFBO = 0, shadowMapTex = 0;
    GLuint reflFBO = 0, reflColorTex = 0, reflDepthRBO = 0;
    GLuint particleVao = 0, particleVbo = 0;

    // --- Dynamic point-light occlusion volume ---
    // A 3D opacity texture of the blocks around the player; the shaders raymarch
    // it so lantern/lamp light is blocked by walls (and passes through glass).
    static constexpr int LIGHTVOL_SIZE = 128;
    GLuint     lightVolTex   = 0;
    glm::ivec3 lightVolOrigin = glm::ivec3(1 << 28);   // sentinel forces first build
    float      lightVolTimer = 1.0e9f;
    void updateLightVolume(AppContext& ctx);

    void setupSkybox();
    void renderSkybox(const glm::mat4& view, const glm::mat4& proj, float gameTime, float time);
};
