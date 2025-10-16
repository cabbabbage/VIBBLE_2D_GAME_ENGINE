#include "light_map.hpp"
#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include <cmath>
#include <limits>

namespace {

float compute_luminance(const SDL_Color& color) {
        // Rec. 709 luminance approximation.
        return (0.2126f * static_cast<float>(color.r) +
                0.7152f * static_cast<float>(color.g) +
                0.0722f * static_cast<float>(color.b)) / 255.0f;
}

} // namespace

SDL_Rect VirtualLightMap::GridMetrics::cell_bounds(int gx, int gy) const {
        SDL_Rect rect{};
        rect.x = static_cast<int>(std::lround(static_cast<float>(gx) * cell_width));
        rect.y = static_cast<int>(std::lround(static_cast<float>(gy) * cell_height));
        rect.w = static_cast<int>(std::lround(cell_width));
        rect.h = static_cast<int>(std::lround(cell_height));
        return rect;
}

SDL_FPoint VirtualLightMap::GridMetrics::cell_center(int gx, int gy) const {
        return SDL_FPoint{ (static_cast<float>(gx) + 0.5f) * cell_width,
                           (static_cast<float>(gy) + 0.5f) * cell_height };
}

SDL_FPoint VirtualLightMap::GridMetrics::screen_to_grid(float x, float y) const {
        return SDL_FPoint{ x * inv_cell_width, y * inv_cell_height };
}

std::optional<VirtualLightMap::GridMetrics> VirtualLightMap::grid_metrics() const {
        if (screen_width <= 0 || screen_height <= 0) {
                return std::nullopt;
        }
        GridMetrics metrics{};
        metrics.cell_width      = static_cast<float>(screen_width) / static_cast<float>(kGridWidth);
        metrics.cell_height     = static_cast<float>(screen_height) / static_cast<float>(kGridHeight);
        metrics.inv_cell_width  = (metrics.cell_width > 0.0f) ? (1.0f / metrics.cell_width) : 0.0f;
        metrics.inv_cell_height = (metrics.cell_height > 0.0f) ? (1.0f / metrics.cell_height) : 0.0f;
        if (metrics.cell_width <= 0.0f || metrics.cell_height <= 0.0f) {
                return std::nullopt;
        }
        return metrics;
}

std::optional<VirtualLightMap::GridCoord> VirtualLightMap::locate_index(int index) const {
        if (index < 0 || index >= kQuadrantCount) {
                return std::nullopt;
        }
        const auto metrics = grid_metrics();
        if (!metrics) {
                return std::nullopt;
        }
        const int gx = index % kGridWidth;
        const int gy = index / kGridWidth;
        GridCoord coord{};
        coord.x      = gx;
        coord.y      = gy;
        coord.index  = index;
        coord.bounds = metrics->cell_bounds(gx, gy);
        coord.center = metrics->cell_center(gx, gy);
        coord.bounds.w = std::min(coord.bounds.w, screen_width - coord.bounds.x);
        coord.bounds.h = std::min(coord.bounds.h, screen_height - coord.bounds.y);
        return coord;
}

std::optional<VirtualLightMap::GridCoord> VirtualLightMap::locate_screen_point(float x, float y) const {
        const auto metrics = grid_metrics();
        if (!metrics) {
                return std::nullopt;
        }
        if (x < 0.0f || y < 0.0f ||
            x >= static_cast<float>(screen_width) ||
            y >= static_cast<float>(screen_height)) {
                return std::nullopt;
        }
        SDL_FPoint grid_pos = metrics->screen_to_grid(x, y);
        int gx = static_cast<int>(std::floor(grid_pos.x));
        int gy = static_cast<int>(std::floor(grid_pos.y));
        gx = std::clamp(gx, 0, kGridWidth - 1);
        gy = std::clamp(gy, 0, kGridHeight - 1);
        return locate_index(gy * kGridWidth + gx);
}

std::optional<VirtualLightMap::GridCoord> VirtualLightMap::locate_world_point(SDL_Point world,
                                                                              const camera& view) const {
        SDL_Point screen = view.map_to_screen(world);
        return locate_screen_point(static_cast<float>(screen.x), static_cast<float>(screen.y));
}

SDL_Rect VirtualLightMap::quadrant_bounds(int index) const {
        auto coord = locate_index(index);
        if (!coord) {
                return SDL_Rect{0, 0, 0, 0};
        }
        return coord->bounds;
}

int VirtualLightMap::quadrant_for_point(float x, float y) const {
        auto coord = locate_screen_point(x, y);
        return coord ? coord->index : -1;
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
        SDL_SetTextureBlendMode(fullscreen_texture_, SDL_BLENDMODE_MOD);
        SDL_SetTextureAlphaMod(fullscreen_texture_, 255);
        SDL_SetTextureColorMod(fullscreen_texture_, 255, 255, 255);
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
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
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
        SDL_SetTextureBlendMode(fullscreen_texture_, SDL_BLENDMODE_MOD);
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

        const std::size_t cell_count = static_cast<std::size_t>(VirtualLightMap::kGridWidth) *
                                       static_cast<std::size_t>(VirtualLightMap::kGridHeight);
        cell_brightness_accum_.assign(cell_count, 0.0f);
        cell_sample_counts_.assign(cell_count, 0);

        const float inv_screen_w = (screen_width_ > 0)
                                        ? static_cast<float>(VirtualLightMap::kGridWidth) /
                                                  static_cast<float>(screen_width_)
                                        : 0.0f;
        const float inv_screen_h = (screen_height_ > 0)
                                        ? static_cast<float>(VirtualLightMap::kGridHeight) /
                                                  static_cast<float>(screen_height_)
                                        : 0.0f;

        for (int y = 0; y < screen_height_; ++y) {
                const int gy = std::clamp(static_cast<int>(std::floor(static_cast<float>(y) * inv_screen_h)),
                                           0,
                                           VirtualLightMap::kGridHeight - 1);
                for (int x = 0; x < screen_width_; ++x) {
                        const int gx = std::clamp(static_cast<int>(std::floor(static_cast<float>(x) * inv_screen_w)),
                                                   0,
                                                   VirtualLightMap::kGridWidth - 1);
                        const std::size_t cell_index = VirtualLightMap::index_of(gx, gy);
                        const Uint32 pixel = pixel_buffer_[static_cast<std::size_t>(y) * screen_width_ +
                                                          static_cast<std::size_t>(x)];
                        Uint8 r = 0;
                        Uint8 g = 0;
                        Uint8 b = 0;
                        Uint8 a = 0;
                        SDL_GetRGBA(pixel, capture_format_, &r, &g, &b, &a);
                        SDL_Color color{r, g, b, a};
                        const float brightness = compute_luminance(color) *
                                                 (static_cast<float>(a) / 255.0f);
                        cell_brightness_accum_[cell_index] += brightness;
                        cell_sample_counts_[cell_index] += 1;
                }
        }

        for (std::size_t idx = 0; idx < cell_count; ++idx) {
                const int count = cell_sample_counts_[idx];
                auto& cell = virtual_light_map_.cell_by_index(idx);
                const float averaged = (count > 0)
                        ? std::clamp(cell_brightness_accum_[idx] / static_cast<float>(count), 0.0f, 1.0f)
                        : 0.0f;
                cell.brightness = averaged;
                cell.opacity    = 0.0f;
                cell.offset_x   = 0.0f;
                cell.offset_y   = 0.0f;
                cell.scale      = 1.0f;
        }

        const render_pipeline::shading::ReactiveShadowSettings default_settings =
                render_pipeline::shading::sanitize_reactive_shadow_settings({});
        render_pipeline::shading::ReactiveShadowSettings vlm_settings = default_settings;
        if (const auto* reactive_settings = assets_ ? assets_->reactive_shadow_settings() : nullptr) {
                vlm_settings = render_pipeline::shading::sanitize_reactive_shadow_settings(*reactive_settings);
        }

        const auto metrics = virtual_light_map_.grid_metrics();
        const float cell_width  = metrics ? metrics->cell_width : 1.0f;
        const float cell_height = metrics ? metrics->cell_height : 1.0f;

        const float map_light_factor = std::clamp(vlm_settings.virtual_light_map.map_light_factor, 0.0f, 1.0f);
        const float attenuation      = std::clamp(1.0f - map_light_factor, 0.0f, 1.0f);

        const Global_Light_Source* map_light = assets_ ? assets_->map_light_source() : nullptr;
        if (map_light && metrics && attenuation < 1.0f) {
                SDL_Point camera_center = assets_->getView().get_screen_center();
                SDL_Point light_pos     = map_light->get_position();

                auto clamp_grid = [](float value, int max_index) {
                        const float max_value = static_cast<float>(max_index) - 1e-4f;
                        return std::clamp(value, 0.0f, max_value);
                };

                SDL_FPoint start = metrics->screen_to_grid(static_cast<float>(camera_center.x),
                                                           static_cast<float>(camera_center.y));
                SDL_FPoint end   = metrics->screen_to_grid(static_cast<float>(light_pos.x),
                                                           static_cast<float>(light_pos.y));

                start.x = clamp_grid(start.x, VirtualLightMap::kGridWidth);
                start.y = clamp_grid(start.y, VirtualLightMap::kGridHeight);
                end.x   = clamp_grid(end.x, VirtualLightMap::kGridWidth);
                end.y   = clamp_grid(end.y, VirtualLightMap::kGridHeight);

                auto attenuate_cell = [&](int gx, int gy) {
                        if (gx < 0 || gy < 0 ||
                            gx >= VirtualLightMap::kGridWidth || gy >= VirtualLightMap::kGridHeight) {
                                return;
                        }
                        auto& cell = virtual_light_map_.cell(gx, gy);
                        cell.brightness = std::clamp(cell.brightness * attenuation, 0.0f, 1.0f);
                };

                auto trace_grid = [&](SDL_FPoint s, SDL_FPoint e) {
                        const float dx = e.x - s.x;
                        const float dy = e.y - s.y;

                        int x = static_cast<int>(std::floor(s.x));
                        int y = static_cast<int>(std::floor(s.y));
                        const int end_x = static_cast<int>(std::floor(e.x));
                        const int end_y = static_cast<int>(std::floor(e.y));

                        const int step_x = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
                        const int step_y = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);

                        auto safe_div = [](float numerator, float denominator) {
                                if (denominator == 0.0f) {
                                        return std::numeric_limits<float>::infinity();
                                }
                                return numerator / denominator;
                        };

                        float next_boundary_x = (step_x > 0)
                                ? (static_cast<float>(x) + 1.0f)
                                : static_cast<float>(x);
                        float next_boundary_y = (step_y > 0)
                                ? (static_cast<float>(y) + 1.0f)
                                : static_cast<float>(y);

                        float t_max_x = (step_x != 0)
                                ? safe_div(next_boundary_x - s.x, dx)
                                : std::numeric_limits<float>::infinity();
                        float t_max_y = (step_y != 0)
                                ? safe_div(next_boundary_y - s.y, dy)
                                : std::numeric_limits<float>::infinity();

                        const float t_delta_x = (step_x != 0)
                                ? std::abs(safe_div(1.0f, dx))
                                : std::numeric_limits<float>::infinity();
                        const float t_delta_y = (step_y != 0)
                                ? std::abs(safe_div(1.0f, dy))
                                : std::numeric_limits<float>::infinity();

                        attenuate_cell(x, y);

                        while (x != end_x || y != end_y) {
                                if (t_max_x < t_max_y) {
                                        t_max_x += t_delta_x;
                                        x += step_x;
                                } else {
                                        t_max_y += t_delta_y;
                                        y += step_y;
                                }
                                attenuate_cell(x, y);
                        }
                };

                trace_grid(start, end);
        }

        const float horizontal_falloff = std::max(vlm_settings.virtual_light_map.horizontal_falloff, 0.0f);
        const float vertical_falloff   = std::max(vlm_settings.virtual_light_map.vertical_falloff, 0.0f);
        const float max_offset_x       = std::max(vlm_settings.virtual_light_map.max_offset_x, 0.0f);
        const float max_offset_y       = std::max(vlm_settings.virtual_light_map.max_offset_y, 0.0f);
        const float cell_shadow_scale  = std::max(vlm_settings.virtual_light_map.shadow_scale, 0.0f);

        auto weight_for_delta = [&](float dx, float dy) {
                const float weight_x = (horizontal_falloff > 0.0f)
                        ? std::exp(-std::fabs(dx) * horizontal_falloff)
                        : 1.0f;
                const float weight_y = (vertical_falloff > 0.0f)
                        ? std::exp(-std::fabs(dy) * vertical_falloff)
                        : 1.0f;
                return weight_x * weight_y;
        };

        for (int y = 0; y < VirtualLightMap::kGridHeight; ++y) {
                for (int x = 0; x < VirtualLightMap::kGridWidth; ++x) {
                        auto& cell = virtual_light_map_.cell(x, y);

                        float opacity_sum    = 0.0f;
                        float opacity_weight = 0.0f;
                        for (int sample_y = y; sample_y < VirtualLightMap::kGridHeight; ++sample_y) {
                                for (int sample_x = 0; sample_x < VirtualLightMap::kGridWidth; ++sample_x) {
                                        const auto& sample = virtual_light_map_.cell(sample_x, sample_y);
                                        const float dx = static_cast<float>(sample_x - x);
                                        const float dy = static_cast<float>(sample_y - y);
                                        const float weight = weight_for_delta(dx, dy);
                                        opacity_sum += weight * sample.brightness;
                                        opacity_weight += weight;
                                }
                        }

                        const float normalized_opacity = (opacity_weight > 0.0f)
                                ? std::clamp(opacity_sum / opacity_weight, 0.0f, 1.0f)
                                : 0.0f;
                        cell.opacity = normalized_opacity;

                        float offset_weight = 0.0f;
                        float offset_x_sum  = 0.0f;
                        float offset_y_sum  = 0.0f;
                        for (int sample_y = 0; sample_y < VirtualLightMap::kGridHeight; ++sample_y) {
                                for (int sample_x = 0; sample_x < VirtualLightMap::kGridWidth; ++sample_x) {
                                        const auto& sample = virtual_light_map_.cell(sample_x, sample_y);
                                        if (sample.brightness <= 0.0f) {
                                                continue;
                                        }
                                        const float dx = static_cast<float>(sample_x - x);
                                        const float dy = static_cast<float>(sample_y - y);
                                        const float weight = weight_for_delta(dx, dy) * sample.brightness;
                                        if (weight <= 0.0f) {
                                                continue;
                                        }
                                        offset_weight += weight;
                                        offset_x_sum  += weight * dx;
                                        offset_y_sum  += weight * dy;
                                }
                        }

                        const float offset_scale_x = (offset_weight > 0.0f)
                                ? (offset_x_sum / offset_weight) * cell_width
                                : 0.0f;
                        const float offset_scale_y = (offset_weight > 0.0f)
                                ? (offset_y_sum / offset_weight) * cell_height
                                : 0.0f;

                        cell.offset_x = std::clamp(offset_scale_x, -max_offset_x, max_offset_x);
                        cell.offset_y = std::clamp(offset_scale_y, -max_offset_y, max_offset_y);
                        cell.scale    = cell_shadow_scale;
                }
        }

}
