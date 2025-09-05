#pragma once

#include <SDL.h>
#include "custom_controllers/Davey_controller.hpp"
#include "custom_controllers/Davey_default_controller.hpp"
#include "custom_controllers/Vibble_controller.hpp"
#include "custom_controllers/Bomb_controller.hpp"
#include "custom_controllers/Frog_controller.hpp"

class AssetInfo;

class AnimationLoader {
public:
    static void load(AssetInfo& info, SDL_Renderer* renderer);
    static void get_area_textures(AssetInfo& info, SDL_Renderer* renderer);
};
