#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "gl_loader.h"
#include <mutex>

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
    struct CharacterVertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec4 color;
    };
};

class CharacterNode {
public:
    std::string name;
    VoxelVolume* volume = nullptr;
    glm::vec3 localPos{0.0f};
    glm::vec3 localRot{0.0f}; // Euler angles for simplicity in editor
    glm::vec3 scale{1.0f};
    glm::vec3 pivot{0.0f};

    CharacterNode* parent = nullptr;
    std::vector<CharacterNode*> children;

    CharacterNode(std::string name) : name(name) {}
    ~CharacterNode() {
        delete volume;
        for (auto c : children) delete c;
    }

    void addChild(CharacterNode* child) {
        child->parent = this;
        children.push_back(child);
    }

    glm::mat4 getLocalTransform() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), localPos);
        m = glm::rotate(m, glm::radians(localRot.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(localRot.x), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(localRot.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, scale);
        m = glm::translate(m, -pivot);
        return m;
    }

    void draw(const glm::mat4& parentTransform, GLuint modelLoc) const {
        glm::mat4 worldM = parentTransform * getLocalTransform();
        if (volume) {
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &worldM[0][0]);
            volume->draw();
        }
        for (auto c : children) {
            c->draw(worldM, modelLoc);
        }
    }

    struct RayHit {
        CharacterNode* node = nullptr;
        glm::ivec3 voxel;
        glm::ivec3 normal;
        float dist = 1e9f;
    };

    void raycast(glm::vec3 ro, glm::vec3 rd, const glm::mat4& parentTransform, RayHit& bestHit) {
        glm::mat4 worldM = parentTransform * getLocalTransform();
        if (volume) {
            glm::mat4 invM = glm::inverse(worldM);
            glm::vec3 localRo = glm::vec3(invM * glm::vec4(ro, 1.0f));
            glm::vec3 localRd = glm::normalize(glm::vec3(invM * glm::vec4(rd, 0.0f)));
            
            glm::ivec3 hitV, hitN;
            if (volume->raycast(localRo, localRd, 100.0f, hitV, hitN)) {
                float d = glm::distance(ro, glm::vec3(worldM * glm::vec4(glm::vec3(hitV) + 0.5f, 1.0f)));
                if (d < bestHit.dist) {
                    bestHit.dist = d;
                    bestHit.node = this;
                    bestHit.voxel = hitV;
                    bestHit.normal = hitN;
                }
            }
        }
        for (auto c : children) {
            c->raycast(ro, rd, worldM, bestHit);
        }
    }
};

class CharacterRig {
public:
    CharacterNode* root = nullptr;
    
    CharacterRig() {}
    virtual ~CharacterRig() { delete root; }

    virtual void update(float dt, float velocity) = 0;
    void draw(const glm::mat4& baseTransform, GLuint modelLoc) const {
        if (root) root->draw(baseTransform, modelLoc);
    }

    CharacterNode::RayHit raycast(glm::vec3 ro, glm::vec3 rd, const glm::mat4& baseTransform) {
        CharacterNode::RayHit hit;
        if (root) root->raycast(ro, rd, baseTransform, hit);
        return hit;
    }
};

class BipedalRig : public CharacterRig {
public:
    CharacterNode *torso, *head, *lArm, *rArm, *lLeg, *rLeg;
    float animTime = 0.0f;

    // Customization state
    int hairStyle = 0;
    Voxel hairColor = {60, 40, 20, 255};
    Voxel eyeColor = {0, 0, 0, 255};
    int earType = 0; // 0=None, 1=Human, 2=Elven

    BipedalRig();
    void setupDefaultHuman(bool male);
    void update(float dt, float velocity) override;
    void applyCustomization();
};

class QuadrupedRig : public CharacterRig {
public:
    CharacterNode *body, *head, *flLeg, *frLeg, *blLeg, *brLeg, *tail;
    float animTime = 0.0f;

    QuadrupedRig();
    void update(float dt, float velocity) override;
};
