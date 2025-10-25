#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

class Assets;

namespace devmode::core {
class ManifestStore;
}

namespace devmode::room_editor_detail {

nlohmann::json resolve_map_info_blob(const Assets* assets, const devmode::core::ManifestStore* manifest_store, const std::string& map_id);

}

