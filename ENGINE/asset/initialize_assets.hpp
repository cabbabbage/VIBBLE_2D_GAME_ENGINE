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
    static void initialize(Assets& assets, std::vector<Room*> rooms, int screen_width, int screen_height, int screen_center_x, int screen_center_y, int map_radius);

        private:
    static void find_player(Assets& assets);
};
