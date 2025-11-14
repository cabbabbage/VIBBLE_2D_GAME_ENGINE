#pragma once

#include <SDL.h>

#include "render/image_effect_settings.hpp"

namespace image_effects {

bool ApplyImageEffectsToSurface(SDL_Surface* surface, const camera_effects::ImageEffectSettings& settings);
bool ApplyImageEffectsToTexture(SDL_Renderer* renderer,
                                SDL_Texture*& texture,
                                int width,
                                int height,
                                const camera_effects::ImageEffectSettings& settings);
SDL_Texture* BakeImageEffectTexture(SDL_Renderer* renderer,
                                    SDL_Texture* source,
                                    int width,
                                    int height,
                                    const camera_effects::ImageEffectSettings& settings);

} // namespace image_effects
