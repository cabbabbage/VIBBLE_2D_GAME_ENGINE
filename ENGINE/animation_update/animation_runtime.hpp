#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "asset/Asset.hpp"
#include "stride_types.hpp"
#include "path_sanitizer.hpp"
#include "get_best_path.hpp"
#include "movement_plan_executor.hpp"

namespace vibble::grid {
class Grid;
}

class Asset;
class Assets;
class AnimationFrame;
class Animation;
class AnimationUpdate;

class PathSanitizer;
class GetBestPath;
class MovementPlanExecutor;

class AnimationRuntime {
public:
    AnimationRuntime(Asset* self, Assets* assets);

    void update();

    void set_planner(AnimationUpdate* planner) { planner_iface_ = planner; }

    std::size_t path_index_for(const std::string& anim_id) const;

    vibble::grid::Grid& grid() const;
    bool path_blocked(SDL_Point from, SDL_Point to, const Asset* ignored, std::vector<const Asset*>* blockers = nullptr) const;
    bool handle_blocked_path(SDL_Point from, SDL_Point to, const std::vector<const Asset*>& blockers);
    void refresh_z_index();
    void mark_progress_toward_checkpoints();
    bool advance(AnimationFrame*& frame);
    void switch_to(const std::string& anim_id, std::size_t path_index = 0);
    bool should_defer_for_non_locked(bool override_non_locked) const;

    void reset_plan_progress();
    void set_debug_enabled(bool enabled);

    bool run_child_animation(const std::string& name);

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
    void       apply_child_frame_data(Animation& anim, const AnimationFrame* frame, float dt);
    void       sync_child_assets();
    void       advance_child_timelines(float dt);
    Asset*     spawn_child_asset(Asset::AnimationChildAttachment& slot);
    void       destroy_child_assets();
    void       handle_async_requests(const std::vector<std::string>& requests);
    Asset::AnimationChildAttachment* find_child_slot(const std::string& name);
    void       restart_child_timeline(Asset::AnimationChildAttachment& slot);

    void       apply_pending_move();

private:
    friend class MovementPlanExecutor;

    Asset*  self_         = nullptr;
    Assets* assets_owner_ = nullptr;
    vibble::grid::Grid* grid_service_ = nullptr;
    AnimationUpdate* planner_iface_ = nullptr;

    std::size_t stride_index_         = 0;
    int         stride_frame_counter_ = 0;
    std::size_t next_checkpoint_index_ = 0;

    PathSanitizer  sanitizer_{};
    GetBestPath    planner_{};
    MovementPlanExecutor   executor_{};

    std::unordered_map<std::string, std::size_t> active_paths_{};

    bool debug_enabled_ = false;
    bool just_applied_controller_move_ = false;
    int  suppress_root_motion_frames_ = 0;
    std::vector<AnimationChildFrameData> child_frame_buffer_{};

    bool suppress_root_motion_active() const { return suppress_root_motion_frames_ > 0; }
};
