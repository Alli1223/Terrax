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

VoxelVolume* buildFlowerPot() {
    // Small terracotta pot with a bright mixed-colour flower crown.
    const int W = 10, H = 16, D = 10;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel TERRA  {172,  92,  56, 255};
    const Voxel TERRAL {192, 108,  68, 255};
    const Voxel SOIL   { 56,  38,  22, 255};
    const Voxel STEM   { 72, 106,  52, 255};
    const Voxel FRED   {218,  62,  52, 255};
    const Voxel FYEL   {230, 192,  40, 255};
    const Voxel FPNK   {228, 108, 148, 255};
    const Voxel FWHT   {240, 236, 232, 255};
    voxFill(v, 2, 0, 2, 7, 0, 7, TERRA);           // narrow base
    voxFill(v, 1, 1, 1, 8, 5, 8, TERRA);            // pot body
    voxFill(v, 0, 6, 0, 9, 7, 9, TERRAL);           // flared rim
    voxFill(v, 2, 8, 2, 7, 8, 7, SOIL);             // soil
    v->setVoxel(4,  9, 4, STEM); v->setVoxel(5,  9, 5, STEM);
    v->setVoxel(4, 10, 6, STEM); v->setVoxel(6, 10, 4, STEM);
    voxFill(v, 2, 10, 2, 5, 13, 5, FRED);           // red cluster
    voxFill(v, 5, 10, 4, 7, 13, 7, FYEL);           // yellow cluster
    voxFill(v, 3, 13, 4, 6, 15, 7, FPNK);           // pink crown
    v->setVoxel(3, 14, 3, FWHT); v->setVoxel(7, 13, 3, FWHT);
    return v;
}

VoxelVolume* buildFlowerBed() {
    // A low stone-bordered bed filled with rows of colourful flowers.
    const int W = 28, H = 10, D = 16;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel STONE  {140, 130, 120, 255};
    const Voxel SOIL   { 56,  38,  22, 255};
    const Voxel STEM   { 72, 106,  52, 255};
    const Voxel FRED   {218,  62,  52, 255};
    const Voxel FYEL   {230, 192,  40, 255};
    const Voxel FPNK   {228, 108, 148, 255};
    const Voxel FPUR   {148,  72, 200, 255};
    voxFill(v,  0, 0,  0, W-1, 2, D-1, STONE);      // stone border/base
    voxFill(v,  1, 3,  1, W-2, 3, D-2, SOIL);        // inner soil
    voxFill(v,  1, 4,  1, W-2, 5, D-2, STEM);        // stem layer
    voxFill(v,  1, 6,  1,  5,  9,  5, FRED);         // red strip
    voxFill(v,  6, 6,  1, 12,  9,  5, FYEL);         // yellow strip
    voxFill(v, 13, 6,  1, 19,  9,  5, FPNK);         // pink strip
    voxFill(v, 20, 6,  1, W-2, 9,  5, FPUR);         // purple strip
    voxFill(v,  1, 6,  6,  8,  9, D-2, FPNK);        // back-row pink
    voxFill(v,  9, 6,  6, 18,  9, D-2, FYEL);        // back-row yellow
    voxFill(v, 19, 6,  6, W-2, 9, D-2, FRED);        // back-row red
    return v;
}

VoxelVolume* buildBarrel() {
    // A wooden barrel with iron hoops — octagonal cross-section via two rects.
    const int W = 12, H = 18, D = 12;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel WOOD  {140,  90,  50, 255};
    const Voxel WOODL {165, 110,  65, 255};
    const Voxel IRON  { 70,  72,  80, 255};
    // Stave body (octagonal cross-section via overlapping rectangles)
    voxFill(v, 1, 0,  0, W-2, H-2, D-1, WOOD);
    voxFill(v, 0, 0,  1, W-1, H-2, D-2, WOOD);
    // Plank-seam highlights
    for (int x = 3; x < W-1; x += 3)
        voxFill(v, x, 1, 1, x, H-3, D-2, WOODL);
    // Three iron hoops
    for (int hy : {2, H/2, H-4}) {
        voxFill(v, 1, hy, 0, W-2, hy+1, D-1, IRON);
        voxFill(v, 0, hy, 1, W-1, hy+1, D-2, IRON);
    }
    // Lid
    voxFill(v, 2, H-1, 2, W-3, H-1, D-3, WOODL);
    voxFill(v, 1, H-1, 3, W-2, H-1, D-4, WOODL);
    return v;
}

VoxelVolume* buildBuntingSpan() {
    // One 3-world-unit section of bunting rope — tiled end-to-end between lamp
    // posts.  The rope runs along model +Z; pennants hang down from it.
    // W=4, H=14, D=50  (50 voxels × 0.06 = 3.0 world units per segment).
    const int W = 4, H = 14, D = 50;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel ROPE  { 80,  60,  38, 255};
    const Voxel FLAGS[4] = {
        {210,  65,  55, 255},   // red
        { 55, 115, 195, 255},   // blue
        {220, 185,  45, 255},   // yellow
        { 65, 155,  75, 255},   // green
    };
    // Rope: thin line along the top.
    voxFill(v, 1, H-1, 0, 2, H-1, D-1, ROPE);
    // Small hook loops at each end so adjoining segments visually connect.
    voxFill(v, 1, H-3, 0, 2, H-1, 1, ROPE);
    voxFill(v, 1, H-3, D-2, 2, H-1, D-1, ROPE);
    // Three pennants evenly spaced along the segment.
    for (int p = 0; p < 3; p++) {
        int fz = 8 + p * 17;
        voxFill(v, 1, H-8, fz, 2, H-2, fz + 1, FLAGS[p]);
    }
    return v;
}

VoxelVolume* buildCrate() {
    // A wooden shipping crate — stacked near market stalls and warehouses.
    const int W = 14, H = 14, D = 14;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel PLANK  {145,  95,  52, 255};
    const Voxel PLANKD {100,  64,  34, 255};
    const Voxel BAND   { 70,  48,  26, 255};
    const Voxel STRAW  {218, 198,  78, 255};
    voxFill(v, 0, 0, 0, W-1, H-1, D-1, PLANK);     // body
    // Horizontal strap bands
    for (int b : {2, H/2, H-3}) {
        voxFill(v, 0, b, 0, W-1, b, D-1, BAND);
    }
    // Vertical edge reinforcements
    for (int ex : {0, W-1}) for (int ez : {0, D-1})
        voxFill(v, ex, 0, ez, ex, H-1, ez, PLANKD);
    // Lid plank seam lines
    voxFill(v, 0, H-1, D/3,   W-1, H-1, D/3,   PLANKD);
    voxFill(v, 0, H-1, 2*D/3, W-1, H-1, 2*D/3, PLANKD);
    // Straw peeking from lid gap
    voxFill(v, 3, H-1, 3, W-4, H-1, D-4, STRAW);
    return v;
}

VoxelVolume* buildProducePile() {
    // A wicker basket overflowing with market produce — sits on a stall counter.
    const int W = 16, H = 14, D = 16;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel BASK  {168, 126,  72, 255};  // wicker
    const Voxel BASKD {128,  90,  48, 255};  // wicker dark
    const Voxel RED   {210,  58,  48, 255};  // apples / tomatoes
    const Voxel ORAN  {228, 136,  44, 255};  // oranges / squash
    const Voxel YELL  {228, 200,  48, 255};  // lemons / corn
    const Voxel GRN   { 72, 148,  56, 255};  // cabbage / herbs
    const Voxel PURP  {138,  68, 172, 255};  // grapes / plums
    // Basket body
    voxFill(v, 1, 0, 1, W-2, 6, D-2, BASK);
    voxFill(v, 0, 1, 2, W-1, 5, D-3, BASK);  // octagonal cross-section
    voxFill(v, 2, 1, 0, W-3, 5, D-1, BASK);
    // Wicker weave lines
    for (int wy = 2; wy < 6; wy += 2)
        voxFill(v, 0, wy, 0, W-1, wy, D-1, BASKD);
    // Mounded produce spilling over the basket rim
    voxFill(v, 2, 7, 2, W-3, 8, D-3, RED);    // base produce layer
    voxFill(v, 3, 9, 3, W-4, 9, D-4, ORAN);   // orange layer
    voxFill(v, 4, 10, 4, W-5, 10, D-5, YELL); // yellow on top
    // Accent colours scattered across the pile
    voxFill(v, 2,  8, 5, 4, 9, 7, GRN);
    voxFill(v, W-5, 8, W-8, W-3, 9, W-6, PURP);
    voxFill(v, 5, 11, 5, W-6, 12, D-6, RED);  // crowning layer
    return v;
}

VoxelVolume* buildFountain() {
    // A tiered stone fountain — lower basin, central column, upper bowl, spray.
    const int W = 20, H = 16, D = 20;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel STONE  {158, 153, 146, 255};
    const Voxel STONEL {182, 177, 170, 255};
    const Voxel WATER  { 54, 124, 200, 255};
    const Voxel SPRAY  {150, 205, 240, 255};
    // Lower basin — full base, then 1-block rim walls, water fill inside.
    voxFill(v,  0, 0,  0, W-1, 0, D-1, STONE);      // base slab
    voxFill(v,  0, 1,  0, W-1, 3, 0,   STONE);       // south wall
    voxFill(v,  0, 1, D-1, W-1, 3, D-1, STONE);      // north wall
    voxFill(v,  0, 1,  0,  0,  3, D-1, STONE);       // west wall
    voxFill(v, W-1,1,  0, W-1, 3, D-1, STONE);       // east wall
    voxFill(v,  1, 1,  1, W-2, 2, D-2, WATER);       // water in lower basin
    // Central column
    voxFill(v,  8, 4,  8, 11, 9, 11, STONE);
    // Upper bowl
    voxFill(v,  5,10,  5, 14,10, 14, STONEL);        // bowl base ring
    voxFill(v,  5,10,  5,  5,12, 14, STONEL);        // left wall
    voxFill(v, 14,10,  5, 14,12, 14, STONEL);        // right wall
    voxFill(v,  5,10,  5, 14,12,  5, STONEL);        // front wall
    voxFill(v,  5,10, 14, 14,12, 14, STONEL);        // back wall
    voxFill(v,  6,11,  6, 13,12, 13, WATER);         // water in upper bowl
    // Top spout
    voxFill(v,  9,13,  9, 10,13, 10, STONE);
    // Water spray
    v->setVoxel( 9,H-1, 8, SPRAY); v->setVoxel(10,H-1,11, SPRAY);
    v->setVoxel( 8,H-1, 9, SPRAY); v->setVoxel(11,H-1,10, SPRAY);
    v->setVoxel( 9,H-1,10, SPRAY); v->setVoxel(10,H-1, 9, SPRAY);
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
