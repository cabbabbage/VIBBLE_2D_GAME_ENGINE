#pragma once

#include <ostream>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace devmode::core {
class ManifestStore;
}

namespace devmode {

bool persist_map_manifest_entry(core::ManifestStore& store, const std::string& map_id, const nlohmann::json& data, std::ostream& log);

}
