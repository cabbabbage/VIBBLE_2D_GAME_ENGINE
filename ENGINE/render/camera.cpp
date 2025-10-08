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
    Area adjusted_start = convert_area_to_aspect(starting_zoom);
    SDL_Point start_center = adjusted_start.get_center();
    base_zoom_    = make_rect_area("base_zoom", start_center, screen_width_, screen_height_);
    current_view_ = adjusted_start;
    screen_center_ = start_center;
    screen_center_initialized_ = true;
    pan_offset_x_ = 0.0;
    pan_offset_y_ = 0.0;
    const int base_w = width_from_area(base_zoom_);
    const int curr_w = width_from_area(current_view_);
    scale_ = (base_w > 0) ? static_cast<float>(static_cast<double>(curr_w) / static_cast<double>(base_w)) : 1.0f;
    zooming_ = false;
    steps_total_ = steps_done_ = 0;
    start_scale_ = target_scale_ = scale_;
}

void camera::set_screen_center(SDL_Point p) {
    if (!screen_center_initialized_) {
        screen_center_ = p;
        screen_center_initialized_ = true;
        pan_offset_x_ = 0.0;
        pan_offset_y_ = 0.0;
        return;
    }
    const double dx = static_cast<double>(p.x) - static_cast<double>(screen_center_.x);
    const double dy = static_cast<double>(p.y) - static_cast<double>(screen_center_.y);
    pan_offset_x_ += dx;
    pan_offset_y_ += dy;
    screen_center_ = p;
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
        recompute_current_view();
        intro = false;
        return;
    }
    ++steps_done_;
    double t = static_cast<double>(steps_done_) / static_cast<double>(std::max(1, steps_total_));
    t = std::clamp(t, 0.0, 1.0);
    double s = start_scale_ + (target_scale_ - start_scale_) * t;
    scale_ = static_cast<float>(std::max(0.0001, s));
    if (pan_override_) {
        const double cx = static_cast<double>(start_center_.x) + (static_cast<double>(target_center_.x) - static_cast<double>(start_center_.x)) * t;
        const double cy = static_cast<double>(start_center_.y) + (static_cast<double>(target_center_.y) - static_cast<double>(start_center_.y)) * t;
        SDL_Point new_center{ static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy)) };
        set_screen_center(new_center);
    }
    recompute_current_view();
    if (steps_done_ >= steps_total_) {
        scale_ = static_cast<float>(target_scale_);
        if (pan_override_) {
            set_screen_center(target_center_);
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
    pan_offset_x_ = 0.0;
    pan_offset_y_ = 0.0;
    if (!pan_override_) {
        if (focus_override_) {
            set_screen_center(focus_point_);
        } else if (player) {
            set_screen_center(SDL_Point{ player->pos.x, player->pos.y });
        } else if (cur && cur->room_area) {
            set_screen_center(cur->room_area->get_center());
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

Area camera::convert_area_to_aspect(const Area& in) const {
    auto [minx, miny, maxx, maxy] = in.get_bounds();
    int w = std::max(1, maxx - minx);
    int h = std::max(1, maxy - miny);
    SDL_Point c = in.get_center();
    const double cur = static_cast<double>(w) / static_cast<double>(h);
    int target_w = w;
    int target_h = h;
    if (cur < aspect_) {
        target_w = static_cast<int>(std::lround(static_cast<double>(h) * aspect_));
    } else if (cur > aspect_) {
        target_h = static_cast<int>(std::lround(static_cast<double>(w) / aspect_));
    }
    return make_rect_area("adjusted_" + in.get_name(), c, target_w, target_h);
}

void camera::recompute_current_view() {
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int base_h = std::max(1, height_from_area(base_zoom_));
    const int cur_w  = static_cast<int>(std::lround(static_cast<double>(base_w) * std::max(0.0001, static_cast<double>(scale_))));
    const int cur_h  = static_cast<int>(std::lround(static_cast<double>(base_h) * std::max(0.0001, static_cast<double>(scale_))));
    current_view_    = make_rect_area("current_view", screen_center_, cur_w, cur_h);
}

void camera::pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps) {
    set_focus_override(world_pos);
    start_center_  = screen_center_;
    target_center_ = world_pos;
    double factor = (zoom_scale_factor > 0.0) ? zoom_scale_factor : 1.0;
    start_scale_   = scale_;
    target_scale_  = std::max(0.0001, static_cast<double>(scale_) * factor);
    steps_total_   = std::max(1, duration_steps);
    steps_done_    = 0;
    zooming_       = true;
    pan_override_  = true;
    manual_zoom_override_ = true;
}

void camera::pan_and_zoom_to_asset(const Asset* a, double zoom_scale_factor, int duration_steps) {
    if (!a) return;
    SDL_Point target{ a->pos.x, a->pos.y };
    pan_and_zoom_to_point(target, zoom_scale_factor, duration_steps);
}

void camera::animate_zoom_multiply(double factor, int duration_steps) {
    if (factor <= 0.0) factor = 1.0;
    start_center_  = screen_center_;
    target_center_ = screen_center_;
    start_scale_   = scale_;
    target_scale_  = std::max(0.0001, static_cast<double>(scale_) * factor);
    steps_total_   = std::max(1, duration_steps);
    steps_done_    = 0;
    zooming_       = true;
    pan_override_  = false;
    manual_zoom_override_ = true;
}

SDL_Point camera::map_to_screen(SDL_Point world, float, float) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double inv_scale = (scale_ > 0.000001f) ? (1.0 / static_cast<double>(scale_)) : 1e6;
    int sx = static_cast<int>(std::lround((static_cast<double>(world.x - left)) * inv_scale));
    int sy = static_cast<int>(std::lround((static_cast<double>(world.y - top)) * inv_scale));
    return SDL_Point{ sx, sy };
}

SDL_Point camera::screen_to_map(SDL_Point screen, float, float) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double s = static_cast<double>(std::max(0.000001f, scale_));
    double wx = static_cast<double>(left) + static_cast<double>(screen.x) * s;
    double wy = static_cast<double>(top)  + static_cast<double>(screen.y) * s;
    return SDL_Point{ static_cast<int>(std::lround(wx)), static_cast<int>(std::lround(wy)) };
}

camera::RenderEffects camera::compute_render_effects(
    SDL_Point world,
    float asset_screen_height,
    float reference_screen_height) const
{
    RenderEffects result;
    result.screen_position = map_to_screen(world);
    result.vertical_scale  = 1.0f;
    result.distance_scale  = 1.0f;

    const double safe_scale       = std::max(1e-6, static_cast<double>(scale_));
    const double pixels_per_world = 1.0 / safe_scale;

    if (!realism_enabled_) {
        return result;
    }

    // --- Tunable constants ---
    constexpr double EPS              = 1e-6;
    constexpr double SY               = 200.0;
    constexpr double PARALLAX_KV      = 0.25;
    constexpr double PARALLAX_STEEPEN = 1.5;
    constexpr double PARALLAX_MAX     = 4000.0;
    constexpr double SQUASH_HEIGHT_WT = 0.3;   // weight of height-based squash
    constexpr double SQUASH_BASE_WT   = 1.0 - SQUASH_HEIGHT_WT;
    constexpr double ZOOM_ATTEN_WT    = 0.8;
    constexpr double DIST_EXPONENT    = 3;   // modulation strength by squash
    constexpr double DIST_MIN         = 0.3;
    constexpr double DIST_MAX         = 1.3;
    constexpr double DY_WEIGHT        = 1.2;
    constexpr double RANGE_COMPRESS   = 2.0;
    constexpr double R_REF            = 400.0;

    // --- Camera setup ---
    const double raw_scale      = std::isfinite(scale_) ? static_cast<double>(scale_) : 0.0;
    const double zoom_norm      = std::clamp(raw_scale, 0.0, 1.0);
    const double height_at_zoom1 = std::isfinite(settings_.height_at_zoom1)
                                       ? std::max(0.0f, settings_.height_at_zoom1)
                                       : 0.0f;
    const double camera_height  = height_at_zoom1 * zoom_norm;

    const double tripod_distance = std::isfinite(settings_.tripod_distance_y)
                                        ? static_cast<double>(settings_.tripod_distance_y)
                                        : 0.0;

    const double base_x = static_cast<double>(screen_center_.x);
    const double base_y = static_cast<double>(screen_center_.y) - tripod_distance;

    const double dx = static_cast<double>(world.x) - base_x;
    const double dy = static_cast<double>(world.y) - base_y;
    const double r  = std::hypot(dx, dy);

    const double zoom_attenuation = (camera_height > EPS)
        ? camera_height / (camera_height + height_at_zoom1 + EPS)
        : 1.0;

    const double screen_bias = 0.5 + 0.5 * std::tanh(dy / SY);

    // --- Parallax ---
    if (parallax_enabled_) {
        const double parallax_strength = std::max(0.0f, settings_.parallax_strength);
        if (parallax_strength > 0.0 && camera_height > EPS) {
            const int view_height = height_from_area(current_view_);
            const int view_width  = width_from_area(current_view_);

            const double ndy = dy / (view_height * 0.5);
            const double ndx = dx / (view_width  * 0.5);

            const double vertical_bias = 1.0 + PARALLAX_KV *
                                         std::tanh(ndy * (view_height / SY) * PARALLAX_STEEPEN);

            double zoom_gain = (height_at_zoom1 > EPS)
                                ? (height_at_zoom1 / (camera_height + EPS))
                                : 1.0;
            if (zoom_gain >= 1.0) {
                zoom_gain = std::pow(zoom_gain, 1.5);
            }

            double parallax_px = parallax_strength *
                                 ndx * ndy *
                                 pixels_per_world * vertical_bias * zoom_gain;

            parallax_px = std::clamp(parallax_px, -PARALLAX_MAX, PARALLAX_MAX);
            result.screen_position.x += static_cast<int>(std::lround(parallax_px));
        }
    }

    // --- Foreshortening ---
    {
        const double foreshorten_strength = std::max(0.0f, settings_.foreshorten_strength);
        if (foreshorten_strength > 0.0 && camera_height > EPS) {
            const double ref_h = (reference_screen_height > EPS) ? reference_screen_height : 1.0;

            const double squash_base   = foreshorten_strength * screen_bias *
                                         (zoom_attenuation * ZOOM_ATTEN_WT);
            const double height_factor = std::sqrt(static_cast<double>(asset_screen_height) / ref_h);
            const double squash_height = squash_base * height_factor;

            const double squash = SQUASH_BASE_WT * squash_base +
                                  SQUASH_HEIGHT_WT * squash_height;

            const double new_vertical_scale = std::clamp(1.0 - squash, 0.1, 1.0);
            result.vertical_scale = static_cast<float>(new_vertical_scale);
        }
    }

    // --- Distance scaling ---
    {
        const double distance_strength = std::max(0.0f, settings_.distance_scale_strength);
        if (distance_strength > 0.0) {
            const double r_weighted   = std::hypot(dx, dy * DY_WEIGHT);
            const double r_normalized = r_weighted / RANGE_COMPRESS;

            const double base_scale = std::sqrt(
                (camera_height + R_REF) / (camera_height + r_normalized + EPS)
            );

            double distance_scale = 1.0 + (base_scale - 1.0) * distance_strength;

            const double squash_factor = static_cast<double>(result.vertical_scale);
            distance_scale = 1.0 + (distance_scale - 1.0) *
                             std::pow(squash_factor, DIST_EXPONENT);

            distance_scale = std::clamp(distance_scale, DIST_MIN, DIST_MAX);
            result.distance_scale = static_cast<float>(distance_scale);
        }
    }

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

    try_read_float("render_distance", settings_.render_distance);
    try_read_float("parallax_strength", settings_.parallax_strength);
    try_read_float("foreshorten_strength", settings_.foreshorten_strength);
    try_read_float("distance_scale_strength", settings_.distance_scale_strength);
    try_read_float("height_at_zoom1", settings_.height_at_zoom1);
    try_read_float("tripod_distance_y", settings_.tripod_distance_y);

    if (!std::isfinite(settings_.render_distance) || settings_.render_distance < 0.0f) {
        settings_.render_distance = 800.0f;
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
}

nlohmann::json camera::camera_settings_to_json() const {
    nlohmann::json j = nlohmann::json::object();
    j["realism_enabled"]       = realism_enabled_;
    j["render_distance"]       = settings_.render_distance;
    j["parallax_strength"]     = settings_.parallax_strength;
    j["foreshorten_strength"]  = settings_.foreshorten_strength;
    j["distance_scale_strength"] = settings_.distance_scale_strength;
    j["height_at_zoom1"]       = settings_.height_at_zoom1;
    j["tripod_distance_y"]     = settings_.tripod_distance_y;
    return j;
}

int camera::get_render_distance_world_margin() const {
    const double margin = std::max(0.0, static_cast<double>(settings_.render_distance));
    return static_cast<int>(std::lround(margin));
}
