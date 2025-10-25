#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace devmode::core {
class ManifestStore;
}

class MapLayersController {
public:
    using Listener = std::function<void()>;
    using ListenerId = std::size_t;

    MapLayersController() = default;

    void bind(nlohmann::json* map_info, std::string map_path);
    void set_manifest_store(devmode::core::ManifestStore* store, std::string map_id);

    ListenerId add_listener(Listener cb);
    void remove_listener(ListenerId id);
    void clear_listeners();

    bool save();
    bool reload();

    bool dirty() const { return dirty_; }
    void mark_clean();

    int layer_count() const;
    const nlohmann::json* layer(int index) const;
    nlohmann::json* layer(int index);
    const nlohmann::json& layers() const;
    std::vector<std::string> available_rooms() const;

    double min_edge_distance() const;
    bool set_min_edge_distance(double value);

    int create_layer(const std::string& display_name = {});
    bool delete_layer(int index);
    bool reorder_layer(int from, int to);
    int duplicate_layer(int index);

    bool rename_layer(int index, const std::string& name);

    bool add_candidate(int layer_index, const std::string& room_name);
    bool remove_candidate(int layer_index, int candidate_index);
    bool set_candidate_instance_range(int layer_index, int candidate_index, int min_instances, int max_instances);
    bool set_candidate_instance_count(int layer_index, int candidate_index, int max_instances);
    bool add_candidate_child(int layer_index, int candidate_index, const std::string& child_room);
    bool remove_candidate_child(int layer_index, int candidate_index, const std::string& child_room);

private:
    void ensure_initialized();
    void ensure_layer_indices();
    bool validate_layer_index(int index) const;
    bool validate_candidate_index(const nlohmann::json& layer, int candidate_index) const;
    void notify();
    void clamp_layer_counts(nlohmann::json& layer) const;
    void ensure_spawn_room_data(const std::string& previous_name) const;
    void ensure_map_settings();

private:
    struct ListenerEntry {
        ListenerId id = 0;
        Listener callback;
};

    nlohmann::json* map_info_ = nullptr;
    std::string map_id_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    bool dirty_ = false;
    ListenerId next_listener_id_ = 1;
    std::vector<ListenerEntry> listeners_;
};

