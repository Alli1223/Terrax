#include "voxel_model.h"
#include <iostream>
#include <algorithm>

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

BipedalRig::BipedalRig() {
    root = new CharacterNode("Root");
    torso = new CharacterNode("Torso");
    head = new CharacterNode("Head");
    lArm = new CharacterNode("LArm");
    rArm = new CharacterNode("RArm");
    lLeg = new CharacterNode("LLeg");
    rLeg = new CharacterNode("RLeg");
    sword = new CharacterNode("Sword");
    root->addChild(torso);
    torso->addChild(head);
    torso->addChild(lArm);
    torso->addChild(rArm);
    torso->addChild(lLeg);
    torso->addChild(rLeg);
    rArm->addChild(sword);
}

void BipedalRig::setupDefaultHuman(bool male) {
    Voxel skin = male ? Voxel{210, 160, 130, 255} : Voxel{230, 180, 150, 255};
    Voxel shirt = male ? Voxel{50, 100, 200, 255} : Voxel{200, 50, 100, 255};
    Voxel pants = {30, 30, 30, 255};

    // Total Height: Legs(11) + Torso(12) + Head(12) = 35.
    // Torso node at hip (Y=11). Mesh is 10x12x8.
    torso->volume = new VoxelVolume(10, 12, 8);
    torso->pivot = glm::vec3(5, 0, 4);
    torso->localPos = glm::vec3(0, 11, 0); 
    for(int x=0; x<10; x++) for(int y=0; y<12; y++) for(int z=0; z<8; z++) torso->volume->setVoxel(x, y, z, shirt);
    torso->volume->updateMesh();

    // --- Head ---
    // Increased size to 24x24x24 to allow for protruding features (nose, ears, big hair)
    // Core head is 12x12x12, centered at (x=6..17, y=0..11, z=6..17) inside this volume.
    head->volume = new VoxelVolume(24, 24, 24);
    head->pivot = glm::vec3(12, 0, 12); // Center of the 24x24 base
    head->localPos = glm::vec3(0, 12, 0); // Sits at torso top
    for(int x=6; x<18; x++) for(int y=0; y<12; y++) for(int z=6; z<18; z++) head->volume->setVoxel(x, y, z, skin);
    head->volume->updateMesh();

    for (auto arm : {lArm, rArm}) {
        arm->volume = new VoxelVolume(6, 12, 6);
        arm->pivot = glm::vec3(3, 10, 3);
        for(int x=1; x<5; x++) for(int y=0; y<10; y++) for(int z=1; z<5; z++) arm->volume->setVoxel(x, y, z, (y > 6 ? shirt : skin));
        for(int x=0; x<6; x++) for(int y=9; y<12; y++) for(int z=0; z<6; z++) arm->volume->setVoxel(x, y, z, shirt);
        arm->volume->updateMesh();
    }
    lArm->localPos = glm::vec3(-5, 10, 0); 
    rArm->localPos = glm::vec3(5, 10, 0);

    for (auto leg : {lLeg, rLeg}) {
        leg->volume = new VoxelVolume(6, 12, 6);
        leg->pivot = glm::vec3(3, 11, 3);
        for(int x=1; x<5; x++) for(int y=0; y<11; y++) for(int z=1; z<5; z++) leg->volume->setVoxel(x, y, z, pants);
        for(int x=0; x<6; x++) for(int y=0; y<3; y++) for(int z=0; z<6; z++) leg->volume->setVoxel(x, y, z, {20, 20, 20, 255});
        leg->volume->updateMesh();
    }
    lLeg->localPos = glm::vec3(-2.5, 0, 0);
    rLeg->localPos = glm::vec3(2.5, 0, 0);

    sword->volume = new VoxelVolume(4, 20, 2);
    sword->pivot = glm::vec3(2, 2, 1);
    sword->localPos = glm::vec3(0, -10, 0); // Bottom of arm
    sword->localRot = glm::vec3(90, 0, 0);
    for(int x=0; x<4; x++) for(int y=0; y<20; y++) for(int z=0; z<2; z++) {
        if (y < 4) sword->volume->setVoxel(x, y, z, {100, 60, 20, 255});
        else if (y < 6) sword->volume->setVoxel(x, y, z, {50, 50, 50, 255});
        else sword->volume->setVoxel(x, y, z, {180, 180, 190, 255});
    }
    sword->volume->updateMesh();

    applyCustomization();
}

void BipedalRig::update(float dt, float velocity) {
    animTime += dt;
    float breathe = sinf(animTime * 2.0f) * 1.5f;
    torso->localRot.x = breathe;
    head->localRot.x = -breathe * 0.5f;
    if (isAttacking) {
        attackAnim += dt * 5.0f;
        if (attackAnim > 1.0f) { isAttacking = false; attackAnim = 0.0f; }
        float swing = sinf(attackAnim * 3.14159f) * 90.0f;
        rArm->localRot.x = -swing;
        rArm->localRot.y = swing * 0.5f;
    } else if (velocity > 0.1f) {
        float swing = sinf(animTime * velocity * 0.5f * 5.0f) * 30.0f;
        lArm->localRot.x = swing; rArm->localRot.x = -swing;
        lLeg->localRot.x = -swing; rLeg->localRot.x = swing;
    } else {
        lArm->localRot.x = glm::mix(lArm->localRot.x, 0.0f, dt * 5.0f);
        rArm->localRot.x = glm::mix(rArm->localRot.x, 0.0f, dt * 5.0f);
        lLeg->localRot.x = glm::mix(lLeg->localRot.x, 0.0f, dt * 5.0f);
        rLeg->localRot.x = glm::mix(rLeg->localRot.x, 0.0f, dt * 5.0f);
    }
}

void BipedalRig::applyCustomization() {
    if (!head || !head->volume || !torso || !torso->volume) return;
    
    // --- Base Body Colors ---
    Voxel skin  = {210, 160, 130, 255};
    Voxel shirt = {50, 100, 200, 255};
    Voxel pants = {30, 30, 30, 255};

    // --- Reset Head (24x24x24) ---
    for(int x=0; x<24; x++) for(int y=0; y<24; y++) for(int z=0; z<24; z++) {
        head->volume->setVoxel(x, y, z, {0,0,0,0});
    }
    // Redraw core head (6..17, 0..11, 6..17)
    for(int x=6; x<18; x++) for(int y=0; y<12; y++) for(int z=6; z<18; z++) {
        head->volume->setVoxel(x, y, z, skin);
    }

    // --- 3D Facial Features (Relative to core head) ---
    // Physical Eyebrows (protruding at z=18, front of z=17 face)
    for(int ex : {8,9,10, 13,14,15}) {
        head->volume->setVoxel(ex, 8, 18, hairColor); 
    }

    // Physical Nose (protruding at z=18)
    head->volume->setVoxel(11, 5, 18, {190, 140, 110, 255});
    head->volume->setVoxel(12, 5, 18, {190, 140, 110, 255});

    // Eyes: 2x2 blocks on front face (z=17)
    for(int ex : {8,9, 14,15}) for(int ey : {6,7}) {
        head->volume->setVoxel(ex, ey, 17, {255, 255, 255, 255}); // Sclera
        if (ey == 7) head->volume->setVoxel(ex, ey, 17, eyeColor); // Pupil
    }

    // Physical Ears (protruding from sides x=5 and x=18)
    if (earType == 1) { // Human
        for(int ey:{4,5}) for(int ez:{11,12}) {
            head->volume->setVoxel(5, ey, ez, skin);
            head->volume->setVoxel(18, ey, ez, skin);
        }
    } else if (earType == 2) { // Elven (Pointed)
        for(int ey:{4,5,6,7}) {
            int depth = 12 - (ey - 4);
            head->volume->setVoxel(5, ey, depth, skin);
            head->volume->setVoxel(18, ey, depth, skin);
        }
    }
// --- Detailed Hairstyles (3D Volumes) ---
// Core head top is y=11. x:6..17, z:6..17.
switch (hairStyle) {
    case 1: { // Crew Cut / Short Messy
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) {
            head->volume->setVoxel(x, 12, z, hairColor);
            if ((x+z)%2 == 0) head->volume->setVoxel(x, 13, z, hairColor);
        }
        for(int y=8; y<12; y++) for(int z=8; z<16; z++) {
            head->volume->setVoxel(5, y, z, hairColor);
            head->volume->setVoxel(18, y, z, hairColor);
        }
        break;
    }
    case 2: { // Long / Flowing
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) head->volume->setVoxel(x, 12, z, hairColor);
        for(int y=0; y<12; y++) {
            for(int x=6; x<18; x++) head->volume->setVoxel(x, y, 5, hairColor); // Back
            for(int z=6; z<17; z++) {
                head->volume->setVoxel(5, y, z, hairColor); // Left
                head->volume->setVoxel(18, y, z, hairColor); // Right
            }
        }
        break;
    }
    case 3: { // Mohawk
        for(int z=6; z<18; z++) {
            for(int y=12; y<15; y++) {
                head->volume->setVoxel(11, y, z, hairColor);
                head->volume->setVoxel(12, y, z, hairColor);
            }
        }
        break;
    }
    case 4: { // Spiky
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) head->volume->setVoxel(x, 12, z, hairColor);
        for(int x=7; x<17; x+=3) for(int z=7; z<17; z+=3) {
            head->volume->setVoxel(x, 13, z, hairColor);
            head->volume->setVoxel(x, 14, z, hairColor);
            head->volume->setVoxel(x+1, 13, z, hairColor);
        }
        break;
    }
    case 5: { // Side Swept
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) head->volume->setVoxel(x, 12, z, hairColor);
        for(int y=6; y<12; y++) for(int z=8; z<17; z++) {
            for(int x=18; x<21; x++) head->volume->setVoxel(x, y, z, hairColor);
        }
        for(int x=8; x<18; x++) for(int z=17; z<19; z++) head->volume->setVoxel(x, 11, z, hairColor);
        break;
    }
    case 6: { // Bob
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) head->volume->setVoxel(x, 12, z, hairColor);
        for(int y=4; y<12; y++) {
            for(int x=6; x<18; x++) head->volume->setVoxel(x, y, 5, hairColor);
            for(int z=7; z<17; z++) {
                head->volume->setVoxel(5, y, z, hairColor);
                head->volume->setVoxel(18, y, z, hairColor);
            }
        }
        break;
    }
    case 7: { // Long Straight (Full)
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) head->volume->setVoxel(x, 12, z, hairColor);
        for(int y=0; y<12; y++) {
            for(int x=5; x<19; x++) head->volume->setVoxel(x, y, 5, hairColor);
            for(int z=6; z<18; z++) {
                head->volume->setVoxel(5, y, z, hairColor);
                head->volume->setVoxel(18, y, z, hairColor);
            }
        }
        break;
    }
    case 8: { // Wavy Long
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) {
            head->volume->setVoxel(x, 12, z, hairColor);
            if ((x+z)%3 == 0) head->volume->setVoxel(x, 13, z, hairColor);
        }
        for(int y=0; y<12; y++) {
            float wave = sinf(y * 0.5f) * 1.5f;
            for(int x=6; x<18; x++) head->volume->setVoxel(x, y, 5 + (int)wave, hairColor);
            for(int z=6; z<17; z++) {
                head->volume->setVoxel(5 + (int)wave, y, z, hairColor);
                head->volume->setVoxel(18 + (int)wave, y, z, hairColor);
            }
        }
        break;
    }
    case 9: { // Bun
        for(int x=8; x<16; x++) for(int z=8; z<16; z++) {
            for(int y=12; y<17; y++) head->volume->setVoxel(x, y, z, hairColor);
        }
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) head->volume->setVoxel(x, 12, z, hairColor);
        break;
    }
    case 10: { // Pigtails
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) head->volume->setVoxel(x, 12, z, hairColor);
        for(int y=4; y<10; y++) for(int ez=9; ez<13; ez++) {
            for(int ex=2; ex<6; ex++) head->volume->setVoxel(ex, y, ez, hairColor);
            for(int ex=18; ex<22; ex++) head->volume->setVoxel(ex, y, ez, hairColor);
        }
        break;
    }
    case 11: { // Braided
        for(int x=6; x<18; x++) for(int z=6; z<18; z++) head->volume->setVoxel(x, 12, z, hairColor);
        for(int y=0; y<12; y++) {
            int off = (y % 4 < 2) ? 0 : 1;
            for(int x=10+off; x<14+off; x++) head->volume->setVoxel(x, y, 5, hairColor);
            for(int x=10+off; x<14+off; x++) head->volume->setVoxel(x, y, 4, hairColor);
        }
        break;
    }
}


    // --- Armor Overlay ---
    Voxel armorCol = {0,0,0,0};
    if (armorType == 1) armorCol = {40, 120, 40, 255}; // Cloth
    else if (armorType == 2) armorCol = {100, 60, 30, 255}; // Leather
    else if (armorType == 3) armorCol = {180, 180, 200, 255}; // Heavy
    
    // Reset Torso for armor
    for(int x=0; x<10; x++) for(int y=0; y<12; y++) for(int z=0; z<8; z++) {
        torso->volume->setVoxel(x, y, z, shirt);
    }

    if (armorType > 0) {
        for(int x=0; x<10; x++) for(int y=0; y<12; y++) for(int z=0; z<8; z++) {
            // Shell of armor
            if (x==0 || x==9 || z==0 || z==7 || y==0 || y==11) {
                if (armorType == 3 && (y == 0 || y == 11)) torso->volume->setVoxel(x, y, z, {220, 220, 100, 255}); // Gold trim
                else torso->volume->setVoxel(x, y, z, armorCol);
            }
        }
    }

    head->volume->updateMesh();
    torso->volume->updateMesh();
}

QuadrupedRig::QuadrupedRig() {
    root = new CharacterNode("Root"); body = new CharacterNode("Body"); head = new CharacterNode("Head");
    flLeg = new CharacterNode("FLLeg"); frLeg = new CharacterNode("FRLeg"); blLeg = new CharacterNode("BLLeg");
    brLeg = new CharacterNode("BRLeg"); tail = new CharacterNode("Tail");
    root->addChild(body); body->addChild(head); body->addChild(flLeg); body->addChild(frLeg);
    body->addChild(blLeg); body->addChild(brLeg); body->addChild(tail);
}

void QuadrupedRig::update(float dt, float velocity) {
    animTime += dt;
    body->localPos.y = sinf(animTime * 1.5f) * 0.2f; head->localRot.x = sinf(animTime * 1.5f) * 1.0f;
    if (velocity > 0.1f) {
        float swing = sinf(animTime * velocity * 0.5f * 5.0f) * 25.0f;
        flLeg->localRot.x = swing; frLeg->localRot.x = -swing; blLeg->localRot.x = -swing; brLeg->localRot.x = swing;
        tail->localRot.y = sinf(animTime * 5.0f) * 20.0f;
    }
}
