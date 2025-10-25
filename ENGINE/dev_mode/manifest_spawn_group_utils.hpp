#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace devmode::manifest_utils {

bool remove_asset_from_spawn_groups(nlohmann::json& node, const std::string& asset_name);

}

