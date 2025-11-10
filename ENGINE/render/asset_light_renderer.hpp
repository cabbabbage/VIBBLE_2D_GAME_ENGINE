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
                       std::vector<int>& scratch_indices,
                       float light_visibility);
    ~AssetLightRenderer();

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
    bool render_light_with_asset_mask(const LightSource& light,
                                      const ComputedLight& computed,
                                      const SDL_Color& base_color);
    float compute_flicker_multiplier(const LightSource& light) const;
    SDL_Texture* ensure_mask_composite_texture(int width, int height, Uint32 format_hint);
    SDL_Rect     scale_mask_rect_to_asset(const SDL_Rect& rect, int mask_width, int mask_height) const;

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
    SDL_Texture*                           mask_composite_texture_ = nullptr;
    int                                    mask_composite_w_ = 0;
    int                                    mask_composite_h_ = 0;
    Uint32                                 mask_composite_format_ = 0;
    float                                  overlay_visibility_ = 1.0f;
    float                                  flicker_time_seconds_ = 0.0f;
};
