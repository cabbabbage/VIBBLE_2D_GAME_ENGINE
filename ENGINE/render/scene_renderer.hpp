#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "light_map.hpp"
#include "global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "render/camera.hpp"

class Assets;
class Asset;

class SceneRenderer {

public:
    SceneRenderer(SDL_Renderer* renderer, Assets* assets, int screen_width, int screen_height, const std::string& map_path);
    ~SceneRenderer();
    void render();
    void apply_map_light_config(const nlohmann::json& data);
    SDL_Renderer* get_renderer() const;
    void set_low_quality_rendering(bool enabled);
    bool low_quality_rendering() const { return low_quality_rendering_; }
    void toggle_light_map_only_mode() { light_map_only_mode_ = !light_map_only_mode_; }
    bool light_map_only_mode() const { return light_map_only_mode_; }
    Global_Light_Source& map_light_source() { return main_light_source_; }
    const Global_Light_Source& map_light_source() const { return main_light_source_; }
    SDL_Color screen_light_color() const { return screen_light_color_; }
    int screen_light_min_opacity() const { return screen_light_min_opacity_; }
    int screen_light_max_opacity() const { return screen_light_max_opacity_; }
    render_pipeline::shading::ReactiveShadowSettings& reactive_shadow_settings() { return reactive_shadow_settings_; }
    const render_pipeline::shading::ReactiveShadowSettings& reactive_shadow_settings() const { return reactive_shadow_settings_; }

private:
    void update_shading_groups();
    bool shouldRegen(Asset* a);
    SDL_Rect get_scaled_position_rect(Asset* a, int fw, int fh, float inv_scale, int min_w, int min_h, float reference_screen_height);
    void apply_screen_light_settings(const nlohmann::json& data);
    void update_fullscreen_light_texture();

    // ---------- Lens flare (helpers + state; no new classes) ----------
    struct FlareSeed {
        float x = 0.f, y = 0.f;     // raw screen
        float sx = 0.f, sy = 0.f;   // smoothed screen
        float strength = 0.f;       // [0..1]
        bool  valid = false;
    };

    struct FlareGhost {
        // A rendered element (circle/streak/star) that slides along camera axis.
        float x = 0.f, y = 0.f;     // current
        float tx = 0.f, ty = 0.f;   // target along axis
        float vx = 0.f, vy = 0.f;   // drift velocity
        float alpha = 0.f;          // current alpha [0..1]
        float target_alpha = 0.f;   // goal alpha [0..1]
        float size_px = 160.f;      // base pixel size
        int   kind = 0;             // 0=circle, 1=streak, 2=starburst
        float hue = 30.f;           // degrees (for subtle warm tint)
        float life = 0.f;           // frames since spawn
        float max_life = 300.f;     // soft lifetime
        bool  dying = false;
    };

    void draw_lens_flares_after_light_map();

    // seed detection from current backbuffer (light map already drawn there)
    bool detect_bright_seeds(std::vector<FlareSeed>& out, int stride_px, float threshold_norm);
    void smooth_and_track_seeds(std::vector<FlareSeed>& seeds);
    void spawn_or_update_ghosts(const std::vector<FlareSeed>& seeds);
    void step_and_render_ghosts();

    // elements
    void ensure_flare_textures();
    void destroy_flare_textures();
    void make_circle_tex();
    void make_streak_tex();
    void make_starburst_tex();

    // sprites
    void render_sprite(SDL_Texture* tex, float cx, float cy, float intensity, float base_px, float angle_deg = 0.f, SDL_Color tint = {255,255,255,255});
    SDL_Color warm_tint(float hue_deg, float intensity_scale) const;

    // geometry helpers
    void axis_cascade_points(const FlareSeed& seed, std::vector<SDL_FPoint>& out) const;
    bool on_screen(float x, float y, int margin_px = 0) const;
    SDL_FPoint screen_center() const { return SDL_FPoint{ (float)screen_width_ * 0.5f, (float)screen_height_ * 0.5f }; }

    // cached textures
    SDL_Texture* circle_tex_   = nullptr;   // soft bokeh
    SDL_Texture* streak_tex_   = nullptr;   // anamorphic streak
    SDL_Texture* star_tex_     = nullptr;   // compact starburst

    // persistent state
    std::vector<FlareSeed>  last_seeds_;
    std::vector<FlareGhost> ghosts_;

    // tunables (low, cinematic)
    int   seed_stride_px_         = 18;     // coarse scan stride
    float seed_threshold_norm_    = 0.78f;  // luma threshold for seeds
    float seed_pos_ema_           = 0.18f;  // seed smoothing
    float ghost_follow_ema_       = 0.12f;  // ghost follows target
    float ghost_spawn_speed_      = 20.f;   // px/frame from off-screen
    float ghost_alpha_rise_       = 0.05f;  // fade-in
    float ghost_alpha_fall_       = 0.04f;  // fade-out
    float ghost_drift_            = 0.08f;  // subtle drift
    float ghost_size_min_         = 90.f;
    float ghost_size_max_         = 360.f;
    float ghost_intensity_gain_   = 0.65f;  // overall multiplier
    float ghost_alpha_cap_        = 0.28f;  // absolute alpha cap (keeps subtle)
    float streak_angle_lean_      = 10.f;   // slight tilt
    float offscreen_spawn_bias_   = 64.f;   // px beyond edges
    float axis_factors_[7]        = { -0.55f, -0.25f, 0.22f, 0.55f, 0.95f, 1.45f, 2.0f }; // cascade along axis
    int   max_new_per_frame_      = 8;      // throughput limiter

private:
    std::string    map_path_;
    SDL_Renderer*  renderer_;
    Assets*        assets_;
    int            screen_width_;
    int            screen_height_;
    Global_Light_Source main_light_source_;
    SDL_Texture*   fullscreen_light_tex_;
    SDL_Color      screen_light_color_{255, 255, 255, 255};
    int            screen_light_min_opacity_ = 0;
    int            screen_light_max_opacity_ = 255;
    render_pipeline::shading::ReactiveShadowSettings reactive_shadow_settings_{};
    AssetRenderPipeline render_pipeline_;
    std::unique_ptr<LightMap> z_light_pass_;
    int            current_shading_group_ = 0;
    int            num_groups_ = 40;
    bool           debugging = false;
    bool           low_quality_rendering_ = false;
    bool           light_map_only_mode_ = false;

    std::unordered_set<Asset*> last_active_assets_;
};
