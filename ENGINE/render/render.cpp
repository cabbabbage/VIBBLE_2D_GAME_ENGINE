#include "render/render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <SDL_image.h>

#include "animation_update/animation_update.hpp"
#include "animation_update/child_attachment_math.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"
#include "asset/asset_types.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/depth_cue_settings.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "render/camera_grid.hpp"
#include "tiling/grid_tile.hpp"
#include "utils/log.hpp"
#include "utils/grid.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

////////////////////////////////////////////////////////////////////////////////
// Global_Light_Source implementation
////////////////////////////////////////////////////////////////////////////////



using json = nlohmann::json;

namespace {

SDL_Color ranged_color_to_sdl(const utils::color::RangedColor& range) {
    return utils::color::resolve_ranged_color(range);
}

std::optional<utils::color::RangedColor> first_key_color(const json& data) {
    auto keys_it = data.find("keys");
    if (keys_it == data.end() || !keys_it->is_array() || keys_it->empty()) {
        return std::nullopt;
    }
    const auto& entry = (*keys_it)[0];
    if (!entry.is_array() || entry.size() < 2) {
        return std::nullopt;
    }
    return utils::color::ranged_color_from_json(entry[1]);
}

} // namespace

Global_Light_Source::Global_Light_Source(SDL_Renderer* renderer,
                                         SDL_Point screen_center,
                                         int /*screen_width*/,
                                         SDL_Color fallback_base_color)
: renderer_(renderer),
  screen_center_(screen_center) {
    set_defaults(fallback_base_color);
}

void Global_Light_Source::set_defaults(SDL_Color fallback_base_color) {
    base_color_range_ = utils::color::RangedColor{
        {fallback_base_color.r, fallback_base_color.r},
        {fallback_base_color.g, fallback_base_color.g},
        {fallback_base_color.b, fallback_base_color.b},
        {fallback_base_color.a, fallback_base_color.a}
    };
    base_color_      = clamp_color_alpha(fallback_base_color);
    current_color_   = base_color_;
    light_brightness = current_color_.a;
}

bool Global_Light_Source::load_from_map_manifest(const json& map_info, std::string_view map_id) {
    if (!map_info.is_object()) {
        std::cerr << "[MapLight] Map manifest for '" << map_id << "' is not an object. Using defaults.\n";
        return false;
    }

    auto it = map_info.find("map_light_data");
    if (it == map_info.end() || !it->is_object()) {
        if (!map_id.empty()) {
            std::cerr << "[MapLight] Manifest for '" << map_id << "' has no valid map_light_data object. Using defaults.\n";
        } else {
            std::cerr << "[MapLight] Manifest has no valid map_light_data object. Using defaults.\n";
        }
        return false;
    }

    apply_config(*it);
    return true;
}

bool Global_Light_Source::initialize_from_map_manifest(const json& map_info, std::string_view map_id) {
    if (!load_from_map_manifest(map_info, map_id)) {
        current_color_   = base_color_;
        light_brightness = current_color_.a;
        return false;
    }
    return true;
}

void Global_Light_Source::apply_config(const json& data) {
    if (!data.is_object()) {
        return;
    }

    SDL_Color resolved = resolve_color_from_config(data);
    base_color_        = clamp_color_alpha(resolved);
    current_color_     = base_color_;
    base_color_range_ = utils::color::RangedColor{
        {base_color_.r, base_color_.r},
        {base_color_.g, base_color_.g},
        {base_color_.b, base_color_.b},
        {base_color_.a, base_color_.a}
    };
    light_brightness = current_color_.a;
}

SDL_Color Global_Light_Source::resolve_color_from_config(const json& data) const {
    if (auto base = utils::color::ranged_color_from_json(data.value("base_color", json{}))) {
        return clamp_color_alpha(ranged_color_to_sdl(*base));
    }
    if (auto key_color = first_key_color(data)) {
        return clamp_color_alpha(ranged_color_to_sdl(*key_color));
    }
    return current_color_;
}

SDL_Color Global_Light_Source::get_current_color() const {
    if (alpha_override_.has_value()) {
        SDL_Color c = current_color_;
        c.a = clamp_alpha(*alpha_override_);
        return c;
    }
    return current_color_;
}

int Global_Light_Source::get_brightness() const {
    return light_brightness;
}

void Global_Light_Source::set_alpha_override(std::optional<Uint8> alpha) {
    if (alpha.has_value()) {
        alpha_override_ = clamp_alpha(*alpha);
    } else {
        alpha_override_.reset();
    }
}

Uint8 Global_Light_Source::clamp_alpha(Uint8 value) const {
    const Uint8 min_alpha = 0;
    const Uint8 max_alpha = 255;
    if (value < min_alpha) {
        return min_alpha;
    }
    if (value > max_alpha) {
        return max_alpha;
    }
    return value;
}

SDL_Color Global_Light_Source::clamp_color_alpha(SDL_Color color) const {
    color.a = clamp_alpha(color.a);
    return color;
}


////////////////////////////////////////////////////////////////////////////////
// GridTileRenderer implementation
////////////////////////////////////////////////////////////////////////////////

void GridTileRenderer::render(SDL_Renderer* renderer) {
    if (!renderer || !assets_) return;
    render(renderer, assets_->getView(), assets_->world_grid());
}

void GridTileRenderer::render(SDL_Renderer* renderer, const camera_grid& cam, const world::Grid& grid) {
    if (!renderer) return;

    const auto& chunks = grid.active_chunks();
    if (chunks.empty()) return;

    // Depth-cue effects are intentionally not applied to tiles.

    const SDL_Color white{255, 255, 255, 255};
    int indices[6] = {0, 1, 2, 0, 2, 3};

    for (const world::Chunk* chunk : chunks) {
        if (!chunk) continue;
        for (const auto& tile : chunk->tiles) {
            if (!tile.texture || tile.world_rect.w <= 0 || tile.world_rect.h <= 0) continue;

            SDL_Point world_tl{ tile.world_rect.x, tile.world_rect.y };
            SDL_Point world_tr{ tile.world_rect.x + tile.world_rect.w, tile.world_rect.y };
            SDL_Point world_br{ tile.world_rect.x + tile.world_rect.w, tile.world_rect.y + tile.world_rect.h };
            SDL_Point world_bl{ tile.world_rect.x, tile.world_rect.y + tile.world_rect.h };

            SDL_FPoint screen_tl = grid.floor_warped_screen_position(cam, world_tl);
            SDL_FPoint screen_tr = grid.floor_warped_screen_position(cam, world_tr);
            SDL_FPoint screen_br = grid.floor_warped_screen_position(cam, world_br);
            SDL_FPoint screen_bl = grid.floor_warped_screen_position(cam, world_bl);

            // Drop degenerate quads from extreme warping/parallax.
            const float area_doubled =
                (screen_tr.x - screen_tl.x) * (screen_bl.y - screen_tl.y) -
                (screen_bl.x - screen_tl.x) * (screen_tr.y - screen_tl.y);
            if (std::fabs(area_doubled) < 1e-5f) {
                continue;
            }

            int tex_w_int = 0, tex_h_int = 0;
            if (SDL_QueryTexture(tile.texture, nullptr, nullptr, &tex_w_int, &tex_h_int) != 0) {
                continue;
            }
            const float tex_w = static_cast<float>(tex_w_int);
            const float tex_h = static_cast<float>(tex_h_int);
            if (tex_w <= 0.0f || tex_h <= 0.0f) {
                continue;
            }
            const float padding_x = 0.5f / tex_w;
            const float padding_y = 0.5f / tex_h;

            const float tx0 = padding_x;
            const float ty0 = padding_y;
            const float tx1 = 1.0f - padding_x;
            const float ty1 = 1.0f - padding_y;

            SDL_Vertex vertices[4]{};
            vertices[0].position = SDL_FPoint{ screen_tl.x, screen_tl.y };
            vertices[1].position = SDL_FPoint{ screen_tr.x, screen_tr.y };
            vertices[2].position = SDL_FPoint{ screen_br.x, screen_br.y };
            vertices[3].position = SDL_FPoint{ screen_bl.x, screen_bl.y };
            vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = white;
            vertices[0].tex_coord = SDL_FPoint{ tx0, ty0 };
            vertices[1].tex_coord = SDL_FPoint{ tx1, ty0 };
            vertices[2].tex_coord = SDL_FPoint{ tx1, ty1 };
            vertices[3].tex_coord = SDL_FPoint{ tx0, ty1 };

            SDL_RenderGeometry(renderer, tile.texture, vertices, 4, indices, 6);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////
// AssetLightRenderer implementation
////////////////////////////////////////////////////////////////////////////////




namespace {
constexpr float kTwoPi       = 6.28318530718f;
constexpr int   kRadialSteps = 12;

SDL_Rect clamp_rect_to_bounds(const SDL_Rect& rect, int width, int height) {
    SDL_Rect clamped = rect;
    const int min_x  = std::max(rect.x, 0);
    const int min_y  = std::max(rect.y, 0);
    const int max_x  = std::min(rect.x + rect.w, width);
    const int max_y  = std::min(rect.y + rect.h, height);
    clamped.x        = min_x;
    clamped.y        = min_y;
    clamped.w        = std::max(0, max_x - min_x);
    clamped.h        = std::max(0, max_y - min_y);
    return clamped;
}

SDL_BlendMode mask_alpha_multiply_blend() {
    static SDL_BlendMode cached = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,
                                                             SDL_BLENDFACTOR_SRC_ALPHA,
                                                             SDL_BLENDOPERATION_ADD,
                                                             SDL_BLENDFACTOR_ZERO,
                                                             SDL_BLENDFACTOR_SRC_ALPHA,
                                                             SDL_BLENDOPERATION_ADD);
    return cached;
}
}

AssetLightRenderer::AssetLightRenderer(SDL_Renderer* renderer,
                                       const runtime_lighting::AssetLight& source,
                                       std::vector<SDL_Vertex>& scratch_vertices,
                                       std::vector<int>& scratch_indices,
                                       float light_visibility,
                                       float flicker_time_seconds)
    : renderer_(renderer),
      source_(source),
      asset_(source.asset),
      scratch_vertices_(scratch_vertices),
      scratch_indices_(scratch_indices),
      overlay_visibility_(std::clamp(light_visibility, 0.0f, 1.0f)),
      flicker_time_seconds_(std::isfinite(flicker_time_seconds) ? flicker_time_seconds : 0.0f) {
    if (!renderer_ || !asset_ || !asset_->info) {
        return;
    }

    const auto& lights = asset_->info->light_sources;
    if (lights.empty()) {
        return;
    }
    lights_ = &lights;

    const float base_width  = static_cast<float>(std::max(1, source.base_width));
    const float base_height = static_cast<float>(std::max(1, source.base_height));
    scale_x_                = std::isfinite(static_cast<float>(source.asset_rect.w) / base_width)
                               ? static_cast<float>(source.asset_rect.w) / base_width
                               : 1.0f;
    const float scale_y_base = std::isfinite(static_cast<float>(source.asset_rect.h) / base_height)
                                   ? static_cast<float>(source.asset_rect.h) / base_height
                                   : scale_x_;
    scale_y_                = (source.base_height > 0) ? scale_y_base : scale_x_;
    if (!std::isfinite(scale_x_) || !std::isfinite(scale_y_)) {
        lights_ = nullptr;
        return;
    }

    const float safe_base_scale =
        (std::isfinite(source.asset_base_scale) && source.asset_base_scale > 0.0f)
            ? source.asset_base_scale
            : 1.0f;
    const float zoom_scale_x = scale_x_ / safe_base_scale;
    const float zoom_scale_y = scale_y_ / safe_base_scale;
    safe_zoom_scale_x_       = (std::isfinite(zoom_scale_x) && zoom_scale_x > 0.0f) ? zoom_scale_x : 1.0f;
    safe_zoom_scale_y_       = (std::isfinite(zoom_scale_y) && zoom_scale_y > 0.0f) ? zoom_scale_y : 1.0f;

    center_base_x_ = static_cast<float>(source.asset_rect.x) + static_cast<float>(source.asset_rect.w) * 0.5f;
    center_base_y_ = static_cast<float>(source.asset_rect.y + source.asset_rect.h);

    valid_ = true;
}

AssetLightRenderer::~AssetLightRenderer() {
    if (mask_composite_texture_) {
        SDL_DestroyTexture(mask_composite_texture_);
        mask_composite_texture_ = nullptr;
    }
}

bool AssetLightRenderer::prepare_light(const LightSource& light, ComputedLight& out) const {
    const int raw_radius = light.radius;
    if (raw_radius <= 0) {
        return false;
    }

    int intensity = std::clamp(light.intensity, 0, 255);
    if (intensity <= 0) {
        return false;
    }

    const float flicker_multiplier = compute_flicker_multiplier(light);
    intensity = static_cast<int>(std::lround(static_cast<float>(intensity) * flicker_multiplier));
    intensity = std::clamp(intensity, 0, 255);
    if (intensity <= 0) {
        return false;
    }

    const float radius_base = static_cast<float>(std::max(1, raw_radius));
    const float radius_x    = std::max(1.0f, radius_base * safe_zoom_scale_x_);
    const float radius_y    = std::max(1.0f, radius_base * safe_zoom_scale_y_);
    if (!std::isfinite(radius_x) || !std::isfinite(radius_y)) {
        return false;
    }

    const float offset_x = static_cast<float>(source_.flipped ? -light.offset_x : light.offset_x);
    const float offset_y = static_cast<float>(light.offset_y);
    const float center_x = center_base_x_ + offset_x * scale_x_;
    const float center_y = center_base_y_ + offset_y * scale_y_;

    SDL_Rect dst{};
    dst.w = std::max(1, static_cast<int>(std::lround(radius_x * 2.0f)));
    dst.h = std::max(1, static_cast<int>(std::lround(radius_y * 2.0f)));
    dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(dst.w) * 0.5f));
    dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(dst.h) * 0.5f));

    const float falloff_norm  = std::clamp(static_cast<float>(light.fall_off) / 100.0f, 0.0f, 1.0f);
    const float fade_exponent = 0.6f + 3.4f * falloff_norm;

    out.source        = &light;
    out.intensity     = intensity;
    out.center_x      = center_x;
    out.center_y      = center_y;
    out.radius_x      = radius_x;
    out.radius_y      = radius_y;
    out.bounds        = dst;
    out.fade_exponent = fade_exponent;
    out.textured      = false;
    out.texture_dst   = SDL_Rect{0, 0, 0, 0};

    const float width_f  = static_cast<float>(std::max(1, source_.asset_rect.w));
    const float height_f = static_cast<float>(std::max(1, source_.asset_rect.h));
    out.center_ratio_x   = (center_x - static_cast<float>(source_.asset_rect.x)) / width_f;
    out.center_ratio_y   = (center_y - static_cast<float>(source_.asset_rect.y)) / height_f;
    out.radius_ratio_x   = radius_x / width_f;
    out.radius_ratio_y   = radius_y / height_f;

    if (light.texture) {
        int base_w = light.cached_w;
        int base_h = light.cached_h;
        if (base_w <= 0 || base_h <= 0) {
            SDL_QueryTexture(light.texture, nullptr, nullptr, &base_w, &base_h);
        }
        if (base_w <= 0 || base_h <= 0) {
            base_w = static_cast<int>(std::lround(radius_base * 2.0f));
            base_h = static_cast<int>(std::lround(radius_base * 2.0f));
        }

        const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(base_w) * safe_zoom_scale_x_)));
        const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(base_h) * safe_zoom_scale_y_)));

        SDL_Rect tex_dst{};
        tex_dst.w = scaled_w;
        tex_dst.h = scaled_h;
        tex_dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(tex_dst.w) * 0.5f));
        tex_dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(tex_dst.h) * 0.5f));

        out.textured        = true;
        out.texture_dst     = tex_dst;
        out.texture_ratio_x = (static_cast<float>(tex_dst.x) - static_cast<float>(source_.asset_rect.x)) / width_f;
        out.texture_ratio_y = (static_cast<float>(tex_dst.y) - static_cast<float>(source_.asset_rect.y)) / height_f;
        out.texture_ratio_w = static_cast<float>(tex_dst.w) / width_f;
        out.texture_ratio_h = static_cast<float>(tex_dst.h) / height_f;
    }

    return true;
}

void AssetLightRenderer::draw_pass(Pass pass) {
    if (!valid_ || !lights_) {
        return;
    }

    if (overlay_visibility_ <= 0.0f) {
        return;
    }

    SDL_Texture* original_target = SDL_GetRenderTarget(renderer_);

    for (const LightSource& light : *lights_) {
        // Render lights only on passes they are configured for.
        // Mask-only lights with no explicit front/behind flags default to the behind pass.
        const bool mask_only_light = light.render_front_and_back_to_asset_alpha_mask &&
                                     !light.in_front && !light.behind;
        const bool should_render_in_behind_pass = light.behind || mask_only_light;
        if ((pass == Pass::kBehind && !should_render_in_behind_pass) ||
            (pass == Pass::kFront  && !light.in_front)) {
            continue;
        }

        ComputedLight computed{};
        if (!prepare_light(light, computed)) {
            continue;
        }

        computed.intensity = static_cast<int>(
            std::lround(static_cast<float>(computed.intensity) * overlay_visibility_));
        computed.intensity = std::clamp(computed.intensity, 0, 255);
        if (computed.intensity <= 0) {
            continue;
        }

        SDL_Color base_color = computed.source ? computed.source->color : SDL_Color{255, 255, 255, 255};

        bool handled_with_mask = false;
        if (light.render_front_and_back_to_asset_alpha_mask) {
            handled_with_mask = render_light_with_asset_mask(light, computed, base_color);
        }

        if (handled_with_mask) {
            if (original_target != SDL_GetRenderTarget(renderer_)) {
                SDL_SetRenderTarget(renderer_, original_target);
            }
            continue;
        }

        if (original_target != SDL_GetRenderTarget(renderer_)) {
            SDL_SetRenderTarget(renderer_, original_target);
        }

        if (computed.textured && computed.source && computed.source->texture) {
            render_textured_light(computed, computed.texture_dst);
            continue;
        }

        render_radial_light(computed,
                            base_color,
                            1.0f,
                            computed.center_x,
                            computed.center_y,
                            computed.radius_x,
                            computed.radius_y,
                            computed.bounds);
    }

    if (original_target != SDL_GetRenderTarget(renderer_)) {
        SDL_SetRenderTarget(renderer_, original_target);
    }
}

void AssetLightRenderer::render_textured_light(const ComputedLight& info, const SDL_Rect& dst) {
    if (!info.source || !info.source->texture || dst.w <= 0 || dst.h <= 0) {
        return;
    }

    SDL_Texture* tex = info.source->texture;
    Uint8        prev_a = 255;
    Uint8        prev_r = 255;
    Uint8        prev_g = 255;
    Uint8        prev_b = 255;
    SDL_BlendMode prev_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureAlphaMod(tex, &prev_a);
    SDL_GetTextureColorMod(tex, &prev_r, &prev_g, &prev_b);
    SDL_GetTextureBlendMode(tex, &prev_blend);

    SDL_Color draw_color{255, 255, 255, 255};
    if (info.source) {
        draw_color = info.source->color;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(tex, draw_color.r, draw_color.g, draw_color.b);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(info.intensity));
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(tex, prev_a);
    SDL_SetTextureColorMod(tex, prev_r, prev_g, prev_b);
    SDL_SetTextureBlendMode(tex, prev_blend);
}

void AssetLightRenderer::render_radial_light(const ComputedLight& info,
                                             const SDL_Color&     base_color,
                                             float                alpha_scale,
                                             float                center_x,
                                             float                center_y,
                                             float                radius_x,
                                             float                radius_y,
                                             const SDL_Rect&      fallback_rect) {
    if (!(radius_x > 0.0f) || !(radius_y > 0.0f)) {
        return;
    }

    const float radius_hint   = std::max(radius_x, radius_y);
    const int   angular_steps = std::clamp(static_cast<int>(std::ceil(radius_hint / 6.0f)), 16, 64);

    const std::size_t desired_vertex_capacity =
        static_cast<std::size_t>((kRadialSteps + 1) * (angular_steps + 1));
    const std::size_t desired_index_capacity =
        static_cast<std::size_t>(kRadialSteps * angular_steps * 6);

    scratch_vertices_.clear();
    scratch_indices_.clear();
    if (desired_vertex_capacity > scratch_vertices_.capacity()) {
        scratch_vertices_.reserve(desired_vertex_capacity);
    }
    if (desired_index_capacity > scratch_indices_.capacity()) {
        scratch_indices_.reserve(desired_index_capacity);
    }

    for (int ring = 0; ring <= kRadialSteps; ++ring) {
        const float ring_ratio = static_cast<float>(ring) / static_cast<float>(kRadialSteps);
        const float base       = std::max(0.0f, 1.0f - ring_ratio);
        float       alpha_ratio = std::pow(base, info.fade_exponent);
        alpha_ratio             = std::clamp(alpha_ratio, 0.0f, 1.0f);
        const float scaled_alpha = std::min(255.0f, static_cast<float>(info.intensity) * alpha_ratio * alpha_scale);
        const Uint8 alpha        = static_cast<Uint8>(std::clamp(std::lround(scaled_alpha), 0L, 255L));

        for (int step = 0; step <= angular_steps; ++step) {
            const float angle = (static_cast<float>(step) / static_cast<float>(angular_steps)) * kTwoPi;
            const float px    = center_x + std::cos(angle) * radius_x * ring_ratio;
            const float py    = center_y + std::sin(angle) * radius_y * ring_ratio;

            SDL_Vertex vertex{};
            vertex.position.x = px;
            vertex.position.y = py;
            vertex.color      = SDL_Color{ base_color.r, base_color.g, base_color.b, alpha };
            vertex.tex_coord  = SDL_FPoint{ 0.0f, 0.0f };
            scratch_vertices_.push_back(vertex);
        }
    }

    const int stride = angular_steps + 1;
    for (int ring = 0; ring < kRadialSteps; ++ring) {
        for (int step = 0; step < angular_steps; ++step) {
            const int current = ring * stride + step;
            const int next    = current + stride;

            scratch_indices_.push_back(current);
            scratch_indices_.push_back(next);
            scratch_indices_.push_back(current + 1);

            scratch_indices_.push_back(current + 1);
            scratch_indices_.push_back(next);
            scratch_indices_.push_back(next + 1);
        }
    }

    if (SDL_RenderGeometry(renderer_, nullptr,
                           scratch_vertices_.data(), static_cast<int>(scratch_vertices_.size()),
                           scratch_indices_.data(), static_cast<int>(scratch_indices_.size())) != 0) {
        const Uint8 fallback_alpha = static_cast<Uint8>(std::clamp(
            std::lround(static_cast<float>(info.intensity) * alpha_scale), 0L, 255L));
        SDL_SetRenderDrawColor(renderer_, base_color.r, base_color.g, base_color.b, fallback_alpha);
        SDL_RenderFillRect(renderer_, &fallback_rect);
    }
}

bool AssetLightRenderer::render_light_with_asset_mask(const LightSource& light,
                                                      const ComputedLight& computed,
                                                      const SDL_Color& base_color) {
    (void)light;
    if (!renderer_ || !asset_) {
        return false;
    }

    SDL_Texture* mask = asset_->get_current_mask_texture();
    if (!mask) {
        return false;
    }

    Uint32 mask_format = 0;
    int    mask_w      = 0;
    int    mask_h      = 0;
    if (SDL_QueryTexture(mask, &mask_format, nullptr, &mask_w, &mask_h) != 0 || mask_w <= 0 || mask_h <= 0) {
        return false;
    }

    SDL_Texture* composite = ensure_mask_composite_texture(mask_w, mask_h, mask_format);
    if (!composite) {
        return false;
    }

    SDL_Texture* saved_target = SDL_GetRenderTarget(renderer_);
    if (SDL_SetRenderTarget(renderer_, composite) != 0) {
        SDL_SetRenderTarget(renderer_, saved_target);
        return false;
    }

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    SDL_Rect mask_space_rect{0, 0, mask_w, mask_h};
    SDL_Rect light_rect{0, 0, 0, 0};

    if (computed.textured && computed.source && computed.source->texture) {
        SDL_Rect dst = computed.texture_dst;
        dst.x = static_cast<int>(std::lround(computed.texture_ratio_x * static_cast<float>(mask_space_rect.w)));
        dst.y = static_cast<int>(std::lround(computed.texture_ratio_y * static_cast<float>(mask_space_rect.h)));
        dst.w = std::max(1, static_cast<int>(std::lround(computed.texture_ratio_w * static_cast<float>(mask_space_rect.w))));
        dst.h = std::max(1, static_cast<int>(std::lround(computed.texture_ratio_h * static_cast<float>(mask_space_rect.h))));
        render_textured_light(computed, dst);
        light_rect = dst;
    } else {
        const float center_x = computed.center_ratio_x * static_cast<float>(mask_space_rect.w);
        const float center_y = computed.center_ratio_y * static_cast<float>(mask_space_rect.h);
        const float radius_x = computed.radius_ratio_x * static_cast<float>(mask_space_rect.w);
        const float radius_y = computed.radius_ratio_y * static_cast<float>(mask_space_rect.h);

        SDL_Rect bounds{};
        bounds.w = std::max(1, static_cast<int>(std::lround(radius_x * 2.0f)));
        bounds.h = std::max(1, static_cast<int>(std::lround(radius_y * 2.0f)));
        bounds.x = static_cast<int>(std::lround(center_x - static_cast<float>(bounds.w) * 0.5f));
        bounds.y = static_cast<int>(std::lround(center_y - static_cast<float>(bounds.h) * 0.5f));

        render_radial_light(computed,
                            base_color,
                            1.0f,
                            center_x,
                            center_y,
                            radius_x,
                            radius_y,
                            bounds);
        light_rect = bounds;
    }

    SDL_Rect clipped_src = clamp_rect_to_bounds(light_rect, mask_space_rect.w, mask_space_rect.h);

    if (clipped_src.w <= 0 || clipped_src.h <= 0) {
        SDL_SetRenderTarget(renderer_, saved_target);
        return true;
    }

    SDL_BlendMode prev_mask_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureBlendMode(mask, &prev_mask_blend);
    Uint8 prev_r = 255;
    Uint8 prev_g = 255;
    Uint8 prev_b = 255;
    Uint8 prev_a = 255;
    SDL_GetTextureColorMod(mask, &prev_r, &prev_g, &prev_b);
    SDL_GetTextureAlphaMod(mask, &prev_a);

    SDL_SetTextureColorMod(mask, 255, 255, 255);
    SDL_SetTextureAlphaMod(mask, 255);
    SDL_SetTextureBlendMode(mask, mask_alpha_multiply_blend());
    SDL_RenderCopy(renderer_, mask, &clipped_src, &clipped_src);
    SDL_SetTextureBlendMode(mask, prev_mask_blend);
    SDL_SetTextureColorMod(mask, prev_r, prev_g, prev_b);
    SDL_SetTextureAlphaMod(mask, prev_a);

    SDL_SetRenderTarget(renderer_, saved_target);

    SDL_Rect dst_rect = scale_mask_rect_to_asset(clipped_src, mask_space_rect.w, mask_space_rect.h);
    if (dst_rect.w <= 0 || dst_rect.h <= 0) {
        return true;
    }

    SDL_RenderCopy(renderer_, composite, &clipped_src, &dst_rect);
    return true;
}

float AssetLightRenderer::compute_flicker_multiplier(const LightSource& light) const {
    const float speed_setting =
        std::clamp(static_cast<float>(light.flicker_speed), 0.0f, 100.0f) / 100.0f;
    const float smooth_setting =
        std::clamp(static_cast<float>(light.flicker_smoothness), 0.0f, 100.0f) / 100.0f;

    if (speed_setting <= 0.001f) {
        return 1.0f;
    }

    // Lightweight hash combiner (deterministic per-light)
    auto mix = [](std::uint32_t seed, std::uint32_t value) {
        seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    };

    auto to_rand = [](std::uint32_t h) {
        return static_cast<float>(h & 0xFFFFu) / 32767.5f - 1.0f; // [-1, 1]
    };

    // 1D value-noise with smootherstep interpolation
    auto value_noise_1d = [&](float t, std::uint32_t seed) {
        if (!(std::isfinite(t))) return 0.0f;
        const int   i   = static_cast<int>(std::floor(t));
        const float f   = t - static_cast<float>(i);
        const float f2  = f * f;
        const float f3  = f2 * f;
        const float u   = f3 * (f * (f * 6.0f - 15.0f) + 10.0f); // smootherstep
        const float a   = to_rand(mix(seed, static_cast<std::uint32_t>(i)));
        const float b   = to_rand(mix(seed, static_cast<std::uint32_t>(i + 1)));
        return a + (b - a) * u;
    };

    // Per-light seed
    std::uint32_t base = 0x811C9DC5u;
    base = mix(base, static_cast<std::uint32_t>(light.offset_x));
    base = mix(base, static_cast<std::uint32_t>(light.offset_y));
    base = mix(base, static_cast<std::uint32_t>(light.radius));
    base = mix(base, static_cast<std::uint32_t>(light.intensity));
    base = mix(base, static_cast<std::uint32_t>(light.fall_off));
    base = mix(base, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(light.texture) & 0xFFFFu));

    // Base rate in samples/sec; increases with speed setting
    const float base_rate = 0.4f + 6.0f * speed_setting;

    // Octave frequencies (incommensurate multipliers) and per-octave seeds
    const float f0 = base_rate * 1.00f;
    const float f1 = base_rate * 2.17f;
    const float f2 = base_rate * 3.73f;
    const std::uint32_t s0 = mix(base, 0xA1B2C3D4u);
    const std::uint32_t s1 = mix(base, 0xBEEF1234u);
    const std::uint32_t s2 = mix(base, 0xDEADBEEFu);

    // Smoothness reduces high-frequency contribution
    float w0 = 0.6f + 0.3f * smooth_setting;          // 0.6 .. 0.9
    float w1 = 0.3f * (1.0f - 0.5f * smooth_setting); // 0.15 .. 0.3
    float w2 = 0.1f * (1.0f - smooth_setting);        // 0.0 .. 0.1
    float wsum = std::max(1e-6f, w0 + w1 + w2);
    w0 /= wsum; w1 /= wsum; w2 /= wsum;

    const float t = flicker_time_seconds_;
    float n0 = value_noise_1d(t * f0, s0);
    float n1 = value_noise_1d(t * f1, s1);
    float n2 = value_noise_1d(t * f2, s2);
    float noise = w0 * n0 + w1 * n1 + w2 * n2;

    // Optional micro-jitter when smoothness is low
    if (smooth_setting < 0.5f) {
        const float jitter_rate = 70.0f + 260.0f * speed_setting;
        const float jt = t * jitter_rate + static_cast<float>((base >> 8) & 0xFFu) * 0.013f;
        const int   ji = static_cast<int>(std::floor(jt));
        const float jf = jt - static_cast<float>(ji);
        const float u  = jf * jf * (3.0f - 2.0f * jf); // smoothstep
        const float ja = to_rand(mix(base, static_cast<std::uint32_t>(ji)));
        const float jb = to_rand(mix(base, static_cast<std::uint32_t>(ji + 1)));
        const float j  = ja + (jb - ja) * u;
        const float jitter_amp = (0.1f + 0.15f * speed_setting) * (1.0f - smooth_setting);
        noise = std::clamp(noise * (1.0f - jitter_amp) + j * jitter_amp, -1.0f, 1.0f);
    }

    // Map noise to brightness multiplier
    const float amplitude  = 0.12f + 0.45f * speed_setting;
    const float multiplier = 1.0f + std::clamp(noise, -1.0f, 1.0f) * amplitude;
    return std::clamp(multiplier, 0.2f, 1.0f + amplitude);
}

SDL_Texture* AssetLightRenderer::ensure_mask_composite_texture(int width, int height, Uint32 format_hint) {
    if (width <= 0 || height <= 0 || !renderer_) {
        return nullptr;
    }

    if (mask_composite_texture_) {
        if (mask_composite_w_ != width || mask_composite_h_ != height ||
            (format_hint != 0 && mask_composite_format_ != format_hint)) {
            SDL_DestroyTexture(mask_composite_texture_);
            mask_composite_texture_ = nullptr;
            mask_composite_w_       = 0;
            mask_composite_h_       = 0;
            mask_composite_format_  = 0;
        }
    }

    if (!mask_composite_texture_) {
        Uint32 format = format_hint ? format_hint : SDL_PIXELFORMAT_RGBA8888;
        mask_composite_texture_ = SDL_CreateTexture(renderer_, format, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!mask_composite_texture_ && format != SDL_PIXELFORMAT_RGBA8888) {
            format = SDL_PIXELFORMAT_RGBA8888;
            mask_composite_texture_ =
                SDL_CreateTexture(renderer_, format, SDL_TEXTUREACCESS_TARGET, width, height);
        }
        if (!mask_composite_texture_) {
            return nullptr;
        }
        SDL_SetTextureBlendMode(mask_composite_texture_, SDL_BLENDMODE_BLEND);
        mask_composite_w_      = width;
        mask_composite_h_      = height;
        mask_composite_format_ = format;
    }

    return mask_composite_texture_;
}

SDL_Rect AssetLightRenderer::scale_mask_rect_to_asset(const SDL_Rect& rect, int mask_width, int mask_height) const {
    SDL_Rect result{0, 0, 0, 0};
    if (mask_width <= 0 || mask_height <= 0) {
        return result;
    }

    SDL_Rect asset_rect = source_.asset_rect;
    const float scale_x = static_cast<float>(asset_rect.w) / static_cast<float>(mask_width);
    const float scale_y = static_cast<float>(asset_rect.h) / static_cast<float>(mask_height);

    result.x = asset_rect.x + static_cast<int>(std::lround(static_cast<float>(rect.x) * scale_x));
    result.y = asset_rect.y + static_cast<int>(std::lround(static_cast<float>(rect.y) * scale_y));
    result.w = std::max(0, static_cast<int>(std::lround(static_cast<float>(rect.w) * scale_x)));
    result.h = std::max(0, static_cast<int>(std::lround(static_cast<float>(rect.h) * scale_y)));
    return result;
}

AssetLightRenderer::DarkMaskResult AssetLightRenderer::accumulate_dark_mask() {
    DarkMaskResult result{};
    if (!valid_ || !lights_) {
        return result;
    }

    for (const LightSource& light : *lights_) {
        if (!light.render_to_dark_mask) {
            continue;
        }
        ComputedLight computed{};
        if (!prepare_light(light, computed)) {
            continue;
        }

        const float radius_hint   = std::max(computed.radius_x, computed.radius_y);
        const int   angular_steps = std::clamp(static_cast<int>(std::ceil(radius_hint / 6.0f)), 16, 64);
        const std::size_t desired_vertex_capacity =
            static_cast<std::size_t>((kRadialSteps + 1) * (angular_steps + 1));
        const std::size_t desired_index_capacity =
            static_cast<std::size_t>(kRadialSteps * angular_steps * 6);
        result.max_vertices = std::max(result.max_vertices, desired_vertex_capacity);
        result.max_indices  = std::max(result.max_indices, desired_index_capacity);

        SDL_Color base_color{0, 0, 0, 255};
        render_radial_light(computed, base_color, 1.6f,
                            computed.center_x, computed.center_y,
                            computed.radius_x, computed.radius_y,
                            computed.bounds);
    }

    return result;
}

void AssetLightRenderer::draw_behind() { draw_pass(Pass::kBehind); }

void AssetLightRenderer::draw_in_front() { draw_pass(Pass::kFront); }



////////////////////////////////////////////////////////////////////////////////
// SceneRenderer core render loop
////////////////////////////////////////////////////////////////////////////////


static constexpr float kDefaultMinVisibleScreenRatio = 0.015f;

namespace {
constexpr std::string_view kUpdateMapLightSettingKey = "dev_ui.lighting.map_panel.update_map_light";

constexpr float kDepthCueDeadzonePx = 1.5f;

enum class DepthCuePlane {
    None = 0,
    Foreground,
    Background
};

struct DepthCueSample {
    DepthCuePlane plane = DepthCuePlane::None;
    float         t     = 0.0f;
};

float evaluate_depth_curve(camera_grid::BlurFalloffMethod method, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (method) {
        case camera_grid::BlurFalloffMethod::Quadratic:
            return t * t;
        case camera_grid::BlurFalloffMethod::Cubic:
            return t * t * t;
        case camera_grid::BlurFalloffMethod::Logarithmic: {
            const float k = 4.0f;
            const float num = std::log1p(k * t);
            const float den = std::log1p(k);
            return (den > 0.0f) ? (num / den) : t;
        }
        case camera_grid::BlurFalloffMethod::Exponential: {
            const float k = 3.0f;
            const float num = std::exp(k * t) - 1.0f;
            const float den = std::exp(k) - 1.0f;
            return (den > 0.0f) ? (num / den) : t;
        }
        case camera_grid::BlurFalloffMethod::Linear:
        default:
            return t;
    }
}

DepthCueSample make_depthcue_sample(float screen_y,
                                    float center_screen_y,
                                    float fg_plane_screen_y,
                                    float bg_plane_screen_y,
                                    float fg_span_screen,
                                    float bg_span_screen) {
    DepthCueSample sample;
    if (!std::isfinite(screen_y)) {
        return sample;
    }
    if (std::fabs(screen_y - center_screen_y) <= kDepthCueDeadzonePx) {
        return sample;
    }
    if (screen_y > center_screen_y) {
        sample.plane = DepthCuePlane::Foreground;
        sample.t = (screen_y >= fg_plane_screen_y || fg_span_screen <= 0.0f)
            ? 1.0f
            : std::clamp((screen_y - center_screen_y) / fg_span_screen, 0.0f, 1.0f);
    } else if (screen_y < center_screen_y) {
        sample.plane = DepthCuePlane::Background;
        sample.t = (screen_y <= bg_plane_screen_y || bg_span_screen <= 0.0f)
            ? 1.0f
            : std::clamp((center_screen_y - screen_y) / bg_span_screen, 0.0f, 1.0f);
    }
    return sample;
}

float compute_depthcue_opacity(const DepthCueSample& sample,
                               int max_opacity,
                               camera_grid::BlurFalloffMethod method) {
    if (sample.plane == DepthCuePlane::None || max_opacity <= 0) {
        return 0.0f;
    }
    const float curve = evaluate_depth_curve(method, sample.t);
    return curve * static_cast<float>(std::clamp(max_opacity, 0, 255)) / 255.0f;
}

constexpr const char* kEnableChunkLightingEnv  = "VIBBLE_ENABLE_CHUNK_LIGHTING";
constexpr const char* kDisableChunkLightingEnv = "VIBBLE_DISABLE_CHUNK_LIGHTING";

bool env_truthy(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T';
}

bool env_falsey(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F';
}

bool chunk_lighting_suspended_flag() {
    if (env_truthy(std::getenv(kDisableChunkLightingEnv))) {
        return true;
    }
    if (const char* value = std::getenv(kEnableChunkLightingEnv)) {
        if (env_falsey(value)) {
            return true;
        }
        if (env_truthy(value)) {
            return false;
        }
    }
    return false;
}

SDL_BlendMode darkness_cutout_blend_mode() {
    static SDL_BlendMode cached = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    return cached;
}

// Conservative float-rect intersection with small padding to avoid edge popping
static inline bool intersects_padded(const SDL_FRect& a,
                                     const SDL_FRect& b,
                                     float pad_px = 2.0f) {
    const float ax0 = a.x - pad_px;
    const float ay0 = a.y - pad_px;
    const float ax1 = a.x + a.w + pad_px;
    const float ay1 = a.y + a.h + pad_px;
    const float bx0 = b.x;
    const float by0 = b.y;
    const float bx1 = b.x + b.w;
    const float by1 = b.y + b.h;
    return !(ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0);
}

// Ensure reusable render-target textures exist and match the screen size
static void ensure_scene_targets(SDL_Renderer* renderer,
                                 int width,
                                 int height,
                                 SDL_Texture*& composite,
                                 SDL_Texture*& postprocess,
                                 SDL_Texture*& blurtex) {
    auto ensure_target = [&](SDL_Texture*& tex) {
        int tw = 0, th = 0; Uint32 fmt = 0; int access = 0;
        if (tex && SDL_QueryTexture(tex, &fmt, &access, &tw, &th) == 0) {
            if (tw == width && th == height && access == SDL_TEXTUREACCESS_TARGET) {
                return; // ok
            }
        }
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        if (tex) { SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND); }
    };
    auto ensure_streaming = [&](SDL_Texture*& tex) {
        int tw = 0, th = 0; Uint32 fmt = 0; int access = 0;
        if (tex && SDL_QueryTexture(tex, &fmt, &access, &tw, &th) == 0) {
            if (tw == width && th == height && access == SDL_TEXTUREACCESS_STREAMING) {
                return; // ok
            }
        }
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (tex) { SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND); }
    };
    ensure_target(composite);
    ensure_streaming(postprocess);
    ensure_streaming(blurtex);
}

bool animation_frame_belongs_to_animation(const Animation& anim, const AnimationFrame* frame) {
    if (!frame) {
        return false;
    }
    const std::uintptr_t needle = reinterpret_cast<std::uintptr_t>(frame);
    const std::size_t path_count = anim.movement_path_count();
    for (std::size_t path_index = 0; path_index < path_count; ++path_index) {
        const auto& path = anim.movement_path(path_index);
        if (path.empty()) {
            continue;
        }
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(path.data());
        const std::uintptr_t end   = reinterpret_cast<std::uintptr_t>(path.data() + path.size());
        if (needle >= begin && needle < end) {
            return true;
        }
    }
    return false;
}

}

SceneRenderer::SceneRenderer(SDL_Renderer* renderer,
                             Assets* assets,
                             int screen_width,
                             int screen_height,
                             const nlohmann::json& map_manifest,
                             const std::string& map_id)
: SceneRenderer(require_prerequisites(renderer, assets),
                renderer,
                assets,
                screen_width,
                screen_height,
                map_manifest,
                map_id) {}

SceneRenderer::PrevalidatedTag SceneRenderer::require_prerequisites(SDL_Renderer* renderer, Assets* assets) {
    std::string reason;
    if (!SceneRenderer::prerequisites_ready(renderer, assets, &reason)) {
        const std::string message = reason.empty() ? "SceneRenderer prerequisites missing." : reason;
        vibble::log::error(std::string{"[SceneRenderer] Initialization aborted: "} + message);
        if (!renderer) { SDL_assert(renderer != nullptr); }
        if (!assets)   { SDL_assert(assets != nullptr); }
        throw std::invalid_argument(message);
    }
    return PrevalidatedTag{};
}

SceneRenderer::SceneRenderer(PrevalidatedTag,
                             SDL_Renderer* renderer,
                             Assets* assets,
                             int screen_width,
                             int screen_height,
                             const nlohmann::json& map_manifest,
                             const std::string& map_id)
: renderer_(renderer),
  assets_(assets),
  screen_width_(screen_width),
  screen_height_(screen_height),
  main_light_source_(renderer_, SDL_Point{ screen_width / 2, screen_height / 2 },
                     screen_width, SDL_Color{255, 255, 255, 255}),
  render_pipeline_(renderer_,
                    SceneLighting{ assets_->getView(),
                                   main_light_source_,
                                   assets_->player,
                                   nullptr,
                                   &assets_->world_grid() }),
  update_map_light_enabled_(devmode::ui_settings::load_bool(kUpdateMapLightSettingKey, true)),
  sky_texture_path_(std::filesystem::path("SRC") / "misc_content" / "sky.png")
{
    vibble::log::debug(std::string{"[SceneRenderer] Initializing for map '"} + map_id +
                       "' with screen " + std::to_string(screen_width_) + "x" + std::to_string(screen_height_) + ".");


    // Allow override of warmup frames via env var (optional): VIBBLE_DEPTHCUE_WARMUP_FRAMES
    if (const char* override_frames = std::getenv("VIBBLE_DEPTHCUE_WARMUP_FRAMES")) {
        const int v = std::atoi(override_frames);
        if (v >= 0 && v <= 120) {
            depthcue_warmup_frames_ = static_cast<std::uint32_t>(v);
        }
    }
    if (map_manifest.is_object()) {
        auto it = map_manifest.find("map_light_data");
        if (it != map_manifest.end() && it->is_object()) {
            map_clear_color_ = utils::color::resolve_ranged_color(
                it->value("map_color", nlohmann::json{}),
                SDL_Color{0, 0, 0, 255});
        }
    }
    main_light_source_.initialize_from_map_manifest(map_manifest, map_id);
    chunk_lighting_suspended_ = chunk_lighting_suspended_flag();
    light_map_ = std::make_unique<LightMap>(assets_, screen_width_, screen_height_);
    if (chunk_lighting_suspended_) {
        vibble::log::info("[SceneRenderer] Chunk lighting suspended; skipping light-map initialization.");
        render_pipeline_.lighting().light_map_sampler = nullptr;
    } else if (light_map_) {
        initialize_static_light_chunks();
        light_map_->rebuild(renderer_);
        render_pipeline_.lighting().light_map_sampler = light_map_.get();
    } else {
        render_pipeline_.lighting().light_map_sampler = nullptr;
    }
    tile_renderer_ = std::make_unique<GridTileRenderer>(assets_);
}

SceneRenderer::~SceneRenderer() {
    destroy_darkness_overlay();
    destroy_sky_texture();

    if (scene_composite_tex_) { SDL_DestroyTexture(scene_composite_tex_); scene_composite_tex_ = nullptr; }
    if (postprocess_tex_)     { SDL_DestroyTexture(postprocess_tex_);     postprocess_tex_     = nullptr; }
    if (blur_tex_)            { SDL_DestroyTexture(blur_tex_);            blur_tex_            = nullptr; }
}

SDL_Renderer* SceneRenderer::get_renderer() const { return renderer_; }

void SceneRenderer::set_update_map_light_enabled(bool enabled) {
    update_map_light_enabled_ = enabled;
}

void SceneRenderer::set_dark_mask_enabled(bool enabled) {
    if (dark_mask_enabled_ == enabled) {
        return;
    }
    dark_mask_enabled_ = enabled;
    if (!dark_mask_enabled_) {
        destroy_darkness_overlay();
    }
}

LightMap* SceneRenderer::light_map() {
    return light_map_ ? light_map_.get() : nullptr;
}

const LightMap* SceneRenderer::light_map() const {
    return const_cast<SceneRenderer*>(this)->light_map();
}

bool SceneRenderer::initialize_static_light_chunks() {
    if (!assets_) {
        vibble::log::debug("[SceneRenderer] Skipping light initialization (assets unavailable).");
        return false;
    }

    world::Grid& grid = assets_->world_grid();
    std::vector<world::Chunk*> chunks = grid.all_chunks();
    if (chunks.empty()) {
        vibble::log::info("[SceneRenderer] No map chunks detected; light initialization skipped.");
        return false;
    }

    bool initialized_chunks = false;
    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        chunk->releaseLightingArtifacts();
        chunk->lighting.static_strength          = 1.0f;
        chunk->lighting.dynamic_strength         = 1.0f;
        chunk->lighting.current_strength         = 1.0f;
        chunk->lighting.runtime_average_strength = 1.0f;
        chunk->lighting.runtime_average_color    = SDL_Color{255, 255, 255, 255};
        chunk->lighting.needs_update             = true;
        initialized_chunks                      = true;
    }

    return initialized_chunks;
}

void SceneRenderer::set_low_quality_rendering(bool enabled){
    if (low_quality_rendering_==enabled) return;
    low_quality_rendering_=enabled;
    render_pipeline_.set_low_quality_mode(enabled);
}

void SceneRenderer::apply_map_light_config(const nlohmann::json& data){
    main_light_source_.apply_config(data);
    map_clear_color_ = utils::color::resolve_ranged_color(
        data.value("map_color", nlohmann::json{}),
        SDL_Color{0, 0, 0, 255});

}

bool SceneRenderer::shouldRegen(Asset* a){
    if (!a) return false;

    if (a->is_shaded || a->is_shading_group_set()) {
        return true;
    }

    SDL_Texture* final_texture = a->get_final_texture();
    if (!final_texture) {
        return true;
    }

    const bool locked = a->is_current_animation_locked_in_progress();
    const bool treat_static = a->static_frame || locked;
    if (treat_static) {
        return false;
    }

    const AnimationFrame* current_frame = a->current_frame;
    const AnimationFrame* last_frame    = a->last_rendered_frame();
    if (!last_frame) {
        return true;
    }

    return last_frame != current_frame;
}

SDL_FRect SceneRenderer::get_scaled_position_rect(Asset* a,int fw,int fh,float inv_scale,int min_w,int min_h,float ref_sh){
    float world_x = a ? a->smoothed_translation_x() : 0.0f;
    float world_y = a ? a->smoothed_translation_y() : 0.0f;

    // In dev mode, bypass translation smoothing so editor drags are visible immediately.
    if (assets_ && assets_->is_dev_mode()) {
        world_x = a ? static_cast<float>(a->pos.x) : 0.0f;
        world_y = a ? static_cast<float>(a->pos.y) : 0.0f;
    }

    float base_scale = 1.0f;
    if (a) {
        base_scale = a->smoothed_scale();
        if (!std::isfinite(base_scale) || base_scale <= 0.0f) {
            base_scale = 1.0f;
        }
    }
    const float scaled_fw = static_cast<float>(fw) * base_scale;
    const float scaled_fh = static_cast<float>(fh) * base_scale;
    const float base_sw   = scaled_fw * inv_scale;
    const float base_sh   = scaled_fh * inv_scale;

    camera_grid& cam = assets_->getView();
    const SDL_Point world_point{
        static_cast<int>(std::lround(world_x)),
        static_cast<int>(std::lround(world_y))
    };
    const float horizon_y = cam.horizon_screen_y_for_scale();
    const float bottom_limit = static_cast<float>(screen_height_) + 4000.0f;

    float center_x = 0.0f;
    float center_y = 0.0f;
    float distance_scale = 1.0f;
    float vertical_scale = 1.0f;

    world::GridPoint* gp = (assets_ && a) ? assets_->getView().grid_point_for_asset(a) : nullptr;
    if (gp) {
        distance_scale = (a && a->info && a->info->apply_distance_scaling) ? gp->distance_scale : 1.0f;
        vertical_scale = (a && a->info && a->info->apply_vertical_scaling) ? gp->vertical_scale : 1.0f;
        center_x = gp->screen.x;
        center_y = gp->screen.y;
        if (assets_ && assets_->player == a) {
            center_x -= gp->parallax_dx;
        }
    } else {
        const camera_grid::RenderSmoothingKey smoothing_key = a ?
            camera_grid::RenderSmoothingKey(a) : camera_grid::RenderSmoothingKey();
        camera_grid::RenderEffects ef = cam.compute_render_effects(
            world_point,
            base_sh,
            ref_sh,
            smoothing_key);
        if (!std::isfinite(ef.screen_position.y) ||
            ef.screen_position.y < horizon_y - 0.5f ||
            ef.screen_position.y > bottom_limit + 1.0f) {
            return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
        }
        center_x = ef.screen_position.x;
        center_y = ef.screen_position.y;
        if (assets_) {
            if (!(a && assets_->player == a)) {
                world::Grid& grid = assets_->world_grid();
                center_x = grid.parallax_adjusted_screen_x(world_point, center_x);
            }
        }
        distance_scale  = (a && a->info && a->info->apply_distance_scaling) ? ef.distance_scale : 1.0f;
        vertical_scale  = (a && a->info && a->info->apply_vertical_scaling) ? ef.vertical_scale : 1.0f;
    }

    if (!std::isfinite(center_y) ||
        center_y < horizon_y - 0.5f ||
        center_y > bottom_limit + 1.0f) {
        return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    }

    const float scaled_sw = base_sw * distance_scale;
    const float scaled_sh2 = base_sh * distance_scale;
    const float final_h = scaled_sh2 * vertical_scale;

    const float min_w_f = static_cast<float>(min_w);
    const float min_h_f = static_cast<float>(min_h);
    float width  = scaled_sw;
    float height = final_h;

    if (width <= min_w_f || height <= min_h_f) {
        return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    }

    width  = std::max(width, 1.0f);
    height = std::max(height, 1.0f);

    const float left     = center_x - width * 0.5f;
    const float top      = center_y - height;

    return SDL_FRect{ left, top, width, height };
}

SDL_FRect SceneRenderer::get_child_position_rect(const Asset* parent,
                                                 SDL_Point world_point,
                                                 int fw,
                                                 int fh,
                                                 float inv_scale,
                                                 int min_w,
                                                 int min_h,
                                                 float reference_screen_height) {
    if (!parent || fw <= 0 || fh <= 0) {
        return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    float base_scale = parent->smoothed_scale();
    if (!std::isfinite(base_scale) || base_scale <= 0.0f) {
        base_scale = 1.0f;
    }
    const float scaled_fw = static_cast<float>(fw) * base_scale;
    const float scaled_fh = static_cast<float>(fh) * base_scale;
    const float base_sw   = scaled_fw * inv_scale;
    const float base_sh   = scaled_fh * inv_scale;

    camera_grid& cam = assets_->getView();
    const float horizon_y = cam.horizon_screen_y_for_scale();
    const float bottom_limit = static_cast<float>(screen_height_) + 4000.0f;

    float center_x = 0.0f;
    float center_y = 0.0f;
    const bool apply_distance = parent->info && parent->info->apply_distance_scaling;
    const bool apply_vertical = parent->info && parent->info->apply_vertical_scaling;
    float distance_scale = 1.0f;
    float vertical_scale = 1.0f;

    world::GridPoint* gp = assets_ ? assets_->getView().grid_point_for_asset(parent) : nullptr;
    if (gp) {
        distance_scale = apply_distance ? gp->distance_scale : 1.0f;
        vertical_scale = apply_vertical ? gp->vertical_scale : 1.0f;
        center_x = gp->screen.x;
        center_y = gp->screen.y;
        if (assets_ && assets_->player == parent) {
            center_x -= gp->parallax_dx;
        }
        const float dx = static_cast<float>(world_point.x - parent->pos.x) * inv_scale * distance_scale;
        const float dy = static_cast<float>(world_point.y - parent->pos.y) * inv_scale;
        center_x += dx;
        center_y += dy;
    } else {
        const camera_grid::RenderSmoothingKey smoothing_key =
            camera_grid::RenderSmoothingKey(parent);
        camera_grid::RenderEffects ef = cam.compute_render_effects(
            world_point,
            base_sh,
            reference_screen_height,
            smoothing_key);
        if (!std::isfinite(ef.screen_position.y) ||
            ef.screen_position.y < horizon_y - 0.5f ||
            ef.screen_position.y > bottom_limit + 1.0f) {
            return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
        }
        center_x = ef.screen_position.x;
        center_y = ef.screen_position.y;
        if (assets_ && assets_->player != parent) {
            world::Grid& grid = assets_->world_grid();
            center_x = grid.parallax_adjusted_screen_x(world_point, center_x);
        }
        distance_scale = apply_distance ? ef.distance_scale : 1.0f;
        vertical_scale = apply_vertical ? ef.vertical_scale : 1.0f;
    }

    if (!std::isfinite(center_y) ||
        center_y < horizon_y - 0.5f ||
        center_y > bottom_limit + 1.0f) {
        return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    }

    float width  = base_sw * distance_scale;
    float height = (base_sh * distance_scale) * vertical_scale;

    const float min_w_f = static_cast<float>(min_w);
    const float min_h_f = static_cast<float>(min_h);
    if (width <= min_w_f || height <= min_h_f) {
        return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    }

    width  = std::max(width, 1.0f);
    height = std::max(height, 1.0f);

    const float left = center_x - width * 0.5f;
    const float top  = center_y - height;

    return SDL_FRect{ left, top, width, height };
}
void SceneRenderer::render(){
    ++frame_counter_;
    // Opportunistically prune tinted texture cache to prevent stale growth


    if (light_map_ && !chunk_lighting_suspended_){
        render_pipeline_.lighting().light_map_sampler = light_map_.get();
    } else {
        render_pipeline_.lighting().light_map_sampler = nullptr;
    }

    const SDL_Color map_light_color = main_light_source_.get_current_color();
    const float     map_light_opacity =
        std::clamp(static_cast<float>(map_light_color.a) / 255.0f, 0.0f, 1.0f);
    const float light_overlay_visibility = map_light_opacity;
    const float frame_flicker_time_seconds =
        static_cast<float>(SDL_GetTicks64() % 1000000ULL) * 0.001f;

    // Apply depth-cue per-asset only; disable full-screen post
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_Color clear_color = light_map_only_mode_ ? SDL_Color{0,0,0,255} : map_clear_color_;
    SDL_SetRenderDrawColor(renderer_, clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    SDL_RenderClear(renderer_);

    bool rendered_light_map = false;
    const bool suppress_asset_lights = (assets_ && assets_->is_dev_mode() && !assets_->is_asset_info_lighting_section_expanded());
    auto render_light_map = [&]() {
        if (!light_map_only_mode_) {
            return;
        }
        if (chunk_lighting_suspended_) {
            return;
        }
        if (!light_map_ || rendered_light_map) {
            return;
        }
        const float alpha_mult = map_light_opacity;

        SDL_Rect screen_view{0, 0, screen_width_, screen_height_};
        SDL_BlendMode previous_mode = SDL_BLENDMODE_BLEND;
        if (SDL_GetRenderDrawBlendMode(renderer_, &previous_mode) != 0) {
            previous_mode = SDL_BLENDMODE_BLEND;
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        light_map_->render_visible_chunks(renderer_, screen_view, alpha_mult, map_light_color);
        SDL_SetRenderDrawBlendMode(renderer_, previous_mode);
        rendered_light_map = true;
    };

    light_overlay_sources_.clear();
    light_overlay_sources_have_dark_mask_cached_ = false;
    light_overlay_sources_dark_mask_cache_dirty_ = false;

    //////////////////////////////////////////////////////////////////////////////////
    // SceneRenderer::render - depth cue and quality setup
    //////////////////////////////////////////////////////////////////////////////////
    if (!light_map_only_mode_){
        const camera_grid* camera_state = assets_ ? &assets_->getView() : nullptr;
        const camera_grid::RealismSettings cam_settings = camera_state
            ? camera_state->realism_settings()
            : camera_grid::RealismSettings{};
        const bool depthcue_setting_enabled = assets_
            ? assets_->depth_effects_enabled()
            : devmode::camera_prefs::load_depthcue_enabled();
        const int effective_quality_percent = assets_
                                                  ? assets_->effective_render_quality_percent() : cam_settings.render_quality_percent;
        const float quality_percent =
            std::clamp(static_cast<float>(effective_quality_percent), 10.0f, 100.0f);
        render_pipeline::ScalingLogic::SetQualityCap(quality_percent / 100.0f);

        float scale = camera_state ? camera_state->get_scale() : 1.0f;
        if (!std::isfinite(scale) || scale <= 0.0f) {
            scale = 1.0f;
        }
        float inv_scale = 1.0f / scale;
        float min_ratio = cam_settings.min_visible_screen_ratio;
        if (!std::isfinite(min_ratio) || min_ratio < 0.0f) {
            min_ratio = kDefaultMinVisibleScreenRatio;
        }
        min_ratio = std::clamp(min_ratio, 0.0f, 0.5f);
        int min_w=(int)std::lround(static_cast<double>(screen_width_)*min_ratio);
        int min_h=(int)std::lround(static_cast<double>(screen_height_)*min_ratio);

        float player_sh=1.f;
        Asset* player=assets_?assets_->player:nullptr;
        if (player){
            SDL_Texture* tf=player->get_final_texture();
            SDL_Texture* fr=player->get_current_frame();
            int pw=player->cached_w, ph=player->cached_h;
            if ((pw==0||ph==0) && tf) SDL_QueryTexture(tf,nullptr,nullptr,&pw,&ph);
            if ((pw==0||ph==0) && fr) SDL_QueryTexture(fr,nullptr,nullptr,&pw,&ph);
            if (pw!=0) player->cached_w=pw;
            if (ph!=0) player->cached_h=ph;
            const float pscale=(player->info && std::isfinite(player->info->scale_factor) && player->info->scale_factor>=0.f) ? player->info->scale_factor : 1.f;
            if (ph>0) player_sh=(float)ph*pscale*inv_scale;
        }
        if (player_sh<=0.f) player_sh=1.f;

        //////////////////////////////////////////////////////////////////////////////////
        // SceneRenderer::render - grid tile pass
        //////////////////////////////////////////////////////////////////////////////////
        // Draw grid tiles first
        if (tile_renderer_) {
            tile_renderer_->render(renderer_, assets_->getView(), assets_->world_grid());
        }

        if (camera_state) {
            render_sky_layer(*camera_state, depthcue_setting_enabled);
        }

        const int fg_max_opacity = std::clamp(cam_settings.foreground_texture_max_opacity, 0, 255);
        const int bg_max_opacity = std::clamp(cam_settings.background_texture_max_opacity, 0, 255);
        const bool cold_start = frame_counter_ <= static_cast<std::uint64_t>(depthcue_warmup_frames_);
        const bool depthcue_values_active = (fg_max_opacity > 0 || bg_max_opacity > 0);

        float center_screen_y = static_cast<float>(screen_height_) * 0.5f;
        float fg_plane_screen_y = std::clamp(cam_settings.foreground_plane_screen_y, 0.0f, static_cast<float>(screen_height_));
        float bg_plane_screen_y = std::clamp(cam_settings.background_plane_screen_y, 0.0f, static_cast<float>(screen_height_));
        if (camera_state) {
            SDL_FPoint center_world_f = camera_state->get_view_center_f();
            SDL_FPoint center_screen_f = camera_state->map_to_screen_f(center_world_f);
            if (std::isfinite(center_screen_f.y)) {
                center_screen_y = std::clamp(center_screen_f.y, 0.0f, static_cast<float>(screen_height_));
            }
        }
        const float fg_span_screen = std::max(0.0f, fg_plane_screen_y - center_screen_y);
        const float bg_span_screen = std::max(0.0f, center_screen_y - bg_plane_screen_y);
        auto depth_sample_for = [&](float screen_y) -> DepthCueSample {
            return make_depthcue_sample(screen_y,
                                        center_screen_y,
                                        fg_plane_screen_y,
                                        bg_plane_screen_y,
                                        fg_span_screen,
                                        bg_span_screen);
        };

        //////////////////////////////////////////////////////////////////////////////////
        // SceneRenderer::render - build asset render commands
        //////////////////////////////////////////////////////////////////////////////////
        const std::vector<world::GridPoint*>& active_points = assets_->active_points();
        active_asset_infos_.clear();
        active_asset_infos_.reserve(active_points.size() * 2 + 16);
        active_asset_order_.clear();
        active_asset_order_.reserve(active_points.size() * 2 + 16);
        for (world::GridPoint* gp : active_points) {
            if (!gp) {
                continue;
            }
            for (const auto& occ_up : gp->occupants) {
                Asset* occupant_asset = occ_up ? occ_up.get() : nullptr;
                if (!occupant_asset) {
                    continue;
                }
                ActiveAssetInfo info;
                info.asset      = occupant_asset;
                info.grid_point = gp;
                info.screen_y   = gp->screen.y;
                info.z_index    = occupant_asset->z_index;
                active_asset_infos_.push_back(info);
            }
        }
        std::stable_sort(active_asset_infos_.begin(), active_asset_infos_.end(), [](const ActiveAssetInfo& lhs, const ActiveAssetInfo& rhs) {
            if (lhs.asset == rhs.asset) {
                return false;
            }
            if (std::fabs(lhs.screen_y - rhs.screen_y) > 0.5f) {
                return lhs.screen_y < rhs.screen_y;
            }
            if (lhs.z_index != rhs.z_index) {
                return lhs.z_index < rhs.z_index;
            }
            return lhs.asset < rhs.asset;
        });
        for (const ActiveAssetInfo& info : active_asset_infos_) {
            if (info.asset) {
                active_asset_order_.push_back(info.asset);
            }
        }
        // Horizon-aware culling rect (float) for perspective/warping
        float horizon_y = camera_state ? camera_state->horizon_screen_y_for_scale() : 0.0f;
        float cull_top = std::max(0.0f, horizon_y);
        float cull_bottom = static_cast<float>(screen_height_) + 4000.0f;
        const SDL_FRect scene_cull_rect{ 0.0f, cull_top, static_cast<float>(screen_width_), cull_bottom - cull_top };

        // Fog rendering removed.
        texture_commands_.clear();
        texture_commands_.reserve(active_asset_order_.size());
        remaining_commands_.clear();
        remaining_commands_.reserve(active_asset_order_.size());
        light_overlay_sources_.reserve(active_asset_order_.size());

        struct ChildRenderBatch {
            bool processed = false;
            bool has_visible_child = false;
            std::vector<AssetRenderCommand> commands_back;
            std::vector<AssetRenderCommand> commands_front;
        };
        std::vector<ChildRenderBatch> child_render_batches(active_asset_order_.size());

        auto ensure_child_commands = [&](Asset* parent, ChildRenderBatch& batch) {
            if (!parent) {
                batch.processed = true;
                batch.has_visible_child = false;
                batch.commands_back.clear();
                batch.commands_front.clear();
                return;
            }
            if (batch.processed) {
                return;
            }
            batch.processed = true;
            batch.has_visible_child = false;
            batch.commands_back.clear();
            batch.commands_front.clear();

            const auto& child_slots = parent->animation_children();
            if (child_slots.empty()) {
                return;
            }

            batch.commands_back.reserve(child_slots.size());
            batch.commands_front.reserve(child_slots.size());

            const AnimationFrame* parent_frame = parent ? parent->current_animation_frame() : nullptr;
            const Animation* parent_anim = nullptr;
            bool parent_frame_valid = false;
            if (parent && parent->info) {
                auto parent_anim_it = parent->info->animations.find(parent->current_animation);
                if (parent_anim_it != parent->info->animations.end()) {
                    parent_anim = &parent_anim_it->second;
                    parent_frame_valid =
                        animation_frame_belongs_to_animation(*parent_anim, parent_frame);
                }
            }

            auto resolve_child_animation = [](Asset::AnimationChildAttachment& slot) -> const Animation* {
                if (slot.animation) {
                    return slot.animation;
                }
                if (!slot.info) {
                    return nullptr;
                }
                auto anim_it = slot.info->animations.find(animation_update::detail::kDefaultAnimation);
                if (anim_it == slot.info->animations.end() && !slot.info->animations.empty()) {
                    anim_it = slot.info->animations.begin();
                }
                if (anim_it != slot.info->animations.end()) {
                    slot.animation = &anim_it->second;
                }
                return slot.animation;
            };

            for (std::size_t child_index = 0; child_index < child_slots.size(); ++child_index) {
                auto& slot = const_cast<Asset::AnimationChildAttachment&>(child_slots[child_index]);
                const Animation* slot_animation = resolve_child_animation(slot);
                const AnimationFrame* frame_ptr = slot.current_frame;

                if (slot.spawned_asset) {
                    batch.has_visible_child = batch.has_visible_child || slot.visible;
                    continue;
                }

                if (slot_animation &&
                    !animation_frame_belongs_to_animation(*slot_animation, frame_ptr)) {
                    // Child animations may be refreshed; rebind to a valid frame so rendering resumes.
                    slot.current_frame = slot_animation->get_first_frame();
                    slot.frame_progress = 0.0f;
                    slot.cached_w = 0;
                    slot.cached_h = 0;
                    frame_ptr = slot.current_frame;
                }

                // If the runtime never pushed the child visibility for the current parent frame,
                // fall back to the parent frame's child list so frame-bound children still render.
                if (!slot.visible && parent_frame_valid && parent_frame) {
                    for (const auto& child_data : parent_frame->children) {
                        if (child_data.child_index != slot.child_index) {
                            continue;
                        }
                        slot.visible = child_data.visible;
                        if (child_data.visible) {
                            slot.render_in_front = child_data.render_in_front;
                            const int dx = parent->flipped ? -child_data.dx : child_data.dx;
                            slot.world_pos = SDL_Point{ parent->pos.x + dx, parent->pos.y + child_data.dy };
                            slot.rotation_degrees = ::mirrored_child_rotation(parent->flipped, child_data.degree);
                            if (!frame_ptr || !slot_animation) {
                                slot_animation = resolve_child_animation(slot);
                                if (slot_animation) {
                                    slot.current_frame = slot_animation->get_first_frame();
                                    slot.frame_progress = 0.0f;
                                    slot.cached_w = 0;
                                    slot.cached_h = 0;
                                    frame_ptr = slot.current_frame;
                                }
                            }
                        }
                        break;
                    }
                }

                if (!slot.visible || !frame_ptr) {
                    //std::cout << "[Render] Skipping child '" << slot.asset_name << "' (visible=" << slot.visible << ", frame_ptr=" << (frame_ptr ? "yes" : "no") << ")\n";
                    continue;
                }
                SDL_Texture* child_tex = frame_ptr->get_base_texture();
                if (!child_tex) {
                    //std::cout << "[Render] No texture for child '" << slot.asset_name << "'\n";
                    continue;
                }

                int child_fw = slot.cached_w;
                int child_fh = slot.cached_h;
                if (child_fw <= 0 || child_fh <= 0) {
                    if (SDL_QueryTexture(child_tex, nullptr, nullptr, &child_fw, &child_fh) == 0 &&
                        child_fw > 0 && child_fh > 0) {
                        slot.cached_w = child_fw;
                        slot.cached_h = child_fh;
                    }
                }
                if (child_fw <= 0 || child_fh <= 0) {
                    if (debugging) {
                        std::cout << "[Render] Invalid child texture size for '" << slot.asset_name << "'\n";
                    }
                    continue;
                }

                SDL_FRect child_rect = get_child_position_rect(parent,
                                                               slot.world_pos,
                                                               child_fw,
                                                               child_fh,
                                                               inv_scale,
                                                               min_w,
                                                               min_h,
                                                               player_sh);
                if (child_rect.w <= 0.0f || child_rect.h <= 0.0f) {
                    if (debugging) {
                        std::cout << "[Render] Child rect not drawable for '" << slot.asset_name << "'\n";
                    }
                    continue;
                }
                if (!intersects_padded(child_rect, scene_cull_rect)) {
                    continue;
                }

                batch.has_visible_child = true;

                SDL_Texture* draw_tex = child_tex;
                if (slot.animation && frame_ptr) {
                    const int frame_index = frame_ptr->frame_index;
                    if (frame_index >= 0) {
                        const auto& steps = slot.animation->variant_steps();
                        if (!steps.empty()) {
                            const float desired = render_pipeline::ScalingLogic::ComputeScale(
                                child_fw,
                                child_fh,
                                static_cast<int>(std::lround(child_rect.w)),
                                static_cast<int>(std::lround(child_rect.h)));
                            auto sel = render_pipeline::ScalingLogic::Choose(desired, steps);
                            if (sel.index >= 0) {
                                if (SDL_Texture* variant = slot.animation->frame_variant(
                                        static_cast<std::size_t>(frame_index),
                                        static_cast<std::size_t>(sel.index))) {
                                    draw_tex = variant;
                                }
                            }
                        }
                    }
                }

                AssetRenderCommand child_cmd;
                child_cmd.asset = parent;
                child_cmd.final_texture = draw_tex;
                child_cmd.source_texture = draw_tex;
                child_cmd.dst = child_rect;
                child_cmd.highlighted = parent->is_highlighted();
                child_cmd.selected = parent->is_selected();
                child_cmd.flipped = parent->flipped;
                child_cmd.alpha = parent ? parent->smoothed_alpha() : 1.0f;
                if (!std::isfinite(child_cmd.alpha)) {
                    child_cmd.alpha = 1.0f;
                }
                child_cmd.alpha = std::clamp(child_cmd.alpha, 0.0f, 1.0f);
                child_cmd.rotation_degrees = slot.rotation_degrees;
                if (std::fabs(slot.rotation_degrees) > std::numeric_limits<float>::epsilon()) {
                    child_cmd.has_custom_pivot = true;
                    child_cmd.rotation_pivot = SDL_FPoint{ child_rect.w * 0.5f, child_rect.h };
                }

                if (slot.render_in_front) {
                    batch.commands_front.push_back(std::move(child_cmd));
                } else {
                    batch.commands_back.push_back(std::move(child_cmd));
                }
            }
        };

        // Build and queue sprite draw commands
        auto should_skip_asset = [&](Asset* asset, ChildRenderBatch& child_batch) -> bool {
            if (!asset || !asset->info) {
                return true;
            }

            int approx_fw = asset->cached_w;
            int approx_fh = asset->cached_h;
            auto try_query_dimensions = [&](SDL_Texture* tex) {
                if (!tex) {
                    return;
                }
                int tw = 0;
                int th = 0;
                if (SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th) != 0) {
                    return;
                }
                if (tw > 0 && th > 0) {
                    approx_fw = tw;
                    approx_fh = th;
                }
            };

            if (approx_fw <= 0 || approx_fh <= 0) {
                try_query_dimensions(asset->get_current_frame());
            }
            if (approx_fw <= 0 || approx_fh <= 0) {
                try_query_dimensions(asset->get_final_texture());
            }
            if (approx_fw <= 0 || approx_fh <= 0) {
                return false;
            }

            SDL_FRect dst = get_scaled_position_rect(asset,
                                                     approx_fw,
                                                     approx_fh,
                                                     inv_scale,
                                                     min_w,
                                                     min_h,
                                                     player_sh);
            if (dst.w <= 0.0f || dst.h <= 0.0f) {
                return true;
            }

            SDL_FRect expanded_bounds{};
            const bool has_expanded_bounds =
                assets_ && assets_->asset_bounds_in_screen_space(asset, expanded_bounds);
            const SDL_FRect& asset_visible_bounds = has_expanded_bounds ? expanded_bounds : dst;
            const bool sprite_visible  = intersects_padded(dst, scene_cull_rect);
            const bool bounds_visible  = intersects_padded(asset_visible_bounds, scene_cull_rect);
            const bool has_light_sources = asset->info && !asset->info->light_sources.empty();

            bool any_child_visible = false;
            if ((!bounds_visible || (!sprite_visible && !has_light_sources)) && asset->info) {
                ensure_child_commands(asset, child_batch);
                any_child_visible = child_batch.has_visible_child;
            }

            if (!bounds_visible && !any_child_visible) {
                return true;
            }
            if (!sprite_visible && !has_light_sources && !any_child_visible) {
                return true;
            }
            return false;
        };

        for (std::size_t asset_index = 0; asset_index < active_asset_order_.size(); ++asset_index) {
            Asset* a = active_asset_order_[asset_index];
            auto& child_batch = child_render_batches[asset_index];
            if (!a || !a->info) {
                continue;
            }

            if (should_skip_asset(a, child_batch)) {
                continue;
            }

            // Tileable assets are composited into grid tiles and should skip the normal sprite pass.
            const auto& tiling_opt      = a->tiling_info();
            const bool  is_chunk_tiled  = tiling_opt && tiling_opt->is_valid();
            const bool  has_light_sources = !a->info->light_sources.empty();

            const bool is_new = a->last_render_frame_id != frame_counter_ - 1;
            if (is_new) {
                SDL_Texture* tex = render_pipeline_.regenerateFinalTexture(a);
                a->set_final_texture(tex);
            } else if (shouldRegen(a)) {
                SDL_Texture* tex = render_pipeline_.regenerateFinalTexture(a);
                a->set_final_texture(tex);
            }

            SDL_Texture* final_tex = a->get_final_texture();
            if (!final_tex) {
                a->reset_last_rendered_frame();
                continue;
            }

            a->last_render_frame_id = frame_counter_;

            int fw = a->cached_w;
            int fh = a->cached_h;
            if (fw == 0 || fh == 0) {
                SDL_QueryTexture(final_tex, nullptr, nullptr, &fw, &fh);
                a->cached_w = fw;
                a->cached_h = fh;
            }

            auto cache_last_frame = [&]() {
                if (a->current_frame) {
                    a->set_last_rendered_frame(a->current_frame);
                } else {
                    a->reset_last_rendered_frame();
                }
            };

            SDL_FRect dst = get_scaled_position_rect(a, fw, fh, inv_scale, min_w, min_h, player_sh);
            if (dst.w <= 0.0f || dst.h <= 0.0f) {
                cache_last_frame();
                continue;
            }

            SDL_FRect expanded_bounds{};
            const bool has_expanded_bounds =
                assets_ && assets_->asset_bounds_in_screen_space(a, expanded_bounds);
            const SDL_FRect& asset_visible_bounds = has_expanded_bounds ? expanded_bounds : dst;
            const bool sprite_visible  = intersects_padded(dst, scene_cull_rect);
            const bool bounds_visible  = intersects_padded(asset_visible_bounds, scene_cull_rect);

            // Evaluate child attachments once so their visibility can keep the parent alive.
            ensure_child_commands(a, child_batch);
            const bool any_child_visible = child_batch.has_visible_child;

            if (!bounds_visible && !any_child_visible) {
                cache_last_frame();
                continue;
            }
            if (!sprite_visible && !has_light_sources && !any_child_visible) {
                cache_last_frame();
                continue;
            }

            for (auto& cmd : child_batch.commands_back) {
                remaining_commands_.push_back(std::move(cmd));
            }

            if (sprite_visible && !is_chunk_tiled) {
                const float hysteresis_margin = camera_state
                    ? camera_state->realism_settings().scale_variant_hysteresis_margin
                    : render_pipeline::ScalingLogic::kDefaultHysteresisMargin;
                SDL_Texture* draw_tex = render_pipeline_.texture_for_scale(
                    a,
                    final_tex,
                    fw,
                    fh,
                    static_cast<int>(std::lround(dst.w)),
                    static_cast<int>(std::lround(dst.h)),
                    hysteresis_margin);
                {
                    AssetRenderCommand cmd;
                    cmd.asset        = a;
                    cmd.final_texture = final_tex;
                    cmd.dst           = dst;
                    // Draw the sprite by default
                    cmd.source_texture      = draw_tex ? draw_tex : final_tex;
                    cmd.uses_scaled_texture = draw_tex && draw_tex != final_tex;
                    cmd.highlighted         = a->is_highlighted();
                    cmd.selected            = a->is_selected();
                    cmd.flipped             = a->flipped;
                    cmd.alpha               = a ? a->smoothed_alpha() : 1.0f;
                    if (!std::isfinite(cmd.alpha)) cmd.alpha = 1.0f;
                    cmd.alpha = std::clamp(cmd.alpha, 0.0f, 1.0f);

                    // Depth-cue overlays
                    cmd.depthcue_foreground_texture = nullptr;
                    cmd.depthcue_background_texture = nullptr;
                    cmd.depthcue_foreground_alpha   = 0;
                    cmd.depthcue_background_alpha   = 0;
                    const bool is_texture_asset = a && a->info && a->info->type == asset_types::texture;
                    bool is_chunk_tiled = false;
                    if (a) {
                        const auto& tiling_opt = a->tiling_info();
                        is_chunk_tiled = (tiling_opt && tiling_opt->is_valid());
                    }
                    const bool is_tillable_asset = a && a->info && a->info->tillable;
                    const bool depthcue_allowed = depthcue_setting_enabled && depthcue_values_active &&
                        !is_texture_asset && !is_chunk_tiled && !is_tillable_asset &&
                        camera_state && camera_state->realism_enabled() && !cold_start;
                    if (depthcue_allowed) {
                        float wx = a ? a->smoothed_translation_x() : 0.0f;
                        float wy = a ? a->smoothed_translation_y() : 0.0f;
                        if (assets_ && assets_->is_dev_mode() && a) {
                            wx = static_cast<float>(a->pos.x);
                            wy = static_cast<float>(a->pos.y);
                        }
                        SDL_FPoint screen_pos = camera_state->map_to_screen_f(SDL_FPoint{ wx, wy });
                        const float screen_y = screen_pos.y;
                        SDL_Texture* fg_overlay = nullptr;
                        SDL_Texture* bg_overlay = nullptr;
                        if (a) {
                            const AnimationFrame* frame_ptr = a->current_animation_frame();
                            if (frame_ptr && a->info) {
                                const auto anim_it = a->info->animations.find(a->current_animation);
                                if (anim_it != a->info->animations.end() &&
                                    animation_frame_belongs_to_animation(anim_it->second, frame_ptr)) {
                                    fg_overlay = frame_ptr->get_foreground_texture();
                                    bg_overlay = frame_ptr->get_background_texture();
                                }
                            }
                        }
                        const DepthCueSample depth_sample = depth_sample_for(screen_y);
                        if (depth_sample.plane != DepthCuePlane::None) {
                            if (depth_sample.plane == DepthCuePlane::Foreground && fg_overlay && fg_max_opacity > 0) {
                                const float normalized = compute_depthcue_opacity(
                                    depth_sample,
                                    fg_max_opacity,
                                    cam_settings.texture_opacity_falloff_method);
                                const int alpha_value = static_cast<int>(std::round(normalized * 255.0f));
                                cmd.depthcue_foreground_alpha = static_cast<Uint8>(std::clamp(alpha_value, 0, 255));
                                if (cmd.depthcue_foreground_alpha > 0) {
                                    cmd.depthcue_foreground_texture = fg_overlay;
                                    try {
                                        vibble::log::debug(std::string{"[SceneRenderer] Assigned FG depthcue tex ptr="} +
                                                           std::to_string(reinterpret_cast<std::uintptr_t>(fg_overlay)) +
                                                           " alpha=" + std::to_string(static_cast<int>(cmd.depthcue_foreground_alpha)));
                                    } catch (...) {}
                                }
                            } else if (depth_sample.plane == DepthCuePlane::Background && bg_overlay && bg_max_opacity > 0) {
                                const float normalized = compute_depthcue_opacity(
                                    depth_sample,
                                    bg_max_opacity,
                                    cam_settings.texture_opacity_falloff_method);
                                const int alpha_value = static_cast<int>(std::round(normalized * 255.0f));
                                cmd.depthcue_background_alpha = static_cast<Uint8>(std::clamp(alpha_value, 0, 255));
                                if (cmd.depthcue_background_alpha > 0) {
                                    cmd.depthcue_background_texture = bg_overlay;
                                    try {
                                        vibble::log::debug(std::string{"[SceneRenderer] Assigned BG depthcue tex ptr="} +
                                                           std::to_string(reinterpret_cast<std::uintptr_t>(bg_overlay)) +
                                                           " alpha=" + std::to_string(static_cast<int>(cmd.depthcue_background_alpha)));
                                    } catch (...) {}
                                }
                            }
                        }
                    }

                    auto& target_commands = (a->info->type == asset_types::texture) ? texture_commands_ : remaining_commands_;
                    target_commands.push_back(std::move(cmd));
                }
            } else if (!suppress_asset_lights && sprite_visible && has_light_sources) {
                // Keep a command placeholder so the lighting system can still sample this asset.
                {
                    AssetRenderCommand cmd;
                    cmd.asset         = a;
                    cmd.final_texture = final_tex;
                    cmd.dst           = dst;
                    // Suppress base sprite draw; keep placeholder for lights
                    cmd.source_texture      = nullptr;
                    cmd.uses_scaled_texture = false;
                    cmd.highlighted         = a->is_highlighted();
                    cmd.selected            = a->is_selected();
                    cmd.flipped             = a->flipped;
                    cmd.alpha               = a ? a->smoothed_alpha() : 1.0f;
                    if (!std::isfinite(cmd.alpha)) cmd.alpha = 1.0f;
                    cmd.alpha = std::clamp(cmd.alpha, 0.0f, 1.0f);
                    cmd.depthcue_foreground_texture = nullptr;
                    cmd.depthcue_background_texture = nullptr;
                    cmd.depthcue_foreground_alpha   = 0;
                    cmd.depthcue_background_alpha   = 0;
                    auto& target_commands = (a->info->type == asset_types::texture) ? texture_commands_ : remaining_commands_;
                    target_commands.push_back(std::move(cmd));
                }
            }

            for (auto& cmd : child_batch.commands_front) {
                remaining_commands_.push_back(std::move(cmd));
            }

            if (!suppress_asset_lights && has_light_sources && dst.w > 0.0f && dst.h > 0.0f && fw > 0 && fh > 0) {
                bool has_front_lights         = false;
                bool has_back_lights          = false;
                bool has_dark_mask_lights     = false;
                bool has_alpha_mask_only_lights = false;
                for (const LightSource& light : a->info->light_sources) {
                    has_front_lights         |= light.in_front;
                    has_back_lights          |= light.behind;
                    has_dark_mask_lights     |= light.render_to_dark_mask;
                    has_alpha_mask_only_lights |=
                        light.render_front_and_back_to_asset_alpha_mask && !light.in_front &&
                        !light.behind;
                    if (has_front_lights && has_back_lights && has_dark_mask_lights && has_alpha_mask_only_lights) {
                        break;
                    }
                }
                if (!(has_front_lights || has_back_lights || has_dark_mask_lights || has_alpha_mask_only_lights)) {
                    continue;
                }
                LightOverlaySource source;
                source.asset       = a;
                source.asset_rect  = SDL_Rect{
                    static_cast<int>(std::lround(dst.x)),
                    static_cast<int>(std::lround(dst.y)),
                    static_cast<int>(std::lround(dst.w)),
                    static_cast<int>(std::lround(dst.h))
                };
                source.base_width  = fw;
                source.base_height = fh;
                source.flipped     = a->flipped;
                float base_scale   = 1.0f;
                if (a->info && std::isfinite(a->info->scale_factor) && a->info->scale_factor > 0.0f) {
                    base_scale = a->info->scale_factor;
                }
                source.asset_base_scale     = base_scale;
                source.has_front_lights     = has_front_lights;
                // Treat alpha-mask-only lights as a behind-pass so they get processed.
                source.has_back_lights      = has_back_lights || has_alpha_mask_only_lights;
                source.has_dark_mask_lights = has_dark_mask_lights;
                light_overlay_sources_.push_back(std::move(source));
                if (has_dark_mask_lights) {
                    light_overlay_sources_have_dark_mask_cached_ = true;
                    light_overlay_sources_dark_mask_cache_dirty_ = false;
                } else if (!light_overlay_sources_have_dark_mask_cached_) {
                    light_overlay_sources_dark_mask_cache_dirty_ = true;
                }
            }

            if (a->current_frame) {
                a->set_last_rendered_frame(a->current_frame);
            } else {
                a->reset_last_rendered_frame();
            }
        }

    // ---- Modified: bold outline around non-transparent pixels (no interior fill) ----
    std::unordered_map<const Asset*, const LightOverlaySource*> overlay_lookup;
    if (!suppress_asset_lights) {
        overlay_lookup.reserve(light_overlay_sources_.size());
        for (const auto& source : light_overlay_sources_) {
            if (!source.asset) {
                continue;
            }
            overlay_lookup.emplace(source.asset, &source);
        }
    }

    std::vector<const LightOverlaySource*> pending_front_lights;
    std::size_t frame_grid_slice_batches = 0;
    std::size_t frame_grid_slice_cells = 0;
    std::size_t frame_grid_slice_calls_saved = 0;

    //////////////////////////////////////////////////////////////////////////////////
    // SceneRenderer::render - base sprite and depth cue overlays
    //////////////////////////////////////////////////////////////////////////////////
    auto render_commands = [&](const std::vector<AssetRenderCommand>& commands, bool overlay_passed) {
        const int outline_px = 3; // outline thickness in screen pixels

        // 8-direction offsets for a chunky outline; adjust if you want thinner/thicker
        const SDL_FPoint OFFS[] = {
            {  0, -1 }, {  0,  1 }, { -1,  0 }, {  1,  0 },
            { -1, -1 }, {  1, -1 }, { -1,  1 }, {  1,  1 },
            // add an extra "ring" for a bolder edge
            {  0, -2 }, {  0,  2 }, { -2,  0 }, {  2,  0 }
        };

        for (const AssetRenderCommand& cmd : commands) {
            const LightOverlaySource* overlay_source = nullptr;
            bool has_front_light = false;
            if (cmd.asset) {
                if (const auto it = overlay_lookup.find(cmd.asset); it != overlay_lookup.end()) {
                    overlay_source = it->second;
                    if (overlay_source->has_back_lights && light_overlay_visibility > 0.0f) {
                        // Per-asset depth-cue compositing may render lights into a local target.
                        // We only draw behind-lights directly when not compositing for this asset.
                    }
                    has_front_light = light_overlay_visibility > 0.0f && overlay_source->has_front_lights;
                }
            }
            SDL_Texture* tex = cmd.source_texture;

            // Draw behind-lights directly in non-composited path
            if (overlay_source && overlay_source->has_back_lights && light_overlay_visibility > 0.0f) {
                AssetLightRenderer light_renderer(renderer_,
                                                  *overlay_source,
                                                  darkness_overlay_vertices_,
                                                  darkness_overlay_indices_,
                                                  light_overlay_visibility,
                                                  frame_flicker_time_seconds);
                light_renderer.draw_behind();
            }
            if (!tex) {
                if (overlay_source && has_front_light) {
                    pending_front_lights.push_back(overlay_source);
                }
                // Tinted textures are cached and owned by the renderer cache; do not destroy here.
                continue;
            }

            // Compute base alpha
            const Uint8 base_alpha_mod = static_cast<Uint8>(
                std::clamp(std::lround(cmd.alpha * 255.0f), 0L, 255L));

            // --------------------
            // 1) OUTLINE PASS (only if highlighted/selected)
            //    We draw the sprite as a colored mask at multiple small offsets,
            //    then later draw the real sprite on top (so the interior is not tinted).
            // --------------------
            if (cmd.highlighted || cmd.selected) {
                // Better/clearer colors
                Uint8 r = cmd.highlighted ? 255 : 0;   // yellow for highlighted
                Uint8 g = cmd.highlighted ? 220 : 220; // cyan for selected
                Uint8 b = cmd.highlighted ? 0   : 255;
                Uint8 a = 200; // fairly bold

                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
                SDL_SetTextureColorMod(tex, r, g, b);
                SDL_SetTextureAlphaMod(tex, a);

                // Draw multiple offset copies for a thick outline
                for (const SDL_FPoint& o : OFFS) {
                    SDL_FRect orect = cmd.dst;
                    orect.x += o.x * outline_px;
                    orect.y += o.y * outline_px;
                SDL_RenderCopyExF(
                    renderer_,
                    tex,
                    nullptr,
                    &orect,
                    cmd.rotation_degrees,
                    cmd.has_custom_pivot ? &cmd.rotation_pivot : nullptr,
                    cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE
                );
                }

                // Restore defaults on the texture before the base pass
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            }

            // --------------------
            // 2) BASE SPRITE PASS (grid-sliced trapezoids when large relative to grid)
            //    Always draw base pass so blur never reduces opacity
            // --------------------
            bool drew_grid_sliced = false;
            if (assets_ && cmd.asset && tex) {
                // Only apply when not already handled by loader-composed grid tiles
                const auto& tiling_opt = cmd.asset->tiling_info();
                const bool allow_grid_sliced = (cmd.asset->info && cmd.asset->info->tillable);
                const bool tiling_managed_by_chunk = (tiling_opt && tiling_opt->is_valid());
                if (allow_grid_sliced && !tiling_managed_by_chunk) {
                    // Compute grid step (world units)
                    const int grid_step = std::max(1, 1 << std::clamp(cmd.asset->grid_resolution, 0, vibble::grid::kMaxResolution));

                    // Camera parameters
                    camera_grid& cam = assets_->getView();
                    const float scale = std::max(1e-6f, cam.get_scale());
                    const float inv_scale_local = 1.0f / scale;

                    // Asset base dimensions and scale
                    int fw = cmd.asset->cached_w;
                    int fh = cmd.asset->cached_h;
                    if ((fw <= 0 || fh <= 0) && cmd.final_texture) {
                        SDL_QueryTexture(cmd.final_texture, nullptr, nullptr, &fw, &fh);
                    }
                    float base_scale = cmd.asset->smoothed_scale();
                    if (!std::isfinite(base_scale) || base_scale <= 0.0f) base_scale = 1.0f;
                    const float scaled_fw = static_cast<float>(fw) * base_scale;
                    const float scaled_fh = static_cast<float>(fh) * base_scale;
                    const float base_sh   = scaled_fh * inv_scale_local;

                    // Reference screen height (player) for realism effects
                    float ref_sh = player_sh;
                    if (!std::isfinite(ref_sh) || ref_sh <= 0.0f) ref_sh = 1.0f;

                    // Effects (distance + vertical squash)
                    const SDL_Point world_point{
                        static_cast<int>(std::lround(cmd.asset->smoothed_translation_x())),
                        static_cast<int>(std::lround(cmd.asset->smoothed_translation_y()))
                    };
                    camera_grid::RenderEffects ef = cam.compute_render_effects(world_point, base_sh, ref_sh, camera_grid::RenderSmoothingKey(cmd.asset));
                    const float distance_scale = (cmd.asset->info && cmd.asset->info->apply_distance_scaling) ? ef.distance_scale : 1.0f;
                    const float vertical_scale = (cmd.asset->info && cmd.asset->info->apply_vertical_scaling) ? ef.vertical_scale : 1.0f;

                    // Determine world extents covered by this sprite so we can slice on world grid
                    const float world_width  = cmd.dst.w * scale / std::max(1e-6f, distance_scale);
                    const float world_height = cmd.dst.h * scale / std::max(1e-6f, (distance_scale * vertical_scale));

                    if (std::isfinite(world_width) && std::isfinite(world_height) && world_width > 0.0f && world_height > 0.0f) {
                        // Only slice when larger than a single grid cell in at least one dimension
                        if (world_width > static_cast<float>(grid_step) || world_height > static_cast<float>(grid_step)) {
                            // Anchor world and screen positions
                            SDL_FPoint anchor_screen = cam.map_to_screen_f(SDL_FPoint{ static_cast<float>(world_point.x), static_cast<float>(world_point.y) });

                            const double left_world   = static_cast<double>(world_point.x) - static_cast<double>(world_width) * 0.5;
                            const double top_world    = static_cast<double>(world_point.y) - static_cast<double>(world_height);
                            const double right_world  = left_world + static_cast<double>(world_width);
                            const double bottom_world = static_cast<double>(world_point.y);

                            auto align_down = [](double v, int step) {
                                if (step <= 0) return v;
                                const double s = static_cast<double>(step);
                                return std::floor(v / s) * s;
                            };
                            auto align_up = [](double v, int step) {
                                if (step <= 0) return v;
                                const double s = static_cast<double>(step);
                                return std::ceil(v / s) * s;
                            };

                            const double start_x = align_down(left_world, grid_step);
                            const double end_x   = align_up(right_world, grid_step);
                            const double start_y = align_down(top_world, grid_step);
                            const double end_y   = align_up(bottom_world, grid_step);

                            // Normalize helpers for UVs (relative to entire texture)
                            const double inv_w = (world_width > 0.0f) ? (1.0 / static_cast<double>(world_width)) : 0.0;
                            const double inv_h = (world_height > 0.0f) ? (1.0 / static_cast<double>(world_height)) : 0.0;

                            // Ensure the texture is in a neutral mod state; use vertex alpha/color
                            SDL_SetTextureColorMod(tex, 255, 255, 255);
                            SDL_SetTextureAlphaMod(tex, 255);
                            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

                            auto estimate_cell_count = [&](double span) -> std::size_t {
                                if (grid_step <= 0) {
                                    return 1;
                                }
                                const double step = static_cast<double>(grid_step);
                                const double cells = std::ceil(std::max(0.0, span) / step);
                                return static_cast<std::size_t>(std::max(1.0, cells));
                            };
                            const std::size_t estimated_cols = estimate_cell_count(end_x - start_x);
                            const std::size_t estimated_rows = estimate_cell_count(end_y - start_y);
                            const std::size_t worst_case_cells = std::max<std::size_t>(1, estimated_cols * estimated_rows);
                            const std::size_t required_vertices = worst_case_cells * 4;
                            const std::size_t required_indices  = worst_case_cells * 6;

                            if (required_vertices > grid_slice_vertex_capacity_hint_) {
                                grid_slice_vertex_capacity_hint_ = required_vertices;
                            }
                            if (required_indices > grid_slice_index_capacity_hint_) {
                                grid_slice_index_capacity_hint_ = required_indices;
                            }
                            if (grid_slice_vertex_capacity_hint_ > grid_slice_vertices_.capacity()) {
                                grid_slice_vertices_.reserve(grid_slice_vertex_capacity_hint_);
                            }
                            if (grid_slice_index_capacity_hint_ > grid_slice_indices_.capacity()) {
                                grid_slice_indices_.reserve(grid_slice_index_capacity_hint_);
                            }

                            grid_slice_vertices_.clear();
                            grid_slice_indices_.clear();
                            std::size_t emitted_cells = 0;
                            const Uint8 a_mod = base_alpha_mod;
                            auto emit_vertex = [&](const SDL_FPoint& pos, double u, double v) {
                                SDL_Vertex vert{};
                                vert.position = pos;
                                vert.color    = SDL_Color{255, 255, 255, a_mod};
                                vert.tex_coord = SDL_FPoint{ static_cast<float>(u), static_cast<float>(v) };
                                grid_slice_vertices_.push_back(vert);
                            };

                            for (double wy = start_y; wy < end_y; wy += grid_step) {
                                const double cell_top    = std::max(wy, top_world);
                                const double cell_bottom = std::min(wy + grid_step, end_y);
                                if (cell_bottom <= cell_top) continue;
                                for (double wx = start_x; wx < end_x; wx += grid_step) {
                                    const double cell_left  = std::max(wx, left_world);
                                    const double cell_right = std::min(wx + grid_step, end_x);
                                    if (cell_right <= cell_left) continue;

                                    // World corners of this patch (TL, TR, BR, BL)
                                    SDL_FPoint w_tl{ static_cast<float>(cell_left),  static_cast<float>(cell_top) };
                                    SDL_FPoint w_tr{ static_cast<float>(cell_right), static_cast<float>(cell_top) };
                                    SDL_FPoint w_br{ static_cast<float>(cell_right), static_cast<float>(cell_bottom) };
                                    SDL_FPoint w_bl{ static_cast<float>(cell_left),  static_cast<float>(cell_bottom) };

                                    auto to_screen = [&](const SDL_FPoint& w)->SDL_FPoint {
                                        // Offsets from anchor in world units
                                        const double dx_world = static_cast<double>(w.x) - static_cast<double>(world_point.x);
                                        const double dy_world = static_cast<double>(world_point.y) - static_cast<double>(w.y);
                                        double sx = static_cast<double>(anchor_screen.x) + (dx_world * inv_scale_local * distance_scale);
                                        double sy = static_cast<double>(anchor_screen.y) - (dy_world * inv_scale_local * distance_scale * vertical_scale);
                                        // Parallax-adjusted X
                                        if (!(assets_ && assets_->player == cmd.asset)) {
                                            sx = static_cast<double>(assets_->world_grid().parallax_adjusted_screen_x(
                                                SDL_Point{ static_cast<int>(std::lround(w.x)), static_cast<int>(std::lround(w.y)) },
                                                static_cast<float>(sx)));
                                        }
                                        return SDL_FPoint{ static_cast<float>(sx), static_cast<float>(sy) };
                                    };

                                    SDL_FPoint s_tl = to_screen(w_tl);
                                    SDL_FPoint s_tr = to_screen(w_tr);
                                    SDL_FPoint s_br = to_screen(w_br);
                                    SDL_FPoint s_bl = to_screen(w_bl);

                                    // UVs normalized to 0..1 over the full sprite
                                    double u0 = (static_cast<double>(w_tl.x) - left_world) * inv_w;
                                    double u1 = (static_cast<double>(w_tr.x) - left_world) * inv_w;
                                    double v0 = (static_cast<double>(w_tl.y) - top_world)  * inv_h;
                                    double v1 = (static_cast<double>(w_br.y) - top_world)  * inv_h;

                                    // Horizontal flip
                                    if (cmd.flipped) {
                                        u0 = 1.0 - u0;
                                        u1 = 1.0 - u1;
                                        std::swap(u0, u1);
                                    }

                                    const int base_index = static_cast<int>(grid_slice_vertices_.size());
                                    emit_vertex(s_tl, u0, v0);
                                    emit_vertex(s_tr, u1, v0);
                                    emit_vertex(s_br, u1, v1);
                                    emit_vertex(s_bl, u0, v1);

                                    grid_slice_indices_.push_back(base_index + 0);
                                    grid_slice_indices_.push_back(base_index + 1);
                                    grid_slice_indices_.push_back(base_index + 2);
                                    grid_slice_indices_.push_back(base_index + 0);
                                    grid_slice_indices_.push_back(base_index + 2);
                                    grid_slice_indices_.push_back(base_index + 3);
                                    ++emitted_cells;
                                }
                            }

                            if (!grid_slice_vertices_.empty()) {
                                SDL_RenderGeometry(renderer_,
                                                   tex,
                                                   grid_slice_vertices_.data(),
                                                   static_cast<int>(grid_slice_vertices_.size()),
                                                   grid_slice_indices_.data(),
                                                   static_cast<int>(grid_slice_indices_.size()));
                                drew_grid_sliced = true;
                                ++frame_grid_slice_batches;
                                frame_grid_slice_cells += emitted_cells;
                                if (emitted_cells > 1) {
                                    frame_grid_slice_calls_saved += (emitted_cells - 1);
                                }
                            }

                            grid_slice_vertices_.clear();
                            grid_slice_indices_.clear();
                        }
                    }
                }
            }

            if (!drew_grid_sliced) {
                // Fallback: draw normally as a rect
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, base_alpha_mod);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_RenderCopyExF(
                    renderer_,
                    tex,
                    nullptr,
                    &cmd.dst,
                    cmd.rotation_degrees,
                    cmd.has_custom_pivot ? &cmd.rotation_pivot : nullptr,
                    cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE
                );
            }
            auto draw_depthcue_overlay = [&](SDL_Texture* overlay_tex, Uint8 overlay_alpha) {
                if (!overlay_tex || overlay_alpha == 0) {
                    return;
                }
                // Defensive: ensure texture is still valid (may have been destroyed elsewhere).
                int tex_w = 0, tex_h = 0; Uint32 fmt = 0; int access = 0;
                if (SDL_QueryTexture(overlay_tex, &fmt, &access, &tex_w, &tex_h) != 0) {
                    // Texture invalid or destroyed; skip drawing to avoid crash.
                    try {
                        vibble::log::debug(std::string{"[SceneRenderer] Skipping invalid depthcue tex ptr="} +
                                           std::to_string(reinterpret_cast<std::uintptr_t>(overlay_tex)));
                    } catch (...) {}
                    return;
                }
                // Match the sprite's overall alpha so overlays fade alongside the base.
                const Uint8 combined_alpha =
                    static_cast<Uint8>((static_cast<int>(overlay_alpha) * base_alpha_mod) / 255);
                if (combined_alpha == 0) {
                    return;
                }
                SDL_SetTextureBlendMode(overlay_tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureColorMod(overlay_tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(overlay_tex, combined_alpha);
                SDL_RenderCopyExF(
                    renderer_,
                    overlay_tex,
                    nullptr,
                    &cmd.dst,
                    cmd.rotation_degrees,
                    cmd.has_custom_pivot ? &cmd.rotation_pivot : nullptr,
                    cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE
                );
                // Restore defaults for shared textures so future draws use normal modulation.
                SDL_SetTextureColorMod(overlay_tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(overlay_tex, 255);
            };
            draw_depthcue_overlay(cmd.depthcue_background_texture, cmd.depthcue_background_alpha);
            draw_depthcue_overlay(cmd.depthcue_foreground_texture, cmd.depthcue_foreground_alpha);

            if (cmd.uses_scaled_texture && cmd.final_texture) {
                SDL_SetTextureColorMod(cmd.final_texture, 255, 255, 255);
                SDL_SetTextureAlphaMod(cmd.final_texture, 255);
                SDL_SetTextureBlendMode(cmd.final_texture, SDL_BLENDMODE_BLEND);
            }

            if (overlay_source && has_front_light) {
                pending_front_lights.push_back(overlay_source);
            }
            // Tinted textures are cached and owned by the renderer cache; do not destroy here.
        }

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    };
    // -------------------------------------------------------------------------------


        render_commands(texture_commands_, /*overlay_passed=*/false);
        if (light_map_ && !chunk_lighting_suspended_) {
            light_map_->update(renderer_, 0u);
        }
        render_commands(remaining_commands_, /*overlay_passed=*/false);

        if (frame_grid_slice_batches > 0) {
            grid_slice_batches_accum_ += frame_grid_slice_batches;
            grid_slice_draw_calls_saved_accum_ += frame_grid_slice_calls_saved;
            if ((frame_counter_ % 120ull) == 0ull) {
                vibble::log::debug(std::string{"[SceneRenderer] Grid-slice batching saved "} +
                                   std::to_string(frame_grid_slice_calls_saved) +
                                   " draw calls across " +
                                   std::to_string(frame_grid_slice_batches) +
                                   " sprites (" +
                                   std::to_string(frame_grid_slice_cells) +
                                   " cells this frame, total_saved=" +
                                   std::to_string(grid_slice_draw_calls_saved_accum_) +
                                   ", total_batches=" +
                                   std::to_string(grid_slice_batches_accum_) + ").");
            }
        }

        //////////////////////////////////////////////////////////////////////////////////
        // SceneRenderer::render - dynamic darkness and light map
        //////////////////////////////////////////////////////////////////////////////////
        // After all base sprites are drawn, apply the dynamic darkness overlay once
        if (dark_mask_enabled_) {
            render_dynamic_darkness_overlay(map_light_opacity, frame_flicker_time_seconds);
        }
        render_light_map();

        // Draw all deferred front-lights above the darkness overlay
        if (light_overlay_visibility > 0.0f) {
            for (const LightOverlaySource* src : pending_front_lights) {
                if (src && src->has_front_lights) {
                    AssetLightRenderer light_renderer(renderer_,
                                                      *src,
                                                      darkness_overlay_vertices_,
                                                      darkness_overlay_indices_,
                                                      light_overlay_visibility,
                                                      frame_flicker_time_seconds);
                    light_renderer.draw_in_front();
                }
            }
        }
        pending_front_lights.clear();

    } else {
        //////////////////////////////////////////////////////////////////////////////////
        // SceneRenderer::render - light-map only fallback
        //////////////////////////////////////////////////////////////////////////////////
        // Light-map only mode: keep original behavior
        SDL_SetRenderTarget(renderer_, nullptr);
        render_light_map();
        if (chunk_preview_enabled_ && light_map_) {
            SDL_Rect screen_view{0, 0, screen_width_, screen_height_};
            light_map_->render_chunk_preview(renderer_, screen_view);
        }
        if (assets_) {
            assets_->render_overlays(renderer_);
        }
        SDL_RenderPresent(renderer_);
    }
}

bool SceneRenderer::ensure_darkness_overlay() {
    if (!renderer_ || screen_width_ <= 0 || screen_height_ <= 0) {
        return false;
    }

    if (darkness_overlay_texture_ &&
        (darkness_overlay_width_ != screen_width_ || darkness_overlay_height_ != screen_height_)) {
        destroy_darkness_overlay();
    }

    if (!darkness_overlay_texture_) {
        SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
        if (!texture) {
            vibble::log::warn(std::string{"[SceneRenderer] Failed to allocate darkness overlay: "} + SDL_GetError());
            return false;
        }
        darkness_overlay_texture_ = texture;
        darkness_overlay_width_   = screen_width_;
        darkness_overlay_height_  = screen_height_;
        SDL_SetTextureBlendMode(darkness_overlay_texture_, SDL_BLENDMODE_BLEND);
    }

    return darkness_overlay_texture_ != nullptr;
}

void SceneRenderer::destroy_darkness_overlay() {
    if (darkness_overlay_texture_) {
        SDL_DestroyTexture(darkness_overlay_texture_);
        darkness_overlay_texture_ = nullptr;
        darkness_overlay_width_   = 0;
        darkness_overlay_height_  = 0;
    }
}

bool SceneRenderer::ensure_sky_texture() {
    if (sky_texture_ || sky_texture_failed_) {
        return sky_texture_ != nullptr;
    }
    if (!renderer_) {
        return false;
    }

    std::filesystem::path path = sky_texture_path_;
    if (!path.is_absolute()) {
        path = std::filesystem::current_path() / path;
    }

    const std::string path_str = path.string();
    SDL_Texture* tex = IMG_LoadTexture(renderer_, path_str.c_str());
    if (!tex) {
        vibble::log::warn(std::string{"[SceneRenderer] Failed to load sky texture '"} +
                          path_str + "': " + IMG_GetError());
        sky_texture_failed_ = true;
        return false;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
        vibble::log::warn(std::string{"[SceneRenderer] Invalid sky texture '"} +
                          path_str + "': " + SDL_GetError());
        SDL_DestroyTexture(tex);
        sky_texture_failed_ = true;
        return false;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    sky_texture_        = tex;
    sky_texture_width_  = tex_w;
    sky_texture_height_ = tex_h;
    return true;
}

void SceneRenderer::destroy_sky_texture() {
    if (sky_texture_) {
        SDL_DestroyTexture(sky_texture_);
        sky_texture_ = nullptr;
    }
    sky_texture_width_  = 0;
    sky_texture_height_ = 0;
}

void SceneRenderer::render_sky_layer(const camera_grid& cam, bool depth_effects_enabled) {
    if (!depth_effects_enabled) {
        return;
    }
    if (!renderer_ || screen_width_ <= 0 || screen_height_ <= 0) {
        return;
    }

    const double horizon_y = cam.horizon_screen_y_for_scale();
    if (!std::isfinite(horizon_y)) {
        return;
    }
    if (horizon_y < 0.0 || horizon_y > static_cast<double>(screen_height_)) {
        return;
    }

    if (!ensure_sky_texture() || !sky_texture_) {
        return;
    }

    const float tex_w = static_cast<float>(sky_texture_width_);
    const float tex_h = static_cast<float>(sky_texture_height_);
    if (tex_w <= 0.0f || tex_h <= 0.0f) {
        return;
    }

    const float target_w = static_cast<float>(screen_width_);
    const float scale    = target_w / tex_w;
    const float target_h = tex_h * scale;
    if (!std::isfinite(target_h) || target_h <= 0.0f || !std::isfinite(scale)) {
        return;
    }

    SDL_FRect dst{
        0.0f,
        static_cast<float>(horizon_y) - target_h,
        target_w,
        target_h
    };

    SDL_SetTextureColorMod(sky_texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(sky_texture_, 255);
    SDL_RenderCopyF(renderer_, sky_texture_, nullptr, &dst);
}

void SceneRenderer::render_dynamic_darkness_overlay(float map_light_opacity, float flicker_time_seconds) {
    if (!renderer_) {
        return;
    }

    if (!has_dark_mask_overlay_sources()) {
        ++darkness_overlay_skipped_frames_;
        if (!darkness_overlay_skip_logged_) {
            vibble::log::debug(std::string{"[SceneRenderer] Skipping dynamic darkness overlay; no dark-mask lights. skipped_frames="} +
                               std::to_string(darkness_overlay_skipped_frames_));
            darkness_overlay_skip_logged_ = true;
        }
        return;
    }

    ++darkness_overlay_rendered_frames_;
    if (darkness_overlay_skip_logged_) {
        vibble::log::debug(std::string{"[SceneRenderer] Dynamic darkness overlay pass resumed. rendered_frames="} +
                           std::to_string(darkness_overlay_rendered_frames_));
        darkness_overlay_skip_logged_ = false;
    }

    const float overlay_alpha             = std::clamp(map_light_opacity, 0.0f, 1.0f);
    const float light_overlay_visibility = overlay_alpha;
    if (!ensure_darkness_overlay()) {
        return;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, darkness_overlay_texture_);
    SDL_SetTextureAlphaMod(darkness_overlay_texture_, 255);
    SDL_SetTextureColorMod(darkness_overlay_texture_, 255, 255, 255);

    SDL_BlendMode previous_draw_blend = SDL_BLENDMODE_BLEND;
    if (SDL_GetRenderDrawBlendMode(renderer_, &previous_draw_blend) != 0) {
        previous_draw_blend = SDL_BLENDMODE_BLEND;
    }

    const SDL_BlendMode cutout_blend = darkness_cutout_blend_mode();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, cutout_blend);

    if (darkness_overlay_vertex_capacity_hint_ > darkness_overlay_vertices_.capacity()) {
        darkness_overlay_vertices_.reserve(darkness_overlay_vertex_capacity_hint_);
    }
    if (darkness_overlay_index_capacity_hint_ > darkness_overlay_indices_.capacity()) {
        darkness_overlay_indices_.reserve(darkness_overlay_index_capacity_hint_);
    }

    std::size_t frame_max_vertices = 0;
    std::size_t frame_max_indices  = 0;

    for (const LightOverlaySource& source : light_overlay_sources_) {
        if (!source.has_dark_mask_lights) {
            continue;
        }
        AssetLightRenderer light_renderer(renderer_,
                                          source,
                                          darkness_overlay_vertices_,
                                          darkness_overlay_indices_,
                                          light_overlay_visibility,
                                          flicker_time_seconds);
        auto               result = light_renderer.accumulate_dark_mask();
        frame_max_vertices        = std::max(frame_max_vertices, result.max_vertices);
        frame_max_indices         = std::max(frame_max_indices, result.max_indices);
    }

    if (frame_max_vertices > darkness_overlay_vertex_capacity_hint_) {
        darkness_overlay_vertex_capacity_hint_ = frame_max_vertices;
    }
    if (frame_max_indices > darkness_overlay_index_capacity_hint_) {
        darkness_overlay_index_capacity_hint_ = frame_max_indices;
    }

    SDL_SetRenderDrawBlendMode(renderer_, previous_draw_blend);
    SDL_SetRenderTarget(renderer_, previous_target);

    if (overlay_alpha <= 0.0f) {
        return;
    }

    SDL_SetTextureAlphaMod(darkness_overlay_texture_, static_cast<Uint8>(std::clamp(std::lround(overlay_alpha * 255.0f), 0L, 255L)));
    SDL_SetTextureColorMod(darkness_overlay_texture_, 0, 0, 0);

    SDL_Rect screen_dst{0, 0, screen_width_, screen_height_};
    SDL_RenderCopy(renderer_, darkness_overlay_texture_, nullptr, &screen_dst);
}

bool SceneRenderer::has_dark_mask_overlay_sources() {
    if (light_overlay_sources_dark_mask_cache_dirty_) {
        light_overlay_sources_have_dark_mask_cached_ = std::any_of(
            light_overlay_sources_.begin(),
            light_overlay_sources_.end(),
            [](const LightOverlaySource& source) { return source.has_dark_mask_lights; });
        light_overlay_sources_dark_mask_cache_dirty_ = false;
    }
    return light_overlay_sources_have_dark_mask_cached_;
}


////////////////////////////////////////////////////////////////////////////////
// AssetRenderPipeline and StageContext implementation
////////////////////////////////////////////////////////////////////////////////




namespace {

float compute_asset_screen_height(Asset& asset, float inv_scale) {
    int cached_w = asset.cached_w;
    int cached_h = asset.cached_h;
    if ((cached_w <= 0 || cached_h <= 0)) {
        if (SDL_Texture* final = asset.get_final_texture()) {
            SDL_QueryTexture(final, nullptr, nullptr, &cached_w, &cached_h);
}

}
    if ((cached_w <= 0 || cached_h <= 0)) {
        if (SDL_Texture* frame = asset.get_current_frame()) {
            SDL_QueryTexture(frame, nullptr, nullptr, &cached_w, &cached_h);
        }
    }

    if (cached_w > 0) {
        asset.cached_w = cached_w;
    }
    if (cached_h > 0) {
        asset.cached_h = cached_h;
    }

    if (cached_h <= 0) {
        return 0.0f;
    }

    float scale = 1.0f;
    if (asset.info && std::isfinite(asset.info->scale_factor) && asset.info->scale_factor >= 0.0f) {
        scale = asset.info->scale_factor;
    }
    return static_cast<float>(cached_h) * scale * inv_scale;
}

}

Uint8 StageContext::main_light_alpha() const {
    return lighting ? lighting->main_light.get_current_color().a : 0;
}

Uint8 StageContext::main_light_brightness() const {
    return lighting ? static_cast<Uint8>(lighting->main_light.get_brightness()) : 0;
}

Global_Light_Source& StageContext::main_light() {
    return lighting->main_light;
}

const Global_Light_Source& StageContext::main_light() const {
    return lighting->main_light;
}

camera_grid& StageContext::camera_view() {
    return lighting->camera_view;
}

const camera_grid& StageContext::camera_view() const {
    return lighting->camera_view;
}

Asset* StageContext::player() const {
    return lighting ? lighting->player : nullptr;
}

void StageContext::update_projection(Asset& asset) {
    base_shadow_opacity     = 204.0f / 255.0f;
    screen_rect             = SDL_Rect{ 0, 0, 0, 0 };
    screen_center           = SDL_FPoint{ 0.0f, 0.0f };
    reference_screen_height = 1.0f;
    static_light_strength   = 1.0f;
    dynamic_light_strength  = 1.0f;
    blended_light_strength  = 1.0f;
    runtime_light_color     = SDL_Color{255, 255, 255, 255};
    has_runtime_light_color = false;

    if (!lighting || width <= 0 || height <= 0) {
        return;
    }

    camera_grid& cam = lighting->camera_view;
    const float scale = cam.get_scale();
    const float inv_scale = (std::isfinite(scale) && scale > 1e-6f) ? (1.0f / scale) : 1.0f;

    float asset_scale = 1.0f;
    if (asset.info && std::isfinite(asset.info->scale_factor) && asset.info->scale_factor >= 0.0f) {
        asset_scale = asset.info->scale_factor;
    }

    const float scaled_fw = static_cast<float>(width) * asset_scale;
    const float scaled_fh = static_cast<float>(height) * asset_scale;
    if (scaled_fw <= 0.0f || scaled_fh <= 0.0f) {
        return;
    }

    const float base_sw = scaled_fw * inv_scale;
    const float base_sh = scaled_fh * inv_scale;

    float reference_height = 1.0f;
    if (Asset* player_asset = player()) {
        reference_height = compute_asset_screen_height(*player_asset, inv_scale);
    }
    if (!std::isfinite(reference_height) || reference_height <= 0.0f) {
        reference_height = 1.0f;
    }
    reference_screen_height = reference_height;

    const float world_x = asset.smoothed_translation_x();
    const float world_y = asset.smoothed_translation_y();
    const SDL_Point world_point{
        static_cast<int>(std::lround(world_x)),
        static_cast<int>(std::lround(world_y))
    };
    const camera_grid::RenderEffects effects =
        cam.compute_render_effects(
            SDL_Point{ static_cast<int>(std::lround(world_x)), static_cast<int>(std::lround(world_y)) },
            base_sh,
            reference_height,
            camera_grid::RenderSmoothingKey(&asset));

    world::Grid* grid = (lighting && lighting->world_grid) ? lighting->world_grid : nullptr;
    const float distance_scale  = (asset.info && asset.info->apply_distance_scaling) ? effects.distance_scale : 1.0f;
    const float vertical_scale  = (asset.info && asset.info->apply_vertical_scaling) ? effects.vertical_scale : 1.0f;

    const float scaled_sw       = base_sw * distance_scale;
    const float scaled_sh       = base_sh * distance_scale;
    const float final_visible_h = scaled_sh * vertical_scale;

    if (!std::isfinite(scaled_sw) || !std::isfinite(final_visible_h) || scaled_sw <= 0.0f || final_visible_h <= 0.0f) {
        return;
    }

    // Do not apply grid parallax to the player asset
    const bool is_player_asset = (&asset == player());
    const float center_x = (!grid || is_player_asset)
        ? effects.screen_position.x
        : grid->parallax_adjusted_screen_x(world_point, effects.screen_position.x);
    const float center_y = effects.screen_position.y;

    const float rect_w = std::max(scaled_sw, 1.0f);
    const float rect_h = std::max(final_visible_h, 1.0f);

    const float left_f = center_x - rect_w * 0.5f;
    const float top_f  = center_y - rect_h;

    screen_center = SDL_FPoint{ center_x, center_y - rect_h * 0.5f };

    const int sw = std::max(1, static_cast<int>(std::lround(rect_w)));
    const int sh = std::max(1, static_cast<int>(std::lround(rect_h)));
    const int left = static_cast<int>(std::lround(left_f));
    const int top  = static_cast<int>(std::lround(top_f));
    screen_rect    = SDL_Rect{ left, top, sw, sh };

    if (const LightMap* light_map_sampler = light_map()) {
        const LightMap::SampledBrightness sample =
            light_map_sampler->sample_lighting(asset.pos.x, asset.pos.y);
        static_light_strength  = sample.static_component;
        dynamic_light_strength = sample.dynamic_component;
        blended_light_strength = sample.blended;
        if (sample.has_color) {
            runtime_light_color     = sample.color;
            has_runtime_light_color = true;
        }
    }
}

AssetRenderPipeline::AssetRenderPipeline(SDL_Renderer* renderer, const SceneLighting& lighting)
: renderer_(renderer)
, lighting_(lighting)
, render_asset_(renderer) {
    using render_pipeline::shading::RenderAsset;
    using render_pipeline::shading::RenderShadowMask;

    stages_.push_back(StageEntry{ std::make_unique<RenderAsset>(), SDL_BLENDMODE_BLEND, false, false });
    stages_.push_back(StageEntry{ std::make_unique<RenderShadowMask>(), SDL_BLENDMODE_BLEND, true, false });
}

SDL_Texture* AssetRenderPipeline::run(Asset& asset) {
    if (!renderer_) {
        return nullptr;
    }

    SDL_Texture* base_frame = asset.get_current_frame();
    if (!base_frame) {
        return nullptr;
    }

    int    width       = asset.cached_w;
    int    height      = asset.cached_h;
    Uint32 base_format = SDL_PIXELFORMAT_UNKNOWN;

    if (width <= 0 || height <= 0) {
        SDL_QueryTexture(base_frame, nullptr, nullptr, &width, &height);
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    StageContext context{};
    context.base_texture = base_frame;
    context.lighting     = &lighting_;
    context.width        = width;
    context.height       = height;
    context.reusable_final = asset.get_final_texture();
    if (renderer_) {
        SDL_GetRendererOutputSize(renderer_, &context.screen_width_px, &context.screen_height_px);
    }

    // Motion blur path removed: no previous-frame accumulation/blending.

    if (stages_.empty() || !stages_[0].stage) {
        return nullptr;
    }

    StageEntry& base_stage_entry = stages_[0];
    if (!base_stage_entry.stage->supports(asset)) {
        return nullptr;
    }

    SDL_Texture* final_texture = base_stage_entry.stage->run(renderer_, asset, context);
    if (!final_texture) {
        return nullptr;
    }

    context.final_texture     = final_texture;
    context.stage_destination = final_texture;
    context.stage_blend       = SDL_BLENDMODE_BLEND;
    context.stage_drew_to_destination = false;

    std::vector<SDL_Texture*> intermediates;
    intermediates.reserve(stages_.size());

    for (std::size_t i = 1; i < stages_.size(); ++i) {
        StageEntry& entry = stages_[i];
        if (!entry.stage || !entry.stage->supports(asset)) {
            continue;
        }
        if (low_quality_mode_ && entry.skip_in_low_quality) {
            continue;
        }
        context.final_texture     = final_texture;
        context.stage_destination = final_texture;
        context.stage_blend       = entry.blend;
        context.stage_drew_to_destination = false;
        SDL_Texture* stage_texture = entry.stage->run(renderer_, asset, context);
        if (context.stage_drew_to_destination) {
            continue;
        }
        if (!stage_texture || stage_texture == final_texture) {
            continue;
        }

        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, final_texture);
        SDL_SetTextureBlendMode(stage_texture, entry.blend);
        SDL_RenderCopy(renderer_, stage_texture, nullptr, nullptr);
        SDL_SetRenderTarget(renderer_, prev_target);

        if (!entry.stage_manages_texture) {
            intermediates.push_back(stage_texture);
        }
    }

    // Motion blur blending removed.

    for (SDL_Texture* tex : intermediates) {
        SDL_DestroyTexture(tex);
    }

    asset.cached_w = context.width;
    asset.cached_h = context.height;

    return final_texture;
}

SDL_Texture* AssetRenderPipeline::regenerateFinalTexture(Asset* asset) {
    if (!asset) {
        return nullptr;
    }
    SDL_Texture* tex = run(*asset);
    return tex;
}

SDL_Texture* AssetRenderPipeline::texture_for_scale(Asset* asset,
                                                    SDL_Texture* base_tex,
                                                    int base_w,
                                                    int base_h,
                                                    int target_w,
                                                    int target_h,
                                                    float hysteresis_margin) {
    return render_asset_.texture_for_scale(asset, base_tex, base_w, base_h, target_w, target_h, hysteresis_margin);
}

void AssetRenderPipeline::set_low_quality_mode(bool enable) {
    low_quality_mode_ = enable;
}

SceneLighting& AssetRenderPipeline::lighting() {
    return lighting_;
}

const SceneLighting& AssetRenderPipeline::lighting() const {
    return lighting_;
}

void AssetRenderPipeline::set_player_asset(Asset* player) {
    lighting_.player = player;
}


////////////////////////////////////////////////////////////////////////////////
// RenderAsset downscale helper implementation
////////////////////////////////////////////////////////////////////////////////




RenderAsset::RenderAsset(SDL_Renderer* renderer)
: renderer_(renderer) {
        render_pipeline::EnsureBestScaleHint();
}

namespace {

SDL_Texture* create_half_scale(SDL_Renderer* renderer,
                               SDL_Texture* source,
                               Uint32 format,
                               int src_w,
                               int src_h) {
        if (!renderer || !source || src_w <= 0 || src_h <= 0) {
                return nullptr;
        }
        int dst_w = std::max(1, src_w / 2);
        int dst_h = std::max(1, src_h / 2);
        SDL_Texture* half = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, dst_w, dst_h);
        if (!half) {
                return nullptr;
        }
        SDL_SetTextureBlendMode(half, SDL_BLENDMODE_BLEND);
        #if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(half, SDL_ScaleModeBest);
        #endif
        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, half);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_Rect dst{0, 0, dst_w, dst_h};
        SDL_RenderCopy(renderer, source, nullptr, &dst);
        SDL_SetRenderTarget(renderer, prev_target);
        return half;
}

bool rerender_scaled_texture(SDL_Renderer* renderer,
                             SDL_Texture* destination,
                             SDL_Texture* source,
                             int dst_w,
                             int dst_h)
{
        if (!renderer || !destination || !source || dst_w <= 0 || dst_h <= 0) {
                return false;
        }

        SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
        if (SDL_SetRenderTarget(renderer, destination) != 0) {
                SDL_SetRenderTarget(renderer, previous_target);
                return false;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        if (SDL_RenderClear(renderer) != 0) {
                SDL_SetRenderTarget(renderer, previous_target);
                return false;
        }

        SDL_Rect dst{0, 0, dst_w, dst_h};
        const int copy_result = SDL_RenderCopy(renderer, source, nullptr, &dst);

        SDL_SetRenderTarget(renderer, previous_target);
        return copy_result == 0;
}

}

SDL_Texture* RenderAsset::texture_for_scale(Asset* asset,
                                            SDL_Texture* base_tex,
                                            int base_w,
                                            int base_h,
                                            int target_w,
                                            int target_h,
                                            float hysteresis_margin) {
        if (!asset || !base_tex || base_w <= 0 || base_h <= 0 || target_w <= 0 || target_h <= 0) {
                if (asset) {
                        asset->update_scale_usage(1.0f,
                                                  1.0f,
                                                  1.0f,
                                                  0,
                                                  0.0f,
                                                  std::numeric_limits<float>::max());
                }
                return base_tex;
        }

        const float desired_scale = render_pipeline::ScalingLogic::ComputeScale(base_w, base_h, target_w, target_h);
        const auto& scale_steps = (asset->info && !asset->info->scale_variants.empty()) ? static_cast<const std::vector<float>&>(asset->info->scale_variants) : render_pipeline::ScalingLogic::DefaultScaleSteps();

        if (asset->downscale_cache_.size() != scale_steps.size()) {
                asset->clear_downscale_cache();
        }

        render_pipeline::ScalingLogic::HysteresisState hysteresis_state{};
        const auto& variant_state = asset->scale_variant_state();
        hysteresis_state.last_index = variant_state.last_variant_index;
        hysteresis_state.min_scale  = variant_state.hysteresis_min;
        hysteresis_state.max_scale  = variant_state.hysteresis_max;

        render_pipeline::ScalingLogic::HysteresisOptions hysteresis_options{};
        hysteresis_options.margin         = std::isfinite(hysteresis_margin) && hysteresis_margin >= 0.0f
            ? hysteresis_margin
            : render_pipeline::ScalingLogic::kDefaultHysteresisMargin;
        hysteresis_options.preload_margin = render_pipeline::ScalingLogic::kDefaultPreloadMargin;

        const float smoothed_scale = asset->smoothed_scale();
        const render_pipeline::ScaleSelection selection = render_pipeline::ScalingLogic::Choose(
            desired_scale,
            scale_steps,
            hysteresis_state,
            smoothed_scale,
            hysteresis_options);

        auto compute_bounds = [&](int index) {
                if (scale_steps.empty()) {
                        return std::make_pair(0.0f, std::numeric_limits<float>::max());
                }
                const float margin = hysteresis_options.margin;
                const int clamped_index = std::clamp(index, 0, static_cast<int>(scale_steps.size() - 1));
                const float step_value  = scale_steps[clamped_index];
                float min_bound = 0.0f;
                float max_bound = std::numeric_limits<float>::max();
                if (clamped_index + 1 < static_cast<int>(scale_steps.size())) {
                        const float boundary = 0.5f * (step_value + scale_steps[clamped_index + 1]);
                        min_bound = std::max(0.0f, boundary - margin);
                }
                if (clamped_index > 0) {
                        const float boundary = 0.5f * (step_value + scale_steps[clamped_index - 1]);
                        max_bound = boundary + margin;
                }
                if (min_bound > max_bound) {
                        const float midpoint = 0.5f * (min_bound + max_bound);
                        min_bound            = std::min(min_bound, midpoint);
                        max_bound            = std::max(max_bound, midpoint);
                }
                return std::make_pair(min_bound, max_bound);
        };

        auto ensure_downscale_entry = [&](int index, float scale_value) -> Asset::DownscaleCacheEntry* {
                if (index <= 0 || static_cast<std::size_t>(index) >= asset->downscale_cache_.size()) {
                        return nullptr;
                }
                Asset::DownscaleCacheEntry& entry = asset->downscale_cache_[index];
                const int expected_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_w) * scale_value)));
                const int expected_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_h) * scale_value)));
                const bool needs_rebuild =
                    !entry.texture ||
                    entry.width  != expected_w ||
                    entry.height != expected_h ||
                    std::fabs(entry.scale - scale_value) > 0.0001f;

                if (needs_rebuild) {
                        if (entry.texture) {
                                SDL_DestroyTexture(entry.texture);
                                entry.texture = nullptr;
                        }
                        SDL_Texture* scaled = render_pipeline::CreateScaledTexture(renderer_, base_tex, base_w, base_h, scale_value);
                        if (!scaled) {
                                entry.texture  = nullptr;
                                entry.width    = 0;
                                entry.height   = 0;
                                entry.scale    = scale_value;
                                entry.revision = 0;
                                return nullptr;
                        }
                        entry.texture  = scaled;
                        entry.width    = expected_w;
                        entry.height   = expected_h;
                        entry.scale    = scale_value;
                        entry.revision = asset->final_texture_revision_;
                } else if (entry.revision != asset->final_texture_revision_) {
                        if (!rerender_scaled_texture(renderer_, entry.texture, base_tex, expected_w, expected_h)) {
                                SDL_DestroyTexture(entry.texture);
                                entry.texture  = nullptr;
                                entry.width    = 0;
                                entry.height   = 0;
                                entry.scale    = scale_value;
                                entry.revision = 0;

                                SDL_Texture* scaled = render_pipeline::CreateScaledTexture(renderer_, base_tex, base_w, base_h, scale_value);
                                if (!scaled) {
                                        return nullptr;
                                }

                                entry.texture  = scaled;
                                entry.width    = expected_w;
                                entry.height   = expected_h;
                                entry.scale    = scale_value;
                        }

                        entry.width    = expected_w;
                        entry.height   = expected_h;
                        entry.scale    = scale_value;
                        entry.revision = asset->final_texture_revision_;
                }

                return entry.texture ? &entry : nullptr;
        };

        auto warm_scale_variants = [&]() {
                const std::size_t variant_count = std::min(scale_steps.size(), asset->downscale_cache_.size());
                if (variant_count <= 1) {
                        asset->downscale_cache_ready_revision_ = asset->final_texture_revision_;
                        return;
                }

                if (asset->downscale_cache_ready_revision_ == asset->final_texture_revision_) {
                        return;
                }

                bool all_ready = true;
                for (std::size_t idx = 1; idx < variant_count; ++idx) {
                        const float step = scale_steps[idx];
                        if (!std::isfinite(step) || step <= 0.0f) {
                                continue;
                        }

                        Asset::DownscaleCacheEntry* entry = ensure_downscale_entry(static_cast<int>(idx), step);
                        if (!entry || !entry->texture) {
                                all_ready = false;
                        }
                }

                if (all_ready) {
                        asset->downscale_cache_ready_revision_ = asset->final_texture_revision_;
                }
        };

        warm_scale_variants();

        if (selection.preload_index > 0 && static_cast<std::size_t>(selection.preload_index) < scale_steps.size()) {
                ensure_downscale_entry(selection.preload_index, scale_steps[selection.preload_index]);
        }

        SDL_Texture* result          = base_tex;
        float        texture_scale   = 1.0f;
        float        remainder_scale = desired_scale;
        int          variant_index   = 0;
        auto         default_bounds  = compute_bounds(0);
        float        hysteresis_min  = default_bounds.first;
        float        hysteresis_max  = default_bounds.second;

        const bool can_use_variant =
            selection.index > 0 &&
            selection.stored_scale < 0.995f &&
            static_cast<std::size_t>(selection.index) < scale_steps.size();

        if (can_use_variant) {
                if (Asset::DownscaleCacheEntry* entry = ensure_downscale_entry(selection.index, selection.stored_scale)) {
                        if (entry->texture) {
                                result          = entry->texture;
                                texture_scale   = selection.stored_scale;
                                remainder_scale = selection.remainder_scale;
                                variant_index   = selection.index;
                                hysteresis_min  = selection.hysteresis_min;
                                hysteresis_max  = selection.hysteresis_max;
                        }
                }
        } else {
                hysteresis_min = selection.hysteresis_min;
                hysteresis_max = selection.hysteresis_max;
        }

        if (variant_index == 0) {
                auto bounds = compute_bounds(0);
                hysteresis_min = bounds.first;
                hysteresis_max = bounds.second;
        }

        asset->update_scale_usage(desired_scale,
                                  texture_scale,
                                  remainder_scale,
                                  variant_index,
                                  hysteresis_min,
                                  hysteresis_max);
        return result ? result : base_tex;
}


////////////////////////////////////////////////////////////////////////////////
// Shading stages: RenderAsset and RenderShadowMask
////////////////////////////////////////////////////////////////////////////////




namespace render_pipeline::shading {

namespace {

void ensure_texture_defaults(SDL_Texture* texture, Asset::MaskRenderMetadata::TextureDefaults& defaults) {
    if (!texture) {
        defaults.reset();
        return;
    }
    if (defaults.texture != texture) {
        defaults.reset();
        defaults.texture = texture;
    }
    if (!defaults.initialized) {
        Uint8 r = defaults.r;
        Uint8 g = defaults.g;
        Uint8 b = defaults.b;
        Uint8 a = defaults.a;
        SDL_BlendMode blend = defaults.blend;
        if (SDL_GetTextureColorMod(texture, &r, &g, &b) != 0) {
            r = g = b = 255;
        }
        if (SDL_GetTextureAlphaMod(texture, &a) != 0) {
            a = 255;
        }
        if (SDL_GetTextureBlendMode(texture, &blend) != 0) {
            blend = SDL_BLENDMODE_BLEND;
        }
        defaults.r = r;
        defaults.g = g;
        defaults.b = b;
        defaults.a = a;
        defaults.blend = blend;
        defaults.initialized = true;
    }
}

void ensure_mask_dimensions(Asset::MaskRenderMetadata& metadata,
                            SDL_Texture*               texture,
                            int                        fallback_w,
                            int                        fallback_h) {
    if (!texture) {
        metadata.mask_w         = std::max(1, fallback_w);
        metadata.mask_h         = std::max(1, fallback_h);
        metadata.last_mask_texture = nullptr;
        metadata.has_dimensions = true;
        return;
    }

    if (metadata.last_mask_texture != texture) {
        metadata.last_mask_texture = texture;
        metadata.has_dimensions    = false;
    }

    if (!metadata.has_dimensions) {
        int queried_w = fallback_w;
        int queried_h = fallback_h;
        if (SDL_QueryTexture(texture, nullptr, nullptr, &queried_w, &queried_h) != 0) {
            queried_w = fallback_w;
            queried_h = fallback_h;
        }
        metadata.mask_w         = std::max(1, queried_w);
        metadata.mask_h         = std::max(1, queried_h);
        metadata.has_dimensions = true;
    }
}

#if SDL_VERSION_ATLEAST(2, 0, 6)
SDL_BlendMode cached_crop_blend_mode() {
    static SDL_BlendMode blend     = SDL_BLENDMODE_INVALID;
    static bool          computed  = false;
    if (!computed) {
        blend    = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,
                                              SDL_BLENDFACTOR_SRC_ALPHA,
                                              SDL_BLENDOPERATION_ADD,
                                              SDL_BLENDFACTOR_ZERO,
                                              SDL_BLENDFACTOR_SRC_ALPHA,
                                              SDL_BLENDOPERATION_ADD);
        computed = true;
    }
    return blend;
}
#endif

} // namespace

void ClearShadowStateFor(const Asset*) {}

bool RenderAsset::supports(const Asset& asset) const {
    return asset.get_current_frame() != nullptr;
}

SDL_Texture* RenderAsset::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    if (!renderer) {
        return nullptr;
    }

    SDL_Texture* base_texture = context.base_texture ? context.base_texture : asset.get_current_frame();
    if (!base_texture) {
        return nullptr;
    }

    int width  = context.width;
    int height = context.height;
    if (width <= 0 || height <= 0) {
        SDL_QueryTexture(base_texture, nullptr, nullptr, &width, &height);
        context.width  = width;
        context.height = height;
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    SDL_Texture* target = nullptr;
    if (context.reusable_final) {
        int tex_w = 0;
        int tex_h = 0;
        if (SDL_QueryTexture(context.reusable_final, nullptr, nullptr, &tex_w, &tex_h) == 0 && tex_w == width && tex_h == height) {
            target = context.reusable_final;
        }
    }

    if (!target) {
        target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!target) {
            return nullptr;
        }
    }

    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(target, (asset.info && !asset.info->smooth_scaling) ? SDL_ScaleModeNearest : SDL_ScaleModeBest);
#endif

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_SetTextureAlphaMod(base_texture, 255);
    SDL_SetTextureColorMod(base_texture, 255, 255, 255);
    SDL_RenderCopy(renderer, base_texture, nullptr, nullptr);
    SDL_SetTextureAlphaMod(base_texture, 255);
    SDL_SetTextureColorMod(base_texture, 255, 255, 255);

    SDL_SetRenderTarget(renderer, prev_target);

    return target;
}

bool RenderShadowMask::supports(const Asset& asset) const {
    return asset.is_shaded;
}

SDL_Texture* RenderShadowMask::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    if (!renderer || !asset.is_shaded) {
        return nullptr;
    }

    int width  = context.width;
    int height = context.height;
    if (width <= 0 || height <= 0) {
        if (SDL_Texture* base = context.base_texture) {
            SDL_QueryTexture(base, nullptr, nullptr, &width, &height);
            context.width  = width;
            context.height = height;
        }
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    auto& cache = asset.shadow_mask_cache();
    const auto ensure_cache_target = [&]() -> SDL_Texture* {
        if (cache.texture) {
            const bool metadata_matches = cache.width == width && cache.height == height && cache.width > 0 && cache.height > 0;
            if (!metadata_matches) {
                int tex_w = 0;
                int tex_h = 0;
                if (SDL_QueryTexture(cache.texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w != width || tex_h != height) {
                    SDL_DestroyTexture(cache.texture);
                    cache.texture = nullptr;
                    cache.width   = 0;
                    cache.height  = 0;
                } else {
                    cache.width  = tex_w;
                    cache.height = tex_h;
                }
            }
        }

        if (!cache.texture) {
            cache.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
            if (!cache.texture) {
                cache.width  = 0;
                cache.height = 0;
                return nullptr;
            }
        }
        SDL_SetTextureBlendMode(cache.texture, SDL_BLENDMODE_BLEND);
        cache.width  = width;
        cache.height = height;
        return cache.texture;
    };

    SDL_Texture* destination = nullptr;
    if (context.stage_destination) {
        int dst_w = 0;
        int dst_h = 0;
        if (SDL_QueryTexture(context.stage_destination, nullptr, nullptr, &dst_w, &dst_h) == 0 && dst_w == width && dst_h == height) {
            destination = context.stage_destination;
        }
    }

    const float opacity = context.base_shadow_opacity;

    // TODO(#reactive-shadows): Mask currently renders unadjusted until new settings land.

    SDL_Texture* mask_texture = nullptr;
    const auto&  scale_usage  = asset.last_scale_usage();
    std::size_t  mask_variant = (scale_usage.variant_index < 0) ? 0u : static_cast<std::size_t>(scale_usage.variant_index);
    if (SDL_Texture* mask = asset.get_current_mask_texture(mask_variant)) {
        mask_texture = mask;
    } else {
        mask_texture = context.base_texture;
    }

    if (!mask_texture) {
        if (destination) {
            return destination;
        }
        return ensure_cache_target();
    }

    Asset::MaskRenderMetadata& metadata = asset.mask_render_metadata();
    ensure_mask_dimensions(metadata, mask_texture, width, height);
    ensure_texture_defaults(mask_texture, metadata.mask_defaults);

    const auto render_mask = [&](SDL_Texture* target, SDL_BlendMode blend_mode, bool clear_target) {
        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
        SDL_BlendMode prev_blend = SDL_BLENDMODE_INVALID;
        if (SDL_GetRenderDrawBlendMode(renderer, &prev_blend) != 0) {
            prev_blend = SDL_BLENDMODE_INVALID;
        }

        SDL_SetRenderTarget(renderer, target);
        SDL_SetRenderDrawBlendMode(renderer, blend_mode);
        if (clear_target) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
        }

        SDL_SetTextureBlendMode(mask_texture, SDL_BLENDMODE_BLEND);
        const Uint8 shade_alpha = static_cast<Uint8>(std::lround(opacity * 255.0f));
        SDL_SetTextureColorMod(mask_texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(mask_texture, shade_alpha);

        const int   scaled_w      = std::max(1, metadata.mask_w);
        const int   scaled_h      = std::max(1, metadata.mask_h);
        const float base_center_x = static_cast<float>(width) * 0.5f;
        const float base_center_y = static_cast<float>(height) * 0.5f;
        const float dest_x_f      = base_center_x - static_cast<float>(scaled_w) * 0.5f;
        const float dest_y_f      = base_center_y - static_cast<float>(scaled_h) * 0.5f;
        const int   dest_px_x     = static_cast<int>(std::lround(dest_x_f));
        const int   dest_px_y     = static_cast<int>(std::lround(dest_y_f));
        SDL_Rect    dest{dest_px_x, dest_px_y, scaled_w, scaled_h};
        SDL_RenderCopy(renderer, mask_texture, nullptr, &dest);

        SDL_SetTextureBlendMode(mask_texture, metadata.mask_defaults.blend);
        SDL_SetTextureColorMod(mask_texture, metadata.mask_defaults.r, metadata.mask_defaults.g, metadata.mask_defaults.b);
        SDL_SetTextureAlphaMod(mask_texture, metadata.mask_defaults.a);

#if SDL_VERSION_ATLEAST(2, 0, 6)
        if (SDL_Texture* base_mask = context.base_texture ? context.base_texture : asset.get_current_frame()) {
            ensure_texture_defaults(base_mask, metadata.base_defaults);
            const SDL_BlendMode crop_blend = cached_crop_blend_mode();
            if (crop_blend != SDL_BLENDMODE_INVALID) {
                SDL_SetTextureBlendMode(base_mask, crop_blend);
                SDL_SetTextureColorMod(base_mask, 255, 255, 255);
                SDL_SetTextureAlphaMod(base_mask, 255);
                SDL_RenderCopy(renderer, base_mask, nullptr, nullptr);
                SDL_SetTextureBlendMode(base_mask, metadata.base_defaults.blend);
                SDL_SetTextureColorMod(base_mask, metadata.base_defaults.r, metadata.base_defaults.g, metadata.base_defaults.b);
                SDL_SetTextureAlphaMod(base_mask, metadata.base_defaults.a);
            }
        }
#endif
        // TODO(#reactive-shadows): Runtime subtraction is disabled until reactive tuning returns.

        SDL_SetRenderTarget(renderer, prev_target);
        if (prev_blend != SDL_BLENDMODE_INVALID) {
            SDL_SetRenderDrawBlendMode(renderer, prev_blend);
        } else {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        }
    };

    if (destination) {
        render_mask(destination, context.stage_blend, false);
        context.stage_drew_to_destination = true;
        return destination;
    }

    SDL_Texture* cache_target = ensure_cache_target();
    if (!cache_target) {
        return nullptr;
    }
    render_mask(cache_target, SDL_BLENDMODE_BLEND, true);
    return cache_target;
}

}
