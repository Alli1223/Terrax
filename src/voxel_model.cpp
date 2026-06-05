#include "voxel_model.h"
#include "building.h"
#include <iostream>
#include <algorithm>
#include <memory>
#include <random>

VoxelVolume::VoxelVolume(int x, int y, int z) : sizeX(x), sizeY(y), sizeZ(z) {
    voxels.resize(x * y * z, {0, 0, 0, 0});
}

VoxelVolume::~VoxelVolume() {
    if (vao) { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); }
}

void VoxelVolume::setVoxel(int x, int y, int z, Voxel v) {
    if (x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return;
    voxels[z * sizeX * sizeY + y * sizeX + x] = v;
    needsMeshUpdate = true;
}

Voxel VoxelVolume::getVoxel(int x, int y, int z) const {
    if (x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return {0,0,0,0};
    return voxels[z * sizeX * sizeY + y * sizeX + x];
}

void VoxelVolume::updateMesh() {
    if (!needsMeshUpdate) return;
    std::vector<CharacterVertex> mesh;
    auto addFace = [&](int x, int y, int z, int face, Voxel v) {
        glm::vec4 color(v.r/255.0f, v.g/255.0f, v.b/255.0f, v.a/255.0f);
        float xf = (float)x, yf = (float)y, zf = (float)z;
        static const glm::vec3 normals[] = {{0,0,1}, {0,0,-1}, {-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0}};
        static const float verts[] = {
            0,0,1, 1,0,1, 1,1,1, 1,1,1, 0,1,1, 0,0,1,
            0,0,0, 0,1,0, 1,1,0, 1,1,0, 1,0,0, 0,0,0,
            0,0,0, 0,0,1, 0,1,1, 0,1,1, 0,1,0, 0,0,0,
            1,0,0, 1,1,0, 1,1,1, 1,1,1, 1,0,1, 1,0,0,
            0,1,0, 0,1,1, 1,1,1, 1,1,1, 1,1,0, 0,1,0,
            0,0,0, 1,0,0, 1,0,1, 1,0,1, 0,0,1, 0,0,0
        };
        for (int i=0; i<6; i++) {
            mesh.push_back({glm::vec3(xf + verts[face*18 + i*3], yf + verts[face*18 + i*3 + 1], zf + verts[face*18 + i*3 + 2]), normals[face], color});
        }
    };
    for (int z=0; z<sizeZ; z++) for (int y=0; y<sizeY; y++) for (int x=0; x<sizeX; x++) {
        Voxel v = getVoxel(x, y, z);
        if (v.a == 0) continue;
        if (getVoxel(x, y, z + 1).a == 0) addFace(x, y, z, 0, v);
        if (getVoxel(x, y, z - 1).a == 0) addFace(x, y, z, 1, v);
        if (getVoxel(x - 1, y, z).a == 0) addFace(x, y, z, 2, v);
        if (getVoxel(x + 1, y, z).a == 0) addFace(x, y, z, 3, v);
        if (getVoxel(x, y + 1, z).a == 0) addFace(x, y, z, 4, v);
        if (getVoxel(x, y - 1, z).a == 0) addFace(x, y, z, 5, v);
    }
    if (!vao) glGenVertexArrays(1, &vao);
    if (!vbo) glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(CharacterVertex), mesh.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CharacterVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CharacterVertex), (void*)offsetof(CharacterVertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CharacterVertex), (void*)offsetof(CharacterVertex, color));
    glEnableVertexAttribArray(2);
    vertexCount = (int)mesh.size();
    needsMeshUpdate = false;
}

void VoxelVolume::draw() const {
    if (vertexCount == 0) return;
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

bool VoxelVolume::raycast(glm::vec3 ro, glm::vec3 rd, float maxDist, glm::ivec3& hitVoxel, glm::ivec3& hitNormal) const {
    glm::ivec3 ipos = glm::floor(ro);
    glm::vec3 rdInv = 1.0f / rd;
    glm::ivec3 step = glm::sign(rd);
    glm::vec3 tMax = (glm::vec3(ipos) + glm::vec3(std::max(step.x, 0), std::max(step.y, 0), std::max(step.z, 0)) - ro) * rdInv;
    glm::vec3 tDelta = glm::vec3(step) * rdInv;
    float dist = 0;
    while (dist < maxDist) {
        if (ipos.x >= 0 && ipos.x < sizeX && ipos.y >= 0 && ipos.y < sizeY && ipos.z >= 0 && ipos.z < sizeZ) {
            if (getVoxel(ipos.x, ipos.y, ipos.z).a > 0) { hitVoxel = ipos; return true; }
        }
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) { dist = tMax.x; tMax.x += tDelta.x; ipos.x += step.x; hitNormal = {-step.x, 0, 0}; }
            else { dist = tMax.z; tMax.z += tDelta.z; ipos.z += step.z; hitNormal = {0, 0, -step.z}; }
        } else {
            if (tMax.y < tMax.z) { dist = tMax.y; tMax.y += tDelta.y; ipos.y += step.y; hitNormal = {0, -step.y, 0}; }
            else { dist = tMax.z; tMax.z += tDelta.z; ipos.z += step.z; hitNormal = {0, 0, -step.z}; }
        }
    }
    return false;
}
