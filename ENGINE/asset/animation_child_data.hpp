#pragma once

#include <string>
#include <vector>

#include "animation_frame_variant.hpp"

enum class AnimationChildMode {
    Static,
    Async,
};

struct AnimationChildData {
    std::string name;
    std::string asset_name;
    std::string animation_override;
    AnimationChildMode mode = AnimationChildMode::Static;
    bool auto_start = false;
    std::vector<AnimationChildFrameData> frames;

    bool valid() const { return !name.empty() && !asset_name.empty(); }
    bool is_static() const { return mode == AnimationChildMode::Static; }
    bool is_async() const { return mode == AnimationChildMode::Async; }
    std::size_t frame_count() const { return frames.size(); }
};
