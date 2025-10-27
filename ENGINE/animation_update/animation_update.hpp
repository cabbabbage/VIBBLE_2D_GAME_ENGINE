#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

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

class AnimationUpdate {
public:
    AnimationUpdate(Asset* self, Assets* assets);
    AnimationUpdate(Asset* self, Assets* assets, double path_bias);

    void update();
    void set_animation_now(const std::string& anim_id);
    void set_animation_qued(const std::string& anim_id);
    void auto_move(const std::vector<SDL_Point>& rel_checkpoints,
                   int visited_thresh_px,
                   std::optional<int> checkpoint_resolution = std::nullopt);
    void move(SDL_Point delta, bool resort_z = true);
    void just_move(SDL_Point delta, const std::string& animation_id, bool resort_z = true);
    void clear_movement_plan();
    void set_manual_animation(const std::string& anim_id, bool loop = true);
    void clear_manual_animation();
    void refresh_z_index();
    std::size_t path_index_for(const std::string& anim_id) const;

    bool      path_requested = false;
    SDL_Point final_dest{0, 0};

private:
    bool advance(AnimationFrame*& frame);
    void switch_to(const std::string& anim_id, std::size_t path_index = 0);

    SDL_Point bottom_middle(SDL_Point pos) const;
    bool point_in_impassable(SDL_Point pt, const Asset* ignored) const;
    bool path_blocked(SDL_Point from, SDL_Point to, const Asset* ignored, std::vector<const Asset*>* blockers = nullptr) const;
    bool attempt_unstick(SDL_Point from, SDL_Point to, const std::vector<const Asset*>& blockers);
    bool adjust_next_checkpoint(const std::vector<const Asset*>& blockers);
    bool handle_blocked_path(SDL_Point from, SDL_Point to, const std::vector<const Asset*>& blockers);
    void mark_progress_toward_checkpoints();
    bool replan_to_destination();

    vibble::grid::Grid& grid() const;
    int                 effective_grid_resolution(std::optional<int> override_resolution) const;
    SDL_Point           convert_delta_to_world(SDL_Point delta, int resolution) const;

private:
    friend class StridePlayer;

    Asset*  self_          = nullptr;
    Assets* assets_owner_  = nullptr;
    vibble::grid::Grid* grid_service_ = nullptr;

    int visited_thresh_ = 0;

    Plan   plan_{};
    size_t stride_index_        = 0;
    int    stride_frame_counter_ = 0;
    size_t next_checkpoint_index_ = 0;

    PathSanitizer sanitizer_{};
    GetBestPath   planner_{};
    StridePlayer  player_{};

    std::optional<std::string> queued_anim_{};
    std::unordered_map<std::string, std::size_t> active_paths_{};
    struct ManualAnimationState {
        std::string id;
        bool        loop = true;
};
    std::optional<ManualAnimationState> manual_animation_{};
};
