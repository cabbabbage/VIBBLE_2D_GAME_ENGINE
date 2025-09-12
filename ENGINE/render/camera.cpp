#include "camera.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "room/room.hpp"
#include "find_current_room.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

static inline int width_from_area(const Area& a) {
    int minx, miny, maxx, maxy;
    std::tie(minx, miny, maxx, maxy) = a.get_bounds();
    (void)miny; (void)maxy;
    return std::max(0, maxx - minx);
}

static inline int height_from_area(const Area& a) {
    int minx, miny, maxx, maxy;
    std::tie(minx, miny, maxx, maxy) = a.get_bounds();
    (void)minx; (void)maxx;
    return std::max(0, maxy - miny);
}

static inline Area make_rect_area(const std::string& name, SDL_Point center, int w, int h) {
    const int left   = center.x - (w / 2);
    const int top    = center.y - (h / 2);
    const int right  = left + w;
    const int bottom = top + h;
    std::vector<Area::Point> corners{
        { left,  top    },
        { right, top    },
        { right, bottom },
        { left,  bottom }
    };
    return Area(name, corners);
}

camera::camera(int screen_width, int screen_height, const Area& starting_zoom)
{
    screen_width_  = screen_width;
    screen_height_ = screen_height;
    aspect_        = (screen_height_ > 0) ? static_cast<double>(screen_width_) / static_cast<double>(screen_height_) : 1.0;

    // Base zoom is exactly the screen size (zoom=1), centered on the starting area center.
    Area adjusted_start = convert_area_to_aspect(starting_zoom);
    SDL_Point start_center = adjusted_start.get_center();
    base_zoom_    = make_rect_area("base_zoom", start_center, screen_width_, screen_height_);

    // Current view starts at the adjusted starting area.
    current_view_ = adjusted_start;
    screen_center_ = start_center;

    const int base_w = width_from_area(base_zoom_);
    const int curr_w = width_from_area(current_view_);
    scale_ = (base_w > 0) ? static_cast<float>(static_cast<double>(curr_w) / static_cast<double>(base_w)) : 1.0f;
    zooming_ = false;
    steps_total_ = steps_done_ = 0;
    start_scale_ = target_scale_ = scale_;
}

void camera::set_scale(float s) {
	scale_ = (s > 0.0f) ? s : 0.0001f;
	zooming_ = false;
	steps_total_ = steps_done_ = 0;
	start_scale_ = target_scale_ = scale_;
}

float camera::get_scale() const { return scale_; }

void camera::zoom_to_scale(double target_scale, int duration_steps) {
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

void camera::zoom_to_area(const Area& target_area, int duration_steps) {
    Area adjusted = convert_area_to_aspect(target_area);
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int tgt_w  = std::max(1, width_from_area(adjusted));
    const double target = static_cast<double>(tgt_w) / static_cast<double>(base_w);
    zoom_to_scale(target, duration_steps);
}

void camera::update() {
    if (!zooming_) {
        // Still keep current_view centered if center changes externally
        recompute_current_view();
        intro = false;
        return;
    }
    ++steps_done_;
    if (steps_done_ >= steps_total_) {
        scale_ = static_cast<float>(target_scale_);
        zooming_ = false;
        steps_total_ = steps_done_ = 0;
        start_scale_ = target_scale_;
        recompute_current_view();
        return;
    }
    double t = static_cast<double>(steps_done_) / static_cast<double>(steps_total_);
    double s = start_scale_ + (target_scale_ - start_scale_) * t;
    scale_ = static_cast<float>(std::max(0.0001, s));
    recompute_current_view();
}

namespace {
	static constexpr double BASE_RATIO = 1.1;
}

double camera::compute_room_scale_from_area(const Room* room) const {
	if (!room || !room->room_area || starting_area_ <= 0.0) return BASE_RATIO;
	// Adjust to screen aspect ratio before computing area
	Area adjusted = convert_area_to_aspect(*room->room_area);
	double a = adjusted.get_size();
	if (a <= 0.0 || room->type == "trail") return BASE_RATIO * 0.8;
	double s = (a / starting_area_) * BASE_RATIO;
	s = std::clamp(s, BASE_RATIO * 0.9, BASE_RATIO * 1.05);
	return s;
}

void camera::set_up_rooms(CurrentRoomFinder* finder) {
	if (!finder) return;
	Room* current = finder->getCurrentRoom();
	if (!current) return;
	starting_room_ = current;
	if (starting_room_ && starting_room_->room_area) {
		Area adjusted = convert_area_to_aspect(*starting_room_->room_area);
		starting_area_ = adjusted.get_size();
		if (starting_area_ <= 0.0) starting_area_ = 1.0;
	}
}

void camera::update_zoom(Room* cur, CurrentRoomFinder* finder, Asset* player) {
	if (!player || !finder || !starting_room_) return;
	// Keep camera centered on player for now
	screen_center_ = SDL_Point{ player->pos.x, player->pos.y };
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
	zoom_to_scale(target_zoom, 35);
}

// Aspect conversion (cover)
Area camera::convert_area_to_aspect(const Area& in) const {
    auto [minx, miny, maxx, maxy] = in.get_bounds();
    int w = std::max(1, maxx - minx);
    int h = std::max(1, maxy - miny);
    SDL_Point c = in.get_center();

    const double cur = static_cast<double>(w) / static_cast<double>(h);
    int target_w = w;
    int target_h = h;
    if (cur < aspect_) {
        // too tall/narrow: expand width
        target_w = static_cast<int>(std::lround(static_cast<double>(h) * aspect_));
    } else if (cur > aspect_) {
        // too wide/short: expand height
        target_h = static_cast<int>(std::lround(static_cast<double>(w) / aspect_));
    }
    return make_rect_area("adjusted_" + in.get_name(), c, target_w, target_h);
}

void camera::recompute_current_view() {
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int base_h = std::max(1, height_from_area(base_zoom_));
    const int cur_w  = static_cast<int>(std::lround(static_cast<double>(base_w) * std::max(0.0001, static_cast<double>(scale_))))
                     ;
    const int cur_h  = static_cast<int>(std::lround(static_cast<double>(base_h) * std::max(0.0001, static_cast<double>(scale_))))
                     ;
    current_view_    = make_rect_area("current_view", screen_center_, cur_w, cur_h);
}

SDL_Point camera::map_to_screen(SDL_Point world, float parallax_x, float parallax_y) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double inv_scale = (scale_ > 0.000001f) ? (1.0 / static_cast<double>(scale_)) : 1e6;
    const int view_w = right - left;
    const int view_h = bottom - top;
    int sx = static_cast<int>(std::lround((static_cast<double>(world.x - left)) * inv_scale));
    int sy = static_cast<int>(std::lround((static_cast<double>(world.y - top)) * inv_scale));
    if (parallax_x != 0.0f || parallax_y != 0.0f) {
        const double half_w_world = std::max(1.0, static_cast<double>(view_w) * 0.5);
        const double half_h_world = std::max(1.0, static_cast<double>(view_h) * 0.5);
        const double ndx = (static_cast<double>(world.x - screen_center_.x)) / half_w_world; // [-inf..inf], usually ~[-1..1]
        const double ndy = (static_cast<double>(world.y - screen_center_.y)) / half_h_world;
        sx += static_cast<int>(std::lround(static_cast<double>(parallax_x) * ndx));
        sy += static_cast<int>(std::lround(static_cast<double>(parallax_y) * ndy));
    }
    return SDL_Point{ sx, sy };
}

SDL_Point camera::screen_to_map(SDL_Point screen, float parallax_x, float parallax_y) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double s = static_cast<double>(std::max(0.000001f, scale_));
    // Initial estimate without parallax
    double wx = static_cast<double>(left) + static_cast<double>(screen.x) * s;
    double wy = static_cast<double>(top)  + static_cast<double>(screen.y) * s;
    if (parallax_x != 0.0f || parallax_y != 0.0f) {
        const int view_w = right - left;
        const int view_h = bottom - top;
        const double half_w_world = std::max(1.0, static_cast<double>(view_w) * 0.5);
        const double half_h_world = std::max(1.0, static_cast<double>(view_h) * 0.5);
        const double ndx0 = (wx - static_cast<double>(screen_center_.x)) / half_w_world;
        const double ndy0 = (wy - static_cast<double>(screen_center_.y)) / half_h_world;
        // Correct screen by subtracting estimated parallax offset, then remap
        const double corr_sx = static_cast<double>(screen.x) - static_cast<double>(parallax_x) * ndx0;
        const double corr_sy = static_cast<double>(screen.y) - static_cast<double>(parallax_y) * ndy0;
        wx = static_cast<double>(left) + corr_sx * s;
        wy = static_cast<double>(top)  + corr_sy * s;
    }
    return SDL_Point{ static_cast<int>(std::lround(wx)), static_cast<int>(std::lround(wy)) };
}
