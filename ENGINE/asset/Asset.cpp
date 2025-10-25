#include "Asset.hpp"
#include "controller_factory.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp"
#include "core/asset_list.hpp"
#include "render/camera.hpp"
#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"
#include "utils/area_helpers.hpp"
#include "asset/asset_types.hpp"
#include "util/grid.hpp"
#include <iostream>
#include <random>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <SDL.h>

namespace {

std::mt19937& asset_rng()
{
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
}

std::mutex& asset_rng_mutex()
{
        static std::mutex mutex;
        return mutex;
}

}

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
        clear_downscale_cache();
}

Asset::~Asset() {
        if (parent) {
                auto& vec = parent->children;
                vec.erase(std::remove(vec.begin(), vec.end(), this), vec.end());
                parent = nullptr;
        }
        for (Asset* c : children) {
                if (c && c->parent == this) c->parent = nullptr;
        }
        clear_downscale_cache();
        clear_render_caches();
        if (final_texture) {
                SDL_DestroyTexture(final_texture);
                final_texture = nullptr;
        }
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
, children(o.children)
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
, anim_(nullptr)
, last_scaled_texture_(nullptr)
, last_scaled_source_(nullptr)
, last_scaled_w_(0)
, last_scaled_h_(0)
, last_scaled_camera_scale_(-1.0f)
, last_scale_usage_()
, final_texture_revision_(o.final_texture_revision_)
{
        clear_downscale_cache();
        clear_render_caches();
        last_scale_usage_ = o.last_scale_usage_;
        cached_grid_residency_    = o.cached_grid_residency_;
        has_cached_grid_residency_ = o.has_cached_grid_residency_;
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
        children             = o.children;
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
        assets_              = o.assets_;
        spawn_id             = o.spawn_id;
        spawn_method         = o.spawn_method;
        owning_room_name_    = o.owning_room_name_;
        controller_.reset();
        anim_.reset();
        last_scaled_texture_      = nullptr;
        last_scaled_source_       = nullptr;
        last_scaled_w_            = 0;
        last_scaled_h_            = 0;
        last_scaled_camera_scale_ = -1.0f;
        last_scale_usage_         = o.last_scale_usage_;
        cached_grid_residency_    = o.cached_grid_residency_;
        has_cached_grid_residency_ = o.has_cached_grid_residency_;
        return *this;
}

void Asset::finalize_setup() {
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
	for (Asset* child : children)
	if (child) child->finalize_setup();
        #ifdef VIBBLE_DEBUG_ASSET_LOGS
        if (!children.empty()) {
                std::cout << "[Asset] \"" << (info ? info->name : std::string{"<null>"})
                << "\" at (" << pos.x << ", " << pos.y
                << ") has " << children.size() << " child(ren):\n";
                for (Asset* child : children)
                if (child && child->info)
                std::cout << "    - \"" << child->info->name
                << "\" at (" << child->pos.x << ", " << child->pos.y << ")\n";
        }
        #endif
        if (assets_ && !anim_) {
                anim_ = std::make_unique<AnimationUpdate>(this, assets_);
        }
        if (assets_ && !controller_) {
                ControllerFactory cf(assets_);
                controller_ = cf.create_for_asset(this);
        }
        NeighborSearchRadius = info->NeighborSearchRadius;
        refresh_cached_dimensions();
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
                    anim_->set_animation_now(def->first);
                } else {
                    current_animation = def->first;
                    Animation& anim   = def->second;
                    current_frame     = anim.get_first_frame();
                    frame_progress    = 0.0f;
                    static_frame      = anim.is_static() || anim.locked;
                }
            }
        } else {
            Animation& anim = iti->second;
            if (anim.index_of(current_frame) < 0) {
                std::size_t path_index = anim_ ? anim_->path_index_for(current_animation) : 0;
                current_frame = anim.get_first_frame(path_index);
                frame_progress = 0.0f;
                static_frame = anim.is_static() || anim.locked;
            }
        }
    }

    if (!dead && anim_) {
        anim_->update();
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
}

std::string Asset::get_current_animation() const { return current_animation; }

bool Asset::is_current_animation_locked_in_progress() const {
        if (!info || !current_frame) return false;
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

void Asset::add_child(Asset* child) {
        if (!child || !child->info) return;
        if (info) {
                for (const auto& ci : info->children) {
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

                        if (!child_spawn_id.empty() && child_spawn_id == child->spawn_id) {
                                int z_offset = ci.z_offset;
                                if (ci.placed_on_top_parent && z_offset <= 0) {
                                        z_offset = 1;
                                }
                                child->set_z_offset(z_offset);
                                break;
                        }
                }
        }
        child->parent = this;
        if (!child->get_assets()) child->set_assets(this->assets_);
        child->set_z_index();
        children.push_back(child);
}

void Asset::set_assets(Assets* a) {
    assets_ = a;
    if (assets_ && !anim_) {
            anim_ = std::make_unique<AnimationUpdate>(this, assets_);
    }
    if (!controller_ && assets_) {
            ControllerFactory cf(assets_);
            controller_ = cf.create_for_asset(this);
    }
    neighbors.reset();
    impassable_naighbors = nullptr;
    neighbor_lists_initialized_ = false;
    last_neighbor_origin_ = SDL_Point{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
}

void Asset::set_owning_room_name(std::string name) {
    owning_room_name_ = std::move(name);
}

AssetList* Asset::get_neighbors_list() { return neighbors.get(); }
const AssetList* Asset::get_neighbors_list() const { return neighbors.get(); }
AssetList* Asset::get_impassable_naighbors() { return impassable_naighbors; }
const AssetList* Asset::get_impassable_naighbors() const { return impassable_naighbors; }

void Asset::update_neighbor_lists(bool force_update) {
    if (!assets_ || !info || !info->moving_asset) {
        return;
    }

    AssetList* active = assets_->active_asset_list_.get();
    if (!active) {
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

    const bool rebuild_neighbors = force_update || !neighbors;

    if (!rebuild_neighbors && !force_update) {
        if (neighbor_lists_initialized_ && last_neighbor_origin_.x == pos.x && last_neighbor_origin_.y == pos.y) {

            return;
        }
    }

    if (rebuild_neighbors) {
        neighbors = std::make_unique<AssetList>(
            *active,
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
        }
    } else if (neighbors) {
        neighbors->set_center(this);
        neighbors->set_search_radius(info->NeighborSearchRadius);
        neighbors->update();
        if (!impassable_naighbors) {
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
        }
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
        std::uniform_int_distribution<int> dist(0, 1);
        bool should_flip;
        {
                std::lock_guard<std::mutex> lock(asset_rng_mutex());
                should_flip = (dist(asset_rng()) == 1);
        }
        flipped = should_flip;
}

void Asset::set_final_texture(SDL_Texture* tex) {
        if (tex != final_texture) {
                if (final_texture) {
                        SDL_DestroyTexture(final_texture);
                }
                final_texture = tex;
                clear_downscale_cache();
        }

        invalidate_downscale_cache();

        if (tex) {
                SDL_QueryTexture(tex, nullptr, nullptr, &cached_w, &cached_h);
        } else {
                cached_w = cached_h = 0;
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
                return Area(name);
        }

        Area* base = info->find_area(name);
        if (!base) {
                base = info->find_area(name + "_area");
        }
        if (!base) {
                return Area(name);
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
        destroy_render_cache(light_front_cache_);
        destroy_render_cache(light_behind_cache_);
        destroy_render_cache(shadow_mask_cache_);
        destroy_render_cache(motion_blur_cache_);
        render_pipeline::shading::ClearShadowStateFor(this);
}

void Asset::invalidate_downscale_cache() {
        ++final_texture_revision_;

        last_scaled_texture_      = nullptr;
        last_scaled_source_       = nullptr;
        last_scaled_w_            = 0;
        last_scaled_h_            = 0;
        last_scaled_camera_scale_ = -1.0f;
        last_scale_usage_         = {};
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

void Asset::on_scale_factor_changed() {
        clear_downscale_cache();
        last_scale_usage_ = {};
        refresh_cached_dimensions();

        light_front_cache_.width  = 0;
        light_front_cache_.height = 0;
        light_behind_cache_.width  = 0;
        light_behind_cache_.height = 0;
        shadow_mask_cache_.width  = 0;
        shadow_mask_cache_.height = 0;
        motion_blur_cache_.width  = 0;
        motion_blur_cache_.height = 0;

        if (!children.empty() && info) {
                for (Asset* child : children) {
                        if (!child || !child->info) {
                                continue;
                        }
                        if (child->info.get() == info.get()) {
                                child->on_scale_factor_changed();
                        }
                }
        }
}

void Asset::update_scale_usage(float requested, float texture_scale, float remainder, int variant_index) {
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

        if (info) {
                render_pipeline::ScalingLogic::RecordUsage(info->name, requested, texture_scale);
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

bool Asset::has_grid_residency_cache() const {
        return has_cached_grid_residency_;
}

SDL_Point Asset::grid_residency_cache() const {
        return cached_grid_residency_;
}

Asset::RenderTextureCache& Asset::light_front_cache() { return light_front_cache_; }
Asset::RenderTextureCache& Asset::light_front_cache() const { return light_front_cache_; }
Asset::RenderTextureCache& Asset::light_behind_cache() { return light_behind_cache_; }
Asset::RenderTextureCache& Asset::light_behind_cache() const { return light_behind_cache_; }
Asset::RenderTextureCache& Asset::shadow_mask_cache() { return shadow_mask_cache_; }
Asset::RenderTextureCache& Asset::shadow_mask_cache() const { return shadow_mask_cache_; }
Asset::RenderTextureCache& Asset::motion_blur_cache() { return motion_blur_cache_; }
Asset::RenderTextureCache& Asset::motion_blur_cache() const { return motion_blur_cache_; }

void Asset::Delete() {
        dead = true;
        hidden = true;
        if (assets_) {
                assets_->mark_active_assets_dirty();
                assets_->schedule_removal(this);
        }
}
