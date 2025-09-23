#include "map_layers_controller.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
constexpr int kDefaultRoomRangeMax = 64;
constexpr int kDefaultCandidateRangeMax = 128;
constexpr int kDefaultLayerRadiusStep = 512;

int next_layer_radius(const json& layers) {
    int max_radius = 0;
    bool has_layer = false;
    if (layers.is_array()) {
        for (const auto& layer : layers) {
            if (!layer.is_object()) continue;
            has_layer = true;
            max_radius = std::max(max_radius, layer.value("radius", 0));
        }
    }
    if (!has_layer) return 0;
    int step = std::max(kDefaultLayerRadiusStep, max_radius / 3);
    if (max_radius <= 0) {
        return kDefaultLayerRadiusStep;
    }
    return max_radius + step;
}
}

void MapLayersController::bind(json* map_info, std::string map_path) {
    map_info_ = map_info;
    map_path_ = std::move(map_path);
    ensure_initialized();
    dirty_ = false;
    notify();
}

void MapLayersController::add_listener(Listener cb) {
    if (!cb) return;
    listeners_.push_back(std::move(cb));
}

void MapLayersController::clear_listeners() {
    listeners_.clear();
}

bool MapLayersController::save() {
    if (!map_info_) return false;
    std::string path = map_info_path();
    if (path.empty()) return false;

    std::ofstream out(path);
    if (!out) {
        std::cerr << "[MapLayersController] Failed to open " << path << " for writing\n";
        return false;
    }

    try {
        out << map_info_->dump(2);
        mark_clean();
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[MapLayersController] Serialize error: " << ex.what() << "\n";
        return false;
    }
}

bool MapLayersController::reload() {
    if (!map_info_) return false;
    std::string path = map_info_path();
    if (path.empty()) return false;

    std::ifstream in(path);
    if (!in) {
        std::cerr << "[MapLayersController] Failed to open " << path << " for reading\n";
        return false;
    }

    try {
        json fresh;
        in >> fresh;
        *map_info_ = std::move(fresh);
        ensure_initialized();
        mark_clean();
        notify();
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[MapLayersController] Parse error: " << ex.what() << "\n";
        return false;
    }
}

void MapLayersController::mark_clean() {
    dirty_ = false;
}

int MapLayersController::layer_count() const {
    if (!map_info_) return 0;
    const json* layers = map_info_->contains("map_layers") ? &(*map_info_)["map_layers"] : nullptr;
    if (!layers || !layers->is_array()) return 0;
    return static_cast<int>(layers->size());
}

const json* MapLayersController::layer(int index) const {
    if (!map_info_) return nullptr;
    const auto& arr = layers();
    if (!arr.is_array() || index < 0 || index >= static_cast<int>(arr.size())) return nullptr;
    return &arr[index];
}

json* MapLayersController::layer(int index) {
    if (!map_info_) return nullptr;
    auto& arr = (*map_info_)["map_layers"];
    if (!arr.is_array() || index < 0 || index >= static_cast<int>(arr.size())) return nullptr;
    return &arr[index];
}

const json& MapLayersController::layers() const {
    static json empty = json::array();
    if (!map_info_) return empty;
    const auto it = map_info_->find("map_layers");
    if (it == map_info_->end() || !it->is_array()) return empty;
    return *it;
}

std::vector<std::string> MapLayersController::available_rooms() const {
    std::vector<std::string> result;
    if (!map_info_) return result;
    const auto rooms_it = map_info_->find("rooms_data");
    if (rooms_it == map_info_->end() || !rooms_it->is_object()) return result;
    result.reserve(rooms_it->size());
    for (auto it = rooms_it->begin(); it != rooms_it->end(); ++it) {
        result.push_back(it.key());
    }
    std::sort(result.begin(), result.end());
    return result;
}

int MapLayersController::create_layer(const std::string& display_name) {
    if (!map_info_) return -1;
    ensure_initialized();
    auto& arr = (*map_info_)["map_layers"];
    const int idx = static_cast<int>(arr.size());
    int radius = arr.empty() ? 0 : next_layer_radius(arr);
    json layer = {
        {"level", idx},
        {"name", display_name.empty() ? std::string("layer_") + std::to_string(idx) : display_name},
        {"radius", arr.empty() ? 0 : radius},
        {"max_rooms", 0},
        {"rooms", json::array()}
    };
    arr.push_back(std::move(layer));
    ensure_layer_indices();
    dirty_ = true;
    notify();
    return idx;
}

bool MapLayersController::delete_layer(int index) {
    if (!map_info_) return false;
    auto& arr = (*map_info_)["map_layers"];
    if (!arr.is_array() || index < 0 || index >= static_cast<int>(arr.size())) return false;
    arr.erase(arr.begin() + index);
    ensure_layer_indices();
    dirty_ = true;
    notify();
    return true;
}

bool MapLayersController::reorder_layer(int from, int to) {
    if (!map_info_) return false;
    auto& arr = (*map_info_)["map_layers"];
    if (!arr.is_array() || arr.empty()) return false;
    const int count = static_cast<int>(arr.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) return false;
    json layer = arr[from];
    arr.erase(arr.begin() + from);
    arr.insert(arr.begin() + to, std::move(layer));
    ensure_layer_indices();
    dirty_ = true;
    notify();
    return true;
}

bool MapLayersController::rename_layer(int index, const std::string& name) {
    if (!validate_layer_index(index)) return false;
    auto* layer_json = layer(index);
    if (!layer_json) return false;
    std::string trimmed = name;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), trimmed.end());
    if (trimmed.empty()) return false;
    (*layer_json)["name"] = trimmed;
    dirty_ = true;
    notify();
    return true;
}

bool MapLayersController::set_layer_radius(int index, int radius) {
    if (!validate_layer_index(index)) return false;
    auto* layer_json = layer(index);
    if (!layer_json) return false;
    (*layer_json)["radius"] = std::max(0, radius);
    dirty_ = true;
    notify();
    return true;
}

bool MapLayersController::set_layer_room_count(int index, int max_rooms) {
    if (!validate_layer_index(index)) return false;
    auto* layer_json = layer(index);
    if (!layer_json) return false;
    (*layer_json)["max_rooms"] = std::max(0, max_rooms);
    clamp_layer_counts(*layer_json);
    dirty_ = true;
    notify();
    return true;
}

bool MapLayersController::add_candidate(int layer_index, const std::string& room_name) {
    if (!validate_layer_index(layer_index)) return false;
    auto* layer_json = layer(layer_index);
    if (!layer_json) return false;
    auto& rooms = (*layer_json)["rooms"];
    if (!rooms.is_array()) rooms = json::array();
    if (room_name.empty()) return false;
    json candidate = {
        {"name", room_name},
        {"max_instances", 1},
        {"required_children", json::array()}
    };
    rooms.push_back(std::move(candidate));
    clamp_layer_counts(*layer_json);
    dirty_ = true;
    notify();
    return true;
}

bool MapLayersController::remove_candidate(int layer_index, int candidate_index) {
    if (!validate_layer_index(layer_index)) return false;
    auto* layer_json = layer(layer_index);
    if (!layer_json) return false;
    auto& rooms = (*layer_json)["rooms"];
    if (!rooms.is_array() || candidate_index < 0 || candidate_index >= static_cast<int>(rooms.size())) return false;
    rooms.erase(rooms.begin() + candidate_index);
    clamp_layer_counts(*layer_json);
    dirty_ = true;
    notify();
    return true;
}

bool MapLayersController::set_candidate_instance_count(int layer_index, int candidate_index, int max_instances) {
    if (!validate_layer_index(layer_index)) return false;
    auto* layer_json = layer(layer_index);
    if (!layer_json) return false;
    auto& rooms = (*layer_json)["rooms"];
    if (!rooms.is_array() || candidate_index < 0 || candidate_index >= static_cast<int>(rooms.size())) return false;
    auto& candidate = rooms[candidate_index];
    candidate["max_instances"] = std::max(0, max_instances);
    clamp_layer_counts(*layer_json);
    dirty_ = true;
    notify();
    return true;
}

bool MapLayersController::add_candidate_child(int layer_index, int candidate_index, const std::string& child_room) {
    if (!validate_layer_index(layer_index)) return false;
    auto* layer_json = layer(layer_index);
    if (!layer_json) return false;
    auto& rooms = (*layer_json)["rooms"];
    if (!rooms.is_array() || candidate_index < 0 || candidate_index >= static_cast<int>(rooms.size())) return false;
    if (child_room.empty()) return false;
    auto& candidate = rooms[candidate_index];
    auto& required = candidate["required_children"];
    if (!required.is_array()) required = json::array();
    bool changed = false;
    if (std::find(required.begin(), required.end(), child_room) == required.end()) {
        required.push_back(child_room);
        changed = true;
    }

    auto& layers_arr = (*map_info_)["map_layers"];
    if (!layers_arr.is_array()) layers_arr = json::array();
    int child_layer_index = layer_index + 1;
    bool layer_added = false;

    if (child_layer_index >= static_cast<int>(layers_arr.size())) {
        int new_level = static_cast<int>(layers_arr.size());
        int radius = layers_arr.empty() ? 0 : next_layer_radius(layers_arr);
        json child_layer = {
            {"level", new_level},
            {"name", std::string("layer_") + std::to_string(new_level)},
            {"radius", layers_arr.empty() ? 0 : radius},
            {"max_rooms", 0},
            {"rooms", json::array()}
        };
        layers_arr.push_back(std::move(child_layer));
        child_layer_index = new_level;
        layer_added = true;
    }

    json& child_layer = layers_arr[child_layer_index];
    if (!child_layer.is_object()) child_layer = json::object();
    auto& child_rooms = child_layer["rooms"];
    if (!child_rooms.is_array()) child_rooms = json::array();

    bool child_layer_changed = false;
    auto child_it = std::find_if(child_rooms.begin(), child_rooms.end(), [&](const json& entry) {
        return entry.is_object() && entry.value("name", std::string()) == child_room;
    });
    if (child_it == child_rooms.end()) {
        json child_candidate = {
            {"name", child_room},
            {"max_instances", 1},
            {"required_children", json::array()}
        };
        child_rooms.push_back(std::move(child_candidate));
        child_layer_changed = true;
    } else {
        json& entry = *child_it;
        int max_inst = entry.value("max_instances", 0);
        if (max_inst < 1) {
            entry["max_instances"] = 1;
            child_layer_changed = true;
        }
    }

    clamp_layer_counts(child_layer);
    if (layer_added) {
        ensure_layer_indices();
    }

    if (child_layer_changed) changed = true;
    if (layer_added) changed = true;

    clamp_layer_counts(*layer_json);

    if (changed) {
        dirty_ = true;
        notify();
    }
    return changed;
}

bool MapLayersController::remove_candidate_child(int layer_index, int candidate_index, const std::string& child_room) {
    if (!validate_layer_index(layer_index)) return false;
    auto* layer_json = layer(layer_index);
    if (!layer_json) return false;
    auto& rooms = (*layer_json)["rooms"];
    if (!rooms.is_array() || candidate_index < 0 || candidate_index >= static_cast<int>(rooms.size())) return false;
    auto& candidate = rooms[candidate_index];
    auto& required = candidate["required_children"];
    if (!required.is_array()) return false;
    auto it = std::find(required.begin(), required.end(), child_room);
    if (it == required.end()) return false;
    required.erase(it);
    dirty_ = true;
    notify();
    return true;
}

void MapLayersController::ensure_initialized() {
    if (!map_info_) return;
    if (!map_info_->contains("map_layers") || !(*map_info_)["map_layers"].is_array()) {
        (*map_info_)["map_layers"] = json::array();
    }
    ensure_layer_indices();
}

void MapLayersController::ensure_layer_indices() {
    if (!map_info_) return;
    auto& arr = (*map_info_)["map_layers"];
    if (!arr.is_array()) {
        arr = json::array();
        return;
    }
    for (size_t i = 0; i < arr.size(); ++i) {
        auto& layer_json = arr[i];
        if (!layer_json.is_object()) layer_json = json::object();
        layer_json["level"] = static_cast<int>(i);
        if (!layer_json.contains("name")) {
            std::ostringstream oss;
            oss << "layer_" << i;
            layer_json["name"] = oss.str();
        }
        if (!layer_json.contains("radius")) layer_json["radius"] = 0;
        if (layer_json.contains("min_rooms")) layer_json.erase("min_rooms");
        if (!layer_json.contains("max_rooms")) layer_json["max_rooms"] = 0;
        if (!layer_json.contains("rooms") || !layer_json["rooms"].is_array()) {
            layer_json["rooms"] = json::array();
        }
        clamp_layer_counts(layer_json);
        auto& rooms = layer_json["rooms"];
        for (auto& candidate : rooms) {
            if (!candidate.is_object()) candidate = json::object();
            if (!candidate.contains("name")) candidate["name"] = "";
            if (!candidate.contains("max_instances")) candidate["max_instances"] = 0;
            if (!candidate.contains("required_children") || !candidate["required_children"].is_array()) {
                candidate["required_children"] = json::array();
            }
            if (candidate.contains("min_instances")) candidate.erase("min_instances");
            int max_inst = std::max(0, candidate.value("max_instances", 0));
            candidate["max_instances"] = std::min(max_inst, kDefaultCandidateRangeMax);
        }
    }
}

bool MapLayersController::validate_layer_index(int index) const {
    if (!map_info_) return false;
    const auto& arr = layers();
    return arr.is_array() && index >= 0 && index < static_cast<int>(arr.size());
}

bool MapLayersController::validate_candidate_index(const json& layer, int candidate_index) const {
    if (!layer.is_object()) return false;
    const auto it = layer.find("rooms");
    if (it == layer.end() || !it->is_array()) return false;
    return candidate_index >= 0 && candidate_index < static_cast<int>(it->size());
}

void MapLayersController::notify() {
    for (auto& cb : listeners_) {
        if (cb) cb();
    }
}

std::string MapLayersController::map_info_path() const {
    if (!map_info_) return {};
    if (!map_path_.empty()) {
        return map_path_ + "/map_info.json";
    }
    return {};
}

void MapLayersController::clamp_layer_counts(json& layer) const {
    if (!layer.is_object()) return;
    int max_rooms = std::max(0, layer.value("max_rooms", 0));
    layer["max_rooms"] = std::min(max_rooms, kDefaultRoomRangeMax);

    int max_sum = 0;
    const auto rooms_it = layer.find("rooms");
    if (rooms_it != layer.end() && rooms_it->is_array()) {
        for (auto& candidate : *rooms_it) {
            if (!candidate.is_object()) continue;
            int max_inst = std::max(0, candidate.value("max_instances", 0));
            candidate["max_instances"] = std::min(max_inst, kDefaultCandidateRangeMax);
            max_sum += candidate["max_instances"].get<int>();
        }
    }
    if (max_sum > 0 && layer["max_rooms"].get<int>() > max_sum) {
        layer["max_rooms"] = max_sum;
    }
}

