#pragma once

#include <SDL.h>
#include <vector>
#include <utility>
#include "area.hpp"

class FadeTextureGenerator {
public:
    FadeTextureGenerator(SDL_Renderer* renderer, SDL_Color color, double expand);
    std::vector<std::pair<SDL_Texture*, SDL_Rect>> generate_all(const std::vector<Area>& areas);
private:
    SDL_Renderer* renderer_;
    SDL_Color color_;
    double expand_;
};
