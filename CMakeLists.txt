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
find_package(nlohmann_json CONFIG REQUIRED)
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
        message(FATAL_ERROR
            "glad not found.\n"
            "Fix options:\n"
            "  1) Install via vcpkg and pass the vcpkg toolchain to CMake (recommended):\n"
            "       vcpkg install glad:x64-windows\n"
            "       -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake\n"
            "  2) Add glad sources to third_party/glad (with include/ and src/glad.c).")
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
        ${CMAKE_SOURCE_DIR}/ENGINE/room
        ${CMAKE_SOURCE_DIR}/ENGINE/spawn
        ${CMAKE_SOURCE_DIR}/ENGINE/ui
        ${CMAKE_SOURCE_DIR}/ENGINE/utils
)

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
