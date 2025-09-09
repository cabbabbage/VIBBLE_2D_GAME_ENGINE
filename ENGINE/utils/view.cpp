#include "view.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "room/room.hpp"
#include "find_current_room.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
view::view(int screen_width, int screen_height, const Bounds& starting_bounds)
{
	current_bounds_ = starting_bounds;
	base_bounds_.left   = -screen_width  ;
	base_bounds_.right  =  screen_width  ;
	base_bounds_.top    = -screen_height ;
	base_bounds_.bottom =  screen_height ;
	int extra_w = static_cast<int>((base_bounds_.right - base_bounds_.left) * 1.0f / 2.0f);
	int extra_h = static_cast<int>((base_bounds_.bottom - base_bounds_.top) * 1.0f / 2.0f);
	base_bounds_.left   -= extra_w;
	base_bounds_.right  += extra_w;
	base_bounds_.top    -= extra_h;
	base_bounds_.bottom += extra_h + 100;
	int base_w = base_bounds_.right - base_bounds_.left;
	int curr_w = current_bounds_.right - current_bounds_.left;
	scale_ = (base_w != 0) ? static_cast<float>(curr_w) / static_cast<float>(base_w) : 1.0f;
	zooming_ = false;
	steps_total_ = steps_done_ = 0;
	start_scale_ = target_scale_ = scale_;
}

void view::set_scale(float s) {
	scale_ = (s > 0.0f) ? s : 0.0001f;
	zooming_ = false;
	steps_total_ = steps_done_ = 0;
	start_scale_ = target_scale_ = scale_;
}

float view::get_scale() const {
	return scale_;
}

view::Bounds view::get_base_bounds() const {
	return base_bounds_;
}

view::Bounds view::get_current_bounds() const {
	Bounds b;
	b.left   = static_cast<int>(std::round(base_bounds_.left   * scale_));
	b.right  = static_cast<int>(std::round(base_bounds_.right  * scale_));
	b.top    = static_cast<int>(std::round(base_bounds_.top    * scale_));
	b.bottom = static_cast<int>(std::round(base_bounds_.bottom * scale_));
	return b;
}

SDL_Rect view::to_world_rect(int cx, int cy) const {
	Bounds b = get_current_bounds();
	int x = cx + b.left;
	int y = cy + b.top;
	int w = (b.right - b.left);
	int h = (b.bottom - b.top);
	return SDL_Rect{ x, y, w, h };
}

Area view::get_view_area(int cx, int cy) const {
	Bounds b = get_current_bounds();
	const int vx = cx + b.left;
	const int vy = cy + b.top;
	const int vw = (b.right - b.left);
	const int vh = (b.bottom - b.top);
	std::vector<Area::Point> corners{
		{vx, vy},
		{vx + vw, vy},
		{vx + vw, vy + vh},
		{vx, vy + vh}
	};
	return Area("view_bounds", corners);
}

bool view::is_point_in_bounds(int x, int y, int cx, int cy) const {
	Area view_area = get_view_area(cx, cy);
	return view_area.contains_point({x, y});
}

void view::zoom_scale(double target_scale, int duration_steps) {
	double clamped = (target_scale > 0.0) ? target_scale : 0.0001;
	if (duration_steps <= 0) {
		set_scale(static_cast<float>(clamped));
		return;
	}
	start_scale_  = scale_;
	target_scale_ = clamped;
	steps_total_  = duration_steps;
	steps_done_   = 0;
	zooming_      = true;
}

void view::zoom_bounds(const Bounds& target_bounds, int duration_steps) {
	const int base_w = base_bounds_.right - base_bounds_.left;
	const int base_h = base_bounds_.bottom - base_bounds_.top;
	const int tgt_w  = target_bounds.right - target_bounds.left;
	const int tgt_h  = target_bounds.bottom - target_bounds.top;
	double sx = base_w != 0 ? static_cast<double>(tgt_w) / static_cast<double>(base_w) : 1.0;
	double sy = base_h != 0 ? static_cast<double>(tgt_h) / static_cast<double>(base_h) : 1.0;
	double target = sx;
	if (std::abs(sx - sy) > 0.001) {
		target = (sx + sy) * 0.5;
	}
	zoom_scale(target, duration_steps);
}

void view::update() {
	if (!zooming_) {
		intro = false;
		return;}
	++steps_done_;
	if (steps_done_ >= steps_total_) {
		scale_ = static_cast<float>(target_scale_);
		zooming_ = false;
		steps_total_ = steps_done_ = 0;
		start_scale_ = target_scale_;
		return;
	}
	double t = static_cast<double>(steps_done_) / static_cast<double>(steps_total_);
	double s = start_scale_ + (target_scale_ - start_scale_) * t;
	scale_ = static_cast<float>(std::max(0.0001, s));
}

namespace {
	static constexpr double BASE_RATIO = 1.1;
}

double view::compute_room_scale_from_area(const Room* room) const {
	if (!room || !room->room_area || starting_area_ <= 0.0) return BASE_RATIO;
	double a = room->room_area->get_size();
	if (a <= 0.0 || room->type == "trail") return BASE_RATIO * 0.8;
	double s = (a / starting_area_) * BASE_RATIO;
	s = std::clamp(s, BASE_RATIO * 0.9, BASE_RATIO * 1.05);
	return s;
}

void view::set_up_rooms(CurrentRoomFinder* finder) {
	if (!finder) return;
	Room* current = finder->getCurrentRoom();
	if (!current) return;
	starting_room_ = current;
	if (starting_room_ && starting_room_->room_area) {
		starting_area_ = starting_room_->room_area->get_size();
		if (starting_area_ <= 0.0) starting_area_ = 1.0;
	}
}

void view::update_zoom(Room* cur, CurrentRoomFinder* finder, Asset* player) {
	if (!player || !finder || !starting_room_) return;
	update();
	if (!cur) return;
	Room* neigh = finder->getNeighboringRoom(cur);
	if (!neigh) neigh = cur;
	const double sa = compute_room_scale_from_area(cur);
	const double sb = compute_room_scale_from_area(neigh);
	auto [ax, ay] = cur->room_area->get_center();
	auto [bx, by] = neigh->room_area->get_center();
	const double pax = double(player->pos.x);
	const double pay = double(player->pos.y);
	const double vx = double(bx - ax);
	const double vy = double(by - ay);
	const double wx = double(pax - ax);
	const double wy = double(pay - ay);
	const double vlen2 = vx * vx + vy * vy;
	double t = (vlen2 > 0.0) ? ((wx * vx + wy * vy) / vlen2) : 0.0;
	t = std::clamp(t, 0.0, 1.0);
	double target_zoom = (sa * (1.0 - t)) + (sb * t);
	target_zoom = std::clamp(target_zoom, BASE_RATIO * 0.7, BASE_RATIO * 1.3);
	zoom_scale(target_zoom, 35);
}
