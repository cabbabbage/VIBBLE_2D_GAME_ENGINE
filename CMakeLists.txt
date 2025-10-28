cmake_minimum_required(VERSION 3.16)
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

list(REMOVE_DUPLICATES ENGINE_SRC)

add_executable(engine ${ENGINE_SRC})

# Shared logging library for tests and tools
add_library(vibble_log STATIC
    ENGINE/utils/log.cpp
)
target_include_directories(vibble_log PUBLIC
    ${CMAKE_SOURCE_DIR}/ENGINE/utils
)

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
find_package(CURL REQUIRED)

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
        CURL::libcurl
        ${GLAD_TARGET}
        OpenGL::GL
)

# Faster relinks on MSVC Debug
if(MSVC)
    target_link_options(engine PRIVATE "$<$<CONFIG:Debug>:/INCREMENTAL>")
endif()

enable_testing()

add_executable(dev_mode_ui_tests
    tests/dev_mode/dev_footer_bar_tests.cpp
    tests/dev_mode/tag_cache_tests.cpp
    tests/dev_mode/asset_info_stub.cpp
    ENGINE/dev_mode/dev_ui_settings.cpp
    ENGINE/dev_mode/dev_footer_bar.cpp
    ENGINE/dev_mode/draw_utils.cpp
    ENGINE/dev_mode/font_cache.cpp
    ENGINE/dev_mode/widgets.cpp
    ENGINE/dev_mode/dm_styles.cpp
    ENGINE/dev_mode/DockableCollapsible.cpp
    ENGINE/dev_mode/FloatingDockableManager.cpp
    ENGINE/dev_mode/FloatingPanelLayoutManager.cpp
    ENGINE/dev_mode/search_assets.cpp
    ENGINE/dev_mode/core/manifest_store.cpp
    ENGINE/dev_mode/core/dev_json_store.cpp
    ENGINE/dev_mode/tag_utils.cpp
    ENGINE/utils/input.cpp
    ENGINE/core/manifest/manifest_loader.cpp
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
    vibble_log
)
target_compile_definitions(dev_mode_ui_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}" ASSET_INFO_ENABLE_TEST_ACCESS)
add_test(NAME dev_mode_ui_tests COMMAND dev_mode_ui_tests)

set(AREA_SOURCES
    ENGINE/utils/area.cpp
    ENGINE/util/grid_math.cpp
)

add_executable(asset_loader_distance_tests
    tests/core/asset_loader_distance_tests.cpp
    ${AREA_SOURCES}
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
    vibble_log
)
target_compile_definitions(asset_loader_distance_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME asset_loader_distance_tests COMMAND asset_loader_distance_tests)

# === scaling_logic_usage_sampling_tests ===
add_executable(scaling_logic_usage_sampling_tests
    tests/core/scaling_logic_usage_sampling_tests.cpp
)
target_include_directories(scaling_logic_usage_sampling_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/render_pipeline
)
target_link_libraries(scaling_logic_usage_sampling_tests PRIVATE
    nlohmann_json::nlohmann_json
    SDL2::SDL2
)
target_compile_definitions(scaling_logic_usage_sampling_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME scaling_logic_usage_sampling_tests COMMAND scaling_logic_usage_sampling_tests)

# === camera_refresh_tests ===
add_executable(camera_refresh_tests
    tests/core/camera_refresh_tests.cpp
    ENGINE/render/camera.cpp
    ENGINE/core/find_current_room.cpp
    ${AREA_SOURCES}
    ENGINE/utils/range_util.cpp
    ENGINE/utils/cache_manager.cpp
)
target_include_directories(camera_refresh_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/stubs
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/render
    ${CMAKE_SOURCE_DIR}/ENGINE/utils
)
target_link_libraries(camera_refresh_tests PRIVATE
    SDL2::SDL2
    SDL2_image::SDL2_image
    nlohmann_json::nlohmann_json
    vibble_log
)
target_compile_definitions(camera_refresh_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME camera_refresh_tests COMMAND camera_refresh_tests)


add_executable(world_grid_chunk_tests
    tests/world/grid_chunk_tests.cpp
    tests/world/chunk_stub.cpp
    ENGINE/world/grid.cpp
    ENGINE/world/chunk.cpp
    ENGINE/world/chunk_manager.cpp
)
target_include_directories(world_grid_chunk_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/world
)
target_link_libraries(world_grid_chunk_tests PRIVATE
    SDL2::SDL2
    vibble_log
)
target_compile_definitions(world_grid_chunk_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME world_grid_chunk_tests COMMAND world_grid_chunk_tests)

add_executable(map_layers_controller_tests
    tests/dev_mode/map_layers_controller_tests.cpp
    tests/dev_mode/map_layers_controller_main.cpp
    ENGINE/dev_mode/core/manifest_store.cpp
    ENGINE/dev_mode/core/dev_json_store.cpp
    ENGINE/dev_mode/tag_utils.cpp
    ENGINE/dev_mode/map_layers_controller.cpp
    ENGINE/dev_mode/dev_controls_persistence.cpp
    ENGINE/core/manifest/manifest_loader.cpp
    ENGINE/utils/display_color.cpp
    ENGINE/utils/ranged_color.cpp
    ENGINE/map_generation/map_layers_geometry.cpp
)
target_include_directories(map_layers_controller_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/core
    ${CMAKE_SOURCE_DIR}/ENGINE/dev_mode
)
target_link_libraries(map_layers_controller_tests PRIVATE
    nlohmann_json::nlohmann_json
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    SDL2_image::SDL2_image
    SDL2_mixer::SDL2_mixer
    vibble_log
)
target_compile_definitions(map_layers_controller_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME map_layers_controller_tests COMMAND map_layers_controller_tests)

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
    vibble_log
)
target_compile_definitions(manifest_loader_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME manifest_loader_tests COMMAND manifest_loader_tests)

add_executable(manifest_store_tests
    tests/manifest/manifest_store_tests.cpp
    tests/manifest/spawn_group_manifest_tests.cpp
    tests/dev_mode/room_editor_manifest_tests.cpp
    tests/dev_mode/spawn_group_config_tests.cpp
    tests/dev_mode/spawn_group_model_tests.cpp
    ENGINE/dev_mode/search_assets.cpp
    ENGINE/asset/asset_info.cpp
    ENGINE/asset/animation.cpp
    ENGINE/dev_mode/dev_controls_persistence.cpp
    ENGINE/dev_mode/core/manifest_store.cpp
    ENGINE/dev_mode/core/dev_json_store.cpp
    ENGINE/dev_mode/asset_sections/Section_SpawnGroups.cpp
    ENGINE/dev_mode/spawn_group_config/SpawnGroupConfig.cpp
    ENGINE/dev_mode/spawn_group_config/spawn_group_utils.cpp
    ENGINE/dev_mode/spawn_group_config/widgets/CandidateEditorPieGraphWidget.cpp
    ENGINE/dev_mode/spawn_group_config/widgets/ExactWidget.cpp
    ENGINE/dev_mode/spawn_group_config/widgets/PerimeterWidget.cpp
    ENGINE/dev_mode/spawn_group_config/widgets/EdgeWidget.cpp
    ENGINE/dev_mode/spawn_group_config/widgets/RandomWidget.cpp
    ENGINE/dev_mode/spawn_group_config/spawn_method_control_widgets/LinkToAreaButton.cpp
    ENGINE/dev_mode/spawn_group_config/spawn_method_control_widgets/SpawnMethodDropdown.cpp
    ENGINE/dev_mode/DockableCollapsible.cpp
    ENGINE/dev_mode/FloatingDockableManager.cpp
    ENGINE/dev_mode/FloatingPanelLayoutManager.cpp
    ENGINE/dev_mode/draw_utils.cpp
    ENGINE/dev_mode/font_cache.cpp
    ENGINE/dev_mode/widgets.cpp
    ENGINE/dev_mode/dm_styles.cpp
    ENGINE/dev_mode/tag_utils.cpp
    ENGINE/dev_mode/dev_ui_settings.cpp
    ENGINE/dev_mode/manifest_spawn_group_utils.cpp
    ENGINE/dev_mode/search_assets.cpp
    ENGINE/utils/map_grid_settings.cpp
    ENGINE/utils/input.cpp
    ${AREA_SOURCES}
    ENGINE/asset_info_methods/animation_loader.cpp
    ENGINE/asset_info_methods/lighting_loader.cpp
    ENGINE/asset_info_methods/asset_child_loader.cpp
    ENGINE/utils/cache_manager.cpp
    ENGINE/utils/generate_faded_mask.cpp
    ENGINE/utils/loading_status_notifier.cpp
    ENGINE/dev_mode/room_editor_map_info.cpp
    ENGINE/core/manifest/manifest_loader.cpp
    ENGINE/map_generation/map_layers_geometry.cpp
    ENGINE/asset/asset_library.cpp
    ENGINE/spawn/asset_spawn_planner.cpp
    ENGINE/spawn/room_relative_size_resolver.cpp
)
target_include_directories(manifest_store_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/core
    ${CMAKE_SOURCE_DIR}/ENGINE/dev_mode
    ${CMAKE_SOURCE_DIR}/ENGINE/map_generation
)
target_link_libraries(manifest_store_tests PRIVATE
    nlohmann_json::nlohmann_json
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_mixer::SDL2_mixer
    SDL2_ttf::SDL2_ttf
    vibble_log
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
    ENGINE/asset_info_methods/asset_child_loader.cpp
    ENGINE/utils/generate_faded_mask.cpp
    ${AREA_SOURCES}
    ENGINE/utils/cache_manager.cpp
    ENGINE/utils/loading_status_notifier.cpp
    ENGINE/dev_mode/core/manifest_store.cpp
    ENGINE/dev_mode/core/dev_json_store.cpp
    ENGINE/dev_mode/tag_utils.cpp
    ENGINE/core/manifest/manifest_loader.cpp
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
    vibble_log
)
target_compile_definitions(asset_info_manifest_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME asset_info_manifest_tests COMMAND asset_info_manifest_tests)

add_executable(edge_spawner_tests
    tests/spawn/edge_spawner_tests.cpp
    ENGINE/spawn/methods/edge_spawner.cpp
    ${AREA_SOURCES}
)
target_include_directories(edge_spawner_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include
    ${CMAKE_SOURCE_DIR}/ENGINE
    ${CMAKE_SOURCE_DIR}/ENGINE/spawn
    ${CMAKE_SOURCE_DIR}/ENGINE/util
    ${CMAKE_SOURCE_DIR}/ENGINE/utils
)
target_link_libraries(edge_spawner_tests PRIVATE
    SDL2::SDL2
    vibble_log
)
target_compile_definitions(edge_spawner_tests PRIVATE SDL_MAIN_HANDLED PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(edge_spawner_tests PRIVATE EDGE_SPAWNER_DISABLE_RUNTIME)
add_test(NAME edge_spawner_tests COMMAND edge_spawner_tests)

