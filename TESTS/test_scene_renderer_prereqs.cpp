#include "doctest/doctest.h"

#include <cstdint>
#include <string>

#include "render/render.hpp"

TEST_CASE("SceneRenderer prerequisites reject missing renderer") {
    Assets* dummy_assets = reinterpret_cast<Assets*>(static_cast<std::uintptr_t>(0x1));
    std::string reason;
    CHECK_FALSE(SceneRenderer::prerequisites_ready(nullptr, dummy_assets, &reason));
    CHECK(reason == "SDL_Renderer pointer is null.");
}

TEST_CASE("SceneRenderer prerequisites reject missing assets") {
    SDL_Renderer* dummy_renderer = reinterpret_cast<SDL_Renderer*>(static_cast<std::uintptr_t>(0x1));
    std::string reason;
    CHECK_FALSE(SceneRenderer::prerequisites_ready(dummy_renderer, nullptr, &reason));
    CHECK(reason == "Assets pointer is null.");
}

TEST_CASE("SceneRenderer prerequisites succeed when dependencies are present") {
    SDL_Renderer* dummy_renderer = reinterpret_cast<SDL_Renderer*>(static_cast<std::uintptr_t>(0x1));
    Assets* dummy_assets = reinterpret_cast<Assets*>(static_cast<std::uintptr_t>(0x1));
    std::string reason{"not empty"};
    CHECK(SceneRenderer::prerequisites_ready(dummy_renderer, dummy_assets, &reason));
    CHECK(reason.empty());
}
