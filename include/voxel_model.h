#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "gl_loader.h"
#include "world.h"
#include "interactable.h"
#include <mutex>
#include <random>

struct Voxel {
    uint8_t r, g, b, a;
};

class VoxelVolume {
public:
    int sizeX, sizeY, sizeZ;
    std::vector<Voxel> voxels;
    GLuint vao = 0, vbo = 0;
    int vertexCount = 0;
    bool needsMeshUpdate = true;

    VoxelVolume(int x, int y, int z);
    ~VoxelVolume();
    void setVoxel(int x, int y, int z, Voxel v);
    Voxel getVoxel(int x, int y, int z) const;
    void updateMesh();
    void draw() const;
    bool raycast(glm::vec3 ro, glm::vec3 rd, float maxDist, glm::ivec3& hitVoxel, glm::ivec3& hitNormal) const;
private:
    struct CharacterVertex { glm::vec3 pos; glm::vec3 normal; glm::vec4 color; };
};

class CharacterNode {
public:
    std::string name;
    VoxelVolume* volume = nullptr;
    glm::vec3 localPos{0.0f}, localRot{0.0f}, scale{1.0f}, pivot{0.0f};
    CharacterNode* parent = nullptr;
    std::vector<CharacterNode*> children;

    CharacterNode(std::string name) : name(name) {}
    ~CharacterNode() { delete volume; for (auto c : children) delete c; }
    void addChild(CharacterNode* child) { child->parent = this; children.push_back(child); }

    glm::mat4 getNodeTransform() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), localPos);
        m = glm::rotate(m, glm::radians(localRot.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(localRot.x), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(localRot.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, scale);
        return m;
    }

    void draw(const glm::mat4& parentTransform, GLuint modelLoc) const {
        glm::mat4 nodeM = parentTransform * getNodeTransform();
        if (volume) {
            glm::mat4 meshM = glm::translate(nodeM, -pivot);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &meshM[0][0]);
            volume->draw();
        }
        for (auto c : children) c->draw(nodeM, modelLoc);
    }

    struct RayHit { CharacterNode* node = nullptr; glm::ivec3 voxel, normal; float dist = 1e9f; };
    void raycast(glm::vec3 ro, glm::vec3 rd, const glm::mat4& parentTransform, RayHit& bestHit) {
        glm::mat4 nodeM = parentTransform * getNodeTransform();
        if (volume) {
            glm::mat4 meshM = glm::translate(nodeM, -pivot);
            glm::mat4 invM = glm::inverse(meshM);
            glm::vec3 localRo = glm::vec3(invM * glm::vec4(ro, 1.0f));
            glm::vec3 localRd = glm::normalize(glm::vec3(invM * glm::vec4(rd, 0.0f)));
            glm::ivec3 hitV, hitN;
            if (volume->raycast(localRo, localRd, 100.0f, hitV, hitN)) {
                float d = glm::distance(ro, glm::vec3(meshM * glm::vec4(glm::vec3(hitV) + 0.5f, 1.0f)));
                if (d < bestHit.dist) { bestHit.dist = d; bestHit.node = this; bestHit.voxel = hitV; bestHit.normal = hitN; }
            }
        }
        for (auto c : children) c->raycast(ro, rd, nodeM, bestHit);
    }
};

class CharacterRig {
public:
    CharacterNode* root = nullptr;
    CharacterRig() {}
    virtual ~CharacterRig() { delete root; }
    virtual void update(float dt, float velocity) = 0;
    void draw(const glm::mat4& baseTransform, GLuint modelLoc) const { if (root) root->draw(baseTransform, modelLoc); }
    CharacterNode::RayHit raycast(glm::vec3 ro, glm::vec3 rd, const glm::mat4& baseTransform) {
        CharacterNode::RayHit hit; if (root) root->raycast(ro, rd, baseTransform, hit); return hit;
    }
};

class BipedalRig : public CharacterRig {
public:
    CharacterNode *torso, *head, *lArm, *rArm, *lLeg, *rLeg, *sword, *lantern;
    float animTime = 0.0f, attackAnim = 0.0f;
    bool isAttacking = false;
    bool lanternHeld = false;
    bool isSwimming  = false;
    // Static pose override: when set to Sitting / Lying the update() method
    // skips the walking / breathing cycle and snaps every limb to a fixed
    // resting posture (and lying tilts the whole rig 90° forward, hands on
    // chest, legs straight). Reset to Standing to resume normal animation.
    PlayerPose pose  = PlayerPose::Standing;
    int hairStyle = 0, earType = 0, armorType = 0, noseStyle = 0, eyebrowStyle = 0, eyeType = 0;
    Voxel hairColor = {60, 40, 20, 255}, eyeColor = {0, 0, 0, 255};
    Voxel skinColor = {210, 160, 130, 255};
    float heightScale = 1.0f;
    float weightScale = 1.0f;
    BipedalRig();
    void setupDefaultHuman(bool male);
    void update(float dt, float velocity) override;
    void applyCustomization();
    void randomizeAppearance();                    // uses a shared global RNG
    void randomizeAppearance(std::mt19937& rng);   // deterministic from a seed
};

class QuadrupedRig : public CharacterRig {
public:
    CharacterNode *body, *head, *flLeg, *frLeg, *blLeg, *brLeg, *tail;
    float animTime = 0.0f;
    float restY    = 0.0f;   // body-centre height above the rig root (feet)
    QuadrupedRig();
    void update(float dt, float velocity) override;
};

// --- House generator ---------------------------------------------------------
// A customizable building stored as a grid of world BlockTypes (1 voxel = 1
// world block) so it can be baked straight into the terrain. `volume` is a
// colour mesh derived from the block grid, used only for editor / ghost display.

// Buildings are emitted into a fixed-size voxel grid before being tight-cropped
// to their non-air bounds. The grid has to be large enough for the biggest
// concrete Building subclass (currently the Manor template + a steep roof).
static constexpr int HOUSE_VX = 32;
static constexpr int HOUSE_VY = 40;
static constexpr int HOUSE_VZ = 32;

// Representative display colour for a block type (editor + placement ghost).
Voxel houseBlockColor(BlockType t);

// Fills `blocks` (HOUSE_VX*HOUSE_VY*HOUSE_VZ, index ((z*HOUSE_VY)+y)*HOUSE_VX+x)
// with a procedural house. Shared by the house editor and the town generator.
void generateHouseGrid(int templateType, int roofType, int material,
                       std::vector<BlockType>& blocks);

class HouseModel {
public:
    VoxelVolume* volume = nullptr;            // display mesh, rebuilt from `blocks`
    std::vector<BlockType> blocks;            // HOUSE_VX*HOUSE_VY*HOUSE_VZ grid
    glm::ivec3 boundMin{0, 0, 0};             // tight bounding box of non-air blocks
    glm::ivec3 boundMax{0, 0, 0};
    int templateType = 0;   // 0 Bungalow, 1 Two-Story, 2 Cottage, 3 Tower
    int roofType     = 1;   // 0 Flat, 1 Gabled, 2 Hipped, 3 Pyramid
    int material     = 0;   // 0 Wood, 1 Stone, 2 Sandstone, 3 Snow

    HouseModel();
    ~HouseModel();
    BlockType get(int x, int y, int z) const;
    void      set(int x, int y, int z, BlockType t);
    void      rebuild();        // regenerates the block grid from template settings
    void      refreshMesh();    // recomputes bounds + display mesh from the grid
};
