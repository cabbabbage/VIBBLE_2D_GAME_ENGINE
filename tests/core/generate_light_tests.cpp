#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "utils/generate_light.hpp"

#include <SDL.h>
#include <SDL_image.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

class SDLSubsystemGuard {
public:
    SDLSubsystemGuard() {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw std::runtime_error(SDL_GetError());
        }
        if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
            SDL_Quit();
            throw std::runtime_error(IMG_GetError());
        }
    }

    ~SDLSubsystemGuard() {
        IMG_Quit();
        SDL_Quit();
    }
};

struct SDLRendererGuard {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    SDLRendererGuard() {
        window = SDL_CreateWindow("test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 32, 32, SDL_WINDOW_HIDDEN);
        if (!window) {
            throw std::runtime_error(SDL_GetError());
        }
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) {
            SDL_DestroyWindow(window);
            throw std::runtime_error(SDL_GetError());
        }
    }

    ~SDLRendererGuard() {
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
    }
};

SDL_Texture* make_texture(SDL_Renderer* renderer, int w, int h) {
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tex) {
        throw std::runtime_error(SDL_GetError());
    }
    return tex;
}

}  // namespace

TEST_CASE("GenerateLight preserves existing textures when generation fails") {
    SDLSubsystemGuard sdl_guard;
    SDLRendererGuard renderer_guard;
    SDL_Renderer* renderer = renderer_guard.renderer;

    const std::string asset_name = "generate_light_test_asset";
    std::filesystem::remove_all(std::filesystem::path("cache") / asset_name);

    LightSource light;
    light.radius = 32;
    light.intensity = 200;
    light.fall_off = 50;
    light.color = SDL_Color{255, 200, 150, 255};

    SDL_Texture* original = make_texture(renderer, 8, 8);
    light.texture = original;
    light.cached_variants[0] = original;
    light.cached_w = 8;
    light.cached_h = 8;
    light.variant_w[0] = 8;
    light.variant_h[0] = 8;

    GenerateLight generator(renderer);

    generate_light_testing::set_force_failure(true);
    SDL_Texture* failure_result = generator.generate(renderer, asset_name, light, 0);
    CHECK(failure_result == nullptr);
    CHECK(light.texture == original);
    CHECK(light.cached_variants[0] == original);
    CHECK(light.cached_w == 8);
    CHECK(light.cached_h == 8);
    CHECK(light.variant_w[0] == 8);
    CHECK(light.variant_h[0] == 8);

    generate_light_testing::set_force_failure(false);
    SDL_Texture* success_result = generator.generate(renderer, asset_name, light, 0);
    REQUIRE(success_result != nullptr);
    CHECK(light.texture == success_result);
    CHECK(light.texture != original);
    CHECK(light.cached_w > 0);
    CHECK(light.cached_h > 0);
    CHECK(light.cached_variants[0] == success_result);
    CHECK(light.variant_w[0] == light.cached_w);
    CHECK(light.variant_h[0] == light.cached_h);

    SDL_DestroyTexture(light.texture);
    light.texture = nullptr;
    light.cached_variants[0] = nullptr;
    std::filesystem::remove_all(std::filesystem::path("cache") / asset_name);
}
