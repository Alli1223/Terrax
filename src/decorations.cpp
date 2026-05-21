#include "prop_builders.h"

// --- Decorations: small-voxel models placed around the towns ---------------

namespace {
const Voxel WOOD  {120,  80,  46, 255};
const Voxel WOODD { 86,  56,  32, 255};
const Voxel IRON  { 56,  58,  64, 255};
const Voxel IRONL { 70,  72,  80, 255};
const Voxel LAMP  {255, 220, 130, 255};
const Voxel LEAF  { 58, 128,  52, 255};
const Voxel LEAFD { 44, 102,  42, 255};
}

VoxelVolume* buildStreetLamp() {
    const int W = 8, H = 58, D = 8;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 1, 0, 1, 6, 2, 6, IRON);          // base
    voxFill(v, 3, 0, 3, 4, 48, 4, IRONL);        // post
    voxFill(v, 1, 48, 1, 6, 49, 6, IRON);        // lamp bracket
    voxFill(v, 2, 50, 2, 5, 55, 5, LAMP);        // glowing lamp head
    voxFill(v, 1, 56, 1, 6, 57, 6, IRON);        // cap
    return v;
}

VoxelVolume* buildPottedPlant() {
    const int W = 12, H = 26, D = 12;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 1, 0, 1, W - 2, 7, D - 2, {172, 92, 56, 255});  // terracotta pot
    voxFill(v, 2, 8, 2, W - 3, 8, D - 3, {74, 52, 34, 255});   // soil
    voxFill(v, 4, 9, 4, 7, 16, 7, {90, 70, 45, 255});          // stem
    voxFill(v, 1, 15, 1, W - 2, 24, D - 2, LEAF);              // foliage
    voxFill(v, 3, 24, 3, W - 4, 25, D - 4, LEAFD);             // crown
    return v;
}

VoxelVolume* buildBush() {
    const int W = 18, H = 16, D = 18;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 7, 0, 7, 10, 4, 10, {86, 64, 42, 255});         // stem
    voxFill(v, 2, 3, 2, W - 3, 12, D - 3, LEAF);               // main mass
    voxFill(v, 4, 12, 4, W - 5, 14, D - 5, LEAFD);             // top
    voxFill(v, 1, 5, 5, 1, 9, D - 6, LEAFD);                   // side tufts
    voxFill(v, W - 2, 5, 5, W - 2, 9, D - 6, LEAFD);
    return v;
}

VoxelVolume* buildBench() {
    const int W = 28, H = 16, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int ex = 0; ex <= W - 3; ex += W - 3)
        voxFill(v, ex, 0, 1, ex + 2, 8, D - 2, WOODD);         // end supports
    voxFill(v, 0, 9, 0, W - 1, 10, D - 1, WOOD);               // seat
    voxFill(v, 0, 11, 0, W - 1, H - 1, 1, WOOD);               // backrest
    return v;
}

VoxelVolume* buildFenceSection() {
    const int W = 24, H = 18, D = 4;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int px = 0; px <= W - 3; px += (W - 3) / 2)
        voxFill(v, px, 0, 1, px + 2, H - 1, 2, WOOD);          // posts
    voxFill(v, 0, 4, 1, W - 1, 6, 2, WOOD);                    // lower rail
    voxFill(v, 0, 11, 1, W - 1, 13, 2, WOOD);                  // upper rail
    return v;
}
