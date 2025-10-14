#pragma once

#include <SDL.h>
#include <array>
#include <vector>

#include "render_pipeline/ScalingLogic.hpp"

struct LightSource {
        int intensity = 255;
        int radius = 64;
        int fall_off = 50;
        int flare = 0;
        int flicker = 20;
        int offset_x = 0;
        int offset_y = 0;
        int x_radius = 0;
        int y_radius = 0;
        int apex_speed_bias = 0;
        int cached_w = 0;
        int cached_h = 0;
        SDL_Color color = {255, 255, 255, 255};
        SDL_Texture* texture = nullptr;
        bool behind = false;
        std::array<SDL_Texture*, render_pipeline::ScalingLogic::kDefaultVariantCount> cached_variants{};
        std::array<int, render_pipeline::ScalingLogic::kDefaultVariantCount> variant_w{};
        std::array<int, render_pipeline::ScalingLogic::kDefaultVariantCount> variant_h{};

        SDL_Texture* texture_for_scale(float desired_scale) const {
                const auto selection = render_pipeline::ScalingLogic::Choose(desired_scale);
                const std::size_t idx = static_cast<std::size_t>(selection.index);
                if (idx == 0) {
                        return texture;
                }
                if (idx < cached_variants.size() && cached_variants[idx]) {
                        return cached_variants[idx];
                }
                return texture;
        }
};
