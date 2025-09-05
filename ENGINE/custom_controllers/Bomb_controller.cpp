#include "Bomb_controller.hpp"

#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"

#include <cmath>
#include <algorithm>
#include <climits>

/*
  bomb ai
  random wander
  follow player inside 500
  explode at 20
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
  if (!self_ || !self_->info) return;

  if (!player_) {
    if (frames_until_think_ > 0) { frames_until_think_ -= 1; return; }
    think_random();
    frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
    return;
  }

  int d2 = dist2_to_player();
  if (d2 <= explode_radius_ * explode_radius_) {
    if (self_->get_current_animation() != "Explode")
      self_->change_animation("Explode");
    return;
  }

  if (d2 <= follow_radius_ * follow_radius_) {
    pursue();
    return;
  }

  if (frames_until_think_ > 0) {
    frames_until_think_ -= 1;
    return;
  }

  think_random();
  frames_until_think_ = rand_range(think_interval_min_, think_interval_max_);
}

void BombController::think_random() {
  if (!self_) return;

  if (coin(50)) {
    if (self_->get_current_animation() != "default")
      self_->change_animation("default");
    return;
  }

  static const char* names[4] = { "left", "right", "forward", "backward" };
  int dx[4] = { -probe_,  probe_, 0,  0 };
  int dy[4] = {      0,       0,  probe_, -probe_ };

  for (int i=0;i<8;i++) {
    int a = rand_range(0,3), b = rand_range(0,3);
    if (a!=b) {
      std::swap(dx[a], dx[b]);
      std::swap(dy[a], dy[b]);
      std::swap(const_cast<const char*&>(names[a]), const_cast<const char*&>(names[b]));
    }
  }

  if (!try_hop_dirs(names, dx, dy, 4)) {
    if (self_->get_current_animation() != "default")
      self_->change_animation("default");
  }
}

void BombController::pursue() {
  if (!self_ || !player_) return;

  int vx = player_->pos_X - self_->pos_X;
  int vy = player_->pos_Y - self_->pos_Y;

  const char* order[4];
  int dx[4], dy[4];

  if (std::abs(vx) >= std::abs(vy)) {
    order[0] = vx < 0 ? "left" : "right";
    order[1] = vy < 0 ? "backward" : "forward";
    order[2] = vy < 0 ? "backward" : "forward";
    order[3] = vx < 0 ? "left" : "right";
  } else {
    order[0] = vy < 0 ? "backward" : "forward";
    order[1] = vx < 0 ? "left" : "right";
    order[2] = vx < 0 ? "left" : "right";
    order[3] = vy < 0 ? "backward" : "forward";
  }

  for (int i=0;i<4;i++) {
    if      (order[i] == std::string("left"))     { dx[i] = -probe_; dy[i] = 0; }
    else if (order[i] == std::string("right"))    { dx[i] =  probe_; dy[i] = 0; }
    else if (order[i] == std::string("forward"))  { dx[i] = 0;       dy[i] =  probe_; }
    else                                          { dx[i] = 0;       dy[i] = -probe_; }
  }

  if (!try_hop_dirs(order, dx, dy, 4)) {
    if (self_->get_current_animation() != "default")
      self_->change_animation("default");
  } else {
    explode_if_close();
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

void BombController::explode_if_close() {
  if (!self_ || !player_) return;
  int d2 = dist2_to_player();
  if (d2 <= explode_radius_ * explode_radius_) {
    if (self_->get_current_animation() != "Explode")
      self_->change_animation("Explode");
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

int BombController::dist2_to_player() const {
  if (!self_ || !player_) return INT_MAX;
  long dx = long(player_->pos_X) - long(self_->pos_X);
  long dy = long(player_->pos_Y) - long(self_->pos_Y);
  return int(dx*dx + dy*dy);
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
