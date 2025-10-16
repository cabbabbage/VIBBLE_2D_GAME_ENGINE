#include "light_map.hpp"
#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>
#include <iostream>
#include <cmath>

namespace {

float compute_luminance(const SDL_Color& color) {
        // Rec. 709 luminance approximation.
        return (0.2126f * static_cast<float>(color.r) +
                0.7152f * static_cast<float>(color.g) +
                0.0722f * static_cast<float>(color.b)) / 255.0f;
}

} // namespace
LightMap::LightMap(SDL_Renderer* renderer,
                   Assets* assets,
                   Global_Light_Source& main_light,
                   int screen_width,
                   int screen_height,
                   SDL_Texture* fullscreen_light_tex)
: renderer_(renderer),
assets_(assets),
main_light_(main_light),
screen_width_(screen_width),
screen_height_(screen_height),
fullscreen_light_tex_(fullscreen_light_tex)
{
        virtual_light_map_.clear();
}

void LightMap::set_fullscreen_light_settings(SDL_Color color, int min_opacity, int max_opacity) {
        fullscreen_light_color_ = SDL_Color{color.r, color.g, color.b, 255};
        fullscreen_light_min_opacity_ = std::clamp(min_opacity, 0, 255);
        fullscreen_light_max_opacity_ = std::clamp(max_opacity, 0, 255);
        if (fullscreen_light_min_opacity_ > fullscreen_light_max_opacity_) {
                std::swap(fullscreen_light_min_opacity_, fullscreen_light_max_opacity_);
        }
}

void LightMap::update_virtual_light_map() {
        cached_layers_.clear();
        collect_layers(cached_layers_);
        cached_layers_ready_ = true;
        compute_virtual_light_map(cached_layers_);
}

void LightMap::render(bool debugging, bool light_map_only) {
        if (debugging) std::cout << "[render_asset_lights_z] start\n";
        const int downscale = 4;
        const int low_w = screen_width_  / downscale;
        const int low_h = screen_height_ / downscale;
        const std::vector<LightEntry>* layers_ptr = nullptr;
        std::vector<LightEntry> transient_layers;
        if (cached_layers_ready_) {
                layers_ptr = &cached_layers_;
        } else {
                transient_layers.clear();
                collect_layers(transient_layers);
                layers_ptr = &transient_layers;
        }

        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
        SDL_Texture* lowres_mask = build_lowres_mask(*layers_ptr, low_w, low_h, downscale);
        SDL_SetTextureBlendMode(lowres_mask, light_map_only ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_MOD);

        SDL_SetRenderTarget(renderer_, prev_target);
        SDL_RenderCopy(renderer_, lowres_mask, nullptr, nullptr);
        SDL_DestroyTexture(lowres_mask);
        cached_layers_ready_ = false;
        if (debugging) std::cout << "[render_asset_lights_z] end\n";
}

void LightMap::collect_layers(std::vector<LightEntry>& out) {
        const float camera_scale = assets_ ? assets_->getView().get_scale() : 1.0f;
        const float inv_scale = (camera_scale > 0.0f && std::isfinite(camera_scale)) ? (1.0f / camera_scale) : 1.0f;
        constexpr int min_visible_w = 1;
        constexpr int min_visible_h = 1;
        Uint8 main_alpha = main_light_.get_current_color().a;
        Uint8 screen_alpha = static_cast<Uint8>(std::clamp<int>(main_alpha, fullscreen_light_min_opacity_, fullscreen_light_max_opacity_));
        last_main_light_alpha_ = screen_alpha;
        const auto& lit_assets = assets_->getActiveLitAssets();
        if (out.capacity() < lit_assets.size() + 3) {
                out.reserve(lit_assets.size() + 3);
        }
        if (fullscreen_light_tex_ && fullscreen_light_enabled_) {
                LightEntry entry{};
                entry.tex = fullscreen_light_tex_;
                entry.dst = { 0, 0, screen_width_, screen_height_ };
                entry.alpha = screen_alpha;
                entry.flip = SDL_FLIP_NONE;
                entry.color_mod = fullscreen_light_color_;
                out.push_back(entry);
        }

        for (Asset* asset : lit_assets) {
                if (!asset || !asset->info) {
                        continue;
                }
                const auto& lights = asset->info->light_sources;
                if (lights.empty()) {
                        continue;
                }

                const bool flipped = asset->flipped;
                for (const LightSource& light : lights) {
                        if (!light.texture || light.intensity <= 0) {
                                continue;
                        }

                        int base_w = light.cached_w;
                        int base_h = light.cached_h;
                        if (base_w <= 0 || base_h <= 0) {
                                SDL_QueryTexture(light.texture, nullptr, nullptr, &base_w, &base_h);
                        }
                        if (base_w <= 0 || base_h <= 0) {
                                continue;
                        }

                        int scaled_fw = base_w;
                        int scaled_fh = base_h;

                        const int offset_x = flipped ? -light.offset_x : light.offset_x;
                        SDL_Point light_pos{asset->pos.x + offset_x, asset->pos.y + light.offset_y};
                        SDL_Rect dst = get_scaled_position_rect(light_pos, scaled_fw, scaled_fh, inv_scale, min_visible_w, min_visible_h);
                        if (dst.w == 0 && dst.h == 0) {
                                continue;
                        }

                        float desired_scale = render_pipeline::ScalingLogic::ComputeScale(base_w, base_h, dst.w, dst.h);
                        const float base_scale = (asset->info->scale_factor > 0.0f && std::isfinite(asset->info->scale_factor))
                                                     ? asset->info->scale_factor
                                                     : 1.0f;
                        const auto& usage = asset->last_scale_usage();
                        if (std::isfinite(usage.requested_scale) && usage.requested_scale > 0.0f) {
                                const bool usage_default =
                                        (std::fabs(usage.requested_scale - 1.0f) < 1e-4f) &&
                                        (std::fabs(usage.texture_scale   - 1.0f) < 1e-4f) &&
                                        (std::fabs(usage.remainder_scale - 1.0f) < 1e-4f) &&
                                        usage.variant_index == 0 &&
                                        (std::fabs(base_scale - 1.0f) > 1e-4f);
                                if (!usage_default && base_scale > 0.0f && std::isfinite(base_scale)) {
                                        float normalized = usage.requested_scale / base_scale;
                                        if (std::isfinite(normalized) && normalized > 0.0f) {
                                                desired_scale = normalized;
                                        }
                                }
                        }
                        SDL_Texture* tex = light.texture_for_scale(desired_scale);
                        if (!tex) {
                                continue;
                        }

                        LightEntry entry{};
                        entry.tex = tex;
                        entry.dst = dst;
                        entry.alpha = static_cast<Uint8>(std::clamp(light.intensity, 0, 255));
                        entry.flip = flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                        entry.color_mod = SDL_Color{light.color.r, light.color.g, light.color.b, 255};
                        out.push_back(entry);
                }
        }
}

SDL_Texture* LightMap::build_lowres_mask(const std::vector<LightEntry>& layers,
                                         int low_w, int low_h, int downscale) {
        SDL_Texture* lowres_mask = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, low_w, low_h);
        SDL_SetTextureBlendMode(lowres_mask, SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(lowres_mask, SDL_ScaleModeNearest);
#endif
        SDL_SetRenderTarget(renderer_, lowres_mask);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 200);
        SDL_RenderClear(renderer_);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
        if (fullscreen_light_enabled_ && last_main_light_alpha_ > 0) {
                SDL_SetRenderDrawColor(renderer_, fullscreen_light_color_.r, fullscreen_light_color_.g, fullscreen_light_color_.b, last_main_light_alpha_);
                SDL_Rect fullscreen_rect{ 0, 0, low_w, low_h };
                SDL_RenderFillRect(renderer_, &fullscreen_rect);
        }
        for (auto& e : layers) {
                SDL_SetTextureBlendMode(e.tex, SDL_BLENDMODE_ADD);
                SDL_SetTextureAlphaMod(e.tex, e.alpha);
                SDL_SetTextureColorMod(e.tex, e.color_mod.r, e.color_mod.g, e.color_mod.b);
                SDL_Rect scaled_dst{
                        e.dst.x / downscale,
                        e.dst.y / downscale,
                        e.dst.w / downscale,
                        e.dst.h / downscale
};
		SDL_RenderCopyEx(renderer_, e.tex, nullptr, &scaled_dst, 0, nullptr, e.flip);
	}
        return lowres_mask;
}

SDL_Rect LightMap::get_scaled_position_rect(SDL_Point pos, int fw, int fh,
                                            float inv_scale, int min_w, int min_h) {
        float base_sw = static_cast<float>(fw) * inv_scale;
        float base_sh = static_cast<float>(fh) * inv_scale;
        if (base_sw < static_cast<float>(min_w) && base_sh < static_cast<float>(min_h)) {
                return {0, 0, 0, 0};
        }
        const camera::RenderEffects effects = assets_->getView().compute_render_effects(pos, base_sh, base_sh);
        float scaled_sw = base_sw * effects.distance_scale;
        float scaled_sh = base_sh * effects.distance_scale;
        float final_visible_h = scaled_sh * effects.vertical_scale;
        if (scaled_sw < static_cast<float>(min_w) && final_visible_h < static_cast<float>(min_h)) {
                return {0, 0, 0, 0};
        }
        int sw = std::max(1, static_cast<int>(std::lround(scaled_sw)));
        int sh = std::max(1, static_cast<int>(std::lround(final_visible_h)));
        if (sw < min_w && sh < min_h) {
                return {0, 0, 0, 0};
        }
        SDL_Point cp = effects.screen_position;
        return SDL_Rect{ cp.x - sw / 2, cp.y - sh / 2, sw, sh };
}

void LightMap::compute_virtual_light_map(const std::vector<LightEntry>& layers) {
        virtual_light_map_.clear();
        if (screen_width_ <= 0 || screen_height_ <= 0) {
                return;
        }

        const float cell_width  = static_cast<float>(screen_width_) / static_cast<float>(VirtualLightMap::kGridWidth);
        const float cell_height = static_cast<float>(screen_height_) / static_cast<float>(VirtualLightMap::kGridHeight);
        if (cell_width <= 0.0f || cell_height <= 0.0f) {
                return;
        }

        const float cell_area = cell_width * cell_height;
        for (const LightEntry& entry : layers) {
                if (entry.dst.w <= 0 || entry.dst.h <= 0 || entry.alpha == 0) {
                        continue;
                }

                float luminance = std::clamp(compute_luminance(entry.color_mod), 0.0f, 1.0f);
                float brightness = (static_cast<float>(entry.alpha) / 255.0f) * luminance;
                if (brightness <= 0.0f) {
                        continue;
                }

                float rect_x0 = static_cast<float>(entry.dst.x);
                float rect_y0 = static_cast<float>(entry.dst.y);
                float rect_x1 = rect_x0 + static_cast<float>(entry.dst.w);
                float rect_y1 = rect_y0 + static_cast<float>(entry.dst.h);

                rect_x0 = std::max(rect_x0, 0.0f);
                rect_y0 = std::max(rect_y0, 0.0f);
                rect_x1 = std::min(rect_x1, static_cast<float>(screen_width_));
                rect_y1 = std::min(rect_y1, static_cast<float>(screen_height_));

                if (rect_x1 <= rect_x0 || rect_y1 <= rect_y0) {
                        continue;
                }

                int grid_x0 = static_cast<int>(std::floor(rect_x0 / cell_width));
                int grid_y0 = static_cast<int>(std::floor(rect_y0 / cell_height));
                int grid_x1 = static_cast<int>(std::floor((rect_x1 - 1.0f) / cell_width));
                int grid_y1 = static_cast<int>(std::floor((rect_y1 - 1.0f) / cell_height));

                grid_x0 = std::clamp(grid_x0, 0, VirtualLightMap::kGridWidth - 1);
                grid_y0 = std::clamp(grid_y0, 0, VirtualLightMap::kGridHeight - 1);
                grid_x1 = std::clamp(grid_x1, 0, VirtualLightMap::kGridWidth - 1);
                grid_y1 = std::clamp(grid_y1, 0, VirtualLightMap::kGridHeight - 1);

                for (int gy = grid_y0; gy <= grid_y1; ++gy) {
                        float cell_y0 = static_cast<float>(gy) * cell_height;
                        float cell_y1 = cell_y0 + cell_height;
                        float iy0 = std::max(cell_y0, rect_y0);
                        float iy1 = std::min(cell_y1, rect_y1);
                        if (iy1 <= iy0) {
                                continue;
                        }

                        for (int gx = grid_x0; gx <= grid_x1; ++gx) {
                                float cell_x0 = static_cast<float>(gx) * cell_width;
                                float cell_x1 = cell_x0 + cell_width;
                                float ix0 = std::max(cell_x0, rect_x0);
                                float ix1 = std::min(cell_x1, rect_x1);
                                if (ix1 <= ix0) {
                                        continue;
                                }

                                const float area = (ix1 - ix0) * (iy1 - iy0);
                                if (area <= 0.0f) {
                                        continue;
                                }

                                float weight = area / cell_area;
                                weight = std::clamp(weight, 0.0f, 1.0f);
                                float& cell_value = virtual_light_map_.at(gx, gy);
                                cell_value = std::min(1.0f, cell_value + brightness * weight);
                        }
                }
        }
}
