CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Iinclude -Isrc -Ithird_party/imgui -MMD -MP
LIBS     := -lGL -lglfw -lm -lpthread -ldl

IMGUI_DIR := third_party/imgui
IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_impl_glfw.cpp $(IMGUI_DIR)/imgui_impl_opengl3.cpp
IMGUI_OBJS := $(IMGUI_SRCS:$(IMGUI_DIR)/%.cpp=build/imgui/%.o)

SRCS := src/main.cpp src/shader.cpp src/camera.cpp src/world.cpp src/gl_loader.cpp src/atlas.cpp src/network.cpp src/voxel_model.cpp
OBJS := $(SRCS:src/%.cpp=build/%.o)

DEPS := $(OBJS:.o=.d) $(IMGUI_OBJS:.o=.d)
-include $(DEPS)

TARGET := terrax

$(shell mkdir -p build/imgui)

.PHONY: all build run clean

all: $(TARGET)

build: $(TARGET)

run: $(TARGET)
	./$(TARGET)

$(TARGET): $(OBJS) $(IMGUI_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/imgui/%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf build $(TARGET)
