#include "camera_grid.hpp"
#include "ndc.hpp"
#include "render/camera_ndc_utils.hpp"
#include "render/grid_camera.hpp"
#include "render/parallax.hpp"

#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "map_generation/room.hpp"
#include "core/find_current_room.hpp"
#include "utils/transform_smoothing_settings.hpp"
#include "utils/log.hpp"
#include "world/grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <tuple>
#include <string>
#include <nlohmann/json.hpp>

// Helper function for linear interpolation
template <typename T>
T lerp(T a, T b, double t) {
    return static_cast<T>(a + (b - a) * t);
}

namespace {
    constexpr float  kMinTau    = 1e-4f;
    constexpr double SCALE_EPS  = 1e-4;
    constexpr double BASE_RATIO = 1.1;
    constexpr double PI_D       = 3.14159265358979323846;
    constexpr float  kDefaultPitchDegrees   = 60.0f;
    constexpr float  kFixedDepthOffsetPx    = 4000.0f;
    constexpr double kMinZoomRange = 1e-4;
    constexpr double kMinPerspectiveScale   = 0.35;
    constexpr double kMaxPerspectiveScale   = 1.65;

    struct ZoomInterpolator {
        double t = 0.0;
        ZoomInterpolator(const camera_grid::RealismSettings& settings, double scale_value) {
            const double safe_low = std::max(static_cast<double>(camera_grid::kMinZoomAnchors),
                                             static_cast<double>(settings.zoom_low));
            const double safe_high = std::max(safe_low + kMinZoomRange,
                                               static_cast<double>(settings.zoom_high));
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
        const float wrapped = wrap_degrees_0_360(std::isfinite(raw_value)
            ? raw_value
            : kDefaultPitchDegrees);
        const float clamped_value = std::clamp(
            wrapped,
            camera_grid::kMinPitchDegrees,
            camera_grid::kMaxPitchDegrees);
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
        return std::clamp(
            value,
            0.0001,
            static_cast<double>(camera_grid::kMaxZoomAnchors));
    }

    camera_grid::CameraGeometry from_ndc_geometry(const ndc::CameraGeometry& src) {
        camera_grid::CameraGeometry out{};
        out.valid            = src.valid;
        out.camera_height    = src.camera_height;
        out.focus_depth      = src.focus_depth;
        out.anchor_world_y   = src.anchor_world_y;
        out.focus_ndc_offset = src.focus_ndc_offset;
        out.pitch_radians    = src.pitch_radians;
        out.pitch_degrees    = src.pitch_degrees;
        out.camera_world_y   = src.camera_world_y;
        return out;
    }

    ndc::CameraGeometry to_ndc_geometry(const camera_grid::CameraGeometry& src) {
        ndc::CameraGeometry out{};
        out.camera_height    = src.camera_height;
        out.focus_depth      = src.focus_depth;
        out.anchor_world_y   = src.anchor_world_y;
        out.camera_world_y   = src.camera_world_y;
        out.focus_ndc_offset = src.focus_ndc_offset;
        out.pitch_radians    = src.pitch_radians;
        out.pitch_degrees    = src.pitch_degrees;
        out.valid            = src.valid;
        return out;
    }

    camera_grid::FloorDepthParams from_ndc_floor_params(
        const ndc::FloorDepthParams& src,
        const camera_grid::CameraGeometry& geom) {
        camera_grid::FloorDepthParams out{};
        out.enabled            = src.enabled;
        out.horizon_screen_y   = src.horizon_screen_y;
        out.bottom_screen_y    = src.bottom_screen_y;
        out.camera_height      = src.camera_height;
        out.focus_depth        = geom.focus_depth;
        out.pitch_radians      = src.pitch_radians;
        out.anchor_world_y     = geom.anchor_world_y;
        out.base_world_y       = src.base_world_y;
        out.camera_world_y     = src.camera_world_y;
        out.focus_ndc_offset   = src.focus_ndc_offset;
        out.horizon_ndc        = src.horizon_ndc;
        out.near_ndc           = src.near_ndc;
        out.ndc_scale          = src.ndc_scale;
        out.pitch_norm         = src.pitch_norm;
        out.strength           = src.strength;
        return out;
    }

    ndc::FloorDepthParams to_ndc_floor_params(const camera_grid::FloorDepthParams& src) {
        ndc::FloorDepthParams out{};
        out.horizon_screen_y = src.horizon_screen_y;
        out.bottom_screen_y  = src.bottom_screen_y;
        out.base_world_y     = src.base_world_y;
        out.camera_world_y   = src.camera_world_y;
        out.horizon_ndc      = src.horizon_ndc;
        out.near_ndc         = src.near_ndc;
        out.ndc_scale        = src.ndc_scale;
        out.camera_height    = src.camera_height;
        out.pitch_radians    = src.pitch_radians;
        out.focus_ndc_offset = src.focus_ndc_offset;
        out.pitch_norm       = src.pitch_norm;
        out.strength         = src.strength;
        out.enabled          = src.enabled;
        return out;
    }

    struct PerspectiveRange {
        double near_distance = 0.0;
        double far_distance  = 1.0;
    };

    PerspectiveRange sanitize_perspective_range(const camera_grid::RealismSettings& settings) {
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

    double compute_floor_distance_measure(double screen_y, const camera_grid::FloorDepthParams& params) {
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

    double perspective_scale_from_measure(double measure, const PerspectiveRange& range) {
        const double denom = std::max(std::abs(range.far_distance - range.near_distance), 1e-4);
        double t = std::clamp((measure - range.near_distance) / denom, 0.0, 1.0);
        
        // Apply strong gamma curve for aggressive shrinking near horizon
        // Higher gamma makes objects shrink much faster as they approach horizon
        constexpr double kGamma = 2.5;
        t = std::pow(t, kGamma);
        
        const double scale = kMaxPerspectiveScale + (kMinPerspectiveScale - kMaxPerspectiveScale) * t;
        return std::clamp(scale, 0.01, 4.0);
    }

}
camera_grid::CameraGeometry camera_grid::compute_geometry_for_scale(double scale_value) const {
    if (!ndc_calculator_) {
        return CameraGeometry{};
    }
    const ndc::CameraGeometry ndc_geom = ndc_calculator_->compute_geometry_for_scale(
        scale_value,
        anchor_world_y(),
        realism_enabled_);
    return from_ndc_geometry(ndc_geom);
}

camera_grid::CameraGeometry camera_grid::compute_geometry() const {
    return compute_geometry_for_scale(static_cast<double>(smoothed_scale_));
}

void camera_grid::update_geometry_cache(const CameraGeometry& g) {
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

camera_grid::camera_grid(int screen_width, int screen_height, const Area& starting_zoom)
{
    screen_width_  = screen_width;
    screen_height_ = screen_height;
    aspect_        = (screen_height_ > 0)
        ? static_cast<double>(screen_width_) / static_cast<double>(screen_height_)
        : 1.0;

    // Initialize calculators
    ndc_calculator_ = std::make_unique<ndc>(screen_width, screen_height);
    grid_calculator_ = std::make_unique<grid_camera>(screen_width, screen_height);
    parallax_calculator_ = std::make_unique<parallax>();

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

    smoothed_center_.x = static_cast<float>(screen_center_.x);
    smoothed_center_.y = static_cast<float>(screen_center_.y);
    smoothed_scale_    = std::max(0.0001f, scale_);
    update_geometry_cache(compute_geometry());
}

camera_grid::~camera_grid() = default;

void camera_grid::set_realism_settings(const RealismSettings& settings) {
    settings_ = settings;
    settings_.zoom_low = std::clamp(settings_.zoom_low,
                                    camera_grid::kMinZoomAnchors,
                                    camera_grid::kMaxZoomAnchors);
    const float min_high = std::min(camera_grid::kMaxZoomAnchors, settings_.zoom_low + 0.0001f);
    settings_.zoom_high = std::clamp(settings_.zoom_high, min_high, camera_grid::kMaxZoomAnchors);
    if (!std::isfinite(settings_.base_height_px) || settings_.base_height_px <= 0.0f) {
        settings_.base_height_px = 720.0f;
    }
    settings_.tilt_zoom_in_degrees  = sanitize_pitch_degrees(settings_.tilt_zoom_in_degrees);
    settings_.tilt_zoom_out_degrees = sanitize_pitch_degrees(settings_.tilt_zoom_out_degrees);
    // Depth offset is fixed in screen space; ignore user-supplied values.
    settings_.grid_depth_offset_px = kFixedDepthOffsetPx;
    settings_.parallax_smoothing = sanitize_params(settings_.parallax_smoothing);
    if (settings_.parallax_smoothing.method == TransformSmoothingMethod::Lerp &&
        settings_.parallax_smoothing.lerp_rate <= 0.0f) {
        settings_.parallax_smoothing.lerp_rate = rate_from_tau(0.08f);
    } else if (settings_.parallax_smoothing.method == TransformSmoothingMethod::CriticallyDampedSpring &&
               settings_.parallax_smoothing.spring_frequency <= 0.0f) {
        settings_.parallax_smoothing.spring_frequency = 10.0f;
    }



    // Lock the depth offset at the fixed value so the convergence point never flips.
    settings_.depth_offset_at_zoom_low  = kFixedDepthOffsetPx;
    settings_.depth_offset_at_zoom_high = kFixedDepthOffsetPx;
    settings_.grid_depth_offset_px      = kFixedDepthOffsetPx;

    // Update NDC calculator settings
    if (ndc_calculator_) {
        ndc::Settings ndc_settings;
        ndc_settings.zoom_low = settings_.zoom_low;
        ndc_settings.zoom_high = settings_.zoom_high;
        ndc_settings.base_height_px = settings_.base_height_px;
        ndc_settings.tilt_zoom_in_degrees = settings_.tilt_zoom_in_degrees;
        ndc_settings.tilt_zoom_out_degrees = settings_.tilt_zoom_out_degrees;
        ndc_settings.base_height_at_zoom_low = settings_.base_height_at_zoom_low;
        ndc_settings.base_height_at_zoom_high = settings_.base_height_at_zoom_high;
        ndc_calculator_->set_settings(ndc_settings);
    }
    
    update_geometry_cache(compute_geometry());
}

void camera_grid::set_screen_center(SDL_Point p) {
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
    smoothed_center_.x = static_cast<float>(screen_center_.x);
    smoothed_center_.y = static_cast<float>(screen_center_.y);
}

void camera_grid::set_scale(float s) {
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

float camera_grid::get_scale() const {
    return smoothed_scale_;
}

void camera_grid::zoom_to_scale(double target_scale, int duration_steps) {
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

void camera_grid::zoom_to_area(const Area& target_area, int duration_steps) {
    Area adjusted = convert_area_to_aspect(target_area);
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int tgt_w  = std::max(1, width_from_area(adjusted));
    const double target = static_cast<double>(tgt_w) / static_cast<double>(base_w);
    zoom_to_scale(target, duration_steps);
}

void camera_grid::update(float dt) {
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
    }

    const float safe_sx = static_cast<float>(screen_center_.x);
    const float safe_sy = static_cast<float>(screen_center_.y);
    const float safe_ss = std::max(0.0001f, scale_);

    const float clamped_ss = static_cast<float>(std::clamp(static_cast<double>(safe_ss), 0.0001, static_cast<double>(camera_grid::kMaxZoomAnchors)));

    smoothed_center_.x = std::clamp(safe_sx, -1e8f, 1e8f);
    smoothed_center_.y = std::clamp(safe_sy, -1e8f, 1e8f);
    smoothed_scale_    = clamped_ss;

    recompute_current_view();
}

double camera_grid::compute_room_scale_from_area(const Room* room) const {
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

void camera_grid::set_up_rooms(CurrentRoomFinder* finder) {
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



void camera_grid::update_zoom(Room* cur,
                         CurrentRoomFinder* finder,
                         Asset* player,
                         bool refresh_requested,
                         float dt,
                         bool dev_mode)
{
    pan_offset_x_ = 0.0;
    pan_offset_y_ = 0.0;

    if (!pan_override_) {
        // In normal mode, lock camera center to player when present.
        // In dev mode we intentionally avoid forcing the camera to follow
        // the player so panning/zooming can be used to inspect the scene.
        if (player && !dev_mode) {
            set_screen_center(SDL_Point{ player->pos.x, player->pos.y });
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

Area camera_grid::convert_area_to_aspect(const Area& in) const {
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

void camera_grid::recompute_current_view() {
    const int base_w = std::max(1, width_from_area(base_zoom_));
    const int base_h = std::max(1, height_from_area(base_zoom_));
    const double scale_value = std::max(0.0001, static_cast<double>(smoothed_scale_));
    const int cur_w  = static_cast<int>(std::lround(static_cast<double>(base_w) * scale_value));
    const int cur_h  = static_cast<int>(std::lround(static_cast<double>(base_h) * scale_value));
    SDL_Point center{
        static_cast<int>(std::lround(smoothed_center_.x)),
        static_cast<int>(std::lround(smoothed_center_.y))
    };
    current_view_ = make_rect_area("current_view", center, cur_w, cur_h, 0);
    update_geometry_cache(compute_geometry());
}

void camera_grid::pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps) {
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

void camera_grid::pan_and_zoom_to_asset(const Asset* a, double zoom_scale_factor, int duration_steps) {
    if (!a) return;
    SDL_Point target{ a->pos.x, a->pos.y };
    pan_and_zoom_to_point(target, zoom_scale_factor, duration_steps);
}

void camera_grid::animate_zoom_multiply(double factor, int duration_steps) {
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

void camera_grid::animate_zoom_towards_point(double factor, SDL_Point screen_point, int duration_steps) {
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


SDL_FPoint camera_grid::map_to_screen(SDL_Point world) const {
    SDL_FPoint world_f{ static_cast<float>(world.x), static_cast<float>(world.y) };
    return map_to_screen_f(world_f);
}

SDL_FPoint camera_grid::map_to_screen_f(SDL_FPoint world) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double inv_scale =
        (smoothed_scale_ > 0.000001f)
            ? (1.0 / static_cast<double>(smoothed_scale_))
            : 1e6;
    const double sx = (static_cast<double>(world.x) - static_cast<double>(left)) * inv_scale;
    const double sy = (static_cast<double>(world.y) - static_cast<double>(top)) * inv_scale + static_cast<double>(player_center_offset_y_);
    const double safe_sx = std::isfinite(sx) ? sx : static_cast<double>(left);
    const double safe_sy = std::isfinite(sy) ? sy : static_cast<double>(top);
    const float out_x = static_cast<float>(std::clamp(safe_sx, -1e8, 1e8));
    const float out_y = static_cast<float>(std::clamp(safe_sy, -1e8, 1e8));
    return SDL_FPoint{ out_x, out_y };
}



SDL_FPoint camera_grid::screen_to_map(SDL_Point screen) const {
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const double s = static_cast<double>(std::max(0.000001f, smoothed_scale_));
    // Apply inverse of player centering offset to screen Y before converting to world coordinates
    const double adjusted_screen_y = static_cast<double>(screen.y) - static_cast<double>(player_center_offset_y_);
    double wx = static_cast<double>(left) + static_cast<double>(screen.x) * s;
    double wy = static_cast<double>(top)  + adjusted_screen_y * s;
    const double safe_wx = std::isfinite(wx) ? wx : static_cast<double>(left);
    const double safe_wy = std::isfinite(wy) ? wy : static_cast<double>(top);
    const float out_wx = static_cast<float>(std::clamp(safe_wx, -1e8, 1e8));
    const float out_wy = static_cast<float>(std::clamp(safe_wy, -1e8, 1e8));
    return SDL_FPoint{ out_wx, out_wy };
}
camera_grid::RenderEffects camera_grid::compute_render_effects(
    SDL_Point world,
    float asset_screen_height,
    float reference_screen_height,
    RenderSmoothingKey /*smoothing_key*/) const
{
    RenderEffects result;

    SDL_FPoint world_f{ static_cast<float>(world.x), static_cast<float>(world.y) };
    SDL_FPoint linear_screen = map_to_screen_f(world_f);
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

    // ---------------------------------------------------------------------
    // Distance scaling: use screen-space position relative to horizon.
    // Assets near the horizon (top) should be smaller.
    // Assets near the bottom should be larger.
    // ---------------------------------------------------------------------

    FloorDepthParams p = runtime_floor_params_;
    if (!p.enabled) {
        // Fallback to fresh params if cache is empty (early frames).
        p = compute_floor_depth_params();
    }

    const double horizon = p.horizon_screen_y;
    const double bottom  = p.bottom_screen_y;
    if (!std::isfinite(horizon) || !std::isfinite(bottom) || bottom <= horizon + EPS) {
        // Bad or degenerate range: bail out to neutral scaling.
        return result;
    }

    const PerspectiveRange range = sanitize_perspective_range(settings_);
    
    // Use warped screen Y for consistent distance measurement
    const double distance_measure = compute_floor_distance_measure(warped_screen.y, p);
    double depth_scale = perspective_scale_from_measure(distance_measure, range);
    
    // Additional scale factor based on distance from horizon for extra shrinking
    const double horizon_dist = std::max(0.0, static_cast<double>(warped_screen.y) - p.horizon_screen_y);
    const double horizon_range = std::max(1.0, p.bottom_screen_y - p.horizon_screen_y);
    const double horizon_factor = std::clamp(horizon_dist / horizon_range, 0.0, 1.0);
    const double horizon_scale = 0.1 + 0.9 * std::pow(horizon_factor, 1.5);
    depth_scale *= horizon_scale;

    // Optional blend with any screen-height hint if you ever pass a real asset_screen_height.
    double final_scale = depth_scale;
    if (reference_screen_height > EPS && asset_screen_height > EPS) {
        double screen_based_scale = std::clamp(
            static_cast<double>(reference_screen_height) /
            std::max(static_cast<double>(asset_screen_height), EPS),
            0.35,
            1.5);
        final_scale = 0.5 * depth_scale + 0.5 * screen_based_scale;
    }

    // Final clamp to keep sprites reasonable.
    const double distance_scale = std::clamp(final_scale, 0.01, 4.0);
    result.distance_scale = static_cast<float>(distance_scale);

    // ---------------------------------------------------------------------
    // Horizon fade: aggressively fade and shrink sprites as they approach horizon
    // ---------------------------------------------------------------------
    result.horizon_fade_alpha = 1.0f;
    float horizon_scale_multiplier = 1.0f;
    
    const float fade_band_px = std::max(1.0f, settings_.horizon_fade_band_px);
    const float horizon_y = static_cast<float>(horizon);
    const float screen_y = warped_screen.y;
    
    // Distance from horizon (negative = above horizon, positive = below)
    const float dist_from_horizon = screen_y - horizon_y;
    
    if (dist_from_horizon <= 0.5f) {
        // At or above horizon: completely invisible and zero scale
        result.horizon_fade_alpha = 0.0f;
        horizon_scale_multiplier = 0.0f;
    } else if (dist_from_horizon < fade_band_px) {
        // Within fade band: aggressive fadeout with cubic easing
        const float t = dist_from_horizon / fade_band_px;
        
        // Cubic easing for more dramatic fade
        const float fade_alpha = t * t * t;
        result.horizon_fade_alpha = std::clamp(fade_alpha, 0.0f, 1.0f);
        
        // Even stronger scale reduction - quadratic so objects shrink to nearly nothing
        const float scale_factor = t * t;
        horizon_scale_multiplier = std::clamp(scale_factor, 0.01f, 1.0f);
    }
    
    // Apply horizon scale multiplier to distance scale
    result.distance_scale *= horizon_scale_multiplier;
    result.distance_scale = std::clamp(result.distance_scale, 0.001f, 4.0f);
    // ---------------------------------------------------------------------

    if (!std::isfinite(result.vertical_scale) || result.vertical_scale <= 0.0f) {
        result.vertical_scale = 1.0f;
    } else {
        result.vertical_scale = std::clamp(result.vertical_scale, 0.1f, 2.0f);
    }

    if (!std::isfinite(result.distance_scale) || result.distance_scale <= 0.0f) {
        result.distance_scale = 1.0f;
    } else {
        result.distance_scale = std::clamp(result.distance_scale, 0.1f, 4.0f);
    }

    if (!std::isfinite(result.screen_position.x) || !std::isfinite(result.screen_position.y)) {
        result.screen_position = linear_screen;
    }

    return result;
}


void camera_grid::apply_camera_settings(const nlohmann::json& data) {
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

    const std::array<std::pair<const char*, float*>, 23> float_fields{ {
        { "base_height_at_zoom_low", &settings_.base_height_at_zoom_low },
        { "base_height_at_zoom_high", &settings_.base_height_at_zoom_high },
        { "extra_cull_margin", &settings_.extra_cull_margin },
        { "zoom_low", &settings_.zoom_low },
        { "zoom_high", &settings_.zoom_high },
        { "base_height_px", &settings_.base_height_px },
        { "min_visible_screen_ratio", &settings_.min_visible_screen_ratio },
        { "tilt_zoom_in_degrees", &settings_.tilt_zoom_in_degrees },
        { "tilt_zoom_out_degrees", &settings_.tilt_zoom_out_degrees },
        { "grid_depth_offset_px", &settings_.grid_depth_offset_px },
        { "depth_offset_at_zoom_low", &settings_.depth_offset_at_zoom_low },
        { "depth_offset_at_zoom_high", &settings_.depth_offset_at_zoom_high },
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
        { "perspective_scale_gamma", &settings_.perspective_scale_gamma }
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

    // Force depth offset to the fixed value after loading.
    settings_.grid_depth_offset_px   = kFixedDepthOffsetPx;
    settings_.depth_offset_at_zoom_low  = kFixedDepthOffsetPx;
    settings_.depth_offset_at_zoom_high = kFixedDepthOffsetPx;

    if (!std::isfinite(settings_.zoom_low)) {
        settings_.zoom_low = 0.75f;
    }

    if (!std::isfinite(settings_.zoom_high)) {
        settings_.zoom_high = std::max(settings_.zoom_low + 0.25f, 1.0f);
    }


    if (!std::isfinite(settings_.base_height_px) || settings_.base_height_px <= 0.0f) {
        settings_.base_height_px = 720.0f;
    }


    if (!std::isfinite(settings_.tilt_zoom_in_degrees )) {
        settings_.tilt_zoom_in_degrees  = 345.0f;
    }
    if (!std::isfinite(settings_.tilt_zoom_out_degrees)) {
        settings_.tilt_zoom_out_degrees = 310.0f;
    }
    settings_.tilt_zoom_in_degrees   = sanitize_pitch_degrees(settings_.tilt_zoom_in_degrees );
    settings_.tilt_zoom_out_degrees = sanitize_pitch_degrees(settings_.tilt_zoom_out_degrees);

    settings_.depth_offset_at_zoom_low = kFixedDepthOffsetPx;
    settings_.depth_offset_at_zoom_high = kFixedDepthOffsetPx;
    settings_.grid_depth_offset_px = kFixedDepthOffsetPx;

    if (!std::isfinite(settings_.min_visible_screen_ratio) ||
        settings_.min_visible_screen_ratio < 0.0f) {
        settings_.min_visible_screen_ratio = 0.015f;
    } else {
        settings_.min_visible_screen_ratio =
            std::clamp(settings_.min_visible_screen_ratio, 0.0f, 0.5f);
    }

    settings_.zoom_low = std::clamp(settings_.zoom_low,
                                    camera_grid::kMinZoomAnchors,
                                    camera_grid::kMaxZoomAnchors);
    const float min_high = std::min(camera_grid::kMaxZoomAnchors, settings_.zoom_low + 0.0001f);
    settings_.zoom_high = std::clamp(settings_.zoom_high, min_high, camera_grid::kMaxZoomAnchors);

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

    if (ndc_calculator_) {
        ndc::Settings ndc_settings;
        ndc_settings.zoom_low = settings_.zoom_low;
        ndc_settings.zoom_high = settings_.zoom_high;
        ndc_settings.base_height_px = settings_.base_height_px;
        ndc_settings.tilt_zoom_in_degrees = settings_.tilt_zoom_in_degrees;
        ndc_settings.tilt_zoom_out_degrees = settings_.tilt_zoom_out_degrees;
        ndc_settings.base_height_at_zoom_low = settings_.base_height_at_zoom_low;
        ndc_settings.base_height_at_zoom_high = settings_.base_height_at_zoom_high;
        ndc_calculator_->set_settings(ndc_settings);
    }

    recompute_current_view();
}

nlohmann::json camera_grid::camera_settings_to_json() const {
    nlohmann::json j = nlohmann::json::object();
    j["realism_enabled"] = realism_enabled_;

    const std::pair<const char*, float> float_fields[] = {
        { "extra_cull_margin", settings_.extra_cull_margin },
        { "depth_offset_at_zoom_low", settings_.depth_offset_at_zoom_low },
        { "depth_offset_at_zoom_high", settings_.depth_offset_at_zoom_high },
        { "base_height_at_zoom_low", settings_.base_height_at_zoom_low },
        { "base_height_at_zoom_high", settings_.base_height_at_zoom_high },
        { "zoom_low", settings_.zoom_low },
        { "zoom_high", settings_.zoom_high },
        { "perspective_distance_at_scale_zero", settings_.perspective_distance_at_scale_zero },
        { "perspective_distance_at_scale_hundred", settings_.perspective_distance_at_scale_hundred },
        { "base_height_px", settings_.base_height_px },
        { "tilt_zoom_in_degrees", settings_.tilt_zoom_in_degrees },
        { "tilt_zoom_out_degrees", settings_.tilt_zoom_out_degrees },
        { "min_visible_screen_ratio", settings_.min_visible_screen_ratio },
        { "scale_hysteresis_margin", settings_.scale_variant_hysteresis_margin },
        { "parallax_smoothing_lerp_rate", settings_.parallax_smoothing.lerp_rate },
        { "parallax_smoothing_spring_frequency", settings_.parallax_smoothing.spring_frequency },
        { "parallax_smoothing_max_step", settings_.parallax_smoothing.max_step },
        { "parallax_smoothing_snap_threshold", settings_.parallax_smoothing.snap_threshold },
        { "foreground_plane_screen_y", settings_.foreground_plane_screen_y },
        { "background_plane_screen_y", settings_.background_plane_screen_y },
        { "grid_depth_offset_px", settings_.grid_depth_offset_px },
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
SDL_FPoint camera_grid::get_view_center_f() const {
    if (std::isfinite(smoothed_center_.x) && std::isfinite(smoothed_center_.y)) {
        return smoothed_center_;
    }
    int left, top, right, bottom;
    std::tie(left, top, right, bottom) = current_view_.get_bounds();
    const float cx = (static_cast<float>(left) + static_cast<float>(right)) * 0.5f;
    const float cy = (static_cast<float>(top)  + static_cast<float>(bottom)) * 0.5f;
    return SDL_FPoint{ cx, cy };
}

// Floor depth helpers for warped grid lines using actual camera_grid height (from zoom) and pitch.


camera_grid::FloorDepthParams camera_grid::compute_floor_depth_params_for_geometry(const CameraGeometry& geom, double scale_value) const {
    if (!ndc_calculator_ || !geom.valid) {
        return FloorDepthParams{};
    }
    const ndc::CameraGeometry ndc_geom = to_ndc_geometry(geom);
    const ndc::FloorDepthParams ndc_params = ndc_calculator_->compute_floor_depth_params_for_geometry(
        ndc_geom,
        scale_value,
        realism_enabled_);
    return from_ndc_floor_params(ndc_params, geom);
}

camera_grid::FloorDepthParams camera_grid::compute_floor_depth_params_for_scale(double scale_value) const {
    const CameraGeometry geom = compute_geometry_for_scale(scale_value);
    return compute_floor_depth_params_for_geometry(geom, scale_value);
}

camera_grid::FloorDepthParams camera_grid::compute_floor_depth_params() const {
    const CameraGeometry geom = compute_geometry();
    return compute_floor_depth_params_for_geometry(geom, static_cast<double>(smoothed_scale_));
}
float camera_grid::warp_floor_screen_y(float world_y, float linear_screen_y) const {
    if (!ndc_calculator_) {
        return std::isfinite(linear_screen_y) ? linear_screen_y : 0.0f;
    }
    FloorDepthParams p = runtime_floor_params_;
    if (!p.enabled) {
        // Fallback to a fresh computation if the cache is empty (e.g., before the first update).
        p = compute_floor_depth_params();
    }
    const ndc::FloorDepthParams native = to_ndc_floor_params(p);
    return ndc_calculator_->warp_floor_screen_y(world_y, linear_screen_y, native);
}

double camera_grid::view_height_world() const {
    int minx = 0, miny = 0, maxx = 0, maxy = 0;
    std::tie(minx, miny, maxx, maxy) = current_view_.get_bounds();
    return static_cast<double>(std::max(0, maxy - miny));
}

double camera_grid::anchor_world_y() const {
    // Anchor at the camera_grid focus to keep depth ordering stable and avoid inversion.
    return static_cast<double>(smoothed_center_.y);
}

double camera_grid::zoom_lerp_t_for_scale(double scale_value) const {
    return ZoomInterpolator(settings_, scale_value).t;
}

float camera_grid::depth_offset_for_scale(double scale_value) const {
    (void)scale_value;
    return kFixedDepthOffsetPx;
}

double camera_grid::horizon_screen_y_for_scale_value(double scale_value) const {
    if (!realism_enabled_) {
        return 0.0;
    }
    if (!ndc_calculator_) {
        return screen_height_ > 0 ? static_cast<double>(screen_height_) * 0.5 : 0.0;
    }

    const double cached_scale = static_cast<double>(smoothed_scale_);
    const double kScaleEps = 1e-6;
    if (std::abs(scale_value - cached_scale) <= kScaleEps && runtime_floor_params_.enabled) {
        const double extent    = static_cast<double>(screen_height_);
        const double min_bound = -4.0 * extent;
        const double max_bound = extent * 0.45;
        return std::clamp(runtime_floor_params_.horizon_screen_y, min_bound, max_bound);
    }

    return ndc_calculator_->horizon_screen_y_for_scale_value(
        scale_value,
        anchor_world_y(),
        realism_enabled_);
}

double camera_grid::horizon_screen_y_for_scale() const {
    return horizon_screen_y_for_scale_value(static_cast<double>(smoothed_scale_));
}

// Minimal grid state management and lookup implementations.
void camera_grid::clear_grid_state() {
    warped_points_.clear();
    visible_assets_.clear();
    visible_points_.clear();
    active_chunks_.clear();
    id_to_index_.clear();
    cached_world_rect_ = SDL_Rect{0, 0, 0, 0};
    bounds_ = GridBounds{};
}

void camera_grid::rebuild_grid_bounds() {
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

    // Populate a conservative screen-space bounds; callers may overwrite later.
    bounds_.left = 0.0f;
    bounds_.top = 0.0f;
    bounds_.right = static_cast<float>(screen_width_);
    bounds_.bottom = static_cast<float>(screen_height_);
}

void camera_grid::rebuild_grid(world::Grid& world_grid, float dt_seconds) {
    clear_grid_state();

    world_grid.update_parallax(*this, dt_seconds);

    std::vector<Asset*> assets = world_grid.all_assets();
    warped_points_.reserve(assets.size());
    visible_assets_.reserve(assets.size());
    visible_points_.reserve(assets.size());

    const float inv_scale   = 1.0f / std::max(0.000001f, smoothed_scale_);
    const float screen_w    = static_cast<float>(screen_width_);
    const float screen_h    = static_cast<float>(screen_height_);
    
    // Compute player centering offset: find player and calculate Y offset needed to center them
    player_center_offset_y_ = 0.0f;
    Asset* player_asset = nullptr;
    for (Asset* a : assets) {
        if (a && a->info && a->info->type == "player") {
            player_asset = a;
            break;
        }
    }
    
    if (player_asset) {
        // Temporarily compute player's screen Y without offset to determine what offset is needed
        const float old_offset = player_center_offset_y_;
        player_center_offset_y_ = 0.0f;
        
        SDL_Point player_world{ player_asset->pos.x, player_asset->pos.y };
        SDL_FPoint player_screen_base = map_to_screen(player_world);
        
        // Apply warping to get the final Y position
        const float player_world_y_f = static_cast<float>(player_world.y);
        const float player_warped_y = warp_floor_screen_y(player_world_y_f, player_screen_base.y);
        const float player_final_y = std::isfinite(player_warped_y) ? player_warped_y : player_screen_base.y;
        
        // Calculate offset to center player vertically at screen center
        const float screen_center_y = screen_h * 0.5f;
        player_center_offset_y_ = screen_center_y - player_final_y;
    }
    
    const float horizon_y   = static_cast<float>(horizon_screen_y_for_scale());
    const float margin_px   = std::max(0.0f, settings_.extra_cull_margin);
    const float bottom_pad  = std::max(settings_.grid_depth_offset_px, margin_px);
    const float cull_top    = std::max(0.0f, horizon_y - margin_px);
    const SDL_FRect cull_rect{
        -margin_px,
        cull_top,
        screen_w + margin_px * 2.0f,
        (screen_h + bottom_pad) - cull_top
    };
    const float min_visible_px =
        screen_h * std::clamp(settings_.min_visible_screen_ratio, 0.0f, 0.5f);

    // ------------------------------------------------------------------
    // Distance-based perspective context
    // ------------------------------------------------------------------
    // Use current floor depth params to derive two reference distances
    // (far and near) that map to perspective scales 0 and 100.
    // We approximate distance along the floor using NDC depth.
    FloorDepthParams depth_params = runtime_floor_params_;
    if (!depth_params.enabled) {
        depth_params = compute_floor_depth_params();
    }

    const PerspectiveRange perspective_range = sanitize_perspective_range(settings_);

    // Cache the current mapping distances for debugging/inspection.
    perspective_distance_at_scale_zero_    = perspective_range.far_distance;
    perspective_distance_at_scale_hundred_ = perspective_range.near_distance;

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

        SDL_FPoint screen_pos = world_grid.floor_warped_screen_position(*this, world_pos);
        if (!std::isfinite(screen_pos.x) || !std::isfinite(screen_pos.y)) {
            screen_pos = map_to_screen(world_pos);
        }

        const float parallax_dx = world_grid.parallax_offset(world_pos);
        const RenderEffects effects = compute_render_effects(
            world_pos,
            0.0f,
            settings_.base_height_px,
            RenderSmoothingKey(a));

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
        const bool on_screen = rects_intersect(bounds, cull_rect);

        gp->screen             = screen_pos;
        gp->parallax_dx        = parallax_dx;
        gp->vertical_scale     = effects.vertical_scale;

        // Distance-based perspective scale: assets rely solely on this
        // value (and their base scale). We estimate the floor distance
        // by using world Y position relative to horizon in world space.
        double camera_world_y = depth_params.camera_world_y;
        double base_world_y = depth_params.base_world_y;
        double asset_world_y = static_cast<double>(world_pos.y);
        double distance_measure = 0.0;
        if (std::isfinite(camera_world_y) && std::isfinite(base_world_y) && base_world_y != camera_world_y) {
            distance_measure = std::clamp((asset_world_y - camera_world_y) / (base_world_y - camera_world_y), 0.0, 1.0);
        }
        const double perspective_scale_value = perspective_scale_from_measure(distance_measure, perspective_range);

        gp->perspective_scale  = static_cast<float>(perspective_scale_value);
        gp->distance_to_camera = static_cast<float>(distance_measure);
        gp->tilt_radians       = runtime_pitch_rad_;
        gp->on_screen          = on_screen;

        // Calculate depth cue opacities
        float fg_opacity = 0.0f;
        float bg_opacity = 1.0f;
        
        if (on_screen && !gp->occupants.empty()) {
            // Only calculate if on screen and has assets
            float screen_y = screen_pos.y;
            float fg_y = settings_.foreground_plane_screen_y;
            float bg_y = settings_.background_plane_screen_y;
            
            // Simple linear falloff for now, can be expanded based on texture_opacity_falloff_method
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
            
            // Apply max opacity settings
            fg_opacity *= (static_cast<float>(settings_.foreground_texture_max_opacity) / 255.0f);
            bg_opacity *= (static_cast<float>(settings_.background_texture_max_opacity) / 255.0f);
        }
        
        // gp->depth_cue_foreground_opacity = fg_opacity;
        // gp->depth_cue_background_opacity = bg_opacity;

        id_to_index_[gp->id] = warped_points_.size();
        warped_points_.push_back(gp);
        if (on_screen) {
            visible_assets_.push_back(a);
            visible_points_.push_back(gp);
        }
        if (gp->chunk) active_chunks_.push_back(gp->chunk);
    }

    // Deduplicate active chunks
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

world::GridPoint* camera_grid::grid_point_for_asset(const Asset* asset) {
    if (!asset) return nullptr;
    const std::uint64_t id = asset->grid_id();
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) return nullptr;
    std::size_t idx = it->second;
    if (idx >= warped_points_.size()) return nullptr;
    return warped_points_[idx];
}

const world::GridPoint* camera_grid::grid_point_for_asset(const Asset* asset) const {
    if (!asset) return nullptr;
    const std::uint64_t id = asset->grid_id();
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) return nullptr;
    std::size_t idx = it->second;
    if (idx >= warped_points_.size()) return nullptr;
    return warped_points_[idx];
}

// RenderSmoothingKey constructor
camera_grid::RenderSmoothingKey::RenderSmoothingKey(const Asset* asset, int frame)
    : asset_id(asset
        ? (asset->grid_id() != 0
            ? asset->grid_id()
            : static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(asset)))
        : 0),
      frame_index(frame) {}

// Focus and zoom override methods
void camera_grid::set_focus_override(SDL_Point focus) {
    focus_override_ = true;
    focus_point_ = focus;
}

void camera_grid::set_manual_zoom_override(bool enabled) {
    manual_zoom_override_ = enabled;
}

void camera_grid::clear_focus_override() {
    focus_override_ = false;
}

void camera_grid::clear_manual_zoom_override() {
    manual_zoom_override_ = false;
}

// Default zoom for room
double camera_grid::default_zoom_for_room(const Room* room) const {
    return compute_room_scale_from_area(room);
}

// Recompute current view
