CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Iinclude -Isrc -Ithird_party/imgui -Ithird_party/imgui/backends -MMD -MP
LIBS     := -lGL -lglfw -lm -lpthread -ldl

IMGUI_DIR := third_party/imgui
IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp
IMGUI_BACKEND_SRCS := $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS := $(IMGUI_SRCS:$(IMGUI_DIR)/%.cpp=build/imgui/%.o)
IMGUI_BACKEND_OBJS := $(IMGUI_BACKEND_SRCS:$(IMGUI_DIR)/backends/%.cpp=build/imgui/%.o)

SRCS := src/main.cpp src/shader.cpp src/camera.cpp src/world.cpp src/town.cpp \
        src/gl_loader.cpp src/atlas.cpp src/network.cpp src/voxel_model.cpp \
        src/game_session.cpp src/app_context.cpp src/physics.cpp src/input.cpp \
        src/renderer.cpp src/gameplay.cpp src/ui.cpp src/graphics_settings.cpp \
        src/object_manager.cpp src/player_object.cpp src/prop.cpp \
        src/furniture.cpp src/decorations.cpp src/prop_placement.cpp \
        src/vehicles.cpp src/ferry_routes.cpp src/npc.cpp src/animal.cpp \
        src/vegetation.cpp src/building.cpp src/items.cpp src/inventory.cpp \
        src/clothing_painter.cpp src/weapon_builder.cpp src/item_generator.cpp \
        src/inventory_ui.cpp src/loot_drop.cpp src/projectile.cpp \
        src/npc_appearance.cpp
OBJS := $(SRCS:src/%.cpp=build/%.o)

DEPS := $(OBJS:.o=.d) $(IMGUI_OBJS:.o=.d) $(IMGUI_BACKEND_OBJS:.o=.d)
-include $(DEPS)

TARGET := terrax

# --- Test build (headless, no GL/GLFW) ---
TEST_TARGET  := terrax_tests
TEST_SRCS    := src/voxel_model.cpp src/building.cpp tests/gl_stub.cpp tests/test_main.cpp tests/test_voxel_model.cpp
TEST_FLAGS   := -std=c++17 -O0 -g -Wall -Iinclude -Isrc -Itests -DTERRAX_TESTING

$(shell mkdir -p build/imgui)

.PHONY: all build run clean test

all: $(TARGET)

build: $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	$(CXX) $(TEST_FLAGS) -o $@ $^ -lm

$(TARGET): $(OBJS) $(IMGUI_OBJS) $(IMGUI_BACKEND_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/imgui/%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/imgui/%.o: $(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf build $(TARGET) $(TEST_TARGET)
