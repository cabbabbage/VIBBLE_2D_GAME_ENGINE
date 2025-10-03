#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

// Central authority for reading/writing map layer data inside map_info.json.
// Provides validation and mutation helpers that UI code can consume without
// touching JSON directly.
class MapLayersController {
public:
    using Listener = std::function<void()>;

    MapLayersController() = default;

    // Bind to a map_info document and the directory that owns map_info.json.
    void bind(nlohmann::json* map_info, std::string map_path);

    // Observer registration. Callbacks are invoked whenever the controller
    // mutates the document or reloads from disk.
    void add_listener(Listener cb);
    void clear_listeners();

    // Persistence helpers.
    bool save();
    bool reload();

    // Dirty flag to track unsaved changes.
    bool dirty() const { return dirty_; }
    void mark_clean();

    // Basic queries.
    int layer_count() const;
    const nlohmann::json* layer(int index) const;
    nlohmann::json* layer(int index);
    const nlohmann::json& layers() const;
    std::vector<std::string> available_rooms() const;

    // Layer level mutations. Every mutation validates inputs before applying
    // changes and notifies listeners if the document was mutated.
    int create_layer(const std::string& display_name = {});
    bool delete_layer(int index);
    bool reorder_layer(int from, int to);

    bool rename_layer(int index, const std::string& name);
    bool set_layer_radius(int index, int radius);

    // Candidate mutations (rooms array inside a layer)
    bool add_candidate(int layer_index, const std::string& room_name);
    bool remove_candidate(int layer_index, int candidate_index);
    bool set_candidate_instance_count(int layer_index, int candidate_index, int max_instances);
    bool add_candidate_child(int layer_index, int candidate_index, const std::string& child_room);
    bool remove_candidate_child(int layer_index, int candidate_index, const std::string& child_room);

private:
    void ensure_initialized();
    void ensure_layer_indices();
    bool validate_layer_index(int index) const;
    bool validate_candidate_index(const nlohmann::json& layer, int candidate_index) const;
    void notify();
    std::string map_info_path() const;
    void clamp_layer_counts(nlohmann::json& layer) const;

private:
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    bool dirty_ = false;
    std::vector<Listener> listeners_;
};

