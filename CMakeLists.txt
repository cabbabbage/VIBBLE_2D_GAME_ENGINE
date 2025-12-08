# VIBBLE 2D GAME ENGINE
 
cmake_minimum_required(VERSION 3.20)
project(VIBBLEEngine)

# ------------------------------------------
# Build settings focused on compile speed
# ------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

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
    add_compile_options(/MP /Zc:inline /Zc:__cplusplus /permissive /FS /bigobj)
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

list(REMOVE_DUPLICATES ENGINE_SRC)

add_executable(engine ${ENGINE_SRC})

# Exclude old shim files from the build
set(SHIM_FILES stride_player.hpp stride_player.cpp)
list(REMOVE_ITEM ENGINE_SRC ${SHIM_FILES})

# Unity build (jumbo) for faster compiles
option(ENABLE_UNITY_BUILD "Enable unity builds for faster compilation" OFF)
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

# Link Windows COM/Shell libs for modern file dialogs
if (WIN32)
    target_link_libraries(engine PRIVATE Ole32 Shell32)
endif()

# Faster relinks on MSVC Debug
if(MSVC)
    target_link_options(engine PRIVATE "$<$<CONFIG:Debug>:/INCREMENTAL>")
endif()

# ------------------------------------------
# Unit tests (doctest-based)
# ------------------------------------------
set(TEST_ROOT "${CMAKE_SOURCE_DIR}/TEST_TMP")
add_executable(engine_tests
    TESTS/test_main.cpp
    TESTS/test_dev_json_store.cpp
    TESTS/test_manifest_loader.cpp
    TESTS/test_scene_renderer_prereqs.cpp
    TESTS/test_animation_child_visibility.cpp
    TESTS/test_animation_child_runtime.cpp
    TESTS/test_animation_child_clone.cpp
    ENGINE/asset/animation.cpp
    ENGINE/asset/surface_utils.cpp
    ENGINE/asset/animation_cloner.cpp
    ENGINE/animation_update/child_attachment_controller.cpp
    ENGINE/dev_mode/frame_editor_session_child_timelines.cpp
    ENGINE/dev_mode/core/dev_json_store.cpp
    ENGINE/dev_mode/core/manifest_store.cpp
    ENGINE/dev_mode/manifest_asset_utils.cpp
    ENGINE/dev_mode/tag_utils.cpp
    ENGINE/core/manifest/manifest_loader.cpp
    ENGINE/utils/log.cpp
    ENGINE/utils/cache_manager.cpp
    ENGINE/utils/generate_faded_mask.cpp
    ENGINE/utils/loading_status_notifier.cpp
  )

target_include_directories(engine_tests
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
        ${CMAKE_SOURCE_DIR}/external
)

target_compile_definitions(engine_tests PRIVATE
    DEV_MODE_DISABLE_JSON_DEBOUNCE=1
    PROJECT_ROOT="${TEST_ROOT}"
    FRAME_EDITOR_TEST_PUBLIC_ACCESS=1
    FRAME_EDITOR_ACCESS=public
)

target_link_libraries(engine_tests
    PRIVATE
        nlohmann_json::nlohmann_json
        SDL2::SDL2 ${_SDL2MAIN_TARGET}
        SDL2_image::SDL2_image
        SDL2_mixer::SDL2_mixer
        SDL2_ttf::SDL2_ttf
        ${GLAD_TARGET}
        OpenGL::GL
)

if (WIN32)
    target_link_libraries(engine_tests PRIVATE Ole32 Shell32)
endif()

# ------------------------------------------
# Isolated WorldGrid tests (lightweight target)
# ------------------------------------------
add_executable(engine_world_tests
    TESTS/test_main.cpp
    TESTS/test_world_grid_same_chunk_move.cpp
    ENGINE/world/world_grid.cpp
    ENGINE/world/chunk_manager.cpp
    ENGINE/utils/grid_math.cpp
    ENGINE/utils/log.cpp
    TESTS/stubs/chunk_stubs.cpp
)

target_include_directories(engine_world_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/TESTS/stubs
        ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/external
)

target_compile_definitions(engine_world_tests PRIVATE
    PROJECT_ROOT="${TEST_ROOT}"
)

# No heavy libs needed; these sources don't call into SDL functions
# or OpenGL, they only use SDL types.
target_link_libraries(engine_world_tests
    PRIVATE
        SDL2::SDL2 ${_SDL2MAIN_TARGET}
)
