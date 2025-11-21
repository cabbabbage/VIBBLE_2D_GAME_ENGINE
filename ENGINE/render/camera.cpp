#include "camera.hpp"

#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "map_generation/room.hpp"
#include "core/find_current_room.hpp"
#include "utils/transform_smoothing_settings.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <cmath>
#include <algorithm>
#include <vector>
#include <tuple>
#include <string>
#include <nlohmann/json.hpp>

namespace {
    constexpr float  kMinTau    = 1e-4f;
    constexpr double SCALE_EPS  = 1e-4;
    constexpr double BASE_RATIO = 1.1;
    constexpr double PI_D       = 3.14159265358979323846;
    constexpr double HALF_FOV_Y = PI_D / 4.0; // 45 deg half FOV (90 total)
    constexpr double RAD_TO_DEG = 180.0 / PI_D;
    constexpr float  kMaxForeshortenSliderStrength = 2.0f;
    constexpr float  kMaxDistanceSliderStrength = 1.0f;
    constexpr float  kDefaultPitchDegrees   = 330.0f;

    double wrap_degrees_0_360(double raw_value) {
        if (!std::isfinite(raw_value)) {
            return static_cast<double>(kDefaultPitchDegrees);
        }
        double wrapped = std::fmod(raw_value, 360.0);
        if (wrapped < 0.0) wrapped += 360.0;
        if (wrapped >= 360.0 || !std::isfinite(wrapped)) {
            wrapped = std::fmod(wrapped, 360.0);
            if (wrapped < 0.0) wrapped += 360.0;
        }
        return std::isfinite(wrapped) ? wrapped : static_cast<double>(kDefaultPitchDegrees);
    }

    float wrap_degrees_0_360(float raw_value) {
        return static_cast<float>(wrap_degrees_0_360(static_cast<double>(raw_value)));
    }

    double signed_radians_from_degrees(double degrees) {
        const double wrapped_deg = wrap_degrees_0_360(degrees);
        const double signed_deg  = (wrapped_deg > 180.0) ? wrapped_deg - 360.0 : wrapped_deg;
        return signed_deg * (PI_D / 180.0);
    }

    double shortest_delta_degrees(double from_deg, double to_deg) {
        return std::remainder(to_deg - from_deg, 360.0);
    }

    float sanitize_pitch_degrees(float raw_value, bool* clamped = nullptr) {
        if (clamped) *clamped = false;
        const float wrapped = wrap_degrees_0_360(std::isfinite(raw_value)
            ? raw_value
            : kDefaultPitchDegrees);
        const float clamped_value = std::clamp(
            wrapped,
            camera::kMinPitchDegrees,
            camera::kMaxPitchDegrees);
        if (clamped && clamped_value != raw_value) {
            *clamped = true;
        }
        return clamped_value;
    }

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

    double clamp_zoom_scale(double value) {
        return std::clamp(
            value,
            0.0001,
            static_cast<double>(camera::kMaxZoomAnchors));
    }

    double camera_height_from_scale(double scale_value, const camera::RealismSettings& settings) {
        const double base_height = std::isfinite(settings.base_height_px)
            ? std::max(0.0, static_cast<double>(settings.base_height_px))
            : 0.0;
        if (base_height <= 0.0) {
            return 0.0;
        }
        return std::max(0.0, base_height * scale_value);
    }

    double pitch_from_scale(double scale_value, const camera::RealismSettings& settings) {
        const double low_zoom  = std::max(static_cast<double>(camera::kMinZoomAnchors),
                                          static_cast<double>(settings.zoom_low));
        const double high_zoom = std::max(low_zoom + 1e-4, static_cast<double>(settings.zoom_high));
        const double t_raw = (scale_value - low_zoom) / std::max(1e-4, high_zoom - low_zoom);
        const double t     = std::clamp(t_raw, 0.0, 1.0);

        bool clamped_in  = false;
        bool clamped_out = false;
        const double pitch_in_deg  = static_cast<double>(sanitize_pitch_degrees(settings.tilt_zoom_in_degrees, &clamped_in));
        const double pitch_out_deg = static_cast<double>(sanitize_pitch_degrees(settings.tilt_zoom_out_degrees, &clamped_out));

        const double delta = shortest_delta_degrees(pitch_in_deg, pitch_out_deg);
        const double pitch = pitch_in_deg + delta * t;

        return wrap_degrees_0_360(pitch);
    }

    template <typename T>
    T lerp(T a, T b, double t) {
        return static_cast<T>(a + (b - a) * t);
    }

    float clamp_foreshorten(float raw_value) {
        const float value = std::isfinite(raw_value) ? std::max(0.0f, raw_value) : 0.0f;
        return std::clamp(value, 0.0f, kMaxForeshortenSliderStrength);
    }

    float clamp_distance_strength(float raw_value) {
        const float value = std::isfinite(raw_value) ? std::max(0.0f, raw_value) : 0.0f;
        return std::clamp(value, 0.0f, kMaxDistanceSliderStrength);
    }

    float clamp_depth_offset(float raw_value) {
        const float value = std::isfinite(raw_value) ? raw_value : 0.0f;
        return std::clamp(value, -4000.0f, 4000.0f);
    }
}

camera::CameraGeometry camera::compute_geometry_for_scale(double scale_value) const {
    CameraGeometry g{};
    if (!realism_enabled_) {
        return g;
    }

    const double clamped_scale = std::max(0.0001, scale_value);
    g.camera_height = camera_height_from_scale(clamped_scale, settings_);
    if (g.camera_height <= 0.0) {
        return g;
    }

    g.anchor_world_y = anchor_world_y();
    g.focus_depth = 0.0; // measured in world units along +Y from anchor

    const double target_pitch_deg = pitch_from_scale(clamped_scale, settings_);
    g.pitch_degrees = static_cast<float>(target_pitch_deg);
    g.pitch_radians = signed_radians_from_degrees(target_pitch_deg);

    g.focus_ndc_offset = 0.0;

    g.valid = true;
    return g;
}

camera::CameraGeometry camera::compute_geometry() const {
    return compute_geometry_for_scale(static_cast<double>(smoothed_scale_));
}

void camera::update_geometry_cache(const CameraGeometry& g) {
    const double scale_value = std::max(0.0001, static_cast<double>(smoothed_scale_));
    runtime_camera_height_ = g.camera_height;
    runtime_focus_depth_   = g.focus_depth;
    runtime_anchor_world_y_ = g.anchor_world_y;
    runtime_focus_ndc_offset_ = g.focus_ndc_offset;
    runtime_pitch_rad_     = g.pitch_radians;
    runtime_pitch_deg_     = g.pitch_degrees;
    runtime_foreshorten_strength_ = foreshorten_for_scale(scale_value);
    runtime_distance_scale_strength_ = distance_scale_for_scale(scale_value);
    runtime_depth_offset_px_ = depth_offset_for_scale(scale_value);
    runtime_floor_params_ = compute_floor_depth_params_for_geometry(g, scale_value);
    geometry_valid_        = g.valid;
    if (!g.valid) {
        runtime_camera_height_ = 0.0;
        runtime_focus_depth_   = 0.0;
        runtime_anchor_world_y_ = 0.0;
        runtime_focus_ndc_offset_ = 0.0;
        runtime_pitch_rad_     = 0.0;
        runtime_pitch_deg_     = 0.0f;
        runtime_foreshorten_strength_ = foreshorten_for_scale(scale_value);
        runtime_distance_scale_strength_ = distance_scale_for_scale(scale_value);
        runtime_depth_offset_px_ = depth_offset_for_scale(scale_value);
        runtime_floor_params_ = FloorDepthParams{};
    }
}

// TransformSmoother1D implementation

void camera::TransformSmoother1D::set_params(const TransformSmoothingParams& p) {
    params = sanitize_params(p);
}

void camera::TransformSmoother1D::reset(float value) {
    current  = value;
    target   = value;
    velocity = 0.0f;
}

void camera::TransformSmoother1D::advance(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return;
    }

    const float snap_threshold = std::max(0.0f, params.snap_threshold);
    const float max_step       = std::max(0.0f, params.max_step);

    const float delta = target - current;
    if (snap_threshold > 0.0f && std::fabs(delta) < snap_threshold) {
        current  = target;
        velocity = 0.0f;
        return;
    }

    switch (params.method) {
    case TransformSmoothingMethod::None: {
        current  = target;
        velocity = 0.0f;
        break;
    }
    case TransformSmoothingMethod::Lerp: {
        const float rate = std::max(0.0f, params.lerp_rate);
        if (rate <= 0.0f) {
            current  = target;
            velocity = 0.0f;
            break;
        }
        const float t = 1.0f - std::exp(-rate * dt);
        float step = delta * t;
        if (max_step > 0.0f) {
            const float max_delta = max_step * dt;
            if (step >  max_delta) step =  max_delta;
            if (step < -max_delta) step = -max_delta;
        }
        current += step;
        velocity = step / std::max(dt, 1e-6f);
        break;
    }
    case TransformSmoothingMethod::CriticallyDampedSpring: {
        const float freq = std::max(0.0f, params.spring_frequency);
        if (freq <= 0.0f) {
            current  = target;
            velocity = 0.0f;
            break;
        }
        const float omega = 2.0f * static_cast<float>(PI_D) * freq;
        const float x0    = current - target;
        const float v0    = velocity;
        const float e     = std::exp(-omega * dt);

        const float x = (x0 + (v0 + omega * x0) * dt) * e;
        const float v = (v0 - omega * (v0 + omega * x0) * dt) * e;

        current  = target + x;
        velocity = v;
        break;
    }
    default:
        break;
    }
}

camera::camera(int screen_width, int screen_height, const Area& starting_zoom)
{
    screen_width_  = screen_width;
    screen_height_ = screen_height;
    aspect_        = (screen_height_ > 0)
        ? static_cast<double>(screen_width_) / static_cast<double>(screen_height_)
        : 1.0;

    Area      adjusted_start = convert_area_to_aspect(starting_zoom);
    SDL_Point start_center   = adjusted_start.get_center();

    base_zoom_    = make_rect_area("base_zoom", start_center, screen_width_, screen_height_, adjusted_start.resolution());
    current_view_ = adjusted_start;
    screen_center_ = start_center;
    screen_center_initialized_ = true;
    pan_offset_x_ = 0.0;
    pan_offset_y_ = 0.0;

    const int base_w = width_from_area(base_zoom_);
    const int curr_w = width_from_area(current_view_);
    scale_ = (base_w > 0)
        ? static_cast<float>(static_cast<double>(curr_w) / static_cast<double>(base_w))
        : 1.0f;

    zooming_     = false;
    steps_total_ = 0;
    steps_done_  = 0;
    start_scale_ = scale_;
    target_scale_ = scale_;

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
    update_geometry_cache(compute_geometry());

    settings_.smooth_motion_zoom                 = center_defaults.method != TransformSmoothingMethod::None;
    settings_.motion_smoothing_method            = center_defaults.method;
    settings_.motion_smoothing_tau               = tau_from_rate(center_defaults.lerp_rate);
    settings_.motion_smoothing_spring_frequency  = center_defaults.spring_frequency;
    settings_.motion_smoothing_max_step          = center_defaults.max_step;
    settings_.motion_smoothing_snap_threshold    = center_defaults.snap_threshold;
}

void camera::set_realism_settings(const RealismSettings& settings) {
    settings_ = settings;
    settings_.zoom_low = std::clamp(settings_.zoom_low,
                                    camera::kMinZoomAnchors,
                                    camera::kMaxZoomAnchors);
    const float min_high = std::min(camera::kMaxZoomAnchors, settings_.zoom_low + 0.0001f);
    settings_.zoom_high = std::clamp(settings_.zoom_high, min_high, camera::kMaxZoomAnchors);
    if (!std::isfinite(settings_.base_height_px) || settings_.base_height_px <= 0.0f) {
        settings_.base_height_px = 720.0f;
    }
    settings_.tilt_zoom_in_degrees  = sanitize_pitch_degrees(settings_.tilt_zoom_in_degrees);
    settings_.tilt_zoom_out_degrees = sanitize_pitch_degrees(settings_.tilt_zoom_out_degrees);
    settings_.grid_depth_offset_px = clamp_depth_offset(settings_.grid_depth_offset_px);
    auto sanitize_horizon_value = [](std::optional<float>& value) {
        if (value && !std::isfinite(*value)) {
            value.reset();
        }
    };
    sanitize_horizon_value(settings_.horizon_y_at_zoom_low);
    sanitize_horizon_value(settings_.horizon_y_at_zoom_high);
    settings_.parallax_smoothing = sanitize_params(settings_.parallax_smoothing);
    if (settings_.parallax_smoothing.method == TransformSmoothingMethod::Lerp &&
        settings_.parallax_smoothing.lerp_rate <= 0.0f) {
        settings_.parallax_smoothing.lerp_rate = rate_from_tau(0.08f);
    } else if (settings_.parallax_smoothing.method == TransformSmoothingMethod::CriticallyDampedSpring &&
               settings_.parallax_smoothing.spring_frequency <= 0.0f) {
        settings_.parallax_smoothing.spring_frequency = 10.0f;
    }

    settings_.foreshorten_strength = clamp_foreshorten(settings_.foreshorten_strength);
    settings_.foreshorten_at_zoom_low = clamp_foreshorten(
        std::isfinite(settings_.foreshorten_at_zoom_low)
            ? settings_.foreshorten_at_zoom_low
            : settings_.foreshorten_strength);
    settings_.foreshorten_at_zoom_high = clamp_foreshorten(
        std::isfinite(settings_.foreshorten_at_zoom_high)
            ? settings_.foreshorten_at_zoom_high
            : settings_.foreshorten_strength);
    settings_.foreshorten_strength =
        0.5f * (settings_.foreshorten_at_zoom_low + settings_.foreshorten_at_zoom_high);

    settings_.distance_scale_strength = clamp_distance_strength(settings_.distance_scale_strength);
    settings_.distance_scale_at_zoom_low = clamp_distance_strength(
        std::isfinite(settings_.distance_scale_at_zoom_low)
            ? settings_.distance_scale_at_zoom_low
            : settings_.distance_scale_strength);
    settings_.distance_scale_at_zoom_high = clamp_distance_strength(
        std::isfinite(settings_.distance_scale_at_zoom_high)
            ? settings_.distance_scale_at_zoom_high
            : settings_.distance_scale_strength);
    settings_.distance_scale_strength =
        0.5f * (settings_.distance_scale_at_zoom_low + settings_.distance_scale_at_zoom_high);

    settings_.depth_offset_at_zoom_low = clamp_depth_offset(
        std::isfinite(settings_.depth_offset_at_zoom_low)
            ? settings_.depth_offset_at_zoom_low
            : settings_.grid_depth_offset_px);
    settings_.depth_offset_at_zoom_high = clamp_depth_offset(
        std::isfinite(settings_.depth_offset_at_zoom_high)
            ? settings_.depth_offset_at_zoom_high
            : settings_.grid_depth_offset_px);
    settings_.grid_depth_offset_px =
        0.5f * (settings_.depth_offset_at_zoom_low + settings_.depth_offset_at_zoom_high);

    TransformSmoothingParams motion_params = motion_params_from_settings(settings_);
    center_smoothing_x_.set_params(motion_params);
    center_smoothing_y_.set_params(motion_params);
    zoom_smoothing_.set_params(motion_params);
    update_geometry_cache(compute_geometry());
}

void camera::set_screen_center(SDL_Point p) {
    if (!screen_center_initialized_) {
        screen_center_              = p;
        screen_center_initialized_  = true;
        pan_offset_x_               = 0.0;
        pan_offset_y_               = 0.0;
        center_smoothing_x_.reset(static_cast<float>(screen_center_.x));
        center_smoothing_y_.reset(static_cast<float>(screen_center_.y));
        smoothed_center_.x = center_smoothing_x_.value_for_render();
        smoothed_center_.y = center_smoothing_y_.value_for_render();
        return;
    }

    const double dx = static_cast<double>(p.x) - static_cast<double>(screen_center_.x);
    const double dy = static_cast<double>(p.y) - static_cast<double>(screen_center_.y);
    pan_offset_x_ += dx;
    pan_offset_y_ += dy;
    screen_center_ = p;

    const double distance     = std::hypot(dx, dy);
    const double px_margin    = static_cast<double>(std::max(screen_width_, screen_height_));
    const double scale_world  = std::max(0.0001, static_cast<double>(smoothed_scale_));
    const double teleport_thr = std::max(200.0, px_margin * scale_world * 0.25);
    if (distance > teleport_thr) {
        center_smoothing_x_.reset(static_cast<float>(screen_center_.x));
        center_smoothing_y_.reset(static_cast<float>(screen_center_.y));
        smoothed_center_.x = center_smoothing_x_.value_for_render();
        smoothed_center_.y = center_smoothing_y_.value_for_render();
    }
}

void camera::set_scale(float s) {
    const double clamped = clamp_zoom_scale(static_cast<double>(s));
    scale_ = static_cast<float>(clamped);
    zooming_     = false;
    steps_total_ = 0;
    steps_done_  = 0;
    start_scale_ = scale_;
    target_scale_= scale_;
    zoom_smoothing_.reset(scale_);
    smoothed_scale_ = scale_;
    update_geometry_cache(compute_geometry());
}

float camera::get_scale() const {
    return smoothed_scale_;
}

void camera::zoom_to_scale(double target_scale, int duration_steps) {
    double clamped = clamp_zoom_scale(target_scale);
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

    if (zooming_) {
        ++steps_done_;
        double t = static_cast<double>(steps_done_) /
                   static_cast<double>(std::max(1, steps_total_));
        t = std::clamp(t, 0.0, 1.0);
        double s = start_scale_ + (target_scale_ - start_scale_) * t;
        scale_ = static_cast<float>(std::max(0.0001, s));

        if (pan_override_) {
            const double cx = static_cast<double>(start_center_.x) +
                              (static_cast<double>(target_center_.x) - static_cast<double>(start_center_.x)) * t;
            const double cy = static_cast<double>(start_center_.y) +
                              (static_cast<double>(target_center_.y) - static_cast<double>(start_center_.y)) * t;
            SDL_Point new_center{
                static_cast<int>(std::lround(cx)),
                static_cast<int>(std::lround(cy))
            };
            set_screen_center(new_center);
        }

        if (steps_done_ >= steps_total_) {
            scale_ = static_cast<float>(target_scale_);
            if (pan_override_) {
                set_screen_center(target_center_);
            }
            zooming_      = false;
            pan_override_ = false;
            steps_total_  = 0;
            steps_done_   = 0;
            start_scale_  = target_scale_;
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
}

double camera::compute_room_scale_from_area(const Room* room) const {
    if (!room || !room->room_area || starting_area_ <= 0.0) {
        return BASE_RATIO;
    }

    Area adjusted = convert_area_to_aspect(*room->room_area);
    double a = adjusted.get_size();
    if (a <= 0.0 || room->type == "trail") {
        return BASE_RATIO * 0.8;
    }

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



void camera::update_zoom(Room* cur,
                         CurrentRoomFinder* finder,
                         Asset* player,
                         bool refresh_requested,
                         float dt)
{
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

    target_zoom = std::clamp(
        target_zoom,
        static_cast<double>(settings_.zoom_low),
        static_cast<double>(settings_.zoom_high)
    );

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
    current_view_ = make_rect_area("current_view", center, cur_w, cur_h, base_zoom_.resolution());
    update_geometry_cache(compute_geometry());
}

void camera::pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps) {
    focus_override_ = true;
    focus_point_    = world_pos;

    const double factor    = (zoom_scale_factor > 0.0) ? zoom_scale_factor : 1.0;
    const double new_scale = clamp_zoom_scale(static_cast<double>(scale_) * factor);

    if (duration_steps <= 0) {
        manual_zoom_override_ = true;
        pan_override_         = false;
        zooming_              = false;
        steps_total_          = 0;
        steps_done_           = 0;
        start_scale_          = new_scale;
        target_scale_         = new_scale;
        start_center_         = world_pos;
        target_center_        = world_pos;
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
    const double new_scale = clamp_zoom_scale(static_cast<double>(scale_) * factor);

    if (duration_steps <= 0) {
        manual_zoom_override_ = true;
        pan_override_         = false;
        zooming_              = false;
        steps_total_          = 0;
        steps_done_           = 0;
        start_scale_          = new_scale;
        target_scale_         = new_scale;
        start_center_         = screen_center_;
        target_center_        = screen_center_;
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

    const double current_scale = clamp_zoom_scale(static_cast<double>(scale_));
    const double new_scale     = clamp_zoom_scale(current_scale * factor);

    int left = 0, top = 0, right = 0, bottom = 0;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();

    const double world_x = static_cast<double>(left) + static_cast<double>(screen_point.x) * current_scale;
    const double world_y = static_cast<double>(top)  + static_cast<double>(screen_point.y) * current_scale;

    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int base_h = std::max(1, height_from_area(base_zoom_));

    const double anchored_center_x =
        world_x - static_cast<double>(screen_point.x) * new_scale +
        (static_cast<double>(base_w) * new_scale) * 0.5;
    const double anchored_center_y =
        world_y - static_cast<double>(screen_point.y) * new_scale +
        (static_cast<double>(base_h) * new_scale) * 0.5;

    constexpr double PAN_GAIN = 2.0;
    const double dx = anchored_center_x - static_cast<double>(screen_center_.x);
    const double dy = anchored_center_y - static_cast<double>(screen_center_.y);
    const double target_center_x = static_cast<double>(screen_center_.x) + dx * PAN_GAIN;
    const double target_center_y = static_cast<double>(screen_center_.y) + dy * PAN_GAIN;

    SDL_Point target_center{
        static_cast<int>(std::lround(target_center_x)),
        static_cast<int>(std::lround(target_center_y))
    };

    if (duration_steps <= 0) {
        manual_zoom_override_ = true;
        pan_override_         = false;
        zooming_              = false;
        steps_total_          = 0;
        steps_done_           = 0;
        start_scale_          = new_scale;
        target_scale_         = new_scale;
        start_center_         = screen_center_;
        target_center_        = target_center;
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

SDL_FPoint camera::map_to_screen_f(SDL_FPoint world) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double inv_scale =
        (smoothed_scale_ > 0.000001f)
            ? (1.0 / static_cast<double>(smoothed_scale_))
            : 1e6;
    const double sx = (static_cast<double>(world.x) - static_cast<double>(left)) * inv_scale;
    const double sy = (static_cast<double>(world.y) - static_cast<double>(top)) * inv_scale;
    return SDL_FPoint{ static_cast<float>(sx), static_cast<float>(sy) };
}

SDL_FPoint camera::map_to_screen(SDL_Point world) const {
    SDL_FPoint world_f{ static_cast<float>(world.x), static_cast<float>(world.y) };
    return map_to_screen_f(world_f);
}

SDL_FPoint camera::screen_to_map(SDL_Point screen) const {
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
    RenderSmoothingKey /*smoothing_key*/) const
{
    RenderEffects result;
    SDL_FPoint world_f{ static_cast<float>(world.x), static_cast<float>(world.y) };
    result.screen_position  = map_to_screen_f(world_f);
    if (realism_enabled_) {
        result.screen_position.y = warp_floor_screen_y(
            world_f.y,
            result.screen_position.y
        );
    }
    result.vertical_scale   = 1.0f;
    result.distance_scale   = 1.0f;

    if (!realism_enabled_) {
        return result;
    }

    constexpr double EPS              = 1e-6;
    constexpr double SCREEN_Y_SCALE   = 200.0;
    constexpr double SQUASH_HEIGHT_WT = 0.3;
    constexpr double SQUASH_BASE_WT   = 1.0 - SQUASH_HEIGHT_WT;
    constexpr double DIST_EXPONENT    = 3.0;
    constexpr double DIST_MIN         = 0.3;
    constexpr double DIST_MAX         = 1.3;
    constexpr double DEPTH_RANGE_PIXELS = 600.0;
    constexpr double R_REF              = 400.0;

    const CameraGeometry geom = compute_geometry();
    if (!geom.valid || geom.camera_height <= EPS) {
        return result;
    }

    const double camera_height = geom.camera_height;
    const double height_reference = std::max(
        1.0,
        static_cast<double>(settings_.base_height_px));
    const double pitch_rad     = geom.pitch_radians;
    const double pitch_factor  = std::min(std::abs(pitch_rad) / (PI_D / 3.0), 1.0);
    const double tilt_up       = 1.0 - pitch_factor; // 0 = steep down, 1 = level/upward
    const double slider_vertical_strength = std::clamp(
        static_cast<double>(runtime_foreshorten_strength_),
        0.0,
        static_cast<double>(kMaxForeshortenSliderStrength));
    const double slider_distance_strength = std::clamp(
        static_cast<double>(runtime_distance_scale_strength_),
        0.0,
        static_cast<double>(kMaxDistanceSliderStrength));

    const double base_x = static_cast<double>(smoothed_center_.x);
    const double base_y = anchor_world_y();
    const double base_h = static_cast<double>(std::max(1, height_from_area(base_zoom_)));

    const double dx = static_cast<double>(world.x) - base_x;
    const double dy = base_y - static_cast<double>(world.y);

    // View depth factor: stronger depth cues at higher camera and stronger pitch.
    const double view_depth_factor =
        (camera_height > EPS)
            ? std::clamp(
                  (camera_height / (camera_height + base_h)) *
                      (0.25 + 0.75 * pitch_factor),
                  0.0, 1.0)
            : 0.0;

    // Vertical squash from perspective.
    {
        const double base_foreshorten_strength =
            std::clamp(camera_height / (camera_height + height_reference), 0.0, 1.0);
        const double effective_foreshorten_strength = base_foreshorten_strength * slider_vertical_strength;
        if (effective_foreshorten_strength > EPS) {
            const double ref_h = (reference_screen_height > EPS) ? reference_screen_height : 1.0;

            // Bias: things lower on the screen are closer.
            const double screen_bias = 0.5 + 0.5 * std::tanh(dy / SCREEN_Y_SCALE);

            const double depth_norm = std::clamp(std::abs(dy) / (camera_height + height_reference), 0.0, 1.0);

            double squash_base = effective_foreshorten_strength *
                                 view_depth_factor *
                                 screen_bias *
                                 depth_norm;

            // Near assets squash more; upward tilt reduces squash.
            squash_base *= (0.6 + 0.4 * depth_norm);
            squash_base *= (1.0 - 0.5 * tilt_up);

            const double height_factor = std::sqrt(
                static_cast<double>(asset_screen_height) / ref_h
            );
            const double squash_height = squash_base * height_factor;

            const double squash = SQUASH_BASE_WT * squash_base +
                                  SQUASH_HEIGHT_WT * squash_height;

            const double new_vertical_scale = std::clamp(1.0 - squash, 0.1, 1.0);
            result.vertical_scale = static_cast<float>(new_vertical_scale);
        }
    }

    // Distance based scaling: objects further away in depth (dy) appear smaller.
    {
        double distance_strength = view_depth_factor;
        // More extreme distance scaling when tilting upward (seeing farther).
        distance_strength *= (1.0 + tilt_up);
        distance_strength *= slider_distance_strength;
        if (distance_strength > 0.0) {
            // Only use vertical depth from the camera anchor, ignore lateral offset.
            const double depth_abs = std::abs(dy);

            const double depth_norm   = depth_abs / (DEPTH_RANGE_PIXELS + EPS);
            const double depth_weight = 1.0 + depth_norm;

            // Effective depth that grows slightly faster than linear with distance.
            const double r_weighted = depth_abs * depth_weight + EPS;

            const double base_scale =
                std::sqrt(
                    (camera_height + R_REF) /
                    (camera_height + r_weighted + EPS)
                );

            double distance_scale = 1.0 + (base_scale - 1.0) * distance_strength;

            // Push far (top) assets to shrink more when looking upward.
            double horizon_y = horizon_screen_y_for_scale();
            double depth_screen = 0.0;
            if (screen_height_ > 0.0) {
                depth_screen = std::clamp(
                    (result.screen_position.y - static_cast<float>(horizon_y)) /
                    std::max(1.0f, static_cast<float>(screen_height_) - static_cast<float>(horizon_y)),
                    0.0f, 1.0f);
            }
            const double far_boost = 1.0 + tilt_up * (1.0 - depth_screen);
            distance_scale = 1.0 + (distance_scale - 1.0) * far_boost;

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

    const auto try_read_optional_float = [&](const char* key, std::optional<float>& target) {
        auto it = data.find(key);
        if (it == data.end()) return;
        if (it->is_null()) {
            target.reset();
            return;
        }
        if (it->is_number_float()) {
            target = static_cast<float>(it->get<double>());
            return;
        }
        if (it->is_number_integer()) {
            target = static_cast<float>(it->get<int>());
        }
    };

    auto realism_it = data.find("realism_enabled");
    if (realism_it != data.end()) {
        if (realism_it->is_boolean()) {
            realism_enabled_ = realism_it->get<bool>();
        } else if (realism_it->is_number_integer()) {
            realism_enabled_ = realism_it->get<int>() != 0;
        }
    }

    try_read_float("foreshorten_strength",       settings_.foreshorten_strength);
    try_read_float("distance_scale_strength",    settings_.distance_scale_strength);
    bool foreshorten_low_seen  = try_read_float("foreshorten_at_zoom_low", settings_.foreshorten_at_zoom_low);
    bool foreshorten_high_seen = try_read_float("foreshorten_at_zoom_high", settings_.foreshorten_at_zoom_high);
    bool distance_low_seen     = try_read_float("distance_scale_at_zoom_low", settings_.distance_scale_at_zoom_low);
    bool distance_high_seen    = try_read_float("distance_scale_at_zoom_high", settings_.distance_scale_at_zoom_high);
    try_read_float("zoom_low",                    settings_.zoom_low);
    try_read_float("zoom_high",                   settings_.zoom_high);
    const bool base_height_read = try_read_float("base_height_px",         settings_.base_height_px);
    float legacy_height_low  = settings_.base_height_px;
    float legacy_height_high = settings_.base_height_px;
    const bool legacy_low_seen  = try_read_float("height_low_px",  legacy_height_low);
    const bool legacy_high_seen = try_read_float("height_high_px", legacy_height_high);
    try_read_float("min_visible_screen_ratio",   settings_.min_visible_screen_ratio);

    // Grid depth / pitch controls (strength was deprecated).
    const bool tilt_in_seen  = try_read_float("tilt_zoom_in_degrees",  settings_.tilt_zoom_in_degrees);
    const bool tilt_out_seen = try_read_float("tilt_zoom_out_degrees", settings_.tilt_zoom_out_degrees);
    float legacy_pitch = settings_.tilt_zoom_out_degrees;
    const bool legacy_pitch_seen    = try_read_float("grid_pitch_degrees", legacy_pitch);
    try_read_float("grid_depth_offset_px",       settings_.grid_depth_offset_px);
    bool depth_low_seen  = try_read_float("depth_offset_at_zoom_low", settings_.depth_offset_at_zoom_low);
    bool depth_high_seen = try_read_float("depth_offset_at_zoom_high", settings_.depth_offset_at_zoom_high);
    try_read_optional_float("horizon_y_at_zoom_low",  settings_.horizon_y_at_zoom_low);
    try_read_optional_float("horizon_y_at_zoom_high", settings_.horizon_y_at_zoom_high);

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
    try_read_smoothing_method("motion_smoothing_method",   settings_.motion_smoothing_method);
    try_read_float("parallax_smoothing_lerp_rate",         settings_.parallax_smoothing.lerp_rate);
    try_read_float("parallax_smoothing_spring_frequency",  settings_.parallax_smoothing.spring_frequency);
    try_read_float("parallax_smoothing_max_step",          settings_.parallax_smoothing.max_step);
    try_read_float("parallax_smoothing_snap_threshold",    settings_.parallax_smoothing.snap_threshold);
    try_read_float("motion_smoothing_tau",                 settings_.motion_smoothing_tau);
    try_read_float("motion_smoothing_spring_frequency",    settings_.motion_smoothing_spring_frequency);
    try_read_float("motion_smoothing_max_step",            settings_.motion_smoothing_max_step);
    try_read_float("motion_smoothing_snap_threshold",      settings_.motion_smoothing_snap_threshold);
    try_read_float("scale_hysteresis_margin",              settings_.scale_variant_hysteresis_margin);

    auto try_read_opacity = [&](const char* key, int& target) -> bool {
        auto it = data.find(key);
        if (it == data.end()) return false;
        if (it->is_number_integer()) {
            target = it->get<int>();
        } else if (it->is_number_float()) {
            target = static_cast<int>(std::lround(it->get<double>()));
        } else {
            return false;
        }
        target = std::clamp(target, 0, 255);
        return true;
    };
    try_read_opacity("foreground_texture_max_opacity", settings_.foreground_texture_max_opacity);
    try_read_opacity("background_texture_max_opacity", settings_.background_texture_max_opacity);

    try_read_float("foreground_plane_screen_y", settings_.foreground_plane_screen_y);
    try_read_float("background_plane_screen_y", settings_.background_plane_screen_y);

    auto try_read_curve = [&](const char* key, BlurFalloffMethod& target) -> bool {
        auto it = data.find(key);
        if (it == data.end() || !it->is_number_integer()) return false;
        int raw = it->get<int>();
        raw = std::clamp(raw, 0, 4);
        target = static_cast<BlurFalloffMethod>(raw);
        return true;
    };
    if (!try_read_curve("texture_opacity_falloff_method", settings_.texture_opacity_falloff_method)) {
        settings_.texture_opacity_falloff_method = BlurFalloffMethod::Linear;
    }

    settings_.foreground_texture_max_opacity =
        std::clamp(settings_.foreground_texture_max_opacity, 0, 255);
    settings_.background_texture_max_opacity =
        std::clamp(settings_.background_texture_max_opacity, 0, 255);
    auto sanitize_horizon = [](std::optional<float>& v) {
        if (v && !std::isfinite(*v)) {
            v.reset();
        }
    };
    sanitize_horizon(settings_.horizon_y_at_zoom_low);
    sanitize_horizon(settings_.horizon_y_at_zoom_high);

    if (!std::isfinite(settings_.foreground_plane_screen_y)) {
        settings_.foreground_plane_screen_y = 1080.0f;
    } else {
        settings_.foreground_plane_screen_y =
            std::clamp(settings_.foreground_plane_screen_y, 0.0f, 4000.0f);
    }

    if (!std::isfinite(settings_.background_plane_screen_y)) {
        settings_.background_plane_screen_y = 0.0f;
    } else {
        settings_.background_plane_screen_y =
            std::clamp(settings_.background_plane_screen_y, 0.0f, 4000.0f);
    }

    settings_.foreshorten_strength = clamp_foreshorten(settings_.foreshorten_strength);
    if (!foreshorten_low_seen) {
        settings_.foreshorten_at_zoom_low = settings_.foreshorten_strength;
    }
    if (!foreshorten_high_seen) {
        settings_.foreshorten_at_zoom_high = settings_.foreshorten_strength;
    }
    settings_.foreshorten_at_zoom_low = clamp_foreshorten(settings_.foreshorten_at_zoom_low);
    settings_.foreshorten_at_zoom_high = clamp_foreshorten(settings_.foreshorten_at_zoom_high);
    settings_.foreshorten_strength =
        0.5f * (settings_.foreshorten_at_zoom_low + settings_.foreshorten_at_zoom_high);

    settings_.distance_scale_strength = clamp_distance_strength(settings_.distance_scale_strength);
    if (!distance_low_seen) {
        settings_.distance_scale_at_zoom_low = settings_.distance_scale_strength;
    }
    if (!distance_high_seen) {
        settings_.distance_scale_at_zoom_high = settings_.distance_scale_strength;
    }
    settings_.distance_scale_at_zoom_low = clamp_distance_strength(settings_.distance_scale_at_zoom_low);
    settings_.distance_scale_at_zoom_high = clamp_distance_strength(settings_.distance_scale_at_zoom_high);
    settings_.distance_scale_strength =
        0.5f * (settings_.distance_scale_at_zoom_low + settings_.distance_scale_at_zoom_high);

    if (!std::isfinite(settings_.zoom_low)) {
        settings_.zoom_low = 0.75f;
    }

    if (!std::isfinite(settings_.zoom_high)) {
        settings_.zoom_high = std::max(settings_.zoom_low + 0.25f, 1.0f);
    }

    if (!base_height_read) {
        if (legacy_low_seen && legacy_high_seen) {
            settings_.base_height_px = 0.5f * (legacy_height_low + legacy_height_high);
        } else if (legacy_low_seen) {
            settings_.base_height_px = legacy_height_low;
        } else if (legacy_high_seen) {
            settings_.base_height_px = legacy_height_high;
        }
    }
    if (!std::isfinite(settings_.base_height_px) || settings_.base_height_px <= 0.0f) {
        settings_.base_height_px = 720.0f;
    }

    if (legacy_pitch_seen) {
        if (!tilt_in_seen) {
            settings_.tilt_zoom_in_degrees = legacy_pitch;
        }
        if (!tilt_out_seen) {
            settings_.tilt_zoom_out_degrees = legacy_pitch;
        }
    }
    if (!std::isfinite(settings_.tilt_zoom_in_degrees)) {
        settings_.tilt_zoom_in_degrees = 345.0f;
    }
    if (!std::isfinite(settings_.tilt_zoom_out_degrees)) {
        settings_.tilt_zoom_out_degrees = 310.0f;
    }
    settings_.tilt_zoom_in_degrees  = sanitize_pitch_degrees(settings_.tilt_zoom_in_degrees);
    settings_.tilt_zoom_out_degrees = sanitize_pitch_degrees(settings_.tilt_zoom_out_degrees);

    settings_.grid_depth_offset_px = clamp_depth_offset(settings_.grid_depth_offset_px);
    if (!depth_low_seen) {
        settings_.depth_offset_at_zoom_low = settings_.grid_depth_offset_px;
    }
    if (!depth_high_seen) {
        settings_.depth_offset_at_zoom_high = settings_.grid_depth_offset_px;
    }
    settings_.depth_offset_at_zoom_low = clamp_depth_offset(settings_.depth_offset_at_zoom_low);
    settings_.depth_offset_at_zoom_high = clamp_depth_offset(settings_.depth_offset_at_zoom_high);
    settings_.grid_depth_offset_px =
        0.5f * (settings_.depth_offset_at_zoom_low + settings_.depth_offset_at_zoom_high);

    if (!std::isfinite(settings_.min_visible_screen_ratio) ||
        settings_.min_visible_screen_ratio < 0.0f) {
        settings_.min_visible_screen_ratio = 0.015f;
    } else {
        settings_.min_visible_screen_ratio =
            std::clamp(settings_.min_visible_screen_ratio, 0.0f, 0.5f);
    }

    settings_.zoom_low = std::clamp(settings_.zoom_low,
                                    camera::kMinZoomAnchors,
                                    camera::kMaxZoomAnchors);
    const float min_high = std::min(camera::kMaxZoomAnchors, settings_.zoom_low + 0.0001f);
    settings_.zoom_high = std::clamp(settings_.zoom_high, min_high, camera::kMaxZoomAnchors);

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

    settings_.smooth_motion_zoom =
        settings_.smooth_motion_zoom &&
        settings_.motion_smoothing_method != TransformSmoothingMethod::None;

    if (!std::isfinite(settings_.motion_smoothing_tau) ||
        settings_.motion_smoothing_tau < 0.0f) {
        settings_.motion_smoothing_tau = 0.0f;
    }
    if (!std::isfinite(settings_.motion_smoothing_max_step) ||
        settings_.motion_smoothing_max_step < 0.0f) {
        settings_.motion_smoothing_max_step = 0.0f;
    }
    if (!std::isfinite(settings_.motion_smoothing_snap_threshold) ||
        settings_.motion_smoothing_snap_threshold < 0.0f) {
        settings_.motion_smoothing_snap_threshold = 0.0f;
    }
    if (!std::isfinite(settings_.motion_smoothing_spring_frequency) ||
        settings_.motion_smoothing_spring_frequency < 0.0f) {
        settings_.motion_smoothing_spring_frequency = 0.0f;
    }
    if (!std::isfinite(settings_.scale_variant_hysteresis_margin) ||
        settings_.scale_variant_hysteresis_margin < 0.0f) {
        settings_.scale_variant_hysteresis_margin = 0.05f;
    }
    TransformSmoothingParams motion_params = motion_params_from_settings(settings_);
    center_smoothing_x_.set_params(motion_params);
    center_smoothing_y_.set_params(motion_params);
    zoom_smoothing_.set_params(motion_params);
    update_geometry_cache(compute_geometry());
}

nlohmann::json camera::camera_settings_to_json() const {
    nlohmann::json j = nlohmann::json::object();
    j["realism_enabled"]                 = realism_enabled_;
    j["foreshorten_strength"]            = settings_.foreshorten_strength;
    j["distance_scale_strength"]         = settings_.distance_scale_strength;
    j["foreshorten_at_zoom_low"]         = settings_.foreshorten_at_zoom_low;
    j["foreshorten_at_zoom_high"]        = settings_.foreshorten_at_zoom_high;
    j["distance_scale_at_zoom_low"]      = settings_.distance_scale_at_zoom_low;
    j["distance_scale_at_zoom_high"]     = settings_.distance_scale_at_zoom_high;
    j["depth_offset_at_zoom_low"]        = settings_.depth_offset_at_zoom_low;
    j["depth_offset_at_zoom_high"]       = settings_.depth_offset_at_zoom_high;
    j["zoom_low"]                        = settings_.zoom_low;
    j["zoom_high"]                       = settings_.zoom_high;
    j["base_height_px"]                  = settings_.base_height_px;
    j["tilt_zoom_in_degrees"]            = settings_.tilt_zoom_in_degrees;
    j["tilt_zoom_out_degrees"]           = settings_.tilt_zoom_out_degrees;
    if (settings_.horizon_y_at_zoom_low.has_value()) {
        j["horizon_y_at_zoom_low"] = *settings_.horizon_y_at_zoom_low;
    }
    if (settings_.horizon_y_at_zoom_high.has_value()) {
        j["horizon_y_at_zoom_high"] = *settings_.horizon_y_at_zoom_high;
    }
    j["min_visible_screen_ratio"]        = settings_.min_visible_screen_ratio;
    j["render_quality_percent"]          = settings_.render_quality_percent;
    j["smooth_motion_zoom"]              = settings_.smooth_motion_zoom;
    j["motion_smoothing_method"]         = static_cast<int>(settings_.motion_smoothing_method);
    j["motion_smoothing_tau"]            = settings_.motion_smoothing_tau;
    j["motion_smoothing_spring_frequency"] = settings_.motion_smoothing_spring_frequency;
    j["motion_smoothing_max_step"]       = settings_.motion_smoothing_max_step;
    j["motion_smoothing_snap_threshold"] = settings_.motion_smoothing_snap_threshold;
    j["scale_hysteresis_margin"]         = settings_.scale_variant_hysteresis_margin;
    j["parallax_smoothing_method"]       = static_cast<int>(settings_.parallax_smoothing.method);
    j["parallax_smoothing_lerp_rate"]    = settings_.parallax_smoothing.lerp_rate;
    j["parallax_smoothing_spring_frequency"] = settings_.parallax_smoothing.spring_frequency;
    j["parallax_smoothing_max_step"]     = settings_.parallax_smoothing.max_step;
    j["parallax_smoothing_snap_threshold"] = settings_.parallax_smoothing_snap_threshold;

    // Depth cue texture blending
    j["foreground_texture_max_opacity"]  = settings_.foreground_texture_max_opacity;
    j["background_texture_max_opacity"]  = settings_.background_texture_max_opacity;
    j["foreground_plane_screen_y"]       = settings_.foreground_plane_screen_y;
    j["background_plane_screen_y"]       = settings_.background_plane_screen_y;
    j["texture_opacity_falloff_method"]  =
        static_cast<int>(settings_.texture_opacity_falloff_method);

    j["grid_depth_offset_px"]            = settings_.grid_depth_offset_px;

    return j;
}

TransformSmoothingParams camera::motion_smoothing_params() const {
    return motion_params_from_settings(settings_);
}

SDL_FPoint camera::get_view_center_f() const {
    if (std::isfinite(smoothed_center_.x) && std::isfinite(smoothed_center_.y)) {
        return smoothed_center_;
    }
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const float cx = (static_cast<float>(left) + static_cast<float>(right)) * 0.5f;
    const float cy = (static_cast<float>(top)  + static_cast<float>(bottom)) * 0.5f;
    return SDL_FPoint{ cx, cy };
}

// Floor depth helpers for warped grid lines using actual camera height (from zoom) and pitch.


camera::FloorDepthParams camera::compute_floor_depth_params_for_geometry(const CameraGeometry& geom, double scale_value) const {
    FloorDepthParams p{};
    if (!realism_enabled_ || !geom.valid) {
        return p;
    }

    const double screen_h = static_cast<double>(screen_height_);
    if (screen_h <= 1.0) {
        return p;
    }

    const double top_margin     = std::max(12.0, screen_h * 0.05);
    const double min_horizon    = top_margin + 1.0;

    const double tan_fov = std::tan(HALF_FOV_Y);
    const double tan_pitch = std::tan(geom.pitch_radians);
    double pitch_norm = geom.pitch_radians / (PI_D / 3.0);
    pitch_norm = std::clamp(pitch_norm, -1.0, 1.0);
    if (pitch_norm < 0.0 && pitch_norm > -1e-4) {
        pitch_norm = 0.0;
    }

    double horizon_ndc = (tan_pitch / tan_fov) - geom.focus_ndc_offset;
    horizon_ndc = std::clamp(horizon_ndc, -10.0, 10.0);
    double horizon_y = screen_h * (0.5 - 0.5 * horizon_ndc);
    horizon_y = std::max(min_horizon, horizon_y);

    horizon_y = apply_horizon_override(horizon_y, scale_value, screen_h);

    // Recompute the NDC focus offset so the warped grid tracks the chosen horizon.
    double horizon_ndc_target = 1.0 - 2.0 * (horizon_y / screen_h);
    horizon_ndc_target = std::clamp(horizon_ndc_target, -10.0, 10.0);
    const double focus_ndc_offset = (tan_pitch / tan_fov) - horizon_ndc_target;

    p.horizon_screen_y     = horizon_y;
    p.bottom_screen_y      = screen_h;
    p.base_world_y         = anchor_world_y();
    p.camera_height        = geom.camera_height;
    p.pitch_radians        = geom.pitch_radians;
    p.pitch_norm           = pitch_norm;
    p.focus_ndc_offset     = focus_ndc_offset;
    p.strength             = 1.0;
    p.enabled              = true;

    return p;
}

camera::FloorDepthParams camera::compute_floor_depth_params_for_scale(double scale_value) const {
    const CameraGeometry geom = compute_geometry_for_scale(scale_value);
    return compute_floor_depth_params_for_geometry(geom, scale_value);
}

camera::FloorDepthParams camera::compute_floor_depth_params() const {
    const CameraGeometry geom = compute_geometry();
    return compute_floor_depth_params_for_geometry(geom, static_cast<double>(smoothed_scale_));
}

double camera::apply_horizon_override(double base_horizon_y, double scale_value, double screen_height) const {
    const bool has_low  = settings_.horizon_y_at_zoom_low.has_value();
    const bool has_high = settings_.horizon_y_at_zoom_high.has_value();
    if (!has_low && !has_high) {
        return base_horizon_y;
    }

    const double low_value  = has_low  ? static_cast<double>(*settings_.horizon_y_at_zoom_low)  : base_horizon_y;
    const double high_value = has_high ? static_cast<double>(*settings_.horizon_y_at_zoom_high) : base_horizon_y;

    const double t = zoom_lerp_t_for_scale(scale_value);
    double target = low_value + (high_value - low_value) * t;

    // Keep within a generous screen-space range to avoid runaway values.
    const double extent    = std::max(1.0, screen_height);
    const double min_bound = -2.0 * extent;
    const double max_bound = 3.0 * extent;
    target = std::clamp(target, min_bound, max_bound);
    return target;
}
float camera::warp_floor_screen_y(float world_y, float linear_screen_y) const {
    FloorDepthParams p = runtime_floor_params_;
    if (!p.enabled) {
        // Fallback to a fresh computation if the cache is empty (e.g., before the first update).
        p = compute_floor_depth_params();
    }
    if (!p.enabled) {
        // No pitch or realism disabled, keep the original linear mapping.
        return linear_screen_y;
    }

    const double screen_h = static_cast<double>(screen_height_);
    const double cos_p = std::cos(p.pitch_radians);
    const double sin_p = std::sin(p.pitch_radians);
    const double tan_fov = std::tan(HALF_FOV_Y);

    const double depth_world = p.base_world_y - static_cast<double>(world_y);
    const double y_cam = depth_world * cos_p + p.camera_height * sin_p;
    const double z_cam = depth_world * sin_p - p.camera_height * cos_p;
    double forward = -z_cam;

    if (forward < 1e-6) {
        // Clamp to the projected horizon when the point lies beyond the viewing direction.
        return static_cast<float>(p.horizon_screen_y);
    }

    double y_ndc = (y_cam / forward) / tan_fov;
    y_ndc -= p.focus_ndc_offset;

    const double screen_y = screen_h * (0.5 - 0.5 * y_ndc);
    double clamped = std::clamp(screen_y, 0.0, std::max(1.0, screen_h));

    // Compress spacing near the horizon so distant (top) assets cluster more when looking upward.
    const double denom = std::max(1.0, p.bottom_screen_y - p.horizon_screen_y);
    const double depth_t = std::clamp((clamped - p.horizon_screen_y) / denom, 0.0, 1.0);
    const double pitch_norm = std::clamp(std::abs(p.pitch_radians) / (PI_D / 3.0), 0.0, 1.0);
    const double tilt_up = 1.0 - pitch_norm; // more when tilting upward (toward level)
    const double compression = 1.0 + tilt_up * (1.0 - depth_t) * 0.35; // stronger near horizon
    clamped = p.horizon_screen_y + (clamped - p.horizon_screen_y) / compression;

    return static_cast<float>(clamped);
}

double camera::view_height_world() const {
    int minx = 0, miny = 0, maxx = 0, maxy = 0;
    std::tie(minx, miny, maxx, maxy) = current_view_.get_bounds();
    return static_cast<double>(std::max(0, maxy - miny));
}

double camera::anchor_world_y() const {
    // Anchor at the camera focus to keep depth ordering stable and avoid inversion.
    return static_cast<double>(smoothed_center_.y);
}

double camera::zoom_lerp_t_for_scale(double scale_value) const {
    const double low_zoom  = std::max(static_cast<double>(camera::kMinZoomAnchors),
                                      static_cast<double>(settings_.zoom_low));
    const double high_zoom = std::max(low_zoom + 1e-4, static_cast<double>(settings_.zoom_high));
    const double t_raw     = (scale_value - low_zoom) / std::max(1e-4, high_zoom - low_zoom);
    return std::clamp(t_raw, 0.0, 1.0);
}

float camera::foreshorten_for_scale(double scale_value) const {
    const double t = zoom_lerp_t_for_scale(scale_value);
    const double value = lerp(
        static_cast<double>(settings_.foreshorten_at_zoom_low),
        static_cast<double>(settings_.foreshorten_at_zoom_high),
        t);
    return clamp_foreshorten(static_cast<float>(value));
}

float camera::distance_scale_for_scale(double scale_value) const {
    const double t = zoom_lerp_t_for_scale(scale_value);
    const double value = lerp(
        static_cast<double>(settings_.distance_scale_at_zoom_low),
        static_cast<double>(settings_.distance_scale_at_zoom_high),
        t);
    return clamp_distance_strength(static_cast<float>(value));
}

float camera::depth_offset_for_scale(double scale_value) const {
    const double t = zoom_lerp_t_for_scale(scale_value);
    const double value = lerp(
        static_cast<double>(settings_.depth_offset_at_zoom_low),
        static_cast<double>(settings_.depth_offset_at_zoom_high),
        t);
    return clamp_depth_offset(static_cast<float>(value));
}

double camera::horizon_screen_y_for_scale_value(double scale_value) const {
    if (screen_height_ <= 0) {
        return 0.0;
    }

    const double cached_scale = static_cast<double>(smoothed_scale_);
    const double kScaleEps = 1e-6;
    if (std::abs(scale_value - cached_scale) <= kScaleEps && runtime_floor_params_.enabled) {
        const double extent    = static_cast<double>(screen_height_);
        const double min_bound = -2.0 * extent;
        const double max_bound = 3.0 * extent;
        return std::clamp(runtime_floor_params_.horizon_screen_y, min_bound, max_bound);
    }

    const FloorDepthParams depth = compute_floor_depth_params_for_scale(scale_value);
    if (!depth.enabled) {
        return static_cast<double>(screen_height_) * 0.5;
    }

    const double extent    = static_cast<double>(screen_height_);
    const double min_bound = -2.0 * extent;
    const double max_bound = 3.0 * extent;
    return std::clamp(depth.horizon_screen_y, min_bound, max_bound);
}

double camera::horizon_screen_y_for_scale() const {
    return horizon_screen_y_for_scale_value(static_cast<double>(smoothed_scale_));
}
