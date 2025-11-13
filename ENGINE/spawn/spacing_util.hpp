#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "spawn_info.hpp"

inline std::unordered_set<std::string> collect_spacing_asset_names(const std::vector<SpawnInfo>& queue) {
    std::unordered_set<std::string> names;
    names.reserve(queue.size());
    for (const auto& item : queue) {
        if (!item.check_min_spacing) {
            continue;
        }
        for (const auto& cand : item.candidates) {
            if (!cand.info || cand.info->name.empty()) {
                continue;
            }
            names.insert(cand.info->name);
        }
    }
    return names;
}
