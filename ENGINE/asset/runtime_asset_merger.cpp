#include "runtime_asset_merger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "Asset.hpp"
#include "animation.hpp"
#include "asset_info.hpp"

#include "render/camera_grid.hpp"
#include "render/render.hpp"
#include "utils/area.hpp"
#include "utils/cache_manager.hpp"
#include "utils/log.hpp"

#include <nlohmann/json.hpp>

namespace runtime {

namespace {

namespace fs = std::filesystem;

SDL_Surface* texture_to_surface(SDL_Renderer* renderer, SDL_Texture* texture, int width, int height) {
    if (!renderer || !texture || width <= 0 || height <= 0) {
        return nullptr;
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surface) {
        return nullptr;
    }
    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        SDL_FreeSurface(surface);
        return nullptr;
    }
    const int read_status = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, surface->pixels, surface->pitch);
    SDL_SetRenderTarget(renderer, previous_target);
    if (read_status != 0) {
        SDL_FreeSurface(surface);
        return nullptr;
    }
    return surface;
}

void free_surface_lists(std::vector<std::vector<SDL_Surface*>>& lists) {
    for (auto& list : lists) {
        for (SDL_Surface*& surface : list) {
            if (surface) {
                SDL_FreeSurface(surface);
                surface = nullptr;
            }
        }
        list.clear();
    }
}

struct DummyEffectState {
    // Dummy structure since image effects are now handled by Python
};

DummyEffectState resolve_effect_state() {
    // Image effects are now handled by Python, so we don't need runtime resolvement
    return {};
}

std::optional<AssetInfo::NamedArea::RenderFrame> select_render_frame(const AssetInfo& info) {
    for (const auto& area : info.areas) {
        if (area.render_frame) {
            return area.render_frame;
        }
    }
    return std::nullopt;
}

float sanitize_scale(float value) {
    if (!std::isfinite(value) || value <= 0.0f) {
        return 1.0f;
    }
    return value;
}

bool has_custom_shadow_settings(const ShadowMaskSettings& settings) {
    const ShadowMaskSettings defaults{};
    const auto differs = [](float a, float b) {
        return std::fabs(a - b) > 1e-4f;
};
    return differs(settings.expansion_ratio, defaults.expansion_ratio) || differs(settings.blur_scale, defaults.blur_scale) || differs(settings.falloff_start, defaults.falloff_start) || differs(settings.falloff_exponent, defaults.falloff_exponent) || differs(settings.alpha_multiplier, defaults.alpha_multiplier);
}

LightSource adjust_light_source(const LightSource& source, const Asset& asset, SDL_Point merged_origin) {
    LightSource adjusted = source;
    int offset_x = source.offset_x;
    if (asset.flipped) {
        offset_x = -offset_x;
    }
    adjusted.offset_x = offset_x + (asset.pos.x - merged_origin.x);
    adjusted.offset_y = source.offset_y + (asset.pos.y - merged_origin.y);
    return adjusted;
}

}

TemporaryMergedAssetInfo::TemporaryMergedAssetInfo(std::string name)
    : info_(std::make_shared<AssetInfo>(std::move(name), nlohmann::json::object())) {
    if (!info_) {
        throw std::runtime_error("Failed to allocate AssetInfo for merged asset");
    }
    info_->animations.clear();
    info_->areas.clear();
    info_->tags.clear();
    info_->anti_tags.clear();
    info_->light_sources.clear();
}

void TemporaryMergedAssetInfo::absorb(const Asset& asset, SDL_Point merged_origin) {
    if (!asset.info) {
        return;
    }

    const AssetInfo& src = *asset.info;
    if (!type_) {
        type_ = src.type;
    } else if (*type_ != src.type && !src.type.empty()) {
        *type_ = "merged";
    }

    passable_ = passable_ && src.passable;
    flipable_ = flipable_ && src.flipable;
    smooth_scaling_ = smooth_scaling_ && src.smooth_scaling;
    is_shaded_ = is_shaded_ || src.is_shaded;
    moving_asset_ = moving_asset_ || src.moving_asset;
    is_light_source_ = is_light_source_ || src.is_light_source;
    min_same_type_distance_ = std::min(min_same_type_distance_, src.min_same_type_distance);
    min_distance_all_ = std::min(min_distance_all_, src.min_distance_all);
    neighbor_radius_ = std::max(neighbor_radius_, src.NeighborSearchRadius);
    z_threshold_ = std::max(z_threshold_, src.z_threshold);

    if (src.scale_factor > 0.0f && std::isfinite(src.scale_factor)) {
        scale_factor_sum_ += src.scale_factor;
        ++scale_factor_count_;
    }

    if (!shadow_settings_ && (src.is_shaded || has_custom_shadow_settings(src.shadow_mask_settings))) {
        shadow_settings_ = src.shadow_mask_settings;
    }

    if (!src.custom_controller_key.empty()) {
        custom_controller_ = src.custom_controller_key;
    }

    for (const auto& tag : src.tags) {
        tags_.insert(tag);
    }
    for (const auto& anti : src.anti_tags) {
        anti_tags_.insert(anti);
    }

    for (const auto& light : src.light_sources) {
        light_sources_.push_back(adjust_light_source(light, asset, merged_origin));
    }
}

void TemporaryMergedAssetInfo::set_geometry(int width,
                                            int height,
                                            SDL_Point pivot,
                                            const std::vector<SDL_Point>& local_polygon) {
    info_->original_canvas_width = width;
    info_->original_canvas_height = height;

    AssetInfo::NamedArea area_entry;
    area_entry.name = "merged_bounds";
    area_entry.type = "render";
    area_entry.kind = "render";
    area_entry.area = std::make_unique<Area>(area_entry.name, local_polygon, 2);
    area_entry.area->set_resolution(2);
    area_entry.area->set_type("render");

    AssetInfo::NamedArea::RenderFrame frame;
    frame.width = width;
    frame.height = height;
    frame.pivot_x = pivot.x;
    frame.pivot_y = pivot.y;
    frame.pixel_scale = 1.0f;
    area_entry.render_frame = frame;

    info_->areas.clear();
    info_->areas.emplace_back(std::move(area_entry));
}

void TemporaryMergedAssetInfo::set_animation(const std::string& animation_id, Animation animation) {
    info_->animations.clear();
    info_->animations.emplace(animation_id, std::move(animation));
    info_->start_animation = animation_id;
}

std::shared_ptr<AssetInfo> TemporaryMergedAssetInfo::finalize(const std::vector<float>& variant_steps) {
    info_->passable = passable_;
    info_->flipable = flipable_;
    info_->smooth_scaling = smooth_scaling_;
    info_->is_shaded = is_shaded_;
    info_->moving_asset = moving_asset_;
    info_->is_light_source = is_light_source_;
    info_->min_same_type_distance = (min_same_type_distance_ == std::numeric_limits<int>::max()) ? 0 : min_same_type_distance_;
    info_->min_distance_all = (min_distance_all_ == std::numeric_limits<int>::max()) ? 0 : min_distance_all_;
    info_->NeighborSearchRadius = neighbor_radius_;
    info_->z_threshold = z_threshold_;
    if (shadow_settings_) {
        info_->shadow_mask_settings = *shadow_settings_;
    }
    if (custom_controller_) {
        info_->custom_controller_key = *custom_controller_;
    }

    info_->light_sources = light_sources_;

    info_->tags.assign(tags_.begin(), tags_.end());
    info_->anti_tags.assign(anti_tags_.begin(), anti_tags_.end());
    std::sort(info_->tags.begin(), info_->tags.end());
    info_->tags.erase(std::unique(info_->tags.begin(), info_->tags.end()), info_->tags.end());
    std::sort(info_->anti_tags.begin(), info_->anti_tags.end());
    info_->anti_tags.erase(std::unique(info_->anti_tags.begin(), info_->anti_tags.end()), info_->anti_tags.end());

    info_->scale_variants = variant_steps;
    render_pipeline::ScalingLogic::NormalizeVariantSteps(info_->scale_variants);
    if (scale_factor_count_ > 0) {
        info_->scale_factor = scale_factor_sum_ / static_cast<float>(scale_factor_count_);
    } else {
        info_->scale_factor = 1.0f;
    }

    if (type_) {
        info_->type = *type_;
    }

    return info_;
}

AssetMerger::AssetMerger(SDL_Renderer* renderer, const camera* active_camera)
    : renderer_(renderer), camera_(active_camera) {
    if (!renderer_) {
        throw std::invalid_argument("AssetMerger requires a valid SDL_Renderer");
    }
}

SDL_Texture* AssetMerger::create_scaled_texture(SDL_Texture* source,
                                                int source_w,
                                                int source_h,
                                                float scale,
                                                bool smooth) const {
    if (!source || scale <= 0.0f || !std::isfinite(scale)) {
        return nullptr;
    }

    const int dst_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(source_w) * scale)));
    const int dst_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(source_h) * scale)));

    SDL_Texture* target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, dst_w, dst_h);
    if (!target) {
        return nullptr;
    }

    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(target, smooth ? SDL_ScaleModeBest : SDL_ScaleModeNearest);
#endif

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, target);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    SDL_Rect src_rect{0, 0, source_w, source_h};
    SDL_Rect dst_rect{0, 0, dst_w, dst_h};
    SDL_RenderCopy(renderer_, source, &src_rect, &dst_rect);
    SDL_SetRenderTarget(renderer_, prev);

    return target;
}

std::vector<float> AssetMerger::build_variant_steps(float camera_scale_hint) const {
    std::vector<float> steps(render_pipeline::ScalingLogic::DefaultScaleSteps().begin(), render_pipeline::ScalingLogic::DefaultScaleSteps().end());
    if (std::isfinite(camera_scale_hint) && camera_scale_hint > 0.0f) {
        const auto it = std::find_if(steps.begin(), steps.end(), [camera_scale_hint](float step) {
            return std::fabs(step - camera_scale_hint) <= 1e-3f;
        });
        if (it == steps.end()) {
            steps.push_back(camera_scale_hint);
        }
    }

    steps.erase(std::remove_if(steps.begin(), steps.end(), [](float step) {
                    return !std::isfinite(step) || step <= 0.0f;
                }),
                steps.end());

    if (steps.empty()) {
        steps.push_back(1.0f);
    }

    std::sort(steps.begin(), steps.end(), std::greater<float>());
    steps.erase(std::unique(steps.begin(), steps.end(), [](float a, float b) { return std::fabs(a - b) <= 1e-3f; }), steps.end());
    if (std::fabs(steps.front() - 1.0f) > 1e-3f) {
        steps.insert(steps.begin(), 1.0f);
    }
    return steps;
}

std::vector<AssetMerger::SampledAsset> AssetMerger::sample_assets(const std::vector<std::unique_ptr<Asset>>& assets,
                                                                  double& min_x,
                                                                  double& min_y,
                                                                  double& max_x,
                                                                  double& max_y) const {
    std::vector<SampledAsset> samples;
    min_x = std::numeric_limits<double>::infinity();
    min_y = std::numeric_limits<double>::infinity();
    max_x = -std::numeric_limits<double>::infinity();
    max_y = -std::numeric_limits<double>::infinity();

    for (const auto& asset_ptr : assets) {
        Asset* asset = asset_ptr.get();
        if (!asset || !asset->info) {
            continue;
        }

        const std::string animation_name = !asset->current_animation.empty()
                                               ? asset->current_animation
                                               : (!asset->info->start_animation.empty()
                                                      ? asset->info->start_animation
                                                      : std::string{"default"});

        auto anim_it = asset->info->animations.find(animation_name);
        if (anim_it == asset->info->animations.end()) {
            if (asset->info->animations.empty()) {
                continue;
            }
            anim_it = asset->info->animations.begin();
        }

        Animation& anim = anim_it->second;
        AnimationFrame* frame_ptr = asset->current_frame;
        if (!frame_ptr) {
            frame_ptr = anim.get_first_frame();
        }
        int frame_index = anim.index_of(frame_ptr);
        if (frame_index < 0) {
            frame_index = 0;
        }

        SDL_Texture* frame_texture = anim.get_frame(frame_ptr);
        if (!frame_texture) {
            continue;
        }

        int width = 0;
        int height = 0;
        if (SDL_QueryTexture(frame_texture, nullptr, nullptr, &width, &height) != 0 || width <= 0 || height <= 0) {
            continue;
        }

        SDL_Texture* mask_texture = anim.mask_variant(static_cast<std::size_t>(frame_index), 0);

        auto render_frame = select_render_frame(*asset->info);
        float pivot_ratio_x = 0.5f;
        float pivot_ratio_y = 1.0f;
        if (render_frame && render_frame->width > 0 && render_frame->height > 0) {
            pivot_ratio_x = static_cast<float>(render_frame->pivot_x) / static_cast<float>(render_frame->width);
            pivot_ratio_y = static_cast<float>(render_frame->pivot_y) / static_cast<float>(render_frame->height);
        }

        const double pivot_x = pivot_ratio_x * static_cast<double>(width);
        const double pivot_y = pivot_ratio_y * static_cast<double>(height);

        const double left = static_cast<double>(asset->pos.x) - pivot_x;
        const double top = static_cast<double>(asset->pos.y) - pivot_y;
        const double right = left + static_cast<double>(width);
        const double bottom = top + static_cast<double>(height);

        min_x = std::min(min_x, left);
        min_y = std::min(min_y, top);
        max_x = std::max(max_x, right);
        max_y = std::max(max_y, bottom);

        SampledAsset sample;
        sample.asset = asset;
        sample.frame_texture = frame_texture;
        sample.mask_texture = mask_texture;
        sample.width = width;
        sample.height = height;
        sample.left = left;
        sample.top = top;
        sample.pivot_x = pivot_x;
        sample.pivot_y = pivot_y;
        sample.pivot_ratio_x = pivot_ratio_x;
        sample.pivot_ratio_y = pivot_ratio_y;
        samples.push_back(sample);
    }

    return samples;
}

std::unique_ptr<Asset> AssetMerger::merge(std::vector<std::unique_ptr<Asset>> assets) {
    if (assets.empty()) {
        return nullptr;
    }

    if (assets.size() == 1) {
        return std::move(assets.front());
    }

    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;
    auto samples = sample_assets(assets, min_x, min_y, max_x, max_y);
    if (samples.size() < 2) {
        return std::move(assets.front());
    }

    SDL_Point merged_origin{
        static_cast<int>(std::lround((min_x + max_x) * 0.5)), static_cast<int>(std::lround(max_y)) };

    const int base_width = std::max(1, static_cast<int>(std::ceil(max_x - min_x)));
    const int base_height = std::max(1, static_cast<int>(std::ceil(max_y - min_y)));
    SDL_Point pivot{ base_width / 2, base_height };

    std::vector<SDL_Point> local_polygon{
        SDL_Point{ static_cast<int>(std::lround(min_x - merged_origin.x + pivot.x)), static_cast<int>(std::lround(min_y - merged_origin.y + pivot.y)) },
        SDL_Point{ static_cast<int>(std::lround(max_x - merged_origin.x + pivot.x)), static_cast<int>(std::lround(min_y - merged_origin.y + pivot.y)) },
        SDL_Point{ static_cast<int>(std::lround(max_x - merged_origin.x + pivot.x)), static_cast<int>(std::lround(max_y - merged_origin.y + pivot.y)) },
        SDL_Point{ static_cast<int>(std::lround(min_x - merged_origin.x + pivot.x)), static_cast<int>(std::lround(max_y - merged_origin.y + pivot.y)) }
};

    SDL_Texture* composite = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, base_width, base_height);
    if (!composite) {
        throw std::runtime_error("Failed to create merged texture");
    }
    SDL_SetTextureBlendMode(composite, SDL_BLENDMODE_BLEND);

    bool any_mask = std::any_of(samples.begin(), samples.end(), [](const SampledAsset& sample) {
        return sample.mask_texture != nullptr;
    });
    SDL_Texture* composite_mask = nullptr;
    if (any_mask) {
        composite_mask = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, base_width, base_height);
        if (!composite_mask) {
            SDL_DestroyTexture(composite);
            throw std::runtime_error("Failed to create merged mask texture");
        }
        SDL_SetTextureBlendMode(composite_mask, SDL_BLENDMODE_BLEND);
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, composite);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    if (composite_mask) {
        SDL_SetRenderTarget(renderer_, composite_mask);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
        SDL_RenderClear(renderer_);
    }

    for (const auto& sample : samples) {
        SDL_Rect src{0, 0, sample.width, sample.height};
        SDL_Rect dst{
            static_cast<int>(std::lround(sample.left - min_x)), static_cast<int>(std::lround(sample.top - min_y)), sample.width, sample.height };

        SDL_SetRenderTarget(renderer_, composite);
        SDL_RenderCopyEx(renderer_, sample.frame_texture, &src, &dst, 0.0, nullptr, sample.asset->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

        if (composite_mask && sample.mask_texture) {
            SDL_SetRenderTarget(renderer_, composite_mask);
            SDL_RenderCopyEx(renderer_, sample.mask_texture, &src, &dst, 0.0, nullptr, sample.asset->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }

    SDL_SetRenderTarget(renderer_, previous_target);

    const float camera_scale = camera_ ? static_cast<float>(sanitize_scale(camera_->get_scale())) : 1.0f;
    const std::vector<float> variant_steps = build_variant_steps(camera_scale);
    const std::size_t variant_count = variant_steps.size();

    std::string merged_name = "runtime_merged";
    if (samples.front().asset && samples.front().asset->info && !samples.front().asset->info->name.empty()) {
        merged_name = samples.front().asset->info->name + "_merged";
    }

    SDL_Surface* base_surface = texture_to_surface(renderer_, composite, base_width, base_height);
    SDL_Surface* base_mask_surface = composite_mask ? texture_to_surface(renderer_, composite_mask, base_width, base_height) : nullptr;
    SDL_DestroyTexture(composite);
    composite = nullptr;
    if (composite_mask) {
        SDL_DestroyTexture(composite_mask);
        composite_mask = nullptr;
    }

    if (!base_surface) {
        if (base_mask_surface) {
            SDL_FreeSurface(base_mask_surface);
        }
        throw std::runtime_error("Failed to convert merged texture into CPU surface");
    }
    if (any_mask && !base_mask_surface) {
        SDL_FreeSurface(base_surface);
        throw std::runtime_error("Failed to convert merged mask texture into CPU surface");
    }

    std::vector<std::vector<SDL_Surface*>> variant_surfaces(variant_count);
    std::vector<std::vector<SDL_Surface*>> mask_surfaces(variant_count);
    // Foreground/background overlay surfaces are not generated at runtime.
    std::vector<std::vector<SDL_Surface*>> foreground_surfaces(variant_count);
    std::vector<std::vector<SDL_Surface*>> background_surfaces(variant_count);

    auto cleanup_surfaces = [&]() {
        free_surface_lists(variant_surfaces);
        free_surface_lists(mask_surfaces);
        free_surface_lists(foreground_surfaces);
        free_surface_lists(background_surfaces);
    };

    variant_surfaces[0].push_back(base_surface);
    if (base_mask_surface) {
        mask_surfaces[0].push_back(base_mask_surface);
    }

    for (std::size_t idx = 1; idx < variant_count; ++idx) {
        SDL_Surface* scaled = render_pipeline::CreateScaledSurface(base_surface, variant_steps[idx]);
        if (!scaled) {
            cleanup_surfaces();
            throw std::runtime_error("Failed to scale merged surface variant");
        }
        variant_surfaces[idx].push_back(scaled);

        if (base_mask_surface) {
            SDL_Surface* scaled_mask = render_pipeline::CreateScaledSurface(base_mask_surface, variant_steps[idx]);
            if (!scaled_mask) {
                cleanup_surfaces();
                throw std::runtime_error("Failed to scale merged mask surface");
            }
            mask_surfaces[idx].push_back(scaled_mask);
        }
    }

    // Do not generate foreground/background overlay stacks at runtime.

    vibble::log::debug("[AssetMerger] Skipping disk cache writes for merged asset; Python handles cache generation.");

    const std::string animation_id = "merged_static";

    std::vector<Animation::FrameCache> caches(1);
    caches[0].resize(variant_count);
    std::vector<SDL_Texture*> frames;
    frames.reserve(1);
    std::vector<SDL_Texture*> masks;
    masks.reserve(1);

    auto surface_to_texture_checked = [&](SDL_Surface* surface, const char* context, bool required) -> SDL_Texture* {
        if (!surface) {
            if (required) {
                cleanup_surfaces();
                throw std::runtime_error(std::string("Missing surface for ") + context);
            }
            return nullptr;
        }
        SDL_Texture* tex = CacheManager::surface_to_texture(renderer_, surface);
        if (!tex) {
            cleanup_surfaces();
            throw std::runtime_error(std::string("Failed to convert ") + context + " surface to texture");
        }
        return tex;
    };

    for (std::size_t idx = 0; idx < variant_count; ++idx) {
        SDL_Surface* sprite_surface = variant_surfaces[idx].empty() ? nullptr : variant_surfaces[idx][0];
        SDL_Texture* frame_texture = surface_to_texture_checked(sprite_surface, "sprite", true);
        caches[0].textures[idx] = frame_texture;
        caches[0].widths[idx] = sprite_surface->w;
        caches[0].heights[idx] = sprite_surface->h;
        if (idx == 0) {
            frames.push_back(frame_texture);
        }

        SDL_Surface* mask_surface = mask_surfaces[idx].empty() ? nullptr : mask_surfaces[idx][0];
        SDL_Texture* mask_texture = surface_to_texture_checked(mask_surface, "mask", false);
        if (mask_surface) {
            caches[0].mask_widths[idx] = mask_surface->w;
            caches[0].mask_heights[idx] = mask_surface->h;
        } else {
            caches[0].mask_widths[idx] = 0;
            caches[0].mask_heights[idx] = 0;
        }
        caches[0].mask_textures[idx] = mask_texture;
        if (idx == 0) {
            masks.push_back(mask_texture);
        }

        caches[0].foreground_textures[idx] = nullptr;
        caches[0].background_textures[idx] = nullptr;
    }

    cleanup_surfaces();

    Animation animation;
    animation.adopt_prebuilt_frames(std::move(caches), std::move(frames), std::move(masks), variant_steps);
    animation.loop = true;
    animation.locked = true;
    animation.speed_factor = 1.0f;
    animation.number_of_frames = 1;
    auto& path = animation.movement_path(0);
    if (path.empty()) {
        path.emplace_back();
    }
    path[0].frame_index = 0;
    path[0].is_first = true;
    path[0].is_last = true;
    path[0].next = nullptr;
    path[0].prev = nullptr;
    path[0].base_texture = animation.frame_variant(0, 0);
    path[0].foreground_texture = animation.depthcue_foreground_variant(0, 0);
    path[0].background_texture = animation.depthcue_background_variant(0, 0);

    TemporaryMergedAssetInfo info_builder(merged_name);
    info_builder.set_geometry(base_width, base_height, pivot, local_polygon);
    info_builder.set_animation(animation_id, std::move(animation));

    for (const auto& sample : samples) {
        info_builder.absorb(*sample.asset, merged_origin);
    }

    std::shared_ptr<AssetInfo> merged_info = info_builder.finalize(variant_steps);

    Area spawn_area = merged_info->areas.empty() || !merged_info->areas.front().area ? Area("merged_spawn", 0) : Area(*merged_info->areas.front().area);

    Asset* exemplar = samples.front().asset;
    const int depth = exemplar ? exemplar->depth : 0;
    const std::string spawn_id = exemplar ? exemplar->spawn_id : std::string{};
    const std::string spawn_method = exemplar ? exemplar->spawn_method : std::string{};
    const std::string owning_room = exemplar ? exemplar->owning_room_name() : std::string{};
    const bool hidden = exemplar ? exemplar->is_hidden() : false;

    auto merged_asset = std::make_unique<Asset>(merged_info, spawn_area, merged_origin, depth, nullptr, spawn_id, spawn_method);
    merged_asset->set_owning_room_name(owning_room);
    merged_asset->set_hidden(hidden);
    merged_asset->NeighborSearchRadius = merged_info->NeighborSearchRadius;

    merged_asset->on_scale_factor_changed();

    assets.clear();
    return merged_asset;
}

}
