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
    };

    // Clone |source| into |dest| applying flips/reverse as requested.
    // Returns true on success; leaves |dest| with fully rebuilt frame cache and movement paths.
    static bool Clone(const Animation& source,
                      Animation&       dest,
                      const Options&   opts,
                      SDL_Renderer*    renderer,
                      AssetInfo&       info);
};
