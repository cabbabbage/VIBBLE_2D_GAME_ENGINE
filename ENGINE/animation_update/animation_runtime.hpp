#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include <SDL.h>

#include "asset/Asset.hpp"
#include "stride_types.hpp"
#include "path_sanitizer.hpp"
#include "get_best_path.hpp"
#include "stride_player.hpp"

namespace vibble::grid {
class Grid;
}

class Asset;
class Assets;
class AnimationFrame;
class Animation;
class AnimationUpdate; // planner (public-facing)

class PathSanitizer;
class GetBestPath;
class StridePlayer;

// Non-public executor: advances frames, applies movement, handles path following.
class AnimationRuntime {
public:
    AnimationRuntime(Asset* self, Assets* assets);

    // Called by Asset each tick.
    void update();

    // Wiring after construction
    void set_planner(AnimationUpdate* planner) { planner_iface_ = planner; }

    // Query current active path index for an animation
    std::size_t path_index_for(const std::string& anim_id) const;

    // Internal helpers used by StridePlayer
    vibble::grid::Grid& grid() const;
    bool path_blocked(SDL_Point from, SDL_Point to, const Asset* ignored, std::vector<const Asset*>* blockers = nullptr) const;
    bool handle_blocked_path(SDL_Point from, SDL_Point to, const std::vector<const Asset*>& blockers);
    void refresh_z_index();
    void mark_progress_toward_checkpoints();
    bool advance(AnimationFrame*& frame);
    void switch_to(const std::string& anim_id, std::size_t path_index = 0);
    bool should_defer_for_non_locked(bool override_non_locked) const;

    // Planner notifications
    void reset_plan_progress();

private:
    int        effective_grid_resolution(std::optional<int> override_resolution) const;
    SDL_Point  convert_delta_to_world(SDL_Point delta, int resolution) const;
    SDL_Point  bottom_middle(SDL_Point pos) const;
    bool       point_in_impassable(SDL_Point pt, const Asset* ignored) const;
    bool       attempt_unstick(SDL_Point from, SDL_Point to, const std::vector<const Asset*>& blockers);
    bool       adjust_next_checkpoint(const std::vector<const Asset*>& blockers);
    bool       replan_to_destination();
    void       update_child_attachments(Animation& anim, float dt);
    void       ensure_child_slots(Animation& anim);
    void       advance_child_frames(float dt);
    void       apply_child_frame_data(const AnimationFrame* frame);
    void       sync_child_assets();
    Asset*     spawn_child_asset(Asset::AnimationChildAttachment& slot);
    void       destroy_child_assets();

    // Apply a pending one-shot move from the planner
    void       apply_pending_move();

private:
    friend class StridePlayer;

    Asset*  self_         = nullptr;
    Assets* assets_owner_ = nullptr;
    vibble::grid::Grid* grid_service_ = nullptr;
    AnimationUpdate* planner_iface_ = nullptr; // public planner we read plan/intents from

    // Execution state for current plan
    std::size_t stride_index_         = 0;
    int         stride_frame_counter_ = 0;
    std::size_t next_checkpoint_index_ = 0;

    // Executors that can also re-plan when blocked
    PathSanitizer  sanitizer_{};
    GetBestPath    planner_{};
    StridePlayer   player_{};

    // Active movement path per animation id
    std::unordered_map<std::string, std::size_t> active_paths_{};

    // Controller input tracking for on-end redirection
    bool just_applied_controller_move_ = false;
};
