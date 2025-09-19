#include "camera.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "room/room.hpp"
#include "find_current_room.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <nlohmann/json.hpp>


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
        // No active animation; keep view consistent
        recompute_current_view();
        intro = false;
        return;
    }

    // Advance animation
    ++steps_done_;
    double t = static_cast<double>(steps_done_) / static_cast<double>(std::max(1, steps_total_));
    t = std::clamp(t, 0.0, 1.0);
    double s = start_scale_ + (target_scale_ - start_scale_) * t;
    scale_ = static_cast<float>(std::max(0.0001, s));
    if (pan_override_) {
        const double cx = static_cast<double>(start_center_.x) + (static_cast<double>(target_center_.x) - static_cast<double>(start_center_.x)) * t;
        const double cy = static_cast<double>(start_center_.y) + (static_cast<double>(target_center_.y) - static_cast<double>(start_center_.y)) * t;
        screen_center_ = SDL_Point{ static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy)) };
    }
    recompute_current_view();

    if (steps_done_ >= steps_total_) {
        // Done
        scale_ = static_cast<float>(target_scale_);
        // If we were panning, snap to exact target center at the end
        if (pan_override_) {
            screen_center_ = target_center_;
        }
        zooming_ = false;
        pan_override_ = false;
        steps_total_ = steps_done_ = 0;
        start_scale_ = target_scale_;
    }
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
    if (!pan_override_) {
        if (focus_override_) {
            screen_center_ = focus_point_;
        } else if (player) {
            screen_center_ = SDL_Point{ player->pos.x, player->pos.y };
        } else if (cur && cur->room_area) {
            screen_center_ = cur->room_area->get_center();
        }
    }

    if (!starting_room_ && cur && cur->room_area) {
        starting_room_ = cur;
        Area adjusted = convert_area_to_aspect(*cur->room_area);
        starting_area_ = adjusted.get_size();
        if (starting_area_ <= 0.0) starting_area_ = 1.0;
    }

    update();

    if (!cur) return;
    if (manual_zoom_override_) {
        // Respect manual zoom override: do not compute or apply room-based zoom target
        return;
    }

    Room* neigh = nullptr;
    if (finder) {
        neigh = finder->getNeighboringRoom(cur);
    }
    if (!neigh) neigh = cur;

    const double sa = compute_room_scale_from_area(cur);
    const double sb = compute_room_scale_from_area(neigh);

    double target_zoom = sa;
    if (player && cur && cur->room_area && neigh && neigh->room_area) {
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
        target_zoom = (sa * (1.0 - t)) + (sb * t);
    }

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

void camera::pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps) {
    // Schedule a smooth pan+zoom toward the target point and target scale
    set_focus_override(world_pos); // keep focus afterward
    start_center_  = screen_center_;
    target_center_ = world_pos;
    double factor = (zoom_scale_factor > 0.0) ? zoom_scale_factor : 1.0;
    start_scale_   = scale_;
    target_scale_  = std::max(0.0001, static_cast<double>(scale_) * factor);
    steps_total_   = std::max(1, duration_steps);
    steps_done_    = 0;
    zooming_       = true;
    pan_override_  = true;
    manual_zoom_override_ = true; // prevent room-logic from changing target mid-flight
}

void camera::pan_and_zoom_to_asset(const Asset* a, double zoom_scale_factor, int duration_steps) {
    if (!a) return;
    SDL_Point target{ a->pos.x, a->pos.y };
    pan_and_zoom_to_point(target, zoom_scale_factor, duration_steps);
}

void camera::animate_zoom_multiply(double factor, int duration_steps) {
    if (factor <= 0.0) factor = 1.0;
    start_center_  = screen_center_; // no pan by default
    target_center_ = screen_center_;
    start_scale_   = scale_;
    target_scale_  = std::max(0.0001, static_cast<double>(scale_) * factor);
    steps_total_   = std::max(1, duration_steps);
    steps_done_    = 0;
    zooming_       = true;
    pan_override_  = false;
    manual_zoom_override_ = true;
}

SDL_Point camera::map_to_screen(SDL_Point world, float parallax_x, float parallax_y) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double inv_scale = (scale_ > 0.000001f) ? (1.0 / static_cast<double>(scale_)) : 1e6;
    const int view_w = right - left;
    const int view_h = bottom - top;
    int sx = static_cast<int>(std::lround((static_cast<double>(world.x - left)) * inv_scale));
    int sy = static_cast<int>(std::lround((static_cast<double>(world.y - top)) * inv_scale));
    if (parallax_enabled_ && (parallax_x != 0.0f || parallax_y != 0.0f)) {
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
    if (parallax_enabled_ && (parallax_x != 0.0f || parallax_y != 0.0f)) {
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

camera::RenderEffects camera::compute_render_effects(SDL_Point world,
                                                     float asset_screen_height,
                                                     float reference_screen_height) const {
    RenderEffects result;
    result.screen_position = map_to_screen(world);
    result.vertical_scale  = 1.0f;
    result.distance_scale  = 1.0f;

    const double safe_scale = std::max(0.0001, static_cast<double>(scale_));
    const double world_to_screen = 1.0 / safe_scale;

    if (realism_enabled_) {
        const float raw_scale = std::isfinite(scale_) ? scale_ : 0.0f;
        const float zoom_norm = std::clamp(raw_scale, 0.0f, 1.0f);
        const float base_height = std::isfinite(settings_.height_at_zoom1)
                                      ? std::max(0.0f, settings_.height_at_zoom1)
                                      : 0.0f;
        const double camera_height = static_cast<double>(base_height) * static_cast<double>(zoom_norm);

        const double tripod_distance = std::isfinite(settings_.tripod_distance_y)
                                           ? static_cast<double>(settings_.tripod_distance_y)
                                           : 0.0;
        const double base_x = static_cast<double>(screen_center_.x);
        const double base_y = static_cast<double>(screen_center_.y) - tripod_distance;

        const double dx = static_cast<double>(world.x) - base_x;
        const double dy = static_cast<double>(world.y) - base_y;

        const double r = std::hypot(dx, dy);
        const double d = std::hypot(r, camera_height);

        const double sin_pitch = (d > 0.0) ? (camera_height / d) : 0.0;
        const double cos_pitch = (d > 0.0) ? (r / d) : 1.0;

        const double denom = r + camera_height;
        const double t = (denom > 0.0) ? (r / denom) : 0.0;

        const float parallax_strength = std::max(0.0f, settings_.parallax_strength);
        if (parallax_enabled_ && parallax_strength > 0.0f && d > 0.0) {
            const double parallax_pixels_y =
                static_cast<double>(parallax_strength) * sin_pitch * dy * world_to_screen;
            result.screen_position.y += static_cast<int>(std::lround(parallax_pixels_y));
        }

        const float foreshorten_strength = std::max(0.0f, settings_.foreshorten_strength);
        if (foreshorten_strength > 0.0f) {
            const double foreshorten = static_cast<double>(foreshorten_strength) * t * cos_pitch;
            const double scaled = std::clamp(1.0 - foreshorten, 0.1, 1.0);
            result.vertical_scale = static_cast<float>(scaled);
        }

        const float distance_strength = std::max(0.0f, settings_.distance_scale_strength);
        if (distance_strength > 0.0f) {
            const double bias = 0.5 - t;
            const double scale = 1.0 + static_cast<double>(distance_strength) * cos_pitch * bias;
            const double clamped_scale = std::clamp(scale, 0.35, 2.0);
            result.distance_scale = static_cast<float>(clamped_scale);
        }
    }

    if (settings_.camera_vertical_offset != 0.0f) {
        const double offset_pixels =
            static_cast<double>(settings_.camera_vertical_offset) * world_to_screen;
        result.screen_position.y += static_cast<int>(std::lround(offset_pixels));
    }

    (void)asset_screen_height;
    (void)reference_screen_height;

    return result;
}


void camera::apply_camera_settings(const nlohmann::json& data) {
    if (!data.is_object()) {
        return;
    }

    const auto try_read_float = [&](const char* key, float& target) -> bool {
        auto it = data.find(key);
        if (it == data.end()) return false;
        if (it->is_number_float()) {
            target = static_cast<float>(it->get<double>());
            return true;
        }
        if (it->is_number_integer()) {
            target = static_cast<float>(it->get<int>());
            return true;
        }
        return false;
    };

    auto realism_it = data.find("realism_enabled");
    if (realism_it != data.end()) {
        if (realism_it->is_boolean()) {
            realism_enabled_ = realism_it->get<bool>();
        } else if (realism_it->is_number_integer()) {
            realism_enabled_ = realism_it->get<int>() != 0;
        }
    }

    bool has_render_distance = try_read_float("render_distance", settings_.render_distance);
    if (!has_render_distance) {
        float legacy_factor = 0.0f;
        if (try_read_float("render_distance_factor", legacy_factor)) {
            settings_.render_distance = std::max(0.0f, legacy_factor * 200.0f);
        }
    }

    bool has_parallax = try_read_float("parallax_strength", settings_.parallax_strength);
    if (!has_parallax) {
        float legacy_parallax = 0.0f;
        if (try_read_float("parallax_vertical_strength", legacy_parallax) ||
            try_read_float("parallax_horizontal_strength", legacy_parallax)) {
            settings_.parallax_strength = std::max(0.0f, std::abs(legacy_parallax));
        }
    }

    bool has_foreshorten = try_read_float("foreshorten_strength", settings_.foreshorten_strength);
    if (!has_foreshorten) {
        float legacy_squash = 0.0f;
        if (try_read_float("squash_strength", legacy_squash) ||
            try_read_float("squash_overall_strength", legacy_squash)) {
            settings_.foreshorten_strength = std::max(0.0f, legacy_squash);
        }
    }

    try_read_float("distance_scale_strength", settings_.distance_scale_strength);

    bool has_height = try_read_float("height_at_zoom1", settings_.height_at_zoom1);
    if (!has_height) {
        float legacy_height = 0.0f;
        if (try_read_float("camera_height_at_zoom0", legacy_height)) {
            settings_.height_at_zoom1 = std::max(0.0f, legacy_height);
        }
    }

    try_read_float("tripod_distance_y", settings_.tripod_distance_y);
    try_read_float("camera_vertical_offset", settings_.camera_vertical_offset);

    if (!std::isfinite(settings_.render_distance) || settings_.render_distance < 0.0f) {
        settings_.render_distance = 800.0f;
    } else {
        settings_.render_distance = std::max(0.0f, settings_.render_distance);
    }

    settings_.parallax_strength = std::isfinite(settings_.parallax_strength)
        ? std::max(0.0f, settings_.parallax_strength)
        : 0.0f;

    settings_.foreshorten_strength = std::isfinite(settings_.foreshorten_strength)
        ? std::max(0.0f, settings_.foreshorten_strength)
        : 0.0f;

    settings_.distance_scale_strength = std::isfinite(settings_.distance_scale_strength)
        ? std::max(0.0f, settings_.distance_scale_strength)
        : 0.0f;

    if (!std::isfinite(settings_.height_at_zoom1) || settings_.height_at_zoom1 < 0.0f) {
        settings_.height_at_zoom1 = 18.0f;
    }

    if (!std::isfinite(settings_.tripod_distance_y)) {
        settings_.tripod_distance_y = 0.0f;
    } else {
        settings_.tripod_distance_y = std::clamp(settings_.tripod_distance_y, -2000.0f, 2000.0f);
    }

    if (!std::isfinite(settings_.camera_vertical_offset)) {
        settings_.camera_vertical_offset = 0.0f;
    }
}


nlohmann::json camera::camera_settings_to_json() const {
    nlohmann::json j = nlohmann::json::object();
    j["realism_enabled"] = realism_enabled_;
    j["render_distance"] = settings_.render_distance;
    j["parallax_strength"] = settings_.parallax_strength;
    j["foreshorten_strength"] = settings_.foreshorten_strength;
    j["distance_scale_strength"] = settings_.distance_scale_strength;
    j["height_at_zoom1"] = settings_.height_at_zoom1;
    j["tripod_distance_y"] = settings_.tripod_distance_y;
    j["camera_vertical_offset"] = settings_.camera_vertical_offset;
    j["render_distance_factor"] = settings_.render_distance / 200.0f;
    return j;
}


int camera::get_render_distance_world_margin() const {
    const double margin = std::max(0.0, static_cast<double>(settings_.render_distance));
    return static_cast<int>(std::lround(margin));
}

