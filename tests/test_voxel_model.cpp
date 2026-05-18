#include "terrax_test.h"
#include "voxel_model.h"

// --- VoxelVolume ---

TEST_CASE(VoxelVolume_SetGet_Basic) {
    VoxelVolume vol(8, 8, 8);
    Voxel v = {255, 0, 128, 255};
    vol.setVoxel(2, 3, 4, v);
    Voxel got = vol.getVoxel(2, 3, 4);
    CHECK_EQ((int)got.r, 255);
    CHECK_EQ((int)got.g, 0);
    CHECK_EQ((int)got.b, 128);
    CHECK_EQ((int)got.a, 255);
}

TEST_CASE(VoxelVolume_DefaultsTransparent) {
    VoxelVolume vol(4, 4, 4);
    Voxel got = vol.getVoxel(1, 1, 1);
    CHECK_EQ((int)got.a, 0);
}

TEST_CASE(VoxelVolume_OutOfBounds_ReturnsTransparent) {
    VoxelVolume vol(4, 4, 4);
    CHECK_EQ((int)vol.getVoxel(-1, 0, 0).a, 0);
    CHECK_EQ((int)vol.getVoxel(4, 0, 0).a, 0);
    CHECK_EQ((int)vol.getVoxel(0, 4, 0).a, 0);
    CHECK_EQ((int)vol.getVoxel(0, 0, -1).a, 0);
}

TEST_CASE(VoxelVolume_Overwrite) {
    VoxelVolume vol(4, 4, 4);
    vol.setVoxel(1, 1, 1, {100, 100, 100, 255});
    vol.setVoxel(1, 1, 1, {200, 50,  50,  255});
    CHECK_EQ((int)vol.getVoxel(1,1,1).r, 200);
}

// --- BipedalRig properties ---

TEST_CASE(BipedalRig_HeightScaleDefault) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    CHECK_EQ(rig.heightScale, 1.0f);
}

TEST_CASE(BipedalRig_WeightScaleDefault) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    CHECK_EQ(rig.weightScale, 1.0f);
}

TEST_CASE(BipedalRig_HeadVolumeCreated) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    CHECK(rig.head != nullptr);
    CHECK(rig.head->volume != nullptr);
}

// --- Hair styles ---

// Returns true if any voxel in the region [x0,x1)x[y0,y1)x[z0,z1) has a != 0
static bool anyVoxelInRegion(VoxelVolume* vol, int x0, int x1, int y0, int y1, int z0, int z1) {
    for(int x=x0; x<x1; x++) for(int y=y0; y<y1; y++) for(int z=z0; z<z1; z++)
        if(vol->getVoxel(x,y,z).a != 0) return true;
    return false;
}

TEST_CASE(Hair_Bald_NoVoxelsAboveSphere) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.hairStyle = 0;
    rig.applyCustomization();
    // y=12+ should be empty for bald
    CHECK(!anyVoxelInRegion(rig.head->volume, 0,24, 12,24, 0,24));
}

TEST_CASE(Hair_CrewCut_HasTopCap) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.hairStyle = 1;
    rig.hairColor = {200, 100, 50, 255};
    rig.applyCustomization();
    // Crew cut puts hair at y=11 (top of sphere) and y=12
    CHECK(rig.head->volume->getVoxel(12, 11, 12).a != 0);
    CHECK(rig.head->volume->getVoxel(12, 12, 12).a != 0);
}

TEST_CASE(Hair_Mohawk_CentralStrip) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.hairStyle = 3;
    rig.hairColor = {255, 0, 0, 255};
    rig.applyCustomization();
    // Mohawk: x=11 and x=12, y>=12
    CHECK(rig.head->volume->getVoxel(11, 14, 12).a != 0);
    CHECK(rig.head->volume->getVoxel(12, 14, 12).a != 0);
    // Should NOT have hair at x=8 at high y
    CHECK_EQ((int)rig.head->volume->getVoxel(8, 15, 12).a, 0);
}

TEST_CASE(Hair_Bob_HasSideCurtains) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.hairStyle = 6;
    rig.hairColor = {80, 40, 10, 255};
    rig.applyCustomization();
    // Bob has hair curtains at x=5 (left side)
    CHECK(rig.head->volume->getVoxel(5, 7, 12).a != 0);
    CHECK(rig.head->volume->getVoxel(18, 7, 12).a != 0);
}

TEST_CASE(Hair_LongStraight_HasBackAndSides) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.hairStyle = 7;
    rig.hairColor = {60, 30, 10, 255};
    rig.applyCustomization();
    CHECK(rig.head->volume->getVoxel(5, 5, 12).a != 0);   // left side
    CHECK(rig.head->volume->getVoxel(18, 5, 12).a != 0);  // right side
    CHECK(rig.head->volume->getVoxel(12, 5, 5).a != 0);   // back
}

TEST_CASE(Hair_Bun_HasTopKnot) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.hairStyle = 9;
    rig.hairColor = {40, 20, 10, 255};
    rig.applyCustomization();
    // Bun top at y=12-15, centered
    CHECK(rig.head->volume->getVoxel(12, 13, 12).a != 0);
    CHECK(rig.head->volume->getVoxel(12, 15, 12).a != 0);
}

TEST_CASE(Hair_Pigtails_HasSideBunches) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.hairStyle = 10;
    rig.hairColor = {200, 150, 100, 255};
    rig.applyCustomization();
    // Left pigtail at x=4-5, y=1-8
    CHECK(rig.head->volume->getVoxel(4, 4, 11).a != 0);
    // Right pigtail at x=18-19
    CHECK(rig.head->volume->getVoxel(19, 4, 11).a != 0);
}

// Regression: all non-bald styles must produce at least one hair voxel
TEST_CASE(Hair_AllNonBaldStyles_HaveVoxels) {
    for(int style = 1; style <= 11; style++) {
        BipedalRig rig;
        rig.setupDefaultHuman(true);
        rig.hairStyle = style;
        rig.hairColor = {180, 100, 60, 255};
        rig.applyCustomization();
        bool found = anyVoxelInRegion(rig.head->volume, 0,24, 0,24, 0,24);
        // At minimum the sphere base is always there, but we specifically
        // want y>=11 (top cap or above) to have at least one hair voxel
        bool hairOnTop = anyVoxelInRegion(rig.head->volume, 0,24, 11,24, 0,24);
        if(!hairOnTop) {
            throw std::runtime_error(std::string("Hair style ") + std::to_string(style) +
                " produced no voxels at y>=11");
        }
        (void)found;
    }
}

// --- Nose styles ---

TEST_CASE(Nose_Button_TwoVoxelsOnFront) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.noseStyle = 0;
    rig.applyCustomization();
    CHECK(rig.head->volume->getVoxel(11, 5, 18).a != 0);
    CHECK(rig.head->volume->getVoxel(12, 5, 18).a != 0);
    // Narrow voxel should NOT be set for button style
    CHECK_EQ((int)rig.head->volume->getVoxel(10, 5, 18).a, 0);
}

TEST_CASE(Nose_Wide_FourVoxels) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.noseStyle = 1;
    rig.applyCustomization();
    CHECK(rig.head->volume->getVoxel(10, 5, 18).a != 0);
    CHECK(rig.head->volume->getVoxel(13, 5, 18).a != 0);
}

TEST_CASE(Nose_Narrow_TallSingle) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.noseStyle = 2;
    rig.applyCustomization();
    CHECK(rig.head->volume->getVoxel(12, 5, 18).a != 0);
    CHECK(rig.head->volume->getVoxel(12, 6, 18).a != 0);
    // Should be narrower than button — no x=11 voxel at z=18
    CHECK_EQ((int)rig.head->volume->getVoxel(11, 5, 18).a, 0);
}

TEST_CASE(Nose_Broad_TallAndWide) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.noseStyle = 4;
    rig.applyCustomization();
    CHECK(rig.head->volume->getVoxel(10, 4, 18).a != 0);
    CHECK(rig.head->volume->getVoxel(13, 5, 18).a != 0);
    CHECK(rig.head->volume->getVoxel(12, 6, 18).a != 0);
}

// --- Ear types ---

TEST_CASE(Ear_None_NoSideVoxels) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.earType = 0;
    rig.applyCustomization();
    CHECK_EQ((int)rig.head->volume->getVoxel(5, 4, 11).a, 0);
    CHECK_EQ((int)rig.head->volume->getVoxel(18, 4, 11).a, 0);
}

TEST_CASE(Ear_Human_HasSmallStubs) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.earType = 1;
    rig.applyCustomization();
    CHECK(rig.head->volume->getVoxel(5,  4, 11).a != 0);
    CHECK(rig.head->volume->getVoxel(18, 4, 11).a != 0);
    // Human ears don't extend to y=7 (that's elven)
    CHECK_EQ((int)rig.head->volume->getVoxel(4, 7, 11).a, 0);
}

TEST_CASE(Ear_Elven_HasPointedTip) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.earType = 2;
    rig.applyCustomization();
    // Elven point extends to y=7 at x=4/x=19
    CHECK(rig.head->volume->getVoxel(4,  7, 11).a != 0);
    CHECK(rig.head->volume->getVoxel(19, 7, 11).a != 0);
}

TEST_CASE(Ear_Rounded_WiderThanHuman) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.earType = 3;
    rig.applyCustomization();
    // Rounded ears go to x=3 and x=20
    CHECK(rig.head->volume->getVoxel(3,  4, 11).a != 0);
    CHECK(rig.head->volume->getVoxel(20, 4, 11).a != 0);
}

TEST_CASE(Ear_Wide_ExtendsToX3AndX20) {
    BipedalRig rig;
    rig.setupDefaultHuman(true);
    rig.earType = 4;
    rig.applyCustomization();
    CHECK(rig.head->volume->getVoxel(3,  4, 12).a != 0);
    CHECK(rig.head->volume->getVoxel(20, 4, 12).a != 0);
}
