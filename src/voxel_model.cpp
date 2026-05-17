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

// Returns true if this voxel (local coords: lx 0-11, y 0-11, z 0-11) belongs to the head.
// Uses graduated x-margins and back-of-head z-rounding to create an oval silhouette.
static bool inHeadCore(int lx, int y, int z) {
    // Vertical oval: head narrows significantly at crown and chin
    int xm = 0;
    if      (y == 0 || y == 11) xm = 3;
    else if (y == 1 || y == 10) xm = 2;
    else if (y == 2 || y == 9)  xm = 1;
    if (lx < xm || lx > 11 - xm) return false;

    // Back-of-head rounding (z=0 is back)
    if (y >= 9  && z == 0) return false;
    if (y >= 10 && z <= 1) return false;
    if (y <= 1  && z == 0) return false;

    // Front-crown corners clipped slightly
    if (y == 11 && z >= 10) return false;

    return true;
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
    Voxel skin  = male ? Voxel{210, 160, 130, 255} : Voxel{230, 180, 150, 255};
    Voxel shirt = male ? Voxel{50, 100, 200, 255}  : Voxel{200, 50, 100, 255};
    Voxel pants = {40, 40, 80, 255};

    // ---------------------------------------------------------------
    // Layout: all parts centred at root x=0, z=0.  feet at root y=0.
    //
    // Transform: root_pos = voxel_pos - pivot + localPos
    //   (localPos lives in parent voxel-space before parent's pivot shift)
    //
    // Parts:       w × h × d   pivot       localPos (in parent space)
    //  Torso       9 × 9 × 7   (4,0,3)     (0, 6, 0)   root x:−4..+4 ✓
    //  Head       14 ×12 ×13   (6.5,0,6)   (4,9,3)     centred on torso top ✓
    //  lArm        4 × 8 × 4   (2,7,2)     (−1,9,3.5)  arm.x=3 → root x=−4 ✓
    //  rArm        4 × 8 × 4   (2,7,2)     (10,9,3.5)  arm.x=0 → root x=+4 ✓
    //  lLeg        4 × 6 × 4   (2,5,2)     (1,0,3)     centre root x=−2 ✓
    //  rLeg        4 × 6 × 4   (2,5,2)     (6,0,3)     centre root x=+3 ✓
    // ---------------------------------------------------------------

    // --- Torso: 9×9×7, centred exactly (odd widths → integer pivot) ---
    torso->volume = new VoxelVolume(9, 9, 7);
    torso->pivot    = glm::vec3(4.0f, 0.0f, 3.0f);
    torso->localPos = glm::vec3(0.0f, 6.0f, 0.0f);
    for(int x=0; x<9; x++) for(int y=0; y<9; y++) for(int z=0; z<7; z++)
        torso->volume->setVoxel(x, y, z, shirt);
    torso->volume->updateMesh();

    // --- Head: 14×15×13, core y=0..11, hair space y=12..14 ---
    head->volume = new VoxelVolume(14, 15, 13);
    head->pivot    = glm::vec3(6.5f, 0.0f, 6.0f);
    head->localPos = glm::vec3(4.0f, 9.0f, 3.0f);
    for(int x=1; x<=12; x++) for(int y=0; y<12; y++) for(int z=0; z<12; z++)
        if (inHeadCore(x-1, y, z))
            head->volume->setVoxel(x, y, z, skin);
    head->volume->updateMesh();

    // --- Arms: 4×8×4 ---
    for (auto arm : {lArm, rArm}) {
        arm->volume = new VoxelVolume(4, 8, 4);
        arm->pivot  = glm::vec3(2.0f, 7.0f, 2.0f);
        for(int x=0; x<4; x++) for(int y=0; y<8; y++) for(int z=0; z<4; z++)
            arm->volume->setVoxel(x, y, z, (y >= 3 ? shirt : skin));
        // Shoulder cap
        for(int x=0; x<4; x++) for(int z=0; z<4; z++)
            arm->volume->setVoxel(x, 7, z, shirt);
        arm->volume->updateMesh();
    }
    lArm->localPos = glm::vec3(-1.0f, 9.0f, 3.5f);
    rArm->localPos = glm::vec3(10.0f, 9.0f, 3.5f);

    // --- Legs: 4×6×4 ---
    for (auto leg : {lLeg, rLeg}) {
        leg->volume = new VoxelVolume(4, 6, 4);
        leg->pivot  = glm::vec3(2.0f, 5.0f, 2.0f);
        // Pants upper
        for(int x=0; x<4; x++) for(int y=2; y<6; y++) for(int z=0; z<4; z++)
            leg->volume->setVoxel(x, y, z, pants);
        // Boots
        Voxel boot = {20, 20, 20, 255};
        for(int x=0; x<4; x++) for(int y=0; y<2; y++) for(int z=0; z<4; z++)
            leg->volume->setVoxel(x, y, z, boot);
        leg->volume->updateMesh();
    }
    lLeg->localPos = glm::vec3(1.0f, 0.0f, 3.0f);
    rLeg->localPos = glm::vec3(6.0f, 0.0f, 3.0f);
    
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
    Voxel skin  = {210, 160, 130, 255};
    Voxel shirt = {50, 100, 200, 255};
    Voxel pants = {40, 40, 80, 255};

    // Reset Torso (9×9×7)
    for(int x=0; x<9; x++) for(int y=0; y<9; y++) for(int z=0; z<7; z++)
        torso->volume->setVoxel(x, y, z, shirt);

    // Reset Arms (4×8×4)
    for (auto arm : {lArm, rArm}) {
        for(int x=0; x<4; x++) for(int y=0; y<8; y++) for(int z=0; z<4; z++)
            arm->volume->setVoxel(x, y, z, {0,0,0,0});
        for(int x=0; x<4; x++) for(int y=0; y<7; y++) for(int z=0; z<4; z++)
            arm->volume->setVoxel(x, y, z, (y >= 3 ? shirt : skin));
        for(int x=0; x<4; x++) for(int z=0; z<4; z++)
            arm->volume->setVoxel(x, 7, z, shirt);
    }

    // Reset Legs (4×6×4)
    for (auto leg : {lLeg, rLeg}) {
        for(int x=0; x<4; x++) for(int y=0; y<6; y++) for(int z=0; z<4; z++)
            leg->volume->setVoxel(x, y, z, {0,0,0,0});
        Voxel boot = {20, 20, 20, 255};
        for(int x=0; x<4; x++) for(int y=0; y<2; y++) for(int z=0; z<4; z++)
            leg->volume->setVoxel(x, y, z, boot);
        for(int x=0; x<4; x++) for(int y=2; y<6; y++) for(int z=0; z<4; z++)
            leg->volume->setVoxel(x, y, z, pants);
    }

    Voxel noseSkin = {182, 132, 102, 255};
    Voxel earSkin  = {220, 168, 138, 255};

    // --- Head Redraw: clear 14×15×13, re-fill rounded core ---
    for(int x=0; x<14; x++) for(int y=0; y<15; y++) for(int z=0; z<13; z++)
        head->volume->setVoxel(x, y, z, {0,0,0,0});
    for(int x=1; x<=12; x++) for(int y=0; y<12; y++) for(int z=0; z<12; z++)
        if (inHeadCore(x-1, y, z))
            head->volume->setVoxel(x, y, z, skin);

    // --- Hair: physical voxels. Core top = y11, raised layers y12-14. ---
    // x coords are in the 14-wide volume (core x=1..12).
    switch (hairStyle) {
        case 1: { // Crew Cut: tight scalp cap
            for(int x=3; x<=10; x++) for(int z=2; z<=9; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            for(int y=9; y<=11; y++) for(int z=2; z<=9; z++) {
                head->volume->setVoxel(2,  y, z, hairColor);
                head->volume->setVoxel(11, y, z, hairColor);
            }
            break;
        }
        case 2: { // Messy Short: full cap + raised clumps
            for(int x=2; x<=11; x++) for(int z=1; z<=10; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            // Irregular raised clumps at y=12
            for(int x=3; x<=10; x+=2) for(int z=2; z<=9; z+=3)
                head->volume->setVoxel(x, 12, z, hairColor);
            for(int x=4; x<=9; x+=3) for(int z=4; z<=7; z+=2)
                head->volume->setVoxel(x, 12, z, hairColor);
            break;
        }
        case 3: { // Mohawk: tall fin tapering toward front and back
            for(int z=0; z<12; z++) {
                head->volume->setVoxel(6, 11, z, hairColor);
                head->volume->setVoxel(7, 11, z, hairColor);
            }
            for(int z=1; z<=10; z++) {
                head->volume->setVoxel(6, 12, z, hairColor);
                head->volume->setVoxel(7, 12, z, hairColor);
            }
            for(int z=2; z<=9; z++) {
                head->volume->setVoxel(6, 13, z, hairColor);
                head->volume->setVoxel(7, 13, z, hairColor);
            }
            for(int z=3; z<=8; z++) {
                head->volume->setVoxel(6, 14, z, hairColor);
                head->volume->setVoxel(7, 14, z, hairColor);
            }
            break;
        }
        case 4: { // Spiky: base cap + individual spike towers
            for(int x=2; x<=11; x++) for(int z=1; z<=10; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            // 3×3 grid of spikes; every other one is taller
            static const int sx[] = {3,6,9, 3,6,9, 3,6,9};
            static const int sz[] = {2,2,2, 5,5,5, 8,8,8};
            for(int i=0; i<9; i++) {
                head->volume->setVoxel(sx[i], 12, sz[i], hairColor);
                if (i % 2 == 0)
                    head->volume->setVoxel(sx[i], 13, sz[i], hairColor);
            }
            break;
        }
        case 5: { // Side Swept: volume swept to one side + fringe
            for(int x=2; x<=11; x++) for(int z=1; z<=10; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            // Raised volume on right side
            for(int x=8; x<=11; x++) for(int z=3; z<=8; z++)
                head->volume->setVoxel(x, 12, z, hairColor);
            // Front fringe sweeping right
            for(int x=3; x<=10; x++) head->volume->setVoxel(x, 10, 11, hairColor);
            // Right-side flow
            for(int y=5; y<=11; y++) for(int z=2; z<=9; z++)
                head->volume->setVoxel(12, y, z, hairColor);
            break;
        }
        case 6: { // Bob: jaw-length curtains framing face
            for(int x=1; x<=12; x++) for(int z=1; z<=10; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            for(int y=3; y<=11; y++) for(int z=1; z<=9; z++) {
                head->volume->setVoxel(1,  y, z, hairColor);
                head->volume->setVoxel(12, y, z, hairColor);
            }
            for(int x=2; x<=11; x++) for(int y=4; y<=10; y++)
                head->volume->setVoxel(x, y, 0, hairColor);
            for(int x=3; x<=10; x++) {
                head->volume->setVoxel(x, 10, 11, hairColor);
                head->volume->setVoxel(x,  9, 11, hairColor);
            }
            break;
        }
        case 7: { // Long Straight: cascades fully down sides and back
            for(int x=1; x<=12; x++) for(int z=0; z<=11; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            for(int y=0; y<=11; y++) for(int z=0; z<=10; z++) {
                head->volume->setVoxel(1,  y, z, hairColor);
                head->volume->setVoxel(12, y, z, hairColor);
            }
            for(int x=2; x<=11; x++) for(int y=0; y<=10; y++)
                head->volume->setVoxel(x, y, 0, hairColor);
            for(int x=3; x<=10; x++) head->volume->setVoxel(x, 10, 11, hairColor);
            break;
        }
        case 8: { // Wavy Long: long with layered wave bumps
            for(int x=1; x<=12; x++) for(int z=0; z<=11; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            for(int y=0; y<=11; y++) for(int z=0; z<=10; z++) {
                head->volume->setVoxel(1,  y, z, hairColor);
                head->volume->setVoxel(12, y, z, hairColor);
            }
            for(int x=2; x<=11; x++) for(int y=0; y<=10; y++)
                head->volume->setVoxel(x, y, 0, hairColor);
            // Wave bumps at y=12 across the crown
            for(int x=3; x<=10; x+=2) for(int z : {2, 5, 8})
                head->volume->setVoxel(x, 12, z, hairColor);
            for(int x=3; x<=10; x++) head->volume->setVoxel(x, 10, 11, hairColor);
            break;
        }
        case 9: { // Bun: compact tiered dome on top
            for(int x=3; x<=10; x++) for(int z=2; z<=9; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            for(int x=5; x<=8; x++) for(int z=4; z<=7; z++) {
                head->volume->setVoxel(x, 12, z, hairColor);
                head->volume->setVoxel(x, 13, z, hairColor);
            }
            for(int x=6; x<=7; x++) for(int z=5; z<=6; z++)
                head->volume->setVoxel(x, 14, z, hairColor);
            break;
        }
        case 10: { // Pigtails: two side bunches
            for(int x=2; x<=11; x++) for(int z=1; z<=10; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            for(int y=5; y<=10; y++) for(int z=3; z<=8; z++) {
                head->volume->setVoxel(0,  y, z, hairColor);
                head->volume->setVoxel(13, y, z, hairColor);
            }
            for(int y=6; y<=9; y++) for(int z=4; z<=7; z++) {
                head->volume->setVoxel(1,  y, z, hairColor);
                head->volume->setVoxel(12, y, z, hairColor);
            }
            break;
        }
        case 11: { // Braided: top + alternating braid down back
            for(int x=2; x<=11; x++) for(int z=1; z<=10; z++)
                head->volume->setVoxel(x, 11, z, hairColor);
            for(int y=4; y<=11; y++) for(int z=1; z<=9; z++) {
                head->volume->setVoxel(1,  y, z, hairColor);
                head->volume->setVoxel(12, y, z, hairColor);
            }
            for(int y=0; y<=10; y++) {
                bool odd = (y % 2 != 0);
                int bx1 = odd ? 5 : 7, bx2 = odd ? 6 : 8;
                head->volume->setVoxel(bx1, y, 0, hairColor);
                head->volume->setVoxel(bx2, y, 0, hairColor);
                head->volume->setVoxel(bx1, y, 1, hairColor);
                head->volume->setVoxel(bx2, y, 1, hairColor);
            }
            break;
        }
        default: break; // Bald
    }

    // --- Eyes: 2×2 blocks on front face z=11 (x shifted by 1 vs old 12-wide head) ---
    for(int ex : {3,4, 9,10}) for(int ey : {6,7}) {
        head->volume->setVoxel(ex, ey, 11, {255, 255, 255, 255});
        if (ey == 7) head->volume->setVoxel(ex, ey, 11, eyeColor);
    }

    // --- Eyebrows: physical voxels protruding at z=12 (1 voxel in front of face) ---
    switch (eyebrowStyle) {
        case 1: // Arched: centre voxel raised
            head->volume->setVoxel(3,  9, 12, hairColor);
            head->volume->setVoxel(4, 10, 12, hairColor);
            head->volume->setVoxel(5,  9, 12, hairColor);
            head->volume->setVoxel(8,  9, 12, hairColor);
            head->volume->setVoxel(9, 10, 12, hairColor);
            head->volume->setVoxel(10, 9, 12, hairColor);
            break;
        case 2: // Thick: two rows deep
            for(int ey:{8,9}) {
                for(int ex:{3,4,5}) head->volume->setVoxel(ex, ey, 12, hairColor);
                for(int ex:{8,9,10}) head->volume->setVoxel(ex, ey, 12, hairColor);
            }
            break;
        case 3: // Thin: single voxel each side
            head->volume->setVoxel(4,  9, 12, hairColor);
            head->volume->setVoxel(9,  9, 12, hairColor);
            break;
        case 4: // Furrowed: inner corners angled down
            head->volume->setVoxel(3,  9, 12, hairColor);
            head->volume->setVoxel(4,  9, 12, hairColor);
            head->volume->setVoxel(5,  8, 12, hairColor);
            head->volume->setVoxel(8,  8, 12, hairColor);
            head->volume->setVoxel(9,  9, 12, hairColor);
            head->volume->setVoxel(10, 9, 12, hairColor);
            break;
        default: // Straight
            for(int ex:{3,4,5})   head->volume->setVoxel(ex, 9, 12, hairColor);
            for(int ex:{8,9,10})  head->volume->setVoxel(ex, 9, 12, hairColor);
            break;
    }

    // --- Nose: skin-toned voxel(s) protruding at z=12 ---
    switch (noseStyle) {
        case 1: // Wide
            for(int nx:{5,6,7,8}) head->volume->setVoxel(nx, 5, 12, noseSkin);
            break;
        case 2: // Narrow: single voxel
            head->volume->setVoxel(6, 5, 12, noseSkin);
            break;
        case 3: // Upturned: sits one row higher
            head->volume->setVoxel(6, 6, 12, noseSkin);
            head->volume->setVoxel(7, 6, 12, noseSkin);
            break;
        case 4: // Broad: wide 2-row bridge
            for(int nx:{5,6,7,8}) head->volume->setVoxel(nx, 5, 12, noseSkin);
            head->volume->setVoxel(6, 6, 12, noseSkin);
            head->volume->setVoxel(7, 6, 12, noseSkin);
            break;
        default: // Button
            head->volume->setVoxel(6, 5, 12, noseSkin);
            head->volume->setVoxel(7, 5, 12, noseSkin);
            break;
    }

    // --- Ears: physical voxels at x=0 (left) and x=13 (right), protruding from the head sides ---
    switch (earType) {
        case 1: { // Human: small rounded square
            for(int ey:{3,4,5,6}) for(int ez:{4,5,6,7}) {
                head->volume->setVoxel(0,  ey, ez, earSkin);
                head->volume->setVoxel(13, ey, ez, earSkin);
            }
            break;
        }
        case 2: { // Elven: tall with upward taper to a point
            for(int ey:{3,4,5,6}) for(int ez:{4,5,6,7}) {
                head->volume->setVoxel(0,  ey, ez, earSkin);
                head->volume->setVoxel(13, ey, ez, earSkin);
            }
            // Narrowing upper portion
            for(int ez:{4,5,6}) {
                head->volume->setVoxel(0,  7, ez, earSkin);
                head->volume->setVoxel(13, 7, ez, earSkin);
            }
            // Pointed tip
            head->volume->setVoxel(0,  8, 5, earSkin);
            head->volume->setVoxel(13, 8, 5, earSkin);
            break;
        }
        case 3: { // Rounded: larger, rounder shape
            for(int ey:{2,3,4,5,6,7}) for(int ez:{3,4,5,6,7,8}) {
                head->volume->setVoxel(0,  ey, ez, earSkin);
                head->volume->setVoxel(13, ey, ez, earSkin);
            }
            break;
        }
        case 4: { // Wide/Floppy: large, drooping downward
            for(int ey:{1,2,3,4,5,6}) for(int ez:{3,4,5,6,7,8,9}) {
                head->volume->setVoxel(0,  ey, ez, earSkin);
                head->volume->setVoxel(13, ey, ez, earSkin);
            }
            break;
        }
        default: break;
    }

    head->volume->updateMesh();

    // --- Armor Overlay ---
    Voxel armorCol = {0,0,0,0};
    if (armorType == 1) armorCol = {40, 120, 40, 255}; // Cloth (Green)
    else if (armorType == 2) armorCol = {100, 60, 30, 255}; // Leather (Brown)
    else if (armorType == 3) armorCol = {180, 180, 200, 255}; // Heavy (Silver)

    if (armorType > 0) {
        // Torso Armor (9×9×7)
        for(int x=0; x<9; x++) for(int y=0; y<9; y++) for(int z=0; z<7; z++) {
            if (x==0 || x==8 || z==0 || z==6 || y==0 || y==8) {
                if (armorType == 3 && (y==0 || y==8))
                    torso->volume->setVoxel(x, y, z, {220, 220, 100, 255});
                else
                    torso->volume->setVoxel(x, y, z, armorCol);
            }
        }
        // Shoulder Armor (4×8×4)
        for (auto arm : {lArm, rArm}) {
            for(int x=0; x<4; x++) for(int y=6; y<8; y++) for(int z=0; z<4; z++)
                arm->volume->setVoxel(x, y, z, armorCol);
        }
        // Knee Armor (4×6×4)
        for (auto leg : {lLeg, rLeg}) {
            for(int x=0; x<4; x++) for(int z=3; z<4; z++) for(int y=3; y<5; y++)
                leg->volume->setVoxel(x, y, z, armorCol);
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
