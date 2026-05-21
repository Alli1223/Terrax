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

// Decoration builders (decorations.cpp).
VoxelVolume* buildStreetLamp();
VoxelVolume* buildPottedPlant();
VoxelVolume* buildBush();
VoxelVolume* buildBench();
VoxelVolume* buildFenceSection();
