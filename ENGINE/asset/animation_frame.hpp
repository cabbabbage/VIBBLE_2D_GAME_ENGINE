#pragma once
#include <SDL.h>

class AnimationFrame {
public:
    int dx = 0;
    int dy = 0;
    bool z_resort = true;
    SDL_Color rgb{255, 255, 255, 255};
    AnimationFrame* prev = nullptr;
    AnimationFrame* next = nullptr;
    bool is_last = false;
    bool is_first = false;
};

