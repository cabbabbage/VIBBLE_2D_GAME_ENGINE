#pragma once

#include "utils/transform_smoothing.hpp"
#include "render/image_effect_settings.hpp"
#include "utils/area.hpp"
#include <SDL.h>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

class Asset;
class Room;
class CurrentRoomFinder;
namespace world {
    class WorldGrid;
    struct GridPoint;
    struct Chunk;
}

class WarpedScreenGrid {
public:

    static constexpr float kMinZoomAnchors = 0.5f;
    static constexpr float kMaxZoomAnchors = 20.0f;
    static constexpr float kMinPitchDegrees = 0.0f;
    static constexpr float kMaxPitchDegrees = 150.0f;
    static constexpr bool kForceDepthPerspectiveDisabled = true;

    enum class BlurFalloffMethod {
        Linear = 0,
        Quadratic = 1,
        Cubic = 2,
        Logarithmic = 3,
        Exponential = 4
};

    struct RealismSettings {

        float min_visible_screen_ratio     = 0.015f;

        float zoom_low                     = 0.75f;
        float zoom_high                    = 3.0f;

        float base_height_px               = 1000.0f;

        int   render_quality_percent       = 100;

        TransformSmoothingParams  parallax_smoothing{};
        float parallax_smoothing_snap_threshold = 0.0f;

        float scale_variant_hysteresis_margin = 0.05f;

        int   foreground_texture_max_opacity  = 255;
        int   background_texture_max_opacity  = 255;
        float foreground_plane_screen_y       = 1080.0f;
        float background_plane_screen_y       = 0.0f;
        BlurFalloffMethod texture_opacity_falloff_method = BlurFalloffMethod::Linear;

        float extra_cull_margin = 300.0f;

        float perspective_distance_at_scale_zero   = 1.0f;
        float perspective_distance_at_scale_hundred = 0.5f;

        float horizon_fade_band_px = 150.0f;

        float perspective_scale_gamma = 2.5f;

        camera_effects::ImageEffectSettings foreground_effects{};
        camera_effects::ImageEffectSettings background_effects{};
};

    struct CameraGeometry {
        bool valid = false;
        double camera_height = 0.0;
        double focus_depth = 0.0;
        double anchor_world_y = 0.0;
        double focus_ndc_offset = 0.0;
        double pitch_radians = 0.0;
        float pitch_degrees = 0.0f;
        double camera_world_y = 0.0;
};

    struct FloorDepthParams {
        bool enabled = false;
        double horizon_screen_y = 0.0;
        double bottom_screen_y = 0.0;
        double camera_height = 0.0;
        double focus_depth = 0.0;
        double pitch_radians = 0.0;
        double anchor_world_y = 0.0;
        double base_world_y = 0.0;
        double camera_world_y = 0.0;
        double focus_ndc_offset = 0.0;
        double horizon_ndc = 0.0;
        double near_ndc = -1.0;
        double ndc_scale = 1.0;
        double pitch_norm = 0.0;
        double strength = 0.0;
};

    struct RenderEffects {
        SDL_FPoint screen_position{0.0f, 0.0f};
        float vertical_scale = 1.0f;
        float distance_scale = 1.0f;
        float horizon_fade_alpha = 1.0f;
};

    struct GridBounds {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
};

    struct RenderSmoothingKey {
        std::uint64_t asset_id = 0;
        int frame_index = 0;

        RenderSmoothingKey() = default;
        RenderSmoothingKey(std::uint64_t id, int frame) : asset_id(id), frame_index(frame) {}
        explicit RenderSmoothingKey(const Asset* asset, int frame = 0);
};

    WarpedScreenGrid(int screen_width, int screen_height, const Area& starting_zoom);
    ~WarpedScreenGrid();

    void set_scale(float s);
    float get_scale() const;
    void zoom_to_scale(double target_scale, int duration_steps);
    void zoom_to_area(const Area& target_area, int duration_steps);
    void animate_zoom_multiply(double factor, int duration_steps);
    void animate_zoom_towards_point(double factor, SDL_Point screen_point, int duration_steps);

    void pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps);
    void pan_and_zoom_to_asset(const Asset* a, double zoom_scale_factor, int duration_steps);

    void update(float dt);
    void update_zoom(Room* cur, CurrentRoomFinder* finder, Asset* player, bool refresh_requested, float dt, bool dev_mode = false);

    void set_focus_override(SDL_Point focus);
    void set_manual_zoom_override(bool enabled);
    void clear_focus_override();
    void clear_manual_zoom_override();
    bool has_focus_override() const { return focus_override_; }
    bool is_manual_zoom_override() const { return manual_zoom_override_; }
    SDL_Point get_focus_override_point() const { return focus_point_; }

    void set_realism_settings(const RealismSettings& settings);
    void set_screen_center(SDL_Point p, bool snap_immediately = true);
    void set_up_rooms(CurrentRoomFinder* finder);
    void apply_camera_settings(const nlohmann::json& data);
    nlohmann::json camera_settings_to_json() const;

    SDL_FPoint map_to_screen(SDL_Point world) const;
    SDL_FPoint map_to_screen_f(SDL_FPoint world) const;
    SDL_FPoint screen_to_map(SDL_Point screen) const;

    RenderEffects compute_render_effects(SDL_Point world, float asset_screen_height, float reference_screen_height, RenderSmoothingKey smoothing_key) const;

    CameraGeometry compute_geometry() const;
    CameraGeometry compute_geometry_for_scale(double scale_value) const;
    void update_geometry_cache(const CameraGeometry& g);

    FloorDepthParams compute_floor_depth_params() const;
    FloorDepthParams compute_floor_depth_params_for_scale(double scale_value) const;
    FloorDepthParams compute_floor_depth_params_for_geometry(const CameraGeometry& geom, double scale_value) const;
    const FloorDepthParams& current_floor_depth_params() const { return runtime_floor_params_; }
    float warp_floor_screen_y(float world_y, float linear_screen_y) const;

    double current_camera_height() const { return runtime_camera_height_; }
    double current_focus_depth() const { return runtime_focus_depth_; }
    double current_focus_ndc_offset() const { return runtime_focus_ndc_offset_; }
    float current_depth_offset_px() const { return runtime_depth_offset_px_; }
    double current_anchor_world_y() const { return runtime_anchor_world_y_; }
    double current_pitch_radians() const { return runtime_pitch_rad_; }
    float current_pitch_degrees() const { return runtime_pitch_deg_; }

    double view_height_world() const;
    double view_height_for_scale(double scale_value) const;
    double anchor_world_y() const;
    double zoom_lerp_t_for_scale(double scale_value) const;
    float depth_offset_for_scale(double scale_value) const;
    double horizon_screen_y_for_scale() const;
    double horizon_screen_y_for_scale_value(double scale_value) const;
    SDL_FPoint get_view_center_f() const;
    SDL_Point get_screen_center() const {
        return SDL_Point{
            static_cast<int>(smoothed_center_.x), static_cast<int>(smoothed_center_.y) };
    }
    void recompute_current_view();

    void clear_grid_state();
    void rebuild_grid_bounds();
    void rebuild_grid(world::WorldGrid& world_grid, float dt_seconds);
    void project_to_screen(world::GridPoint& point) const;
    world::GridPoint* grid_point_for_asset(const Asset* asset);
    const world::GridPoint* grid_point_for_asset(const Asset* asset) const;

    const RealismSettings& get_settings() const { return settings_; }
    RealismSettings& get_settings() { return settings_; }
    const RealismSettings& realism_settings() const { return settings_; }
    RealismSettings& realism_settings() { return settings_; }
    bool is_realism_enabled() const { return !kForceDepthPerspectiveDisabled && realism_enabled_; }
    bool realism_enabled() const { return is_realism_enabled(); }
    bool parallax_enabled() const { return is_realism_enabled(); }
    void set_realism_enabled(bool enabled) {
        if (kForceDepthPerspectiveDisabled) {
            (void)enabled;
            realism_enabled_ = false;
        } else {
            realism_enabled_ = enabled;
        }
    }
    void set_parallax_enabled(bool enabled) { set_realism_enabled(enabled); }
    void set_render_areas_enabled(bool enabled) { render_areas_enabled_ = enabled; }
    const Area& get_current_view() const { return current_view_; }
    const Area& get_camera_area() const { return current_view_; }
    bool is_zooming() const { return zooming_; }
    double default_zoom_for_room(const Room* room) const;
    const std::vector<world::GridPoint*>& get_warped_points() const { return warped_points_; }
    const std::vector<Asset*>& get_visible_assets() const { return visible_assets_; }
    const std::vector<world::GridPoint*>& get_visible_points() const { return visible_points_; }
    const std::vector<world::GridPoint*>& grid_visible_points() const { return visible_points_; }
    const std::vector<world::Chunk*>& get_active_chunks() const { return active_chunks_; }
    const GridBounds& get_bounds() const { return bounds_; }
    const SDL_Rect& get_cached_world_rect() const { return cached_world_rect_; }
    Area convert_area_to_aspect(const Area& in) const;

private:

    double compute_room_scale_from_area(const Room* room) const;

    int screen_width_ = 0;
    int screen_height_ = 0;
    double aspect_ = 1.0;

    bool realism_enabled_ = false;
    bool render_areas_enabled_ = false;
    RealismSettings settings_{};

    Area base_zoom_;
    Area current_view_;

    SDL_Point screen_center_{0, 0};
    SDL_FPoint smoothed_center_{0.0f, 0.0f};
    bool screen_center_initialized_ = false;
    double pan_offset_x_ = 0.0;
    double pan_offset_y_ = 0.0;

    float scale_ = 1.0f;
    float smoothed_scale_ = 1.0f;
    bool zooming_ = false;
    int steps_total_ = 0;
    int steps_done_ = 0;
    double start_scale_ = 1.0;
    double target_scale_ = 1.0;

    bool focus_override_ = false;
    SDL_Point focus_point_{0, 0};
    bool pan_override_ = false;
    SDL_Point start_center_{0, 0};
    SDL_Point target_center_{0, 0};
    bool manual_zoom_override_ = false;

    Room* starting_room_ = nullptr;
    double starting_area_ = 0.0;

    double runtime_camera_height_ = 0.0;
    double runtime_focus_depth_ = 0.0;
    double runtime_anchor_world_y_ = 0.0;
    double runtime_focus_ndc_offset_ = 0.0;
    double runtime_pitch_rad_ = 0.0;
    float runtime_pitch_deg_ = 0.0f;
    float runtime_depth_offset_px_ = 0.0f;
    FloorDepthParams runtime_floor_params_{};
    bool geometry_valid_ = false;

    double perspective_distance_at_scale_zero_    = 1.0;
    double perspective_distance_at_scale_hundred_ = 0.5;

    float player_center_offset_y_ = 0.0f;

    std::vector<world::GridPoint*> warped_points_;
    std::vector<Asset*> visible_assets_;
    std::vector<world::GridPoint*> visible_points_;
    std::vector<world::Chunk*> active_chunks_;
    std::unordered_map<std::uint64_t, std::size_t> id_to_index_;
    SDL_Rect cached_world_rect_{0, 0, 0, 0};
    GridBounds bounds_{};
};
