#pragma once

#include <SDL.h>

#include <string>
#include <utility>
#include <vector>

#include "shadow_mask_settings.hpp"

class GenerateFadedMask {
public:
    using MaskVariants = std::vector<std::vector<SDL_Surface*>>;

    /**
     * Build alpha mask surfaces for each animation variant.
     *
     * The returned surfaces are newly allocated and owned by the caller; callers must free
     * every SDL_Surface* with SDL_FreeSurface when they are finished with them. The boolean
     * flag in the returned pair indicates whether the masks were loaded from cache (true) or
     * freshly generated (false).
     */
    static std::pair<MaskVariants, bool> BuildMasks(const std::string& asset_name,
                                                    const std::string& animation_id,
                                                    const std::vector<int>& scale_steps,
                                                    const MaskVariants& variant_frames,
                                                    const ShadowMaskSettings& settings);

    /**
     * Convert the provided mask surfaces into textures using CacheManager::surface_to_texture.
     * Callers own the resulting textures and are responsible for destroying them via
     * SDL_DestroyTexture.
     */
    static std::vector<std::vector<SDL_Texture*>> SurfacesToTextures(SDL_Renderer* renderer,
                                                                     const MaskVariants& masks);

    static SDL_Surface* GenerateSingleMask(SDL_Surface* source,
                                           const ShadowMaskSettings& settings);
};
