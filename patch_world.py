import re

with open('src/world.cpp', 'r') as f:
    code = f.read()

# 1. Update BiomeDef
biome_def_old = """struct BiomeDef {
    float idealTemp, idealHumid; // centre in [0,1]×[0,1] temperature-humidity space
    float freq;                  // 2-D heightmap noise frequency
    float amplitude;             // height-noise multiplier (world Y units)
    int   octaves;
    float persistence;
    BlockType surfaceBlock;
    BlockType subSurfaceBlock;
};"""

biome_def_new = """struct BiomeDef {
    float idealTemp, idealHumid; // centre in [0,1]×[0,1] temperature-humidity space
    float freq;                  // 2-D heightmap noise frequency
    float amplitude;             // height-noise multiplier (world Y units)
    float baseOffset;            // base height offset from SEA_LEVEL
    int   octaves;
    float persistence;
    BlockType surfaceBlock;
    BlockType subSurfaceBlock;
};"""

code = code.replace(biome_def_old, biome_def_new)

# 2. Update NUM_BIOMES and BIOMES array
num_biomes_old = "static constexpr int NUM_BIOMES = 7;"
num_biomes_new = "static constexpr int NUM_BIOMES = 8;"
code = code.replace(num_biomes_old, num_biomes_new)

biomes_enum_old = "enum class Biome : uint8_t { Plains=0, Forest=1, Desert=2, Mountains=3, Tundra=4, Savanna=5, Jungle=6 };"
biomes_enum_new = "enum class Biome : uint8_t { Plains=0, Forest=1, Desert=2, Mountains=3, Tundra=4, Savanna=5, Jungle=6, Ocean=7 };"
code = code.replace(biomes_enum_old, biomes_enum_new)

biomes_array_old = """static const BiomeDef BIOMES[NUM_BIOMES] = {
  // temp   humid  freq     amp   oct pers   surf                   sub
    {0.50f, 0.40f, 0.008f, 10.0f, 4, 0.50f, BlockType::Grass,      BlockType::Dirt      }, // Plains
    {0.50f, 0.80f, 0.010f, 16.0f, 5, 0.55f, BlockType::Grass,      BlockType::Dirt      }, // Forest
    {0.90f, 0.10f, 0.009f,  8.0f, 3, 0.40f, BlockType::Sand,       BlockType::Sandstone }, // Desert
    {0.10f, 0.50f, 0.013f, 32.0f, 6, 0.60f, BlockType::Snow,       BlockType::Stone     }, // Mountains
    {0.10f, 0.20f, 0.007f, 10.0f, 4, 0.50f, BlockType::Snow,       BlockType::Stone     }, // Tundra
    {0.75f, 0.25f, 0.007f,  9.0f, 3, 0.45f, BlockType::Grass,      BlockType::Dirt      }, // Savanna
    {0.85f, 0.90f, 0.011f, 20.0f, 5, 0.55f, BlockType::Grass,      BlockType::Dirt      }, // Jungle
};"""

biomes_array_new = """static const BiomeDef BIOMES[NUM_BIOMES] = {
  // temp   humid  freq     amp    offset oct pers   surf                   sub
    {0.50f, 0.40f, 0.008f, 15.0f,  2.0f,  4, 0.55f, BlockType::Grass,      BlockType::Dirt      }, // Plains (more varied)
    {0.50f, 0.80f, 0.012f, 25.0f,  5.0f,  5, 0.60f, BlockType::Grass,      BlockType::Dirt      }, // Forest
    {0.90f, 0.10f, 0.015f, 18.0f,  0.0f,  4, 0.50f, BlockType::Sand,       BlockType::Sandstone }, // Desert (dunes)
    {0.20f, 0.50f, 0.015f, 80.0f, 30.0f,  6, 0.65f, BlockType::Snow,       BlockType::Stone     }, // Mountains (taller)
    {0.10f, 0.20f, 0.007f, 12.0f,  2.0f,  4, 0.50f, BlockType::Snow,       BlockType::Stone     }, // Tundra
    {0.75f, 0.25f, 0.010f, 20.0f,  5.0f,  4, 0.55f, BlockType::Grass,      BlockType::Dirt      }, // Savanna
    {0.85f, 0.90f, 0.014f, 35.0f, 10.0f,  5, 0.60f, BlockType::Grass,      BlockType::Dirt      }, // Jungle
    {0.50f, 0.50f, 0.005f, 10.0f,-22.0f,  3, 0.40f, BlockType::Sand,       BlockType::Sand      }, // Ocean (deep)
};"""

code = code.replace(biomes_array_old, biomes_array_new)

# 3. Update height calculation in generateChunk
blend_old = """            // Blend surface height from each biome's own noise sample
            float blendH = 0.0f;
            for (int i = 0; i < NUM_BIOMES; i++) {
                float w = weights[i] / wTotal;
                if (w < 0.005f) continue;
                float h = gNoise.octave(wx * BIOMES[i].freq, wz * BIOMES[i].freq,
                                        BIOMES[i].octaves, BIOMES[i].persistence, 2.0f);
                blendH += w * ((float)SEA_LEVEL + h * BIOMES[i].amplitude);
            }"""

blend_new = """            // Blend surface height from each biome's own noise sample
            float blendH = 0.0f;
            for (int i = 0; i < NUM_BIOMES; i++) {
                float w = weights[i] / wTotal;
                if (w < 0.005f) continue;
                float h = gNoise.octave(wx * BIOMES[i].freq, wz * BIOMES[i].freq,
                                        BIOMES[i].octaves, BIOMES[i].persistence, 2.0f);
                blendH += w * ((float)SEA_LEVEL + BIOMES[i].baseOffset + h * BIOMES[i].amplitude);
            }"""

code = code.replace(blend_old, blend_new)

# Make rivers carve deeper valleys
river_old = """            // River carving: zero-crossings of low-freq noise form meandering rivers.
            // Only carve near sea level so mountain ravines are unaffected.
            float riverN = gRiverNoise.octave(wx * 0.005f, wz * 0.005f, 2, 0.5f, 2.0f);
            if (std::abs(riverN) < 0.028f && blendH > SEA_LEVEL - 6 && blendH < SEA_LEVEL + 12) {
                blendH = (float)(SEA_LEVEL - 2);
            }"""

river_new = """            // River carving: deep steep valleys
            float riverN = gRiverNoise.octave(wx * 0.004f, wz * 0.004f, 4, 0.5f, 2.0f);
            float riverDist = std::abs(riverN);
            if (riverDist < 0.04f) {
                // Carve a deep river valley everywhere
                float carve = 1.0f - (riverDist / 0.04f); // 1 at center, 0 at edge
                blendH -= carve * 30.0f; 
                if (blendH < SEA_LEVEL - 4) blendH = SEA_LEVEL - 4; // Flat riverbed
            }"""

code = code.replace(river_old, river_new)

# 4. Remove tree canopies and make them branchy
canopy_regex_tree = r'    // Crown canopy: wide oblate ellipsoid.*?    \}'
code = re.sub(canopy_regex_tree, '', code, flags=re.DOTALL)

canopy_regex_jungle = r'    // Main spherical canopy.*?    \}'
code = re.sub(canopy_regex_jungle, '', code, flags=re.DOTALL)

canopy_regex_acacia = r'    // Three-layer circular disc canopy.*?    \}'
code = re.sub(canopy_regex_acacia, '', code, flags=re.DOTALL)

canopy_regex_pine = r'    // Conical canopy: cone starts.*?    \}'
code = re.sub(canopy_regex_pine, '', code, flags=re.DOTALL)

# Modify tree parameters to be larger and more branchy
code = code.replace('int trunkH   = 14 + (int)(t * 10.0f + s * 6.0f);', 'int trunkH   = 20 + (int)(t * 20.0f + s * 15.0f);')
code = code.replace('const int numBranches = 4 + (int)(t * 6.0f);', 'const int numBranches = 12 + (int)(t * 15.0f);')
code = code.replace('const int blen = 3 + (int)((rng(bi + 200) & 7)     / 7.0f * 4.0f);', 'const int blen = 6 + (int)((rng(bi + 200) & 15) / 15.0f * 10.0f);')
code = code.replace('const int rise = 1 + (int)((rng(bi + 300) & 3)     / 3.0f * 2.0f);', 'const int rise = 2 + (int)((rng(bi + 300) & 7)  / 7.0f * 6.0f);')
# Increase tip leaves for normal tree
code = code.replace('for (int lx = -2; lx <= 2; lx++)', 'for (int lx = -3; lx <= 3; lx++)')
code = code.replace('for (int lz = -2; lz <= 2; lz++)', 'for (int lz = -3; lz <= 3; lz++)')
code = code.replace('for (int ly = -1; ly <= 2; ly++)', 'for (int ly = -2; ly <= 3; ly++)')
code = code.replace('(float)(lx*lx + lz*lz) / 4.0f + (float)(ly*ly) / 4.0f > 1.0f', '(float)(lx*lx + lz*lz) / 9.0f + (float)(ly*ly) / 6.0f > 1.0f')

# Jungle tree branch modifications
code = code.replace('int trunkH = 16 + (int)(t * 12.0f + s * 6.0f);', 'int trunkH = 30 + (int)(t * 25.0f + s * 15.0f);')
code = code.replace('const int numBranches = 3 + (int)(t * 4.0f); // 3–7', 'const int numBranches = 10 + (int)(t * 10.0f);')
# Avoid matching blen twice blindly, jungle tree has slightly different spacing, but we can do a targeted regex if needed.
# Since python string replace replaces all, the first replace already changed it for jungle tree if it matched, but they are slightly different.
code = code.replace('const int blen = 4 + (int)((rng(bi + 200) & 7)     / 7.0f * 4.0f);', 'const int blen = 8 + (int)((rng(bi + 200) & 15) / 15.0f * 12.0f);')
code = code.replace('const int rise = 1 + (int)((rng(bi + 300) & 3)     / 3.0f * 3.0f);', 'const int rise = 3 + (int)((rng(bi + 300) & 7)  / 7.0f * 6.0f);')
# Increase leaf clusters for jungle tree
code = code.replace('for (int lx = -3; lx <= 3; lx++)', 'for (int lx = -4; lx <= 4; lx++)')
code = code.replace('for (int lz = -3; lz <= 3; lz++)', 'for (int lz = -4; lz <= 4; lz++)')
code = code.replace('for (int ly = -1; ly <= 3; ly++)', 'for (int ly = -2; ly <= 4; ly++)')
code = code.replace('(float)(lx*lx + lz*lz) / 9.0f + (float)(ly*ly) / 4.0f > 1.0f', '(float)(lx*lx + lz*lz) / 16.0f + (float)(ly*ly) / 9.0f > 1.0f')


# For Acacia tree, rewrite it to have branches and leaves instead of solid canopy
acacia_new = """    // Branchy Acacia
    int numBranches = 6 + (int)(t * 5.0f);
    for (int bi = 0; bi < numBranches; bi++) {
        int ty = top + trunkH / 2 + (int)((rng(bi) & 0xFF) / 255.0f * trunkH / 2.0f);
        float angle = (rng(bi+1) & 0xFF) / 255.0f * 6.283f;
        int blen = 5 + (int)((rng(bi+2) & 7) / 7.0f * 6.0f);
        int rise = 1 + (int)((rng(bi+3) & 3) / 3.0f * 3.0f);
        
        float dx = cos(angle);
        float dz = sin(angle);
        
        for (int i = 1; i <= blen; i++) {
            int bx = x + (int)(dx * i);
            int bz = z + (int)(dz * i);
            int by = ty + (rise * i) / blen;
            if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE || by >= CHUNK_HEIGHT) break;
            c->set(bx, by, bz, BlockType::Wood);
            
            if (i == blen) {
                // Flat leaf cluster at tip
                for (int lx=-3; lx<=3; lx++) for (int lz=-3; lz<=3; lz++) for(int ly=-1; ly<=1; ly++) {
                    if (lx*lx + lz*lz > 9) continue;
                    int nX = bx+lx, nY = by+ly, nZ = bz+lz;
                    if (nX>=0 && nX<CHUNK_SIZE && nZ>=0 && nZ<CHUNK_SIZE && nY>0 && nY<CHUNK_HEIGHT)
                        if (c->get(nX, nY, nZ) == BlockType::Air) c->set(nX, nY, nZ, BlockType::Leaves);
                }
            }
        }
    }"""
# We must insert this where the canopy was for acacia. 
code = code.replace('// Three-layer circular disc canopy — each layer narrower toward the top', acacia_new)

# For Pine tree, add random branches to replace the solid cone
pine_new = """    // Branchy Pine
    int numBranches = 15 + (int)(t * 10.0f);
    for (int bi = 0; bi < numBranches; bi++) {
        int ty = top + trunkH / 4 + (int)((rng(bi) & 0xFF) / 255.0f * trunkH * 0.75f);
        float angle = (rng(bi+1) & 0xFF) / 255.0f * 6.283f;
        // branches get shorter at top
        float heightFactor = 1.0f - (float)(ty - (top + trunkH / 4)) / (trunkH * 0.75f);
        int blen = 2 + (int)((rng(bi+2) & 7) / 7.0f * 5.0f * heightFactor);
        int rise = (int)((rng(bi+3) & 3) / 3.0f * 2.0f); // mostly flat or slightly up
        
        float dx = cos(angle);
        float dz = sin(angle);
        
        for (int i = 1; i <= blen; i++) {
            int bx = x + (int)(dx * i);
            int bz = z + (int)(dz * i);
            int by = ty + (rise * i) / blen;
            if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE || by >= CHUNK_HEIGHT) break;
            c->set(bx, by, bz, BlockType::Wood);
            
            if (i == blen) {
                // leaf cluster at tip
                for (int lx=-2; lx<=2; lx++) for (int lz=-2; lz<=2; lz++) for(int ly=-1; ly<=1; ly++) {
                    if (lx*lx + lz*lz > 4) continue;
                    int nX = bx+lx, nY = by+ly, nZ = bz+lz;
                    if (nX>=0 && nX<CHUNK_SIZE && nZ>=0 && nZ<CHUNK_SIZE && nY>0 && nY<CHUNK_HEIGHT)
                        if (c->get(nX, nY, nZ) == BlockType::Air) c->set(nX, nY, nZ, BlockType::Leaves);
                }
            }
        }
    }
    // Top spike leaves
    for (int ly=0; ly<=3; ly++) {
        for (int lx=-1; lx<=1; lx++) for (int lz=-1; lz<=1; lz++) {
            if (lx*lx+lz*lz > (3-ly)) continue;
            int nX = x+lx, nY = top+trunkH+ly, nZ = z+lz;
            if (nX>=0 && nX<CHUNK_SIZE && nZ>=0 && nZ<CHUNK_SIZE && nY>0 && nY<CHUNK_HEIGHT)
                if (c->get(nX, nY, nZ) == BlockType::Air) c->set(nX, nY, nZ, BlockType::Leaves);
        }
    }
    """
# The conical canopy block was removed via regex. I need to replace `// Whorl branches: two opposing arms at each tier inside the cone` to `pine_new`
code = code.replace('// Whorl branches: two opposing arms at each tier inside the cone', pine_new)
pine_regex = r'    static const int8_t CARD\[4\]\[2\] = .*?    \}\n    \}\n'
code = re.sub(pine_regex, '', code, flags=re.DOTALL)


# Ocean decorators logic inside pass 3:
code = code.replace('case Biome::Ocean:\n                    break;', '') # in case it exists
ocean_case = """                case Biome::Ocean:
                    // Under water decor like sand mounds, etc.
                    break;"""
# I need to add it to the switch(dominant[x][z]) statement
code = code.replace('case Biome::Jungle:', 'case Biome::Ocean:\n                    break;\n                case Biome::Jungle:')

with open('src/world.cpp', 'w') as f:
    f.write(code)

print("Patching world.cpp completed")
