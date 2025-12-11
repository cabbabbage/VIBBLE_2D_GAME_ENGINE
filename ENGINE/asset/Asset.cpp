#include "Asset.hpp"
#include "controller_factory.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp"
#include "core/asset_list.hpp"
#include "render/warped_screen_grid.hpp"
#include "render/render.hpp"
#include "animation_update/animation_runtime.hpp"
#include "animation_update/child_attachment_controller.hpp"
#include "animation_update/animation_update.hpp"
#include "utils/area_helpers.hpp"
#include "asset/asset_types.hpp"
#include "utils/grid.hpp"
#include "utils/transform_smoothing_settings.hpp"
#include "utils/log.hpp"
#include <iostream>
#include <random>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <SDL.h>
static std::mt19937& asset_rng()
{
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
}

static std::mutex& asset_rng_mutex()
{
        static std::mutex mutex;
        return mutex;
}

static std::vector<std::string> collect_animation_child_names(const AssetInfo& info) {
        std::vector<std::string> names;
        std::unordered_set<std::string> seen;
        auto add = [&](const std::string& name) {
                if (name.empty()) {
                        return;
                }
                if (seen.insert(name).second) {
                        names.push_back(name);
                }
};

        for (const auto& name : info.animation_children) {
                add(name);
        }

        for (const auto& entry : info.animations) {
                for (const auto& child_name : entry.second.child_assets()) {
                        add(child_name);
                }
        }

        return names;
}

std::unordered_map<std::string, std::pair<bool,bool>> Asset::s_flip_overrides_{};
std::mutex Asset::s_flip_overrides_mutex_{};

Asset::Asset(std::shared_ptr<AssetInfo> info_,
             const Area& spawn_area,
             SDL_Point start_pos,
             int depth_,
             Asset* parent_,
             const std::string& spawn_id_,
             const std::string& spawn_method_,
             int grid_resolution_)
: parent(parent_)
, info(std::move(info_))
, current_animation()
, static_frame(false)
, active(false)
, pos(start_pos)
, grid_resolution(vibble::grid::clamp_resolution(grid_resolution_))
, z_index(0)
, z_offset(0)
, depth(depth_)
, spawn_id(spawn_id_)
, spawn_method(spawn_method_)
, owning_room_name_(spawn_area.get_name())
{
	set_flip();
	set_z_index();

        try {
                if (info && asset_types::canonicalize(info->type) == asset_types::player) {
                        grid_resolution = 0;
                }
        } catch (...) {

        }
        if (info) {
                try {
                        is_shaded = info->is_shaded;
                } catch (...) {
                        is_shaded = false;
                }
        }
        std::string start_id = info->start_animation.empty() ? std::string{"default"} : info->start_animation;
        auto it = info->animations.find(start_id);
        if (it == info->animations.end()) {
                it = info->animations.find("default");
        }
        if (it != info->animations.end() && !it->second.frames.empty()) {
                current_animation = it->first;
                Animation& anim  = it->second;
                static_frame     = (anim.frames.size() == 1);
                current_frame    = anim.get_first_frame();
                if ((anim.randomize || anim.rnd_start) && anim.frames.size() > 1) {
                        std::uniform_int_distribution<int> d(0, int(anim.frames.size()) - 1);
                        int idx;
                        {
                                std::lock_guard<std::mutex> lock(asset_rng_mutex());
                                idx = d(asset_rng());
                        }
                        AnimationFrame* f = anim.get_first_frame();
                        while (idx-- > 0 && f && f->next) { f = f->next; }
                        current_frame = f;
                }
        }

        const bool is_player_asset = info && asset_types::canonicalize(info->type) == asset_types::player;
        TransformSmoothingParams translation_params = transform_smoothing::asset_translation_params();
        if (is_player_asset) {
                translation_params.method = TransformSmoothingMethod::Lerp;
                translation_params.lerp_rate = 100.0f;
                translation_params.max_step = 0.0f;
                translation_params.snap_threshold = 0.0f;
        }
        translation_smoothing_x_.set_params(translation_params);
        translation_smoothing_y_.set_params(translation_params);
        scale_smoothing_.set_params(transform_smoothing::asset_scale_params());
        alpha_smoothing_.set_params(transform_smoothing::asset_alpha_params());

        translation_smoothing_x_.reset(static_cast<float>(pos.x));
        translation_smoothing_y_.reset(static_cast<float>(pos.y));
        const float initial_scale = (info && std::isfinite(info->scale_factor) && info->scale_factor > 0.0f) ? info->scale_factor : 1.0f;
        scale_smoothing_.reset(initial_scale);
        alpha_smoothing_.reset(hidden ? 0.0f : 1.0f);

}

void Asset::clear_downscale_cache() {
    if (last_scaled_texture_) {
        SDL_DestroyTexture(last_scaled_texture_);
        last_scaled_texture_ = nullptr;
    }
    last_scaled_source_ = nullptr;
    last_scaled_w_ = 0;
    last_scaled_h_ = 0;
    last_scaled_camera_scale_ = -1.0f;
    downscale_cache_ready_revision_ = 0;
}

Asset::~Asset() {
        if (parent) {
                auto& vec = parent->asset_children;
                vec.erase(std::remove(vec.begin(), vec.end(), this), vec.end());
                parent = nullptr;
        }
        for (Asset* asset_child : asset_children) {
                if (asset_child && asset_child->parent == this) asset_child->parent = nullptr;
        }

        clear_render_caches();
        if (composite_texture_) {
                SDL_DestroyTexture(composite_texture_);
                composite_texture_ = nullptr;
        }
        visibility_stamp = 0;
}

Asset::Asset(const Asset& o)
: parent(o.parent)
, info(o.info)
, current_animation(o.current_animation)
, pos(o.pos)
, grid_resolution(vibble::grid::clamp_resolution(o.grid_resolution))
, z_index(o.z_index)
, z_offset(o.z_offset)
, active(o.active)
, flipped(o.flipped)
, distance_from_camera(o.distance_from_camera)
 , angle_from_camera(o.angle_from_camera)
, asset_children(o.asset_children)
, scene_mask_lights(o.scene_mask_lights)
, depth(o.depth)
, is_shaded(o.is_shaded)
, dead(o.dead)
, static_frame(o.static_frame)
, needs_target(o.needs_target)
, cached_w(o.cached_w)
, cached_h(o.cached_h)
, window(o.window)
, highlighted(o.highlighted)
, hidden(o.hidden)
, selected(o.selected)
, merged_from_neighbors_(o.merged_from_neighbors_)
, current_frame(o.current_frame)
, frame_progress(o.frame_progress)
, shading_group(o.shading_group)
, shading_group_set(o.shading_group_set)
, assets_(o.assets_)
, spawn_id(o.spawn_id)
, spawn_method(o.spawn_method)
, owning_room_name_(o.owning_room_name_)
, controller_(nullptr)
, tiling_info_(o.tiling_info_)
, anim_(nullptr)
, last_scaled_texture_(nullptr)
, last_scaled_source_(nullptr)
, last_scaled_w_(0)
, last_scaled_h_(0)
, last_scaled_camera_scale_(-1.0f)
, last_scale_usage_()
, last_rendered_frame_(nullptr)
, scale_variant_state_(o.scale_variant_state_)
, base_bounds_local_(o.base_bounds_local_)
, grid_id_(o.grid_id_)
, composite_texture_(nullptr)
, composite_dirty_(true)
, composite_rect_({0, 0, 0, 0})
, animation_children_initialized_(o.animation_children_initialized_)
, initializing_animation_children_(false)
{

        clear_render_caches();
        last_scale_usage_ = o.last_scale_usage_;
        scale_variant_state_ = o.scale_variant_state_;
        cached_grid_residency_    = o.cached_grid_residency_;
        has_cached_grid_residency_ = o.has_cached_grid_residency_;
        translation_smoothing_x_  = o.translation_smoothing_x_;
        translation_smoothing_y_  = o.translation_smoothing_y_;
        scale_smoothing_          = o.scale_smoothing_;
        alpha_smoothing_          = o.alpha_smoothing_;
        animation_children_       = o.animation_children_;
        finalized_                = o.finalized_;
        for (auto& slot : animation_children_) {
                slot.timeline = nullptr;
                slot.timeline_active = false;
                slot.timeline_frame_cursor = 0;
                slot.timeline_frame_progress = 0.0f;
        }
}

Asset& Asset::operator=(const Asset& o) {
        if (this == &o) return *this;

        clear_render_caches();
        parent               = o.parent;
        info                 = o.info;
        current_animation    = o.current_animation;
    pos                  = o.pos;
    grid_resolution      = vibble::grid::clamp_resolution(o.grid_resolution);
	z_index              = o.z_index;
	z_offset             = o.z_offset;
	active               = o.active;
        flipped              = o.flipped;
        distance_from_camera = o.distance_from_camera;
        angle_from_camera = o.angle_from_camera;
        asset_children       = o.asset_children;
        scene_mask_lights    = o.scene_mask_lights;
	depth                = o.depth;
        is_shaded            = o.is_shaded;
	dead                 = o.dead;
	static_frame         = o.static_frame;
        needs_target        = o.needs_target;
	cached_w             = o.cached_w;
	cached_h             = o.cached_h;
	window               = o.window;
        highlighted          = o.highlighted;
        hidden               = o.hidden;
        selected             = o.selected;
        merged_from_neighbors_ = o.merged_from_neighbors_;
        current_frame        = o.current_frame;
        frame_progress       = o.frame_progress;
	shading_group        = o.shading_group;
	shading_group_set    = o.shading_group_set;
        last_rendered_frame_   = nullptr;
        assets_              = o.assets_;
        spawn_id             = o.spawn_id;
        spawn_method         = o.spawn_method;
        owning_room_name_    = o.owning_room_name_;
        controller_.reset();
        anim_.reset();
        tiling_info_         = o.tiling_info_;
        last_scaled_texture_      = nullptr;
        last_scaled_source_       = nullptr;
        last_scaled_w_            = 0;
        last_scaled_h_            = 0;
        last_scaled_camera_scale_ = -1.0f;
        last_scale_usage_         = o.last_scale_usage_;
        scale_variant_state_      = o.scale_variant_state_;
        cached_grid_residency_    = o.cached_grid_residency_;
        has_cached_grid_residency_ = o.has_cached_grid_residency_;
        translation_smoothing_x_  = o.translation_smoothing_x_;
        translation_smoothing_y_  = o.translation_smoothing_y_;
        scale_smoothing_          = o.scale_smoothing_;
        alpha_smoothing_          = o.alpha_smoothing_;
        animation_children_       = o.animation_children_;
        finalized_                = o.finalized_;
        base_bounds_local_        = o.base_bounds_local_;
        grid_id_                  = o.grid_id_;
        animation_children_initialized_ = o.animation_children_initialized_;
        initializing_animation_children_ = false;
        for (auto& slot : animation_children_) {
                slot.timeline = nullptr;
                slot.timeline_active = false;
                slot.timeline_frame_cursor = 0;
                slot.timeline_frame_progress = 0.0f;
        }
        return *this;
}

void Asset::finalize_setup() {
        if (finalized_) {
                return;
        }
        if (!info) return;
        if (current_animation.empty() ||
        info->animations[current_animation].frames.empty())
        {
		std::string start_id = info->start_animation.empty() ? std::string{"default"} : info->start_animation;
		auto it = info->animations.find(start_id);
		if (it == info->animations.end()) it = info->animations.find("default");
		if (it == info->animations.end()) it = info->animations.begin();
		if (it != info->animations.end() && !it->second.frames.empty()) {
			current_animation = it->first;
			Animation& anim = it->second;
                        anim.change(current_frame, static_frame);
                        frame_progress = 0.0f;
                        if ((anim.randomize || anim.rnd_start) && anim.frames.size() > 1) {
                                std::uniform_int_distribution<int> dist(0, int(anim.frames.size()) - 1);
                                int idx;
                                {
                                        std::lock_guard<std::mutex> lock(asset_rng_mutex());
                                        idx = dist(asset_rng());
                                }
                                AnimationFrame* f = anim.get_first_frame();
                                while (idx-- > 0 && f && f->next) { f = f->next; }
                                current_frame = f;
                        }
                }
	}
        for (Asset* asset_child : asset_children)
        if (asset_child) asset_child->finalize_setup();
#ifdef VIBBLE_DEBUG_ASSET_LOGS
        if (!asset_children.empty()) {
                std::cout << "[Asset] \"" << (info ? info->name : std::string{"<null>"})
                << "\" at (" << pos.x << ", " << pos.y
                << ") has " << asset_children.size() << " child(ren):\n";
                for (Asset* asset_child : asset_children)
                if (asset_child && asset_child->info)
                std::cout << "    - \"" << asset_child->info->name
                << "\" at (" << asset_child->pos.x << ", " << asset_child->pos.y << ")\n";
        }
#endif
        ensure_animation_runtime(false);
        if (!animation_children_initialized_) {
                initialize_animation_children_recursive();
        }
        if (assets_ && !controller_) {
                ControllerFactory cf(assets_);
                controller_ = cf.create_for_asset(this);
        }
        NeighborSearchRadius = info->NeighborSearchRadius;
        refresh_cached_dimensions();
        finalized_ = true;
}

void Asset::update_scale_values() {
    const float base_scale =
        (info && std::isfinite(info->scale_factor) && info->scale_factor > 0.0f) ? info->scale_factor : 1.0f;

    float perspective_scale = 1.0f;
    if (window) {
        if (auto* gp = window->grid_point_for_asset(this)) {
            perspective_scale = std::max(0.0001f, gp->perspective_scale);
        }
    }

    current_scale = base_scale * perspective_scale;

    float camera_scale = 1.0f;
    if (assets_) {
        camera_scale = std::max(0.0001f, assets_->getView().get_scale());
    } else if (window) {
        camera_scale = std::max(0.0001f, window->get_scale());
    }

    float desired_variant_scale = current_scale / camera_scale;
    if (!std::isfinite(desired_variant_scale) || desired_variant_scale <= 0.0f) {
        desired_variant_scale = current_scale;
    }

    const auto& steps = (info && !info->scale_variants.empty()) ? static_cast<const std::vector<float>&>(info->scale_variants) : render_pipeline::ScalingLogic::DefaultScaleSteps();

    auto selection = render_pipeline::ScalingLogic::Choose(desired_variant_scale, steps);
    current_nearest_variant_scale = selection.stored_scale;
    current_variant_index = selection.index;

    if (current_nearest_variant_scale > 0.0f) {
        current_remaining_scale_adjustment = current_scale / current_nearest_variant_scale;
    } else {
        current_remaining_scale_adjustment = 1.0f;
    }
}

SDL_Texture* Asset::get_current_variant_texture() const {
    if (!current_frame) return nullptr;
    return current_frame->get_base_texture(current_variant_index);
}

SDL_Texture* Asset::get_texture()
{
	if (current_frame) {
		return current_frame->get_base_texture(current_variant_index);
	}
	return nullptr;
}

SDL_Texture* Asset::get_current_frame() const
{
	if (current_frame) {
		return current_frame->get_base_texture(current_variant_index);
	}
	return nullptr;
}

void Asset::set_current_animation(const std::string& name)
{
	if (info == nullptr) {
		return;
	}

	auto it = info->animations.find(name);
	if (it != info->animations.end()) {
		current_animation = name;
		Animation& anim = it->second;
		anim.change(current_frame, static_frame);
		frame_progress = 0.0f;
	}
}

void Asset::update() {
    if (!info) return;

    update_scale_values();

    SDL_Point previous_pos = pos;

    if (controller_ && assets_) {
        if (Input* in = assets_->get_input()) {
            controller_->update(*in);
        }
    }

    if (anim_) {
        auto iti = info->animations.find(current_animation);
        if (iti == info->animations.end()) {

            auto def = info->animations.find("default");
            if (def == info->animations.end()) def = info->animations.begin();
            if (def != info->animations.end()) {
                if (anim_) {
                    anim_->move(SDL_Point{ 0, 0 }, def->first);
                } else {
                    current_animation = def->first;
                    Animation& anim   = def->second;
                    current_frame     = anim.get_first_frame();
                    frame_progress    = 0.0f;
                    if (info && info->type == asset_types::player) {
                        static_frame = false;
                    } else {
                        static_frame = anim.is_frozen() || anim.locked;
                    }
                }
            }
        } else {
            Animation& anim = iti->second;
            if (anim.index_of(current_frame) < 0) {
                std::size_t path_index = anim_ ? anim_->path_index_for(current_animation) : 0;
                current_frame = anim.get_first_frame(path_index);
                frame_progress = 0.0f;
                if (info && info->type == asset_types::player) {
                    static_frame = false;
                } else {
                    static_frame = anim.is_frozen() || anim.locked;
                }
            }
        }
    }

    if (!dead && anim_runtime_) {
        anim_runtime_->update();
    }

    if (info->moving_asset) {
        const bool moved = (pos.x != previous_pos.x || pos.y != previous_pos.y);
        if (moved) {
            update_neighbor_lists(true);
            if (assets_) {
                assets_->notify_light_map_asset_moved(this);
            }
        }
    }

    translation_smoothing_x_.reset(static_cast<float>(pos.x));
    translation_smoothing_y_.reset(static_cast<float>(pos.y));

    float scale_target = 1.0f;
    if (info && std::isfinite(info->scale_factor) && info->scale_factor > 0.0f) {
        scale_target = info->scale_factor;
    }
    scale_smoothing_.reset(scale_target);

    const float alpha_target = hidden ? 0.0f : 1.0f;
    alpha_smoothing_.reset(alpha_target);
}

std::string Asset::get_current_animation() const { return current_animation; }

bool Asset::is_current_animation_locked_in_progress() const {
        if (!info || !current_frame) return false;
        if (info->type == asset_types::player) return false;
        auto it = info->animations.find(current_animation);
        if (it == info->animations.end()) return false;
        const Animation& anim = it->second;
        if (!anim.locked) return false;
        return !current_frame->is_last;
}

bool Asset::is_current_animation_last_frame() const {
        if (!current_frame) return false;
        return current_frame->is_last;
}

bool Asset::is_current_animation_looping() const {
	if (!info) return false;
	auto it = info->animations.find(current_animation);
	if (it == info->animations.end()) return false;
	const Animation& anim = it->second;
	return anim.loop;
}

void Asset::add_child(Asset* asset_child) {
        if (!asset_child || !asset_child->info) return;
        if (info) {

        }
        asset_child->parent = this;
        if (!asset_child->get_assets()) asset_child->set_assets(this->assets_);
        asset_child->set_z_index();
        asset_children.push_back(asset_child);
}

void Asset::set_assets(Assets* a) {
    assets_ = a;
    if (assets_) {
        assets_->track_asset_for_grid(this);
    }
    ensure_animation_runtime(false);
    if (!controller_ && assets_) {
            ControllerFactory cf(assets_);
            controller_ = cf.create_for_asset(this);
    }
    neighbors.reset();
    impassable_naighbors = nullptr;
    neighbor_lists_initialized_ = false;
    last_neighbor_origin_ = SDL_Point{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };

}

void Asset::set_tiling_info(std::optional<TilingInfo> info) {
    tiling_info_ = std::move(info);
}

void Asset::set_owning_room_name(std::string name) {
    owning_room_name_ = std::move(name);
}

void Asset::rebuild_animation_runtime() {
    ensure_animation_runtime(true);
}

void Asset::initialize_animation_children_recursive() {
        if (initializing_animation_children_) {
                return;
        }

        if (!info || !assets_) {
                initializing_animation_children_ = false;
                animation_children_initialized_ = false;
                return;
        }

        const auto child_names = collect_animation_child_names(*info);

        bool needs_refresh = !animation_children_initialized_;
        if (!needs_refresh) {
                std::vector<std::string> current_names;
                current_names.reserve(animation_children_.size());
                for (const auto& slot : animation_children_) {
                        if (slot.child_index >= 0 && !slot.asset_name.empty()) {
                                current_names.push_back(slot.asset_name);
                        }
                }
                if (current_names.size() != child_names.size()) {
                        needs_refresh = true;
                } else {
                        for (std::size_t i = 0; i < child_names.size(); ++i) {
                                if (child_names[i] != current_names[i]) {
                                        needs_refresh = true;
                                        break;
                                }
                        }
                }
        }

        if (!needs_refresh) {
                return;
        }

        initializing_animation_children_ = true;

        try {
                animation_children_initialized_ = false;

        if (child_names.empty()) {
                animation_children_initialized_ = true;
                initializing_animation_children_ = false;
                return;
        }

        std::unordered_map<std::string, std::size_t> existing;
        existing.reserve(animation_children_.size());
        for (std::size_t i = 0; i < animation_children_.size(); ++i) {
                const auto& name = animation_children_[i].asset_name;
                if (!name.empty() && existing.find(name) == existing.end()) {
                        existing[name] = i;
                }
        }

        for (const auto& name : child_names) {
                if (existing.find(name) != existing.end()) {
                        continue;
                }
                animation_children_.emplace_back();
                auto& slot = animation_children_.back();
                slot.child_index = -1;
                slot.asset_name = name;
                slot.visible = false;
                slot.was_visible = false;
                slot.last_parent_frame_index = -1;
                existing[name] = animation_children_.size() - 1;
        }

        for (std::size_t i = 0; i < child_names.size(); ++i) {
                const std::string& desired = child_names[i];
                auto it = existing.find(desired);
                if (it == existing.end()) {
                        continue;
                }
                std::size_t current = it->second;
                if (current != i) {
                        std::swap(animation_children_[i], animation_children_[current]);
                        existing[animation_children_[current].asset_name] = current;
                        existing[desired] = i;
                }
        }

        AssetLibrary* library = assets_ ? &assets_->library() : nullptr;
        auto bind_child_animation = [&](Asset::AnimationChildAttachment& slot) {
                if (slot.animation || !slot.info) return;
                auto child_anim_it =
                        slot.info->animations.find(animation_update::detail::kDefaultAnimation);
                if (child_anim_it == slot.info->animations.end() && !slot.info->animations.empty()) {
                        child_anim_it = slot.info->animations.begin();
                }
                if (child_anim_it != slot.info->animations.end()) {
                        slot.animation = &child_anim_it->second;
                        slot.current_frame = nullptr;
                        slot.frame_progress = 0.0f;
                        slot.last_parent_frame_index = -1;
                }
};

        for (std::size_t i = 0; i < child_names.size(); ++i) {
                auto& slot = animation_children_[i];
                slot.child_index = static_cast<int>(i);
                slot.visible = false;
                slot.was_visible = false;
                slot.render_in_front = true;
                slot.last_parent_frame_index = -1;
                if (!slot.info && library && !slot.asset_name.empty()) {
                        slot.info = library->get(slot.asset_name);
                }
                bind_child_animation(slot);
                if (slot.animation && !slot.current_frame) {
                        animation_update::child_attachments::restart(slot);
                }
                if (!slot.spawned_asset && slot.info) {
                        SDL_Point spawn_pos{
                                static_cast<int>(std::lround(smoothed_translation_x())), static_cast<int>(std::lround(smoothed_translation_y())) };
                        Asset* child = nullptr;
                        try {
                                child = assets_->spawn_asset(slot.asset_name, spawn_pos);
                        } catch (...) {
                                child = nullptr;
                        }
                        if (child) {
                                child->parent = this;
                                child->depth = depth;
                                child->grid_resolution = grid_resolution;
                                child->set_z_offset(z_offset);
                                child->set_z_index();
                                child->set_hidden(true);
                                if (std::find(asset_children.begin(), asset_children.end(), child) ==
                                    asset_children.end()) {
                                        add_child(child);
                                }
                                slot.spawned_asset = child;
                                try { child->initialize_animation_children_recursive(); } catch (...) {}
                        }
                }
        }

        for (std::size_t i = child_names.size(); i < animation_children_.size(); ++i) {
                auto& slot = animation_children_[i];
                slot.child_index = -1;
                slot.visible = false;
                slot.was_visible = false;
                slot.last_parent_frame_index = -1;
                slot.timeline = nullptr;
                slot.timeline_active = false;
                slot.timeline_frame_cursor = 0;
                slot.timeline_frame_progress = 0.0f;
                if (slot.spawned_asset) {
                        slot.spawned_asset->set_hidden(true);
                }
        }

        if (animation_children_.size() > child_names.size()) {
                animation_children_.resize(child_names.size());
        }

        animation_children_initialized_ = true;
        initializing_animation_children_ = false;
        } catch (...) {

                animation_children_initialized_ = false;
                initializing_animation_children_ = false;
        }
}

void Asset::ensure_animation_runtime(bool force_recreate) {
    if (!assets_) {
        return;
    }
    if (!force_recreate && anim_ && anim_runtime_) {
        return;
    }
    anim_runtime_.reset();
    anim_.reset();
    if (force_recreate) {
        animation_children_initialized_ = false;
        initializing_animation_children_ = false;
    }
    anim_runtime_ = std::make_unique<AnimationRuntime>(this, assets_);
    anim_ = std::make_unique<AnimationUpdate>(this, assets_);
    if (anim_runtime_) anim_runtime_->set_planner(anim_.get());
    if (anim_) anim_->set_runtime(anim_runtime_.get());
}

AssetList* Asset::get_neighbors_list() { return neighbors.get(); }
const AssetList* Asset::get_neighbors_list() const { return neighbors.get(); }
AssetList* Asset::get_impassable_naighbors() { return impassable_naighbors; }
const AssetList* Asset::get_impassable_naighbors() const { return impassable_naighbors; }

void Asset::update_neighbor_lists(bool force_update) {
    if (!assets_ || !info || !info->moving_asset) {
        return;
    }

    auto base_filter = [this](const Asset* candidate) {
        if (!candidate || candidate == this || !candidate->info) {
            return false;
        }
        if (candidate->info->type == asset_types::texture) {
            return false;
        }
        return true;
};

    auto impassable_filter = [this](const Asset* candidate) {
        if (!candidate || candidate == this || !candidate->info) {
            return false;
        }
        if (candidate->info->type == asset_types::texture) {
            return false;
        }
        const std::string canonical_type = asset_types::canonicalize(candidate->info->type);
        if (canonical_type == asset_types::player) {
            return false;
        }
        if (canonical_type == asset_types::boundary) {
            return true;
        }
        if (canonical_type == asset_types::enemy || canonical_type == asset_types::npc) {
            return true;
        }
        if (candidate->info->moving_asset) {
            return true;
        }
        return !candidate->info->passable;
};

    const auto& candidates = assets_->getActiveRaw();
    if (candidates.empty()) {
        neighbors.reset();
        impassable_naighbors = nullptr;
        neighbor_lists_initialized_ = false;
        return;
    }

    const bool needs_rebuild = force_update || !neighbors || !neighbor_lists_initialized_ ||
                               last_neighbor_origin_.x != pos.x || last_neighbor_origin_.y != pos.y;
    if (!needs_rebuild) {
        return;
    }

    neighbors = std::make_unique<AssetList>(
        candidates,
        this,
        info->NeighborSearchRadius,
        std::vector<std::string>{},
        std::vector<std::string>{},
        std::vector<std::string>{},
        SortMode::ZIndexAsc,
        base_filter);

    if (neighbors) {
        auto imp_child = std::make_unique<AssetList>(
            *neighbors,
            this,
            info->NeighborSearchRadius,
            std::vector<std::string>{},
            std::vector<std::string>{},
            std::vector<std::string>{},
            SortMode::ZIndexAsc,
            impassable_filter,
            true );
        impassable_naighbors = imp_child.get();
        neighbors->add_child(std::move(imp_child));
    } else {
        impassable_naighbors = nullptr;
    }

    last_neighbor_origin_ = pos;
    neighbor_lists_initialized_ = true;
}

void Asset::set_z_index() {
        int old_z = z_index;
        try {
                if (parent) {
                        if (z_offset > 0)       z_index = parent->z_index + 1;
                        else if (z_offset < 0)  z_index = parent->z_index - 1;
                        else                    z_index = pos.y + info->z_threshold;
                } else if (info) {
                        z_index = pos.y + info->z_threshold;
                }
        } catch (const std::exception& e) {
                std::cerr << "[Asset::set_z_index] Exception: " << e.what() << "\n";
        }
        if (assets_ && z_index != old_z) {
                assets_->mark_active_assets_dirty();
        }
}

void Asset::set_z_offset(int z) {
	z_offset = z;
	set_z_index();
	std::cout << "Z offset set to " << z << " for asset " << info->name << "\n";
}

void Asset::set_flip() {
        if (!info || !info->flipable) return;

        bool use_override = false;
        bool override_value = false;
        if (!spawn_id.empty()) {
                std::lock_guard<std::mutex> lock(s_flip_overrides_mutex_);
                auto it = s_flip_overrides_.find(spawn_id);
                if (it != s_flip_overrides_.end() && it->second.first) {
                        use_override = true;
                        override_value = it->second.second;
                }
        }
        if (use_override) {
                flipped = override_value;
                return;
        }
        std::uniform_int_distribution<int> dist(0, 1);
        bool should_flip;
        {
                std::lock_guard<std::mutex> lock(asset_rng_mutex());
                should_flip = (dist(asset_rng()) == 1);
        }
        flipped = should_flip;
}

void Asset::SetFlipOverrideForSpawnId(const std::string& id, bool enabled, bool flipped) {
        if (id.empty()) return;
        std::lock_guard<std::mutex> lock(s_flip_overrides_mutex_);
        s_flip_overrides_[id] = std::make_pair(enabled, flipped);
}

void Asset::ClearFlipOverrideForSpawnId(const std::string& id) {
        if (id.empty()) return;
        std::lock_guard<std::mutex> lock(s_flip_overrides_mutex_);
        s_flip_overrides_.erase(id);
}

int  Asset::get_shading_group() const { return shading_group; }
bool Asset::is_shading_group_set() const { return shading_group_set; }

void Asset::set_shading_group(int x){
        shading_group = x;
        shading_group_set = true;
}

Area Asset::get_area(const std::string& name) const {
        if (!info) {
                return Area(name, 0);
        }

        Area* base = info->find_area(name);
        if (!base) {
                base = info->find_area(name + "_area");
        }
        if (!base) {
                return Area(name, 0);
        }

        return area_helpers::make_world_area(*info, *base, pos, flipped);
}

void Asset::deactivate() {

        clear_render_caches();
        visibility_stamp = 0;
}

void Asset::MaskRenderMetadata::TextureDefaults::reset() {
        texture     = nullptr;
        r           = 255;
        g           = 255;
        b           = 255;
        a           = 255;
        blend       = SDL_BLENDMODE_BLEND;
        initialized = false;
}

void Asset::MaskRenderMetadata::reset() {
        last_mask_texture = nullptr;
        mask_w            = 0;
        mask_h            = 0;
        has_dimensions    = false;
        mask_defaults.reset();
        base_defaults.reset();
}

void Asset::destroy_render_cache(RenderTextureCache& cache) {
        if (cache.texture) {
                SDL_DestroyTexture(cache.texture);
                cache.texture = nullptr;
        }
        cache.width  = 0;
        cache.height = 0;
}

void Asset::clear_render_caches() {
        destroy_render_cache(shadow_mask_cache_);
        destroy_render_cache(cast_shadow_cache_);
        reset_mask_render_metadata();
        render_pipeline::shading::ClearShadowStateFor(this);
}

void Asset::reset_mask_render_metadata() {
        mask_render_metadata_.reset();
}

void Asset::invalidate_downscale_cache() {
        last_scaled_texture_      = nullptr;
        last_scaled_source_       = nullptr;
        last_scaled_w_            = 0;
        last_scaled_h_            = 0;
        last_scaled_camera_scale_ = -1.0f;
        last_scale_usage_         = {};

        downscale_cache_ready_revision_ = 0;
}

void Asset::refresh_cached_dimensions() {
        int width = 0;
        int height = 0;

        if ((width <= 0 || height <= 0)) {
                SDL_Texture* frame = get_current_variant_texture();
                if (frame) {
                        if (SDL_QueryTexture(frame, nullptr, nullptr, &width, &height) != 0) {
                                width = 0;
                                height = 0;
                        }
                }
        }

        if ((width <= 0 || height <= 0) && info) {
                width  = info->original_canvas_width;
                height = info->original_canvas_height;
        }

        cached_w = (width > 0) ? width : 0;
        cached_h = (height > 0) ? height : 0;
}

void Asset::on_scale_factor_changed() {

        last_scale_usage_ = {};

        refresh_cached_dimensions();

        shadow_mask_cache_.width  = 0;
        shadow_mask_cache_.height = 0;
        cast_shadow_cache_.width  = 0;
        cast_shadow_cache_.height = 0;
        reset_mask_render_metadata();

        float scale_target = 1.0f;
        if (info && std::isfinite(info->scale_factor) && info->scale_factor > 0.0f) {
                scale_target = info->scale_factor;
        }
        scale_smoothing_.reset(scale_target);
        mark_composite_dirty();

        if (!asset_children.empty() && info) {
                for (Asset* asset_child : asset_children) {
                        if (!asset_child || !asset_child->info) {
                                continue;
                        }
                        if (asset_child->info.get() == info.get()) {
                                asset_child->on_scale_factor_changed();
                        }
                }
        }
        if (assets_) {
                assets_->invalidate_max_asset_dimensions();
        }
}

void Asset::set_hidden(bool state){ hidden = state; }
bool  Asset::is_hidden() const { return hidden; }

void Asset::set_merged_from_neighbors(bool state){ merged_from_neighbors_ = state; }
bool  Asset::merged_from_neighbors() const{ return merged_from_neighbors_; }

void Asset::set_highlighted(bool state){ highlighted = state; }
bool  Asset::is_highlighted(){ return highlighted; }

void Asset::set_selected(bool state){ selected = state; }
bool  Asset::is_selected(){ return selected; }

void Asset::cache_grid_residency(SDL_Point point) {
        cached_grid_residency_    = point;
        has_cached_grid_residency_ = true;
}

void Asset::clear_grid_residency_cache() {
        cached_grid_residency_    = SDL_Point{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
        has_cached_grid_residency_ = false;
}

void Asset::sync_transform_to_position() {
        translation_smoothing_x_.reset(static_cast<float>(pos.x));
        translation_smoothing_y_.reset(static_cast<float>(pos.y));
}

bool Asset::has_grid_residency_cache() const {
        return has_cached_grid_residency_;
}

SDL_Point Asset::grid_residency_cache() const {
        return cached_grid_residency_;
}

void Asset::set_grid_id(std::uint64_t id) {
        grid_id_ = id;
}

void Asset::clear_grid_id() {
        grid_id_ = 0;
}

void Asset::set_composite_texture(SDL_Texture* tex) {
    if (composite_texture_ && composite_texture_ != tex) {
        SDL_DestroyTexture(composite_texture_);
    }
    composite_texture_ = tex;
}

float Asset::smoothed_translation_x() const { return translation_smoothing_x_.value_for_render(); }

float Asset::smoothed_translation_y() const { return translation_smoothing_y_.value_for_render(); }

float Asset::smoothed_scale() const { return scale_smoothing_.value_for_render(); }

float Asset::smoothed_alpha() const {
        float value = alpha_smoothing_.value_for_render();
        if (!std::isfinite(value)) {
                value = hidden ? 0.0f : 1.0f;
        }
        return std::clamp(value, 0.0f, 1.0f);
}

Asset::RenderTextureCache& Asset::shadow_mask_cache() { return shadow_mask_cache_; }
Asset::RenderTextureCache& Asset::shadow_mask_cache() const { return shadow_mask_cache_; }
Asset::RenderTextureCache& Asset::cast_shadow_cache() { return cast_shadow_cache_; }
Asset::RenderTextureCache& Asset::cast_shadow_cache() const { return cast_shadow_cache_; }
Asset::MaskRenderMetadata& Asset::mask_render_metadata() { return mask_render_metadata_; }
Asset::MaskRenderMetadata& Asset::mask_render_metadata() const { return mask_render_metadata_; }

void Asset::Delete() {
        dead = true;
        hidden = true;
        if (!animation_children_.empty()) {
                for (auto& slot : animation_children_) {
                        if (slot.spawned_asset) {
                                slot.spawned_asset->Delete();
                                slot.spawned_asset = nullptr;
                        }
                }
                animation_children_.clear();
        }
        if (assets_) {
                assets_->mark_active_assets_dirty();
                assets_->schedule_removal(this);
        }
}
