CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -MMD -MP
LDLIBS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
SRC = main.cpp game.cpp camera_system.cpp ship_physics.cpp world_physics.cpp bulkhead.cpp assets.cpp data.cpp background.cpp particles.cpp weapons.cpp enemies.cpp player.cpp audio.cpp cutscene.cpp profile.cpp render3d.cpp vfx.cpp planet_terrain.cpp planet_render.cpp title.cpp menus.cpp ui_theme.cpp moon_level.cpp flight_audio.cpp
OBJ = $(SRC:%.cpp=build/%.o)

# WSL cross-build configuration for the DOOGEE U11's ARM64 Ubuntu runtime.
# The raylib archive and headers were exported from the tablet so that the
# desktop build uses the same raylib 5.5 implementation as the game runtime.
ARM64_CXX ?= aarch64-linux-gnu-g++
ARM64_RAYLIB_ROOT ?= tools/arm64-raylib
ARM64_JSON_ROOT ?= tools/arm64-json
ARM64_GL_ROOT ?= tools/arm64-gl
ARM64_BUILD = build-arm64
ARM64_OBJ = $(SRC:%.cpp=$(ARM64_BUILD)/%.o)
ARM64_CXXFLAGS = $(CXXFLAGS) -I$(ARM64_RAYLIB_ROOT)/include -I$(ARM64_JSON_ROOT) -I$(ARM64_GL_ROOT)
ARM64_LDLIBS = -L$(ARM64_RAYLIB_ROOT)/lib -Wl,-rpath-link,$(ARM64_RAYLIB_ROOT)/lib -Wl,--allow-shlib-undefined -lraylib -lGL -lGLX -lGLdispatch -lm -lpthread -ldl -lrt -lX11 -lxcb -lXau -lXdmcp

shooter: $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDLIBS)

shooter-arm64: $(ARM64_OBJ)
	$(ARM64_CXX) -o $@ $(ARM64_OBJ) $(ARM64_LDLIBS)

build/%.o: %.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ARM64_BUILD)/%.o: %.cpp | $(ARM64_BUILD)
	$(ARM64_CXX) $(ARM64_CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

$(ARM64_BUILD):
	mkdir -p $(ARM64_BUILD)

clean:
	rm -rf build $(ARM64_BUILD) shooter shooter-arm64

.PHONY: clean shooter-arm64
-include $(OBJ:.o=.d)
-include $(ARM64_OBJ:.o=.d)
