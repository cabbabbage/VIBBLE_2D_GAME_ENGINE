#pragma once

#include <SDL.h>

class WarpedScreenGrid;
class Input;

class PanAndZoom {
public:
    void set_zoom_scale_factor(double factor);

    void handle_input(WarpedScreenGrid& cam, const Input& input, bool pan_blocked);

    void cancel(WarpedScreenGrid& cam);

    bool is_panning() const { return panning_; }

private:
    double zoom_scale_factor_ = 1.1;
    bool panning_ = false;
    bool pan_drag_pending_ = false;
    SDL_Point pan_start_mouse_screen_{0, 0};
    SDL_Point pan_start_center_{0, 0};
};
