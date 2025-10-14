#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <SDL.h>
#include <nlohmann/json.hpp>

class LensFlareRenderer {
public:
    struct Ghost;

    struct Settings {
        static constexpr std::size_t kAxisCount = 7;

        bool enabled = true;
        int seed_stride_px = 28;
        float seed_threshold_norm = 0.72f;
        float seed_pos_ema = 0.25f;
        float ghost_follow_ema = 0.08f;
        float ghost_spawn_speed = 0.0f;
        float ghost_alpha_rise = 0.02f;
        float ghost_alpha_fall = 0.012f;
        float ghost_drift = 0.02f;
        float ghost_size_min = 140.0f;
        float ghost_size_max = 420.0f;
        float ghost_intensity_gain = 0.75f;
        float ghost_alpha_cap = 0.4f;
        float streak_angle_lean = 0.0f;
        float offscreen_spawn_bias = 0.0f;
        int max_new_per_frame = 12;
        std::array<float, kAxisCount> axis_factors = { -0.5f, -0.2f, 0.25f, 0.6f, 0.95f, 1.4f, 1.9f };

        bool operator==(const Settings& other) const;
        bool operator!=(const Settings& other) const { return !(*this == other); }
    };

    struct Seed {
        float x = 0.0f;
        float y = 0.0f;
        float sx = 0.0f;
        float sy = 0.0f;
        float strength = 0.0f;
        float smoothed_strength = 0.0f;
        bool valid = false;
    };

    struct Ghost {
        float x = 0.0f;
        float y = 0.0f;
        float tx = 0.0f;
        float ty = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float alpha = 0.0f;
        float target_alpha = 0.0f;
        float size_px = 160.0f;
        int kind = 0;
        float hue = 30.0f;
        float life = 0.0f;
        float max_life = 600.0f;
        bool dying = false;
        float axis_angle_deg = 0.0f;
        float target_axis_angle_deg = 0.0f;
        int last_seen_frame = 0;
    };

    struct SectorState {
        SDL_FPoint target_pos{ 0.0f, 0.0f };
        SDL_FPoint smoothed_pos{ 0.0f, 0.0f };
        float presence = 0.0f;
        float target_presence = 0.0f;
        int last_seen_frame = -1000;
        bool updated = false;
        bool initialized = false;
    };

    LensFlareRenderer(SDL_Renderer* renderer, int screen_width, int screen_height);
    ~LensFlareRenderer();

    void set_renderer(SDL_Renderer* renderer);
    void set_screen_size(int width, int height);

    void apply_settings(const Settings& settings);
    void apply_settings_from_json(const nlohmann::json& data);

    const Settings& current_settings() const { return settings_; }

    void draw_after_light_map();

    static Settings default_settings();
    static Settings sanitize_settings(const Settings& raw);
    static Settings settings_from_json(const nlohmann::json& data, const Settings& defaults = default_settings());
    static void settings_to_json(nlohmann::json& out, const Settings& settings);

private:
    void ensure_flare_textures();
    void destroy_flare_textures();
    void make_circle_tex();
    void make_streak_tex();
    void make_starburst_tex();

    void render_sprite(SDL_Texture* tex, float cx, float cy, float intensity, float base_px, float angle_deg = 0.f, SDL_Color tint = {255,255,255,255});
    SDL_Color warm_tint(float hue_deg, float intensity_scale) const;

    bool detect_bright_seeds(std::vector<Seed>& out, int stride_px, float threshold_norm);
    void smooth_and_track_seeds(std::vector<Seed>& seeds);
    void spawn_or_update_ghosts(const std::vector<Seed>& seeds);
    void step_and_render_ghosts();
    void axis_cascade_points(const Seed& seed, std::vector<SDL_FPoint>& out) const;
    bool on_screen(float x, float y, int margin_px = 0) const;
    SDL_FPoint screen_center() const;
    void update_sector_states(const std::vector<Seed>& seeds);
    void collect_sector_seeds(std::vector<Seed>& out) const;
    int sector_cell_span() const;

    SDL_Renderer* renderer_ = nullptr;
    int screen_width_ = 0;
    int screen_height_ = 0;

    Settings settings_ = sanitize_settings(default_settings());

    SDL_Texture* circle_tex_ = nullptr;
    SDL_Texture* streak_tex_ = nullptr;
    SDL_Texture* star_tex_ = nullptr;

    std::vector<Seed> last_seeds_;
    std::vector<Ghost> ghosts_;
    std::unordered_map<std::uint64_t, SectorState> sector_states_;
    int frame_counter_ = 0;
};

