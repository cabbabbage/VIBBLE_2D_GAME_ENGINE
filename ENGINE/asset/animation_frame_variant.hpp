#pragma once

#include <SDL.h>
struct AnimationChildFrameData {
    int   child_index     = -1;
    int   dx              = 0;
    int   dy              = 0;
    float degree          = 0.0f;
    bool  render_in_front = true;
    bool  visible         = true;
};

class FrameVariant {
public:
    int varient = -1;
    SDL_Texture* base_texture        = nullptr;
    SDL_Texture* foreground_texture  = nullptr;
    SDL_Texture* background_texture  = nullptr;
    SDL_Texture* shadow_mask_texture = nullptr;

    SDL_Texture* get_base_texture() const        { return base_texture; }
    SDL_Texture* get_foreground_texture() const  { return foreground_texture; }
    SDL_Texture* get_background_texture() const  { return background_texture; }
    SDL_Texture* get_shadow_mask_texture() const { return shadow_mask_texture; }
};
