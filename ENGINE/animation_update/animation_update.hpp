#pragma once

#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include "stride_types.hpp"
#include "path_sanitizer.hpp"
#include "get_best_path.hpp"

namespace vibble::grid {
class Grid;
}

class Asset;
class Assets;
class AnimationRuntime; // non-public executor

// Public-facing planner/controller API. Does not mutate the Asset directly.
class AnimationUpdate {
public:
    AnimationUpdate(Asset* self, Assets* assets);
    AnimationUpdate(Asset* self, Assets* assets, double path_bias);

    // Wire to the executor after both are constructed
    void set_runtime(AnimationRuntime* runtime) { runtime_ = runtime; }

    // Plan a path using relative checkpoints from the controller
    void auto_move(const std::vector<SDL_Point>& rel_checkpoints,
                   int visited_thresh_px,
                   std::optional<int> checkpoint_resolution = std::nullopt,
                   bool override_non_locked = true);

    // Request an immediate move + animation selection (applied by executor in update)
    void move(SDL_Point delta,
              const std::string& animation,
              bool               resort_z            = true,
              bool               override_non_locked = true);

    // Clear any existing path plan
    void clear_movement_plan();

    // Query active movement path index for a given animation (delegates to executor)
    std::size_t path_index_for(const std::string& anim_id) const;

    // Exposed state for controllers to inspect
    bool      path_requested = false;
    SDL_Point final_dest{0, 0};

    // Executor interface (internal)
    bool has_pending_move() const { return move_pending_; }
    struct MoveRequest {
        SDL_Point    delta{0, 0};
        std::string  animation_id;
        bool         resort_z = true;
        bool         override_non_locked = true;
    };
    MoveRequest consume_move_request();
    bool consume_input_event();

private:
    vibble::grid::Grid& grid() const;
    int                 effective_grid_resolution(std::optional<int> override_resolution) const;

private:
    friend class AnimationRuntime;

    Asset*  self_          = nullptr;
    Assets* assets_owner_  = nullptr;
    vibble::grid::Grid* grid_service_ = nullptr;
    AnimationRuntime* runtime_ = nullptr;

    // Planning state
    Plan plan_{};
    int  visited_thresh_ = 0;

    // Planning helpers
    PathSanitizer sanitizer_{};
    GetBestPath   planner_{};

    // Controller interaction state
    bool        input_event_ = false;
    bool        move_pending_ = false;
    MoveRequest pending_move_{};
};
