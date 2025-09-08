#include "Frog_controller.hpp"

#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/auto_movement.hpp"
#include "asset/move.hpp"
#include "utils/area.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"

#include <cmath>
#include <random>
#include <iostream>

/*
  frog ai
  random idle/hop
  only hop into free space
  verbose debug
*/

FrogController::FrogController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
 : assets_(assets),
   self_(self),
   aam_(aam),
   mover_(self, aam, true)
{
  rng_seed_ ^= reinterpret_cast<uintptr_t>(self_) + 0x9e3779b9u;
  frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
  

}

FrogController::~FrogController() {}

void FrogController::update(const Input& /*in*/) {
  updated_by_determine_ = false;
  // Run brain tick if valid; always tick animation at end.
  if (self_ && self_->info) {
    // Recompute or maintain a persistent wander target every 100 frames
    constexpr double pi = 3.14159265358979323846;
    if (pursue_frames_left_ <= 0) {
      int angle_deg = rand_range(0, 359);
      double theta = (static_cast<double>(angle_deg) * pi) / 180.0;
      int radius = 30; // small local wander radius
      pursue_target_x_ = self_->pos_X + static_cast<int>(std::llround(radius * std::cos(theta)));
      pursue_target_y_ = self_->pos_Y + static_cast<int>(std::llround(radius * std::sin(theta)));
      pursue_frames_left_ = pursue_recalc_interval_;
    } else {
      pursue_frames_left_ -= 1;
    }
    // No controller-side animation speed manipulation; timing handled by AnimationManager
    if (frames_until_think_ > 0) {
      frames_until_think_ -= 1;
    } else {


      think();

      frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
      
    }
  }
  if (self_ && !updated_by_determine_) self_->update_animation_manager();
}

void FrogController::think() {
  if (!self_ || !self_->info) return;

  const std::string cur = self_->get_current_animation();

  if (coin(55)) {
    if (cur != "default") {
      if (has_anim("default")) {
        
        self_->change_animation("default");
      } else {
        
      }
    } else {
      
    }
    return;
  }

  if (!try_hop_any_dir()) {
    if (cur != "default") {
      if (has_anim("default")) {
        
        self_->change_animation("default");
      } else {
        
      }
    } else {
      
    }
  }
}

bool FrogController::try_hop_any_dir() {
  if (!self_) return false;

  // Idle-like micro hops for now; pursue will be implemented next.
  const std::string before = self_->get_current_animation();
  mover_.set_idle(/*min=*/0, /*max=*/30, /*rest_ratio=*/3);
  mover_.move();
  updated_by_determine_ = true;
  const std::string after = self_->get_current_animation();

  if (after != before) {
    
    return true;
  }
  
  return false;
}

bool FrogController::canMove(int offset_x, int offset_y) {
  if (!self_ || !self_->info) return false;

  int test_x = self_->pos_X + offset_x;
  int test_y = self_->pos_Y + offset_y - self_->info->z_threshold;

  for (Asset* a : aam_.getImpassableClosest()) {
    if (!a || a == self_) continue;
    Area obstacle = a->get_area("passability");
    if (obstacle.contains_point({ test_x, test_y })) {
      return false;
    }
  }
  return true;
}

bool FrogController::has_anim(const char* name) const {
  if (!self_ || !self_->info) return false;
  return self_->info->animations.find(name) != self_->info->animations.end();
}

bool FrogController::aabb(const Area& A, const Area& B) const {
  auto [a_minx, a_miny, a_maxx, a_maxy] = A.get_bounds();
  auto [b_minx, b_miny, b_maxx, b_maxy] = B.get_bounds();
  return !(a_maxx < b_minx || b_maxx < a_minx ||
           a_maxy < b_miny || b_maxy < a_miny);
}

bool FrogController::pointInAABB(int x, int y, const Area& B) const {
  auto [b_minx, b_miny, b_maxx, b_maxy] = B.get_bounds();
  return (x >= b_minx && x <= b_maxx &&
          y >= b_miny && y <= b_maxy);
}

/* rng */
int FrogController::randu() {
  rng_seed_ = 1664525u * rng_seed_ + 1013904223u;
  return int(rng_seed_ >> 1);
}

int FrogController::rand_range(int lo, int hi) {
  if (hi < lo) { int t = lo; lo = hi; hi = t; }
  int span = hi - lo + 1;
  int r = randu();
  return lo + (span > 0 ? (r % span) : 0);
}

bool FrogController::coin(int percent_true) {
  if (percent_true <= 0) return false;
  if (percent_true >= 100) return true;
  return rand_range(0,99) < percent_true;
}
