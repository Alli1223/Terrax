#include "voxel_model.h"
#include <iostream>

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
        
        static const glm::vec3 normals[] = {
            {0,0,1}, {0,0,-1}, {-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0}
        };
        static const float verts[] = {
            // Front (z+)
            0,0,1, 1,0,1, 1,1,1, 1,1,1, 0,1,1, 0,0,1,
            // Back (z-)
            0,0,0, 0,1,0, 1,1,0, 1,1,0, 1,0,0, 0,0,0,
            // Left (x-)
            0,0,0, 0,0,1, 0,1,1, 0,1,1, 0,1,0, 0,0,0,
            // Right (x+)
            1,0,0, 1,1,0, 1,1,1, 1,1,1, 1,0,1, 1,0,0,
            // Top (y+)
            0,1,0, 0,1,1, 1,1,1, 1,1,1, 1,1,0, 0,1,0,
            // Bottom (y-)
            0,0,0, 1,0,0, 1,0,1, 1,0,1, 0,0,1, 0,0,0
        };

        for (int i=0; i<6; i++) {
            mesh.push_back({
                glm::vec3(xf + verts[face*18 + i*3], yf + verts[face*18 + i*3 + 1], zf + verts[face*18 + i*3 + 2]),
                normals[face],
                color
            });
        }
    };

    for (int z=0; z<sizeZ; z++) {
        for (int y=0; y<sizeY; y++) {
            for (int x=0; x<sizeX; x++) {
                Voxel v = getVoxel(x, y, z);
                if (v.a == 0) continue;

                if (getVoxel(x, y, z + 1).a == 0) addFace(x, y, z, 0, v);
                if (getVoxel(x, y, z - 1).a == 0) addFace(x, y, z, 1, v);
                if (getVoxel(x - 1, y, z).a == 0) addFace(x, y, z, 2, v);
                if (getVoxel(x + 1, y, z).a == 0) addFace(x, y, z, 3, v);
                if (getVoxel(x, y + 1, z).a == 0) addFace(x, y, z, 4, v);
                if (getVoxel(x, y - 1, z).a == 0) addFace(x, y, z, 5, v);
            }
        }
    }

    if (!vao) glGenVertexArrays(1, &vao);
    if (!vbo) glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(CharacterVertex), mesh.data(), GL_STATIC_DRAW);

    // Pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CharacterVertex), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CharacterVertex), (void*)offsetof(CharacterVertex, normal));
    glEnableVertexAttribArray(1);
    // Color
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
            if (getVoxel(ipos.x, ipos.y, ipos.z).a > 0) {
                hitVoxel = ipos;
                return true;
            }
        }
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                dist = tMax.x; tMax.x += tDelta.x; ipos.x += step.x; hitNormal = {-step.x, 0, 0};
            } else {
                dist = tMax.z; tMax.z += tDelta.z; ipos.z += step.z; hitNormal = {0, 0, -step.z};
            }
        } else {
            if (tMax.y < tMax.z) {
                dist = tMax.y; tMax.y += tDelta.y; ipos.y += step.y; hitNormal = {0, -step.y, 0};
            } else {
                dist = tMax.z; tMax.z += tDelta.z; ipos.z += step.z; hitNormal = {0, 0, -step.z};
            }
        }
    }
    return false;
}

BipedalRig::BipedalRig() {
    root = new CharacterNode("Root");
    torso = new CharacterNode("Torso");
    head = new CharacterNode("Head");
    lArm = new CharacterNode("LArm");
    rArm = new CharacterNode("RArm");
    lLeg = new CharacterNode("LLeg");
    rLeg = new CharacterNode("RLeg");

    root->addChild(torso);
    torso->addChild(head);
    torso->addChild(lArm);
    torso->addChild(rArm);
    torso->addChild(lLeg);
    torso->addChild(rLeg);
}

void BipedalRig::setupDefaultHuman(bool male) {
    // Colors
    Voxel skin = male ? Voxel{210, 160, 130, 255} : Voxel{230, 180, 150, 255};
    Voxel shirt = male ? Voxel{50, 100, 200, 255} : Voxel{200, 50, 100, 255};
    Voxel pants = {30, 30, 30, 255};

    // --- Torso (Heart of the chibi) ---
    // Size: 10x12x8 (slightly taller and deeper)
    torso->volume = new VoxelVolume(10, 12, 8);
    torso->pivot = glm::vec3(5, 0, 4);
    torso->localPos = glm::vec3(0, 10, 0); 
    for(int x=0; x<10; x++) for(int y=0; y<12; y++) for(int z=0; z<8; z++) {
        torso->volume->setVoxel(x, y, z, shirt);
    }
    // Add slight chest/back detail
    for(int x=2; x<8; x++) for(int y=8; y<11; y++) torso->volume->setVoxel(x, y, 7, shirt);
    torso->volume->updateMesh();

    // --- Head (Large and Cute) ---
    head->volume = new VoxelVolume(12, 12, 12);
    head->pivot = glm::vec3(6, 0, 6);
    head->localPos = glm::vec3(0, 12, 0); 
    for(int x=0; x<12; x++) for(int y=0; y<12; y++) for(int z=0; z<12; z++) {
        head->volume->setVoxel(x, y, z, skin);
    }
    // Base features will be redrawn in applyCustomization
    head->volume->updateMesh();

    // --- Arms (Shoulder detail) ---
    for (auto arm : {lArm, rArm}) {
        arm->volume = new VoxelVolume(6, 12, 6); // Wider for shoulder
        arm->pivot = glm::vec3(3, 10, 3);
        for(int x=1; x<5; x++) for(int y=0; y<10; y++) for(int z=1; z<5; z++) {
            arm->volume->setVoxel(x, y, z, (y > 6 ? shirt : skin));
        }
        // Shoulder pads
        for(int x=0; x<6; x++) for(int y=9; y<12; y++) for(int z=0; z<6; z++) {
            arm->volume->setVoxel(x, y, z, shirt);
        }
        arm->volume->updateMesh();
    }
    lArm->localPos = glm::vec3(-6, 10, 0);
    rArm->localPos = glm::vec3(6, 10, 0);

    // --- Legs (Knee detail) ---
    for (auto leg : {lLeg, rLeg}) {
        leg->volume = new VoxelVolume(6, 12, 6);
        leg->pivot = glm::vec3(3, 11, 3);
        for(int x=1; x<5; x++) for(int y=0; y<11; y++) for(int z=1; z<5; z++) {
            leg->volume->setVoxel(x, y, z, pants);
        }
        // Knee bulges
        for(int x=1; x<5; x++) for(int y=5; y<7; y++) leg->volume->setVoxel(x, y, 5, pants);
        // Boots
        Voxel bootColor = {20, 20, 20, 255};
        for(int x=0; x<6; x++) for(int y=0; y<3; y++) for(int z=0; z<6; z++) {
            leg->volume->setVoxel(x, y, z, bootColor);
        }
        leg->volume->updateMesh();
    }
    lLeg->localPos = glm::vec3(-2.5, 0, 0);
    rLeg->localPos = glm::vec3(2.5, 0, 0);
    
    applyCustomization(); // Apply hair/ears/armor
}

void BipedalRig::update(float dt, float velocity) {
    animTime += dt;
    
    // Idle breathing
    float breathe = sinf(animTime * 2.0f) * 1.5f;
    torso->localRot.x = breathe;
    head->localRot.x = -breathe * 0.5f;

    // Walk cycle
    if (velocity > 0.1f) {
        float walkSpeed = velocity * 2.0f;
        float swing = sinf(animTime * walkSpeed * 5.0f) * 30.0f;
        
        lArm->localRot.x = swing;
        rArm->localRot.x = -swing;
        lLeg->localRot.x = -swing;
        rLeg->localRot.x = swing;
    } else {
        // Return to neutral
        lArm->localRot.x = glm::mix(lArm->localRot.x, 0.0f, dt * 5.0f);
        rArm->localRot.x = glm::mix(rArm->localRot.x, 0.0f, dt * 5.0f);
        lLeg->localRot.x = glm::mix(lLeg->localRot.x, 0.0f, dt * 5.0f);
        rLeg->localRot.x = glm::mix(rLeg->localRot.x, 0.0f, dt * 5.0f);
    }
}

void BipedalRig::applyCustomization() {
    if (!head || !head->volume || !torso || !torso->volume) return;
    
    // --- Base Body Reset ---
    Voxel skin = {210, 160, 130, 255};
    Voxel shirt = {50, 100, 200, 255};
    Voxel pants = {30, 30, 30, 255};
    // Note: In a real system, these would be members of the rig for full customization

    // Reset Torso
    for(int x=0; x<10; x++) for(int y=0; y<12; y++) for(int z=0; z<8; z++) torso->volume->setVoxel(x, y, z, shirt);
    for(int x=2; x<8; x++) for(int y=8; y<11; y++) torso->volume->setVoxel(x, y, 7, shirt);

    // Reset Arms
    for (auto arm : {lArm, rArm}) {
        for(int x=0; x<6; x++) for(int y=0; y<12; y++) for(int z=0; z<6; z++) arm->volume->setVoxel(x, y, z, {0,0,0,0});
        for(int x=1; x<5; x++) for(int y=0; y<10; y++) for(int z=1; z<5; z++) arm->volume->setVoxel(x, y, z, (y > 6 ? shirt : skin));
        for(int x=0; x<6; x++) for(int y=9; y<12; y++) for(int z=0; z<6; z++) arm->volume->setVoxel(x, y, z, shirt);
    }

    // Reset Legs
    for (auto leg : {lLeg, rLeg}) {
        for(int x=0; x<6; x++) for(int y=0; y<12; y++) for(int z=0; z<6; z++) leg->volume->setVoxel(x, y, z, {0,0,0,0});
        for(int x=1; x<5; x++) for(int y=0; y<11; y++) for(int z=1; z<5; z++) leg->volume->setVoxel(x, y, z, pants);
        for(int x=1; x<5; x++) for(int y=5; y<7; y++) leg->volume->setVoxel(x, y, 5, pants);
        Voxel bootColor = {20, 20, 20, 255};
        for(int x=0; x<6; x++) for(int y=0; y<3; y++) for(int z=0; z<6; z++) leg->volume->setVoxel(x, y, z, bootColor);
    }

    // --- Head Redraw ---
    for(int x=0; x<12; x++) for(int y=0; y<12; y++) for(int z=0; z<12; z++) {
        head->volume->setVoxel(x, y, z, {210, 160, 130, 255});
    }

    // Detailed Hair
    if (hairStyle == 1) { // Short/Messy
        for(int x=1; x<11; x++) for(int z=1; z<11; z++) {
            head->volume->setVoxel(x, 11, z, hairColor);
            if ((x+z)%2 == 0) head->volume->setVoxel(x, 10, z, hairColor);
        }
        for(int ey : {8,9,10}) for(int ex : {0,11}) for(int ez : {3,4,5,6,7,8}) head->volume->setVoxel(ex, ey, ez, hairColor);
    } else if (hairStyle == 2) { // Long/Flowing
        for(int x=0; x<12; x++) for(int z=0; z<12; z++) head->volume->setVoxel(x, 11, z, hairColor);
        for(int ex : {0,1,10,11}) for(int ey=2; ey<11; ey++) for(int ez=0; ez<11; ez++) {
             if (ez < 10) head->volume->setVoxel(ex, ey, ez, hairColor);
        }
        for(int x=0; x<12; x++) for(int y=2; y<11; y++) head->volume->setVoxel(x, y, 0, hairColor);
    }

    // Eyes: 2x2 blocks
    for(int ex : {2,3, 8,9}) for(int ey : {6,7}) {
        head->volume->setVoxel(ex, ey, 11, {255, 255, 255, 255});
        if (ey == 7) head->volume->setVoxel(ex, ey, 11, eyeColor);
    }

    // Ears
    if (earType == 1) { // Human
        for(int ey:{4,5}) { head->volume->setVoxel(0, ey, 6, skin); head->volume->setVoxel(11, ey, 6, skin); }
    } else if (earType == 2) { // Elven
        for(int ey:{4,5,6,7}) { head->volume->setVoxel(0, ey, 6-ey+4, skin); head->volume->setVoxel(11, ey, 6-ey+4, skin); }
    }
    head->volume->updateMesh();

    // --- Armor Overlay ---
    Voxel armorCol = {0,0,0,0};
    if (armorType == 1) armorCol = {40, 120, 40, 255}; // Cloth (Green)
    else if (armorType == 2) armorCol = {100, 60, 30, 255}; // Leather (Brown)
    else if (armorType == 3) armorCol = {180, 180, 200, 255}; // Heavy (Silver)

    if (armorType > 0) {
        // Torso Armor
        for(int x=0; x<10; x++) for(int y=0; y<12; y++) for(int z=0; z<8; z++) {
            if (x==0 || x==9 || z==0 || z==7 || y==0 || y==11) {
                // Add "trim" for heavy armor
                if (armorType == 3 && (y == 0 || y == 11)) torso->volume->setVoxel(x, y, z, {220, 220, 100, 255}); // Gold trim
                else torso->volume->setVoxel(x, y, z, armorCol);
            }
        }
        
        // Shoulder Armor
        for (auto arm : {lArm, rArm}) {
            for(int x=0; x<6; x++) for(int y=8; y<12; y++) for(int z=0; z<6; z++) {
                arm->volume->setVoxel(x, y, z, armorCol);
            }
        }

        // Leg/Knee Armor
        for (auto leg : {lLeg, rLeg}) {
            for(int x=0; x<6; x++) for(int y=4; y<8; y++) for(int z=0; z<6; z++) {
                if (x==0 || x==5 || z==5) leg->volume->setVoxel(x, y, z, armorCol);
            }
        }
    }

    torso->volume->updateMesh();
    lArm->volume->updateMesh();
    rArm->volume->updateMesh();
    lLeg->volume->updateMesh();
    rLeg->volume->updateMesh();
}

QuadrupedRig::QuadrupedRig() {
    root = new CharacterNode("Root");
    body = new CharacterNode("Body");
    head = new CharacterNode("Head");
    flLeg = new CharacterNode("FLLeg");
    frLeg = new CharacterNode("FRLeg");
    blLeg = new CharacterNode("BLLeg");
    brLeg = new CharacterNode("BRLeg");
    tail = new CharacterNode("Tail");

    root->addChild(body);
    body->addChild(head);
    body->addChild(flLeg);
    body->addChild(frLeg);
    body->addChild(blLeg);
    body->addChild(brLeg);
    body->addChild(tail);
}

void QuadrupedRig::update(float dt, float velocity) {
    animTime += dt;
    
    // Breathing/Idle
    float breathe = sinf(animTime * 1.5f) * 1.0f;
    body->localPos.y = breathe * 0.2f;
    head->localRot.x = breathe;

    // Gallop/Walk
    if (velocity > 0.1f) {
        float speed = velocity * 3.0f;
        float swing = sinf(animTime * speed * 5.0f) * 25.0f;
        
        flLeg->localRot.x = swing;
        frLeg->localRot.x = -swing;
        blLeg->localRot.x = -swing;
        brLeg->localRot.x = swing;
        tail->localRot.y = sinf(animTime * speed * 4.0f) * 20.0f;
    } else {
        flLeg->localRot.x = glm::mix(flLeg->localRot.x, 0.0f, dt * 5.0f);
        frLeg->localRot.x = glm::mix(frLeg->localRot.x, 0.0f, dt * 5.0f);
        blLeg->localRot.x = glm::mix(blLeg->localRot.x, 0.0f, dt * 5.0f);
        brLeg->localRot.x = glm::mix(brLeg->localRot.x, 0.0f, dt * 5.0f);
    }
}
