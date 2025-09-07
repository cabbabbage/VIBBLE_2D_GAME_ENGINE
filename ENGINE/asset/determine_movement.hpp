#pragma once

#include <string>
#include <vector>

class Asset;
class ActiveAssetsManager;

/*
  DetermineMovement
  ------------------
  Helper used by controllers to choose the best animation (from a candidate list)
  that moves an asset closest to a target point, using per-animation totals
  (Animation::total_dx / total_dy). It rejects candidates that would move the
  asset through an impassable boundary.

  Usage:
    std::vector<std::string> cands = {"left","right","forward","backward"};
    std::string best = DetermineMovement::pick_best_animation(self, aam, tx, ty, cands);
    if (!best.empty()) self->change_animation(best);
*/
class DetermineMovement {
public:
  // Return the best animation id from candidates, or empty if none is valid.
  static std::string pick_best_animation(
      Asset* self,
      ActiveAssetsManager& aam,
      int target_x,
      int target_y,
      const std::vector<std::string>& candidates);

  // Pick and apply (via change_animation). Returns true if changed.
  static bool apply_best_animation(
      Asset* self,
      ActiveAssetsManager& aam,
      int target_x,
      int target_y,
      const std::vector<std::string>& candidates);

private:
  static bool can_move_by(Asset* self, ActiveAssetsManager& aam, int dx, int dy);
};

