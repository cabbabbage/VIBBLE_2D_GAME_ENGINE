#pragma once

#include <SDL.h>
#include <vector>
#include "utils/area.hpp"

class AreaUI {
public:
    struct Result {
        std::vector<Area::Point> points;
        int bg_w = 0;
        int bg_h = 0;
    };

    // Open editor using the base Area's texture as background.
    // The app_renderer is used to materialize a surface snapshot compatible
    // with a new editor renderer.
    static bool edit_over_area(SDL_Renderer* app_renderer,
                               const Area& base,
                               int window_w,
                               int window_h,
                               Result& out);

    // Open editor using an SDL_Texture as background. The texture must belong
    // to app_renderer; we will snapshot it to a surface and render in the
    // editor window.
    static bool edit_over_texture(SDL_Renderer* app_renderer,
                                  SDL_Texture* texture,
                                  int window_w,
                                  int window_h,
                                  Result& out);
};

