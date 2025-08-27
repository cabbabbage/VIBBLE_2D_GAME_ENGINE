cmake_minimum_required(VERSION 3.16)
project(VIBBLEEngine)

# ------------------------------------------
# Build settings focused on compile speed
# ------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release if not set
if(NOT CMAKE_BUILD_TYPE)
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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/ENGINE)

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
# Dependencies via vcpkg
# ------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
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
        ${CMAKE_SOURCE_DIR}/ENGINE/room
        ${CMAKE_SOURCE_DIR}/ENGINE/spawn
        ${CMAKE_SOURCE_DIR}/ENGINE/ui
        ${CMAKE_SOURCE_DIR}/ENGINE/utils
)

# ------------------------------------------
# Link libraries
# ------------------------------------------
target_link_libraries(engine
    PRIVATE
        nlohmann_json::nlohmann_json
        SDL2::SDL2 SDL2::SDL2main
        SDL2_image::SDL2_image
        SDL2_mixer::SDL2_mixer
        SDL2_ttf::SDL2_ttf
        glad::glad
        OpenGL::GL
)

# Faster relinks on MSVC Debug
if(MSVC)
    target_link_options(engine PRIVATE "$<$<CONFIG:Debug>:/INCREMENTAL>")
endif()
