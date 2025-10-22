#include "map_layers_controller.hpp"

#include "map_layers_common.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dev_controls_persistence.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <SDL_log.h>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
constexpr int kDefaultRoomRangeMax = 64;
using map_layers::clamp_candidate_max;
using map_layers::clamp_candidate_min;
}

void MapLayersController::bind(json* map_info, std::string map_path) {
    map_info_ = map_info;
    static_cast<void>(map_path);
    ensure_initialized();
    dirty_ = false;
    notify();
}

void MapLayersController::set_manifest_store(devmode::core::ManifestStore* store, std::string map_id) {
    manifest_store_ = store;
    map_id_ = std::move(map_id);
}

MapLayersController::ListenerId MapLayersController::add_listener(Listener cb) {
    if (!cb) return 0;
    const ListenerId id = next_listener_id_++;
    listeners_.push_back(ListenerEntry{id, std::move(cb)});
    return id;
}

void MapLayersController::remove_listener(ListenerId id) {
    if (id == 0) {
        return;
    }
    auto it = std::remove_if(listeners_.begin(), listeners_.end(),
                             [id](const ListenerEntry& entry) { return entry.id == id; });
    listeners_.erase(it, listeners_.end());
}

void MapLayersController::clear_listeners() {
    listeners_.clear();
    next_listener_id_ = 1;
}

bool MapLayersController::save() {
    if (!map_info_) return false;
    if (!manifest_store_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MapLayersController] Cannot save map info: manifest store is not available.");
        return false;
    }
    if (map_id_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MapLayersController] Cannot save map info: map identifier is empty.");
        return false;
    }
    if (!devmode::persist_map_manifest_entry(*manifest_store_, map_id_, *map_info_, std::cerr)) {
        return false;
    }
    manifest_store_->flush();
    mark_clean();
    return true;
}

bool MapLayersController::reload() {
    if (!map_info_) return false;
    if (!manifest_store_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MapLayersController] Cannot reload map info: manifest store is not available.");
        return false;
    }
    if (map_id_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MapLayersController] Cannot reload map info: map identifier is empty.");
        return false;
    }
    const nlohmann::json* entry = manifest_store_->find_map_entry(map_id_);
    if (!entry) {
        std::cerr << "[MapLayersController] Map '" << map_id_ << "' not found in manifest\n";
        return false;
    }
    *map_info_ = *entry;
    ensure_initialized();
    mark_clean();
    notify();
    return true;
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
    json layer = {
        {"level", idx},
        {"name", display_name.empty() ? std::string("layer_") + std::to_string(idx) : display_name},
        {"min_rooms", 0},
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

bool MapLayersController::add_candidate(int layer_index, const std::string& room_name) {
    if (!validate_layer_index(layer_index)) return false;
    auto* layer_json = layer(layer_index);
    if (!layer_json) return false;
    auto& rooms = (*layer_json)["rooms"];
    if (!rooms.is_array()) rooms = json::array();
    if (room_name.empty()) return false;
    json candidate = {
        {"name", room_name},
        {"min_instances", 0},
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

bool MapLayersController::set_candidate_instance_range(int layer_index,
                                                       int candidate_index,
                                                       int min_instances,
                                                       int max_instances) {
    if (!validate_layer_index(layer_index)) return false;
    auto* layer_json = layer(layer_index);
    if (!layer_json) return false;
    auto& rooms = (*layer_json)["rooms"];
    if (!rooms.is_array() || candidate_index < 0 || candidate_index >= static_cast<int>(rooms.size())) return false;
    auto& candidate = rooms[candidate_index];
    int clamped_min = clamp_candidate_min(min_instances);
    int clamped_max = clamp_candidate_max(clamped_min, max_instances);
    bool changed = false;
    if (candidate.value("min_instances", -1) != clamped_min) {
        candidate["min_instances"] = clamped_min;
        changed = true;
    }
    if (candidate.value("max_instances", -1) != clamped_max) {
        candidate["max_instances"] = clamped_max;
        changed = true;
    }
    clamp_layer_counts(*layer_json);
    if (changed) {
        dirty_ = true;
        notify();
    }
    return changed;
}

bool MapLayersController::set_candidate_instance_count(int layer_index, int candidate_index, int max_instances) {
    if (!validate_layer_index(layer_index)) return false;
    auto* layer_json = layer(layer_index);
    if (!layer_json) return false;
    auto& rooms = (*layer_json)["rooms"];
    if (!rooms.is_array() || candidate_index < 0 || candidate_index >= static_cast<int>(rooms.size())) return false;
    auto& candidate = rooms[candidate_index];
    int current_min = clamp_candidate_min(candidate.value("min_instances", 0));
    return set_candidate_instance_range(layer_index, candidate_index, current_min, max_instances);
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
        json child_layer = {
            {"level", new_level},
            {"name", std::string("layer_") + std::to_string(new_level)},
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
            {"min_instances", 0},
            {"max_instances", 1},
            {"required_children", json::array()}
};
        child_rooms.push_back(std::move(child_candidate));
        child_layer_changed = true;
    } else {
        json& entry = *child_it;
        int current_min = clamp_candidate_min(entry.value("min_instances", 0));
        int current_max = clamp_candidate_max(current_min, entry.value("max_instances", 1));
        if (entry.value("min_instances", -1) != current_min) {
            entry["min_instances"] = current_min;
            child_layer_changed = true;
        }
        if (entry.value("max_instances", -1) != current_max) {
            entry["max_instances"] = current_max;
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
    auto map_radius_it = map_info_->find("map_radius");
    if (map_radius_it != map_info_->end()) {
        map_info_->erase(map_radius_it);
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
        if (!layer_json.contains("min_rooms")) layer_json["min_rooms"] = 0;
        if (!layer_json.contains("max_rooms")) layer_json["max_rooms"] = 0;
        if (!layer_json.contains("rooms") || !layer_json["rooms"].is_array()) {
            layer_json["rooms"] = json::array();
        }
        auto radius_it = layer_json.find("radius");
        if (radius_it != layer_json.end()) {
            layer_json.erase(radius_it);
        }
        clamp_layer_counts(layer_json);
        auto& rooms = layer_json["rooms"];
        for (auto& candidate : rooms) {
            if (!candidate.is_object()) candidate = json::object();
            if (!candidate.contains("name")) candidate["name"] = "";
            if (!candidate.contains("min_instances")) candidate["min_instances"] = 0;
            if (!candidate.contains("max_instances")) candidate["max_instances"] = 0;
            if (!candidate.contains("required_children") || !candidate["required_children"].is_array()) {
                candidate["required_children"] = json::array();
            }
            int min_inst = clamp_candidate_min(candidate.value("min_instances", 0));
            int max_inst = clamp_candidate_max(min_inst, candidate.value("max_instances", min_inst));
            candidate["min_instances"] = min_inst;
            candidate["max_instances"] = max_inst;
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
    auto it = listeners_.begin();
    while (it != listeners_.end()) {
        if (!it->callback) {
            it = listeners_.erase(it);
            continue;
        }
        it->callback();
        ++it;
    }
}

void MapLayersController::clamp_layer_counts(json& layer) const {
    if (!layer.is_object()) return;

    int min_sum = 0;
    int max_sum = 0;
    const auto rooms_it = layer.find("rooms");
    if (rooms_it != layer.end() && rooms_it->is_array()) {
        for (auto& candidate : *rooms_it) {
            if (!candidate.is_object()) continue;
            int min_inst = clamp_candidate_min(candidate.value("min_instances", 0));
            int max_inst = clamp_candidate_max(min_inst, candidate.value("max_instances", min_inst));
            candidate["min_instances"] = min_inst;
            candidate["max_instances"] = max_inst;
            min_sum += min_inst;
            max_sum += max_inst;
        }
    }

    int derived_min = std::min(min_sum, kDefaultRoomRangeMax);
    int derived_max = std::max(min_sum, max_sum);
    derived_max = std::min(derived_max, kDefaultRoomRangeMax);

    layer["min_rooms"] = derived_min;
    layer["max_rooms"] = derived_max;
}

