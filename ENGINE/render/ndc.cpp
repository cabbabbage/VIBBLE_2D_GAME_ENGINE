#include "ndc.hpp"

#include <algorithm>
#include <cmath>

namespace {
    constexpr double ndc_PI_D       = 3.14159265358979323846;
    constexpr double HALF_FOV_Y = ndc_PI_D / 4.0; // 45 deg half FOV (90 total)
    constexpr double RAD_TO_DEG = 180.0 / ndc_PI_D;
    constexpr double ndc_kMinZoomRange = 1e-4;
    constexpr float  ndc_kDefaultPitchDegrees = 60.0f;

    // Helper for zoom interpolation
    struct NdcZoomInterpolator {
        double t = 0.0;
        NdcZoomInterpolator(const ndc::Settings& settings, double scale_value) {
            const double safe_low = std::max(0.0, static_cast<double>(settings.zoom_low));
            const double safe_high = std::max(safe_low + ndc_kMinZoomRange,
                                               static_cast<double>(settings.zoom_high));
            const double span = std::max(ndc_kMinZoomRange, safe_high - safe_low);
            t = std::clamp((scale_value - safe_low) / span, 0.0, 1.0);
        }

        template <typename V>
        V lerp(V low, V high) const {
            return static_cast<V>(low + (high - low) * t);
        }
    };

    double ndc_wrap_degrees_0_360(double raw_value) {
        if (!std::isfinite(raw_value)) {
            return static_cast<double>(ndc_kDefaultPitchDegrees);
        }
        double wrapped = std::fmod(raw_value, 360.0);
        if (wrapped < 0.0) wrapped += 360.0;
        if (wrapped >= 360.0 || !std::isfinite(wrapped)) {
            wrapped = std::fmod(wrapped, 360.0);
            if (wrapped < 0.0) wrapped += 360.0;
        }
        return std::isfinite(wrapped) ? wrapped : static_cast<double>(ndc_kDefaultPitchDegrees);
    }

    float ndc_sanitize_pitch_degrees(float raw_value) {
        const float kMinPitchDegrees = 0.0f;
        const float kMaxPitchDegrees = 89.0f;
        const float wrapped = static_cast<float>(ndc_wrap_degrees_0_360(
            std::isfinite(raw_value) ? raw_value : ndc_kDefaultPitchDegrees));
        return std::clamp(wrapped, kMinPitchDegrees, kMaxPitchDegrees);
    }

    double ndc_signed_radians_from_degrees(double degrees) {
        const double wrapped_deg = ndc_wrap_degrees_0_360(degrees);
        const double signed_deg  = (wrapped_deg > 180.0) ? wrapped_deg - 360.0 : wrapped_deg;
        return signed_deg * (ndc_PI_D / 180.0);
    }

    double ndc_shortest_delta_degrees(double from_deg, double to_deg) {
        return std::remainder(to_deg - from_deg, 360.0);
    }

    double ndc_lerp_angle(double from_deg, double to_deg, double t) {
        const double delta = ndc_shortest_delta_degrees(from_deg, to_deg);
        return ndc_wrap_degrees_0_360(from_deg + delta * t);
    }
}

ndc::ndc(int screen_width, int screen_height)
    : screen_width_(screen_width)
    , screen_height_(screen_height)
{
}

void ndc::set_screen_dimensions(int width, int height) {
    screen_width_ = width;
    screen_height_ = height;
}

void ndc::set_settings(const Settings& settings) {
    settings_ = settings;
}

double ndc::camera_height_from_scale(double scale_value) const {
    const NdcZoomInterpolator zoom(settings_, scale_value);
    const double base_height_low = std::isfinite(settings_.base_height_at_zoom_low)
        ? std::max(0.0, static_cast<double>(settings_.base_height_at_zoom_low))
        : std::max(1.0, static_cast<double>(settings_.base_height_px));
    const double base_height_high = std::isfinite(settings_.base_height_at_zoom_high)
        ? std::max(0.0, static_cast<double>(settings_.base_height_at_zoom_high))
        : std::max(1.0, static_cast<double>(settings_.base_height_px));
    const double base_height = zoom.lerp(base_height_low, base_height_high);
    return std::max(0.0, base_height * scale_value);
}

double ndc::pitch_from_scale(double scale_value) const {
    const NdcZoomInterpolator zoom(settings_, scale_value);
    const double pitch_in_deg  = static_cast<double>(ndc_sanitize_pitch_degrees(settings_.tilt_zoom_in_degrees));
    const double pitch_out_deg = static_cast<double>(ndc_sanitize_pitch_degrees(settings_.tilt_zoom_out_degrees));
    return ndc_lerp_angle(pitch_in_deg, pitch_out_deg, zoom.t);
}

double ndc::zoom_lerp_t_for_scale(double scale_value) const {
    return NdcZoomInterpolator(settings_, scale_value).t;
}

ndc::CameraGeometry ndc::compute_geometry_for_scale(
    double scale_value,
    double anchor_world_y,
    bool realism_enabled) const
{
    CameraGeometry g{};
    if (!realism_enabled) {
        return g;
    }

    const double clamped_scale = std::max(0.0001, scale_value);
    g.camera_height = camera_height_from_scale(clamped_scale);
    if (g.camera_height <= 0.0) {
        return g;
    }

    const double target_pitch_deg = pitch_from_scale(clamped_scale);
    g.pitch_degrees = static_cast<float>(target_pitch_deg);
    g.pitch_radians = ndc_signed_radians_from_degrees(target_pitch_deg);

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

ndc::FloorDepthParams ndc::compute_floor_depth_params_for_geometry(
    const CameraGeometry& geom,
    double scale_value,
    bool realism_enabled) const
{
    (void)scale_value;
    FloorDepthParams p{};
    if (!realism_enabled || !geom.valid) {
        return p;
    }

    const double screen_h = static_cast<double>(screen_height_);
    if (screen_h <= 1.0) {
        return p;
    }
    if (!std::isfinite(geom.camera_height) ||
        !std::isfinite(geom.pitch_radians) ||
        !std::isfinite(geom.camera_world_y) ||
        !std::isfinite(geom.anchor_world_y)) {
        return p;
    }

    constexpr double kMaxHorizonRatio = 0.45;
    const double max_horizon = screen_h * kMaxHorizonRatio;
    const double min_horizon = -screen_h * 4.0;

    const double tan_fov   = std::tan(HALF_FOV_Y);
    const double tan_pitch = std::tan(geom.pitch_radians);
    if (!std::isfinite(tan_fov) || !std::isfinite(tan_pitch) || std::abs(tan_fov) < 1e-6) {
        return p;
    }

    // Clamp the bottom ray so it always intersects the floor in front of the camera.
    const double max_phi = (PI_D * 0.5) - 1e-3;
    double phi_bottom = geom.pitch_radians + HALF_FOV_Y;
    phi_bottom = std::clamp(phi_bottom, 1e-3, max_phi);

    const double ndc_bottom_raw = std::tan(geom.pitch_radians - phi_bottom) / tan_fov;
    const double ndc_scale = (std::isfinite(ndc_bottom_raw) && ndc_bottom_raw < -1e-4)
        ? (-1.0 / ndc_bottom_raw)
        : 1.0;
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

    double pitch_norm = geom.pitch_radians / (HALF_FOV_Y * 2.0);
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
    p.strength         = 1.0;
    p.enabled          = true;

    return p;
}

ndc::FloorDepthParams ndc::compute_floor_depth_params_for_scale(
    double scale_value,
    double anchor_world_y,
    bool realism_enabled) const
{
    const CameraGeometry geom = compute_geometry_for_scale(scale_value, anchor_world_y, realism_enabled);
    return compute_floor_depth_params_for_geometry(geom, scale_value, realism_enabled);
}

float ndc::warp_floor_screen_y(
    float world_y,
    float linear_screen_y,
    const FloorDepthParams& p) const
{
    if (!p.enabled ||
        !std::isfinite(p.horizon_screen_y) ||
        !std::isfinite(p.bottom_screen_y) ||
        !std::isfinite(p.base_world_y) ||
        !std::isfinite(p.camera_world_y) ||
        !std::isfinite(p.camera_height) ||
        !std::isfinite(p.pitch_radians) ||
        !std::isfinite(p.ndc_scale)) {
        // No pitch or realism disabled, keep the original linear mapping.
        return std::isfinite(linear_screen_y) ? linear_screen_y : 0.0f;
    }

    const double screen_h = static_cast<double>(screen_height_);
    const double safe_linear_y = std::isfinite(linear_screen_y) ? linear_screen_y : 0.0;
    const double tan_fov = std::tan(HALF_FOV_Y);
    if (!std::isfinite(tan_fov) || std::abs(tan_fov) < 1e-6) {
        return static_cast<float>(safe_linear_y);
    }

    const double depth_world = static_cast<double>(world_y) - p.camera_world_y;
    if (!std::isfinite(depth_world) || depth_world <= 1e-4) {
        return static_cast<float>(p.horizon_screen_y);
    }

    const double phi       = std::atan2(p.camera_height, depth_world);
    double       alpha     = p.pitch_radians - phi;
    const double max_alpha = HALF_FOV_Y - 1e-3;
    alpha = std::clamp(alpha, -max_alpha, max_alpha);

    double y_ndc = std::tan(alpha) / tan_fov;
    y_ndc -= p.focus_ndc_offset;
    y_ndc *= p.ndc_scale;

    const double screen_y = screen_h * (0.5 - 0.5 * y_ndc);
    if (!std::isfinite(screen_y)) {
        return static_cast<float>(safe_linear_y);
    }

    const double clamped_y = std::clamp(screen_y, p.horizon_screen_y, p.bottom_screen_y);
    return static_cast<float>(clamped_y);
}

double ndc::horizon_screen_y_for_scale_value(
    double scale_value,
    double anchor_world_y,
    bool realism_enabled) const
{
    if (screen_height_ <= 0) {
        return 0.0;
    }

    const FloorDepthParams depth = compute_floor_depth_params_for_scale(
        scale_value, anchor_world_y, realism_enabled);
    
    if (!depth.enabled) {
        return static_cast<double>(screen_height_) * 0.5;
    }

    const double extent    = static_cast<double>(screen_height_);
    const double min_bound = -4.0 * extent;
    const double max_bound = extent * 0.45;
    return std::clamp(depth.horizon_screen_y, min_bound, max_bound);
}
