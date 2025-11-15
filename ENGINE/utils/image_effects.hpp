#pragma once

#include <SDL.h>
#include <memory>

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

// Shared image effect processor (non-caching, pure processing)
class ImageEffectProcessor {
public:
    ImageEffectProcessor() = default;
    ~ImageEffectProcessor() = default;

    // Delete copy/move to prevent accidental copying
    ImageEffectProcessor(const ImageEffectProcessor&) = delete;
    ImageEffectProcessor& operator=(const ImageEffectProcessor&) = delete;
    ImageEffectProcessor(ImageEffectProcessor&&) = delete;
    ImageEffectProcessor& operator=(ImageEffectProcessor&&) = delete;

    // Apply effects to a texture and return a new texture with effects applied
    // Returns nullptr on failure, caller owns the returned texture
    static SDL_Texture* apply_effects(SDL_Renderer* renderer,
                                      SDL_Texture* source_texture,
                                      int width, int height,
                                      const camera_effects::ImageEffectSettings& settings);

    // Apply effects to an existing texture (modifies in-place if possible)
    // Returns true on success
    static bool apply_effects_in_place(SDL_Renderer* renderer,
                                       SDL_Texture*& texture,
                                       int width, int height,
                                       const camera_effects::ImageEffectSettings& settings);
};

} // namespace image_effects
