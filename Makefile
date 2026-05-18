CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Iinclude -Isrc -Ithird_party/imgui -MMD -MP
LIBS     := -lGL -lglfw -lm -lpthread -ldl

IMGUI_DIR := third_party/imgui
IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_impl_glfw.cpp $(IMGUI_DIR)/imgui_impl_opengl3.cpp
IMGUI_OBJS := $(IMGUI_SRCS:$(IMGUI_DIR)/%.cpp=build/imgui/%.o)

SRCS := src/main.cpp src/shader.cpp src/camera.cpp src/world.cpp src/gl_loader.cpp \
        src/atlas.cpp src/network.cpp src/voxel_model.cpp src/game_session.cpp \
        src/app_context.cpp src/physics.cpp src/input.cpp src/renderer.cpp \
        src/gameplay.cpp src/ui.cpp
OBJS := $(SRCS:src/%.cpp=build/%.o)

DEPS := $(OBJS:.o=.d) $(IMGUI_OBJS:.o=.d)
-include $(DEPS)

TARGET := terrax

# --- Test build (headless, no GL/GLFW) ---
TEST_TARGET  := terrax_tests
TEST_SRCS    := src/voxel_model.cpp tests/gl_stub.cpp tests/test_main.cpp tests/test_voxel_model.cpp
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

$(TARGET): $(OBJS) $(IMGUI_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/imgui/%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf build $(TARGET) $(TEST_TARGET)
