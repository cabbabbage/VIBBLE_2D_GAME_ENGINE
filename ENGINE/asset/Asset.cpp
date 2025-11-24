#include "Asset.hpp"
#include "controller_factory.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp"
#include "core/asset_list.hpp"
#include "render/camera.hpp"
#include "render/render.hpp"
#include "animation_update/animation_runtime.hpp"
#include "utils/area_helpers.hpp"
#include "asset/asset_types.hpp"
#include "utils/grid.hpp"
#include "utils/transform_smoothing_settings.hpp"
#include <iostream>
#include <random>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <limits>
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

// Static storage for per-spawn-group flip overrides
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
        // Ensure the player is always pixel-precise (no grid snapping during movement/rendering)
        try {
                if (info && asset_types::canonicalize(info->type) == asset_types::player) {
                        grid_resolution = 0; // force 0-resolution grid for the player
                }
        } catch (...) {
                // If type introspection fails, leave grid_resolution as-is
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
        translation_smoothing_x_.set_params(transform_smoothing::asset_translation_params());
        translation_smoothing_y_.set_params(transform_smoothing::asset_translation_params());
        scale_smoothing_.set_params(transform_smoothing::asset_scale_params());
        alpha_smoothing_.set_params(transform_smoothing::asset_alpha_params());

        translation_smoothing_x_.reset(static_cast<float>(pos.x));
        translation_smoothing_y_.reset(static_cast<float>(pos.y));
        const float initial_scale = (info && std::isfinite(info->scale_factor) && info->scale_factor > 0.0f)
                                        ? info->scale_factor
                                        : 1.0f;
        scale_smoothing_.reset(initial_scale);
        alpha_smoothing_.reset(hidden ? 0.0f : 1.0f);

        clear_downscale_cache();
        recompute_local_bounds_square();
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
        clear_downscale_cache();
        clear_render_caches();
        if (final_texture) {
                SDL_DestroyTexture(final_texture);
                final_texture = nullptr;
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
, depth(o.depth)
, is_shaded(o.is_shaded)
, dead(o.dead)
, static_frame(o.static_frame)
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
, final_texture(o.final_texture)
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
, final_texture_revision_(o.final_texture_revision_)
, last_rendered_frame_(nullptr)
, scale_variant_state_(o.scale_variant_state_)
, base_bounds_local_(o.base_bounds_local_)
{
        clear_downscale_cache();
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
}

Asset& Asset::operator=(const Asset& o) {
        if (this == &o) return *this;
        clear_downscale_cache();
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
	depth                = o.depth;
        is_shaded            = o.is_shaded;
	dead                 = o.dead;
	static_frame         = o.static_frame;
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
        final_texture        = o.final_texture;
        final_texture_revision_ = o.final_texture_revision_;
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
        if (assets_ && !controller_) {
                ControllerFactory cf(assets_);
                controller_ = cf.create_for_asset(this);
        }
        NeighborSearchRadius = info->NeighborSearchRadius;
        refresh_cached_dimensions();
        finalized_ = true;
}

SDL_Texture* Asset::get_current_frame() const {
        if (!info) return nullptr;
        auto iti = info->animations.find(current_animation);
        if (iti == info->animations.end()) return nullptr;

        Animation& anim = const_cast<Animation&>(iti->second);

        int idx_anim = anim.index_of(current_frame);
        if (idx_anim < 0) {
            std::size_t path_index = 0;
            if (anim_) {
                path_index = anim_->path_index_for(current_animation);
            }
            const_cast<Asset*>(this)->current_frame = anim.get_first_frame(path_index);
            const_cast<Asset*>(this)->frame_progress = 0.0f;
        }

        return anim.get_frame(current_frame);
}

SDL_Texture* Asset::get_current_mask_texture(std::size_t variant_index) const {
        if (!info) {
                return nullptr;
        }

        auto anim_it = info->animations.find(current_animation);
        if (anim_it == info->animations.end()) {
                return nullptr;
        }

        Animation& anim = const_cast<Animation&>(anim_it->second);

        AnimationFrame* frame = current_frame;
        if (!frame) {
                std::size_t path_index = anim_ ? anim_->path_index_for(current_animation) : 0;
                frame = anim.get_first_frame(path_index);
                if (!frame) {
                        return nullptr;
                }
                const_cast<Asset*>(this)->current_frame = frame;
                const_cast<Asset*>(this)->frame_progress = 0.0f;
        }

        int frame_index = anim.index_of(frame);
        if (frame_index < 0) {
                std::size_t path_index = anim_ ? anim_->path_index_for(current_animation) : 0;
                frame = anim.get_first_frame(path_index);
                if (!frame) {
                        return nullptr;
                }
                const_cast<Asset*>(this)->current_frame = frame;
                const_cast<Asset*>(this)->frame_progress = 0.0f;
                frame_index = anim.index_of(frame);
                if (frame_index < 0) {
                        return nullptr;
                }
        }

        return anim.mask_variant(static_cast<std::size_t>(frame_index), variant_index);
}

void Asset::update() {
    if (!info) return;

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

    const float dt = assets_ ? assets_->frame_delta_seconds() : (1.0f / 60.0f);
    translation_smoothing_x_.target = static_cast<float>(pos.x);
    translation_smoothing_y_.target = static_cast<float>(pos.y);

    float scale_target = 1.0f;
    if (info && std::isfinite(info->scale_factor) && info->scale_factor > 0.0f) {
        scale_target = info->scale_factor;
    }
    scale_smoothing_.target = scale_target;

    const float alpha_target = hidden ? 0.0f : 1.0f;
    alpha_smoothing_.target  = alpha_target;

    translation_smoothing_x_.advance(dt);
    translation_smoothing_y_.advance(dt);
    scale_smoothing_.advance(dt);
    alpha_smoothing_.advance(dt);
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
                for (const auto& ci : info->asset_children) {
                        if (!ci.spawn_group.is_object()) {
                                continue;
                        }
                        std::string child_spawn_id;
                        try {
                                if (ci.spawn_group.contains("spawn_id") && ci.spawn_group["spawn_id"].is_string()) {
                                        child_spawn_id = ci.spawn_group["spawn_id"].get<std::string>();
                                }
                        } catch (...) {
                                child_spawn_id.clear();
                        }

                        if (!child_spawn_id.empty() && child_spawn_id == asset_child->spawn_id) {
                                int z_offset = ci.z_offset;
                                if (ci.placed_on_top_parent && z_offset <= 0) {
                                        z_offset = 1;
                                }
                                asset_child->set_z_offset(z_offset);
                                break;
                        }
                }
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

void Asset::ensure_animation_runtime(bool force_recreate) {
    if (!assets_) {
        return;
    }
    if (!force_recreate && anim_ && anim_runtime_) {
        return;
    }
    anim_runtime_.reset();
    anim_.reset();
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
        // Check for explicit flip override set per spawn group
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

// Static API to control flip overrides by spawn_id
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

void Asset::set_final_texture(SDL_Texture* tex) {
        int new_w = 0;
        int new_h = 0;
        if (tex) {
                if (SDL_QueryTexture(tex, nullptr, nullptr, &new_w, &new_h) != 0) {
                        new_w = 0;
                        new_h = 0;
                }
        }

        const bool texture_changed = (tex != final_texture);
        const bool size_changed    = (new_w != cached_w) || (new_h != cached_h);

        if (texture_changed) {
                if (final_texture) {
                        SDL_DestroyTexture(final_texture);
                }
                final_texture = tex;
                if (size_changed) {
                        clear_downscale_cache();
                }
        } else if (size_changed) {
                clear_downscale_cache();
        }

        invalidate_downscale_cache();

        if (tex) {
                cached_w = new_w;
                cached_h = new_h;
        } else {
                cached_w = 0;
                cached_h = 0;
        }
}

SDL_Texture* Asset::get_final_texture() const { return final_texture; }
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
        clear_downscale_cache();
        clear_render_caches();
        if (final_texture) {
                SDL_DestroyTexture(final_texture);
                final_texture = nullptr;
        }
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
        ++final_texture_revision_;

        last_scaled_texture_      = nullptr;
        last_scaled_source_       = nullptr;
        last_scaled_w_            = 0;
        last_scaled_h_            = 0;
        last_scaled_camera_scale_ = -1.0f;
        last_scale_usage_         = {};
        reset_scale_variant_state();
        downscale_cache_ready_revision_ = 0;
}

void Asset::clear_downscale_cache() {
        const auto& steps = (info && !info->scale_variants.empty()) ? static_cast<const std::vector<float>&>(info->scale_variants) : render_pipeline::ScalingLogic::DefaultScaleSteps();

        for (std::size_t idx = 0; idx < downscale_cache_.size(); ++idx) {
                auto& entry = downscale_cache_[idx];
                if (idx != 0 && entry.texture) {
                        SDL_DestroyTexture(entry.texture);
                }
        }

        downscale_cache_.clear();
        downscale_cache_.resize(steps.size());
        for (std::size_t idx = 0; idx < downscale_cache_.size(); ++idx) {
                auto& entry = downscale_cache_[idx];
                entry.texture = nullptr;
                entry.width   = 0;
                entry.height  = 0;
                entry.scale   = (idx < steps.size()) ? steps[idx] : 1.0f;
                entry.revision = 0;
        }

        last_scaled_texture_      = nullptr;
        last_scaled_source_       = nullptr;
        last_scaled_w_            = 0;
        last_scaled_h_            = 0;
        last_scaled_camera_scale_ = -1.0f;
        last_scale_usage_         = {};
        reset_scale_variant_state();
        downscale_cache_ready_revision_ = 0;
}

void Asset::reset_scale_variant_state() {
        scale_variant_state_.last_variant_index = 0;
        scale_variant_state_.hysteresis_min     = 0.0f;
        scale_variant_state_.hysteresis_max     = std::numeric_limits<float>::max();
}

void Asset::refresh_cached_dimensions() {
        int width = 0;
        int height = 0;

        if (final_texture) {
                if (SDL_QueryTexture(final_texture, nullptr, nullptr, &width, &height) != 0) {
                        width = 0;
                        height = 0;
                }
        }

        if ((width <= 0 || height <= 0)) {
                SDL_Texture* frame = get_current_frame();
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

void Asset::recompute_local_bounds_square() {
        auto expand_rect = [&](float left, float top, float right, float bottom, bool& initialized, float& min_x, float& max_x, float& min_y, float& max_y) {
                if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(top) || !std::isfinite(bottom)) {
                        return;
                }
                if (!initialized) {
                        min_x = left;
                        max_x = right;
                        min_y = top;
                        max_y = bottom;
                        initialized = true;
                        return;
                }
                min_x = std::min(min_x, left);
                max_x = std::max(max_x, right);
                min_y = std::min(min_y, top);
                max_y = std::max(max_y, bottom);
        };

        float min_x = 0.0f;
        float max_x = 0.0f;
        float min_y = 0.0f;
        float max_y = 0.0f;
        bool has_rect = false;

        auto include_frame = [&](int frame_w, int frame_h) {
                if (frame_w <= 0 || frame_h <= 0) {
                        return;
                }
                const float half_w = 0.5f * static_cast<float>(frame_w);
                const float height = static_cast<float>(frame_h);
                expand_rect(-half_w, -height, half_w, 0.0f, has_rect, min_x, max_x, min_y, max_y);
        };

        auto include_centered_rect = [&](float center_x, float center_y, float width, float height) {
                if (!std::isfinite(center_x) || !std::isfinite(center_y) || !std::isfinite(width) || !std::isfinite(height)) {
                        return false;
                }
                if (width <= 0.0f || height <= 0.0f) {
                        return false;
                }
                const float half_w = 0.5f * width;
                const float half_h = 0.5f * height;
                expand_rect(center_x - half_w,
                            center_y - half_h,
                            center_x + half_w,
                            center_y + half_h,
                            has_rect,
                            min_x,
                            max_x,
                            min_y,
                            max_y);
                return true;
        };

        if (info) {
                for (const auto& entry : info->animations) {
                        const Animation& animation = entry.second;
                        if (animation.frames.empty()) {
                                continue;
                        }
                        for (SDL_Texture* tex : animation.frames) {
                                if (!tex) {
                                        continue;
                                }
                                int frame_w = 0;
                                int frame_h = 0;
                                if (SDL_QueryTexture(tex, nullptr, nullptr, &frame_w, &frame_h) != 0) {
                                        continue;
                                }
                                include_frame(frame_w, frame_h);
                        }
                }

                for (const auto& light : info->light_sources) {
                        const float offset_x = static_cast<float>(light.offset_x);
                        const float offset_y = static_cast<float>(light.offset_y);

                        auto include_light_texture = [&](int tex_w, int tex_h) {
                                if (tex_w <= 0 || tex_h <= 0) {
                                        return false;
                                }
                                const float width_f  = static_cast<float>(tex_w);
                                const float height_f = static_cast<float>(tex_h);
                                return include_centered_rect(offset_x, offset_y, width_f, height_f);
                        };

                        bool texture_accounted_for = false;
                        int tex_w = light.cached_w;
                        int tex_h = light.cached_h;
                        if (tex_w <= 0 || tex_h <= 0) {
                                if (light.texture) {
                                        int queried_w = 0;
                                        int queried_h = 0;
                                        if (SDL_QueryTexture(light.texture, nullptr, nullptr, &queried_w, &queried_h) == 0) {
                                                tex_w = queried_w;
                                                tex_h = queried_h;
                                        }
                                }
                        }
                        if (tex_w > 0 && tex_h > 0) {
                                texture_accounted_for = include_light_texture(tex_w, tex_h);
                        }

                        const float radius = static_cast<float>(light.radius);
                        if (radius > 0.0f && std::isfinite(radius)) {
                                expand_rect(offset_x - radius,
                                            offset_y - radius,
                                            offset_x + radius,
                                            offset_y + radius,
                                            has_rect,
                                            min_x,
                                            max_x,
                                            min_y,
                                            max_y);
                        } else if (!texture_accounted_for) {
                                // No usable radius or texture to expand from; skip.
                                continue;
                        }
                }
        }

        if (!has_rect && info) {
                include_frame(std::max(0, info->original_canvas_width),
                              std::max(0, info->original_canvas_height));
        }

        if (!has_rect) {
                expand_rect(-0.5f, -0.5f, 0.5f, 0.5f, has_rect, min_x, max_x, min_y, max_y);
        }

        if (!has_rect) {
                base_bounds_local_ = BoundsSquare{};
                return;
        }

        const float width  = std::max(0.0f, max_x - min_x);
        const float height = std::max(0.0f, max_y - min_y);
        const float size   = std::max(width, height);

        BoundsSquare computed{};
        computed.center_x = min_x + width * 0.5f;
        computed.center_y = min_y + height * 0.5f;
        computed.half_size = (size > 0.0f && std::isfinite(size)) ? size * 0.5f : 0.5f;
        base_bounds_local_ = computed;
}

void Asset::on_scale_factor_changed() {
        clear_downscale_cache();
        last_scale_usage_ = {};
        reset_scale_variant_state();
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

void Asset::update_scale_usage(float requested,
                               float texture_scale,
                               float remainder,
                               int   variant_index,
                               float hysteresis_min,
                               float hysteresis_max) {
        if (!std::isfinite(requested) || requested <= 0.0f) {
                requested = 1.0f;
        }
        if (!std::isfinite(texture_scale) || texture_scale <= 0.0f) {
                texture_scale = 1.0f;
        }
        if (!std::isfinite(remainder) || remainder <= 0.0f) {
                remainder = 1.0f;
        }
        last_scale_usage_.requested_scale = requested;
        last_scale_usage_.texture_scale   = texture_scale;
        last_scale_usage_.remainder_scale = remainder;
        const int max_index = downscale_cache_.empty() ? 0 : static_cast<int>(downscale_cache_.size() - 1);
        last_scale_usage_.variant_index   = std::clamp(variant_index, 0, max_index);
        scale_variant_state_.last_variant_index = last_scale_usage_.variant_index;
        if (!std::isfinite(hysteresis_min) || hysteresis_min < 0.0f) {
                hysteresis_min = 0.0f;
        }
        if (!std::isfinite(hysteresis_max) || hysteresis_max <= hysteresis_min) {
                hysteresis_max = std::numeric_limits<float>::max();
        }
        scale_variant_state_.hysteresis_min = hysteresis_min;
        scale_variant_state_.hysteresis_max = std::max(hysteresis_max, hysteresis_min);
}

void Asset::set_smoothing_params(const TransformSmoothingParams& translation,
                                 const TransformSmoothingParams& scale,
                                 const TransformSmoothingParams& alpha) {
        auto sanitize = [](const TransformSmoothingParams& params) {
                TransformSmoothingParams result = params;
                if (!std::isfinite(result.lerp_rate) || result.lerp_rate < 0.0f) {
                        result.lerp_rate = 0.0f;
                }
                if (!std::isfinite(result.spring_frequency) || result.spring_frequency < 0.0f) {
                        result.spring_frequency = 0.0f;
                }
                if (!std::isfinite(result.max_step) || result.max_step < 0.0f) {
                        result.max_step = 0.0f;
                }
                if (!std::isfinite(result.snap_threshold) || result.snap_threshold < 0.0f) {
                        result.snap_threshold = 0.0f;
                }
                switch (result.method) {
                case TransformSmoothingMethod::None:
                case TransformSmoothingMethod::Lerp:
                case TransformSmoothingMethod::CriticallyDampedSpring:
                        break;
                default:
                        result.method = TransformSmoothingMethod::None;
                        break;
                }
                return result;
        };

        TransformSmoothingParams translation_params = sanitize(translation);
        translation_smoothing_x_.set_params(translation_params);
        translation_smoothing_y_.set_params(translation_params);

        TransformSmoothingParams scale_params = sanitize(scale);
        scale_smoothing_.set_params(scale_params);

        TransformSmoothingParams alpha_params = sanitize(alpha);
        alpha_smoothing_.set_params(alpha_params);
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

bool Asset::has_grid_residency_cache() const {
        return has_cached_grid_residency_;
}

SDL_Point Asset::grid_residency_cache() const {
        return cached_grid_residency_;
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
