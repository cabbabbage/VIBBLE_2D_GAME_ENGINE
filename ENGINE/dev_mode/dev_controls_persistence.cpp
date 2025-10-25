#include "dev_controls_persistence.hpp"

#include <iostream>

#include <nlohmann/json.hpp>

#include "dev_mode/core/manifest_store.hpp"

namespace devmode {

bool persist_map_manifest_entry(core::ManifestStore& store,
                                const std::string& map_id,
                                const nlohmann::json& data,
                                std::ostream& log) {
    if (map_id.empty()) {
        log << "[DevControls] Map identifier is empty; cannot persist map entry\n";
        return false;
    }

    try {
        if (!store.update_map_entry(map_id, data)) {
            log << "[DevControls] Failed to persist map entry for '" << map_id << "'\n";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        log << "[DevControls] Exception while persisting map entry '" << map_id
            << "': " << ex.what() << "\n";
    } catch (...) {
        log << "[DevControls] Unknown exception while persisting map entry '" << map_id << "'\n";
    }
    return false;
}

}
