#pragma once

#include <SDL.h>

#include <cstddef>
#include <vector>

#include "render/runtime_lighting_sampler.hpp"
#include "utils/light_source.hpp"

class Asset;

class AssetLightRenderer {
public:
    struct DarkMaskResult {
        std::size_t max_vertices = 0;
        std::size_t max_indices  = 0;
    };

    AssetLightRenderer(SDL_Renderer* renderer,
                       const runtime_lighting::AssetLight& source,
                       std::vector<SDL_Vertex>& scratch_vertices,
                       std::vector<int>& scratch_indices);

    bool valid() const { return valid_; }

    DarkMaskResult accumulate_dark_mask();
    void draw_behind();
    void draw_in_front();

private:
    enum class Pass { kBehind, kFront };

    struct ComputedLight {
        const LightSource* source = nullptr;
        int                intensity = 0;
        float              center_x  = 0.0f;
        float              center_y  = 0.0f;
        float              radius_x  = 0.0f;
        float              radius_y  = 0.0f;
        SDL_Rect           bounds{0, 0, 0, 0};
        float              fade_exponent = 1.0f;
        bool               textured      = false;
        SDL_Rect           texture_dst{0, 0, 0, 0};
        float              center_ratio_x = 0.0f;
        float              center_ratio_y = 0.0f;
        float              radius_ratio_x = 0.0f;
        float              radius_ratio_y = 0.0f;
        float              texture_ratio_x = 0.0f;
        float              texture_ratio_y = 0.0f;
        float              texture_ratio_w = 0.0f;
        float              texture_ratio_h = 0.0f;
    };

    bool prepare_light(const LightSource& light, ComputedLight& out) const;
    void draw_pass(Pass pass);
    void render_textured_light(const ComputedLight& info, const SDL_Rect& dst);
    void render_radial_light(const ComputedLight& info,
                             const SDL_Color&     base_color,
                             float                alpha_scale,
                             float                center_x,
                             float                center_y,
                             float                radius_x,
                             float                radius_y,
                             const SDL_Rect&      fallback_rect);
    SDL_Texture* resolve_target_for_light(const LightSource& light, SDL_Texture* fallback_target);
    SDL_Texture* acquire_asset_mask_texture();

    SDL_Renderer*                          renderer_ = nullptr;
    const runtime_lighting::AssetLight&    source_;
    Asset*                                 asset_ = nullptr;
    const std::vector<LightSource>*        lights_ = nullptr;
    float                                  scale_x_ = 1.0f;
    float                                  scale_y_ = 1.0f;
    float                                  safe_zoom_scale_x_ = 1.0f;
    float                                  safe_zoom_scale_y_ = 1.0f;
    float                                  center_base_x_ = 0.0f;
    float                                  center_base_y_ = 0.0f;
    bool                                   valid_ = false;
    std::vector<SDL_Vertex>&               scratch_vertices_;
    std::vector<int>&                      scratch_indices_;
    SDL_Texture*                           cached_mask_target_ = nullptr;
    bool                                   mask_target_resolved_ = false;
    int                                    mask_width_ = 0;
    int                                    mask_height_ = 0;
};

