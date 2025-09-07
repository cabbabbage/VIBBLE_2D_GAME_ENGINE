#include "Bomb_controller.hpp"

#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/determine_movement.hpp"
#include "utils/area.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"

#include <cmath>
#include <algorithm>

/*
  bomb ai
  random wander
  follow player inside 500
  explosion at 20
*/

BombController::BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
 : BombController(assets, self, aam, nullptr)
{
}

BombController::BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam, Asset* player)
 : assets_(assets)
 , self_(self)
 , aam_(aam)
 , player_(player)
{
  rng_seed_ ^= reinterpret_cast<uintptr_t>(self_) + 0x9e3779b9u;
  frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
}

BombController::~BombController() {}

void BombController::update(const Input& /*in*/) {
  updated_by_determine_ = false;
  if (self_ && self_->info) {
    if (!player_) {
      if (frames_until_think_ > 0) {
        frames_until_think_ -= 1;
      } else {
        think_random();
        frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
      }
    } else {
      float d = self_->distance_to_player;
      if (d <= static_cast<float>(explosion_radius_)) {
        if (self_->get_current_animation() != "explosion")
          self_->change_animation("explosion");
      } else if (d <= static_cast<float>(follow_radius_)) {
        pursue();
      } else {
        if (frames_until_think_ > 0) {
          frames_until_think_ -= 1;
        } else {
          think_random();
          frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
        }
      }
    }
  }
  if (self_ && !updated_by_determine_) self_->update_animation_manager();
}

void BombController::think_random() {
  if (!self_) return;

  if (coin(50)) {
    if (self_->get_current_animation() != "default")
      self_->change_animation("default");
    return;
  }

  static const char* names[4] = { "left", "right", "forward", "backward" };

  // Pick a random nearby target and choose an animation using DetermineMovement
  int choice = rand_range(0,3);
  int dx = 0, dy = 0;
  switch (choice) {
    case 0: dx = -probe_; dy = 0; break;
    case 1: dx =  probe_; dy = 0; break;
    case 2: dx = 0;       dy =  probe_; break;
    default: dx = 0;      dy = -probe_; break;
  }
  int target_x = self_->pos_X + dx;
  int target_y = self_->pos_Y + dy;

  std::vector<std::string> candidates = { names[0], names[1], names[2], names[3] };
  if (!(updated_by_determine_ = DetermineMovement::apply_best_animation(self_, aam_, target_x, target_y, candidates))) {
    if (self_->get_current_animation() != "default")
      self_->change_animation("default");
  }
}

void BombController::pursue() {
  if (!self_ || !player_) return;

  // Use per-animation totals to choose best hop towards the player
  std::vector<std::string> candidates = {"left", "right", "forward", "backward"};
  if (!(updated_by_determine_ = DetermineMovement::apply_best_animation(self_, aam_, player_->pos_X, player_->pos_Y, candidates))) {
    if (self_->get_current_animation() != "default") self_->change_animation("default");
  } else {
    explosion_if_close();
  }
}

bool BombController::try_hop_dirs(const char* const* names, const int* dx, const int* dy, int n) {
  for (int i=0;i<n;i++) {
    if (canMove(dx[i], dy[i])) {
      if (self_->get_current_animation() != names[i])
        self_->change_animation(names[i]);
      return true;
    }
  }
  return false;
}

void BombController::explosion_if_close() {
  if (!self_ || !player_) return;
  float d = self_->distance_to_player;
  if (d <= static_cast<float>(explosion_radius_)) {
    if (self_->get_current_animation() != "explosion")
      self_->change_animation("explosion");
  }
}

bool BombController::canMove(int offset_x, int offset_y) const {
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

bool BombController::aabb(const Area& A, const Area& B) const {
  auto [a_minx, a_miny, a_maxx, a_maxy] = A.get_bounds();
  auto [b_minx, b_miny, b_maxx, b_maxy] = B.get_bounds();
  return !(a_maxx < b_minx || b_maxx < a_minx ||
           a_maxy < b_miny || b_maxy < a_miny);
}

bool BombController::pointInAABB(int x, int y, const Area& B) const {
  auto [b_minx, b_miny, b_maxx, b_maxy] = B.get_bounds();
  return (x >= b_minx && x <= b_maxx &&
          y >= b_miny && y <= b_maxy);
}

int BombController::randu() {
  rng_seed_ = 1664525u * rng_seed_ + 1013904223u;
  return int(rng_seed_ >> 1);
}

int BombController::rand_range(int lo, int hi) {
  if (hi < lo) std::swap(lo, hi);
  int span = hi - lo + 1;
  int r = randu();
  return lo + (span > 0 ? (r % span) : 0);
}

bool BombController::coin(int percent_true) {
  if (percent_true <= 0) return false;
  if (percent_true >= 100) return true;
  return rand_range(0,99) < percent_true;
}
