#pragma once

#include <algorithm>
#include <string>

#include <nlohmann/json.hpp>

namespace map_layers {

constexpr int kCandidateRangeMax = 128;

inline int clamp_candidate_min(int value) {
    return std::clamp(value, 0, kCandidateRangeMax);
}

inline int clamp_candidate_max(int min_value, int max_value) {
    const int clamped_min = clamp_candidate_min(min_value);
    return std::clamp(max_value, clamped_min, kCandidateRangeMax);
}

inline std::string create_room_entry(nlohmann::json& map_info) {
    if (!map_info.is_object()) {
        return std::string{};
    }
    nlohmann::json& rooms = map_info["rooms_data"];
    if (!rooms.is_object()) {
        rooms = nlohmann::json::object();
    }
    std::string base = "NewRoom";
    std::string key = base;
    int suffix = 1;
    while (rooms.contains(key)) {
        key = base + std::to_string(suffix++);
    }
    rooms[key] = nlohmann::json{{"name", key}};
    return key;
}

inline void rename_room_references_in_layers(nlohmann::json& map_info,
                                             const std::string& old_name,
                                             const std::string& new_name) {
    if (old_name == new_name) {
        return;
    }

    auto lit = map_info.find("map_layers");
    if (lit == map_info.end() || !lit->is_array()) {
        return;
    }

    for (auto& layer : *lit) {
        auto rooms_it = layer.find("rooms");
        if (rooms_it == layer.end() || !rooms_it->is_array()) {
            continue;
        }

        for (auto& entry : *rooms_it) {
            if (!entry.is_object()) {
                continue;
            }

            if (entry.value("name", std::string()) == old_name) {
                entry["name"] = new_name;
            }

            auto& children = entry["required_children"];
            if (!children.is_array()) {
                continue;
            }

            for (auto& child : children) {
                if (child.is_string() && child.get<std::string>() == old_name) {
                    child = new_name;
                }
            }
        }
    }
}

}

