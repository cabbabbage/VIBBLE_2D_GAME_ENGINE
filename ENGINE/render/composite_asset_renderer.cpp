#include "composite_asset_renderer.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "world/grid.hpp"
#include "world/grid_point.hpp"
#include "render/render.hpp"
#include <algorithm>
#include <cmath>

CompositeAssetRenderer::CompositeAssetRenderer(SDL_Renderer* renderer, Assets* assets)
    : renderer_(renderer), assets_(assets) {}

CompositeAssetRenderer::~CompositeAssetRenderer() {}

void CompositeAssetRenderer::update(Asset* asset, const world::GridPoint* gp, float desired_scale) {
    if (!asset) return;

    // Recursively update children first
    bool children_dirty = false;
    for (const auto& child_attachment : asset->animation_children()) {
        if (child_attachment.spawned_asset) {
            // Children don't have their own grid point, they are relative to the parent
            update(child_attachment.spawned_asset, nullptr, desired_scale);
            if (child_attachment.spawned_asset->is_composite_dirty()) {
                children_dirty = true;
            }
        }
    }

    // Check if scale changed significantly
    if (std::abs(asset->composite_scale_ - asset->current_nearest_variant_scale) > 0.001f) {
        asset->mark_composite_dirty();
    }

    // If asset is dirty or any child is dirty, regenerate
    if (asset->is_composite_dirty() || children_dirty) {
        regenerate_package(asset, gp, desired_scale);
    }
}

void CompositeAssetRenderer::regenerate_package(Asset* asset, const world::GridPoint* gp, float desired_scale) {
    if (!renderer_ || !asset) return;

    asset->render_package.clear();

    float effective_scale = asset->current_nearest_variant_scale * asset->current_remaining_scale_adjustment;
    
    asset->composite_scale_ = effective_scale;

    // Helper to add a render object
    auto add_render_object = [&](SDL_Texture* tex, SDL_Rect rect, SDL_Color color = {255,255,255,255}, SDL_BlendMode blend = SDL_BLENDMODE_BLEND) {
        if (!tex) return;
        asset->render_package.push_back({tex, rect, color, blend});
    };

    // 1. Behind Lights
    if (asset->info) {
        for (const auto& light_source : asset->info->light_sources) {
            if (light_source.behind && light_source.texture) {
                int w, h;
                SDL_QueryTexture(light_source.texture, nullptr, nullptr, &w, &h);
                SDL_Rect dest_rect = {
                    static_cast<int>(asset->pos.x + light_source.offset_x * effective_scale),
                    static_cast<int>(asset->pos.y + light_source.offset_y * effective_scale),
                    static_cast<int>(w * effective_scale),
                    static_cast<int>(h * effective_scale)
                };
                add_render_object(light_source.texture, dest_rect, light_source.color);
            }
        }
    }

    // 2. Child animation assets behind
    for (const auto& child_attachment : asset->animation_children()) {
        if (child_attachment.visible && !child_attachment.render_in_front && child_attachment.spawned_asset) {
            Asset* child = child_attachment.spawned_asset;
            for (const auto& render_obj : child->render_package) {
                SDL_Rect child_rect = render_obj.screen_rect;
                child_rect.x += (child->pos.x - asset->pos.x);
                child_rect.y += (child->pos.y - asset->pos.y);
                add_render_object(render_obj.texture, child_rect, render_obj.color_mod, render_obj.blend_mode);
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
                 const FrameVariant* variant = anim_ptr->get_frame(asset->current_frame, asset->current_nearest_variant_scale);
                 if (variant) {
                     base_tex = variant->get_base_texture();
                     fg_tex = variant->get_foreground_texture();
                     bg_tex = variant->get_background_texture();
                 }
             }
        }
    }
    if (!base_tex) base_tex = asset->get_current_frame();

    bool has_depth_cue = gp && ( (bg_tex && gp->depth_cue_background_opacity > 0.0f) || (fg_tex && gp->depth_cue_foreground_opacity > 0.0f) );

    if (base_tex) {
        if (has_depth_cue) {
            int w, h;
            SDL_QueryTexture(base_tex, nullptr, nullptr, &w, &h);
            
            if (asset->depth_cue_composite_texture_) {
                SDL_DestroyTexture(asset->depth_cue_composite_texture_);
            }
            asset->depth_cue_composite_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
            SDL_SetTextureBlendMode(asset->depth_cue_composite_texture_, SDL_BLENDMODE_BLEND);

            SDL_SetRenderTarget(renderer_, asset->depth_cue_composite_texture_);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
            SDL_RenderClear(renderer_);

            // Render base
            SDL_RenderCopy(renderer_, base_tex, nullptr, nullptr);

            // Render background cue
            if (bg_tex && gp->depth_cue_background_opacity > 0.0f) {
                SDL_SetTextureAlphaMod(bg_tex, static_cast<Uint8>(gp->depth_cue_background_opacity * 255));
                SDL_RenderCopy(renderer_, bg_tex, nullptr, nullptr);
            }

            // Render foreground cue
            if (fg_tex && gp->depth_cue_foreground_opacity > 0.0f) {
                SDL_SetTextureAlphaMod(fg_tex, static_cast<Uint8>(gp->depth_cue_foreground_opacity * 255));
                SDL_RenderCopy(renderer_, fg_tex, nullptr, nullptr);
            }

            SDL_SetRenderTarget(renderer_, nullptr);

            SDL_Rect dest_rect = {
                asset->pos.x,
                asset->pos.y,
                static_cast<int>(w * effective_scale),
                static_cast<int>(h * effective_scale)
            };
            add_render_object(asset->depth_cue_composite_texture_, dest_rect);

        } else {
            int w, h;
            SDL_QueryTexture(base_tex, nullptr, nullptr, &w, &h);
            SDL_Rect dest_rect = {
                asset->pos.x,
                asset->pos.y,
                static_cast<int>(w * effective_scale),
                static_cast<int>(h * effective_scale)
            };
            add_render_object(base_tex, dest_rect);
        }
    }

    // 5. Child animation assets in front
    for (const auto& child_attachment : asset->animation_children()) {
        if (child_attachment.visible && child_attachment.render_in_front && child_attachment.spawned_asset) {
            Asset* child = child_attachment.spawned_asset;
            for (const auto& render_obj : child->render_package) {
                SDL_Rect child_rect = render_obj.screen_rect;
                child_rect.x += (child->pos.x - asset->pos.x);
                child_rect.y += (child->pos.y - asset->pos.y);
                add_render_object(render_obj.texture, child_rect, render_obj.color_mod, render_obj.blend_mode);
            }
        }
    }

    // 6. In-front lights
    if (asset->info) {
        for (const auto& light_source : asset->info->light_sources) {
            if (light_source.in_front && light_source.texture) {
                int w, h;
                SDL_QueryTexture(light_source.texture, nullptr, nullptr, &w, &h);
                SDL_Rect dest_rect = {
                    static_cast<int>(asset->pos.x + light_source.offset_x * effective_scale),
                    static_cast<int>(asset->pos.y + light_source.offset_y * effective_scale),
                    static_cast<int>(w * effective_scale),
                    static_cast<int>(h * effective_scale)
                };
                add_render_object(light_source.texture, dest_rect, light_source.color);
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
