#pragma once

#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include "stride_types.hpp"
#include "path_sanitizer.hpp"
#include "get_best_path.hpp"
#include "stride_player.hpp"

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
    void move(const std::vector<SDL_Point>& rel_checkpoints, int visited_thresh_px);
    void refresh_z_index();

    bool      path_requested = false;
    SDL_Point final_dest{0, 0};

private:
    bool advance(AnimationFrame*& frame);
    void switch_to(const std::string& anim_id);

    SDL_Point bottom_middle(SDL_Point pos) const;
    bool point_in_impassable(SDL_Point pt, const Asset* ignored) const;
    bool path_blocked(SDL_Point from, SDL_Point to, const Asset* ignored) const;

private:
    friend class StridePlayer;

    Asset*  self_          = nullptr;
    Assets* assets_owner_  = nullptr;

    int visited_thresh_ = 0;

    Plan   plan_{};
    size_t stride_index_        = 0;
    int    stride_frame_counter_ = 0;

    PathSanitizer sanitizer_{};
    GetBestPath   planner_{};
    StridePlayer  player_{};

    std::optional<std::string> queued_anim_{};
};
