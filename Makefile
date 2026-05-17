CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Iinclude -Isrc
LIBS     := -lGL -lglfw -lm

SRCS := src/main.cpp src/shader.cpp src/camera.cpp src/world.cpp src/gl_loader.cpp src/atlas.cpp
OBJS := $(SRCS:src/%.cpp=build/%.o)
TARGET := terrax

$(shell mkdir -p build)

.PHONY: all build run clean

all: $(TARGET)

build: $(TARGET)

run: $(TARGET)
	./$(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf build $(TARGET)
