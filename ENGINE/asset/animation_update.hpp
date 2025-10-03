#pragma once

#include <string>
#include <random>
#include <vector>
#include <optional>
#include <SDL.h>
#include <functional>

class Asset;
class Assets;
class AnimationFrame;

class AnimationUpdate {
public:
    AnimationUpdate(Asset* self, Assets* assets);
    AnimationUpdate(Asset* self, Assets* assets, double path_bias);

    void update();
    void set_animation_now(const std::string& anim_id);
    void set_animation_qued(const std::string& anim_id);
    void move(int x, int y);
    void set_idle(int rest_ratio);
    void set_idle(int rest_loop_min, int rest_loop_max);
    void set_pursue(Asset* final_target);
    void set_run(Asset* threat);
    void set_orbit(Asset* center, int min_radius, int max_radius, int keep_direction_ratio);
    void set_patrol(const std::vector<SDL_Point>& waypoints, bool loop, int hold_frames);
    void set_serpentine(Asset* final_target, int sway, int keep_side_ratio);
    void set_to_point(SDL_Point final_point, std::function<void(AnimationUpdate&)> on_reached);
    inline void set_to_point(int x, int y, std::function<void(AnimationUpdate&)> on_reached) { set_to_point(SDL_Point{ x, y }, std::move(on_reached)); }
    void set_mode_none();

    void set_path_bias(double bias);
    void set_target(SDL_Point desired, const Asset* final_target);
    inline void set_target(int desired_x, int desired_y, const Asset* final_target) { set_target(SDL_Point{ desired_x, desired_y }, final_target); }

private:
    enum class Mode { None, Idle, Pursue, Run, Orbit, Patrol, Serpentine, ToPoint };

    bool can_move_by(int dx, int dy) const;
    bool would_overlap_same_or_player(int dx, int dy) const;
    std::string pick_best_animation_towards(SDL_Point target);
    void ensure_idle_target();
    void ensure_pursue_target( const Asset* final_target);
    void ensure_run_target( const Asset* threat);
    void ensure_orbit_target(int min_radius, int max_radius, const Asset* center, int keep_direction_ratio);
    void ensure_patrol_target(const std::vector<SDL_Point>& waypoints, bool loop, int hold_frames);
    void ensure_serpentine_target(int sway, const Asset* final_target, int keep_side_ratio);
    void ensure_to_point_target();
    SDL_Point bottom_middle(SDL_Point pos) const;
    bool point_in_impassable(SDL_Point pt, const Asset* ignored) const;
    bool path_blocked(SDL_Point from, SDL_Point to, const Asset* ignored) const;
    void transition_mode(Mode m);
    bool is_target_reached();
    int  min_move_len2() const;
    void switch_to(const std::string& anim_id);
    bool advance(AnimationFrame*& frame);
    void get_animation();
    void get_new_target();
    int max_current_target_dist = 100;
    int min_current_target_dist = 50;
    double min_factor = 0.5;

private:
    Asset* self_ = nullptr;
    Assets* assets_owner_ = nullptr;
    Mode mode_ = Mode::None;
    bool have_target_ = false;
    SDL_Point target_{0, 0};
    mutable int cached_min_move_len2_ = -1;
    std::mt19937 rng_;
    double path_bias_ = 0.75;
    int    orbit_dir_ = +1;
    double orbit_angle_ = 0.0;
    int    orbit_radius_ = 0;
    bool   orbit_params_set_ = false;
    bool   orbit_force_dir_ = false;
    int    orbit_forced_dir_ = +1;
    std::vector<SDL_Point> patrol_points_;
    std::size_t patrol_index_ = 0;
    bool patrol_loop_ = true;
    int  patrol_hold_frames_ = 0;
    int  patrol_hold_left_ = 0;
    bool patrol_initialized_ = false;
    int  serp_side_ = +1;
    int  serp_stride_ = 0;
    bool serp_params_set_ = false;
    int idle_rest_loop_min_ = 0;
    int idle_rest_loop_max_ = 0;
    int idle_rest_loops_left_ = 0;
    const Asset* pursue_target_ = nullptr;
    const Asset* run_threat_ = nullptr;
    const Asset* orbit_center_ = nullptr;
    int orbit_min_radius_ = 0;
    int orbit_max_radius_ = 0;
    int orbit_keep_ratio_ = 0;
    const Asset* serp_target_ = nullptr;
    int serp_sway_ = 0;
    int serp_keep_ratio_ = 0;
    int dx_ = 0;
    int dy_ = 0;
    bool override_movement = false;
    bool suppress_movement_ = false;
    bool blocked_last_step_ = false;
    bool moving = true;
    SDL_Point to_point_goal_{0, 0};
    std::function<void(AnimationUpdate&)> to_point_on_reach_;
    std::optional<std::string> queued_anim_;
    bool forced_active_ = false;
    Mode saved_mode_ = Mode::None;
    int slow_frame_interval_ = 1;
    int slow_frame_counter_ = 0;
    bool mode_suspended_ = false;
};
