#include "determine_movement.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "core/active_assets_manager.hpp"

#include <cmath>
#include <limits>

static inline long long dist2(int x1, int y1, int x2, int y2) {
  long long dx = static_cast<long long>(x1) - static_cast<long long>(x2);
  long long dy = static_cast<long long>(y1) - static_cast<long long>(y2);
  return dx*dx + dy*dy;
}

bool DetermineMovement::can_move_by(Asset* self, ActiveAssetsManager& aam, int dx, int dy) {
  if (!self || !self->info) return false;
  int test_x = self->pos_X + dx;
  int test_y = self->pos_Y + dy - self->info->z_threshold;

  for (Asset* a : aam.getImpassableClosest()) {
    if (!a || a == self) continue;
    Area obstacle = a->get_area("passability");
    if (obstacle.contains_point({ test_x, test_y })) {
      return false;
    }
  }
  return true;
}

std::string DetermineMovement::pick_best_animation(
    Asset* self,
    ActiveAssetsManager& aam,
    int target_x,
    int target_y,
    const std::vector<std::string>& candidates)
{
  if (!self || !self->info) return {};

  const auto& all = self->info->animations;
  if (all.empty()) return {};

  long long best_d2 = std::numeric_limits<long long>::max();
  std::string best_id;

  for (const std::string& id : candidates) {
    auto it = all.find(id);
    if (it == all.end()) continue;
    const Animation& anim = it->second;
    // Only consider animations that imply actual movement
    const int dx = anim.total_dx;
    const int dy = anim.total_dy;
    if (dx == 0 && dy == 0) continue;
    if (!can_move_by(self, aam, dx, dy)) continue;

    const int nx = self->pos_X + dx;
    const int ny = self->pos_Y + dy;
    long long d2 = dist2(nx, ny, target_x, target_y);
    if (d2 < best_d2) {
      best_d2 = d2;
      best_id = id;
    }
  }

  return best_id;
}

bool DetermineMovement::apply_best_animation(
    Asset* self,
    ActiveAssetsManager& aam,
    int target_x,
    int target_y,
    const std::vector<std::string>& candidates)
{
  std::string id = pick_best_animation(self, aam, target_x, target_y, candidates);
  if (id.empty()) return false;
  if (self->get_current_animation() == id) return false;
  self->change_animation(id);
  // Ensure the animation change applies immediately so controllers
  // don't need to call update_animation_manager() right after.
  if (self) self->update_animation_manager();
  return true;
}

