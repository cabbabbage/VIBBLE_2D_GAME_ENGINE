#include "Asset.hpp"
#include "controller_factory.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "utils/light_utils.hpp"
#include <filesystem>
#include <iostream>
#include <random>
#include <algorithm>
#include <SDL.h>

Asset::Asset(std::shared_ptr<AssetInfo> info_,
             const Area& spawn_area,
             SDL_Point start_pos,
             int depth_,
             Asset* parent_,
             const std::string& spawn_id_,
             const std::string& spawn_method_)
: parent(parent_)
, info(std::move(info_))
, current_animation()
, static_frame(false)
, active(false)
, pos(start_pos)
, z_index(0)
, z_offset(0)
, alpha_percentage(1.0)
, depth(depth_)
, spawn_id(spawn_id_)
, spawn_method(spawn_method_)
{
	set_flip();
	set_z_index();
	if (info) {
		try { has_shading = info->has_shading; } catch (...) { has_shading = false; }
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
                        std::mt19937 g{ std::random_device{}() };
                        std::uniform_int_distribution<int> d(0, int(anim.frames.size()) - 1);
                        int idx = d(g);
                        AnimationFrame* f = anim.get_first_frame();
                        while (idx-- > 0 && f && f->next) { f = f->next; }
                        current_frame = f;
                }
        }
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
, z_index(o.z_index)
, z_offset(o.z_offset)
, active(o.active)
, flipped(o.flipped)
, render_player_light(o.render_player_light)
, alpha_percentage(o.alpha_percentage)
, distance_to_player_sq(o.distance_to_player_sq)
, children(o.children)
, static_lights(o.static_lights)
, depth(o.depth)
, has_shading(o.has_shading)
, dead(o.dead)
, static_frame(o.static_frame)
, cached_w(o.cached_w)
, cached_h(o.cached_h)
, window(o.window)
, highlighted(o.highlighted)
, hidden(o.hidden)
, selected(o.selected)
, current_frame(o.current_frame)
, frame_progress(o.frame_progress)
, shading_group(o.shading_group)
, shading_group_set(o.shading_group_set)
, final_texture(o.final_texture)
, assets_(o.assets_)
, spawn_id(o.spawn_id)
, spawn_method(o.spawn_method)
, controller_(nullptr)
, anim_(nullptr)
{
}

Asset& Asset::operator=(const Asset& o) {
	if (this == &o) return *this;
	parent               = o.parent;
	info                 = o.info;
	current_animation    = o.current_animation;
    pos                  = o.pos;
	z_index              = o.z_index;
	z_offset             = o.z_offset;
	active               = o.active;
	flipped              = o.flipped;
	render_player_light  = o.render_player_light;
	alpha_percentage     = o.alpha_percentage;
        distance_to_player_sq = o.distance_to_player_sq;
	children             = o.children;
	static_lights        = o.static_lights;
	depth                = o.depth;
	has_shading          = o.has_shading;
	dead                 = o.dead;
	static_frame         = o.static_frame;
	cached_w             = o.cached_w;
	cached_h             = o.cached_h;
	window               = o.window;
	highlighted          = o.highlighted;
	hidden               = o.hidden;
	selected             = o.selected;
        current_frame        = o.current_frame;
        frame_progress       = o.frame_progress;
	shading_group        = o.shading_group;
	shading_group_set    = o.shading_group_set;
	final_texture        = o.final_texture;
	assets_              = o.assets_;
        spawn_id             = o.spawn_id;
        spawn_method         = o.spawn_method;
        controller_.reset();
        anim_.reset();
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
                                        std::mt19937 rng{ std::random_device{}() };
                                        std::uniform_int_distribution<int> dist(0, int(anim.frames.size()) - 1);
                                        int idx = dist(rng);
                                        AnimationFrame* f = anim.get_first_frame();
                                        while (idx-- > 0 && f && f->next) { f = f->next; }
                                        current_frame = f;
                        }
                }
	}
	for (Asset* child : children)
	if (child) child->finalize_setup();
	if (!children.empty()) {
		std::cout << "[Asset] \"" << (info ? info->name : std::string{"<null>"})
                << "\" at (" << pos.x << ", " << pos.y
                << ") has " << children.size() << " child(ren):\n";
                for (Asset* child : children)
                if (child && child->info)
                std::cout << "    - \"" << child->info->name
                << "\" at (" << child->pos.x << ", " << child->pos.y << ")\n";
	}
        if (assets_ && !anim_) {
                anim_ = std::make_unique<AnimationUpdate>(this, assets_->active_manager());
        }
        if (assets_ && !controller_) {
                ControllerFactory cf(assets_);
                controller_ = cf.create_for_asset(this);
        }
}

SDL_Texture* Asset::get_current_frame() const {
        if (!info) return nullptr;
        auto iti = info->animations.find(current_animation);
        if (iti == info->animations.end()) return nullptr;

        const Animation& anim = iti->second;
        // If our current_frame pointer doesn't belong to this animation anymore,
        // fall back to the first frame to avoid invalid memory access.
        int idx_anim = anim.index_of(current_frame);
        if (idx_anim < 0) {
            // Mutable fallback: this is logically const, but we need to heal the pointer.
            const_cast<Asset*>(this)->current_frame = const_cast<AnimationFrame*>(anim.frames_data.empty() ? nullptr : &anim.frames_data[0]);
            const_cast<Asset*>(this)->frame_progress = 0.0f;
        }

        return anim.get_frame(current_frame);
}

void Asset::update() {
    if (!info) return;

    if (controller_ && assets_) {
        if (Input* in = assets_->get_input()) {
            controller_->update(*in);
        }
    }

    // Heal desynced frame/animation state before advancing animations
    if (anim_) {
        auto iti = info->animations.find(current_animation);
        if (iti == info->animations.end()) {
            // Fallback to a safe animation if the current id is missing
            auto def = info->animations.find("default");
            if (def == info->animations.end()) def = info->animations.begin();
            if (def != info->animations.end()) {
                current_animation = def->first;
                current_frame     = def->second.get_first_frame();
                frame_progress    = 0.0f;
                static_frame      = def->second.is_static();
            }
        } else {
            Animation& anim = iti->second;
            if (anim.index_of(current_frame) < 0) {
                current_frame = anim.get_first_frame();
                frame_progress = 0.0f;
                static_frame = anim.is_static();
            }
        }
    }

    if (!dead && anim_) {
        anim_->update();
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
			try {
					if (std::filesystem::path(ci.json_path).stem().string() == child->info->name) {
								child->set_z_offset(ci.z_offset);
								break;
					}
			} catch (...) {}
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
                anim_ = std::make_unique<AnimationUpdate>(this, assets_->active_manager());
        }
        if (!controller_ && assets_) {
                ControllerFactory cf(assets_);
                controller_ = cf.create_for_asset(this);
        }
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
                assets_->active_manager().markNeedsSort();
        }
}

void Asset::set_z_offset(int z) {
	z_offset = z;
	set_z_index();
	std::cout << "Z offset set to " << z << " for asset " << info->name << "\n";
}

void Asset::set_flip() {
	if (!info || !info->flipable) return;
	std::mt19937 rng{ std::random_device{}() };
	std::uniform_int_distribution<int> dist(0, 1);
	flipped = (dist(rng) == 1);
}

void Asset::set_final_texture(SDL_Texture* tex) {
	if (final_texture) SDL_DestroyTexture(final_texture);
	final_texture = tex;
	if (tex) SDL_QueryTexture(tex, nullptr, nullptr, &cached_w, &cached_h);
	else     cached_w = cached_h = 0;
}

SDL_Texture* Asset::get_final_texture() const { return final_texture; }
int  Asset::get_shading_group() const { return shading_group; }
bool Asset::is_shading_group_set() const { return shading_group_set; }

void Asset::set_shading_group(int x){
	shading_group = x;
	shading_group_set = true;
}

void Asset::add_static_light_source(LightSource* light, SDL_Point world, Asset* owner) {
    if (!light) return;

    StaticLight sl;
    sl.source = light;
    sl.offset = { world.x - pos.x, world.y - pos.y };
    sl.alpha_percentage = LightUtils::calculate_static_alpha_percentage(this, owner);
    static_lights.push_back(sl);
}


void Asset::set_render_player_light(bool value) { render_player_light = value; }
bool Asset::get_render_player_light() const { return render_player_light; }

Area Asset::get_area(const std::string& name) const {
	Area result(name);
	if (info) {
		Area* a = info->find_area(name + "_area");
		if (a) result = *a;	
	}
	float scale = (window ? window->get_scale() : 1.0f);
	float inv   = (scale != 0.0f) ? 1.0f / scale : 1.0f;
	result.scale(inv);
	if (flipped) result.flip_horizontal();
	return result;
}

void Asset::deactivate() {
	if (final_texture) {
		SDL_DestroyTexture(final_texture);
		final_texture = nullptr;
	}
}

void Asset::set_hidden(bool state){ hidden = state; }
bool  Asset::is_hidden(){ return hidden; }

void Asset::set_highlighted(bool state){ highlighted = state; }
bool  Asset::is_highlighted(){ return highlighted; }

void Asset::set_selected(bool state){ selected = state; }
bool  Asset::is_selected(){ return selected; }

void Asset::Delete() {
	dead = true;
	hidden = true;
	if (assets_) {
            assets_->active_manager().remove(this);
		assets_->schedule_removal(this);
	}
}
