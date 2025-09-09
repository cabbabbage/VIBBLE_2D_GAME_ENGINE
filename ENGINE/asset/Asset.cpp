#include "Asset.hpp"
#include "controller_factory.hpp"
#include "animation_manager.hpp"
#include "core/AssetsManager.hpp"
#include "view.hpp"
#include "utils/light_utils.hpp"
#include <filesystem>
#include <iostream>
#include <random>
#include <algorithm>
Asset::Asset(std::shared_ptr<AssetInfo> info_,
             const Area& spawn_area,
             int start_pos_X,
             int start_pos_Y,
             int depth_,
             Asset* parent_,
             const std::string& spawn_id_,
             const std::string& spawn_method_)
: parent(parent_)
, info(std::move(info_))
, current_animation()
, current_frame_index(0)
, static_frame(false)
, active(false)
, pos_X(start_pos_X)
, pos_Y(start_pos_Y)
, z_index(0)
, z_offset(0)
, player_speed(10)
, is_lit(info->has_light_source)
, alpha_percentage(1.0)
, has_base_shadow(false)
, spawn_area_local(spawn_area)
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
	if (it == info->animations.end())
	it = info->animations.find("default");
	if (it != info->animations.end() && !it->second.frames.empty()) {
		current_animation = it->first;
		static_frame = (it->second.frames.size() == 1);
		if ((it->second.randomize || it->second.rnd_start) && it->second.frames.size() > 1) {
			std::mt19937 g{ std::random_device{}() };
			std::uniform_int_distribution<int> d(0, int(it->second.frames.size()) - 1);
			current_frame_index = d(g);
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
, pos_X(o.pos_X)
, pos_Y(o.pos_Y)
, screen_X(o.screen_X)
, screen_Y(o.screen_Y)
, z_index(o.z_index)
, z_offset(o.z_offset)
, player_speed(o.player_speed)
, is_lit(o.is_lit)
, has_base_shadow(o.has_base_shadow)
, active(o.active)
, flipped(o.flipped)
, render_player_light(o.render_player_light)
, alpha_percentage(o.alpha_percentage)
, distance_to_player_sq(o.distance_to_player_sq)
, spawn_area_local(o.spawn_area_local)
, base_areas(o.base_areas)
, areas(o.areas)
, children(o.children)
, static_lights(o.static_lights)
, gradient_shadow(o.gradient_shadow)
, depth(o.depth)
, has_shading(o.has_shading)
, dead(o.dead)
, static_frame(o.static_frame)
, cached_w(o.cached_w)
, cached_h(o.cached_h)
, window(o.window)
, highlighted(o.highlighted)
, hidden(o.hidden)
, merged(o.merged)
, selected(o.selected)
, next_animation(o.next_animation)
, current_frame_index(o.current_frame_index)
, frame_progress(o.frame_progress)
, shading_group(o.shading_group)
, shading_group_set(o.shading_group_set)
, final_texture(o.final_texture)
, custom_frames(o.custom_frames)
, assets_(o.assets_)
, spawn_id(o.spawn_id)
, spawn_method(o.spawn_method)
, controller_(nullptr)
{
}

Asset& Asset::operator=(const Asset& o) {
	if (this == &o) return *this;
	parent               = o.parent;
	info                 = o.info;
	current_animation    = o.current_animation;
	pos_X                = o.pos_X;
	pos_Y                = o.pos_Y;
	screen_X             = o.screen_X;
	screen_Y             = o.screen_Y;
	z_index              = o.z_index;
	z_offset             = o.z_offset;
	player_speed         = o.player_speed;
	is_lit               = o.is_lit;
	has_base_shadow      = o.has_base_shadow;
	active               = o.active;
	flipped              = o.flipped;
	render_player_light  = o.render_player_light;
	alpha_percentage     = o.alpha_percentage;
        distance_to_player_sq = o.distance_to_player_sq;
	spawn_area_local     = o.spawn_area_local;
	base_areas           = o.base_areas;
	areas                = o.areas;
	children             = o.children;
	static_lights        = o.static_lights;
	gradient_shadow      = o.gradient_shadow;
	depth                = o.depth;
	has_shading          = o.has_shading;
	dead                 = o.dead;
	static_frame         = o.static_frame;
	cached_w             = o.cached_w;
	cached_h             = o.cached_h;
	window               = o.window;
	highlighted          = o.highlighted;
	hidden               = o.hidden;
	merged               = o.merged;
	selected             = o.selected;
	next_animation       = o.next_animation;
	current_frame_index  = o.current_frame_index;
	frame_progress       = o.frame_progress;
	shading_group        = o.shading_group;
	shading_group_set    = o.shading_group_set;
	final_texture        = o.final_texture;
	custom_frames        = o.custom_frames;
	assets_              = o.assets_;
	spawn_id             = o.spawn_id;
	spawn_method         = o.spawn_method;
	controller_.reset();
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
			anim.change(current_frame_index, static_frame);
			frame_progress = 0.0f;
			if ((anim.randomize || anim.rnd_start) && anim.frames.size() > 1) {
					std::mt19937 rng{ std::random_device{}() };
					std::uniform_int_distribution<int> dist(0, int(anim.frames.size()) - 1);
					current_frame_index = dist(rng);
			}
		}
	}
	for (Asset* child : children)
	if (child) child->finalize_setup();
	if (!children.empty()) {
		std::cout << "[Asset] \"" << (info ? info->name : std::string{"<null>"})
		<< "\" at (" << pos_X << ", " << pos_Y
		<< ") has " << children.size() << " child(ren):\n";
		for (Asset* child : children)
		if (child && child->info)
		std::cout << "    - \"" << child->info->name
		<< "\" at (" << child->pos_X << ", " << child->pos_Y << ")\n";
	}
	if (assets_) {
		ControllerFactory cf(assets_, assets_->activeManager);
		controller_ = cf.create_for_asset(this);
	}
	anim_ = std::make_unique<AnimationManager>(this);
}

bool Asset::get_merge(){ return merged; }

SDL_Texture* Asset::get_current_frame() const {
	auto itc = custom_frames.find(current_animation);
	if (itc != custom_frames.end() && !itc->second.empty())
	return itc->second[current_frame_index];
	auto iti = info->animations.find(current_animation);
	if (iti != info->animations.end())
	return iti->second.get_frame(current_frame_index);
	return nullptr;
}

void Asset::set_position(int x, int y) {
	pos_X = x;
	pos_Y = y;
	set_z_index();
}

void Asset::update() {
	if (!info) return;
	if (controller_ && assets_) {
		if (Input* in = assets_->get_input())
		controller_->update(*in);
	}
	for (Asset* c : children)
	if (c && !c->dead && c->info)
	c->update();
}

void Asset::update_animation_manager() {
	if (anim_) anim_->update();
}

void Asset::change_animation(const std::string& name) {
	if (!info || name.empty()) return;
	if (name == current_animation) return;
	next_animation = name;
}

std::string Asset::get_current_animation() const { return current_animation; }
std::string Asset::get_type() const { return info ? info->type : ""; }

bool Asset::is_current_animation_locked_in_progress() const {
	if (!info) return false;
	auto it = info->animations.find(current_animation);
	if (it == info->animations.end()) return false;
	const Animation& anim = it->second;
	if (!anim.locked) return false;
	int last = static_cast<int>(anim.frames.size()) - 1;
	if (last < 0) return false;
	return current_frame_index != last;
}

bool Asset::is_current_animation_last_frame() const {
	if (!info) return false;
	auto it = info->animations.find(current_animation);
	if (it == info->animations.end()) return false;
	const Animation& anim = it->second;
	int last = static_cast<int>(anim.frames.size()) - 1;
	if (last < 0) return true;
	return current_frame_index >= last;
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
	if (!controller_ && assets_) {
		ControllerFactory cf(assets_, assets_->activeManager);
		controller_ = cf.create_for_asset(this);
	}
}

void Asset::set_z_index() {
        int old_z = z_index;
        try {
                if (parent) {
                        if (z_offset > 0)       z_index = parent->z_index + 1;
                        else if (z_offset < 0)  z_index = parent->z_index - 1;
                        else                    z_index = pos_Y + info->z_threshold;
                } else if (info) {
                        z_index = pos_Y + info->z_threshold;
                }
        } catch (const std::exception& e) {
                std::cerr << "[Asset::set_z_index] Exception: " << e.what() << "\n";
        }
        if (assets_ && z_index != old_z) {
                assets_->activeManager.markNeedsSort();
        }
}

void Asset::set_z_offset(int z) {
	z_offset = z;
	set_z_index();
	std::cout << "Z offset set to " << z << " for asset " << info->name << "\n";
}

void Asset::recompute_z_index() {
	set_z_index();
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

void Asset::set_screen_position(int sx, int sy) {
	screen_X = sx;
	screen_Y = sy;
}

void Asset::add_static_light_source(LightSource* light, int world_x, int world_y, Asset* owner) {
	if (!light) return;
	StaticLight sl;
	sl.source = light;
	sl.offset_x = world_x - pos_X;
	sl.offset_y = world_y - pos_Y;
	sl.alpha_percentage = LightUtils::calculate_static_alpha_percentage(this, owner);
	static_lights.push_back(sl);
}

void Asset::set_render_player_light(bool value) { render_player_light = value; }
bool Asset::get_render_player_light() const { return render_player_light; }

Area Asset::get_area(const std::string& name) const {
	Area result(name);
	if (info) {
		if (name == "clickable") {
			int base_w = (cached_w > 0) ? cached_w : int(info->original_canvas_width * info->scale_factor);
			int base_h = (cached_h > 0) ? cached_h : int(info->original_canvas_height * info->scale_factor);
			if (base_w <= 0) base_w = 1;
			if (base_h <= 0) base_h = 1;
			int click_w = int(base_w * 1.5f);
			int click_h = int(base_h * 1.5f);
			int left    = pos_X - click_w / 2;
			int top     = pos_Y - click_h;
			result = Area(name, left, top, click_w, click_h, "Square", 1, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
		} else {
			Area* a = info->find_area(name + "_area");
			if (a) result = *a;
		}
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
		assets_->activeManager.remove(this);
		assets_->schedule_removal(this);
	}
}
