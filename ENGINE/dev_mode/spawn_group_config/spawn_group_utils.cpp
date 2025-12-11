#include "spawn_group_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>

#include <nlohmann/json.hpp>

#include "utils/map_grid_settings.hpp"
#include "utils/grid.hpp"

namespace devmode::spawn {
namespace {

constexpr int kDefaultMinNumber = 1;
constexpr int kEdgeInsetSliderMin = 0;
constexpr int kEdgeInsetSliderMax = 200;
constexpr int kEdgeInsetDefault = 100;

int read_int(const nlohmann::json& obj, const char* key, int fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number_float()) return static_cast<int>(std::lround(it->get<double>()));
    if (it->is_string()) {
        try {
            const std::string text = it->get<std::string>();
            size_t idx = 0;
            int value = std::stoi(text, &idx);
            if (idx == text.size()) return value;
        } catch (...) {
        }
    }
    return fallback;
}

double read_double(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (it->is_number_float()) return it->get<double>();
    if (it->is_number_integer()) return static_cast<double>(it->get<int>());
    if (it->is_string()) {
        try {
            const std::string text = it->get<std::string>();
            size_t idx = 0;
            double value = std::stod(text, &idx);
            if (idx == text.size()) return value;
        } catch (...) {
        }
    }
    return fallback;
}

bool is_integral(double value) {
    if (!std::isfinite(value)) return false;
    const double rounded = std::round(value);
    return std::fabs(value - rounded) < 1e-9;
}

}

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

    if (root.is_array()) {
        return root;
    }

    if (!root.is_object()) {
        root = nlohmann::json::object();
    }

    if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
        return root["spawn_groups"];
    }

    root["spawn_groups"] = nlohmann::json::array();
    return root["spawn_groups"];
}

const nlohmann::json* find_spawn_groups_array(const nlohmann::json& root) {
    if (root.is_array()) {
        return &root;
    }
    if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
        return &root["spawn_groups"];
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
        if (method != "Perimeter" && method != "Edge") {
            continue;
        }
        if (method == "Perimeter") {
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
        } else if (method == "Edge") {
            int min_number = entry.value("min_number", entry.value("max_number", kDefaultMinNumber));
            int max_number = entry.value("max_number", min_number);
            if (min_number < 1) {
                min_number = 1;
                changed = true;
            }
            if (max_number < min_number) {
                max_number = min_number;
                changed = true;
            }
            int inset = entry.value("edge_inset_percent", kEdgeInsetDefault);
            if (inset < kEdgeInsetSliderMin) {
                inset = kEdgeInsetSliderMin;
                changed = true;
            }
            if (inset > kEdgeInsetSliderMax) {
                inset = kEdgeInsetSliderMax;
                changed = true;
            }
            entry["min_number"] = min_number;
            entry["max_number"] = max_number;
            entry["edge_inset_percent"] = inset;
        } else {
            if (entry.contains("edge_inset_percent")) {
                entry.erase("edge_inset_percent");
                changed = true;
            }
        }
    }
    return changed;
}

bool sanitize_spawn_group_candidates(nlohmann::json& entry) {
    bool changed = false;
    if (!entry.is_object()) {
        entry = nlohmann::json::object();
        changed = true;
    }
    if (!entry.contains("candidates") || !entry["candidates"].is_array()) {
        entry["candidates"] = nlohmann::json::array();
        changed = true;
    }
    auto& candidates = entry["candidates"];
    nlohmann::json sanitized = nlohmann::json::array();
    for (auto& candidate : candidates) {
        if (!candidate.is_object()) {
            changed = true;
            continue;
        }
        nlohmann::json sanitized_candidate = candidate;
        std::string name;
        if (sanitized_candidate.contains("name") && sanitized_candidate["name"].is_string()) {
            name = sanitized_candidate["name"].get<std::string>();
        }
        if (name.empty()) {
            sanitized_candidate["name"] = "null";
            changed = true;
        }

        double chance = 0.0;
        bool have_chance = false;
        if (sanitized_candidate.contains("chance")) {
            chance = read_double(sanitized_candidate, "chance", 0.0);
            have_chance = true;
        }
        if (!have_chance && sanitized_candidate.contains("weight")) {
            chance = read_double(sanitized_candidate, "weight", 0.0);
            have_chance = true;
        }
        if (!std::isfinite(chance) || chance < 0.0) {
            chance = 0.0;
        }
        if (!have_chance || read_double(sanitized_candidate, "chance", chance) != chance) {
            changed = true;
        }
        if (is_integral(chance)) {
            sanitized_candidate["chance"] = static_cast<int>(std::llround(chance));
        } else {
            sanitized_candidate["chance"] = chance;
        }

        sanitized.push_back(std::move(sanitized_candidate));
    }

    if (sanitized.empty()) {
        sanitized.push_back({{"name", "null"}, {"chance", 0}});
        changed = true;
    }

    if (sanitized != candidates) {
        candidates = std::move(sanitized);
        changed = true;
    }

    return changed;
}

bool ensure_spawn_group_entry_defaults(nlohmann::json& entry,
                                       const std::string& default_display_name,
                                       std::optional<int> default_resolution) {
    bool changed = false;
    if (!entry.is_object()) {
        entry = nlohmann::json::object();
        changed = true;
    }

    auto read_bool = [](const nlohmann::json& node, const char* key, bool fallback) {
        if (!node.is_object() || !node.contains(key)) return fallback;
        const auto& value = node.at(key);
        if (value.is_boolean()) return value.get<bool>();
        if (value.is_number_integer()) return value.get<int>() != 0;
        if (value.is_string()) {
            std::string text = value.get<std::string>();
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (text == "true" || text == "1" || text == "yes") return true;
            if (text == "false" || text == "0" || text == "no") return false;
        }
        return fallback;
};

    auto spawn_id_it = entry.find("spawn_id");
    if (spawn_id_it == entry.end() || !spawn_id_it->is_string() ||
        spawn_id_it->get<std::string>().empty()) {
        entry["spawn_id"] = generate_spawn_id();
        changed = true;
    }

    auto display_it = entry.find("display_name");
    if (display_it == entry.end() || !display_it->is_string() ||
        display_it->get<std::string>().empty()) {
        entry["display_name"] = default_display_name;
        changed = true;
    }

    auto position_it = entry.find("position");
    if (position_it == entry.end() || !position_it->is_string() ||
        position_it->get<std::string>().empty()) {
        entry["position"] = "Random";
        changed = true;
    }

    std::string method = entry.value("position", std::string{"Random"});
    if (method == "Exact Position") method = "Exact";

    int min_number = read_int(entry, "min_number", 1);
    int max_number = read_int(entry, "max_number", min_number);
    min_number = std::max(1, min_number);
    max_number = std::max(min_number, max_number);
    if (!entry.contains("min_number") || !entry["min_number"].is_number_integer() ||
        entry["min_number"].get<int>() != min_number) {
        entry["min_number"] = min_number;
        changed = true;
    }
    if (!entry.contains("max_number") || !entry["max_number"].is_number_integer() ||
        entry["max_number"].get<int>() != max_number) {
        entry["max_number"] = max_number;
        changed = true;
    }

    if (!entry.contains("enforce_spacing") || !entry["enforce_spacing"].is_boolean()) {
        entry["enforce_spacing"] = false;
        changed = true;
    }

    const bool geometry_method = (method == "Exact" || method == "Perimeter");
    const bool geometry_flag = read_bool(entry, "resolve_geometry_to_room_size", geometry_method);
    if (!entry.contains("resolve_geometry_to_room_size") || !entry["resolve_geometry_to_room_size"].is_boolean() ||
        entry["resolve_geometry_to_room_size"].get<bool>() != geometry_flag) {
        entry["resolve_geometry_to_room_size"] = geometry_flag;
        changed = true;
    }

    const bool quantity_flag = read_bool(entry, "resolve_quantity_to_room_size", false);
    if (!entry.contains("resolve_quantity_to_room_size") || !entry["resolve_quantity_to_room_size"].is_boolean() ||
        entry["resolve_quantity_to_room_size"].get<bool>() != quantity_flag) {
        entry["resolve_quantity_to_room_size"] = quantity_flag;
        changed = true;
    }

    if (!entry.contains("locked") || !entry["locked"].is_boolean()) {
        entry["locked"] = false;
        changed = true;
    }

    const int fallback_resolution = default_resolution
        ? vibble::grid::clamp_resolution(*default_resolution) : vibble::grid::clamp_resolution(MapGridSettings::defaults().resolution);
    int resolution = read_int(entry, "resolution", fallback_resolution);
    resolution = vibble::grid::clamp_resolution(resolution);
    if (!entry.contains("resolution") || !entry["resolution"].is_number_integer() ||
        entry["resolution"].get<int>() != resolution) {
        entry["resolution"] = resolution;
        changed = true;
    }

    if (sanitize_spawn_group_candidates(entry)) {
        changed = true;
    }

    if (!entry.contains("explicit_flip") || !entry["explicit_flip"].is_boolean()) {
        entry["explicit_flip"] = false;
        changed = true;
    }
    if (!entry.contains("force_flipped") || !entry["force_flipped"].is_boolean()) {
        entry["force_flipped"] = false;
        changed = true;
    }

    if (method == "Edge") {
        int inset = entry.value("edge_inset_percent", kEdgeInsetDefault);
        int clamped_inset = std::clamp(inset, kEdgeInsetSliderMin, kEdgeInsetSliderMax);
        if (inset != clamped_inset ||
            !entry.contains("edge_inset_percent") ||
            !entry["edge_inset_percent"].is_number_integer()) {
            entry["edge_inset_percent"] = clamped_inset;
            changed = true;
        }
    } else if (method == "Perimeter") {
        int radius = entry.value("radius", entry.value("perimeter_radius", kPerimeterRadiusDefault));
        if (!entry.contains("radius") || !entry["radius"].is_number_integer() ||
            entry["radius"].get<int>() != radius) {
            entry["radius"] = radius;
            changed = true;
        }
        if (!entry.contains("perimeter_radius") || !entry["perimeter_radius"].is_number_integer() ||
            entry["perimeter_radius"].get<int>() != radius) {
            entry["perimeter_radius"] = radius;
            changed = true;
        }
    } else {
        if (entry.contains("edge_inset_percent")) {
            entry.erase("edge_inset_percent");
            changed = true;
        }
    }

    return changed;
}

}
