#pragma once

#include <SDL.h>

#include "animation.hpp"

class AssetInfo;

class AnimationCloner {
public:
    struct Options {
        bool flip_horizontal = false;
        bool flip_vertical   = false;
        bool reverse_frames  = false;
        bool flip_movement_horizontal = false;
        bool flip_movement_vertical   = false;
};

    static bool Clone(const Animation& source, Animation&       dest, const Options&   opts, SDL_Renderer*    renderer, AssetInfo&       info);

    static void ApplyChildFrameFlip(std::vector<AnimationChildFrameData>& children, const Options& opts);
};
