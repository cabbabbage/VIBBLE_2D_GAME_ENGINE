#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "render/camera.hpp"
#include "render/depth_cue_utils.hpp"

class DepthCueEffects {
public:
    struct Row {
        float brightness_offset = 0.0f; // [-0.5..0.5]
        float saturation_offset = 0.0f; // [-0.5..0.5] (negative for grayscale mix)
        float primary_offset    = 0.0f; // [0..0.5]    (positive for RBY-only mix)
        float blur_radius_px    = 0.0f; // [0..50]
        float blur_mix          = 0.0f; // [0..1]
    };

    DepthCueEffects() = default;
    explicit DepthCueEffects(SDL_Renderer* renderer);
    DepthCueEffects(const DepthCueEffects&) = delete;
    DepthCueEffects& operator=(const DepthCueEffects&) = delete;
    ~DepthCueEffects();

    void set_renderer(SDL_Renderer* renderer);

    void compute_rows(int height,
                      float center_screen_y,
                      float fg_plane_screen_y,
                      float bg_plane_screen_y,
                      const camera::RealismSettings& cam_settings,
                      bool compute_blur,
                      std::vector<Row>& rows_out) const;

    SDL_Texture* apply_color_pass(SDL_Texture* source,
                                  int width,
                                  int height,
                                  const std::vector<Row>& rows,
                                  SDL_Texture* reusable_out) const;

    SDL_Texture* apply_variable_blur(SDL_Texture* source,
                                     int width,
                                     int height,
                                     const std::vector<Row>& rows,
                                     SDL_Texture* reusable_blur_out) const;

    SDL_Texture* build_color_texture(SDL_Texture* source,
                                     float saturation_percent,
                                     float primary_percent,
                                     float brightness_percent) const;

    SDL_Texture* get_or_build_tinted_texture(SDL_Texture* source,
                                             float saturation_percent,
                                             float primary_percent,
                                             float brightness_percent,
                                             std::uint64_t frame_counter);

    void prune_tinted_cache(std::uint64_t current_frame);
    void clear_cache();

private:
    struct TintedTextureEntry {
        SDL_Texture* texture = nullptr;
        std::uint64_t last_used_frame = 0;
    };

    static Uint8 clamp_to_u8(int v);
    static std::uint64_t pack_color_key(int sat_percent, int primary_percent, int brightness_percent);

    SDL_Texture* build_color_texture_internal(SDL_Texture* source,
                                              float saturation_percent,
                                              float primary_percent,
                                              float brightness_percent) const;

    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<SDL_Texture*, std::unordered_map<std::uint64_t, TintedTextureEntry>> tinted_texture_cache_;
};
