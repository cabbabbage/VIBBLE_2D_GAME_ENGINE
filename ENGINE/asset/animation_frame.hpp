#pragma once

#include <vector>
#include <SDL.h>

#include "animation_update/combat_geometry.hpp"

struct AnimationChildFrameData {
    int   child_index = -1;
    int   dx          = 0;
    int   dy          = 0;
    float degree      = 0.0f;
    bool  render_in_front = true;
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
    SDL_Texture* base_texture = nullptr;
    SDL_Texture* foreground_texture = nullptr;
    SDL_Texture* background_texture = nullptr;
    SDL_Texture* get_base_texture() const { return base_texture; }
    SDL_Texture* get_foreground_texture() const { return foreground_texture; }
    SDL_Texture* get_background_texture() const { return background_texture; }
    std::vector<AnimationChildFrameData> children;
    animation_update::FrameHitGeometry hit_geometry;
    animation_update::FrameAttackGeometry attack_geometry;
};
