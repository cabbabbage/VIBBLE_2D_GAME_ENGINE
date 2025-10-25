#include "dev_mode/manifest_spawn_group_utils.hpp"

#include <algorithm>

namespace devmode::manifest_utils {

bool remove_asset_from_spawn_groups(nlohmann::json& node, const std::string& asset_name) {
    bool modified = false;

    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string& key = it.key();
            nlohmann::json& value = it.value();
            if (key == "candidates" && value.is_array()) {
                auto& candidates = value;
                auto erase_it = std::remove_if(candidates.begin(), candidates.end(),
                    [&](nlohmann::json& candidate) {
                        if (!candidate.is_object()) {
                            return false;
                        }
                        auto name_it = candidate.find("name");
                        if (name_it == candidate.end() || !name_it->is_string()) {
                            return false;
                        }
                        return name_it->get<std::string>() == asset_name;
                    });
                if (erase_it != candidates.end()) {
                    candidates.erase(erase_it, candidates.end());
                    modified = true;
                }
            }
            if (remove_asset_from_spawn_groups(value, asset_name)) {
                modified = true;
            }
        }
    } else if (node.is_array()) {
        for (auto& element : node) {
            if (remove_asset_from_spawn_groups(element, asset_name)) {
                modified = true;
            }
        }
    }
    return modified;
}

}

