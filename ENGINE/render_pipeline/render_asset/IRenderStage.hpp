#pragma once

#include <SDL.h>

class Asset;
struct StageContext;

class IRenderStage {
public:
    virtual ~IRenderStage() = default;

    virtual bool         supports(const Asset& asset) const = 0;
    virtual SDL_Texture* run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) = 0;
};

