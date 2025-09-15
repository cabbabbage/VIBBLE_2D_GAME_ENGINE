#pragma once
#include <string>
#include <memory>
#include "utils/area.hpp"
#include "asset/asset_info.hpp"

struct SpawnInfo {
	std::string name;
	std::string position;
	std::string spawn_id;
	SDL_Point exact_offset{0, 0};
	int exact_origin_w = 0;
	int exact_origin_h = 0;
	int quantity = 0;
	int grid_spacing = 0;
	int jitter = 0;
	int empty_grid_spaces = 0;
	SDL_Point exact_point{ -1, -1 };
	int border_shift = 0;
        int sector_center = 0;
        int sector_range = 0;
        SDL_Point perimeter_offset{0, 0};
        int percent_x_min = 0;
        int percent_x_max = 0;
        int percent_y_min = 0;
        int percent_y_max = 0;
        bool check_overlap = false;
        bool check_min_spacing = false;
        std::shared_ptr<AssetInfo> info;
};