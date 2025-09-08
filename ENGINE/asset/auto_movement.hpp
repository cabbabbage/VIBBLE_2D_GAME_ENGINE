#pragma once

#include <string>
#include <random>
#include <vector>
#include "utils/area.hpp" // for Area::Point

class Asset;
class ActiveAssetsManager;

/*
  AutoMovement
  ------------
  Instance helper for controllers. Owns the notion of a "current method"
  (idle/pursue/etc.) and a persistent target while that method is active.

  Construct with the controlled asset, the ActiveAssetsManager, and whether
  movement must remain confined to the current room area.

  Configure a movement style via set_* methods (idle, pursue, run, orbit,
  patrol, serpentine), then call move() each frame. The helper keeps per-mode
  sticky target points until reached (within one total-animation movement).
*/
class AutoMovement {
public:
  AutoMovement(Asset* self, ActiveAssetsManager& aam, bool confined);
  AutoMovement(Asset* self, ActiveAssetsManager& aam, bool confined,
               double directness_weight, double sparsity_weight);

  // Configure the current movement method, then call move() each frame.
  void set_idle(int min_target_distance, int max_target_distance, int rest_ratio);
  void set_pursue(Asset* final_target, int min_target_distance, int max_target_distance);
  void set_run(Asset* threat, int min_target_distance, int max_target_distance);
  void set_orbit(Asset* center, int min_radius, int max_radius, int keep_direction_ratio);
  void set_patrol(const std::vector<Area::Point>& waypoints, bool loop, int hold_frames);
  void set_serpentine(Asset* final_target, int min_stride, int max_stride, int sway, int keep_side_ratio);

  // Advance one tick based on the configured movement method.
  void move();

  // Update target selection weights
  void set_weights(double directness_weight, double sparsity_weight);

  // Pick and set a balanced target near the desired point, balancing directness
  // toward final_target (if non-null) and sparsity (average distance to nearby
  // assets ahead in the current direction). Excludes final_target from sparsity.
  void set_target(int desired_x, int desired_y, const Asset* final_target);
  // Alias for convenience/typo tolerance
  inline void set_traget(int desired_x, int desired_y, const Asset* final_target) { set_target(desired_x, desired_y, final_target); }

private:
  enum class Mode { None, Idle, Pursue, Run, Orbit, Patrol, Serpentine };

  // Core helpers
  bool can_move_by(int dx, int dy) const;
  bool would_overlap_same_or_player(int dx, int dy) const;
  std::string pick_best_animation_towards(int target_x, int target_y) const;
  std::string pick_least_movement_animation() const; // may return zero-move anim

  void ensure_idle_target(int min_dist, int max_dist);
  void ensure_pursue_target(int min_dist, int max_dist, const Asset* final_target);
  void ensure_run_target(int min_dist, int max_dist, const Asset* threat);
  void ensure_orbit_target(int min_radius, int max_radius, const Asset* center, int keep_direction_ratio);
  void ensure_patrol_target(const std::vector<Area::Point>& waypoints,
                            bool loop,
                            int hold_frames);
  void ensure_serpentine_target(int min_stride,
                                int max_stride,
                                int sway,
                                const Asset* final_target,
                                int keep_side_ratio);
  Area::Point choose_balanced_target(int desired_x, int desired_y, const Asset* final_target) const;

  void transition_mode(Mode m);
  bool is_target_reached() const; // using < 1 total animation movement threshold
  int  min_move_len2() const;     // cached min squared movement length across anims
  void clamp_to_room(int& x, int& y) const;

private:
  Asset* self_ = nullptr;
  ActiveAssetsManager& aam_;
  bool confined_ = true;

  Mode mode_ = Mode::None;
  bool have_target_ = false;
  Area::Point target_ {0, 0};

  mutable int cached_min_move_len2_ = -1; // -1 => not computed yet

  std::mt19937 rng_;
  // Balancing weights for target selection
  double weight_dir_ = 0.6;     // favor directness toward anchor
  double weight_sparse_ = 0.4;  // favor sparse neighbor regions
  // Orbit state
  int    orbit_dir_ = +1;     // +1: CCW, -1: CW
  double orbit_angle_ = 0.0;  // current angle used for next waypoint
  int    orbit_radius_ = 0;   // chosen radius within [min,max]
  bool   orbit_params_set_ = false;

  // Patrol state
  std::vector<Area::Point> patrol_points_;
  std::size_t patrol_index_ = 0;
  bool patrol_loop_ = true;
  int  patrol_hold_frames_ = 0;
  int  patrol_hold_left_ = 0;
  bool patrol_initialized_ = false;

  // Serpentine state
  int  serp_side_ = +1;        // +1: left, -1: right
  int  serp_stride_ = 0;       // last chosen stride
  bool serp_params_set_ = false;

  // Configured parameters per mode
  int idle_min_dist_ = 0;
  int idle_max_dist_ = 0;
  int idle_rest_ratio_ = 0;

  const Asset* pursue_target_ = nullptr;
  int pursue_min_dist_ = 0;
  int pursue_max_dist_ = 0;

  const Asset* run_threat_ = nullptr;
  int run_min_dist_ = 0;
  int run_max_dist_ = 0;

  const Asset* orbit_center_ = nullptr;
  int orbit_min_radius_ = 0;
  int orbit_max_radius_ = 0;
  int orbit_keep_ratio_ = 0;

  const Asset* serp_target_ = nullptr;
  int serp_min_stride_ = 0;
  int serp_max_stride_ = 0;
  int serp_sway_ = 0;
  int serp_keep_ratio_ = 0;
};
