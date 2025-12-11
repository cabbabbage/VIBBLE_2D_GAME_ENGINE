#include "generate_faded_mask.hpp"

#include "utils/cache_manager.hpp"
#include "utils/log.hpp"

#include <SDL.h>

#include <utility>
#include <vector>

std::pair<GenerateFadedMask::MaskVariants, bool> GenerateFadedMask::BuildMasks(
    const std::string& asset_name,
    const std::string& animation_id,
    const std::vector<int>& ,
    const MaskVariants& ,
    const ShadowMaskSettings& )
{
    vibble::log::warn(std::string{"[GenerateFadedMask] C++ mask generation is disabled; "}
                      + "invoke the Python asset pipeline (asset_tool.py / shadow_mask.py) "
                      + "to build masks for '" + asset_name + "::" + animation_id + "'.");
    return {MaskVariants{}, false};
}

std::vector<std::vector<SDL_Texture*>> GenerateFadedMask::SurfacesToTextures(
    SDL_Renderer* renderer,
    const MaskVariants& masks)
{
    std::vector<std::vector<SDL_Texture*>> textures;
    textures.resize(masks.size());
    for (std::size_t variant_idx = 0; variant_idx < masks.size(); ++variant_idx) {
        const auto& surfaces = masks[variant_idx];
        auto& out_list       = textures[variant_idx];
        out_list.reserve(surfaces.size());
        for (SDL_Surface* surface : surfaces) {
            SDL_Texture* texture = surface ? CacheManager::surface_to_texture(renderer, surface) : nullptr;
            out_list.push_back(texture);
        }
    }
    return textures;
}

SDL_Surface* GenerateFadedMask::GenerateSingleMask(SDL_Surface* ,
                                                   const ShadowMaskSettings& ) {
    vibble::log::warn("[GenerateFadedMask] GenerateSingleMask is disabled; " "use the Python shadow mask utilities instead.");
    return nullptr;
}
