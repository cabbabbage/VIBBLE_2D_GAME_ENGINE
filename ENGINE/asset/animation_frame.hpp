#pragma once

#include <vector>
#include <SDL.h>

struct AnimationChildFrameData {
    int   child_index = -1;
    int   dx          = 0;
    int   dy          = 0;
    float degree      = 0.0f;
    bool  visible     = false;
};

class AnimationFrame {
public:
    int dx = 0;
    int dy = 0;
    bool z_resort = true;
    SDL_Color rgb{255, 255, 255, 255};
    int frame_index = -1;
    AnimationFrame* prev = nullptr;
    AnimationFrame* next = nullptr;
    bool is_last = false;
    bool is_first = false;
    std::vector<AnimationChildFrameData> children;
};

