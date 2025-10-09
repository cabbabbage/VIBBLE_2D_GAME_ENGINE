#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace devmode::spawn {

std::string generate_spawn_id();

nlohmann::json& ensure_spawn_groups_array(nlohmann::json& root);

const nlohmann::json* find_spawn_groups_array(const nlohmann::json& root);

bool sanitize_perimeter_spawn_groups(nlohmann::json& groups);

}

