#pragma once

#include <SDL.h>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace animation_editor {

class AnimationDocument {
  public:
    AnimationDocument();

    void load_from_file(const std::filesystem::path& info_path);
    void save_to_file() const;

    void create_animation(const std::string& animation_id);
    void delete_animation(const std::string& animation_id);

    std::vector<std::string> animation_ids() const;
    std::optional<std::string> start_animation() const;
    void set_start_animation(const std::string& animation_id);

    void rename_animation(const std::string& old_id, const std::string& new_id);
    void replace_animation_payload(const std::string& animation_id, const std::string& payload_json);

  private:
    void ensure_document_initialized();
    void rebuild_animation_cache();

  private:
    std::filesystem::path info_path_;
    std::unordered_map<std::string, std::string> animations_;
    std::optional<std::string> start_animation_;
    bool use_nested_container_ = false;
    std::string container_metadata_;
};

}  // namespace animation_editor

