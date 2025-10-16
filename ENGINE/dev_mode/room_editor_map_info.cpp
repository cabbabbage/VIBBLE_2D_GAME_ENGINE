#include "dev_mode/room_editor_map_info.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "core/AssetsManager.hpp"
#include "dev_mode/core/manifest_store.hpp"

namespace devmode::room_editor_detail {

nlohmann::json resolve_map_info_blob(const Assets* assets,
                                     const devmode::core::ManifestStore* manifest_store,
                                     const std::string& map_id,
                                     const std::string& map_path) {
    if (assets) {
        const nlohmann::json& in_memory = assets->map_info_json();
        if (in_memory.is_object()) {
            return in_memory;
        }
    }

    if (manifest_store && !map_id.empty()) {
        if (const nlohmann::json* entry = manifest_store->find_map_entry(map_id)) {
            if (entry->is_null()) {
                return nlohmann::json::object();
            }
            return *entry;
        }
    }

    nlohmann::json map_info_json;
    if (!map_path.empty()) {
        std::ifstream map_info(map_path + "/map_info.json");
        if (map_info.is_open()) {
            try {
                map_info >> map_info_json;
            } catch (...) {
                map_info_json = nlohmann::json::object();
            }
        }
    }

    if (map_info_json.is_null()) {
        map_info_json = nlohmann::json::object();
    }

    return map_info_json;
}

}  // namespace devmode::room_editor_detail

