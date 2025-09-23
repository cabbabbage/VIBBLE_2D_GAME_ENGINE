#include "spawn_group_utils.hpp"

#include <algorithm>
#include <random>

#include <nlohmann/json.hpp>

namespace devmode::spawn {

std::string generate_spawn_id() {
    static std::mt19937 rng(std::random_device{}());
    static const char* hex = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string id = "spn-";
    for (int i = 0; i < 12; ++i) {
        id.push_back(hex[dist(rng)]);
    }
    return id;
}

nlohmann::json& ensure_spawn_groups_array(nlohmann::json& root) {
    if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
        return root["spawn_groups"];
    }
    if (root.contains("assets") && root["assets"].is_array()) {
        root["spawn_groups"] = root["assets"];
        root.erase("assets");
        return root["spawn_groups"];
    }
    root["spawn_groups"] = nlohmann::json::array();
    return root["spawn_groups"];
}

const nlohmann::json* find_spawn_groups_array(const nlohmann::json& root) {
    if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
        return &root["spawn_groups"];
    }
    if (root.contains("assets") && root["assets"].is_array()) {
        return &root["assets"];
    }
    return nullptr;
}

bool sanitize_perimeter_spawn_groups(nlohmann::json& groups) {
    if (!groups.is_array()) {
        return false;
    }
    bool changed = false;
    for (auto& entry : groups) {
        if (!entry.is_object()) {
            continue;
        }
        std::string method = entry.value("position", std::string{});
        if (method == "Exact Position") {
            method = "Exact";
        }
        if (method != "Perimeter") {
            continue;
        }
        int min_number = entry.value("min_number", entry.value("max_number", 2));
        int max_number = entry.value("max_number", min_number);
        if (min_number < 2) {
            min_number = 2;
            changed = true;
        }
        if (max_number < 2) {
            max_number = 2;
            changed = true;
        }
        if (max_number < min_number) {
            max_number = min_number;
            changed = true;
        }
        if (!entry.contains("min_number") || !entry["min_number"].is_number_integer() ||
            entry["min_number"].get<int>() != min_number) {
            entry["min_number"] = min_number;
        }
        if (!entry.contains("max_number") || !entry["max_number"].is_number_integer() ||
            entry["max_number"].get<int>() != max_number) {
            entry["max_number"] = max_number;
        }
    }
    return changed;
}

} // namespace devmode::spawn

