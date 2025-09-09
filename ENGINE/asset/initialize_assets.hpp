#pragma once

#include <vector>
#include <memory>
#include <unordered_set>
#include <SDL.h>

class Assets;
class Asset;
class Room;

class InitializeAssets {

	public:
    static void initialize(Assets& assets, std::vector<Asset>&& loaded, std::vector<Room*> rooms, int screen_width, int screen_height, int screen_center_x, int screen_center_y, int map_radius);

	private:
    static void setup_shading_groups(Assets& assets);
    static void setup_static_sources(Assets& assets);
    static void set_shading_group_recursive(Asset& asset, int group, int );
    static void collect_assets_in_range(const Asset* asset, SDL_Point center, int radius, std::vector<Asset*>& result);
    static void find_player(Assets& assets);
};
