#include "composite_asset_renderer.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "world/world_grid.hpp"
#include "world/grid_point.hpp"
#include "render/light_flicker.hpp"
#include "render/render.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

CompositeAssetRenderer::CompositeAssetRenderer(SDL_Renderer* renderer, Assets* assets)
    : renderer_(renderer), assets_(assets) {}

CompositeAssetRenderer::~CompositeAssetRenderer() {}

void CompositeAssetRenderer::update(Asset* asset,
                                    const world::GridPoint* gp,
                                    float flicker_time_seconds) {
    if (!asset) return;

    // Recursively update children first
    bool children_dirty = false;
    for (const auto& child_attachment : asset->animation_children()) {
        if (child_attachment.child_index < 0) {
            continue;
        }
        if (child_attachment.spawned_asset) {
            Asset* child = child_attachment.spawned_asset;
            const bool child_was_dirty = child->is_composite_dirty();
            // Children do not have their own grid point, they are relative to the parent
            update(child, nullptr, flicker_time_seconds);
            if (child_was_dirty || child->is_composite_dirty()) {
                children_dirty = true;
            }
        }
    }

    float combined_scale = asset->current_nearest_variant_scale * asset->current_remaining_scale_adjustment;
    if (!std::isfinite(combined_scale) || combined_scale <= 0.0f) {
        combined_scale = 1.0f;
    }

    float perspective_scale = 1.0f;
    if (asset->info && asset->info->apply_distance_scaling && gp) {
        perspective_scale = std::max(0.0001f, gp->perspective_scale);
    }

    float package_scale = combined_scale * perspective_scale;
    if (!std::isfinite(package_scale) || package_scale <= 0.0f) {
        package_scale = 1.0f;
    }

    // Check if scale changed significantly
    if (std::abs(asset->composite_scale_ - package_scale) > 0.001f) {
        asset->mark_composite_dirty();
    }

    // If asset is dirty or any child is dirty, regenerate
    if (asset->is_composite_dirty() || children_dirty) {
        regenerate_package(asset, gp, flicker_time_seconds, package_scale, perspective_scale);
    } else {
        asset->composite_scale_ = package_scale;
    }
}

void CompositeAssetRenderer::regenerate_package(Asset* asset,
                                                const world::GridPoint* gp,
                                                float flicker_time_seconds,
                                                float package_scale,
                                                float perspective_scale) {
    if (!renderer_ || !asset) return;

    asset->render_package.clear();
    asset->scene_mask_lights.clear();

    asset->composite_scale_ = package_scale;

    // Helper to add a render object
    auto add_render_object = [&](SDL_Texture* tex,
                                 SDL_Rect rect,
                                 SDL_Color color = {255, 255, 255, 255},
                                 SDL_BlendMode blend = SDL_BLENDMODE_BLEND,
                                 bool apply_scale = true,
                                 double angle = 0.0,
                                 std::optional<SDL_Point> center = std::nullopt,
                                 SDL_RendererFlip flip = SDL_FLIP_NONE) {
        if (!tex) return;
        if (apply_scale) {
            rect.w = static_cast<int>(std::lround(static_cast<float>(rect.w) * package_scale));
            rect.h = static_cast<int>(std::lround(static_cast<float>(rect.h) * package_scale));
            rect.w = std::max(1, rect.w);
            rect.h = std::max(1, rect.h);
        }

        SDL_Point c = {0, 0};
        bool custom = false;
        if (center.has_value()) {
            c = center.value();
            if (apply_scale) {
                 c.x = static_cast<int>(std::lround(static_cast<float>(c.x) * package_scale));
                 c.y = static_cast<int>(std::lround(static_cast<float>(c.y) * package_scale));
            }
            custom = true;
        }

        asset->render_package.push_back({tex, rect, color, blend, angle, c, custom, flip});
    };

    auto add_scene_mask_light = [&](SDL_Texture* tex,
                                    SDL_Rect rect,
                                    SDL_Color color = {255, 255, 255, 255},
                                    SDL_BlendMode blend = SDL_BLENDMODE_BLEND,
                                    bool apply_scale = true) {
        if (!tex) return;
        if (apply_scale) {
            rect.w = static_cast<int>(std::lround(static_cast<float>(rect.w) * package_scale));
            rect.h = static_cast<int>(std::lround(static_cast<float>(rect.h) * package_scale));
            rect.w = std::max(1, rect.w);
            rect.h = std::max(1, rect.h);
        }
        asset->scene_mask_lights.push_back({tex, rect, color, blend});
    };

    auto compute_light_color = [&](const LightSource& light) -> std::optional<SDL_Color> {
        const int raw_intensity = std::clamp(light.intensity, 0, 255);
        if (raw_intensity <= 0) {
            return std::nullopt;
        }

        const float flicker_multiplier =
            LightFlickerCalculator::compute_multiplier(light, flicker_time_seconds);

        int scaled_intensity = static_cast<int>(
            std::lround(static_cast<float>(raw_intensity) * flicker_multiplier));
        scaled_intensity = std::clamp(scaled_intensity, 0, 255);
        if (scaled_intensity <= 0) {
            return std::nullopt;
        }

        const float scale = static_cast<float>(scaled_intensity) / 255.0f;
        SDL_Color color = light.color;

        auto scale_channel = [&](Uint8 channel) -> Uint8 {
            const int scaled = static_cast<int>(std::lround(static_cast<float>(channel) * scale));
            return static_cast<Uint8>(std::clamp(scaled, 0, 255));
        };

        color.r = scale_channel(color.r);
        color.g = scale_channel(color.g);
        color.b = scale_channel(color.b);
        color.a = scale_channel(color.a);
        if (color.a == 0) {
            color.a = static_cast<Uint8>(scaled_intensity);
        }

        return color;
    };

    // 1. Behind lights (and mask light bookkeeping)
    if (asset->info) {
        for (const auto& light_source : asset->info->light_sources) {
            if (light_source.behind && light_source.texture) {
                const auto light_color = compute_light_color(light_source);
                if (!light_color) {
                    continue;
                }

                int w, h;
                SDL_QueryTexture(light_source.texture, nullptr, nullptr, &w, &h);
                SDL_Rect dest_rect = {
                    static_cast<int>(asset->pos.x + light_source.offset_x * package_scale),
                    static_cast<int>(asset->pos.y + light_source.offset_y * package_scale),
                    w,
                    h
                };

                add_render_object(light_source.texture, dest_rect, *light_color);

                if (light_source.render_to_dark_mask) {
                    add_scene_mask_light(light_source.texture,
                                         dest_rect,
                                         *light_color,
                                         SDL_BLENDMODE_BLEND,
                                         false);
                }
            }
        }
    }

    // 2. Child animation assets behind
    for (const auto& child_attachment : asset->animation_children()) {
        if (child_attachment.child_index >= 0 &&
            child_attachment.visible &&
            !child_attachment.render_in_front &&
            child_attachment.spawned_asset) {

            Asset* child = child_attachment.spawned_asset;
            for (const auto& render_obj : child->render_package) {
                // Child render packages are already expressed in world space.
                SDL_Rect child_rect = render_obj.screen_rect;

                double final_angle = render_obj.angle + static_cast<double>(child_attachment.rotation_degrees);
                std::optional<SDL_Point> center_opt = std::nullopt;
                if (render_obj.use_custom_center) {
                    center_opt = render_obj.center;
                }

                add_render_object(render_obj.texture,
                                  child_rect,
                                  render_obj.color_mod,
                                  render_obj.blend_mode,
                                  false,
                                  final_angle,
                                  center_opt,
                                  render_obj.flip);
            }
        }
    }

    // 3. Base Asset and Depth Cue
    SDL_Texture* base_tex = nullptr;
    SDL_Texture* fg_tex = nullptr;
    SDL_Texture* bg_tex = nullptr;

    const Animation* anim_ptr = nullptr;
    if (asset->info) {
        auto anim_it = asset->info->animations.find(asset->current_animation);
        if (anim_it != asset->info->animations.end()) {
            anim_ptr = &anim_it->second;
            if (asset->current_frame) {
                const FrameVariant* variant =
                    anim_ptr->get_frame(asset->current_frame,
                                        asset->current_nearest_variant_scale);
                if (variant) {
                    base_tex = variant->get_base_texture();
                    fg_tex = variant->get_depthcue_foreground_texture();
                    bg_tex = variant->get_depthcue_background_texture();
                }
            }
        }
    }

    if (!base_tex) {
        base_tex = asset->get_current_frame();
    }

    if (base_tex) {
        int w, h;
        SDL_QueryTexture(base_tex, nullptr, nullptr, &w, &h);
        
        float remainder = asset->current_remaining_scale_adjustment;
        if (!std::isfinite(remainder) || remainder <= 0.0f) {
            remainder = 1.0f;
        }
        const float perspective_denominator = std::max(0.0001f, perspective_scale);
        const float base_adjustment = remainder / perspective_denominator;
        int final_w = static_cast<int>(std::lround(static_cast<float>(w) * base_adjustment));
        int final_h = static_cast<int>(std::lround(static_cast<float>(h) * base_adjustment));
        final_w = std::max(1, final_w);
        final_h = std::max(1, final_h);

        SDL_Rect dest_rect = {
            asset->pos.x,
            asset->pos.y,
            final_w,
            final_h
        };
        add_render_object(base_tex, dest_rect, SDL_Color{255, 255, 255, 255}, SDL_BLENDMODE_BLEND, false);
    }

    if (gp) {
        // Background
        /*
        if (bg_tex && gp->depth_cue_background_opacity > 0.0f) {
            int w, h;
            SDL_QueryTexture(bg_tex, nullptr, nullptr, &w, &h);
            SDL_Rect dest_rect = {
                asset->pos.x,
                asset->pos.y,
                static_cast<int>(w * effective_scale),
                static_cast<int>(h * effective_scale)
            };
            SDL_Color color = {255, 255, 255,
                               static_cast<Uint8>(gp->depth_cue_background_opacity * 255)};
            add_render_object(bg_tex, dest_rect, color);
        }
        */

        // Foreground
        /*
        if (fg_tex && gp->depth_cue_foreground_opacity > 0.0f) {
            int w, h;
            SDL_QueryTexture(fg_tex, nullptr, nullptr, &w, &h);
            SDL_Rect dest_rect = {
                asset->pos.x,
                asset->pos.y,
                static_cast<int>(w * effective_scale),
                static_cast<int>(h * effective_scale)
            };
            SDL_Color color = {255, 255, 255,
                               static_cast<Uint8>(gp->depth_cue_foreground_opacity * 255)};
            add_render_object(fg_tex, dest_rect, color);
        }
        */
    }

    // 5. Child animation assets in front
    for (const auto& child_attachment : asset->animation_children()) {
        if (child_attachment.child_index >= 0 &&
            child_attachment.visible &&
            child_attachment.render_in_front &&
            child_attachment.spawned_asset) {

            Asset* child = child_attachment.spawned_asset;
            for (const auto& render_obj : child->render_package) {
                // Child render packages are already expressed in world space.
                SDL_Rect child_rect = render_obj.screen_rect;

                double final_angle = render_obj.angle + static_cast<double>(child_attachment.rotation_degrees);
                std::optional<SDL_Point> center_opt = std::nullopt;
                if (render_obj.use_custom_center) {
                    center_opt = render_obj.center;
                }

                add_render_object(render_obj.texture,
                                  child_rect,
                                  render_obj.color_mod,
                                  render_obj.blend_mode,
                                  false,
                                  final_angle,
                                  center_opt,
                                  render_obj.flip);
            }
        }
    }

    // 6. In-front lights
    if (asset->info) {
        for (const auto& light_source : asset->info->light_sources) {
            if (light_source.in_front && light_source.texture) {
                const auto light_color = compute_light_color(light_source);
                if (!light_color) {
                    continue;
                }

                int w, h;
                SDL_QueryTexture(light_source.texture, nullptr, nullptr, &w, &h);
                SDL_Rect dest_rect = {
                    static_cast<int>(asset->pos.x + light_source.offset_x * package_scale),
                    static_cast<int>(asset->pos.y + light_source.offset_y * package_scale),
                    w,
                    h
                };

                add_render_object(light_source.texture, dest_rect, *light_color);

                if (light_source.render_to_dark_mask) {
                    add_scene_mask_light(light_source.texture,
                                         dest_rect,
                                         *light_color,
                                         SDL_BLENDMODE_BLEND,
                                         false);
                }
            }
        }
    }

    asset->clear_composite_dirty();
    calculate_local_bounds(asset);
}

void CompositeAssetRenderer::calculate_local_bounds(Asset* asset) {
    if (!asset || asset->render_package.empty()) {
        asset->composite_bounds_local_ = {0, 0, 0, 0};
        return;
    }

    SDL_Rect bounds = asset->render_package[0].screen_rect;

    for (size_t i = 1; i < asset->render_package.size(); ++i) {
        const SDL_Rect& rect = asset->render_package[i].screen_rect;
        int new_x = std::min(bounds.x, rect.x);
        int new_y = std::min(bounds.y, rect.y);
        int new_w = std::max(bounds.x + bounds.w, rect.x + rect.w) - new_x;
        int new_h = std::max(bounds.y + bounds.h, rect.y + rect.h) - new_y;
        bounds = {new_x, new_y, new_w, new_h};
    }

    // The package rects are in world space relative to asset->pos.
    // We want local space, so subtract asset->pos.
    bounds.x -= asset->pos.x;
    bounds.y -= asset->pos.y;

    asset->composite_bounds_local_ = bounds;
}
