#pragma once

#include <vector>
#include <string>
#include <SDL.h>
#include "asset\asset_info.hpp"
#include "utils/area.hpp"
#include "utils\parallax.hpp"   

class AreaDebugRenderer {
public:
    AreaDebugRenderer(SDL_Renderer* renderer, Parallax& parallax);

    void setTestAreas(const std::vector<std::string>& areas);
    void render(const AssetInfo* info, int world_x, int world_y);

private:
    SDL_Renderer* renderer_;
    Parallax& parallax_;   
    std::vector<std::string> test_areas_;
};
