#pragma once
#include "voxel_model.h"   // Voxel, VoxelVolume

// Fills an inclusive voxel box [x0,x1]x[y0,y1]x[z0,z1]; clamps to volume bounds.
void voxFill(VoxelVolume* v, int x0, int y0, int z0,
             int x1, int y1, int z1, Voxel c);

// Furniture builders (furniture.cpp). Each allocates a VoxelVolume holding only
// voxel data — the caller (PropLibrary) calls updateMesh() on the main thread.
VoxelVolume* buildBookshelf();
VoxelVolume* buildBed();
VoxelVolume* buildLanternProp();
VoxelVolume* buildCooker();
VoxelVolume* buildTable();
VoxelVolume* buildChair();
VoxelVolume* buildCrockery();

// Phase 2: room-specific furniture.
VoxelVolume* buildSink();
VoxelVolume* buildKitchenCounter();
VoxelVolume* buildWardrobe();
VoxelVolume* buildDesk();
VoxelVolume* buildCouch();
VoxelVolume* buildSideTable();

// Phase 3: special-building furniture.
VoxelVolume* buildAnvil();
VoxelVolume* buildForge();
VoxelVolume* buildBarCounter();
VoxelVolume* buildBarStool();
VoxelVolume* buildCauldron();
VoxelVolume* buildAlchemyTable();

// Cosy home furnishings (furniture.cpp): a glowing stone hearth, a floor rug,
// framed wall art and a vase of flowers.
VoxelVolume* buildFireplace();
VoxelVolume* buildRug();
VoxelVolume* buildWallPainting();
VoxelVolume* buildFlowerVase();

// Trade signs hanging from a wooden post — one builder per icon. Each shares
// the same post + frame geometry; the body of the shield differs.
VoxelVolume* buildTradeSignAnvil();
VoxelVolume* buildTradeSignMug();
VoxelVolume* buildTradeSignStar();
VoxelVolume* buildTradeSignWheat();

// Decoration builders (decorations.cpp).
VoxelVolume* buildStreetLamp();
VoxelVolume* buildPottedPlant();
VoxelVolume* buildBush();
VoxelVolume* buildBushFlowering();
VoxelVolume* buildBushBerry();
VoxelVolume* buildBushConifer();
VoxelVolume* buildBushDry();
VoxelVolume* buildBench();
VoxelVolume* buildFenceSection();
VoxelVolume* buildFlowerPot();
VoxelVolume* buildFlowerBed();
VoxelVolume* buildBarrel();
VoxelVolume* buildBuntingSpan();
VoxelVolume* buildCrate();
VoxelVolume* buildProducePile();
VoxelVolume* buildFountain();
VoxelVolume* buildMarketStall();
VoxelVolume* buildNoticeBoard();

// Graveyard headstones — weathered stone slab, stone cross-topped slab, and a
// simple wooden grave cross on a dirt mound.
VoxelVolume* buildTombstone();
VoxelVolume* buildTombstoneCross();
VoxelVolume* buildGraveCross();

// Graveyard dressing — a mound of dug earth, a spade stuck in the ground, a
// laid posy of flowers, a flower wreath, a stone urn, and a bare dead tree.
VoxelVolume* buildSoilMound();
VoxelVolume* buildSpade();
VoxelVolume* buildGraveFlowers();
VoxelVolume* buildFlowerWreath();
VoxelVolume* buildStoneUrn();
VoxelVolume* buildDeadTree();

// More home furnishings: a banded storage chest, a small round stool (sittable),
// and a candle stand that glows at night.
VoxelVolume* buildChest();
VoxelVolume* buildStool();
VoxelVolume* buildCandelabra();
VoxelVolume* buildBookpile();
VoxelVolume* buildWallShelf();
VoxelVolume* buildWallClock();

// Openable house door panel, one mesh per style/colour (the Door object).
VoxelVolume* buildDoor(int variant);
