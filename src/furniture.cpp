#include "prop_builders.h"
#include <algorithm>

// Shared voxel box-fill used by every prop builder. Lives here (rather than in
// prop.cpp) so the headless asset-catalog tool can link the builders without
// the audio / object-manager closure.
void voxFill(VoxelVolume* v, int x0, int y0, int z0,
             int x1, int y1, int z1, Voxel c) {
    if (!v) return;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (z0 > z1) std::swap(z0, z1);
    x0 = std::max(0, x0); y0 = std::max(0, y0); z0 = std::max(0, z0);
    x1 = std::min(v->sizeX - 1, x1);
    y1 = std::min(v->sizeY - 1, y1);
    z1 = std::min(v->sizeZ - 1, z1);
    for (int z = z0; z <= z1; z++)
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                v->setVoxel(x, y, z, c);
}

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
    const int W = 28, H = 16, D = 28;                          // ~1.25x — roomier
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int lx = 1; lx <= W - 4; lx += W - 5)
        for (int lz = 1; lz <= D - 4; lz += D - 5)
            voxFill(v, lx, 0, lz, lx + 2, 13, lz + 2, WOODD);  // legs
    voxFill(v, 0, 14, 0, W - 1, 15, D - 1, WOOD);              // table top
    return v;
}

VoxelVolume* buildChair() {
    const int W = 12, H = 24, D = 12;                          // ~1.2x — roomier
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int lx = 0; lx <= W - 2; lx += W - 2)
        for (int lz = 0; lz <= D - 2; lz += D - 2)
            voxFill(v, lx, 0, lz, lx + 1, 10, lz + 1, WOODD);  // legs
    voxFill(v, 0, 11, 0, W - 1, 12, D - 1, WOOD);              // seat (~y=12 ≈ 0.72)
    voxFill(v, 0, 13, 0, W - 1, H - 1, 1, WOOD);               // backrest
    return v;
}

VoxelVolume* buildChest() {
    const int W = 16, H = 11, D = 11;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0, 0, 0, W - 1, 6, D - 1, WOOD);                // body
    voxFill(v, 0, 7, 0, W - 1, H - 1, D - 1, WOODD);           // lid
    for (int x : { 2, W - 3 }) voxFill(v, x, 0, 0, x, H - 1, D - 1, METAL);  // iron bands
    voxFill(v, W / 2 - 1, 5, 0, W / 2, 8, 0, METAL);           // front latch
    return v;
}

VoxelVolume* buildStool() {
    const int W = 10, H = 12, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int lx = 0; lx <= W - 2; lx += W - 2)
        for (int lz = 0; lz <= D - 2; lz += D - 2)
            voxFill(v, lx, 0, lz, lx + 1, 8, lz + 1, WOODD);   // four short legs
    voxFill(v, 0, 9, 0, W - 1, 10, D - 1, WOOD);               // seat (~y=10 ≈ 0.5)
    return v;
}

VoxelVolume* buildCandelabra() {
    const int W = 7, H = 20, D = 7;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 2, 0, 2, 4, 1, 4, METAL);                       // weighted base
    voxFill(v, 3, 1, 3, 3, 14, 3, METAL);                      // central stem
    voxFill(v, 1, 14, 3, 5, 14, 3, METAL);                     // candle crossbar
    for (int cx : { 1, 3, 5 }) {
        voxFill(v, cx, 15, 3, cx, 17, 3, WHITE);               // wax candle
        v->setVoxel(cx, 18, 3, GLOW);                          // flame
    }
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

// --- Phase 2 furniture -----------------------------------------------------
//
// All these models are aligned the same way as the existing pieces: width on
// X, depth on Z, with the back of the piece against +Z=0 so that a yaw of 0
// rests the back flush against a wall on the -Z side of the placement cell.

namespace {
const Voxel CERAMIC {236, 234, 226, 255};   // sink basin / counter top
const Voxel COUNTER {186, 168, 140, 255};   // light worktop wood
const Voxel WATER   { 80, 140, 200, 200};   // sink water
const Voxel PAPER   {238, 230, 200, 255};   // desk paper
const Voxel CUSHION {110,  76, 130, 255};   // couch cushion
const Voxel CUSHION2{ 60,  90, 150, 255};   // side table accent (closed lantern)
}

VoxelVolume* buildSink() {
    const int W = 14, H = 14, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0, 0, 0, W - 1, 8, D - 1, COUNTER);                     // cabinet
    voxFill(v, 0, 9, 0, W - 1, 10, D - 1, CERAMIC);                    // counter top
    voxFill(v, 2, 10, 2, W - 3, 11, D - 3, COUNTER);                   // basin rim
    voxFill(v, 3, 10, 3, W - 4, 11, D - 4, WATER);                     // water
    voxFill(v, W / 2 - 1, 11, D - 3, W / 2, 13, D - 2, METAL);         // tap arch
    voxFill(v, W / 2 - 1, 11, D - 2, W / 2, 11, D - 2, METAL);
    return v;
}

VoxelVolume* buildKitchenCounter() {
    const int W = 18, H = 12, D = 9;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0, 0, 0, W - 1, 9, D - 1, COUNTER);                     // base
    voxFill(v, 0, 10, 0, W - 1, 11, D - 1, CERAMIC);                   // worktop
    // Two cabinet doors with small handles.
    voxFill(v, 1, 1, D - 1, W / 2 - 1, 8, D - 1, WOOD);
    voxFill(v, W / 2 + 1, 1, D - 1, W - 2, 8, D - 1, WOOD);
    voxFill(v, W / 4, 4, D - 1, W / 4, 5, D - 1, METAL);
    voxFill(v, 3 * W / 4, 4, D - 1, 3 * W / 4, 5, D - 1, METAL);
    // A loaf of bread and a small bowl on the worktop.
    voxFill(v, 3, 12, 3, 5, 13, 5, {196, 154,  88, 255});
    voxFill(v, W - 6, 12, 3, W - 4, 12, 5, {196, 196, 196, 255});
    return v;
}

VoxelVolume* buildWardrobe() {
    const int W = 14, H = 30, D = 8;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0, 0, 0, W - 1, H - 1, D - 1, WOODD);                   // outer shell
    voxFill(v, 1, 1, 1, W - 2, H - 3, D - 2, WOOD);                    // wood interior
    // Two doors with handles.
    voxFill(v, 1, 1, D - 1, W / 2 - 1, H - 3, D - 1, WOODD);
    voxFill(v, W / 2 + 1, 1, D - 1, W - 2, H - 3, D - 1, WOODD);
    voxFill(v, W / 2 - 2, H / 2 - 1, D - 1, W / 2 - 2, H / 2 + 1, D - 1, METAL);
    voxFill(v, W / 2 + 2, H / 2 - 1, D - 1, W / 2 + 2, H / 2 + 1, D - 1, METAL);
    // Decorative cornice.
    voxFill(v, 0, H - 2, 0, W - 1, H - 1, D - 1, WOODD);
    return v;
}

VoxelVolume* buildDesk() {
    const int W = 20, H = 14, D = 12;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    // Two leg pedestals with a kneehole between.
    voxFill(v, 0, 0, 0, 4, 10, D - 1, WOODD);                          // left pedestal
    voxFill(v, W - 5, 0, 0, W - 1, 10, D - 1, WOODD);                  // right pedestal
    voxFill(v, 0, 11, 0, W - 1, 12, D - 1, WOOD);                      // desk top
    // A small drawer hint on each pedestal.
    voxFill(v, 1, 6, D - 1, 3, 8, D - 1, WOOD);
    voxFill(v, W - 4, 6, D - 1, W - 2, 8, D - 1, WOOD);
    voxFill(v, 2, 7, D - 1, 2, 7, D - 1, METAL);
    voxFill(v, W - 3, 7, D - 1, W - 3, 7, D - 1, METAL);
    // Paper + an ink pot on top.
    voxFill(v, W / 2 - 3, 13, D / 2 - 2, W / 2 + 2, 13, D / 2 + 2, PAPER);
    voxFill(v, W / 2 + 5, 13, D / 2 - 1, W / 2 + 6, 13, D / 2, METAL);
    return v;
}

VoxelVolume* buildCouch() {
    const int W = 26, H = 14, D = 12;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    // Base, with little feet.
    for (int lx = 0; lx <= W - 3; lx += W - 3)
        voxFill(v, lx, 0, 0, lx + 2, 2, D - 1, WOODD);
    voxFill(v, 0, 3, 0, W - 1, 5, D - 1, CUSHION);                     // seat block
    voxFill(v, 0, 6, 0, W - 1, 6, D - 1, CLOTH);                       // seat cushion top
    voxFill(v, 0, 6, 0, W - 1, H - 1, 2, CUSHION);                     // back
    voxFill(v, 0, 6, 0, 1, H - 3, D - 1, CUSHION);                     // left arm
    voxFill(v, W - 2, 6, 0, W - 1, H - 3, D - 1, CUSHION);             // right arm
    // Two scatter cushions on the seat.
    voxFill(v, 4, 7, 4, 8, 9, D - 3, CLOTH);
    voxFill(v, W - 9, 7, 4, W - 5, 9, D - 3, CLOTH);
    return v;
}

VoxelVolume* buildSideTable() {
    const int W = 9, H = 11, D = 9;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    for (int lx = 0; lx <= W - 2; lx += W - 2)
        for (int lz = 0; lz <= D - 2; lz += D - 2)
            voxFill(v, lx, 0, lz, lx + 1, 8, lz + 1, WOODD);            // legs
    voxFill(v, 0, 9, 0, W - 1, 10, D - 1, WOOD);                       // top
    voxFill(v, 1, 7, 1, W - 2, 7, D - 2, WOODD);                       // single drawer hint
    voxFill(v, W / 2, 7, D - 1, W / 2, 7, D - 1, METAL);               // drawer pull
    return v;
}

// --- Phase 3 furniture (stubs filled in alongside the building generators) --

VoxelVolume* buildAnvil() {
    const int W = 16, H = 12, D = 8;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 1, 0, 1, W - 2, 1, D - 2, WOODD);                       // log block base
    voxFill(v, 3, 2, 2, W - 4, 4, D - 3, {52, 54, 60, 255});           // stem
    voxFill(v, 0, 5, 1, W - 1, 7, D - 2, {52, 54, 60, 255});           // body
    voxFill(v, 0, 8, 2, 2, 9, D - 3, {52, 54, 60, 255});               // horn
    voxFill(v, W - 3, 8, 2, W - 1, 9, D - 3, {52, 54, 60, 255});       // heel
    return v;
}

VoxelVolume* buildForge() {
    const int W = 18, H = 22, D = 16;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0, 0, 0, W - 1, 7, D - 1, {78, 70, 60, 255});           // brick base
    voxFill(v, 2, 7, 2, W - 3, 8, D - 3, {30, 30, 30, 255});           // hearth top
    voxFill(v, 4, 8, 4, W - 5, 10, D - 5, EMBER);                      // glowing coals
    voxFill(v, 0, 11, 0, W - 1, 12, D - 1, {78, 70, 60, 255});         // chimney shoulder
    voxFill(v, W / 2 - 2, 13, D / 2 - 2,
            W / 2 + 1, H - 1, D / 2 + 1, {78, 70, 60, 255});           // chimney stack
    return v;
}

VoxelVolume* buildBarCounter() {
    const int W = 24, H = 12, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 0, 0, 0, W - 1, 9, D - 1, WOODD);                       // counter body
    voxFill(v, 0, 10, 0, W - 1, 11, D - 1, WOOD);                      // counter top
    voxFill(v, 0, 1, D - 1, W - 1, 8, D - 1, WOOD);                    // front panelling
    // A row of small tankards on the bar top.
    for (int x = 3; x < W - 3; x += 4)
        voxFill(v, x, 12, 3, x + 1, 14, 4, {196, 196, 196, 255});
    return v;
}

VoxelVolume* buildBarStool() {
    const int W = 7, H = 13, D = 7;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 1, 0, 1, 1, 10, 1, WOODD);
    voxFill(v, W - 2, 0, 1, W - 2, 10, 1, WOODD);
    voxFill(v, 1, 0, D - 2, 1, 10, D - 2, WOODD);
    voxFill(v, W - 2, 0, D - 2, W - 2, 10, D - 2, WOODD);
    voxFill(v, 0, 11, 0, W - 1, 12, D - 1, WOOD);                      // round-ish seat
    return v;
}

VoxelVolume* buildCauldron() {
    const int W = 12, H = 12, D = 12;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 4, 0, 4, 7, 1, 7, {40, 40, 40, 255});                   // hearth stones
    voxFill(v, 2, 1, 2, W - 3, 8, D - 3, {28, 28, 32, 255});           // cauldron body
    voxFill(v, 3, 8, 3, W - 4, 8, D - 4, {130, 220, 100, 255});        // bubbling brew
    voxFill(v, 1, 5, 1, 2, 6, 1, METAL);                               // handles
    voxFill(v, W - 3, 5, 1, W - 2, 6, 1, METAL);
    return v;
}

// --- Trade signs -----------------------------------------------------------
//
// Wall-mounted plaques: a wooden board with an iron-rivet frame and a coloured
// icon painted on the front face. The board sits flush against the building's
// front wall (the placer sets the Z position so model Z=0 hugs the wall) and
// the icon faces outward. New roles only need a new icon builder + a switch
// case in placeTradeSign.

namespace {
const Voxel SIGN_PLANK    {178, 140,  92, 255};   // light board wood
const Voxel SIGN_FRAME    {110,  78,  46, 255};   // darker frame wood
const Voxel SIGN_RIVET    { 60,  62,  68, 255};   // corner rivets / icon iron
const Voxel SIGN_GOLD     {220, 178,  90, 255};   // mug rim / star body
const Voxel SIGN_DEEP     {110,  68,  30, 255};   // mug body
const Voxel SIGN_WHEAT    {214, 178,  74, 255};   // wheat heads

// All trade-sign plaques share the same wood backing + frame + iron rivets.
// Coordinates: X runs along the wall, Y is up, Z is the depth out from the
// wall (Z=0 is the side that hugs the wall, Z=1 is the visible face).
// The icon builder then paints onto the Z=1 layer in the rectangle the frame
// leaves bare.
constexpr int SIGN_W = 18;
constexpr int SIGN_H = 12;
constexpr int SIGN_D = 2;

void buildSignPlaque(VoxelVolume* v) {
    // Solid wooden backing across the entire plaque (the side that touches
    // the wall) so the sign reads as a real plaque from any angle.
    voxFill(v, 0, 0, 0, SIGN_W - 1, SIGN_H - 1, 0, SIGN_PLANK);
    // Light planks on the visible (Z=1) face inside the frame.
    voxFill(v, 1, 1, 1, SIGN_W - 2, SIGN_H - 2, 1, SIGN_PLANK);
    // Darker wooden frame around the border of the visible face.
    voxFill(v, 0, 0,         1, SIGN_W - 1, 0,         1, SIGN_FRAME); // top
    voxFill(v, 0, SIGN_H - 1, 1, SIGN_W - 1, SIGN_H - 1, 1, SIGN_FRAME); // bot
    voxFill(v, 0, 0,         1, 0,         SIGN_H - 1, 1, SIGN_FRAME); // L
    voxFill(v, SIGN_W - 1, 0, 1, SIGN_W - 1, SIGN_H - 1, 1, SIGN_FRAME); // R
    // Four corner rivets so it reads as nailed-on metalwork.
    v->setVoxel(1,         1,         1, SIGN_RIVET);
    v->setVoxel(SIGN_W - 2, 1,         1, SIGN_RIVET);
    v->setVoxel(1,         SIGN_H - 2, 1, SIGN_RIVET);
    v->setVoxel(SIGN_W - 2, SIGN_H - 2, 1, SIGN_RIVET);
}
}   // namespace

VoxelVolume* buildTradeSignAnvil() {
    VoxelVolume* v = new VoxelVolume(SIGN_W, SIGN_H, SIGN_D);
    buildSignPlaque(v);
    const int cx = SIGN_W / 2;
    const int cy = SIGN_H / 2;
    // Wooden stump base under the anvil.
    voxFill(v, cx - 2, cy - 4, 1, cx + 1, cy - 3, 1, SIGN_FRAME);
    // Iron anvil: stem + body + horn + heel.
    voxFill(v, cx - 1, cy - 2, 1, cx,     cy - 1, 1, SIGN_RIVET); // stem
    voxFill(v, cx - 4, cy,     1, cx + 3, cy + 1, 1, SIGN_RIVET); // body
    voxFill(v, cx - 5, cy + 1, 1, cx - 4, cy + 1, 1, SIGN_RIVET); // horn
    voxFill(v, cx + 3, cy + 1, 1, cx + 4, cy + 1, 1, SIGN_RIVET); // heel
    return v;
}

VoxelVolume* buildTradeSignMug() {
    VoxelVolume* v = new VoxelVolume(SIGN_W, SIGN_H, SIGN_D);
    buildSignPlaque(v);
    const int cx = SIGN_W / 2;
    const int cy = SIGN_H / 2;
    // Mug body.
    voxFill(v, cx - 2, cy - 4, 1, cx + 1, cy + 1, 1, SIGN_DEEP);
    // Frothy head.
    voxFill(v, cx - 2, cy + 2, 1, cx + 1, cy + 3, 1, {230, 230, 220, 255});
    // Gold rim band + curving handle.
    voxFill(v, cx - 2, cy + 1, 1, cx + 1, cy + 1, 1, SIGN_GOLD);
    voxFill(v, cx + 2, cy - 2, 1, cx + 2, cy,     1, SIGN_GOLD);
    voxFill(v, cx + 3, cy - 1, 1, cx + 3, cy - 1, 1, SIGN_GOLD);
    return v;
}

VoxelVolume* buildTradeSignStar() {
    VoxelVolume* v = new VoxelVolume(SIGN_W, SIGN_H, SIGN_D);
    buildSignPlaque(v);
    const int cx = SIGN_W / 2;
    const int cy = SIGN_H / 2;
    // Five-point gold star.
    voxFill(v, cx - 1, cy - 1, 1, cx,     cy + 1, 1, SIGN_GOLD); // body
    v->setVoxel(cx,     cy + 2, 1, SIGN_GOLD);                   // top point
    v->setVoxel(cx - 1, cy + 2, 1, SIGN_GOLD);
    v->setVoxel(cx - 1, cy - 3, 1, SIGN_GOLD);                   // bottom point
    v->setVoxel(cx,     cy - 3, 1, SIGN_GOLD);
    voxFill(v, cx - 3, cy - 1, 1, cx - 2, cy,     1, SIGN_GOLD); // left arm
    voxFill(v, cx + 1, cy - 1, 1, cx + 2, cy,     1, SIGN_GOLD); // right arm
    return v;
}

VoxelVolume* buildTradeSignWheat() {
    VoxelVolume* v = new VoxelVolume(SIGN_W, SIGN_H, SIGN_D);
    buildSignPlaque(v);
    const int cy = SIGN_H / 2;
    // Three wheat shafts side-by-side.
    for (int sx : { SIGN_W / 2 - 4, SIGN_W / 2, SIGN_W / 2 + 4 }) {
        voxFill(v, sx, cy - 4, 1, sx, cy + 1, 1, SIGN_FRAME);    // stalk
        voxFill(v, sx - 1, cy + 1, 1, sx + 1, cy + 3, 1, SIGN_WHEAT); // head
    }
    return v;
}

VoxelVolume* buildAlchemyTable() {
    const int W = 18, H = 18, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    // Table base.
    voxFill(v, 0, 0, 0, 2, 10, D - 1, WOODD);
    voxFill(v, W - 3, 0, 0, W - 1, 10, D - 1, WOODD);
    voxFill(v, 0, 11, 0, W - 1, 12, D - 1, WOOD);
    // A row of bottles with coloured contents and a hanging shelf above.
    const Voxel colours[5] = { {180,  60,  60, 255}, { 60, 180, 100, 255},
                               { 80,  80, 200, 255}, {220, 200,  80, 255},
                               {180,  60, 180, 255} };
    for (int i = 0; i < 5; i++) {
        int bx = 2 + i * 3;
        voxFill(v, bx, 13, 3, bx + 1, 15, 4, CERAMIC);                 // bottle glass
        voxFill(v, bx, 13, 3, bx + 1, 14, 4, colours[i]);              // contents
    }
    voxFill(v, 1, 16, 1, W - 2, 17, 2, WOODD);                         // hanging shelf
    return v;
}

// --- Cosy home furnishings -------------------------------------------------
// Aligned like the other wall pieces: back at z=0, so a yaw of 0 sits the back
// flush against a wall on the -Z side and the front faces into the room.

VoxelVolume* buildFireplace() {
    // A stone hearth: a sooty fire box between two pillars under a wooden mantel,
    // a chimney breast rising above, and a lit bed of logs. The ember/flame
    // voxels read as warm; renderer.cpp anchors a point light at the fire box.
    const int W = 22, H = 38, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel STONE   {122, 120, 114, 255};
    const Voxel STONE_D { 88,  86,  80, 255};
    const Voxel SOOT    { 34,  30,  28, 255};
    const Voxel FLAME   {236,  78,  34, 255};
    voxFill(v, 0, 0, 0,     W - 1, 2,     D - 1, STONE_D);   // hearth slab
    voxFill(v, 0, 0, 0,     4,     27,    D - 1, STONE);     // left pillar
    voxFill(v, W - 5, 0, 0, W - 1, 27,    D - 1, STONE);     // right pillar
    voxFill(v, 0, 24, 0,    W - 1, 27,    D - 1, STONE);     // lintel
    voxFill(v, 5, 3, 0,     W - 6, 23,    2,     SOOT);      // sooty back of box
    voxFill(v, 5, 3, 0,     5,     23,    D - 1, SOOT);      // box side walls
    voxFill(v, W - 6, 3, 0, W - 6, 23,    D - 1, SOOT);
    voxFill(v, 6, 3, 3,     W - 7, 5,     D - 2, WOODD);     // log bed
    voxFill(v, 6, 3, 3,     W - 7, 4,     D - 2, EMBER);     // glowing embers
    for (int x = 7; x <= W - 8; x += 2) {                    // flames
        int fh = 7 + ((x * 5) % 8);
        voxFill(v, x, 5, 4,      x, 5 + fh, D - 3, FLAME);
        voxFill(v, x, 5 + fh, 4, x, 7 + fh, D - 3, GLOW);
    }
    voxFill(v, 0, 28, 0,    W - 1, 30,    D - 1, WOOD);      // mantel shelf
    voxFill(v, 3, 31, 0,    W - 4, H - 1, D - 3, STONE);     // chimney breast
    return v;
}

VoxelVolume* buildRug() {
    // A flat woven rug — burgundy pile, a gold border and a central medallion.
    // Enlarged to a proper room rug (≈4.6x the old footprint). Two voxels tall
    // so furniture set on top still reads as resting on a rug.
    const int W = 56, H = 2, D = 40;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel BODY   {150,  46,  46, 255};
    const Voxel BORDER {200, 170,  98, 255};
    const Voxel MEDAL  { 54,  74, 122, 255};
    voxFill(v, 0, 0, 0,     W - 1, 1, D - 1, BODY);          // pile
    voxFill(v, 0, 0, 0,     W - 1, 1, 2,     BORDER);        // borders (3-wide)
    voxFill(v, 0, 0, D - 3, W - 1, 1, D - 1, BORDER);
    voxFill(v, 0, 0, 0,     2,     1, D - 1, BORDER);
    voxFill(v, W - 3, 0, 0, W - 1, 1, D - 1, BORDER);
    voxFill(v, W / 2 - 11, 1, D / 2 - 8, W / 2 + 10, 1, D / 2 + 7, MEDAL);   // medallion
    voxFill(v, W / 2 - 7,  1, D / 2 - 5, W / 2 + 6,  1, D / 2 + 4, BORDER);
    voxFill(v, W / 2 - 3,  1, D / 2 - 2, W / 2 + 2,  1, D / 2 + 1, MEDAL);
    return v;
}

VoxelVolume* buildWallPainting() {
    // A small framed landscape, hung on a wall (back at z=0, picture faces +Z).
    const int W = 12, H = 12, D = 2;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel FRAME {150, 112,  52, 255};
    const Voxel SKY   {120, 172, 220, 255};
    const Voxel FIELD { 88, 142,  72, 255};
    const Voxel SUN   {246, 222, 120, 255};
    voxFill(v, 0, 0, 0,     W - 1, H - 1, 1, FRAME);         // frame + backing
    voxFill(v, 2, 2, 1,     W - 3, H - 3, 1, SKY);           // canvas: sky
    voxFill(v, 2, 2, 1,     W - 3, 5,     1, FIELD);         // canvas: field
    voxFill(v, W - 5, H - 5, 1, W - 4, H - 4, 1, SUN);       // a low sun
    return v;
}

VoxelVolume* buildFlowerVase() {
    // A little ceramic vase of mixed flowers — a table-topper or floor accent.
    const int W = 7, H = 13, D = 7;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel VASE {86, 128, 182, 255};
    const Voxel STEM {72, 132,  62, 255};
    const Voxel F1 {220,  70,  70, 255}, F2 {240, 210,  90, 255};
    const Voxel F3 {224, 120, 180, 255}, F4 {170, 110, 210, 255};
    voxFill(v, 2, 0, 2, 4, 4, 4, VASE);                      // vase body
    voxFill(v, 3, 4, 3, 3, 8, 3, STEM);                      // stems
    voxFill(v, 2, 5, 3, 2, 7, 3, STEM);
    voxFill(v, 4, 6, 4, 4, 8, 4, STEM);
    voxFill(v, 3, 8, 3, 3, 9, 3, F1);                        // flower heads
    voxFill(v, 2, 7, 3, 2, 8, 3, F2);
    voxFill(v, 4, 8, 4, 4, 9, 4, F3);
    voxFill(v, 3, 9, 3, 3, 10, 3, F4);
    return v;
}
