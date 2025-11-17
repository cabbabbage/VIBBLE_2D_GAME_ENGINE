#pragma once

#include <SDL.h>
#include <string>
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
    static bool clear_asset_cache(const std::string& asset_name);

    private:
    struct LoadAttemptResult {
        bool success     = false;
        bool cache_issue = false;
    };

    static LoadAttemptResult load_asset_animations_once(AssetInfo& info, SDL_Renderer* renderer, bool force_rebuild);
    static bool call_python_script_for_asset(const AssetInfo& info);
};
