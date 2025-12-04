#pragma once

#include <SDL.h>

#include "animation.hpp"

class AssetInfo;

// Utility to deep-clone an animation (frames, textures, movement paths, metadata).
// Ensures derived animations sourced from another animation own independent frame data.
class AnimationCloner {
public:
    struct Options {
        bool flip_horizontal = false;
        bool flip_vertical   = false;
        bool reverse_frames  = false;
        bool flip_movement_horizontal = false;
        bool flip_movement_vertical   = false;
    };

    // Clone |source| into |dest| applying flips/reverse as requested.
    // Returns true on success; leaves |dest| with fully rebuilt frame cache and movement paths.
    static bool Clone(const Animation& source,
                      Animation&       dest,
                      const Options&   opts,
                      SDL_Renderer*    renderer,
                      AssetInfo&       info);

    // Mirrors child attachment offsets around the parent's bottom-center pivot
    // using the provided flip options. Exposed for tests and reuse.
    static void ApplyChildFrameFlip(std::vector<AnimationChildFrameData>& children,
                                    const Options& opts);
};
