CXX      := g++

BUILD ?= release
ifeq ($(BUILD),debug)
  OPT_FLAGS := -O0 -g -DDEBUG
else
  OPT_FLAGS := -O2 -DNDEBUG
endif

CXXFLAGS := -std=c++17 $(OPT_FLAGS) -Wall -Iinclude -Isrc -Ithird_party/imgui -Ithird_party/imgui/backends -Ithird_party/miniaudio -MMD -MP
LIBS     := -lGL -lglfw -lm -lpthread -ldl

IMGUI_DIR := third_party/imgui
IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp
IMGUI_BACKEND_SRCS := $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS := $(IMGUI_SRCS:$(IMGUI_DIR)/%.cpp=build/imgui/%.o)
IMGUI_BACKEND_OBJS := $(IMGUI_BACKEND_SRCS:$(IMGUI_DIR)/backends/%.cpp=build/imgui/%.o)

# miniaudio: a single vendored header compiled in one TU (header-only library).
MINIAUDIO_DIR  := third_party/miniaudio
MINIAUDIO_OBJS := build/miniaudio/miniaudio_impl.o

SRCS := src/main.cpp src/shader.cpp src/camera.cpp src/world.cpp src/world_gen.cpp \
        src/town.cpp \
        src/town_stamp.cpp src/town_roads.cpp src/town_layout.cpp \
        src/town_buildings.cpp src/town_terrain.cpp src/dungeon.cpp src/castle.cpp \
        src/gl_loader.cpp src/atlas.cpp src/network.cpp src/voxel_model.cpp \
        src/voxel_rig.cpp src/voxel_house.cpp \
        src/game_session.cpp src/app_context.cpp src/physics.cpp src/input.cpp \
        src/renderer.cpp src/gameplay.cpp src/gameplay_entities.cpp \
        src/gameplay_effects.cpp src/gameplay_spells.cpp \
        src/ui.cpp src/ui_menus.cpp \
        src/ui_editors.cpp src/ui_map.cpp src/ui_play.cpp src/ui_skilltree.cpp src/graphics_settings.cpp \
        src/object_manager.cpp src/player_object.cpp src/prop.cpp src/prop_registry.cpp \
        src/furniture.cpp src/decorations.cpp src/prop_placement.cpp \
        src/vehicles.cpp src/ferry_routes.cpp src/npc.cpp src/animal.cpp \
        src/vegetation.cpp src/building.cpp src/building_house.cpp \
        src/building_special.cpp src/building_farm.cpp src/items.cpp src/inventory.cpp \
        src/role.cpp src/ability.cpp src/skill_tree.cpp src/character_save.cpp \
        src/clothing_painter.cpp src/weapon_builder.cpp src/item_generator.cpp \
        src/inventory_ui.cpp src/loot_drop.cpp src/projectile.cpp \
        src/npc_appearance.cpp src/farm_director.cpp src/audio.cpp src/screenshot.cpp \
        src/quest.cpp
OBJS := $(SRCS:src/%.cpp=build/%.o)

DEPS := $(OBJS:.o=.d) $(IMGUI_OBJS:.o=.d) $(IMGUI_BACKEND_OBJS:.o=.d) $(MINIAUDIO_OBJS:.o=.d)
-include $(DEPS)

TARGET := terrax

# --- Test build (headless, no GL/GLFW) ---
# Engine code under test links against the GL stub (tests/gl_stub.cpp) so it
# runs without a GPU/context. Only files whose dependency closure stays clear
# of GLFW / Boost.Asio / AppContext belong here.
TEST_TARGET  := terrax_tests
TEST_ENGINE_SRCS := src/voxel_model.cpp src/voxel_rig.cpp src/voxel_house.cpp \
                    src/building.cpp src/building_house.cpp \
                    src/building_special.cpp src/building_farm.cpp src/world.cpp src/world_gen.cpp \
                    src/town.cpp \
                    src/town_stamp.cpp src/town_roads.cpp src/town_layout.cpp \
                    src/town_buildings.cpp src/town_terrain.cpp src/dungeon.cpp src/castle.cpp \
                    src/vegetation.cpp src/atlas.cpp src/camera.cpp src/physics.cpp \
                    src/quest.cpp
TEST_CASE_SRCS   := tests/test_main.cpp tests/test_voxel_model.cpp tests/test_noise.cpp \
                    tests/test_camera.cpp tests/test_world.cpp tests/test_physics.cpp \
                    tests/test_building.cpp tests/test_atlas.cpp tests/test_dungeon.cpp \
                    tests/test_quest.cpp
TEST_SRCS    := $(TEST_ENGINE_SRCS) tests/gl_stub.cpp $(TEST_CASE_SRCS)
# -pthread: World spawns std::thread chunk workers, so the test binary must
# link the pthread runtime on Linux (harmless elsewhere).
TEST_FLAGS   := -std=c++17 -O0 -g -Wall -pthread -Iinclude -Isrc -Itests -DTERRAX_TESTING

# --- Asset catalog tool (headless, no GL/GLFW) ---
# Links the prop builders + voxel model against the GL stub and emits
# docs/ASSET_CATALOG.md — a reference of every prop's dimensions + metadata.
CATALOG_TARGET := asset_catalog
# Reuse the proven headless engine set (TEST_ENGINE_SRCS provides voxel_model.cpp,
# dungeon/castle/world/town/building generation) and add the prop/item builders.
CATALOG_SRCS   := tools/asset_catalog.cpp src/prop_registry.cpp \
                  src/furniture.cpp src/decorations.cpp \
                  src/weapon_builder.cpp src/item_generator.cpp \
                  $(TEST_ENGINE_SRCS) tests/gl_stub.cpp
# item_generator.cpp pulls in generators that reference the heavy items.cpp
# closure; --gc-sections drops those unused sections so the tool stays headless.
CATALOG_FLAGS  := -std=c++17 -O0 -g -Wall -pthread -Iinclude -Isrc -DTERRAX_TESTING \
                  -ffunction-sections -fdata-sections -Wl,--gc-sections

$(shell mkdir -p build/imgui build/miniaudio)

.PHONY: all build run clean test catalog

all: $(TARGET)

build: $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

catalog: $(CATALOG_SRCS)
	$(CXX) $(CATALOG_FLAGS) -o $(CATALOG_TARGET) $^ -lm
	./$(CATALOG_TARGET) docs/ASSET_CATALOG.md

$(TEST_TARGET): $(TEST_SRCS)
	$(CXX) $(TEST_FLAGS) -o $@ $^ -lm

$(TARGET): $(OBJS) $(IMGUI_OBJS) $(IMGUI_BACKEND_OBJS) $(MINIAUDIO_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/miniaudio/%.o: $(MINIAUDIO_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -w -c -o $@ $<

build/imgui/%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/imgui/%.o: $(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf build $(TARGET) $(TEST_TARGET)
