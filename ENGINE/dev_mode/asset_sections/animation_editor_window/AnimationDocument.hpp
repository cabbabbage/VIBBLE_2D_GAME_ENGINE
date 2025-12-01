#pragma once

#include <SDL.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace animation_editor {

class AnimationDocument {
  public:
    AnimationDocument();

    void load_from_file(const std::filesystem::path& info_path);
    void load_from_manifest(const nlohmann::json& asset_json, const std::filesystem::path& asset_root, std::function<void(const nlohmann::json&)> persist_callback);
    void save_to_file() const;

    bool consume_dirty_flag() const;

    void create_animation(const std::string& animation_id);
    void delete_animation(const std::string& animation_id);

    std::vector<std::string> animation_ids() const;
    std::optional<std::string> start_animation() const;
    void set_start_animation(const std::string& animation_id);

    void rename_animation(const std::string& old_id, const std::string& new_id);
    void replace_animation_payload(const std::string& animation_id, const std::string& payload_json);
    std::optional<std::string> animation_payload(const std::string& animation_id) const;
    std::vector<std::string> animation_children() const;
    void replace_animation_children(const std::vector<std::string>& children);
    std::string animation_children_signature() const;

    const std::filesystem::path& info_path() const { return info_path_; }
    const std::filesystem::path& asset_root() const { return asset_root_; }

    void set_on_saved_callback(std::function<void()> callback);

    // Returns the base scale percentage for the asset (default 100.0)
    double scale_percentage() const;

  private:
    void load_from_json_object(const nlohmann::json& root);
    void ensure_document_initialized();
    void rebuild_animation_cache();
    void mark_dirty() const;

  private:
    std::filesystem::path info_path_;
    std::filesystem::path asset_root_;
    std::unordered_map<std::string, std::string> animations_;
    std::optional<std::string> start_animation_;
    bool use_nested_container_ = false;
    std::string container_metadata_;
    mutable bool dirty_ = false;
    mutable nlohmann::json base_data_;
    std::function<void(const nlohmann::json&)> persist_callback_;
    std::function<void()> on_saved_callback_;
};

}

