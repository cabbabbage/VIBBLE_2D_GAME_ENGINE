#include "warped_screen_grid.hpp"

#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "map_generation/room.hpp"
#include "core/find_current_room.hpp"
#include "utils/log.hpp"
#include "world/world_grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>
#include <tuple>
#include <string>
#include <limits>
#include <nlohmann/json.hpp>

template <typename T>
T lerp(T a, T b, double t) {
    return static_cast<T>(a + (b - a) * t);
}

namespace {
    constexpr float  kMinTau    = 1e-4f;
    constexpr double SCALE_EPS  = 1e-4;
    constexpr double BASE_RATIO = 1.1;
    constexpr double PI_D       = 3.14159265358979323846;
    constexpr double kHalfFovY  = PI_D / 4.0;
    constexpr double kBottomAngleLimit = (PI_D * 0.5) - 1e-3;
    constexpr float  kDefaultPitchDegrees   = 60.0f;
    constexpr double kMinZoomRange = 1e-4;
    constexpr double kMinPerspectiveScale   = 0.35;
    constexpr double kMaxPerspectiveScale   = 1.65;
    struct ZoomInterpolator {
        double t = 0.0;
        ZoomInterpolator(const WarpedScreenGrid::RealismSettings& settings, double scale_value) {
            const double safe_low = std::max(static_cast<double>(WarpedScreenGrid::kMinZoomAnchors), static_cast<double>(settings.zoom_low));
            const double safe_high = std::max(safe_low + kMinZoomRange, static_cast<double>(settings.zoom_high));
            const double span = std::max(kMinZoomRange, safe_high - safe_low);
            t = std::clamp((scale_value - safe_low) / span, 0.0, 1.0);
        }

        template <typename V>
        V lerp(V low, V high) const {
            return ::lerp(low, high, t);
        }
};

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

    double shortest_delta_degrees(double from_deg, double to_deg);

    double lerp_angle(double from_deg, double to_deg, double t) {
        const double delta = shortest_delta_degrees(from_deg, to_deg);
        return wrap_degrees_0_360(from_deg + delta * t);
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
        const float wrapped = wrap_degrees_0_360(std::isfinite(raw_value) ? raw_value : kDefaultPitchDegrees);
        const float clamped_value = std::clamp( wrapped, WarpedScreenGrid::kMinPitchDegrees, WarpedScreenGrid::kMaxPitchDegrees);
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
        return std::clamp( value, 0.0001, static_cast<double>(WarpedScreenGrid::kMaxZoomAnchors));
    }

    double camera_height_from_scale(const WarpedScreenGrid::RealismSettings& settings, double scale_value) {
        const double base_height = std::max(1.0, static_cast<double>(settings.base_height_px));
        return std::max(0.0, base_height * scale_value);
    }

    double solve_pitch_for_fixed_depth(double camera_height,
                                       double desired_depth_world,
                                       double default_pitch_rad) {
        if (!std::isfinite(camera_height) || camera_height <= 0.0) {
            return default_pitch_rad;
        }
        if (!std::isfinite(desired_depth_world) || desired_depth_world <= 0.0) {
            return default_pitch_rad;
        }

        const double min_pitch_rad = std::max(1e-4, static_cast<double>(WarpedScreenGrid::kMinPitchDegrees) * (PI_D / 180.0));
        const double max_pitch_rad = std::min( static_cast<double>(WarpedScreenGrid::kMaxPitchDegrees) * (PI_D / 180.0), kBottomAngleLimit - 1e-4);

        double low = min_pitch_rad;
        double high = std::max(low + 1e-4, max_pitch_rad);

        const auto depth_span = [&](double pitch) -> double {
            const double clamped_pitch = std::clamp(pitch, min_pitch_rad, high);
            const double tan_center = std::tan(clamped_pitch);
            if (!std::isfinite(tan_center) || std::abs(tan_center) < 1e-6) {
                return std::numeric_limits<double>::infinity();
            }
            const double center_depth = camera_height / tan_center;

            const double bottom_angle = std::min(kBottomAngleLimit, clamped_pitch + kHalfFovY);
            const double tan_bottom = std::tan(bottom_angle);
            if (!std::isfinite(tan_bottom) || std::abs(tan_bottom) < 1e-6) {
                return std::numeric_limits<double>::infinity();
            }
            const double bottom_depth = camera_height / tan_bottom;
            return center_depth - bottom_depth;
};

        const double desired = std::max(0.0, desired_depth_world);
        double span_low = depth_span(low);
        double span_high = depth_span(high);
        if (!std::isfinite(span_low) || !std::isfinite(span_high)) {
            return std::clamp(default_pitch_rad, low, high);
        }

        if (desired >= span_low) {
            return low;
        }
        if (desired <= span_high) {
            return high;
        }

        for (int i = 0; i < 48; ++i) {
            const double mid = 0.5 * (low + high);
            const double span_mid = depth_span(mid);
            if (!std::isfinite(span_mid)) {
                high = mid;
                continue;
            }
            if (span_mid > desired) {
                low = mid;
            } else {
                high = mid;
            }
        }

        return std::clamp(high, low, max_pitch_rad);
    }

    WarpedScreenGrid::CameraGeometry build_geometry(const WarpedScreenGrid::RealismSettings& settings,
                                               double scale_value,
                                               double anchor_world_y,
                                               double desired_depth_world,
                                               bool realism_enabled) {
        WarpedScreenGrid::CameraGeometry g{};
        if (!realism_enabled) {
            return g;
        }

        const double clamped_scale = std::max(0.0001, scale_value);
        g.camera_height = camera_height_from_scale(settings, clamped_scale);
        if (g.camera_height <= 0.0) {
            return g;
        }

        const double default_pitch_deg = kDefaultPitchDegrees;
        const double default_pitch_rad = signed_radians_from_degrees(default_pitch_deg);
        const double solved_pitch_rad = solve_pitch_for_fixed_depth( g.camera_height, desired_depth_world, default_pitch_rad);

        const double solved_pitch_deg = solved_pitch_rad * (180.0 / PI_D);
        const float sanitized_deg = sanitize_pitch_degrees(static_cast<float>(solved_pitch_deg));
        g.pitch_degrees = sanitized_deg;
        g.pitch_radians = signed_radians_from_degrees(static_cast<double>(sanitized_deg));

        const double tan_pitch = std::tan(g.pitch_radians);
        if (!std::isfinite(tan_pitch) || std::abs(tan_pitch) < 1e-6) {
            return g;
        }

        g.anchor_world_y = anchor_world_y;
        if (!std::isfinite(g.anchor_world_y)) {
            return g;
        }

        g.focus_depth   = g.camera_height / tan_pitch;
        g.camera_world_y = g.anchor_world_y - g.focus_depth;
        g.focus_ndc_offset = 0.0;

        g.valid = std::isfinite(g.camera_world_y) && std::isfinite(g.focus_depth);
        return g;
    }

    WarpedScreenGrid::FloorDepthParams build_floor_params(
        const WarpedScreenGrid::RealismSettings& settings,
        int screen_height,
        const WarpedScreenGrid::CameraGeometry& geom,
        double scale_value,
        bool realism_enabled) {
        WarpedScreenGrid::FloorDepthParams p{};
        (void)scale_value;
        if (!realism_enabled || !geom.valid) {
            return p;
        }

        const double screen_h = std::max(1.0, static_cast<double>(screen_height));
        if (!std::isfinite(geom.camera_height) ||
            !std::isfinite(geom.pitch_radians) ||
            !std::isfinite(geom.camera_world_y) ||
            !std::isfinite(geom.anchor_world_y)) {
            return p;
        }

        constexpr double kMaxHorizonRatio = 0.45;
        const double max_horizon = screen_h * kMaxHorizonRatio;
        const double min_horizon = -screen_h * 4.0;

        const double tan_fov   = std::tan(kHalfFovY);
        const double tan_pitch = std::tan(geom.pitch_radians);
        if (!std::isfinite(tan_fov) || !std::isfinite(tan_pitch) || std::abs(tan_fov) < 1e-6) {
            return p;
        }

        const double max_phi = (PI_D * 0.5) - 1e-3;
        double phi_bottom = geom.pitch_radians + kHalfFovY;
        phi_bottom = std::clamp(phi_bottom, 1e-3, max_phi);

        const double ndc_bottom_raw = std::tan(geom.pitch_radians - phi_bottom) / tan_fov;
        const double ndc_scale = (std::isfinite(ndc_bottom_raw) && ndc_bottom_raw < -1e-4) ? (-1.0 / ndc_bottom_raw) : 1.0;
        double near_ndc = ndc_bottom_raw * ndc_scale;
        if (!std::isfinite(near_ndc)) {
            near_ndc = -1.0;
        }

        const double horizon_ndc_raw = tan_pitch / tan_fov;
        if (!std::isfinite(horizon_ndc_raw)) {
            return p;
        }
        const double horizon_ndc = horizon_ndc_raw * ndc_scale;
        double horizon_y = screen_h * (0.5 - 0.5 * horizon_ndc);
        horizon_y = std::clamp(horizon_y, min_horizon, max_horizon);

        double pitch_norm = geom.pitch_radians / (kHalfFovY * 2.0);
        pitch_norm = std::clamp(pitch_norm, 0.0, 1.0);

        p.horizon_screen_y = horizon_y;
        p.bottom_screen_y  = screen_h;
        p.base_world_y     = geom.anchor_world_y;
        p.camera_world_y   = geom.camera_world_y;
        p.camera_height    = geom.camera_height;
        p.pitch_radians    = geom.pitch_radians;
        p.pitch_norm       = pitch_norm;
        p.focus_ndc_offset = 0.0;
        p.horizon_ndc      = horizon_ndc;
        p.near_ndc         = near_ndc;
        p.ndc_scale        = ndc_scale;
        p.strength         = 6.0;
        p.enabled          = true;
        (void)settings;
        return p;
    }

    float warp_floor_screen_y_internal(
        float world_y,
        float linear_screen_y,
        const WarpedScreenGrid::FloorDepthParams& p,
        int screen_height) {
        if (!p.enabled ||
            !std::isfinite(p.horizon_screen_y) ||
            !std::isfinite(p.bottom_screen_y) ||
            !std::isfinite(p.camera_height) ||
            !std::isfinite(p.pitch_radians)) {
            return std::isfinite(linear_screen_y) ? linear_screen_y : 0.0f;
        }

        const double safe_linear_y = std::isfinite(linear_screen_y) ? static_cast<double>(linear_screen_y) : 0.0;

        const double horizon = p.horizon_screen_y;
        const double bottom  = p.bottom_screen_y;
        if (!std::isfinite(horizon) || !std::isfinite(bottom) || bottom <= horizon + 1e-3) {
            return static_cast<float>(safe_linear_y);
        }

        const double range = bottom - horizon;
        double t_linear = (safe_linear_y - horizon) / range;

        const double pitch = std::clamp(p.pitch_radians, 0.0, (PI_D / 4.0) * 2.0);
        const double base_strength = 0.5 + 0.5 * (pitch / ((PI_D / 4.0) * 2.0));
        const double strength = std::clamp(p.strength, 0.0, 10.0);
        const double k = 1.0 + strength * base_strength;

        double t_warp;
        if (t_linear >= 0.0) {
            t_warp = std::pow(t_linear, k);
        } else {
            const double pitch_factor = std::clamp(pitch / ((PI_D / 4.0) * 2.0), 0.1, 1.0);
            const double decay_rate = 0.03 + 0.15 * pitch_factor;
            const double exp_decay = std::exp(t_linear * decay_rate);
            const double scale_factor = 0.005 + 0.015 * pitch_factor;
            t_warp = -exp_decay * scale_factor;
            t_warp = std::min(t_warp, -0.00001);
        }

        double screen_y = horizon + t_warp * range;
        screen_y = std::max(screen_y, horizon);
        if (!std::isfinite(screen_y)) {
            return static_cast<float>(safe_linear_y);
        }

        (void)world_y;
        (void)screen_height;
        return static_cast<float>(screen_y);
    }

    struct PerspectiveRange {
        double near_distance = 0.0;
        double far_distance  = 1.0;
};

    PerspectiveRange sanitize_perspective_range(const WarpedScreenGrid::RealismSettings& settings) {
        double near_distance = static_cast<double>(settings.perspective_distance_at_scale_hundred);
        double far_distance  = static_cast<double>(settings.perspective_distance_at_scale_zero);
        if (!std::isfinite(near_distance)) near_distance = 0.0;
        if (!std::isfinite(far_distance))  far_distance  = near_distance + 1.0;
        if (std::fabs(far_distance - near_distance) < 1e-4) {
            far_distance = near_distance + 1.0;
        }
        if (near_distance > far_distance) {
            std::swap(near_distance, far_distance);
        }
        return PerspectiveRange{ near_distance, far_distance };
    }

    double compute_floor_distance_measure(double screen_y, const WarpedScreenGrid::FloorDepthParams& params) {
        if (!params.enabled) {
            return 0.0;
        }

        const double min_bound = std::min(params.horizon_screen_y, params.bottom_screen_y);
        const double max_bound = std::max(params.horizon_screen_y, params.bottom_screen_y);
        const double clamped_y = std::clamp(static_cast<double>(screen_y), min_bound, max_bound);

        const double denom_screen = std::max(1e-4, std::abs(params.bottom_screen_y - params.horizon_screen_y));
        const double t_screen = std::clamp((clamped_y - params.horizon_screen_y) / denom_screen, 0.0, 1.0);
        const double ndc_y = params.horizon_ndc + (params.near_ndc - params.horizon_ndc) * t_screen;
        const double ndc_span = std::max(1e-4, std::abs(params.near_ndc - params.horizon_ndc));
        return (params.near_ndc - ndc_y) / ndc_span;
    }

    double calculate_reference_perspective_scale(
        double screen_y,
        const WarpedScreenGrid::FloorDepthParams& params,
        const PerspectiveRange& range,
        double zoom_factor)
    {

        const double min_y = std::min(params.horizon_screen_y, params.bottom_screen_y);
        const double max_y = std::max(params.horizon_screen_y, params.bottom_screen_y);
        const double denom = std::max(max_y - min_y, 1e-4);
        double t = std::clamp((screen_y - min_y) / denom, 0.0, 1.0);

        float pitch_deg = static_cast<float>(params.pitch_radians * (180.0 / PI_D));
        if (!std::isfinite(pitch_deg)) pitch_deg = kDefaultPitchDegrees;
        pitch_deg = std::clamp(pitch_deg, WarpedScreenGrid::kMinPitchDegrees, WarpedScreenGrid::kMaxPitchDegrees);

        const float min_falloff = 1.0f;
        const float max_falloff = 1.3f;
        float pitch_norm = (pitch_deg - WarpedScreenGrid::kMinPitchDegrees) / (WarpedScreenGrid::kMaxPitchDegrees - WarpedScreenGrid::kMinPitchDegrees);
        float falloff = max_falloff - (max_falloff - min_falloff) * pitch_norm;

        const double s0 = 0.0;
        const double s1 = 1.0;
        const double s2 = 0.7;

        const double a = -4.0 * (s1 - s0 - 0.5 * (s2 - s0));
        const double b = (s2 - s0) - a;
        const double c = s0;

        double smooth_t = t * t * (3.0 - 2.0 * t);

        double regressed = a * smooth_t * smooth_t + b * smooth_t + c;
        regressed = std::clamp(regressed, 0.0, 2.0);
        regressed = std::pow(regressed, falloff);

        const double zoom_reduction = 1.0 - (zoom_factor * 0.3);

        double final_scale = regressed * zoom_reduction;

        return std::clamp(final_scale, 0.5, 2.0);
    }

    double interpolate_perspective_scale(double screen_y, double horizon_y, double bottom_y,
                                         double horizon_scale, double bottom_scale) {

        screen_y = std::clamp(screen_y, horizon_y, bottom_y);

        const double range = std::max(1.0, bottom_y - horizon_y);
        double t = (screen_y - horizon_y) / range;
        t = std::clamp(t, 0.6, 2.0);

        double smooth_t;
        if (t < 0.5) {
            smooth_t = 1.0 * t * t;
        } else {
            smooth_t = 1.0 - 2.0 * (1.0 - t) * (1.0 - t);
        }

        return horizon_scale + (bottom_scale - horizon_scale) * smooth_t;
    }

}

WarpedScreenGrid::CameraGeometry WarpedScreenGrid::compute_geometry_for_scale(double scale_value) const {
    const double clamped_scale = std::max(0.0001, scale_value);
    const double view_height = view_height_for_scale(clamped_scale);
    const double desired_depth_world = std::max(0.0, view_height * 0.5);
    return build_geometry(settings_, clamped_scale, anchor_world_y(), desired_depth_world, realism_enabled_);
}

WarpedScreenGrid::CameraGeometry WarpedScreenGrid::compute_geometry() const {
    return compute_geometry_for_scale(static_cast<double>(smoothed_scale_));
}

void WarpedScreenGrid::update_geometry_cache(const CameraGeometry& g) {
    const double scale_value = std::max(0.0001, static_cast<double>(smoothed_scale_));
    runtime_camera_height_ = g.camera_height;
    runtime_focus_depth_   = g.focus_depth;
    runtime_anchor_world_y_ = g.anchor_world_y;
    runtime_focus_ndc_offset_ = g.focus_ndc_offset;
    runtime_pitch_rad_     = g.pitch_radians;
    runtime_pitch_deg_     = g.pitch_degrees;
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
        runtime_depth_offset_px_ = depth_offset_for_scale(scale_value);
        runtime_floor_params_ = FloorDepthParams{};
    }
}

WarpedScreenGrid::WarpedScreenGrid(int screen_width, int screen_height, const Area& starting_zoom)
{
    screen_width_  = screen_width;
    screen_height_ = screen_height;
    aspect_        = (screen_height_ > 0) ? static_cast<double>(screen_width_) / static_cast<double>(screen_height_) : 1.0;

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
    scale_ = (base_w > 0) ? static_cast<float>(static_cast<double>(curr_w) / static_cast<double>(base_w)) : 1.0f;

    zooming_     = false;
    steps_total_ = 0;
    steps_done_  = 0;
    start_scale_ = scale_;
    target_scale_ = scale_;

    smoothed_center_.x = static_cast<float>(screen_center_.x);
    smoothed_center_.y = static_cast<float>(screen_center_.y);
    smoothed_scale_    = std::max(0.0001f, scale_);
    update_geometry_cache(compute_geometry());
}

WarpedScreenGrid::~WarpedScreenGrid() = default;

void WarpedScreenGrid::set_realism_settings(const RealismSettings& settings) {
    settings_ = settings;
    settings_.zoom_low = std::clamp(settings_.zoom_low, WarpedScreenGrid::kMinZoomAnchors, WarpedScreenGrid::kMaxZoomAnchors);
    const float min_high = std::min(WarpedScreenGrid::kMaxZoomAnchors, settings_.zoom_low + 0.0001f);
    settings_.zoom_high = std::clamp(settings_.zoom_high, min_high, WarpedScreenGrid::kMaxZoomAnchors);
    if (!std::isfinite(settings_.base_height_px) || settings_.base_height_px <= 0.0f) {
        settings_.base_height_px = 720.0f;
    }
    settings_.parallax_smoothing = sanitize_params(settings_.parallax_smoothing);
    if (settings_.parallax_smoothing.method == TransformSmoothingMethod::Lerp &&
        settings_.parallax_smoothing.lerp_rate <= 0.0f) {
        settings_.parallax_smoothing.lerp_rate = rate_from_tau(0.08f);
    } else if (settings_.parallax_smoothing.method == TransformSmoothingMethod::CriticallyDampedSpring &&
               settings_.parallax_smoothing.spring_frequency <= 0.0f) {
        settings_.parallax_smoothing.spring_frequency = 10.0f;
    }

    update_geometry_cache(compute_geometry());
}

void WarpedScreenGrid::set_screen_center(SDL_Point p, bool snap_immediately) {
    if (!screen_center_initialized_) {
        screen_center_              = p;
        screen_center_initialized_  = true;
        pan_offset_x_               = 0.0;
        pan_offset_y_               = 0.0;
        smoothed_center_.x          = static_cast<float>(screen_center_.x);
        smoothed_center_.y          = static_cast<float>(screen_center_.y);
        return;
    }

    const double dx = static_cast<double>(p.x) - static_cast<double>(screen_center_.x);
    const double dy = static_cast<double>(p.y) - static_cast<double>(screen_center_.y);
    pan_offset_x_ += dx;
    pan_offset_y_ += dy;
    screen_center_ = p;
    if (snap_immediately) {
        smoothed_center_.x = static_cast<float>(screen_center_.x);
        smoothed_center_.y = static_cast<float>(screen_center_.y);
    }
}

void WarpedScreenGrid::set_scale(float s) {
    const double clamped = clamp_zoom_scale(static_cast<double>(s));
    scale_ = static_cast<float>(clamped);
    zooming_     = false;
    steps_total_ = 0;
    steps_done_  = 0;
    start_scale_ = scale_;
    target_scale_= scale_;
    smoothed_scale_ = scale_;
    update_geometry_cache(compute_geometry());
}

float WarpedScreenGrid::get_scale() const {
    return smoothed_scale_;
}

void WarpedScreenGrid::zoom_to_scale(double target_scale, int duration_steps) {
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

void WarpedScreenGrid::zoom_to_area(const Area& target_area, int duration_steps) {
    Area adjusted = convert_area_to_aspect(target_area);
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int tgt_w  = std::max(1, width_from_area(adjusted));
    const double target = static_cast<double>(tgt_w) / static_cast<double>(base_w);
    zoom_to_scale(target, duration_steps);
}

void WarpedScreenGrid::update(float dt) {
    if (!std::isfinite(dt) || dt < 0.0f) {
        dt = 0.0f;
    }

    if (zooming_) {
        ++steps_done_;
        double t = static_cast<double>(steps_done_) / static_cast<double>(std::max(1, steps_total_));
        t = std::clamp(t, 0.0, 1.0);
        double s = start_scale_ + (target_scale_ - start_scale_) * t;
        scale_ = static_cast<float>(std::max(0.0001, s));

        if (pan_override_) {
            const double cx = static_cast<double>(start_center_.x) + (static_cast<double>(target_center_.x) - static_cast<double>(start_center_.x)) * t;
            const double cy = static_cast<double>(start_center_.y) + (static_cast<double>(target_center_.y) - static_cast<double>(start_center_.y)) * t;
            SDL_Point new_center{
                static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy)) };
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
    }

    const float safe_sx = static_cast<float>(screen_center_.x);
    const float safe_sy = static_cast<float>(screen_center_.y);
    const float safe_ss = std::max(0.0001f, scale_);

    smoothed_center_.x = std::clamp(safe_sx, -1e8f, 1e8f);
    smoothed_center_.y = std::clamp(safe_sy, -1e8f, 1e8f);
    smoothed_scale_ = static_cast<float>(std::clamp(static_cast<double>(safe_ss), 0.0001, static_cast<double>(WarpedScreenGrid::kMaxZoomAnchors)));

    recompute_current_view();
}

double WarpedScreenGrid::compute_room_scale_from_area(const Room* room) const {
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

void WarpedScreenGrid::set_up_rooms(CurrentRoomFinder* finder) {
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

void WarpedScreenGrid::update_zoom(Room* cur,
                         CurrentRoomFinder* finder,
                         Asset* player,
                         bool refresh_requested,
                         float dt,
                         bool dev_mode)
{
    pan_offset_x_ = 0.0;
    pan_offset_y_ = 0.0;

    if (!pan_override_) {

        if (player && !dev_mode) {
            set_screen_center(SDL_Point{ player->pos.x, player->pos.y }, false);
        } else if (focus_override_) {
            set_screen_center(focus_point_);
        } else if (cur && cur->room_area) {
            set_screen_center(cur->room_area->get_center());
        }
    }

    if (!refresh_requested && !zooming_) {
        update(dt);
        return;
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

    target_zoom = std::clamp( target_zoom, static_cast<double>(settings_.zoom_low), static_cast<double>(settings_.zoom_high) );

    const bool idle = !zooming_;
    if (idle || std::fabs(target_zoom - target_scale_) > SCALE_EPS) {
        zoom_to_scale(target_zoom, 35);
    }
}

Area WarpedScreenGrid::convert_area_to_aspect(const Area& in) const {
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

void WarpedScreenGrid::recompute_current_view() {
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int base_h = std::max(1, height_from_area(base_zoom_));
    const double scale_value = std::max(0.0001, static_cast<double>(smoothed_scale_));
    const int cur_w  = static_cast<int>(std::lround(static_cast<double>(base_w) * scale_value));
    const int cur_h  = static_cast<int>(std::lround(static_cast<double>(base_h) * scale_value));
    SDL_Point center{
        static_cast<int>(std::lround(smoothed_center_.x)), static_cast<int>(std::lround(smoothed_center_.y)) };
    current_view_ = make_rect_area("current_view", center, cur_w, cur_h, 0);
    update_geometry_cache(compute_geometry());
}

void WarpedScreenGrid::pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps) {
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

void WarpedScreenGrid::pan_and_zoom_to_asset(const Asset* a, double zoom_scale_factor, int duration_steps) {
    if (!a) return;
    SDL_Point target{ a->pos.x, a->pos.y };
    pan_and_zoom_to_point(target, zoom_scale_factor, duration_steps);
}

void WarpedScreenGrid::animate_zoom_multiply(double factor, int duration_steps) {
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

void WarpedScreenGrid::animate_zoom_towards_point(double factor, SDL_Point screen_point, int duration_steps) {
    if (factor <= 0.0) {
        factor = 1.0;
    }

    const double current_scale = clamp_zoom_scale(static_cast<double>(scale_));
    const double new_scale     = clamp_zoom_scale(current_scale * factor);

    int minx = 0, miny = 0, maxx = 0, maxy = 0;
    std::tie(minx, miny, maxx, maxy) = current_view_.get_bounds();

    const double world_x = static_cast<double>(minx) + static_cast<double>(screen_point.x) * current_scale;
    const double world_y = static_cast<double>(maxy) - static_cast<double>(screen_point.y) * current_scale;

    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int base_h = std::max(1, height_from_area(base_zoom_));

    const double anchored_center_x =
        world_x - static_cast<double>(screen_point.x) * new_scale + (static_cast<double>(base_w) * new_scale) * 0.5;
    const double anchored_center_y =
        world_y + static_cast<double>(screen_point.y) * new_scale - (static_cast<double>(base_h) * new_scale) * 0.5;

    constexpr double PAN_GAIN = 2.0;
    const double dx = anchored_center_x - static_cast<double>(screen_center_.x);
    const double dy = anchored_center_y - static_cast<double>(screen_center_.y);
    const double target_center_x = static_cast<double>(screen_center_.x) + dx * PAN_GAIN;
    const double target_center_y = static_cast<double>(screen_center_.y) + dy * PAN_GAIN;

    SDL_Point target_center{
        static_cast<int>(std::lround(target_center_x)), static_cast<int>(std::lround(target_center_y)) };

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

SDL_FPoint WarpedScreenGrid::map_to_screen(SDL_Point world) const {
    SDL_FPoint world_f{ static_cast<float>(world.x), static_cast<float>(world.y) };
    return map_to_screen_f(world_f);
}

SDL_FPoint WarpedScreenGrid::map_to_screen_f(SDL_FPoint world) const {
    int minx, miny, maxx, maxy;
    std::tie(minx, miny, maxx, maxy) = current_view_.get_bounds();
    const double inv_scale =
        (smoothed_scale_ > 0.000001f) ? (1.0 / static_cast<double>(smoothed_scale_)) : 1e6;
    const double sx = (static_cast<double>(world.x) - static_cast<double>(minx)) * inv_scale;
    const double sy = (static_cast<double>(world.y) - static_cast<double>(miny)) * inv_scale + static_cast<double>(player_center_offset_y_);
    const double safe_sx = std::isfinite(sx) ? sx : static_cast<double>(minx);
    const double safe_sy = std::isfinite(sy) ? sy : static_cast<double>(miny);
    const float out_x = static_cast<float>(std::clamp(safe_sx, -1e8, 1e8));
    const float out_y = static_cast<float>(std::clamp(safe_sy, -1e8, 1e8));
    return SDL_FPoint{ out_x, out_y };
}

SDL_FPoint WarpedScreenGrid::screen_to_map(SDL_Point screen) const {
    int minx, miny, maxx, maxy;
    std::tie(minx, miny, maxx, maxy) = current_view_.get_bounds();
    const double s = static_cast<double>(std::max(0.000001f, smoothed_scale_));

    const double adjusted_screen_y = static_cast<double>(screen.y) - static_cast<double>(player_center_offset_y_);
    double wx = static_cast<double>(minx) + static_cast<double>(screen.x) * s;
    double wy = static_cast<double>(miny) + adjusted_screen_y * s;
    const double safe_wx = std::isfinite(wx) ? wx : static_cast<double>(minx);
    const double safe_wy = std::isfinite(wy) ? wy : static_cast<double>(maxy);
    const float out_wx = static_cast<float>(std::clamp(safe_wx, -1e8, 1e8));
    const float out_wy = static_cast<float>(std::clamp(safe_wy, -1e8, 1e8));
    return SDL_FPoint{ out_wx, out_wy };
}
WarpedScreenGrid::RenderEffects WarpedScreenGrid::compute_render_effects(
    SDL_Point world,
    float ,
    float ,
    RenderSmoothingKey ) const
{
    RenderEffects result;

    SDL_FPoint world_f{ static_cast<float>(world.x), static_cast<float>(world.y) };
    SDL_FPoint linear_screen = map_to_screen_f(world_f);

    result.screen_position = linear_screen;
    result.vertical_scale  = 1.0f;
    result.distance_scale  = 1.0f;
    result.horizon_fade_alpha = 1.0f;

    const double horizon_y_raw = horizon_screen_y_for_scale();
    if (std::isfinite(horizon_y_raw)) {
        const float horizon_y = static_cast<float>(horizon_y_raw);
        const bool horizon_in_view =
            horizon_y > 0.0f && horizon_y < static_cast<float>(screen_height_);
        if (horizon_in_view) {
            const float fade_band_px = std::max(1.0f, settings_.horizon_fade_band_px);
            const float dist_from_horizon = result.screen_position.y - horizon_y;
            if (dist_from_horizon <= 0.0f) {
                result.horizon_fade_alpha = 0.0f;
            } else if (dist_from_horizon < fade_band_px) {
                const float t = dist_from_horizon / fade_band_px;
                result.horizon_fade_alpha = std::clamp(t * t * t, 0.0f, 1.0f);
            }
        }
    }

#if 0
    SDL_FPoint warped_screen = linear_screen;

    if (realism_enabled_) {
        warped_screen.y = warp_floor_screen_y(world_f.y, linear_screen.y);
        if (!std::isfinite(warped_screen.y)) {
            warped_screen.y = linear_screen.y;
        }
    }

    if (!std::isfinite(warped_screen.x) || !std::isfinite(warped_screen.y)) {
        warped_screen = linear_screen;
    }

    result.screen_position = warped_screen;
    result.vertical_scale  = 1.0f;
    result.distance_scale  = 1.0f;

    if (!realism_enabled_) {
        return result;
    }

    constexpr double EPS = 1e-6;

    const CameraGeometry geom = compute_geometry();
    if (!geom.valid || geom.camera_height <= EPS) {
        return result;
    }

    result.vertical_scale = 1.0f;

    FloorDepthParams p = runtime_floor_params_;
    if (!p.enabled) {

        p = compute_floor_depth_params();
    }

    const double horizon = p.horizon_screen_y;
    const double bottom  = p.bottom_screen_y;
    if (!std::isfinite(horizon) || !std::isfinite(bottom) || bottom <= horizon + EPS) {

        return result;
    }

    const PerspectiveRange range = sanitize_perspective_range(settings_);

    const double zoom_low = std::max(static_cast<double>(kMinZoomAnchors), static_cast<double>(settings_.zoom_low));
    const double zoom_high = std::max(zoom_low + kMinZoomRange, static_cast<double>(settings_.zoom_high));
    const double current_zoom = std::clamp(static_cast<double>(smoothed_scale_), zoom_low, zoom_high);
    const double zoom_span = std::max(kMinZoomRange, zoom_high - zoom_low);
    const double zoom_factor = std::clamp((current_zoom - zoom_low) / zoom_span, 0.0, 1.0);

    const double horizon_screen_y = p.horizon_screen_y;
    const double bottom_screen_y = p.bottom_screen_y;
    const double horizon_perspective_scale = 0.0;
    const double bottom_perspective_scale = calculate_reference_perspective_scale( bottom_screen_y, p, range, zoom_factor);

    double depth_scale = interpolate_perspective_scale( static_cast<double>(warped_screen.y), horizon_screen_y, bottom_screen_y, horizon_perspective_scale, bottom_perspective_scale);

    double final_scale = depth_scale;
    if (reference_screen_height > EPS && asset_screen_height > EPS) {
        double screen_based_scale = std::clamp( static_cast<double>(reference_screen_height) / std::max(static_cast<double>(asset_screen_height), EPS), 0.35, 1.5);
        final_scale = 0.5 * depth_scale + 0.5 * screen_based_scale;
    }

    const double distance_scale = std::clamp(final_scale, 0.01, 4.0);
    result.distance_scale = static_cast<float>(distance_scale);

    result.horizon_fade_alpha = 1.0f;
    float horizon_scale_multiplier = 1.0f;

    const float fade_band_px = std::max(1.0f, settings_.horizon_fade_band_px);
    const float horizon_y = static_cast<float>(horizon);
    const float screen_y = warped_screen.y;

    const float dist_from_horizon = screen_y - horizon_y;

    if (dist_from_horizon <= 1.0f) {

        result.horizon_fade_alpha = 0.0f;
        horizon_scale_multiplier = 0.0f;
    } else if (dist_from_horizon < fade_band_px) {

        const float t = dist_from_horizon / fade_band_px;

        const float fade_alpha = t * t * t;
        result.horizon_fade_alpha = std::clamp(fade_alpha, 0.0f, 1.0f);

        horizon_scale_multiplier = std::clamp(t * t, 0.0f, 1.0f);
    }

    result.distance_scale *= horizon_scale_multiplier;
    result.distance_scale = std::clamp(result.distance_scale, 0.0f, 4.0f);

    if (!std::isfinite(result.vertical_scale) || result.vertical_scale <= 0.0f) {
        result.vertical_scale = 1.0f;
    } else {
        result.vertical_scale = std::clamp(result.vertical_scale, 0.1f, 2.0f);
    }

    if (!std::isfinite(result.distance_scale)) {
        result.distance_scale = 1.0f;
    } else {
        result.distance_scale = std::clamp(result.distance_scale, 0.0f, 4.0f);
    }

    if (!std::isfinite(result.screen_position.x) || !std::isfinite(result.screen_position.y)) {
        result.screen_position = linear_screen;
    }
#endif

    return result;
}

void WarpedScreenGrid::apply_camera_settings(const nlohmann::json& data) {
    if (!data.is_object()) {
        return;
    }

    const auto try_read_number = [&](const char* key, auto& target) -> bool {
        auto it = data.find(key);
        if (it == data.end() || !it->is_number()) {
            return false;
        }
        if constexpr (std::is_integral_v<std::decay_t<decltype(target)>>) {
            target = static_cast<std::decay_t<decltype(target)>>(std::lround(it->get<double>()));
        } else {
            target = static_cast<std::decay_t<decltype(target)>>(it->get<double>());
        }
        return true;
};

    const auto try_read_bool = [&](const char* key, bool& target) -> bool {
        auto it = data.find(key);
        if (it == data.end()) {
            return false;
        }
        if (it->is_boolean()) {
            target = it->get<bool>();
            return true;
        }
        if (it->is_number_integer()) {
            target = it->get<int>() != 0;
            return true;
        }
        return false;
};

    const auto try_read_enum = [&](const char* key, auto& target, int min_value, int max_value) -> bool {
        auto it = data.find(key);
        if (it == data.end() || !it->is_number_integer()) {
            return false;
        }
        const int raw = it->get<int>();
        if (raw < min_value || raw > max_value) {
            return false;
        }
        target = static_cast<std::decay_t<decltype(target)>>(raw);
        return true;
};

    try_read_bool("realism_enabled", realism_enabled_);

    const std::array<std::pair<const char*, float*>, 15> float_fields{ {
        { "extra_cull_margin", &settings_.extra_cull_margin },
        { "zoom_low", &settings_.zoom_low },
        { "zoom_high", &settings_.zoom_high },
        { "base_height_px", &settings_.base_height_px },
        { "min_visible_screen_ratio", &settings_.min_visible_screen_ratio },
        { "parallax_smoothing_lerp_rate", &settings_.parallax_smoothing.lerp_rate },
        { "parallax_smoothing_spring_frequency", &settings_.parallax_smoothing.spring_frequency },
        { "parallax_smoothing_max_step", &settings_.parallax_smoothing.max_step },
        { "parallax_smoothing_snap_threshold", &settings_.parallax_smoothing.snap_threshold },
        { "scale_hysteresis_margin", &settings_.scale_variant_hysteresis_margin },
        { "foreground_plane_screen_y", &settings_.foreground_plane_screen_y },
        { "background_plane_screen_y", &settings_.background_plane_screen_y },
        { "perspective_distance_at_scale_zero", &settings_.perspective_distance_at_scale_zero },
        { "perspective_distance_at_scale_hundred", &settings_.perspective_distance_at_scale_hundred },
        { "horizon_fade_band_px", &settings_.horizon_fade_band_px },
    } };
    for (const auto& [key, field] : float_fields) {
        try_read_number(key, *field);
    }

    const std::array<std::pair<const char*, int*>, 3> int_fields{ {
        { "render_quality_percent", &settings_.render_quality_percent },
        { "foreground_texture_max_opacity", &settings_.foreground_texture_max_opacity },
        { "background_texture_max_opacity", &settings_.background_texture_max_opacity }
    } };
    for (const auto& [key, field] : int_fields) {
        try_read_number(key, *field);
    }

    try_read_enum("parallax_smoothing_method", settings_.parallax_smoothing.method, 0, 2);
    if (!try_read_enum("texture_opacity_falloff_method", settings_.texture_opacity_falloff_method, 0, 4)) {
        settings_.texture_opacity_falloff_method = BlurFalloffMethod::Linear;
    }

    settings_.foreground_texture_max_opacity =
        std::clamp(settings_.foreground_texture_max_opacity, 0, 255);
    settings_.background_texture_max_opacity =
        std::clamp(settings_.background_texture_max_opacity, 0, 255);

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

    if (!std::isfinite(settings_.zoom_low)) {
        settings_.zoom_low = 0.75f;
    }

    if (!std::isfinite(settings_.zoom_high)) {
        settings_.zoom_high = std::max(settings_.zoom_low + 0.25f, 1.0f);
    }

    if (!std::isfinite(settings_.base_height_px) || settings_.base_height_px <= 0.0f) {
        settings_.base_height_px = 720.0f;
    }

    if (!std::isfinite(settings_.min_visible_screen_ratio) ||
        settings_.min_visible_screen_ratio < 0.0f) {
        settings_.min_visible_screen_ratio = 0.015f;
    } else {
        settings_.min_visible_screen_ratio =
            std::clamp(settings_.min_visible_screen_ratio, 0.0f, 0.5f);
    }

    settings_.zoom_low = std::clamp(settings_.zoom_low, WarpedScreenGrid::kMinZoomAnchors, WarpedScreenGrid::kMaxZoomAnchors);
    const float min_high = std::min(WarpedScreenGrid::kMaxZoomAnchors, settings_.zoom_low + 0.0001f);
    settings_.zoom_high = std::clamp(settings_.zoom_high, min_high, WarpedScreenGrid::kMaxZoomAnchors);

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
    if (!std::isfinite(settings_.scale_variant_hysteresis_margin) ||
        settings_.scale_variant_hysteresis_margin < 0.0f) {
        settings_.scale_variant_hysteresis_margin = 0.05f;
    }

    recompute_current_view();
}

nlohmann::json WarpedScreenGrid::camera_settings_to_json() const {
    nlohmann::json j = nlohmann::json::object();
    j["realism_enabled"] = realism_enabled_;

    const std::pair<const char*, float> float_fields[] = {
        { "extra_cull_margin", settings_.extra_cull_margin },
        { "zoom_low", settings_.zoom_low },
        { "zoom_high", settings_.zoom_high },
        { "perspective_distance_at_scale_zero", settings_.perspective_distance_at_scale_zero },
        { "perspective_distance_at_scale_hundred", settings_.perspective_distance_at_scale_hundred },
        { "base_height_px", settings_.base_height_px },
        { "min_visible_screen_ratio", settings_.min_visible_screen_ratio },
        { "scale_hysteresis_margin", settings_.scale_variant_hysteresis_margin },
        { "parallax_smoothing_lerp_rate", settings_.parallax_smoothing.lerp_rate },
        { "parallax_smoothing_spring_frequency", settings_.parallax_smoothing.spring_frequency },
        { "parallax_smoothing_max_step", settings_.parallax_smoothing.max_step },
        { "parallax_smoothing_snap_threshold", settings_.parallax_smoothing.snap_threshold },
        { "foreground_plane_screen_y", settings_.foreground_plane_screen_y },
        { "background_plane_screen_y", settings_.background_plane_screen_y },
        { "horizon_fade_band_px", settings_.horizon_fade_band_px },
        { "perspective_scale_gamma", settings_.perspective_scale_gamma }
};
    for (const auto& [key, value] : float_fields) {
        j[key] = value;
    }

    const std::pair<const char*, int> int_fields[] = {
        { "render_quality_percent", settings_.render_quality_percent },
        { "parallax_smoothing_method", static_cast<int>(settings_.parallax_smoothing.method) },
        { "foreground_texture_max_opacity", settings_.foreground_texture_max_opacity },
        { "background_texture_max_opacity", settings_.background_texture_max_opacity },
        { "texture_opacity_falloff_method", static_cast<int>(settings_.texture_opacity_falloff_method) }
};
    for (const auto& [key, value] : int_fields) {
        j[key] = value;
    }

    return j;
}
SDL_FPoint WarpedScreenGrid::get_view_center_f() const {
    if (std::isfinite(smoothed_center_.x) && std::isfinite(smoothed_center_.y)) {
        return smoothed_center_;
    }
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const float cx = (static_cast<float>(left) + static_cast<float>(right)) * 0.5f;
    const float cy = (static_cast<float>(top)  + static_cast<float>(bottom)) * 0.5f;
    return SDL_FPoint{ cx, cy };
}

WarpedScreenGrid::FloorDepthParams WarpedScreenGrid::compute_floor_depth_params_for_geometry(const CameraGeometry& geom, double scale_value) const {
    return build_floor_params(settings_, screen_height_, geom, scale_value, realism_enabled_);
}

WarpedScreenGrid::FloorDepthParams WarpedScreenGrid::compute_floor_depth_params_for_scale(double scale_value) const {
    const CameraGeometry geom = compute_geometry_for_scale(scale_value);
    return compute_floor_depth_params_for_geometry(geom, scale_value);
}

WarpedScreenGrid::FloorDepthParams WarpedScreenGrid::compute_floor_depth_params() const {
    const CameraGeometry geom = compute_geometry();
    return compute_floor_depth_params_for_geometry(geom, static_cast<double>(smoothed_scale_));
}

float WarpedScreenGrid::warp_floor_screen_y(float world_y, float linear_screen_y) const {
    (void)world_y;

    return std::isfinite(linear_screen_y) ? linear_screen_y : 0.0f;
}

double WarpedScreenGrid::view_height_world() const {
    int minx = 0, miny = 0, maxx = 0, maxy = 0;
    std::tie(minx, miny, maxx, maxy) = current_view_.get_bounds();
    return static_cast<double>(std::max(0, maxy - miny));
}

double WarpedScreenGrid::view_height_for_scale(double scale_value) const {
    const int base_h = std::max(1, height_from_area(base_zoom_));
    const double clamped_scale = std::max(0.0001, scale_value);
    return static_cast<double>(base_h) * clamped_scale;
}

double WarpedScreenGrid::anchor_world_y() const {

    return static_cast<double>(smoothed_center_.y);
}

double WarpedScreenGrid::zoom_lerp_t_for_scale(double scale_value) const {
    return ZoomInterpolator(settings_, scale_value).t;
}

float WarpedScreenGrid::depth_offset_for_scale(double scale_value) const {
    const double safe_scale = std::max(0.0001, scale_value);
    const double depth_world = std::max(0.0, view_height_for_scale(safe_scale) * 0.5);
    const double depth_px = depth_world / safe_scale;
    if (!std::isfinite(depth_px)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(depth_px, 0.0, 1e6));
}

double WarpedScreenGrid::horizon_screen_y_for_scale_value(double scale_value) const {
    if (!realism_enabled_) {
        return 0.0;
    }

    const double extent    = static_cast<double>(screen_height_);
    const double min_bound = -4.0 * extent;
    const double max_bound = extent * 0.45;

    const double cached_scale = static_cast<double>(smoothed_scale_);
    const double kScaleEps = 1e-6;
    if (std::abs(scale_value - cached_scale) <= kScaleEps && runtime_floor_params_.enabled) {
        return std::clamp(runtime_floor_params_.horizon_screen_y, min_bound, max_bound);
    }

    const CameraGeometry geom = compute_geometry_for_scale(scale_value);
    if (!geom.valid) {
        return extent > 0.0 ? extent * 0.5 : 0.0;
    }

    const FloorDepthParams params = compute_floor_depth_params_for_geometry(geom, scale_value);
    if (!params.enabled) {
        return extent > 0.0 ? extent * 0.5 : 0.0;
    }

    return std::clamp(params.horizon_screen_y, min_bound, max_bound);
}

double WarpedScreenGrid::horizon_screen_y_for_scale() const {
    return horizon_screen_y_for_scale_value(static_cast<double>(smoothed_scale_));
}

void WarpedScreenGrid::clear_grid_state() {
    warped_points_.clear();
    visible_assets_.clear();
    visible_points_.clear();
    active_chunks_.clear();
    id_to_index_.clear();
    cached_world_rect_ = SDL_Rect{0, 0, 0, 0};
    bounds_ = GridBounds{};
}

void WarpedScreenGrid::rebuild_grid_bounds() {
    if (warped_points_.empty()) {
        cached_world_rect_ = SDL_Rect{0, 0, 0, 0};
        bounds_ = GridBounds{};
        return;
    }

    int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
    for (const world::GridPoint* gp : warped_points_) {
        if (!gp) continue;
        minx = std::min(minx, gp->world.x);
        miny = std::min(miny, gp->world.y);
        maxx = std::max(maxx, gp->world.x);
        maxy = std::max(maxy, gp->world.y);
    }
    if (minx > maxx || miny > maxy) {
        cached_world_rect_ = SDL_Rect{0, 0, 0, 0};
        bounds_ = GridBounds{};
        return;
    }
    cached_world_rect_.x = minx;
    cached_world_rect_.y = miny;
    cached_world_rect_.w = std::max(0, maxx - minx);
    cached_world_rect_.h = std::max(0, maxy - miny);

    bounds_.left = 0.0f;
    bounds_.top = 0.0f;
    bounds_.right = static_cast<float>(screen_width_);
    bounds_.bottom = static_cast<float>(screen_height_);
}

void WarpedScreenGrid::rebuild_grid(world::WorldGrid& world_grid, float dt_seconds) {
    clear_grid_state();

    std::vector<Asset*> assets = world_grid.all_assets();
    warped_points_.reserve(assets.size());
    visible_assets_.reserve(assets.size());
    visible_points_.reserve(assets.size());

    const float inv_scale   = 1.0f / std::max(0.000001f, smoothed_scale_);
    const float screen_w    = static_cast<float>(screen_width_);
    const float screen_h    = static_cast<float>(screen_height_);

    player_center_offset_y_ = 0.0f;
    Asset* player_asset = nullptr;
    for (Asset* a : assets) {
        if (a && a->info && a->info->type == "player") {
            player_asset = a;
            break;
        }
    }

    if (!manual_zoom_override_ && player_asset) {
        SDL_Point player_world{ player_asset->pos.x, player_asset->pos.y };
        SDL_FPoint player_screen_base = map_to_screen(player_world);

#if 0

        const float player_world_y_f = static_cast<float>(player_world.y);
        const float player_warped_y = warp_floor_screen_y(player_world_y_f, player_screen_base.y);
        const float player_final_y = std::isfinite(player_warped_y) ? player_warped_y : player_screen_base.y;
#else
        const float player_final_y = player_screen_base.y;
#endif

        const float screen_center_y = screen_h * 0.5f;
        player_center_offset_y_ = screen_center_y - player_final_y;
    }

    const bool perspective_disabled = WarpedScreenGrid::kForceDepthPerspectiveDisabled;
    const double raw_horizon_y = horizon_screen_y_for_scale();
    const bool horizon_valid = std::isfinite(raw_horizon_y);
    const float horizon_y = horizon_valid ? static_cast<float>(raw_horizon_y) : -screen_h;
    const bool horizon_at_or_above_top = !horizon_valid || horizon_y <= 0.0f;

    const float margin_px    = std::max(0.0f, settings_.extra_cull_margin);
    const float depth_pad_px = std::max(0.0f, current_depth_offset_px());

    float side_pad = margin_px;
    float bottom_pad = std::max(depth_pad_px, margin_px);

    if (perspective_disabled) {
        const float expansion_factor = 2.0f;
        side_pad   *= expansion_factor;
        bottom_pad *= expansion_factor;
    }

    const float spawn_top = horizon_at_or_above_top
        ? 0.0f
        : std::max(0.0f, horizon_y - margin_px);
    const float screen_bottom = screen_h + bottom_pad;
    const float cull_top = std::clamp(spawn_top, 0.0f, screen_bottom);
    const float cull_height = std::max(1.0f, screen_bottom - cull_top);

    const SDL_FRect cull_rect{
        -side_pad,
        cull_top,
        screen_w + side_pad * 2.0f,
        cull_height
};
    const float min_visible_px =
        screen_h * std::clamp(settings_.min_visible_screen_ratio, 0.0f, 0.5f);

#if 0

    FloorDepthParams depth_params = runtime_floor_params_;
    if (!depth_params.enabled) {
        depth_params = compute_floor_depth_params();
    }

    const PerspectiveRange perspective_range = sanitize_perspective_range(settings_);

    perspective_distance_at_scale_zero_    = perspective_range.far_distance;
    perspective_distance_at_scale_hundred_ = perspective_range.near_distance;

    const double zoom_low = std::max(static_cast<double>(kMinZoomAnchors), static_cast<double>(settings_.zoom_low));
    const double zoom_high = std::max(zoom_low + kMinZoomRange, static_cast<double>(settings_.zoom_high));
    const double current_zoom = std::clamp(static_cast<double>(smoothed_scale_), zoom_low, zoom_high);
    const double zoom_span = std::max(kMinZoomRange, zoom_high - zoom_low);
    const double zoom_factor = std::clamp((current_zoom - zoom_low) / zoom_span, 0.0, 1.0);

    const double horizon_screen_y = depth_params.horizon_screen_y;
    const double horizon_perspective_scale = 0.0;

    const double bottom_screen_y = depth_params.bottom_screen_y;
    const double bottom_perspective_scale = calculate_reference_perspective_scale( bottom_screen_y, depth_params, perspective_range, zoom_factor);

#endif

    auto rects_intersect = [](const SDL_FRect& a, const SDL_FRect& b) -> bool {
        const float ax1 = a.x + a.w;
        const float ay1 = a.y + a.h;
        const float bx1 = b.x + b.w;
        const float by1 = b.y + b.h;
        return !(ax1 < b.x || bx1 < a.x || ay1 < b.y || by1 < a.y);
};

    for (Asset* a : assets) {
        if (!a) continue;
        world::GridPoint* gp = world_grid.point_for_asset(a);
        if (!gp) continue;

        const SDL_Point world_pos{ gp->world.x, gp->world.y };

        SDL_FPoint linear_screen = map_to_screen(world_pos);
        float warped_y = linear_screen.y;
#if 0

        warped_y = warp_floor_screen_y(static_cast<float>(world_pos.y), linear_screen.y);
#endif
        SDL_FPoint screen_pos{linear_screen.x, warped_y};

        if (!std::isfinite(screen_pos.x) || !std::isfinite(screen_pos.y)) {
            screen_pos = linear_screen;
        }

        const float parallax_dx = 0.0f;
        const RenderEffects effects = compute_render_effects( world_pos, 0.0f, settings_.base_height_px, RenderSmoothingKey(a));

        float base_scale = a->smoothed_scale();
        if (!std::isfinite(base_scale) || base_scale <= 0.0f) {
            base_scale = 1.0f;
        }

        const int fw = (a && a->info) ? std::max(1, a->info->original_canvas_width) : 1;
        const int fh = (a && a->info) ? std::max(1, a->info->original_canvas_height) : 1;
        const float base_sw = static_cast<float>(fw) * base_scale * inv_scale;
        const float base_sh = static_cast<float>(fh) * base_scale * inv_scale;

        float approx_w = base_sw * effects.distance_scale;
        float approx_h = base_sh * effects.distance_scale * effects.vertical_scale;
        const float min_size = std::max(1.0f, min_visible_px);
        approx_w = std::isfinite(approx_w) && approx_w > 0.0f ? std::max(approx_w, min_size) : min_size;
        approx_h = std::isfinite(approx_h) && approx_h > 0.0f ? std::max(approx_h, min_size) : min_size;

        SDL_FRect bounds{
            screen_pos.x - approx_w * 0.5f,
            screen_pos.y - approx_h,
            approx_w,
            approx_h
};

        const bool intersects = rects_intersect(bounds, cull_rect);
        const bool has_alpha  = horizon_at_or_above_top || effects.horizon_fade_alpha > 0.001f;
        const bool on_screen  = intersects && has_alpha;

        gp->screen             = screen_pos;
        gp->parallax_dx        = parallax_dx;
        gp->vertical_scale     = effects.vertical_scale;
        gp->horizon_fade_alpha = effects.horizon_fade_alpha;

        gp->perspective_scale  = 1.0f;
        gp->distance_to_camera = 0.0f;
        gp->tilt_radians       = 0.0f;
        gp->on_screen          = on_screen;

#if 0

        const double perspective_scale_value = interpolate_perspective_scale( static_cast<double>(screen_pos.y), horizon_screen_y, bottom_screen_y, horizon_perspective_scale, bottom_perspective_scale);

        gp->perspective_scale  = static_cast<float>(perspective_scale_value);
        const double distance_measure = compute_floor_distance_measure(screen_pos.y, depth_params);
        gp->distance_to_camera = static_cast<float>(distance_measure);
        gp->tilt_radians       = runtime_pitch_rad_;

        float fg_opacity = 0.0f;
        float bg_opacity = 1.0f;

        if (on_screen && !gp->occupants.empty()) {

            float screen_y = screen_pos.y;
            float fg_y = settings_.foreground_plane_screen_y;
            float bg_y = settings_.background_plane_screen_y;

            if (screen_y > fg_y) {
                fg_opacity = 1.0f;
            } else if (screen_y < bg_y) {
                fg_opacity = 0.0f;
            } else {
                float range = fg_y - bg_y;
                if (range > 0.001f) {
                    fg_opacity = (screen_y - bg_y) / range;
                }
            }
            fg_opacity = std::clamp(fg_opacity, 0.0f, 1.0f);
            bg_opacity = 1.0f - fg_opacity;

            fg_opacity *= (static_cast<float>(settings_.foreground_texture_max_opacity) / 255.0f);
            bg_opacity *= (static_cast<float>(settings_.background_texture_max_opacity) / 255.0f);
        }

#endif

        id_to_index_[gp->id] = warped_points_.size();
        warped_points_.push_back(gp);
        if (on_screen) {
            visible_assets_.push_back(a);
            visible_points_.push_back(gp);
        }
        if (gp->chunk) active_chunks_.push_back(gp->chunk);
    }

    if (!active_chunks_.empty()) {
        std::sort(active_chunks_.begin(), active_chunks_.end());
        active_chunks_.erase(std::unique(active_chunks_.begin(), active_chunks_.end()), active_chunks_.end());
    }

    rebuild_grid_bounds();
    bounds_.left   = cull_rect.x;
    bounds_.top    = cull_rect.y;
    bounds_.right  = cull_rect.x + cull_rect.w;
    bounds_.bottom = cull_rect.y + cull_rect.h;
}

world::GridPoint* WarpedScreenGrid::grid_point_for_asset(const Asset* asset) {
    if (!asset) return nullptr;
    const std::uint64_t id = asset->grid_id();
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) return nullptr;
    std::size_t idx = it->second;
    if (idx >= warped_points_.size()) return nullptr;
    return warped_points_[idx];
}

const world::GridPoint* WarpedScreenGrid::grid_point_for_asset(const Asset* asset) const {
    if (!asset) return nullptr;
    const std::uint64_t id = asset->grid_id();
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) return nullptr;
    std::size_t idx = it->second;
    if (idx >= warped_points_.size()) return nullptr;
    return warped_points_[idx];
}

WarpedScreenGrid::RenderSmoothingKey::RenderSmoothingKey(const Asset* asset, int frame)
    : asset_id(asset
        ? (asset->grid_id() != 0
            ? asset->grid_id()
            : static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(asset)))
        : 0),
      frame_index(frame) {}

void WarpedScreenGrid::set_focus_override(SDL_Point focus) {
    focus_override_ = true;
    focus_point_ = focus;
}

void WarpedScreenGrid::set_manual_zoom_override(bool enabled) {
    manual_zoom_override_ = enabled;
}

void WarpedScreenGrid::clear_focus_override() {
    focus_override_ = false;
}

void WarpedScreenGrid::clear_manual_zoom_override() {
    manual_zoom_override_ = false;
}

double WarpedScreenGrid::default_zoom_for_room(const Room* room) const {
    return compute_room_scale_from_area(room);
}

void WarpedScreenGrid::project_to_screen(world::GridPoint& point) const {

    SDL_FPoint linear_screen = map_to_screen(point.world);

    float warped_y = warp_floor_screen_y(static_cast<float>(point.world.y), linear_screen.y);

    float parallax_dx = 0.0f;

    point.screen = SDL_FPoint{linear_screen.x + parallax_dx, warped_y};
    point.parallax_dx = parallax_dx;
}

