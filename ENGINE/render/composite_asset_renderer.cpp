#include "composite_asset_renderer.hpp"
#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame_variant.hpp"
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

    if (std::abs(asset->composite_scale_ - package_scale) > 0.001f) {
        asset->mark_composite_dirty();
    }

    if (asset->is_composite_dirty()) {
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
                                    bool apply_scale = true,
                                    SDL_RendererFlip flip = SDL_FLIP_NONE) {
        if (!tex) return;
        if (apply_scale) {
            rect.w = static_cast<int>(std::lround(static_cast<float>(rect.w) * package_scale));
            rect.h = static_cast<int>(std::lround(static_cast<float>(rect.h) * package_scale));
            rect.w = std::max(1, rect.w);
            rect.h = std::max(1, rect.h);
        }
        RenderObject obj{};
        obj.texture   = tex;
        obj.screen_rect = rect;
        obj.color_mod = color;
        obj.blend_mode = blend;
        obj.flip       = flip;
        asset->scene_mask_lights.push_back(obj);
};

    auto compute_light_color = [&](const LightSource& light) -> std::optional<SDL_Color> {
        const int raw_intensity = std::clamp(light.intensity, 0, 255);
        if (raw_intensity <= 0) {
            return std::nullopt;
        }

        const float flicker_multiplier =
            LightFlickerCalculator::compute_multiplier(light, flicker_time_seconds);

        int scaled_intensity = static_cast<int>( std::lround(static_cast<float>(raw_intensity) * flicker_multiplier));
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

    if (asset->info) {
        for (const auto& light_source : asset->info->light_sources) {
            if (light_source.behind && light_source.texture) {
                const auto light_color = compute_light_color(light_source);
                if (!light_color) {
                    continue;
                }

                int offset_x = light_source.offset_x;
                if (asset->flipped) {
                    offset_x = -offset_x;
                }

                int w, h;
                SDL_QueryTexture(light_source.texture, nullptr, nullptr, &w, &h);
                SDL_Rect dest_rect = {
                    static_cast<int>(asset->pos.x + offset_x * package_scale), static_cast<int>(asset->pos.y + light_source.offset_y * package_scale), w, h };
                SDL_RendererFlip light_flip = asset->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

                add_render_object(light_source.texture, dest_rect, *light_color, SDL_BLENDMODE_ADD, true, 0.0, std::nullopt, light_flip);

                if (light_source.render_to_dark_mask) {
                    add_scene_mask_light(light_source.texture, dest_rect, *light_color, SDL_BLENDMODE_ADD, true, light_flip);
                }
            }
        }
    }

    auto emit_child = [&](const Asset::AnimationChildAttachment& slot) {
        if (slot.child_index < 0 || !slot.visible || !slot.animation || !slot.current_frame) {
            return;
        }

        float child_base_scale = (slot.info && std::isfinite(slot.info->scale_factor) && slot.info->scale_factor > 0.0f) ? slot.info->scale_factor : 1.0f;

        float child_current_scale = child_base_scale * perspective_scale;

        float camera_scale = 1.0f;
        if (assets_) {
            camera_scale = std::max(0.0001f, assets_->getView().get_scale());
        }

        float child_desired_variant_scale = child_current_scale / camera_scale;
        if (!std::isfinite(child_desired_variant_scale) || child_desired_variant_scale <= 0.0f) {
            child_desired_variant_scale = child_current_scale;
        }

        const auto& child_steps = (slot.info && !slot.info->scale_variants.empty()) ? static_cast<const std::vector<float>&>(slot.info->scale_variants) : render_pipeline::ScalingLogic::DefaultScaleSteps();

        auto child_selection = render_pipeline::ScalingLogic::Choose(child_desired_variant_scale, child_steps);
        float child_nearest_variant_scale = child_selection.stored_scale;

        float child_remaining_adjustment = 1.0f;
        if (child_nearest_variant_scale > 0.0f) {
            child_remaining_adjustment = child_current_scale / child_nearest_variant_scale;
        }

        const FrameVariant* variant =
            slot.animation->get_frame(slot.current_frame, child_nearest_variant_scale);
        SDL_Texture* tex = variant ? variant->get_base_texture() : nullptr;
        if (!tex && slot.current_frame && !slot.current_frame->variants.empty()) {
            tex = slot.current_frame->variants[0].base_texture;
        }
        if (!tex) {
            return;
        }

        int tex_w = 0;
        int tex_h = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tex_w, &tex_h);

        const float base_adjustment = child_remaining_adjustment / std::max(0.0001f, perspective_scale);
        int final_w = static_cast<int>(std::lround(static_cast<float>(tex_w) * base_adjustment));
        int final_h = static_cast<int>(std::lround(static_cast<float>(tex_h) * base_adjustment));
        final_w = std::max(1, final_w);
        final_h = std::max(1, final_h);

        SDL_Rect dest_rect{
            slot.world_pos.x,
            slot.world_pos.y,
            final_w,
            final_h
};
        SDL_Point pivot{ final_w / 2, final_h };
        SDL_RendererFlip flip = asset->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        add_render_object(tex,
                          dest_rect,
                          SDL_Color{255, 255, 255, 255},
                          SDL_BLENDMODE_BLEND,
                          false,
                          static_cast<double>(slot.rotation_degrees),
                          pivot,
                          flip);
};

    for (const auto& child_attachment : asset->animation_children()) {
        if (child_attachment.render_in_front) {
            continue;
        }
        emit_child(child_attachment);
    }

    SDL_Texture* base_tex = nullptr;

    const Animation* anim_ptr = nullptr;
    if (asset->info) {
        auto anim_it = asset->info->animations.find(asset->current_animation);
        if (anim_it != asset->info->animations.end()) {
            anim_ptr = &anim_it->second;
            if (asset->current_frame) {
                const auto& variants = asset->current_frame->variants;
                if (!variants.empty()) {
                    int variant_idx = asset->current_variant_index;
                    variant_idx = std::clamp(variant_idx, 0, static_cast<int>(variants.size()) - 1);
                    const FrameVariant& variant = variants[static_cast<std::size_t>(variant_idx)];
                    base_tex = variant.get_base_texture();
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

    }

    for (const auto& child_attachment : asset->animation_children()) {
        if (!child_attachment.render_in_front) {
            continue;
        }
        emit_child(child_attachment);
    }

    if (asset->info) {
        for (const auto& light_source : asset->info->light_sources) {
            if (light_source.in_front && light_source.texture) {
                const auto light_color = compute_light_color(light_source);
                if (!light_color) {
                    continue;
                }

                int offset_x = light_source.offset_x;
                if (asset->flipped) {
                    offset_x = -offset_x;
                }

                int w, h;
                SDL_QueryTexture(light_source.texture, nullptr, nullptr, &w, &h);
                SDL_Rect dest_rect = {
                    static_cast<int>(asset->pos.x + offset_x * package_scale), static_cast<int>(asset->pos.y + light_source.offset_y * package_scale), w, h };
                SDL_RendererFlip light_flip = asset->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

                add_render_object(light_source.texture, dest_rect, *light_color, SDL_BLENDMODE_ADD, true, 0.0, std::nullopt, light_flip);

                if (light_source.render_to_dark_mask) {
                    add_scene_mask_light(light_source.texture, dest_rect, *light_color, SDL_BLENDMODE_ADD, true, light_flip);
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

    bounds.x -= asset->pos.x;
    bounds.y -= asset->pos.y;

    asset->composite_bounds_local_ = bounds;
}
