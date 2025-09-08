#pragma once
#include <string>
#include <memory>
#include "utils/area.hpp"
#include "asset/asset_info.hpp"

struct SpawnInfo {
	std::string name;
	std::string type;
	std::string spawn_position;
	int quantity;
	int x_position;
	int y_position;
	int spacing_min;
	int spacing_max;
	std::shared_ptr<AssetInfo> info;
};
