#pragma once

#include <SDL.h>

class camera;
class Input;

class PanAndZoom {
public:
    void set_zoom_scale_factor(double factor);

    void handle_input(camera& cam, const Input& input, bool pan_blocked);

    bool is_panning() const { return panning_; }

private:
    double zoom_scale_factor_ = 1.1;
    bool panning_ = false;
    SDL_Point pan_start_mouse_map_{0, 0};
    SDL_Point pan_start_center_{0, 0};
};
