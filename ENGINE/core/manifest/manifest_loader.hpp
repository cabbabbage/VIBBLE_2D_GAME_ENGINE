#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace manifest {

struct ManifestData {
    nlohmann::json assets;
    nlohmann::json maps;
    nlohmann::json rooms;
    nlohmann::json raw;
};

ManifestData load_manifest();

std::string manifest_path();

} // namespace manifest

