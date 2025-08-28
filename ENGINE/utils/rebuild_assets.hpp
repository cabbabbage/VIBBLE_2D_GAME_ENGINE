#ifndef REBUILD_ASSETS_HPP
#define REBUILD_ASSETS_HPP

#include <string>
#include <SDL.h>

class RebuildAssets {
public:
    RebuildAssets(SDL_Renderer* renderer, const std::string& map_dir);
};

#endif 
