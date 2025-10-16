cmake_minimum_required(VERSION 3.16)
project(VIBBLEEngine)

# ------------------------------------------
# Build settings focused on compile speed
# ------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release if not set (for single-config generators)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compiler cache (ccache/sccache) if present
find_program(CCACHE_PROGRAM ccache)
find_program(SCCACHE_PROGRAM sccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
elseif(SCCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
endif()

# Speed-oriented flags
if(MSVC)
    add_compile_options(/MP /Zc:inline /Zc:__cplusplus /permissive- /FS /bigobj)
else()
    add_compile_options(-pipe)
endif()

# Output to ENGINE/ for all configs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/ENGINE)
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" CFG_UP)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UP} ${CMAKE_SOURCE_DIR}/ENGINE)
endforeach()

# ------------------------------------------
# Sources
# ------------------------------------------
file(GLOB_RECURSE ENGINE_SRC CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/ENGINE/*.cpp
    ${CMAKE_SOURCE_DIR}/ENGINE/*.c
    ${CMAKE_SOURCE_DIR}/ENGINE/*.hpp
    ${CMAKE_SOURCE_DIR}/ENGINE/*.h
)

add_executable(engine ${ENGINE_SRC})

# Unity build (jumbo) for faster compiles
option(ENABLE_UNITY_BUILD "Enable unity/jumbo build for faster compiles" ON)
if(ENABLE_UNITY_BUILD)
    set_target_properties(engine PROPERTIES UNITY_BUILD ON)
    set_target_properties(engine PROPERTIES UNITY_BUILD_BATCH_SIZE 16)
endif()

# Precompiled header
option(ENABLE_PCH "Enable precompiled headers" ON)
if(ENABLE_PCH AND EXISTS "${CMAKE_SOURCE_DIR}/ENGINE/pch.hpp")
    target_precompile_headers(engine PRIVATE ${CMAKE_SOURCE_DIR}/ENGINE/pch.hpp)
endif()

# ------------------------------------------
# Dependencies (prefer vcpkg; with glad fallback)
# ------------------------------------------
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    set(_NLOHMANN_JSON_DIR "${CMAKE_SOURCE_DIR}/external/nlohmann_json")
    if(EXISTS "${_NLOHMANN_JSON_DIR}/include/nlohmann/json.hpp")
        add_library(nlohmann_json INTERFACE)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
        target_include_directories(nlohmann_json INTERFACE "${_NLOHMANN_JSON_DIR}/include")
        message(STATUS "Using bundled nlohmann_json from ${_NLOHMANN_JSON_DIR}")
    else()
        message(FATAL_ERROR "nlohmann_json not found. Install the package or add it to ${_NLOHMANN_JSON_DIR}")
    endif()
endif()
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)

# Try glad via package config first (vcpkg provides this)
find_package(glad CONFIG QUIET)
if(glad_FOUND)
    set(GLAD_TARGET glad::glad)
else()
    message(WARNING "glad CONFIG package not found. Trying local source fallback at third_party/glad ...")
    set(GLAD_SRC_DIR "${CMAKE_SOURCE_DIR}/third_party/glad")
    if(EXISTS "${GLAD_SRC_DIR}/src/glad.c")
        add_library(glad STATIC "${GLAD_SRC_DIR}/src/glad.c")
        target_include_directories(glad PUBLIC "${GLAD_SRC_DIR}/include")
        set(GLAD_TARGET glad)
        message(STATUS "Using local glad from ${GLAD_SRC_DIR}")
    elseif(EXISTS "${GLAD_SRC_DIR}/CMakeLists.txt")
        add_subdirectory("${GLAD_SRC_DIR}" EXCLUDE_FROM_ALL)
        if(TARGET glad::glad)
            set(GLAD_TARGET glad::glad)
        elseif(TARGET glad)
            set(GLAD_TARGET glad)
        else()
            message(FATAL_ERROR "Unable to determine glad target from third_party/glad")
        endif()
        message(STATUS "Using third_party glad subdirectory")
    else()
        message(WARNING
            "glad not found; continuing without glad.\n"
            "Fix options (optional):\n"
            "  1) Install via vcpkg and pass the vcpkg toolchain to CMake (recommended):\n"
            "       vcpkg install glad:x64-windows\n"
            "       -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake\n"
            "  2) Add glad sources to third_party/glad (with include/ and src/glad.c).")
        set(GLAD_TARGET "")
    endif()
endif()

# OpenGL (needed by glad-backed GL apps)
find_package(OpenGL REQUIRED)

# ------------------------------------------
# Include dirs
# ------------------------------------------
target_include_directories(engine
    PRIVATE
        ${CMAKE_SOURCE_DIR}/ENGINE
        ${CMAKE_SOURCE_DIR}/ENGINE/asset
        ${CMAKE_SOURCE_DIR}/ENGINE/core
        ${CMAKE_SOURCE_DIR}/ENGINE/dev_mode
        ${CMAKE_SOURCE_DIR}/ENGINE/render
        ${CMAKE_SOURCE_DIR}/ENGINE/render_pipeline
        ${CMAKE_SOURCE_DIR}/ENGINE/map_generation
        ${CMAKE_SOURCE_DIR}/ENGINE/spawn
        ${CMAKE_SOURCE_DIR}/ENGINE/ui
        ${CMAKE_SOURCE_DIR}/ENGINE/utils
)

target_compile_definitions(engine PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# ------------------------------------------
# Link libraries
# ------------------------------------------
# SDL2main is not present on all platforms; link it only if the target exists.
set(_SDL2MAIN_TARGET "")
if(TARGET SDL2::SDL2main)
    set(_SDL2MAIN_TARGET SDL2::SDL2main)
endif()

target_link_libraries(engine
    PRIVATE
        nlohmann_json::nlohmann_json
        SDL2::SDL2 ${_SDL2MAIN_TARGET}
        SDL2_image::SDL2_image
        SDL2_mixer::SDL2_mixer
        SDL2_ttf::SDL2_ttf
        ${GLAD_TARGET}
        OpenGL::GL
)

# Faster relinks on MSVC Debug
if(MSVC)
    target_link_options(engine PRIVATE "$<$<CONFIG:Debug>:/INCREMENTAL>")
endif()

enable_testing()

add_executable(dev_mode_ui_tests
    tests/dev_mode/full_screen_collapsible_tests.cpp
    tests/dev_mode/tag_cache_tests.cpp
    tests/dev_mode/asset_info_stub.cpp
    ENGINE/dev_mode/full_screen_collapsible.cpp
    ENGINE/dev_mode/draw_utils.cpp
    ENGINE/dev_mode/font_cache.cpp
    ENGINE/dev_mode/widgets.cpp
    ENGINE/dev_mode/dm_styles.cpp
    ENGINE/utils/input.cpp
)
target_include_directories(dev_mode_ui_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/dev_mode
    ${CMAKE_SOURCE_DIR}/ENGINE/utils
)
target_link_libraries(dev_mode_ui_tests PRIVATE
    nlohmann_json::nlohmann_json
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_mixer::SDL2_mixer
    SDL2_ttf::SDL2_ttf
)
target_compile_definitions(dev_mode_ui_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME dev_mode_ui_tests COMMAND dev_mode_ui_tests)

add_executable(map_grid_tests
    tests/map_grid_tests.cpp
    ENGINE/utils/map_grid.cpp
    ENGINE/utils/area.cpp
)
target_include_directories(map_grid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/utils
)
target_link_libraries(map_grid_tests PRIVATE
    SDL2::SDL2
)
target_compile_definitions(map_grid_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME map_grid_tests COMMAND map_grid_tests)

add_executable(asset_loader_distance_tests
    tests/core/asset_loader_distance_tests.cpp
    ENGINE/utils/area.cpp
    ENGINE/core/asset_loader_internal.cpp
)
target_include_directories(asset_loader_distance_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/core
    ${CMAKE_SOURCE_DIR}/ENGINE/utils
)
target_link_libraries(asset_loader_distance_tests PRIVATE
    nlohmann_json::nlohmann_json
    SDL2::SDL2
)
target_compile_definitions(asset_loader_distance_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME asset_loader_distance_tests COMMAND asset_loader_distance_tests)

add_executable(manifest_loader_tests
    tests/manifest/manifest_loader_tests.cpp
    ENGINE/core/manifest/manifest_loader.cpp
)
target_include_directories(manifest_loader_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/core
)
target_link_libraries(manifest_loader_tests PRIVATE
    nlohmann_json::nlohmann_json
)
target_compile_definitions(manifest_loader_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME manifest_loader_tests COMMAND manifest_loader_tests)

add_executable(manifest_store_tests
    tests/manifest/manifest_store_tests.cpp
    tests/manifest/spawn_group_manifest_tests.cpp
    ENGINE/dev_mode/core/manifest_store.cpp
    ENGINE/dev_mode/core/dev_json_store.cpp
    ENGINE/core/manifest/manifest_loader.cpp
)
target_include_directories(manifest_store_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/core
    ${CMAKE_SOURCE_DIR}/ENGINE/dev_mode
)
target_link_libraries(manifest_store_tests PRIVATE
    nlohmann_json::nlohmann_json
    SDL2::SDL2
)
target_compile_definitions(manifest_store_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME manifest_store_tests COMMAND manifest_store_tests)

add_executable(asset_info_manifest_tests
    tests/manifest/asset_info_manifest_tests.cpp
    tests/manifest/asset_info_animation_stub.cpp
    ENGINE/asset/asset_info.cpp
    ENGINE/asset/asset_types.cpp
    ENGINE/asset/animation.cpp
    ENGINE/asset_info_methods/lighting_loader.cpp
    ENGINE/asset_info_methods/child_loader.cpp
    ENGINE/utils/area.cpp
    ENGINE/utils/generate_light.cpp
)
target_include_directories(asset_info_manifest_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/asset
    ${CMAKE_SOURCE_DIR}/ENGINE/asset_info_methods
    ${CMAKE_SOURCE_DIR}/ENGINE/core
    ${CMAKE_SOURCE_DIR}/ENGINE/dev_mode
    ${CMAKE_SOURCE_DIR}/ENGINE/utils
)
target_link_libraries(asset_info_manifest_tests PRIVATE
    nlohmann_json::nlohmann_json
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_mixer::SDL2_mixer
    SDL2_ttf::SDL2_ttf
)
target_compile_definitions(asset_info_manifest_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME asset_info_manifest_tests COMMAND asset_info_manifest_tests)
