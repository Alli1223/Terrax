#include "prop_builders.h"

// --- Furniture: small-voxel models baked into the player houses ------------

namespace {
const Voxel WOOD  {125,  82,  45, 255};
const Voxel WOODD { 88,  56,  30, 255};
const Voxel CLOTH {232, 226, 210, 255};
const Voxel WHITE {240, 240, 236, 255};
const Voxel METAL { 74,  76,  82, 255};
const Voxel GLOW  {255, 216, 120, 255};
const Voxel EMBER {255, 138,  44, 255};
}

VoxelVolume* buildBookshelf() {
    const int W = 16, H = 30, D = 8;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0, 0, 0,     W - 1, 1,     D - 1, WOODD);   // bottom
    voxFill(v, 0, H - 2, 0, W - 1, H - 1, D - 1, WOODD);   // top
    voxFill(v, 0, 0, 0,     1,     H - 1, D - 1, WOODD);   // left side
    voxFill(v, W - 2, 0, 0, W - 1, H - 1, D - 1, WOODD);   // right side
    voxFill(v, 0, 0, 0,     W - 1, H - 1, 1,     WOODD);   // back panel

    const Voxel books[4] = { {165, 52, 46, 255}, {52, 82, 150, 255},
                             {56, 128, 66, 255}, {182, 150, 92, 255} };
    for (int s = 0; s < 4; s++) {
        int sy = 3 + s * 7;
        voxFill(v, 2, sy, 2, W - 3, sy, D - 1, WOOD);      // shelf board
        for (int x = 2; x < W - 2; x++)
            voxFill(v, x, sy + 1, 3, x, sy + 5, D - 2, books[(x + s) % 4]);
    }
    return v;
}

VoxelVolume* buildBed() {
    const int W = 18, H = 11, D = 32;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int lx = 0; lx <= W - 3; lx += W - 3)
        for (int lz = 0; lz <= D - 3; lz += D - 3)
            voxFill(v, lx, 0, lz, lx + 2, 4, lz + 2, WOODD);  // legs
    voxFill(v, 0, 4, 0, W - 1, 5, D - 1, WOOD);               // frame
    voxFill(v, 0, 6, 0, W - 1, H - 1, 2, WOOD);               // headboard
    voxFill(v, 1, 6, 3, W - 2, 8, D - 2, CLOTH);              // mattress
    voxFill(v, 1, 8, D / 2, W - 2, 9, D - 2, {90, 120, 180, 255});  // blanket
    voxFill(v, 2, 8, 4, W - 3, 9, 9, WHITE);                  // pillow
    return v;
}

VoxelVolume* buildLanternProp() {
    const int W = 7, H = 13, D = 7;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 1, 0, 1, 5, 1, 5, METAL);                      // base
    for (int cx = 1; cx <= 5; cx += 4)
        for (int cz = 1; cz <= 5; cz += 4)
            voxFill(v, cx, 2, cz, cx, 9, cz, METAL);          // corner posts
    voxFill(v, 2, 3, 2, 4, 8, 4, GLOW);                       // glowing core
    voxFill(v, 1, 9, 1, 5, 10, 5, METAL);                     // cap
    voxFill(v, 3, 11, 3, 3, 12, 3, METAL);                    // top knob
    return v;
}

VoxelVolume* buildCooker() {
    const int W = 16, H = 17, D = 15;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0, 0, 0, W - 1, 12, D - 1, METAL);             // body
    voxFill(v, 2, 3, D - 1, W - 3, 10, D - 1, {40, 42, 46, 255});  // oven door
    voxFill(v, 4, 5, D - 1, W - 5, 8, D - 1, EMBER);          // glowing window
    voxFill(v, 0, 13, 0, W - 1, 13, D - 1, {54, 56, 60, 255});     // cooktop
    for (int bx = 3; bx <= W - 6; bx += 7)
        for (int bz = 3; bz <= D - 6; bz += 7)
            voxFill(v, bx, 14, bz, bx + 2, 14, bz + 2, {30, 30, 32, 255});  // burners
    return v;
}

VoxelVolume* buildTable() {
    const int W = 22, H = 13, D = 22;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int lx = 1; lx <= W - 4; lx += W - 5)
        for (int lz = 1; lz <= D - 4; lz += D - 5)
            voxFill(v, lx, 0, lz, lx + 2, 10, lz + 2, WOODD);  // legs
    voxFill(v, 0, 11, 0, W - 1, 12, D - 1, WOOD);              // table top
    return v;
}

VoxelVolume* buildChair() {
    const int W = 10, H = 20, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int lx = 0; lx <= W - 2; lx += W - 2)
        for (int lz = 0; lz <= D - 2; lz += D - 2)
            voxFill(v, lx, 0, lz, lx + 1, 8, lz + 1, WOODD);   // legs
    voxFill(v, 0, 9, 0, W - 1, 10, D - 1, WOOD);               // seat
    voxFill(v, 0, 11, 0, W - 1, H - 1, 1, WOOD);               // backrest
    return v;
}

VoxelVolume* buildCrockery() {
    const int W = 10, H = 7, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int p = 0; p < 3; p++)
        voxFill(v, 1, p, 1, 6, p, 6, WHITE);                   // stacked plates
    voxFill(v, 2, 3, 2, 5, 3, 5, WHITE);                       // top plate
    voxFill(v, 7, 0, 6, 9, 3, 8, {200, 196, 188, 255});        // a mug
    voxFill(v, 8, 1, 5, 8, 2, 5, {200, 196, 188, 255});        // mug handle
    return v;
}
