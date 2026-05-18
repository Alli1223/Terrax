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
    Voxel skin  = male ? Voxel{210, 160, 130, 255} : Voxel{230, 180, 150, 255};
    Voxel shirt = male ? Voxel{50, 100, 200, 255} : Voxel{200, 50, 100, 255};
    Voxel pants = {30, 30, 30, 255};

    // --- Torso (Short) ---
    torso->volume = new VoxelVolume(10, 8, 8);
    torso->pivot = glm::vec3(5, 0, 4);
    torso->localPos = glm::vec3(0, 7, 0); 
    for(int x=0; x<10; x++) for(int y=0; y<8; y++) for(int z=0; z<8; z++) torso->volume->setVoxel(x, y, z, shirt);
    torso->volume->updateMesh();

    // --- Head (Large) ---
    head->volume = new VoxelVolume(24, 24, 24);
    head->pivot = glm::vec3(12, 0, 12);
    head->localPos = glm::vec3(0, 8, 0); 
    for(int x=6; x<18; x++) for(int y=0; y<12; y++) for(int z=6; z<18; z++) {
        float dx=x+0.5f-12.0f, dy=y+0.5f-6.0f, dz=z+0.5f-12.0f;
        if (dx*dx + dy*dy + dz*dz <= 72.25f) head->volume->setVoxel(x, y, z, skin);
    }
    head->volume->updateMesh();

    // --- Arms ---
    for (auto arm : {lArm, rArm}) {
        arm->volume = new VoxelVolume(6, 8, 6);
        arm->pivot = glm::vec3(3, 7, 3);
        for(int x=1; x<5; x++) for(int y=0; y<7; y++) for(int z=1; z<5; z++) arm->volume->setVoxel(x, y, z, (y > 4 ? shirt : skin));
        for(int x=0; x<6; x++) for(int y=6; y<8; y++) for(int z=0; z<6; z++) arm->volume->setVoxel(x, y, z, shirt);
        arm->volume->updateMesh();
    }
    lArm->localPos = glm::vec3(-5, 7, 0); 
    rArm->localPos = glm::vec3(5, 7, 0);

    // --- Legs (Stumpy) ---
    for (auto leg : {lLeg, rLeg}) {
        leg->volume = new VoxelVolume(7, 7, 7); 
        leg->pivot = glm::vec3(3.5f, 7, 3.5f);
        for(int x=0; x<7; x++) for(int y=0; y<7; y++) for(int z=0; z<7; z++) leg->volume->setVoxel(x, y, z, pants);
        for(int x=0; x<7; x++) for(int y=0; y<2; y++) for(int z=0; z<7; z++) leg->volume->setVoxel(x, y, z, {20, 20, 20, 255});
        leg->volume->updateMesh();
    }
    lLeg->localPos = glm::vec3(-2.5, 0, 0);
    rLeg->localPos = glm::vec3(2.5, 0, 0);

    // --- Weapon ---
    sword->volume = new VoxelVolume(4, 20, 2);
    sword->pivot = glm::vec3(2, 2, 1);
    sword->localPos = glm::vec3(0, -6, 0); 
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
    Voxel skin     = {210, 160, 130, 255};
    Voxel noseSkin = {190, 140, 110, 255};
    Voxel hair     = hairColor;
    Voxel eye      = eyeColor;
    Voxel white    = {255, 255, 255, 255};

    // --- Reset head to blank sphere ---
    for(int x=0; x<24; x++) for(int y=0; y<24; y++) for(int z=0; z<24; z++)
        head->volume->setVoxel(x, y, z, {0,0,0,0});
    for(int x=6; x<18; x++) for(int y=0; y<12; y++) for(int z=6; z<18; z++) {
        float dx=x+0.5f-12.0f, dy=y+0.5f-6.0f, dz=z+0.5f-12.0f;
        if (dx*dx + dy*dy + dz*dz <= 72.25f) head->volume->setVoxel(x, y, z, skin);
    }

    // --- Eyebrows ---
    switch (eyebrowStyle) {
        case 1: // Arched
            head->volume->setVoxel(8,8,18,hair);  head->volume->setVoxel(9,9,18,hair);  head->volume->setVoxel(10,10,18,hair);
            head->volume->setVoxel(13,10,18,hair); head->volume->setVoxel(14,9,18,hair); head->volume->setVoxel(15,8,18,hair); break;
        case 2: // Thick
            for(int ex:{8,9,10,13,14,15}) { head->volume->setVoxel(ex,9,18,hair); head->volume->setVoxel(ex,10,18,hair); } break;
        default: // Straight (0) and fallback
            for(int ex:{8,9,10,13,14,15}) head->volume->setVoxel(ex,9,18,hair);
            break;
    }

    // --- Nose ---
    switch (noseStyle) {
        case 1: // Wide
            for(int nx=10; nx<=13; nx++) head->volume->setVoxel(nx,5,18,noseSkin);
            break;
        case 2: // Narrow
            head->volume->setVoxel(12,5,18,noseSkin);
            head->volume->setVoxel(12,6,18,noseSkin);
            break;
        case 3: // Upturned
            head->volume->setVoxel(11,6,18,noseSkin); head->volume->setVoxel(12,6,18,noseSkin);
            head->volume->setVoxel(11,5,17,noseSkin); head->volume->setVoxel(12,5,17,noseSkin);
            break;
        case 4: // Broad
            for(int nx=10; nx<=13; nx++) {
                head->volume->setVoxel(nx,4,18,noseSkin);
                head->volume->setVoxel(nx,5,18,noseSkin);
            }
            head->volume->setVoxel(11,6,18,noseSkin); head->volume->setVoxel(12,6,18,noseSkin);
            break;
        default: // Button (0)
            head->volume->setVoxel(11,5,18,noseSkin);
            head->volume->setVoxel(12,5,18,noseSkin);
            break;
    }

    // --- Eyes ---
    switch (eyeType) {
        case 1: // Happy
            for(int ex:{8,9,10,13,14,15}) {
                head->volume->setVoxel(ex,7,17,eye);
                if(ex==8||ex==10||ex==13||ex==15) head->volume->setVoxel(ex,6,17,eye);
            }
            break;
        case 4: // Heart
            for(int dx:{0,6}) {
                int bx=8+dx;
                head->volume->setVoxel(bx,7,17,eye); head->volume->setVoxel(bx+2,7,17,eye);
                for(int i=0; i<3; i++) head->volume->setVoxel(bx+i,6,17,eye);
                head->volume->setVoxel(bx+1,5,17,eye);
            }
            break;
        default: // Classic (0) and fallback
            for(int ex:{8,9,14,15}) for(int ey:{6,7}) {
                head->volume->setVoxel(ex,ey,17,white);
                if(ey==7) head->volume->setVoxel(ex,ey,17,eye);
            }
            break;
    }

    // --- Ears ---
    switch (earType) {
        case 1: // Human
            for(int ey:{4,5}) for(int ez:{11,12}) {
                head->volume->setVoxel(5,ey,ez,skin);
                head->volume->setVoxel(18,ey,ez,skin);
            }
            break;
        case 2: // Elven (tall with pointed tip)
            for(int ey=3; ey<=6; ey++) for(int ez:{11,12}) {
                head->volume->setVoxel(5,ey,ez,skin);
                head->volume->setVoxel(18,ey,ez,skin);
            }
            head->volume->setVoxel(4,7,11,skin);
            head->volume->setVoxel(19,7,11,skin);
            break;
        case 3: // Rounded (wide and rounded)
            for(int ey=3; ey<=6; ey++) for(int ez=10; ez<=13; ez++) {
                head->volume->setVoxel(4,ey,ez,skin);
                head->volume->setVoxel(19,ey,ez,skin);
            }
            for(int ez:{10,11,12,13}) {
                head->volume->setVoxel(3,4,ez,skin); head->volume->setVoxel(3,5,ez,skin);
                head->volume->setVoxel(20,4,ez,skin); head->volume->setVoxel(20,5,ez,skin);
            }
            break;
        case 4: // Wide (large flared)
            for(int ey:{3,4,5,6}) for(int ez:{10,11,12,13}) {
                head->volume->setVoxel(3,ey,ez,skin);  head->volume->setVoxel(4,ey,ez,skin);
                head->volume->setVoxel(19,ey,ez,skin); head->volume->setVoxel(20,ey,ez,skin);
            }
            break;
        default: // None (0)
            break;
    }

    // --- Hair ---
    // Helper: paint sphere top surface at y=11
    auto paintTopCap = [&]() {
        for(int x=7; x<17; x++) for(int z=7; z<17; z++) {
            float dx=x+0.5f-12.0f, dy=5.5f, dz=z+0.5f-12.0f;
            if(dx*dx+dy*dy+dz*dz <= 72.25f) head->volume->setVoxel(x,11,z,hair);
        }
    };

    switch (hairStyle) {
        case 0: // Bald
            break;
        case 1: // Crew Cut
            paintTopCap();
            for(int x=8; x<16; x++) for(int z=8; z<16; z++) head->volume->setVoxel(x,12,z,hair);
            break;
        case 2: // Messy Short
            paintTopCap();
            for(int x=8; x<16; x++) for(int z=8; z<16; z++) {
                if((x+z)%2 == 0)    head->volume->setVoxel(x,12,z,hair);
                if((x*2+z)%5 == 0)  head->volume->setVoxel(x,13,z,hair);
            }
            break;
        case 3: // Mohawk
            for(int z=7; z<17; z++) { head->volume->setVoxel(11,11,z,hair); head->volume->setVoxel(12,11,z,hair); }
            for(int y=12; y<=16; y++) for(int z=7; z<17; z++) { head->volume->setVoxel(11,y,z,hair); head->volume->setVoxel(12,y,z,hair); }
            break;
        case 4: // Spiky
            paintTopCap();
            for(int sx=8; sx<17; sx+=2) for(int sz=8; sz<17; sz+=2) {
                int ht = 12 + (sx+sz)%4;
                for(int y=12; y<=ht; y++) head->volume->setVoxel(sx,y,sz,hair);
            }
            break;
        case 5: // Side Swept (swept right)
            for(int x=6; x<18; x++) for(int z=6; z<18; z++) {
                float dx=x+0.5f-12.0f, dy=5.5f, dz=z+0.5f-12.0f;
                if(dx*dx+dy*dy+dz*dz <= 72.25f) head->volume->setVoxel(x,11,z,hair);
            }
            for(int x=7; x<17; x++) for(int z=7; z<17; z++) head->volume->setVoxel(x,12,z,hair);
            for(int y=8; y<=12; y++) for(int z=7; z<17; z++) {
                head->volume->setVoxel(17,y,z,hair);
                head->volume->setVoxel(18,y,z,hair);
            }
            break;
        case 6: // Bob (chin length, all sides)
            paintTopCap();
            for(int y=3; y<=11; y++) {
                for(int z=6; z<18; z++) { head->volume->setVoxel(5,y,z,hair); head->volume->setVoxel(18,y,z,hair); }
                for(int x=5; x<19; x++) head->volume->setVoxel(x,y,5,hair);
            }
            for(int x=5; x<19; x++) for(int z=5; z<19; z++) head->volume->setVoxel(x,3,z,hair);
            break;
        case 7: // Long Straight
            paintTopCap();
            for(int y=0; y<=11; y++) {
                for(int z=6; z<18; z++) { head->volume->setVoxel(5,y,z,hair); head->volume->setVoxel(18,y,z,hair); }
                for(int x=5; x<19; x++) head->volume->setVoxel(x,y,5,hair);
            }
            break;
        case 8: // Wavy Long
            paintTopCap();
            for(int y=0; y<=11; y++) {
                int w = (y/2)%2;
                for(int z=6; z<18; z++) { head->volume->setVoxel(5+w,y,z,hair); head->volume->setVoxel(18-w,y,z,hair); }
                for(int x=5+w; x<19-w; x++) head->volume->setVoxel(x,y,5+w,hair);
            }
            break;
        case 9: // Bun
            for(int y=12; y<=15; y++) {
                int r = 3 - (y - 12);
                for(int x=12-r; x<=12+r; x++) for(int z=12-r; z<=12+r; z++) head->volume->setVoxel(x,y,z,hair);
            }
            for(int x=8; x<16; x++) for(int z=8; z<16; z++) head->volume->setVoxel(x,11,z,hair);
            for(int y=7; y<=11; y++) for(int x=7; x<17; x++) head->volume->setVoxel(x,y,5,hair);
            break;
        case 10: // Pigtails
            paintTopCap();
            for(int y=1; y<=8; y++) for(int z=9; z<=14; z++) {
                head->volume->setVoxel(5,y,z,hair);
                if(y<=6) head->volume->setVoxel(4,y,z,hair);
                head->volume->setVoxel(18,y,z,hair);
                if(y<=6) head->volume->setVoxel(19,y,z,hair);
            }
            break;
        case 11: // Braided (center back braid with side curtains)
            paintTopCap();
            for(int y=0; y<=11; y++) {
                bool cross = (y/2)%2 == 0;
                int bx0 = cross ? 10 : 11, bx1 = cross ? 13 : 14;
                for(int x=bx0; x<=bx1; x++) head->volume->setVoxel(x,y,5,hair);
                if(y%2==0) for(int x=bx0; x<=bx1; x++) head->volume->setVoxel(x,y,4,hair);
                for(int z=6; z<18; z++) { head->volume->setVoxel(5,y,z,hair); head->volume->setVoxel(18,y,z,hair); }
            }
            break;
    }

    // --- Armor ---
    Voxel armorCol = {0,0,0,0};
    if (armorType == 1) armorCol = {40, 120, 40, 255};
    else if (armorType == 2) armorCol = {100, 60, 30, 255};
    else if (armorType == 3) armorCol = {180, 180, 200, 255};
    if (armorType > 0) {
        for(int x=0; x<10; x++) for(int y=0; y<8; y++) for(int z=0; z<8; z++) {
            if (x==0 || x==9 || z==0 || z==7 || y==0 || y==7) torso->volume->setVoxel(x, y, z, armorCol);
        }
    }

    torso->scale.x = weightScale;
    torso->scale.z = weightScale;

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
