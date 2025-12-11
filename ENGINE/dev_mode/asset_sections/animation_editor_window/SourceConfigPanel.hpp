#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <SDL.h>

#include <nlohmann/json.hpp>

class DMButton;
class DMDropdown;
class DMCheckbox;

namespace animation_editor {

class AnimationDocument;
class PreviewProvider;
class AsyncTaskQueue;

using DMCheckbox = ::DMCheckbox;
using DMButton = ::DMButton;
using DMDropdown = ::DMDropdown;

class SourceConfigPanel {
  public:
    SourceConfigPanel();

    enum class SourceMode {
        kFrames = 0,
        kAnimation,
};

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_override_preview_provider(std::shared_ptr<PreviewProvider> provider);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_task_queue(std::shared_ptr<AsyncTaskQueue> tasks);

    using PathPicker = std::function<std::optional<std::filesystem::path>()>;
    using MultiPathPicker = std::function<std::vector<std::filesystem::path>()>;
    using AnimationPicker = std::function<std::optional<std::string>()>;

    void set_folder_picker(PathPicker picker);
    void set_animation_picker(AnimationPicker picker);
    void set_gif_picker(PathPicker picker);
    void set_png_sequence_picker(MultiPathPicker picker);
    void set_status_callback(std::function<void(const std::string&)> callback);

    void set_on_source_changed(std::function<void(const std::string&)> callback) { on_source_changed_ = std::move(callback); }

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    int preferred_height(int width) const;

    bool allow_out_of_bounds_pointer_events() const;

    void commit_animation_dropdown_selection();

    SourceMode source_mode() const;
    void set_source_mode(SourceMode mode);
    bool use_animation_reference() const { return use_animation_reference_; }
    std::vector<std::string> summary_badges() const;

  private:
    struct SourceConfig {
        std::string kind{"folder"};
        std::string path;
        std::optional<std::string> name;
        nlohmann::json extras = nlohmann::json::object();
};

    void reload_from_document();
    void ensure_payload_loaded();
    void commit_payload(bool refresh_document = true);
    void apply_source_config(const SourceConfig& config);
    void update_number_of_frames();
    int compute_frame_count(const SourceConfig& config) const;
    int compute_frame_count_recursive(const SourceConfig& config, std::unordered_set<std::string>& visited) const;
    int count_frames_in_folder(const std::string& relative_path) const;
    std::optional<nlohmann::json> animation_payload(const std::string& id) const;
    SourceConfig parse_source(const nlohmann::json& payload) const;
    nlohmann::json build_source_json(const SourceConfig& config) const;
    bool animation_is_frame_based(const std::string& id) const;
    std::filesystem::path resolve_asset_root() const;
    std::filesystem::path animation_output_directory() const;
    bool prepare_output_directory(std::filesystem::path* out_dir) const;
    bool clean_output_frames() const;
    std::vector<std::filesystem::path> collect_png_files(const std::filesystem::path& folder) const;
    std::vector<std::filesystem::path> normalize_sequence(const std::vector<std::filesystem::path>& files) const;
    void copy_sequence_to_output(const std::vector<std::filesystem::path>& files, const std::filesystem::path& out_dir) const;
    void layout_controls();
    void update_status(const std::string& message) const;
    void refresh_animation_options();
    void apply_animation_selection();
    void clear_derived_fields();
    void render_animation_preview(SDL_Renderer* renderer) const;

    void import_from_folder();
    void import_from_animation();
    void import_from_gif();
    void import_from_png_sequence();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::shared_ptr<AsyncTaskQueue> task_queue_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};

    mutable std::string status_message_;
    std::function<void(const std::string&)> status_callback_;

    PathPicker folder_picker_;
    AnimationPicker animation_picker_;
    PathPicker gif_picker_;
    MultiPathPicker png_sequence_picker_;

    mutable bool payload_loaded_ = false;
    mutable bool reloading_ = false;
    mutable nlohmann::json payload_ = nlohmann::json::object();
    mutable SourceConfig current_source_;
    mutable int frame_count_ = 1;
    mutable std::filesystem::path cached_asset_root_;
    mutable bool cached_asset_root_valid_ = false;

    std::unique_ptr<DMDropdown> animation_dropdown_;
    std::unique_ptr<DMButton> pick_animation_button_;
    std::array<std::unique_ptr<DMButton>, 3> frame_buttons_{};

    SDL_Rect animation_dropdown_rect_{0,0,0,0};
    std::array<SDL_Rect, 3> frame_button_rects_{};

    bool busy_indicator_ = false;
    bool use_animation_reference_ = false;
    std::vector<std::string> animation_options_;
    int animation_index_ = -1;
    std::string animation_ids_signature_;
    std::vector<std::string> previous_animation_options_;
    std::function<void(const std::string&)> on_source_changed_;

    mutable Uint32 animation_start_time_ = 0;
    mutable int current_frame_ = 0;
    mutable bool is_sfa_animation_ = false;
    mutable bool reverse_playback_ = false;
    mutable bool flip_x_ = false;
    mutable bool flip_y_ = false;
};

}
