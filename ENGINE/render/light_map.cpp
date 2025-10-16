#include "light_map.hpp"
#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include <cmath>

namespace {

float compute_luminance(const SDL_Color& color) {
        // Rec. 709 luminance approximation.
        return (0.2126f * static_cast<float>(color.r) +
                0.7152f * static_cast<float>(color.g) +
                0.0722f * static_cast<float>(color.b)) / 255.0f;
}

} // namespace

SDL_Rect VirtualLightMap::quadrant_bounds(int index) const {
        if (index < 0 || index >= kQuadrantCount || screen_width <= 0 || screen_height <= 0) {
                return SDL_Rect{0, 0, 0, 0};
        }
        const int qx = index % kQuadrantCols;
        const int qy = index / kQuadrantCols;
        const int base_w = screen_width / kQuadrantCols;
        const int base_h = screen_height / kQuadrantRows;
        SDL_Rect rect{ qx * base_w, qy * base_h, base_w, base_h };
        if (qx == kQuadrantCols - 1) {
                rect.w = screen_width - rect.x;
        }
        if (qy == kQuadrantRows - 1) {
                rect.h = screen_height - rect.y;
        }
        return rect;
}

int VirtualLightMap::quadrant_for_point(float x, float y) const {
        if (screen_width <= 0 || screen_height <= 0) {
                return -1;
        }
        if (x < 0.0f || y < 0.0f || x >= static_cast<float>(screen_width) || y >= static_cast<float>(screen_height)) {
                return -1;
        }
        const float cell_w = static_cast<float>(screen_width) / static_cast<float>(kQuadrantCols);
        const float cell_h = static_cast<float>(screen_height) / static_cast<float>(kQuadrantRows);
        int qx = static_cast<int>(std::floor(x / cell_w));
        int qy = static_cast<int>(std::floor(y / cell_h));
        qx = std::clamp(qx, 0, kQuadrantCols - 1);
        qy = std::clamp(qy, 0, kQuadrantRows - 1);
        return qy * kQuadrantCols + qx;
}

int VirtualLightMap::quadrant_for_rect(const SDL_Rect& rect) const {
        const float cx = static_cast<float>(rect.x) + static_cast<float>(rect.w) * 0.5f;
        const float cy = static_cast<float>(rect.y) + static_cast<float>(rect.h) * 0.5f;
        return quadrant_for_point(cx, cy);
}
LightMap::LightMap(Assets* assets,
                   int screen_width,
                   int screen_height)
: assets_(assets),
screen_width_(screen_width),
screen_height_(screen_height)
{
        virtual_light_map_.clear();
        capture_format_ = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
}

LightMap::~LightMap() {
        if (fullscreen_texture_) {
                SDL_DestroyTexture(fullscreen_texture_);
                fullscreen_texture_ = nullptr;
        }
        if (capture_format_) {
                SDL_FreeFormat(capture_format_);
                capture_format_ = nullptr;
        }
}

void LightMap::prepare_fullscreen_light_map(SDL_Renderer* renderer) {
        if (!renderer) {
                return;
        }
        scratch_layers_.clear();
        collect_layers(scratch_layers_);
        compute_fullscreen_texture(renderer, scratch_layers_);
}

void LightMap::render_fullscreen_light_map(SDL_Renderer* renderer) const {
        if (!renderer || !fullscreen_texture_) {
                return;
        }
        SDL_SetTextureAlphaMod(fullscreen_texture_, 255);
        SDL_SetTextureBlendMode(fullscreen_texture_, SDL_BLENDMODE_ADD);
        SDL_RenderCopy(renderer, fullscreen_texture_, nullptr, nullptr);
}

void LightMap::update_virtual_light_map(SDL_Renderer* renderer) {
        if (!renderer) {
                return;
        }
        compute_virtual_light_map(renderer);
}

void LightMap::collect_layers(std::vector<LightEntry>& out) {
        const float camera_scale = assets_ ? assets_->getView().get_scale() : 1.0f;
        const float inv_scale = (camera_scale > 0.0f && std::isfinite(camera_scale)) ? (1.0f / camera_scale) : 1.0f;
        constexpr int min_visible_w = 1;
        constexpr int min_visible_h = 1;
        const auto& lit_assets = assets_->getActiveLitAssets();
        if (out.capacity() < lit_assets.size() + 3) {
                out.reserve(lit_assets.size() + 3);
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
                        entry.dst = dst;
                        entry.alpha = static_cast<Uint8>(std::clamp(light.intensity, 0, 255));
                        entry.color_mod = SDL_Color{light.color.r, light.color.g, light.color.b, 255};
                        entry.texture = tex;
                        out.push_back(entry);
                }
        }
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

void LightMap::compute_fullscreen_texture(SDL_Renderer* renderer, const std::vector<LightEntry>& layers) {
        if (!renderer) {
                return;
        }

        int output_w = screen_width_;
        int output_h = screen_height_;
        if (SDL_GetRendererOutputSize(renderer, &output_w, &output_h) == 0) {
                screen_width_ = output_w;
                screen_height_ = output_h;
        }

        if (screen_width_ <= 0 || screen_height_ <= 0) {
                return;
        }

        if (!fullscreen_texture_) {
                fullscreen_texture_ = SDL_CreateTexture(renderer,
                                                       SDL_PIXELFORMAT_RGBA8888,
                                                       SDL_TEXTUREACCESS_TARGET,
                                                       screen_width_,
                                                       screen_height_);
        } else {
                int tex_w = 0;
                int tex_h = 0;
                if (SDL_QueryTexture(fullscreen_texture_, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w != screen_width_ || tex_h != screen_height_) {
                        SDL_DestroyTexture(fullscreen_texture_);
                        fullscreen_texture_ = SDL_CreateTexture(renderer,
                                                               SDL_PIXELFORMAT_RGBA8888,
                                                               SDL_TEXTUREACCESS_TARGET,
                                                               screen_width_,
                                                               screen_height_);
                }
        }

        if (!fullscreen_texture_) {
                return;
        }

        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, fullscreen_texture_);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        for (const LightEntry& entry : layers) {
                if (entry.dst.w <= 0 || entry.dst.h <= 0 || entry.alpha == 0) {
                        continue;
                }

                if (entry.texture) {
                        Uint8 saved_r = 255;
                        Uint8 saved_g = 255;
                        Uint8 saved_b = 255;
                        Uint8 saved_a = 255;
                        SDL_BlendMode saved_blend = SDL_BLENDMODE_BLEND;
                        SDL_GetTextureColorMod(entry.texture, &saved_r, &saved_g, &saved_b);
                        SDL_GetTextureAlphaMod(entry.texture, &saved_a);
                        SDL_GetTextureBlendMode(entry.texture, &saved_blend);

                        SDL_SetTextureColorMod(entry.texture, entry.color_mod.r, entry.color_mod.g, entry.color_mod.b);
                        SDL_SetTextureAlphaMod(entry.texture, entry.alpha);
                        SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_ADD);
                        SDL_RenderCopy(renderer, entry.texture, nullptr, &entry.dst);
                        SDL_SetTextureBlendMode(entry.texture, saved_blend);
                        SDL_SetTextureColorMod(entry.texture, saved_r, saved_g, saved_b);
                        SDL_SetTextureAlphaMod(entry.texture, saved_a);
                } else {
                        SDL_SetRenderDrawColor(renderer, entry.color_mod.r, entry.color_mod.g, entry.color_mod.b, entry.alpha);
                        SDL_RenderFillRect(renderer, &entry.dst);
                }
        }

        SDL_SetRenderTarget(renderer, prev_target);
        SDL_SetTextureBlendMode(fullscreen_texture_, SDL_BLENDMODE_ADD);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(fullscreen_texture_, SDL_ScaleModeBest);
#endif
}

void LightMap::compute_virtual_light_map(SDL_Renderer* renderer) {
        virtual_light_map_.clear();
        if (!renderer) {
                return;
        }

        if (!capture_format_) {
                capture_format_ = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
        }

        if (!capture_format_) {
                return;
        }

        int output_w = screen_width_;
        int output_h = screen_height_;
        if (SDL_GetRendererOutputSize(renderer, &output_w, &output_h) != 0) {
                return;
        }

        screen_width_ = output_w;
        screen_height_ = output_h;
        virtual_light_map_.screen_width = screen_width_;
        virtual_light_map_.screen_height = screen_height_;

        if (screen_width_ <= 0 || screen_height_ <= 0) {
                return;
        }

        const std::size_t pixel_count = static_cast<std::size_t>(screen_width_) * static_cast<std::size_t>(screen_height_);
        pixel_buffer_.resize(pixel_count);
        if (pixel_buffer_.empty()) {
                return;
        }

        SDL_Rect read_rect{0, 0, screen_width_, screen_height_};
        const int pitch = screen_width_ * static_cast<int>(sizeof(Uint32));
        if (SDL_RenderReadPixels(renderer, &read_rect, SDL_PIXELFORMAT_RGBA8888, pixel_buffer_.data(), pitch) != 0) {
                return;
        }

        std::array<float, VirtualLightMap::kGridWidth * VirtualLightMap::kGridHeight> accum{};
        std::array<int, VirtualLightMap::kGridWidth * VirtualLightMap::kGridHeight> counts{};

        for (int y = 0; y < screen_height_; ++y) {
                const int gy = std::clamp((y * VirtualLightMap::kGridHeight) / screen_height_, 0, VirtualLightMap::kGridHeight - 1);
                for (int x = 0; x < screen_width_; ++x) {
                        const int gx = std::clamp((x * VirtualLightMap::kGridWidth) / screen_width_, 0, VirtualLightMap::kGridWidth - 1);
                        const std::size_t cell_index = static_cast<std::size_t>(gy) * VirtualLightMap::kGridWidth + static_cast<std::size_t>(gx);
                        const Uint32 pixel = pixel_buffer_[static_cast<std::size_t>(y) * screen_width_ + static_cast<std::size_t>(x)];
                        Uint8 r = 0;
                        Uint8 g = 0;
                        Uint8 b = 0;
                        Uint8 a = 0;
                        SDL_GetRGBA(pixel, capture_format_, &r, &g, &b, &a);
                        SDL_Color color{r, g, b, a};
                        float brightness = compute_luminance(color) * (static_cast<float>(a) / 255.0f);
                        accum[cell_index] += brightness;
                        counts[cell_index] += 1;
                }
        }

        for (int y = 0; y < VirtualLightMap::kGridHeight; ++y) {
                for (int x = 0; x < VirtualLightMap::kGridWidth; ++x) {
                        const std::size_t idx = static_cast<std::size_t>(y) * VirtualLightMap::kGridWidth + static_cast<std::size_t>(x);
                        const int count = counts[idx];
                        virtual_light_map_.at(x, y) = (count > 0) ? std::clamp(accum[idx] / static_cast<float>(count), 0.0f, 1.0f) : 0.0f;
                }
        }

        const int quad_w = VirtualLightMap::kQuadrantWidth;
        const int quad_h = VirtualLightMap::kQuadrantHeight;
        const float offset_scale = static_cast<float>(std::max(screen_width_, screen_height_)) * 0.05f;

        for (int qy = 0; qy < VirtualLightMap::kQuadrantRows; ++qy) {
                for (int qx = 0; qx < VirtualLightMap::kQuadrantCols; ++qx) {
                        const int quad_index = qy * VirtualLightMap::kQuadrantCols + qx;
                        float total = 0.0f;
                        int total_count = 0;
                        float left_sum = 0.0f;
                        int left_count = 0;
                        float right_sum = 0.0f;
                        int right_count = 0;
                        float top_sum = 0.0f;
                        int top_count = 0;
                        float bottom_sum = 0.0f;
                        int bottom_count = 0;

                        const int base_x = qx * quad_w;
                        const int base_y = qy * quad_h;
                        const int mid_x = base_x + quad_w / 2;
                        const int mid_y = base_y + quad_h / 2;

                        for (int y = 0; y < quad_h; ++y) {
                                for (int x = 0; x < quad_w; ++x) {
                                        const int gx = base_x + x;
                                        const int gy = base_y + y;
                                        float value = virtual_light_map_.at(gx, gy);
                                        total += value;
                                        total_count += 1;

                                        if (gx < mid_x) {
                                                left_sum += value;
                                                left_count += 1;
                                        } else {
                                                right_sum += value;
                                                right_count += 1;
                                        }

                                        if (gy < mid_y) {
                                                top_sum += value;
                                                top_count += 1;
                                        } else {
                                                bottom_sum += value;
                                                bottom_count += 1;
                                        }
                                }
                        }

                        VirtualLightMap::QuadrantSettings settings{};
                        const float average = (total_count > 0) ? (total / static_cast<float>(total_count)) : 0.0f;
                        const float left_avg = (left_count > 0) ? (left_sum / static_cast<float>(left_count)) : average;
                        const float right_avg = (right_count > 0) ? (right_sum / static_cast<float>(right_count)) : average;
                        const float top_avg = (top_count > 0) ? (top_sum / static_cast<float>(top_count)) : average;
                        const float bottom_avg = (bottom_count > 0) ? (bottom_sum / static_cast<float>(bottom_count)) : average;

                        const float grad_x = std::clamp(right_avg - left_avg, -1.0f, 1.0f);
                        const float grad_y = std::clamp(bottom_avg - top_avg, -1.0f, 1.0f);

                        settings.base_light = std::clamp(average, 0.0f, 1.0f);
                        const float darkness = std::clamp(1.0f - settings.base_light, 0.0f, 1.0f);
                        settings.opacity = std::clamp(0.25f + darkness * 0.75f, 0.0f, 1.0f);
                        settings.scale = std::clamp(1.0f + darkness * 0.5f, 0.75f, 2.5f);
                        settings.offset.x = grad_x * offset_scale;
                        settings.offset.y = -grad_y * offset_scale;

                        virtual_light_map_.quadrants[static_cast<std::size_t>(quad_index)] = settings;
                }
        }

}
