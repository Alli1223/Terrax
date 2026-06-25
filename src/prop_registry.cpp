#include "prop_registry.h"
#include "prop_builders.h"
#include <array>
#include <cstdio>

// The single prop table. Each row pairs a PropType with its mesh builder and
// all of its metadata (interaction, light, wind). buildAll(), getInteraction()
// and the renderer's light/wind passes read this — adding a prop means a new
// PropType id, a builder, and one row here. Order is for readability only;
// lookups go through the id index built in propDef().

namespace {

// Reusable metadata shorthands.
const PropInteractDef SIT { InteractAction::SitChair, 0.66f, "Press E to sit" };
const PropInteractDef LIE { InteractAction::LieBed,   0.45f, "Press E to lie down" };

// Light profiles (base intensity/radius; the renderer applies flicker + night).
const PropLightDef HEARTH        { true, 0.70f, 0.70f, 15.0f, glm::vec3(1.00f, 0.52f, 0.22f), false };
const PropLightDef LANTERN_NIGHT { true, 0.45f, 0.55f, 25.0f, glm::vec3(1.00f, 0.70f, 0.36f), true  };
const PropLightDef LAMP_NIGHT    { true, 3.15f, 0.58f, 30.0f, glm::vec3(1.00f, 0.70f, 0.36f), true  };

}  // namespace

const std::vector<PropDef>& propRegistry() {
    static const std::vector<PropDef> reg = {
        // --- Living-room / generic furniture ---
        { PropType::Bookshelf,      "Bookshelf",         buildBookshelf },
        { PropType::Bed,            "Bed",               buildBed,        LIE },
        { PropType::Lantern,        "Lantern",           buildLanternProp, {}, LANTERN_NIGHT },
        { PropType::Cooker,         "Cooker",            buildCooker },
        { PropType::Table,          "Table",             buildTable },
        { PropType::Chair,          "Chair",             buildChair,      SIT },
        { PropType::Crockery,       "Crockery",          buildCrockery },
        // --- Decorations ---
        { PropType::StreetLamp,     "Street Lamp",       buildStreetLamp, {}, LAMP_NIGHT },
        { PropType::PottedPlant,    "Potted Plant",      buildPottedPlant },
        { PropType::Bush,           "Bush",              buildBush,       {}, {}, true },
        { PropType::Bench,          "Bench",             buildBench },
        { PropType::Fence,          "Fence",             buildFenceSection },
        // --- Room-specific furniture ---
        { PropType::Sink,           "Sink",              buildSink },
        { PropType::KitchenCounter, "Kitchen Counter",   buildKitchenCounter },
        { PropType::Wardrobe,       "Wardrobe",          buildWardrobe },
        { PropType::Desk,           "Desk",              buildDesk },
        { PropType::Couch,          "Couch",             buildCouch },
        { PropType::SideTable,      "Side Table",        buildSideTable },
        // --- Special-building furniture ---
        { PropType::Anvil,          "Anvil",             buildAnvil },
        { PropType::Forge,          "Forge",             buildForge },
        { PropType::BarCounter,     "Bar Counter",       buildBarCounter },
        { PropType::BarStool,       "Bar Stool",         buildBarStool,   SIT },
        { PropType::Cauldron,       "Cauldron",          buildCauldron },
        { PropType::AlchemyTable,   "Alchemy Table",     buildAlchemyTable },
        // --- Trade signs ---
        { PropType::SignAnvil,      "Sign: Anvil",       buildTradeSignAnvil },
        { PropType::SignMug,        "Sign: Mug",         buildTradeSignMug },
        { PropType::SignStar,       "Sign: Star",        buildTradeSignStar },
        { PropType::SignWheat,      "Sign: Wheat",       buildTradeSignWheat },
        // --- Cosy home furnishings ---
        { PropType::Fireplace,      "Fireplace",         buildFireplace,  {}, HEARTH },
        { PropType::Rug,            "Rug",               buildRug },
        { PropType::WallPainting,   "Wall Painting",     buildWallPainting },
        { PropType::FlowerVase,     "Flower Vase",       buildFlowerVase },
        // --- Town atmosphere ---
        { PropType::FlowerPot,      "Flower Pot",        buildFlowerPot },
        { PropType::FlowerBed,      "Flower Bed",        buildFlowerBed },
        { PropType::Barrel,         "Barrel",            buildBarrel },
        { PropType::BuntingSpan,    "Bunting",           buildBuntingSpan },
        // --- Town centre detail ---
        { PropType::Crate,          "Crate",             buildCrate },
        { PropType::ProducePile,    "Produce Pile",      buildProducePile },
        { PropType::Fountain,       "Fountain",          buildFountain },
        { PropType::MarketStall,    "Market Stall",      buildMarketStall },
        { PropType::NoticeBoard,    "Notice Board",      buildNoticeBoard },
        // --- Wild bushes (sway in the wind) ---
        { PropType::BushFlowering,  "Bush (Flowering)",  buildBushFlowering, {}, {}, true },
        { PropType::BushBerry,      "Bush (Berry)",      buildBushBerry,     {}, {}, true },
        { PropType::BushConifer,    "Bush (Conifer)",    buildBushConifer,   {}, {}, true },
        { PropType::BushDry,        "Bush (Dry)",        buildBushDry,       {}, {}, true },
        // --- Graveyard headstones ---
        { PropType::Tombstone,      "Tombstone",         buildTombstone },
        { PropType::TombstoneCross, "Tombstone (Cross)", buildTombstoneCross },
        { PropType::GraveCross,     "Grave Cross",       buildGraveCross },
        // --- Graveyard dressing ---
        { PropType::SoilMound,      "Soil Mound",        buildSoilMound },
        { PropType::Spade,          "Spade",             buildSpade },
        { PropType::GraveFlowers,   "Grave Flowers",     buildGraveFlowers },
        { PropType::FlowerWreath,   "Flower Wreath",     buildFlowerWreath },
        { PropType::StoneUrn,       "Stone Urn",         buildStoneUrn },
        { PropType::DeadTree,       "Dead Tree",         buildDeadTree },
        // More home furnishings
        { PropType::Chest,          "Chest",             buildChest },
        { PropType::Stool,          "Stool",             buildStool,      SIT },
        { PropType::Candelabra,     "Candelabra",        buildCandelabra, {}, LANTERN_NIGHT },
        { PropType::Bookpile,       "Book Pile",         buildBookpile },
        { PropType::WallShelf,      "Wall Shelf",        buildWallShelf },
        { PropType::WallClock,      "Wall Clock",        buildWallClock },
        // --- Dungeon dressing ---
        { PropType::BonePile,       "Bone Pile",         buildBonePile },
        { PropType::TreasurePile,   "Treasure Pile",     buildTreasurePile },
    };
    return reg;
}

const PropDef* propDef(PropType t) {
    static const std::array<const PropDef*, (int)PropType::Count> idx = [] {
        std::array<const PropDef*, (int)PropType::Count> a{};
        for (const PropDef& d : propRegistry())
            if ((int)d.type >= 0 && (int)d.type < (int)PropType::Count) a[(int)d.type] = &d;
        // Maintainability guard: flag any PropType that was added to the enum
        // but never given a registry row (it would otherwise silently render
        // nothing). Cheap — runs once, on first lookup.
        for (int i = 0; i < (int)PropType::Count; i++)
            if (!a[i]) std::fprintf(stderr, "[Props] PropType %d has no registry entry\n", i);
        return a;
    }();
    int i = (int)t;
    return (i >= 0 && i < (int)PropType::Count) ? idx[i] : nullptr;
}
