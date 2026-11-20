cmake_minimum_required(VERSION 3.30)
project(shooter LANGUAGES CXX)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
)
FetchContent_MakeAvailable(raylib)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
)
FetchContent_MakeAvailable(nlohmann_json)

set(SHOOTER_SOURCES
    main.cpp
    game.cpp
    camera_system.cpp
    ship_physics.cpp
    world_physics.cpp
    bulkhead.cpp
    assets.cpp
    data.cpp
    background.cpp
    particles.cpp
    weapons.cpp
    enemies.cpp
    player.cpp
    audio.cpp
    cutscene.cpp
    profile.cpp
    render3d.cpp
    vfx.cpp
    planet_terrain.cpp
    planet_render.cpp
    title.cpp
    menus.cpp
    ui_theme.cpp
    moon_level.cpp
    trench_run.cpp
    flight_audio.cpp
)

add_executable(shooter ${SHOOTER_SOURCES})
target_compile_features(shooter PRIVATE cxx_std_17)
target_compile_options(shooter PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4 /O2>)
target_link_libraries(shooter PRIVATE raylib nlohmann_json::nlohmann_json)

# The executable must be runnable directly from its output directory.
add_custom_command(TARGET shooter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:shooter>/assets"
)
