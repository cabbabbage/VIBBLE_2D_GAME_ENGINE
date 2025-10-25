#pragma once

#include <SDL.h>

#include <string>
#include <utility>
#include <vector>

#include "shadow_mask_settings.hpp"

class GenerateFadedMask {
public:
    using MaskVariants = std::vector<std::vector<SDL_Surface*>>;

    static std::pair<MaskVariants, bool> BuildMasks(const std::string& asset_name, const std::string& animation_id, const std::vector<int>& scale_steps, const MaskVariants& variant_frames, const ShadowMaskSettings& settings);

    static std::vector<std::vector<SDL_Texture*>> SurfacesToTextures(SDL_Renderer* renderer, const MaskVariants& masks);

    static SDL_Surface* GenerateSingleMask(SDL_Surface* source, const ShadowMaskSettings& settings);
};
