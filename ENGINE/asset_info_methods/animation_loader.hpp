#pragma once

#include <SDL.h>
#include "animation_update/custom_controllers/Davey_controller.hpp"

#include "animation_update/custom_controllers/Vibble_controller.hpp"
#include "animation_update/custom_controllers/Bomb_controller.hpp"
#include "animation_update/custom_controllers/Frog_controller.hpp"
#include "animation_update/custom_controllers/default_controller.hpp"

class AssetInfo;

class AnimationLoader {

	public:
    static void load(AssetInfo& info, SDL_Renderer* renderer);
    static void get_area_textures(AssetInfo& info, SDL_Renderer* renderer);
};
