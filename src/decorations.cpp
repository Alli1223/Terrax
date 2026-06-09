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

VoxelVolume* buildBushFlowering() {
    // A leafy bush dotted with little flowers — for plains and forest edges.
    const int W = 18, H = 17, D = 18;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel STEM {86, 64, 42, 255};
    const Voxel FP {228, 120, 160, 255}, FW {238, 236, 230, 255}, FY {232, 200, 70, 255};
    voxFill(v, 7, 0, 7, 10, 4, 10, STEM);                      // stem
    voxFill(v, 2, 3, 2, W - 3, 12, D - 3, LEAF);               // main mass
    voxFill(v, 4, 12, 4, W - 5, 14, D - 5, LEAFD);             // crown
    voxFill(v, 1, 5, 5, 1, 9, D - 6, LEAFD);                   // side tufts
    voxFill(v, W - 2, 5, 5, W - 2, 9, D - 6, LEAFD);
    // Scattered flower dots over the crown.
    v->setVoxel(5, 13, 6, FP);  v->setVoxel(9, 14, 9, FW);  v->setVoxel(12, 13, 5, FY);
    v->setVoxel(6, 13, 12, FY); v->setVoxel(11, 14, 11, FP); v->setVoxel(8, 15, 8, FW);
    v->setVoxel(4, 11, 9, FP);  v->setVoxel(13, 12, 10, FW);
    return v;
}

VoxelVolume* buildBushBerry() {
    // A dark, dense shrub speckled with red berries.
    const int W = 17, H = 15, D = 17;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel STEM {80, 58, 38, 255};
    const Voxel DK {40, 92, 40, 255}, DK2 {30, 74, 34, 255};
    const Voxel BERRY {178, 36, 44, 255};
    voxFill(v, 6, 0, 6, 9, 4, 9, STEM);
    voxFill(v, 2, 3, 2, W - 3, 11, D - 3, DK);
    voxFill(v, 3, 11, 3, W - 4, 13, D - 4, DK2);
    v->setVoxel(4, 9, 5, BERRY);  v->setVoxel(11, 8, 4, BERRY);  v->setVoxel(5, 10, 12, BERRY);
    v->setVoxel(12, 10, 11, BERRY); v->setVoxel(8, 7, 13, BERRY); v->setVoxel(13, 6, 8, BERRY);
    v->setVoxel(3, 7, 9, BERRY);  v->setVoxel(9, 12, 8, BERRY);
    return v;
}

VoxelVolume* buildBushConifer() {
    // A small tiered evergreen shrub — for mountains and tundra.
    const int W = 16, H = 26, D = 16;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel TRUNK {84, 60, 40, 255};
    const Voxel PINE {38, 86, 56, 255}, PINED {28, 68, 46, 255};
    voxFill(v, 7, 0, 7, 8, 6, 8, TRUNK);
    voxFill(v, 2, 4, 2, W - 3, 8, D - 3, PINE);                // wide skirt
    voxFill(v, 3, 8, 3, W - 4, 12, D - 4, PINED);
    voxFill(v, 4, 12, 4, W - 5, 16, D - 5, PINE);
    voxFill(v, 5, 16, 5, W - 6, 20, D - 6, PINED);
    voxFill(v, 6, 20, 6, 9, 24, 9, PINE);                      // tapered top
    v->setVoxel(7, 25, 7, PINED); v->setVoxel(8, 25, 8, PINED);
    return v;
}

VoxelVolume* buildBushDry() {
    // A sparse, twiggy olive-brown shrub — for deserts and savanna.
    const int W = 16, H = 13, D = 16;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel TWIG {120, 96, 58, 255};
    const Voxel OLIVE {126, 132, 74, 255}, OLIVED {98, 104, 58, 255};
    voxFill(v, 7, 0, 7, 8, 7, 8, TWIG);                        // twiggy frame
    voxFill(v, 4, 3, 7, 11, 4, 8, TWIG);
    voxFill(v, 7, 3, 4, 8, 4, 11, TWIG);
    voxFill(v, 3, 5, 3, 7, 9, 7, OLIVE);                       // patchy clumps
    voxFill(v, 8, 6, 8, 12, 10, 12, OLIVED);
    voxFill(v, 9, 5, 3, 12, 8, 6, OLIVE);
    voxFill(v, 3, 6, 9, 6, 9, 12, OLIVED);
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

// --- Graveyard headstones --------------------------------------------------
namespace {
const Voxel STONE_G {124, 126, 132, 255};   // pale weathered granite
const Voxel STONE_D { 92,  94, 100, 255};   // shadowed / engraved
const Voxel MOSS_G  { 78, 116,  64, 255};   // creeping moss
const Voxel GWOOD   {110,  74,  42, 255};   // grave-cross timber
const Voxel GWOODD  { 78,  50,  30, 255};
const Voxel MOUND   { 88,  64,  40, 255};   // turned earth at the base
}

VoxelVolume* buildTombstone() {
    // A rounded headstone: a plinth, a slab body, and a stepped arch top that
    // reads as a curved crown. Front face carries a couple of engraved lines
    // and one side is streaked with moss so no two rows look identical.
    const int W = 14, H = 28, D = 6;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    voxFill(v, 1, 0, 0, W - 2, 2, D - 1, STONE_D);        // base plinth (wider, dark)
    voxFill(v, 3, 2, 1, W - 4, H - 7, D - 2, STONE_G);    // main slab
    voxFill(v, 4, H - 7, 1, W - 5, H - 5, D - 2, STONE_G);// shoulders (narrower)
    voxFill(v, 5, H - 5, 1, W - 6, H - 3, D - 2, STONE_G);// arch
    voxFill(v, 6, H - 3, 1, W - 7, H - 2, D - 2, STONE_G);// crown
    voxFill(v, 5,  9, 1, W - 6,  9, 1, STONE_D);          // engraved line
    voxFill(v, 5, 13, 1, W - 6, 13, 1, STONE_D);
    voxFill(v, 6, 17, 1, W - 7, 17, 1, STONE_D);
    voxFill(v, 3, 2, 1, 3,  9, 1, MOSS_G);               // moss up one edge
    voxFill(v, 1, 0, 0, W - 2, 0, D - 1, MOSS_G);         // moss round the base
    return v;
}

VoxelVolume* buildTombstoneCross() {
    // A slab topped with a raised cross — the grander marker.
    const int W = 12, H = 32, D = 6;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    int cx = W / 2;
    voxFill(v, 1, 0, 0, W - 2, 2, D - 1, STONE_D);        // plinth
    voxFill(v, 3, 2, 1, W - 4, H - 13, D - 2, STONE_G);   // slab body
    voxFill(v, cx - 1, H - 13, 2, cx, H - 1, D - 3, STONE_G);   // cross — upright
    voxFill(v, cx - 4, H - 8,  2, cx + 3, H - 6, D - 3, STONE_G);// cross — arms
    voxFill(v, 4, 8, 1, W - 5, 8, 1, STONE_D);            // engraving
    voxFill(v, 3, 2, 1, 3, 7, 1, MOSS_G);                // moss
    return v;
}

VoxelVolume* buildGraveCross() {
    // A humble wooden cross on a small mound of turned earth.
    const int W = 10, H = 26, D = 4;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    int cx = W / 2;
    voxFill(v, 1, 0, 0, W - 2, 1, D - 1, MOUND);          // earth mound
    voxFill(v, cx - 1, 1, 1, cx, H - 1, D - 2, GWOOD);    // upright post
    voxFill(v, 1, H - 9, 1, W - 2, H - 7, D - 2, GWOOD);  // crossbar
    voxFill(v, cx - 1, 4, 1, cx - 1, H - 4, 1, GWOODD);   // grain accent
    return v;
}

VoxelVolume* buildSoilMound() {
    // A low rounded heap of freshly dug earth — a new grave or a digger's pile.
    const int W = 22, H = 8, D = 14;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel SOIL {84, 58, 36, 255}, SOILD {62, 42, 27, 255}, SOILL {106, 76, 48, 255};
    voxFill(v, 1, 0, 1, W - 2, 1, D - 2, SOILD);          // wide dark base
    voxFill(v, 2, 1, 2, W - 3, 3, D - 3, SOIL);
    voxFill(v, 4, 3, 3, W - 5, 5, D - 4, SOIL);           // stepped to a rounded crown
    voxFill(v, 6, 5, 4, W - 7, 6, D - 5, SOILL);
    voxFill(v, 3, 2, 3, 4, 3, 4, SOILL);                  // scattered clods
    voxFill(v, W - 5, 2, D - 5, W - 4, 3, D - 4, SOILD);
    return v;
}

VoxelVolume* buildSpade() {
    // A gravedigger's spade standing blade-down in a little turned earth.
    const int W = 8, H = 30, D = 6;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel SWOOD {124, 84, 48, 255}, METAL {126, 130, 138, 255},
                METALD {96, 100, 108, 255}, SOIL {80, 56, 34, 255};
    int cx = W / 2;
    voxFill(v, 1, 0, 1, W - 2, 1, D - 2, SOIL);           // earth at the base
    voxFill(v, cx - 2, 1, 1, cx + 1, 7, D - 2, METALD);   // blade
    voxFill(v, cx - 1, 7, 2, cx, 9, D - 3, METAL);        // socket
    voxFill(v, cx - 1, 9, 2, cx, H - 4, D - 3, SWOOD);    // shaft
    voxFill(v, cx - 2, H - 4, 2, cx + 1, H - 2, D - 3, SWOOD); // D-grip
    return v;
}

VoxelVolume* buildGraveFlowers() {
    // A small posy of cut flowers laid on the ground, blooms at one end.
    const int W = 12, H = 4, D = 8;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel STEM {70, 110, 50, 255}, R {212, 72, 82, 255}, Y {240, 210, 92, 255},
                P {200, 120, 210, 255}, WH {236, 236, 240, 255};
    voxFill(v, 2, 0, 3, W - 3, 1, 4, STEM);               // bundled stems
    voxFill(v, W - 5, 1, 2, W - 4, 2, 3, R);              // blooms
    voxFill(v, W - 4, 1, 4, W - 3, 2, 5, Y);
    voxFill(v, W - 6, 1, 4, W - 5, 2, 5, P);
    v->setVoxel(W - 3, 1, 4, WH);
    return v;
}

VoxelVolume* buildFlowerWreath() {
    // A circular wreath of greenery and flowers laid flat on a grave.
    const int W = 16, H = 4, D = 16;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel GREEN {64, 108, 52, 255}, GREEND {48, 86, 42, 255},
                R {212, 72, 82, 255}, Y {240, 210, 92, 255}, WH {236, 236, 240, 255};
    const int cx = 8, cz = 8, rin = 4, rout = 7;
    for (int z = 0; z < D; z++)
        for (int x = 0; x < W; x++) {
            int dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
            if (d2 >= rin * rin && d2 <= rout * rout)
                voxFill(v, x, 0, z, x, 1, z, ((x + z) & 1) ? GREEN : GREEND);
        }
    v->setVoxel(cx,          2, cz + rout - 1, R);        // dotted blooms on the ring
    v->setVoxel(cx + rout - 1, 2, cz,          Y);
    v->setVoxel(cx - rout + 1, 2, cz,          WH);
    v->setVoxel(cx,          2, cz - rout + 1, R);
    return v;
}

VoxelVolume* buildStoneUrn() {
    // A classical funerary urn on a small plinth, a touch of moss at the foot.
    const int W = 12, H = 22, D = 12;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel STONE {130, 132, 138, 255}, STONED {100, 102, 108, 255}, MOSS {78, 116, 64, 255};
    voxFill(v, 1, 0, 1, W - 2, 2, D - 2, STONED);         // plinth
    voxFill(v, 3, 2, 3, W - 4, 4, D - 4, STONE);          // foot
    voxFill(v, 2, 4, 2, W - 3, 12, D - 3, STONE);         // body
    voxFill(v, 1, 8, 1, W - 2, 10, D - 2, STONE);         // belly bulge
    voxFill(v, 3, 12, 3, W - 4, 14, D - 4, STONE);        // neck
    voxFill(v, 1, 14, 1, W - 2, 16, D - 2, STONE);        // flared rim
    voxFill(v, 3, 16, 3, W - 4, 17, D - 4, STONED);       // recessed mouth
    voxFill(v, 1, 0, 1, 2, 5, 2, MOSS);                   // moss
    return v;
}

VoxelVolume* buildDeadTree() {
    // A bare, gnarled tree — a leafless yew silhouette for atmosphere.
    const int W = 18, H = 56, D = 18;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel BARK {80, 64, 50, 255}, BARKD {58, 46, 36, 255};
    int cx = W / 2;
    voxFill(v, cx - 2, 0, cx - 2, cx + 1, 6, cx + 1, BARKD);  // root flare
    voxFill(v, cx - 1, 6, cx - 1, cx, H - 14, cx, BARK);      // trunk
    voxFill(v, cx, H - 30, cx, cx + 5, H - 26, cx, BARK);     // a bare branch...
    voxFill(v, cx + 4, H - 26, cx, cx + 5, H - 20, cx, BARK); // ...turning up
    voxFill(v, cx - 5, H - 22, cx, cx - 1, H - 19, cx, BARK); // a branch the other way
    voxFill(v, cx - 5, H - 19, cx, cx - 4, H - 14, cx, BARK);
    voxFill(v, cx, H - 16, cx - 5, cx, H - 12, cx - 1, BARK); // a branch in -Z
    voxFill(v, cx, H - 14, cx, cx, H - 6, cx, BARK);          // top spire
    voxFill(v, cx - 2, H - 34, cx - 1, cx - 1, H - 33, cx, BARKD); // gnarl knot
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

VoxelVolume* buildMarketStall() {
    // A market stall: a timber counter laden with goods under a front-sloping
    // striped awning on four posts. The open customer side faces +Z, so the
    // placer yaws the stall to look onto the plaza.
    const int W = 34, H = 56, D = 22;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel POST  {110,  74,  42, 255};   // timber posts
    const Voxel POSTD { 84,  54,  30, 255};
    const Voxel CNTR  {150, 102,  58, 255};   // counter top
    const Voxel CNTRD {104,  68,  38, 255};   // counter body / back board
    const Voxel CANV  {216, 214, 206, 255};   // cream canvas
    const Voxel STRIP {196,  66,  58, 255};   // red stripe
    const Voxel SACK  {178, 150,  96, 255};   // hessian sack
    const Voxel GOODR {206,  72,  58, 255};
    const Voxel GOODG { 84, 152,  66, 255};
    const Voxel GOODY {224, 196,  64, 255};
    // Counter (front half), chest height.
    voxFill(v, 2,  0, 11, W-3, 12, 19, CNTRD);     // counter body
    voxFill(v, 1, 13, 10, W-2, 14, 20, CNTR);       // counter top, slight overhang
    // Four posts — taller pair at the back so the awning slopes to the front.
    voxFill(v, 2,    0, 2, 4,   50, 4, POST);        // back-left
    voxFill(v, W-5,  0, 2, W-3, 50, 4, POST);        // back-right
    voxFill(v, 2,    0, 18, 4,   38, 20, POSTD);     // front-left
    voxFill(v, W-5,  0, 18, W-3, 38, 20, POSTD);     // front-right
    // Back board between the rear posts.
    voxFill(v, 3, 14, 2, W-4, 44, 3, CNTRD);
    // Sloping striped awning — high at the back (y≈50), low at the front (y≈38).
    for (int zz = 2; zz <= 19; zz++) {
        float t = (float)(zz - 2) / 17.0f;
        int ay = 50 - (int)(t * 12.0f);
        for (int x = 1; x <= W - 2; x++) {
            Voxel c = (((x) / 4) % 2 == 0) ? CANV : STRIP;
            v->setVoxel(x, ay,     zz, c);
            v->setVoxel(x, ay + 1, zz, c);
        }
    }
    // Hanging valance along the awning's front lip.
    for (int x = 1; x <= W - 2; x++) {
        Voxel c = ((x / 4) % 2 == 0) ? CANV : STRIP;
        voxFill(v, x, 34, 20, x, 37, 20, c);
    }
    // Goods heaped on the counter top (rest on y = 15).
    voxFill(v,  4, 15, 12,  9, 18, 17, GOODR);      // red produce
    voxFill(v, 11, 15, 12, 16, 17, 17, GOODG);      // greens
    voxFill(v, 18, 15, 12, 23, 18, 17, GOODY);      // yellow fruit
    voxFill(v, 25, 15, 13, 30, 19, 18, SACK);       // a slumped sack
    return v;
}

VoxelVolume* buildNoticeBoard() {
    // A village notice board: a planked board on two posts under a little peaked
    // roof, papered with pinned notices. The reading face is +Z.
    const int W = 22, H = 42, D = 6;
    VoxelVolume* v = new VoxelVolume(W, H, D);
    const Voxel POST  { 96,  64,  38, 255};
    const Voxel BOARD {132,  92,  54, 255};
    const Voxel BOARDD{100,  68,  40, 255};
    const Voxel ROOF  { 78,  52,  30, 255};
    const Voxel PWHT  {236, 232, 222, 255};
    const Voxel PYEL  {226, 206, 140, 255};
    const Voxel PPNK  {196,  92,  92, 255};
    // Two posts.
    voxFill(v, 2,   0, 2, 4,   30, 3, POST);
    voxFill(v, W-5, 0, 2, W-3, 30, 3, POST);
    // Plank board with seams.
    voxFill(v, 1, 14, 2, W-2, 34, 3, BOARDD);
    voxFill(v, 2, 15, 3, W-3, 33, 3, BOARD);
    for (int yy = 18; yy < 33; yy += 5)
        voxFill(v, 2, yy, 3, W-3, yy, 3, BOARDD);
    // Pinned paper notices (stand one voxel proud of the board face).
    voxFill(v,  3, 24, 4,  8, 31, 4, PWHT);
    voxFill(v, 10, 26, 4, 14, 32, 4, PYEL);
    voxFill(v, 15, 21, 4, 18, 28, 4, PPNK);
    voxFill(v,  4, 16, 4,  9, 20, 4, PYEL);
    // Little peaked shingle roof.
    voxFill(v, 0, 35, 1, W-1, 36, 4, ROOF);
    voxFill(v, 2, 37, 1, W-3, 38, 4, ROOF);
    voxFill(v, 5, 39, 2, W-6, 39, 3, ROOF);
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
