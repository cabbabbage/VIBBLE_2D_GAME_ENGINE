#pragma once

#include <string>
#include <vector>

#include "animation_frame_variant.hpp"

// Describes a single child attachment timeline (static or async) owned by an animation.
enum class AnimationChildMode {
    Static,
    Async,
};

struct AnimationChildData {
    std::string name;                // Logical identifier used by authoring/runtime (unique per asset).
    std::string asset_name;          // Target asset id to spawn when this child runs.
    std::string animation_override;  // Optional animation override for the spawned asset.
    AnimationChildMode mode = AnimationChildMode::Static;
    bool auto_start = false;         // Should this child auto-start at frame 0 (static) when animation begins?
    std::vector<AnimationChildFrameData> frames;  // Per-frame offsets/visibility for this child.

    bool valid() const { return !name.empty() && !asset_name.empty(); }
    bool is_static() const { return mode == AnimationChildMode::Static; }
    bool is_async() const { return mode == AnimationChildMode::Async; }
    std::size_t frame_count() const { return frames.size(); }
};
