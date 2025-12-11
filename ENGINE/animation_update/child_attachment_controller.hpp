#pragma once

#include <string_view>
#include <vector>

#include <SDL.h>

#include "animation_update/animation_update.hpp"
#include "asset/Asset.hpp"
#include "asset/animation_frame.hpp"

namespace animation_update::child_attachments {

struct ParentState {
    SDL_Point position{0, 0};
    SDL_Point base_position{0, 0};
    float scale = 1.0f;
    bool flipped = false;
    std::string_view animation_id{};
};

void update_dimensions(Asset::AnimationChildAttachment& slot);
void restart(Asset::AnimationChildAttachment& slot);
void advance_frames(std::vector<Asset::AnimationChildAttachment>& slots, const ParentState& parent_state, float dt);
void apply_frame_data(std::vector<Asset::AnimationChildAttachment>& slots, const ParentState& parent_state, const AnimationFrame* frame, const std::vector<AnimationChildFrameData>* override_children = nullptr);

}
