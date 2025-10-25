#include "dev_mode/room_editor_map_info.hpp"

#include <nlohmann/json.hpp>

#include "core/AssetsManager.hpp"
#include "dev_mode/core/manifest_store.hpp"

namespace devmode::room_editor_detail {

nlohmann::json resolve_map_info_blob(const Assets* assets,
                                     const devmode::core::ManifestStore* manifest_store,
                                     const std::string& map_id) {
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
    return nlohmann::json::object();
}

}

