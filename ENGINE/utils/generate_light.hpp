#pragma once

#include <SDL.h>
#include <string>
#include <cstddef>
#include "light_source.hpp"

class GenerateLight {

        public:
    GenerateLight(SDL_Renderer* renderer);
    SDL_Texture* generate(SDL_Renderer* renderer, const std::string& asset_name, LightSource& light, std::size_t light_index);

        private:
    SDL_Renderer* renderer_;
    SDL_Texture* createBaseGradientTexture(int size, int radius, SDL_Color baseColor, int intensity);
    SDL_Texture* applyTransparencyMask(SDL_Texture* src, int size, int radius, int intensity, int falloff);
    SDL_Texture* applyFlares(SDL_Texture* src, int size, int radius, int flare);
};

#if defined(GENERATE_LIGHT_ENABLE_TEST_HOOKS)
namespace generate_light_testing {
void set_force_failure(bool enabled);
bool force_failure_enabled();
}
#endif
