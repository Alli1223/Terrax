#pragma once
#include "dungeon.h"

class Chunk;

// An overground multi-storey castle — a concrete Dungeon (the same polymorphic
// rule as CryptDungeon / CaveDungeon / RuinsDungeon). Castles are far more
// elaborate than the carved dungeons (graded foundations, a gatehouse + arch,
// round corner towers, partitioned interiors, a grand stair), so their geometry
// lives in its own translation unit (castle.cpp).
class CastleDungeon : public Dungeon {
public:
    DungeonKind kind() const override { return DungeonKind::Castle; }
    void generateLayout(uint32_t seed, glm::ivec2 anchorXZ, int surfaceY) override;

    BlockType wallBlock()  const override { return BlockType::Stone; }
    BlockType floorBlock() const override { return BlockType::Stone; }

    void fillSpawnTable(std::vector<DungeonSpawn>& out, uint32_t seed) const override;

    // Keep half-extent for a size tier — shared by the layout and the stamp so
    // they agree on the footprint (the keep walls sit at anchor +- this).
    static int keepHalf(int sizeTier);
};

// Stamp the castle `d` into chunk `c`. Called from stampDungeonChunk() for any
// overground dungeon. (`d` is a CastleDungeon; taken as Dungeon& so dungeon.cpp
// needn't know the concrete type.)
void stampCastleChunk(Chunk* c, const Dungeon& d);
