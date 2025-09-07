#include "Frog_controller.hpp"

#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/determine_movement.hpp"
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
   aam_(aam)
{
  rng_seed_ ^= reinterpret_cast<uintptr_t>(self_) + 0x9e3779b9u;
  frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
  std::cout << "[frog] init at (" << (self_ ? self_->pos_X : 0) << "," << (self_ ? self_->pos_Y : 0)
            << ") next think in " << frames_until_think_ << " frames\n";
}

FrogController::~FrogController() {}

void FrogController::update(const Input& /*in*/) {
  updated_by_determine_ = false;
  // Run brain tick if valid; always tick animation at end.
  if (self_ && self_->info) {
    // No controller-side animation speed manipulation; timing handled by AnimationManager
    if (frames_until_think_ > 0) {
      frames_until_think_ -= 1;
    } else {
      std::cout << "[frog] think start cur='" << self_->get_current_animation()
                << "' at (" << self_->pos_X << "," << self_->pos_Y << ")\n";

      think();

      frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
      std::cout << "[frog] next think in " << frames_until_think_ << " frames\n";
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
        std::cout << "[frog] rest -> 'default'\n";
        self_->change_animation("default");
      } else {
        std::cout << "[frog][warn] missing anim 'default'\n";
      }
    } else {
      std::cout << "[frog] rest keep 'default'\n";
    }
    return;
  }

  if (!try_hop_any_dir()) {
    if (cur != "default") {
      if (has_anim("default")) {
        std::cout << "[frog] hop blocked -> fallback 'default'\n";
        self_->change_animation("default");
      } else {
        std::cout << "[frog][warn] missing anim 'default' on fallback\n";
      }
    } else {
      std::cout << "[frog] hop blocked, remain 'default'\n";
    }
  }
}

bool FrogController::try_hop_any_dir() {
  if (!self_) return false;

  // Randomly pick a target offset around the frog, then choose best animation
  // towards this target using per-animation totals.
  static const char* names[4] = { "left", "right", "forward", "backward" };

  // Shuffle choice to vary targets over time
  int idx = rand_range(0,3);
  int dx = 0, dy = 0;
  switch (idx) {
    case 0: dx = -probe_; dy = 0; break;      // left
    case 1: dx =  probe_; dy = 0; break;      // right
    case 2: dx = 0;        dy =  probe_; break; // forward
    default: dx = 0;       dy = -probe_; break; // backward
  }

  int target_x = self_->pos_X + dx;
  int target_y = self_->pos_Y + dy;

  std::vector<std::string> candidates = { names[0], names[1], names[2], names[3] };
  if ((updated_by_determine_ = DetermineMovement::apply_best_animation(self_, aam_, target_x, target_y, candidates))) {
    std::cout << "[frog] hop -> '" << self_->get_current_animation() << "'\n";
    return true;
  }
  std::cout << "[frog] hop blocked (no valid direction)\n";
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
