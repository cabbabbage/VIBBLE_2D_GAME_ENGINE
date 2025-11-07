#include "camera.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "map_generation/room.hpp"
#include "core/find_current_room.hpp"
#include "utils/transform_smoothing_settings.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <nlohmann/json.hpp>

namespace {
    constexpr float kDefaultSmoothingDt = 1.0f / 60.0f;

    constexpr float kMinTau = 1e-4f;

    TransformSmoothingParams sanitize_params(const TransformSmoothingParams& params) {
        TransformSmoothingParams out = params;
        if (!std::isfinite(out.lerp_rate) || out.lerp_rate < 0.0f) {
            out.lerp_rate = 0.0f;
        }
        if (!std::isfinite(out.spring_frequency) || out.spring_frequency < 0.0f) {
            out.spring_frequency = 0.0f;
        }
        if (!std::isfinite(out.max_step) || out.max_step < 0.0f) {
            out.max_step = 0.0f;
        }
        if (!std::isfinite(out.snap_threshold) || out.snap_threshold < 0.0f) {
            out.snap_threshold = 0.0f;
        }
        switch (out.method) {
        case TransformSmoothingMethod::None:
        case TransformSmoothingMethod::Lerp:
        case TransformSmoothingMethod::CriticallyDampedSpring:
            break;
        default:
            out.method = TransformSmoothingMethod::None;
            break;
        }
        return out;
    }

    float rate_from_tau(float tau_seconds) {
        if (!std::isfinite(tau_seconds) || tau_seconds <= kMinTau) {
            return 0.0f;
        }
        return 1.0f / tau_seconds;
    }

    float tau_from_rate(float rate) {
        if (!std::isfinite(rate) || rate <= kMinTau) {
            return 0.0f;
        }
        return 1.0f / rate;
    }

    TransformSmoothingParams motion_params_from_settings(const camera::RealismSettings& settings) {
        TransformSmoothingParams params{};
        if (!settings.smooth_motion_zoom) {
            params.method = TransformSmoothingMethod::None;
            return sanitize_params(params);
        }

        params.method = settings.motion_smoothing_method;
        switch (params.method) {
        case TransformSmoothingMethod::Lerp:
            params.lerp_rate        = rate_from_tau(std::max(settings.motion_smoothing_tau, 0.0f));
            params.spring_frequency = 0.0f;
            break;
        case TransformSmoothingMethod::CriticallyDampedSpring:
            params.spring_frequency = std::max(0.0f, settings.motion_smoothing_spring_frequency);
            params.lerp_rate        = 0.0f;
            break;
        case TransformSmoothingMethod::None:
            params.method = TransformSmoothingMethod::None;
            params.lerp_rate = params.spring_frequency = 0.0f;
            break;
        }

        params.max_step       = std::max(0.0f, settings.motion_smoothing_max_step);
        params.snap_threshold = std::max(0.0f, settings.motion_smoothing_snap_threshold);
        return sanitize_params(params);
    }
}

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

static inline Area make_rect_area(const std::string& name, SDL_Point center, int w, int h, int resolution) {
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
    return Area(name, corners, resolution);
}

namespace {
    static constexpr double SCALE_EPS  = 1e-4;
    static constexpr double BASE_RATIO = 1.1;
}

camera::camera(int screen_width, int screen_height, const Area& starting_zoom)
{
    screen_width_  = screen_width;
    screen_height_ = screen_height;
    aspect_        = (screen_height_ > 0) ? static_cast<double>(screen_width_) / static_cast<double>(screen_height_) : 1.0;
    Area adjusted_start = convert_area_to_aspect(starting_zoom);
    SDL_Point start_center = adjusted_start.get_center();
    base_zoom_    = make_rect_area("base_zoom", start_center, screen_width_, screen_height_, adjusted_start.resolution());
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

    TransformSmoothingParams center_defaults = transform_smoothing::camera_center_params();
    TransformSmoothingParams zoom_defaults   = transform_smoothing::camera_zoom_params();
    center_smoothing_x_.set_params(center_defaults);
    center_smoothing_y_.set_params(center_defaults);
    zoom_smoothing_.set_params(zoom_defaults);
    center_smoothing_x_.reset(static_cast<float>(screen_center_.x));
    center_smoothing_y_.reset(static_cast<float>(screen_center_.y));
    zoom_smoothing_.reset(std::max(scale_, 0.0001f));
    smoothed_center_.x = center_smoothing_x_.value_for_render();
    smoothed_center_.y = center_smoothing_y_.value_for_render();
    smoothed_scale_    = std::max(0.0001f, zoom_smoothing_.value_for_render());
    last_update_dt_    = kDefaultSmoothingDt;
    smoothing_frame_counter_ = 0;

    settings_.smooth_motion_zoom           = center_defaults.method != TransformSmoothingMethod::None;
    settings_.motion_smoothing_method      = center_defaults.method;
    settings_.motion_smoothing_tau         = tau_from_rate(center_defaults.lerp_rate);
    settings_.motion_smoothing_spring_frequency = center_defaults.spring_frequency;
    settings_.motion_smoothing_max_step    = center_defaults.max_step;
    settings_.motion_smoothing_snap_threshold = center_defaults.snap_threshold;
}

void camera::set_realism_settings(const RealismSettings& settings) {
    settings_ = settings;
    settings_.parallax_smoothing = sanitize_params(settings_.parallax_smoothing);
    TransformSmoothingParams motion_params = motion_params_from_settings(settings_);
    center_smoothing_x_.set_params(motion_params);
    center_smoothing_y_.set_params(motion_params);
    zoom_smoothing_.set_params(motion_params);
    reset_parallax_smoothing();
}

void camera::set_screen_center(SDL_Point p) {
    if (!screen_center_initialized_) {
        screen_center_ = p;
        screen_center_initialized_ = true;
        pan_offset_x_ = 0.0;
        pan_offset_y_ = 0.0;
        center_smoothing_x_.reset(static_cast<float>(screen_center_.x));
        center_smoothing_y_.reset(static_cast<float>(screen_center_.y));
        smoothed_center_.x = center_smoothing_x_.value_for_render();
        smoothed_center_.y = center_smoothing_y_.value_for_render();
        reset_parallax_smoothing();
        return;
    }
    const double dx = static_cast<double>(p.x) - static_cast<double>(screen_center_.x);
    const double dy = static_cast<double>(p.y) - static_cast<double>(screen_center_.y);
    pan_offset_x_ += dx;
    pan_offset_y_ += dy;
    screen_center_ = p;

    const double distance = std::hypot(dx, dy);
    // Use screen-relative render distance (in pixels) converted to world units for teleport threshold.
    // This keeps the behavior consistent regardless of zoom level.
    const double px_margin = std::max(0.0, static_cast<double>(settings_.render_distance));
    const double scale_for_world = std::max(0.0001, static_cast<double>(smoothed_scale_));
    const double teleport_threshold = std::max(200.0, px_margin * scale_for_world * 0.25);
    if (distance > teleport_threshold) {
        reset_parallax_smoothing();
    }
}

void camera::set_scale(float s) {
    scale_ = (s > 0.0f) ? s : 0.0001f;
    zooming_ = false;
    steps_total_ = steps_done_ = 0;
    start_scale_ = target_scale_ = scale_;
    zoom_smoothing_.reset(scale_);
    smoothed_scale_ = scale_;
    reset_parallax_smoothing();
}

float camera::get_scale() const { return smoothed_scale_; }

void camera::zoom_to_scale(double target_scale, int duration_steps) {
    double clamped = (target_scale > 0.0) ? target_scale : 0.0001;
    if (duration_steps <= 0) {
        set_scale(static_cast<float>(clamped));
        return;
    }
    duration_steps = std::max(1, duration_steps);

    const bool currently_zooming = zooming_ && steps_total_ > 0;
    bool restart_zoom = !currently_zooming || steps_total_ != duration_steps;

    if (!restart_zoom && std::fabs(clamped - target_scale_) > SCALE_EPS) {
        restart_zoom = true;
    }

    if (restart_zoom) {
        start_scale_ = scale_;
        steps_total_ = duration_steps;
        steps_done_  = 0;
    }

    target_scale_ = clamped;
    zooming_      = true;
}

void camera::zoom_to_area(const Area& target_area, int duration_steps) {
    Area adjusted = convert_area_to_aspect(target_area);
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int tgt_w  = std::max(1, width_from_area(adjusted));
    const double target = static_cast<double>(tgt_w) / static_cast<double>(base_w);
    zoom_to_scale(target, duration_steps);
}

void camera::update(float dt) {
    if (!std::isfinite(dt) || dt < 0.0f) {
        dt = 0.0f;
    }
    last_update_dt_ = dt;
    ++smoothing_frame_counter_;

    if (zooming_) {
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
    } else {
        intro = false;
    }

    center_smoothing_x_.target = static_cast<float>(screen_center_.x);
    center_smoothing_y_.target = static_cast<float>(screen_center_.y);
    zoom_smoothing_.target     = std::max(scale_, 0.0001f);

    center_smoothing_x_.advance(dt);
    center_smoothing_y_.advance(dt);
    zoom_smoothing_.advance(dt);

    smoothed_center_.x = center_smoothing_x_.value_for_render();
    smoothed_center_.y = center_smoothing_y_.value_for_render();
    smoothed_scale_    = std::max(0.0001f, zoom_smoothing_.value_for_render());

    recompute_current_view();

    if (!smoothing_entries_.empty()) {
        const uint64_t prune_threshold = (smoothing_frame_counter_ > 480)
            ? smoothing_frame_counter_ - 480
            : 0;
        for (auto it = smoothing_entries_.begin(); it != smoothing_entries_.end(); ) {
            if (it->second.last_used_frame < prune_threshold) {
                it = smoothing_entries_.erase(it);
            } else {
                ++it;
            }
        }
    }
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

double camera::default_zoom_for_room(const Room* room) const {
    return compute_room_scale_from_area(room);
}

void camera::update_zoom(Room* cur,
                         CurrentRoomFinder* finder,
                         Asset* player,
                         bool refresh_requested,
                         float dt) {
    if (!refresh_requested && !zooming_) {
        update(dt);
        return;
    }
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
    update(dt);
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
    target_zoom = std::clamp(target_zoom, BASE_RATIO * settings_.min_zoom_multiplier, BASE_RATIO * settings_.max_zoom_multiplier);
    const bool idle = !zooming_;
    if (idle || std::fabs(target_zoom - target_scale_) > SCALE_EPS) {
        zoom_to_scale(target_zoom, 35);
    }
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
    return make_rect_area("adjusted_" + in.get_name(), c, target_w, target_h, in.resolution());
}

void camera::recompute_current_view() {
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int base_h = std::max(1, height_from_area(base_zoom_));
    const double scale_value = std::max(0.0001, static_cast<double>(smoothed_scale_));
    const int cur_w  = static_cast<int>(std::lround(static_cast<double>(base_w) * scale_value));
    const int cur_h  = static_cast<int>(std::lround(static_cast<double>(base_h) * scale_value));
    SDL_Point center{
        static_cast<int>(std::lround(smoothed_center_.x)),
        static_cast<int>(std::lround(smoothed_center_.y))
    };
    current_view_    = make_rect_area("current_view", center, cur_w, cur_h, base_zoom_.resolution());
}

void camera::pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps) {
    set_focus_override(world_pos);
    const double factor = (zoom_scale_factor > 0.0) ? zoom_scale_factor : 1.0;
    const double new_scale = std::max(0.0001, static_cast<double>(scale_) * factor);
    if (duration_steps <= 0) {
        manual_zoom_override_ = true;
        pan_override_ = false;
        zooming_ = false;
        steps_total_ = steps_done_ = 0;
        start_scale_ = target_scale_ = new_scale;
        start_center_ = target_center_ = world_pos;
        set_screen_center(world_pos);
        set_scale(static_cast<float>(new_scale));
        recompute_current_view();
        return;
    }

    start_center_  = screen_center_;
    target_center_ = world_pos;
    start_scale_   = scale_;
    target_scale_  = new_scale;
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
    const double new_scale = std::max(0.0001, static_cast<double>(scale_) * factor);
    if (duration_steps <= 0) {
        manual_zoom_override_ = true;
        pan_override_ = false;
        zooming_ = false;
        steps_total_ = steps_done_ = 0;
        start_scale_ = target_scale_ = new_scale;
        start_center_ = target_center_ = screen_center_;
        set_scale(static_cast<float>(new_scale));
        recompute_current_view();
        return;
    }

    start_center_  = screen_center_;
    target_center_ = screen_center_;
    start_scale_   = scale_;
    target_scale_  = new_scale;
    steps_total_   = std::max(1, duration_steps);
    steps_done_    = 0;
    zooming_       = true;
    pan_override_  = false;
    manual_zoom_override_ = true;
}

void camera::animate_zoom_towards_point(double factor, SDL_Point screen_point, int duration_steps) {
    if (factor <= 0.0) {
        factor = 1.0;
    }

    const double current_scale = std::max(0.0001, static_cast<double>(scale_));
    const double new_scale     = std::max(0.0001, current_scale * factor);

    int left = 0, top = 0, right = 0, bottom = 0;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();

    const double world_x = static_cast<double>(left) + static_cast<double>(screen_point.x) * current_scale;
    const double world_y = static_cast<double>(top)  + static_cast<double>(screen_point.y) * current_scale;

    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int base_h = std::max(1, height_from_area(base_zoom_));

    const double anchored_center_x = world_x - static_cast<double>(screen_point.x) * new_scale + (static_cast<double>(base_w) * new_scale) * 0.5;
    const double anchored_center_y = world_y - static_cast<double>(screen_point.y) * new_scale + (static_cast<double>(base_h) * new_scale) * 0.5;

    // Exaggerate the pan toward the mouse by a gain factor.
    // Gain of 2.0 makes the camera move twice as far as the
    // standard "keep mouse anchored" solution, increasing the
    // perceived panning intensity during zoom.
    constexpr double PAN_GAIN = 2.0;
    const double dx = anchored_center_x - static_cast<double>(screen_center_.x);
    const double dy = anchored_center_y - static_cast<double>(screen_center_.y);
    const double target_center_x = static_cast<double>(screen_center_.x) + dx * PAN_GAIN;
    const double target_center_y = static_cast<double>(screen_center_.y) + dy * PAN_GAIN;

    SDL_Point target_center{
        static_cast<int>(std::lround(target_center_x)), static_cast<int>(std::lround(target_center_y)) };

    if (duration_steps <= 0) {
        manual_zoom_override_ = true;
        pan_override_ = false;
        zooming_ = false;
        steps_total_ = steps_done_ = 0;
        start_scale_ = target_scale_ = new_scale;
        start_center_ = screen_center_;
        target_center_ = target_center;
        set_screen_center(target_center);
        set_scale(static_cast<float>(new_scale));
        recompute_current_view();
        return;
    }

    start_center_  = screen_center_;
    target_center_ = target_center;

    start_scale_   = scale_;
    target_scale_  = new_scale;
    steps_total_   = std::max(1, duration_steps);
    steps_done_    = 0;
    zooming_       = true;
    pan_override_  = true;
    manual_zoom_override_ = true;
}

SDL_FPoint camera::map_to_screen_f(SDL_FPoint world, float, float) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double inv_scale = (smoothed_scale_ > 0.000001f) ? (1.0 / static_cast<double>(smoothed_scale_)) : 1e6;
    const double sx = (static_cast<double>(world.x) - static_cast<double>(left)) * inv_scale;
    const double sy = (static_cast<double>(world.y) - static_cast<double>(top)) * inv_scale;
    return SDL_FPoint{ static_cast<float>(sx), static_cast<float>(sy) };
}

SDL_FPoint camera::map_to_screen(SDL_Point world, float parallax_x, float parallax_y) const {
    SDL_FPoint world_f{ static_cast<float>(world.x), static_cast<float>(world.y) };
    return map_to_screen_f(world_f, parallax_x, parallax_y);
}

SDL_FPoint camera::screen_to_map(SDL_Point screen, float, float) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double s = static_cast<double>(std::max(0.000001f, smoothed_scale_));
    double wx = static_cast<double>(left) + static_cast<double>(screen.x) * s;
    double wy = static_cast<double>(top)  + static_cast<double>(screen.y) * s;
    return SDL_FPoint{ static_cast<float>(wx), static_cast<float>(wy) };
}

camera::RenderEffects camera::compute_render_effects(
    SDL_Point world,
    float asset_screen_height,
    float reference_screen_height,
    RenderSmoothingKey smoothing_key) const
{
    RenderEffects result;
    SDL_FPoint world_f{ static_cast<float>(world.x), static_cast<float>(world.y) };
    result.screen_position  = map_to_screen_f(world_f);
    result.parallax_offset_x = 0.0f;
    result.vertical_scale   = 1.0f;
    result.distance_scale   = 1.0f;

    const double safe_scale       = std::max(1e-6, static_cast<double>(smoothed_scale_));
    const double pixels_per_world = 1.0 / safe_scale;

    if (!realism_enabled_) {
        return result;
    }

    constexpr double EPS              = 1e-6;
    constexpr double SY               = 200.0;
    constexpr double PARALLAX_KV      = 0.25;
    constexpr double PARALLAX_STEEPEN = 1.5;
    constexpr double PARALLAX_MAX     = 4000.0;
    constexpr double SQUASH_HEIGHT_WT = 0.3;
    constexpr double SQUASH_BASE_WT   = 1.0 - SQUASH_HEIGHT_WT;
    constexpr double ZOOM_ATTEN_WT    = 0.8;
    constexpr double DIST_EXPONENT    = 3;
    constexpr double DIST_MIN         = 0.3;
    constexpr double DIST_MAX         = 1.3;
    constexpr double DY_WEIGHT        = 1.2;
    constexpr double RANGE_COMPRESS   = 2.0;
    constexpr double R_REF            = 400.0;

    const double raw_scale      = std::isfinite(smoothed_scale_) ? static_cast<double>(smoothed_scale_) : 0.0;
    const double zoom_norm      = std::clamp(raw_scale, 0.0, 1.0);
    const double height_at_zoom1 = std::isfinite(settings_.height_at_zoom1) ? std::max(0.0f, settings_.height_at_zoom1) : 0.0f;
    const double camera_height  = height_at_zoom1 * zoom_norm;

    const double tripod_distance = std::isfinite(settings_.tripod_distance_y) ? static_cast<double>(settings_.tripod_distance_y) : 0.0;

    const double base_x = static_cast<double>(screen_center_.x);
    const double base_y = static_cast<double>(screen_center_.y) - tripod_distance;

    const double dx = static_cast<double>(world.x) - base_x;
    const double dy = static_cast<double>(world.y) - base_y;
    const double r  = std::hypot(dx, dy);

    const double zoom_attenuation = (camera_height > EPS) ? camera_height / (camera_height + height_at_zoom1 + EPS) : 1.0;

    const double screen_bias = 0.5 + 0.5 * std::tanh(dy / SY);

    if (parallax_enabled_) {
        const double parallax_strength = std::max(0.0f, settings_.parallax_strength);
        if (parallax_strength > 0.0 && camera_height > EPS) {
            const int view_height = height_from_area(current_view_);
            const int view_width  = width_from_area(current_view_);

            const double ndy = dy / (view_height * 0.5);
            const double ndx = dx / (view_width  * 0.5);

            const double vertical_bias = 1.0 + PARALLAX_KV *
                                         std::tanh(ndy * (view_height / SY) * PARALLAX_STEEPEN);

            double zoom_gain = (height_at_zoom1 > EPS) ? (height_at_zoom1 / (camera_height + EPS)) : 1.0;
            if (zoom_gain >= 1.0) {
                zoom_gain = std::pow(zoom_gain, 1.5);
            }

            double parallax_px = parallax_strength *
                                 ndx * ndy *
                                 pixels_per_world * vertical_bias * zoom_gain;

            parallax_px = std::clamp(parallax_px, -PARALLAX_MAX, PARALLAX_MAX);
            result.parallax_offset_x = static_cast<float>(parallax_px);
        }
    }

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

    {
        const double distance_strength = std::max(0.0f, settings_.distance_scale_strength);
        if (distance_strength > 0.0) {
            const double r_weighted   = std::hypot(dx, dy * DY_WEIGHT);
            const double r_normalized = r_weighted / RANGE_COMPRESS;

            const double base_scale = std::sqrt( (camera_height + R_REF) / (camera_height + r_normalized + EPS) );

            double distance_scale = 1.0 + (base_scale - 1.0) * distance_strength;

            const double squash_factor = static_cast<double>(result.vertical_scale);
            distance_scale = 1.0 + (distance_scale - 1.0) * std::pow(squash_factor, DIST_EXPONENT);

            distance_scale = std::clamp(distance_scale, DIST_MIN, DIST_MAX);
            result.distance_scale = static_cast<float>(distance_scale);
        }
    }

    if (smoothing_key != 0) {
        TransformSmoothingParams raw_params = sanitize_params(settings_.parallax_smoothing);
        if (!settings_.smooth_motion_zoom) {
            raw_params.method = TransformSmoothingMethod::None;
        }
        if (raw_params.method != TransformSmoothingMethod::None) {
            auto& entry = smoothing_entries_[smoothing_key];
            entry.parallax.set_params(raw_params);
            entry.zoom.set_params(raw_params);

            const float dt = (last_update_dt_ > 0.0f) ? last_update_dt_ : kDefaultSmoothingDt;
            const float target_parallax = std::isfinite(result.parallax_offset_x) ? result.parallax_offset_x : 0.0f;
            const float target_zoom     = std::isfinite(result.distance_scale) && result.distance_scale > 0.0f
                                              ? result.distance_scale
                                              : 1.0f;

            bool force_snap = !entry.initialized;
            const float snap_threshold = std::max(0.0f, raw_params.snap_threshold);
            const float max_step       = std::max(0.0f, raw_params.max_step);
            if (!force_snap) {
                const float current = entry.parallax.current;
                const float delta   = std::fabs(target_parallax - current);
                if (snap_threshold > 0.0f && delta > snap_threshold * 4.0f) {
                    force_snap = true;
                } else if (max_step > 0.0f && dt > 0.0f) {
                    const float max_delta = max_step * dt * 4.0f;
                    if (delta > max_delta) {
                        force_snap = true;
                    }
                }
            }

            entry.parallax.target = target_parallax;
            entry.zoom.target     = target_zoom;

            if (force_snap) {
                entry.parallax.reset(target_parallax);
                entry.zoom.reset(target_zoom);
                entry.parallax.target = target_parallax;
                entry.zoom.target     = target_zoom;
                entry.initialized     = true;
            } else {
                entry.parallax.advance(dt);
                entry.zoom.advance(dt);
            }

            entry.last_used_frame = smoothing_frame_counter_;
            result.parallax_offset_x = entry.parallax.value_for_render();
            result.distance_scale    = std::max(0.0f, entry.zoom.value_for_render());
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
    try_read_float("min_visible_screen_ratio", settings_.min_visible_screen_ratio);

    auto try_read_int = [&](const char* key, int& target) {
        auto it = data.find(key);
        if (it == data.end()) return;
        if (it->is_number_integer()) {
            target = it->get<int>();
        } else if (it->is_number_float()) {
            target = static_cast<int>(std::lround(it->get<double>()));
        }
};

    try_read_int("render_quality_percent", settings_.render_quality_percent);

    auto try_read_bool = [&](const char* key, bool& target) {
        auto it = data.find(key);
        if (it == data.end()) return;
        if (it->is_boolean()) {
            target = it->get<bool>();
        } else if (it->is_number_integer()) {
            target = it->get<int>() != 0;
        }
    };

    try_read_bool("smooth_motion_zoom", settings_.smooth_motion_zoom);

    auto try_read_smoothing_method = [&](const char* key, TransformSmoothingMethod& target) {
        auto it = data.find(key);
        if (it == data.end()) return;
        if (it->is_number_integer()) {
            const int raw = it->get<int>();
            switch (raw) {
            case static_cast<int>(TransformSmoothingMethod::None):
                target = TransformSmoothingMethod::None;
                break;
            case static_cast<int>(TransformSmoothingMethod::Lerp):
                target = TransformSmoothingMethod::Lerp;
                break;
            case static_cast<int>(TransformSmoothingMethod::CriticallyDampedSpring):
                target = TransformSmoothingMethod::CriticallyDampedSpring;
                break;
            default:
                break;
            }
        }
    };

    try_read_smoothing_method("parallax_smoothing_method", settings_.parallax_smoothing.method);
    try_read_smoothing_method("motion_smoothing_method", settings_.motion_smoothing_method);
    try_read_float("parallax_smoothing_lerp_rate", settings_.parallax_smoothing.lerp_rate);
    try_read_float("parallax_smoothing_spring_frequency", settings_.parallax_smoothing.spring_frequency);
    try_read_float("parallax_smoothing_max_step", settings_.parallax_smoothing.max_step);
    try_read_float("parallax_smoothing_snap_threshold", settings_.parallax_smoothing.snap_threshold);
    try_read_float("motion_smoothing_tau", settings_.motion_smoothing_tau);
    try_read_float("motion_smoothing_spring_frequency", settings_.motion_smoothing_spring_frequency);
    try_read_float("motion_smoothing_max_step", settings_.motion_smoothing_max_step);
    try_read_float("motion_smoothing_snap_threshold", settings_.motion_smoothing_snap_threshold);
    try_read_float("scale_hysteresis_margin", settings_.scale_variant_hysteresis_margin);
    try_read_float("min_zoom_multiplier", settings_.min_zoom_multiplier);
    try_read_float("max_zoom_multiplier", settings_.max_zoom_multiplier);

    if (!std::isfinite(settings_.render_distance) || settings_.render_distance < 0.0f) {
        settings_.render_distance = 800.0f;
    }

    settings_.parallax_strength = std::isfinite(settings_.parallax_strength) ? std::max(0.0f, settings_.parallax_strength) : 0.0f;

    settings_.foreshorten_strength = std::isfinite(settings_.foreshorten_strength) ? std::max(0.0f, settings_.foreshorten_strength) : 0.0f;

    settings_.distance_scale_strength = std::isfinite(settings_.distance_scale_strength) ? std::max(0.0f, settings_.distance_scale_strength) : 0.0f;

    if (!std::isfinite(settings_.height_at_zoom1) || settings_.height_at_zoom1 < 0.0f) {
        settings_.height_at_zoom1 = 18.0f;
    }

    if (!std::isfinite(settings_.tripod_distance_y)) {
        settings_.tripod_distance_y = 0.0f;
    } else {
        settings_.tripod_distance_y = std::clamp(settings_.tripod_distance_y, -2000.0f, 2000.0f);
    }

    if (!std::isfinite(settings_.min_visible_screen_ratio) || settings_.min_visible_screen_ratio < 0.0f) {
        settings_.min_visible_screen_ratio = 0.015f;
    } else {
        settings_.min_visible_screen_ratio = std::clamp(settings_.min_visible_screen_ratio, 0.0f, 0.5f);
    }

    auto align_quality = [](int percent) {
        constexpr int kOptions[] = {100, 75, 50, 25, 10};
        int best = kOptions[0];
        int best_diff = std::abs(percent - best);
        for (int option : kOptions) {
            const int diff = std::abs(percent - option);
            if (diff < best_diff) {
                best_diff = diff;
                best = option;
            }
        }
        return best;
};

    settings_.render_quality_percent = align_quality(settings_.render_quality_percent);

    settings_.parallax_smoothing = sanitize_params(settings_.parallax_smoothing);

    settings_.smooth_motion_zoom = settings_.smooth_motion_zoom &&
        settings_.motion_smoothing_method != TransformSmoothingMethod::None;
    if (!std::isfinite(settings_.motion_smoothing_tau) || settings_.motion_smoothing_tau < 0.0f) {
        settings_.motion_smoothing_tau = 0.0f;
    }
    if (!std::isfinite(settings_.motion_smoothing_max_step) || settings_.motion_smoothing_max_step < 0.0f) {
        settings_.motion_smoothing_max_step = 0.0f;
    }
    if (!std::isfinite(settings_.motion_smoothing_snap_threshold) || settings_.motion_smoothing_snap_threshold < 0.0f) {
        settings_.motion_smoothing_snap_threshold = 0.0f;
    }
    if (!std::isfinite(settings_.motion_smoothing_spring_frequency) || settings_.motion_smoothing_spring_frequency < 0.0f) {
        settings_.motion_smoothing_spring_frequency = 0.0f;
    }
    if (!std::isfinite(settings_.scale_variant_hysteresis_margin) || settings_.scale_variant_hysteresis_margin < 0.0f) {
        settings_.scale_variant_hysteresis_margin = 0.05f;
    }
    if (!std::isfinite(settings_.min_zoom_multiplier) || settings_.min_zoom_multiplier < 0.1f) {
        settings_.min_zoom_multiplier = 0.7f;
    } else {
        settings_.min_zoom_multiplier = std::clamp(settings_.min_zoom_multiplier, 0.1f, 2.0f);
    }
    if (!std::isfinite(settings_.max_zoom_multiplier) || settings_.max_zoom_multiplier < 0.1f) {
        settings_.max_zoom_multiplier = 1.3f;
    } else {
        settings_.max_zoom_multiplier = std::clamp(settings_.max_zoom_multiplier, 0.1f, 3.0f);
    }
    TransformSmoothingParams motion_params = motion_params_from_settings(settings_);
    center_smoothing_x_.set_params(motion_params);
    center_smoothing_y_.set_params(motion_params);
    zoom_smoothing_.set_params(motion_params);

    reset_parallax_smoothing();
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
    j["min_visible_screen_ratio"] = settings_.min_visible_screen_ratio;
    j["render_quality_percent"]   = settings_.render_quality_percent;
    j["smooth_motion_zoom"]        = settings_.smooth_motion_zoom;
    j["motion_smoothing_method"]   = static_cast<int>(settings_.motion_smoothing_method);
    j["motion_smoothing_tau"]      = settings_.motion_smoothing_tau;
    j["motion_smoothing_spring_frequency"] = settings_.motion_smoothing_spring_frequency;
    j["motion_smoothing_max_step"] = settings_.motion_smoothing_max_step;
    j["motion_smoothing_snap_threshold"] = settings_.motion_smoothing_snap_threshold;
    j["scale_hysteresis_margin"]   = settings_.scale_variant_hysteresis_margin;
    j["min_zoom_multiplier"]       = settings_.min_zoom_multiplier;
    j["max_zoom_multiplier"]       = settings_.max_zoom_multiplier;
    j["parallax_smoothing_method"] = static_cast<int>(settings_.parallax_smoothing.method);
    j["parallax_smoothing_lerp_rate"] = settings_.parallax_smoothing.lerp_rate;
    j["parallax_smoothing_spring_frequency"] = settings_.parallax_smoothing.spring_frequency;
    j["parallax_smoothing_max_step"] = settings_.parallax_smoothing.max_step;
    j["parallax_smoothing_snap_threshold"] = settings_.parallax_smoothing.snap_threshold;
    return j;
}

TransformSmoothingParams camera::motion_smoothing_params() const {
    return motion_params_from_settings(settings_);
}

int camera::get_render_distance_world_margin() const {
    // Interpret render_distance as a screen-space margin in pixels and
    // convert it to world units using the current smoothed scale.
    const double px_margin = std::max(0.0, static_cast<double>(settings_.render_distance));
    const double scale_for_world = std::max(0.0001, static_cast<double>(smoothed_scale_));
    const double world_margin = px_margin * scale_for_world;
    return static_cast<int>(std::lround(world_margin));
}

void camera::reset_parallax_smoothing() {
    smoothing_entries_.clear();
}
