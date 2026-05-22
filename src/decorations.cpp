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
    // A tall thin post (the "stick") with a small lantern hanging from a hook.
    const int W = 10, H = 72, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 3, 0, 3, 6, 2, 6, IRON);             // base foot
    voxFill(v, 4, 0, 4, 5, 64, 5, IRONL);           // post — the long stick
    voxFill(v, 5, 63, 4, 8, 64, 5, IRON);           // hook arm
    voxFill(v, 8, 57, 4, 8, 64, 5, IRON);           // hook drop
    voxFill(v, 6, 56, 3, 9, 56, 6, IRON);           // lantern cap
    voxFill(v, 6, 50, 3, 6, 55, 3, IRON);           // cage corner posts
    voxFill(v, 9, 50, 3, 9, 55, 3, IRON);
    voxFill(v, 6, 50, 6, 6, 55, 6, IRON);
    voxFill(v, 9, 50, 6, 9, 55, 6, IRON);
    voxFill(v, 7, 50, 4, 8, 55, 5, LAMP);           // glowing lantern core
    voxFill(v, 6, 49, 3, 9, 49, 6, IRON);           // lantern base
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
    // Long axis runs along +Z so a placement yaw aligns it with a path.
    const int W = 4, H = 18, D = 36;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int pz = 0; pz <= D - 3; pz += (D - 3) / 3)
        voxFill(v, 1, 0, pz, 2, H - 1, pz + 2, WOODD);         // posts along the run
    voxFill(v, 1, 4, 0, 2, 6, D - 1, WOOD);                    // lower rail
    voxFill(v, 1, 11, 0, 2, 13, D - 1, WOOD);                  // upper rail
    return v;
}

VoxelVolume* buildDoor(int variant) {
    // A door panel — the hinge is the x = 0 edge. Colour and style vary.
    const int W = 15, H = 20, D = 2;
    struct DoorStyle { Voxel body, trim; bool glazed; };
    static const DoorStyle STYLES[] = {
        { {150, 105,  60, 255}, { 95,  62,  34, 255}, false },  // oak
        { { 84,  56,  38, 255}, { 48,  32,  22, 255}, false },  // dark walnut
        { {158,  58,  50, 255}, { 78,  34,  30, 255}, false },  // painted red
        { { 58,  88, 132, 255}, { 34,  50,  74, 255}, true  },  // painted blue, glazed
        { { 72, 116,  74, 255}, { 42,  66,  44, 255}, false },  // painted green
        { {128, 126, 120, 255}, { 80,  78,  74, 255}, true  },  // weathered grey, glazed
    };
    const int N = (int)(sizeof(STYLES) / sizeof(STYLES[0]));
    const DoorStyle& s = STYLES[((variant % N) + N) % N];
    const Voxel pane { 175, 212, 232, 255 };

    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0,     0,     0, W - 1, H - 1, D - 1, s.body);   // panel body
    voxFill(v, 0,     0,     0, 0,     H - 1, D - 1, s.trim);   // hinge stile
    voxFill(v, W - 1, 0,     0, W - 1, H - 1, D - 1, s.trim);   // latch stile
    voxFill(v, 0,     0,     0, W - 1, 0,     D - 1, s.trim);   // bottom rail
    voxFill(v, 0,     H - 1, 0, W - 1, H - 1, D - 1, s.trim);   // top rail
    voxFill(v, 0,     9,     0, W - 1, 10,    D - 1, s.trim);   // middle rail
    voxFill(v, 5,     1,     0, 5,     8,     D - 1, s.trim);   // lower plank seams
    voxFill(v, 10,    1,     0, 10,    8,     D - 1, s.trim);
    if (s.glazed) {
        voxFill(v, 3, 12, 0, W - 4, 17, D - 1, pane);           // glazed upper panel
        voxFill(v, 7, 12, 0, 7,     17, D - 1, s.trim);         // muntins
        voxFill(v, 3, 14, 0, W - 4, 14, D - 1, s.trim);
    } else {
        voxFill(v, 5,  11, 0, 5,  H - 2, D - 1, s.trim);        // upper plank seams
        voxFill(v, 10, 11, 0, 10, H - 2, D - 1, s.trim);
    }
    voxFill(v, 12, 6, 0, 12, 8, D - 1, IRON);                   // handle
    return v;
}
